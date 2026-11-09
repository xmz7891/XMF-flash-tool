#include "TaskManager.h"

#include "Log.h"

#include <QDateTime>
#include <QHash>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

namespace gf {

namespace {
struct TaskState {
    int id = -1;
    QString title;
    QString toolId;
    std::atomic<bool> canceled{false};
    std::atomic<int> progress{0};
    mutable QMutex mutex;
    QString status;
    QDateTime started;
};
} // namespace

struct TaskManager::Impl {
    QMutex mutex;
    QHash<int, std::shared_ptr<TaskState>> active;
    QList<QVariantMap> recent;
    std::atomic<int> nextId{1};
};

TaskManager::TaskManager()
    : d(std::make_unique<Impl>())
{
}

TaskManager::~TaskManager() = default;

TaskManager& TaskManager::instance()
{
    static TaskManager manager;
    return manager;
}

int TaskManager::submit(const QString& title, const QString& toolId, std::function<void(TaskContext&)> fn)
{
    const int id = d->nextId.fetch_add(1);
    auto state = std::make_shared<TaskState>();
    state->id = id;
    state->title = title;
    state->toolId = toolId;
    state->started = QDateTime::currentDateTime();
    {
        QMutexLocker locker(&d->mutex);
        d->active.insert(id, state);
    }
    emit taskStarted(id, title, toolId);
    emit activeTasksChanged();

    QThreadPool::globalInstance()->start(QRunnable::create([this, id, state, fn = std::move(fn)]() {
        TaskContext ctx(id, state->title, this);
        bool ok = true;
        QString summary;
        try {
            fn(ctx);
            summary = QStringLiteral("任务完成");
        } catch (const TaskContext::TaskCanceled&) {
            ok = false;
            summary = QStringLiteral("已取消");
        } catch (const std::exception& e) {
            ok = false;
            summary = QString::fromUtf8(e.what());
        } catch (...) {
            ok = false;
            summary = QStringLiteral("未知错误");
        }
        if (ok) {
            // only report success if the task was not canceled in the meantime
            ok = !state->canceled.load();
            if (!ok)
                summary = QStringLiteral("已取消");
        }
        QMetaObject::invokeMethod(this, [this, id, ok, summary]() { reportFinished(id, ok, summary); },
                                  Qt::QueuedConnection);
    }));
    return id;
}

void TaskManager::cancel(int id)
{
    std::shared_ptr<TaskState> state;
    {
        QMutexLocker locker(&d->mutex);
        state = d->active.value(id);
    }
    if (!state)
        return;
    state->canceled.store(true);
    reportLog(id, int(LogLevel::Warning), QStringLiteral("正在取消任务…"));
    emit taskProgress(id, state->progress.load(), QStringLiteral("正在取消…"));
}

bool TaskManager::canceled(int id) const
{
    QMutexLocker locker(&d->mutex);
    auto it = d->active.constFind(id);
    return it != d->active.constEnd() && it.value()->canceled.load();
}

void TaskManager::reportProgress(int id, int percent, const QString& status)
{
    int emitPercent = percent;
    {
        QMutexLocker locker(&d->mutex);
        auto it = d->active.find(id);
        if (it == d->active.end())
            return;
        if (percent == -2) {
            // status-only update, keep the current progress value
            emitPercent = it.value()->progress.load();
        } else {
            it.value()->progress.store(percent);
        }
        QMutexLocker slock(&it.value()->mutex);
        it.value()->status = status;
    }
    if (QThread::currentThread() == thread())
        emit taskProgress(id, percent, status);
    else
        QMetaObject::invokeMethod(this, [this, id, percent, status]() { emit taskProgress(id, percent, status); },
                                  Qt::QueuedConnection);
}

void TaskManager::reportLog(int id, int level, const QString& message)
{
    if (QThread::currentThread() == thread())
        emit taskLog(id, level, message);
    else
        QMetaObject::invokeMethod(this, [this, id, level, message]() { emit taskLog(id, level, message); },
                                  Qt::QueuedConnection);
}

void TaskManager::reportFinished(int id, bool ok, const QString& summary)
{
    QVariantMap row;
    {
        QMutexLocker locker(&d->mutex);
        auto it = d->active.find(id);
        if (it == d->active.end())
            return;
        auto state = it.value();
        d->active.erase(it);
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("title"), state->title);
        row.insert(QStringLiteral("toolId"), state->toolId);
        row.insert(QStringLiteral("ok"), ok);
        row.insert(QStringLiteral("summary"), summary);
        row.insert(QStringLiteral("startedAt"), state->started.toString(Qt::ISODate));
        row.insert(QStringLiteral("finishedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
        d->recent.prepend(row);
        while (d->recent.size() > 50)
            d->recent.removeLast();
    }
    emit taskFinished(id, ok, summary);
    emit activeTasksChanged();
    emit recentTasksChanged();
    GF_LOG(QStringLiteral("task"), ok ? LogLevel::Success : LogLevel::Warning,
           QStringLiteral("[%1] %2 — %3").arg(id).arg(row.value(QStringLiteral("title")).toString(), summary));
}

QVariantList TaskManager::activeTasks() const
{
    QVariantList list;
    QMutexLocker locker(&d->mutex);
    for (auto it = d->active.constBegin(); it != d->active.constEnd(); ++it) {
        const auto& s = it.value();
        QVariantMap m;
        m.insert(QStringLiteral("id"), s->id);
        m.insert(QStringLiteral("title"), s->title);
        m.insert(QStringLiteral("toolId"), s->toolId);
        m.insert(QStringLiteral("progress"), s->progress.load());
        {
            QMutexLocker slock(&s->mutex);
            m.insert(QStringLiteral("status"), s->status);
        }
        m.insert(QStringLiteral("startedAt"), s->started.toString(Qt::ISODate));
        list.append(m);
    }
    return list;
}

QVariantList TaskManager::recentTasks() const
{
    QVariantList list;
    QMutexLocker locker(&d->mutex);
    for (const auto& row : d->recent)
        list.append(row);
    return list;
}

int TaskManager::activeCount() const
{
    QMutexLocker locker(&d->mutex);
    return int(d->active.size());
}

void TaskManager::clearRecent()
{
    {
        QMutexLocker locker(&d->mutex);
        if (d->recent.isEmpty())
            return;
        d->recent.clear();
    }
    emit recentTasksChanged();
}

// ---------------------------------------------------------------------------
// TaskContext
// ---------------------------------------------------------------------------

TaskContext::TaskContext(int id, QString title, TaskManager* manager)
    : m_id(id)
    , m_title(std::move(title))
    , m_manager(manager)
{
}

int TaskContext::id() const { return m_id; }
QString TaskContext::title() const { return m_title; }

void TaskContext::setProgress(int percent)
{
    if (m_manager)
        m_manager->reportProgress(m_id, qBound(-1, percent, 100), QString());
}

void TaskContext::setStatus(const QString& text)
{
    if (!m_manager)
        return;
    if (m_manager->canceled(m_id))
        throw TaskCanceled{};
    m_manager->reportProgress(m_id, -2, text); // -2 = keep progress, update status only
}

void TaskContext::log(int level, const QString& message)
{
    if (m_manager)
        m_manager->reportLog(m_id, level, message);
    GF_LOG(QStringLiteral("task"), LogLevel(level), message);
}

void TaskContext::debug(const QString& msg) { log(int(LogLevel::Debug), msg); }
void TaskContext::info(const QString& msg) { log(int(LogLevel::Info), msg); }
void TaskContext::ok(const QString& msg) { log(int(LogLevel::Success), msg); }
void TaskContext::warn(const QString& msg) { log(int(LogLevel::Warning), msg); }
void TaskContext::error(const QString& msg) { log(int(LogLevel::Error), msg); }

void TaskContext::cancel()
{
    if (m_manager)
        m_manager->cancel(m_id);
}

bool TaskContext::isCanceled() const
{
    return m_manager && m_manager->canceled(m_id);
}

void TaskContext::checkCanceled() const
{
    if (isCanceled())
        throw TaskCanceled{};
}

} // namespace gf
