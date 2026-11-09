#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

namespace gf {

enum class LogLevel { Debug, Info, Success, Warning, Error };

class LogModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        TimeRole = Qt::UserRole + 1,
        LevelRole,
        LevelNameRole,
        CategoryRole,
        MessageRole,
        FileRole,
    };
    explicit LogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    Q_INVOKABLE void copyToClipboard() const;
    Q_INVOKABLE QString toPlainText() const;

    void append(LogLevel level, const QString& category, const QString& message, const QString& file = {});

signals:
    void appended();

private:
    struct Row {
        QDateTime time;
        LogLevel level;
        QString category;
        QString message;
        QString file;
    };
    QList<Row> m_rows;
    static constexpr int kMaxRows = 20000;
};

// Application wide logging facade. Thread safe.
class Logger : public QObject {
    Q_OBJECT
public:
    static Logger& instance();

    LogModel* model() { return &m_model; }

    void log(LogLevel level, const QString& category, const QString& message);
    void setLogFile(const QString& path);
    void setMinLevel(LogLevel level) { m_minLevel = level; }
    LogLevel minLevel() const { return m_minLevel; }

signals:
    void messageLogged(int level, const QString& category, const QString& message);

private:
    Logger();
    LogModel m_model;
    LogLevel m_minLevel = LogLevel::Debug;
    QString m_logFile;
};

QString levelName(LogLevel level);
LogLevel levelFromName(const QString& name);

} // namespace gf

#define GF_LOG(category, level, message) ::gf::Logger::instance().log(level, category, message)
#define GF_DEBUG(category, message) GF_LOG(category, ::gf::LogLevel::Debug, message)
#define GF_INFO(category, message) GF_LOG(category, ::gf::LogLevel::Info, message)
#define GF_OK(category, message) GF_LOG(category, ::gf::LogLevel::Success, message)
#define GF_WARN(category, message) GF_LOG(category, ::gf::LogLevel::Warning, message)
#define GF_ERROR(category, message) GF_LOG(category, ::gf::LogLevel::Error, message)
