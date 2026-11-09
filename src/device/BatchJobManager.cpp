#include "BatchJobManager.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Util.h"

namespace gf::device {

/// Shared between the manager and one job execution. Declared in the header so
/// BatchJobContext can keep a shared_ptr to it without exposing internals.
struct BatchJobRuntime {
    QString jobId;
    QString device;
    QString label;
    std::atomic<int> progress{0};
    std::atomic<bool> canceled{false};
    std::function<void(const QString& message, bool isError)> log;
};

BatchJobContext::BatchJobContext(std::shared_ptr<BatchJobRuntime> runtime, int attempt)
    : d(std::move(runtime)), m_attempt(attempt)
{
}

BatchJobContext::~BatchJobContext() = default;

QString BatchJobContext::jobId() const
{
    return d ? d->jobId : QString();
}

QString BatchJobContext::device() const
{
    return d ? d->device : QString();
}

QString BatchJobContext::label() const
{
    return d ? d->label : QString();
}

int BatchJobContext::attempt() const
{
    return m_attempt;
}

void BatchJobContext::setProgress(int percent)
{
    if (!d)
        return;
    d->progress.store(qBound(0, percent, 100), std::memory_order_relaxed);
}

int BatchJobContext::progress() const
{
    return d ? d->progress.load(std::memory_order_relaxed) : 0;
}

bool BatchJobContext::isCanceled() const
{
    return d && d->canceled.load(std::memory_order_relaxed);
}

void BatchJobContext::checkCanceled() const
{
    if (isCanceled())
        throw BatchJobCanceled{};
}

void BatchJobContext::log(const QString& message) const
{
    if (d && d->log)
        d->log(message, false);
}

QString BatchSummary::toString() const
{
    return QStringLiteral("批量任务完成：共 %1 个，成功 %2，失败 %3，取消 %4，尝试 %5 次")
        .arg(total)
        .arg(succeeded)
        .arg(failed)
        .arg(canceled)
        .arg(attempts);
}

struct BatchJobManager::Impl {
    enum class State { Pending, Running, Succeeded, Failed, Canceled };

    struct JobState {
        BatchJob job;
        std::shared_ptr<BatchJobRuntime> runtime;
        State state = State::Pending;
        int attempts = 0;
        QString lastError;
    };

    explicit Impl(int limit)
        : parallelLimit(qMax(1, limit))
    {
    }

    int parallelLimit = 2;
    std::vector<std::shared_ptr<JobState>> jobs;
    QSet<QString> busyDevices;
    int running = 0;
    std::atomic<bool> cancelRequested{false};
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::thread> threads;
    ProgressCallback progress;
    LogCallback log;

    bool canceled() const { return cancelRequested.load(std::memory_order_relaxed); }
};

BatchJobManager::BatchJobManager(int parallelLimit)
    : d(std::make_unique<Impl>(parallelLimit))
{
}

BatchJobManager::~BatchJobManager()
{
    for (auto& thread : d->threads) {
        if (thread.joinable())
            thread.join();
    }
}

void BatchJobManager::setParallelLimit(int limit)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->parallelLimit = qMax(1, limit);
}

int BatchJobManager::parallelLimit() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->parallelLimit;
}

void BatchJobManager::addJob(BatchJob job)
{
    if (!job.run)
        return;
    if (job.maxAttempts < 1)
        job.maxAttempts = 1;
    auto state = std::make_shared<Impl::JobState>();
    state->job = std::move(job);
    state->runtime = std::make_shared<BatchJobRuntime>();
    state->runtime->jobId = state->job.id;
    state->runtime->device = state->job.device;
    state->runtime->label = state->job.label;
    std::lock_guard<std::mutex> lock(d->mutex);
    d->jobs.push_back(std::move(state));
}

int BatchJobManager::jobCount() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return int(d->jobs.size());
}

QList<BatchJob> BatchJobManager::jobs() const
{
    QList<BatchJob> out;
    std::lock_guard<std::mutex> lock(d->mutex);
    for (const auto& state : d->jobs)
        out.append(state->job);
    return out;
}

void BatchJobManager::clear()
{
    std::lock_guard<std::mutex> lock(d->mutex);
    if (d->running > 0)
        return;
    d->jobs.clear();
    d->busyDevices.clear();
    d->cancelRequested.store(false, std::memory_order_relaxed);
}

void BatchJobManager::setProgressCallback(ProgressCallback callback)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->progress = std::move(callback);
}

void BatchJobManager::setLogCallback(LogCallback callback)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->log = std::move(callback);
}

void BatchJobManager::requestCancel()
{
    d->cancelRequested.store(true, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(d->mutex);
    d->cv.notify_all();
}

bool BatchJobManager::isCanceled() const
{
    return d->cancelRequested.load(std::memory_order_relaxed);
}

BatchSummary BatchJobManager::run()
{
    Impl& impl = *d;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        for (auto& state : impl.jobs) {
            state->state = Impl::State::Pending;
            state->attempts = 0;
            state->runtime->progress.store(0, std::memory_order_relaxed);
            state->runtime->canceled.store(false, std::memory_order_relaxed);
            state->runtime->log = [&impl](const QString& message, bool isError) {
                BatchJobManager::LogCallback callback;
                {
                    std::lock_guard<std::mutex> lock(impl.mutex);
                    callback = impl.log;
                }
                if (callback)
                    callback(message, isError);
            };
        }
    }

    auto launch = [&impl](const std::shared_ptr<Impl::JobState>& state) {
        state->state = Impl::State::Running;
        ++state->attempts;
        state->runtime->progress.store(0, std::memory_order_relaxed);
        state->runtime->canceled.store(false, std::memory_order_relaxed);
        if (!state->job.device.isEmpty())
            impl.busyDevices.insert(state->job.device);
        ++impl.running;
        impl.threads.emplace_back([&impl, state]() {
            bool ok = false;
            QString error;
            try {
                BatchJobContext context(state->runtime, state->attempts);
                ok = state->job.run ? state->job.run(context) : false;
            } catch (const BatchJobContext::BatchJobCanceled&) {
                state->runtime->canceled.store(true, std::memory_order_relaxed);
                ok = false;
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
                ok = false;
            } catch (...) {
                error = QStringLiteral("未知异常");
                ok = false;
            }

            std::lock_guard<std::mutex> lock(impl.mutex);
            if (!state->job.device.isEmpty())
                impl.busyDevices.remove(state->job.device);
            --impl.running;
            const bool canceled = impl.canceled()
                                  || state->runtime->canceled.load(std::memory_order_relaxed);
            if (ok) {
                state->runtime->progress.store(100, std::memory_order_relaxed);
                state->state = Impl::State::Succeeded;
            } else if (canceled) {
                state->state = Impl::State::Canceled;
            } else if (state->attempts < state->job.maxAttempts) {
                state->state = Impl::State::Pending;
                state->lastError = error;
            } else {
                state->state = Impl::State::Failed;
                state->lastError = error;
            }
            impl.cv.notify_all();
        });
    };

    for (;;) {
        int percent = 0;
        QString statusText;
        ProgressCallback progressCallback;
        {
            std::unique_lock<std::mutex> lock(impl.mutex);
            if (impl.canceled()) {
                for (auto& state : impl.jobs) {
                    state->runtime->canceled.store(true, std::memory_order_relaxed);
                    if (state->state == Impl::State::Pending)
                        state->state = Impl::State::Canceled;
                }
            }

            bool launched = true;
            while (launched && impl.running < impl.parallelLimit) {
                launched = false;
                for (auto& state : impl.jobs) {
                    if (impl.running >= impl.parallelLimit)
                        break;
                    if (state->state != Impl::State::Pending)
                        continue;
                    if (!state->job.device.isEmpty() && impl.busyDevices.contains(state->job.device))
                        continue;
                    launch(state);
                    launched = true;
                }
            }

            bool pending = false;
            for (const auto& state : impl.jobs) {
                if (state->state == Impl::State::Pending)
                    pending = true;
            }
            if (impl.running == 0 && !pending)
                break;

            int completed = 0;
            for (const auto& state : impl.jobs) {
                if (state->state == Impl::State::Succeeded || state->state == Impl::State::Failed
                    || state->state == Impl::State::Canceled)
                    ++completed;
            }
            const int total = int(impl.jobs.size());
            percent = total > 0 ? completed * 100 / total : 100;
            statusText = QStringLiteral("批量任务：已完成 %1/%2").arg(completed).arg(total);
            if (impl.canceled())
                statusText += QStringLiteral("（正在取消）");
            progressCallback = impl.progress;
        }

        if (progressCallback)
            progressCallback(percent, statusText);
        std::unique_lock<std::mutex> lock(impl.mutex);
        impl.cv.wait_for(lock, std::chrono::milliseconds(50));
    }

    for (auto& thread : impl.threads) {
        if (thread.joinable())
            thread.join();
    }
    impl.threads.clear();

    BatchSummary summary;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        summary.total = int(impl.jobs.size());
        for (const auto& state : impl.jobs) {
            summary.attempts += state->attempts;
            switch (state->state) {
            case Impl::State::Succeeded:
                ++summary.succeeded;
                break;
            case Impl::State::Failed:
                ++summary.failed;
                summary.failedJobs.append(
                    state->job.label.isEmpty() ? state->job.id : state->job.label);
                break;
            case Impl::State::Canceled:
            case Impl::State::Pending:
                ++summary.canceled;
                break;
            case Impl::State::Running:
                break;
            }
        }
        impl.jobs.clear();
        impl.busyDevices.clear();
        impl.cancelRequested.store(false, std::memory_order_relaxed);
    }
    ProgressCallback finalCallback;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        finalCallback = impl.progress;
    }
    if (finalCallback)
        finalCallback(100, summary.toString());
    return summary;
}

// ---------------------------------------------------------------------------
// Batch helpers
// ---------------------------------------------------------------------------

QList<HashManifestEntry> parseHashManifest(const QString& text)
{
    QList<HashManifestEntry> entries;
    static const QRegularExpression gnuFormat(
        QStringLiteral("^([A-Za-z0-9-]+)\\s+\\((.+)\\)\\s*=\\s*([0-9A-Fa-f]+)\\s*$"));
    static const QRegularExpression sumFormat(
        QStringLiteral("^([0-9A-Fa-f]{8,128})\\s+\\*?(.+?)\\s*$"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        QRegularExpressionMatch match = gnuFormat.match(line);
        if (match.hasMatch()) {
            HashManifestEntry entry;
            entry.hash = match.captured(3);
            entry.path = match.captured(2).trimmed();
            if (!entry.hash.isEmpty() && !entry.path.isEmpty())
                entries.append(entry);
            continue;
        }
        match = sumFormat.match(line);
        if (match.hasMatch()) {
            HashManifestEntry entry;
            entry.hash = match.captured(1);
            entry.path = match.captured(2).trimmed();
            if (!entry.hash.isEmpty() && !entry.path.isEmpty())
                entries.append(entry);
        }
    }
    return entries;
}

QString expandRenamePattern(const QString& pattern, int index, const QString& fileName)
{
    const QFileInfo info(fileName);
    QString out = pattern;
    if (out.trimmed().isEmpty())
        return fileName;

    out.replace(QStringLiteral("{index3}"), QString::number(index).rightJustified(3, QLatin1Char('0')));
    out.replace(QStringLiteral("{index2}"), QString::number(index).rightJustified(2, QLatin1Char('0')));
    out.replace(QStringLiteral("{index}"), QString::number(index));
    out.replace(QStringLiteral("{name}"), info.completeBaseName());
    out.replace(QStringLiteral("{ext}"), info.suffix());
    out.replace(QStringLiteral("{filename}"), info.fileName());

    const bool referencesExtension = pattern.contains(QStringLiteral("{ext}"))
                                     || pattern.contains(QStringLiteral("{filename}"));
    if (!referencesExtension && !info.suffix().isEmpty()
        && !out.endsWith(QLatin1Char('.') + info.suffix(), Qt::CaseInsensitive)) {
        out += QLatin1Char('.') + info.suffix();
    }

    static const QRegularExpression illegal(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]"));
    out.replace(illegal, QStringLiteral("_"));
    out = out.trimmed();
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    if (out.isEmpty())
        return fileName;
    return out;
}

bool verifyFileHash(const QString& path, const QString& expectedHash,
                    const QString& algorithm, QString* actualHash, QString* error)
{
    const QString algo = algorithm.trimmed().toLower();
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("文件不存在：%1").arg(path);
        return false;
    }
    QString actual;
    if (algo.isEmpty() || algo == QLatin1String("sha256")) {
        actual = util::sha256File(path);
    } else if (algo == QLatin1String("md5")) {
        actual = util::md5File(path);
    } else {
        if (error)
            *error = QStringLiteral("不支持的哈希算法：%1（仅支持 sha256 / md5）").arg(algorithm);
        return false;
    }
    if (actual.isEmpty()) {
        if (error)
            *error = QStringLiteral("读取文件失败：%1").arg(path);
        return false;
    }
    if (actualHash)
        *actualHash = actual;
    if (actual.compare(expectedHash.trimmed(), Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("哈希不匹配：期望 %1，实际 %2").arg(expectedHash, actual);
        return false;
    }
    if (error)
        error->clear();
    return true;
}

} // namespace gf::device
