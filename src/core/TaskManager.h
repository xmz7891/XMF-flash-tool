#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>

namespace gf {

class TaskManager;

/// Context handed to every background task. Thread-safe.
/// All UI updates are marshalled back to the GUI thread by TaskManager.
class TaskContext {
public:
    TaskContext(int id, QString title, TaskManager* manager);

    int id() const;
    QString title() const;

    /// 0..100, or -1 for indeterminate.
    void setProgress(int percent);
    void setStatus(const QString& text);
    void log(int level, const QString& message);
    void debug(const QString& msg);
    void info(const QString& msg);
    void ok(const QString& msg);
    void warn(const QString& msg);
    void error(const QString& msg);

    void cancel();
    bool isCanceled() const;
    /// Throws TaskCanceled when the user pressed cancel.
    void checkCanceled() const;

    class TaskCanceled {};

private:
    friend class TaskManager;
    int m_id = -1;
    QString m_title;
    TaskManager* m_manager = nullptr;
};

class TaskManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList activeTasks READ activeTasks NOTIFY activeTasksChanged)
    Q_PROPERTY(QVariantList recentTasks READ recentTasks NOTIFY recentTasksChanged)

public:
    static TaskManager& instance();

    ~TaskManager() override;
    int submit(const QString& title, const QString& toolId, std::function<void(TaskContext&)> fn);

    Q_INVOKABLE void cancel(int id);
    Q_INVOKABLE void clearRecent();
    QVariantList activeTasks() const;
    QVariantList recentTasks() const;
    Q_INVOKABLE int activeCount() const;

    // Internal, called from TaskContext (any thread).
    void reportProgress(int id, int percent, const QString& status);
    void reportLog(int id, int level, const QString& message);
    void reportFinished(int id, bool ok, const QString& summary);
    bool canceled(int id) const;

signals:
    void taskStarted(int id, const QString& title, const QString& toolId);
    void taskProgress(int id, int percent, const QString& status);
    void taskLog(int id, int level, const QString& message);
    void taskFinished(int id, bool ok, const QString& summary);
    void activeTasksChanged();
    void recentTasksChanged();

private:
    TaskManager();
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace gf
