#include "Log.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace gf {

QString levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return QStringLiteral("debug");
    case LogLevel::Info: return QStringLiteral("info");
    case LogLevel::Success: return QStringLiteral("success");
    case LogLevel::Warning: return QStringLiteral("warning");
    case LogLevel::Error: return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

LogLevel levelFromName(const QString& name)
{
    const auto n = name.trimmed().toLower();
    if (n == QLatin1String("debug")) return LogLevel::Debug;
    if (n == QLatin1String("success")) return LogLevel::Success;
    if (n == QLatin1String("warning") || n == QLatin1String("warn")) return LogLevel::Warning;
    if (n == QLatin1String("error")) return LogLevel::Error;
    return LogLevel::Info;
}

LogModel::LogModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int LogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row& r = m_rows.at(index.row());
    switch (role) {
    case TimeRole: return r.time.toString(QStringLiteral("HH:mm:ss.zzz"));
    case LevelRole: return int(r.level);
    case LevelNameRole: return levelName(r.level);
    case CategoryRole: return r.category;
    case MessageRole: return r.message;
    case FileRole: return r.file;
    default: return {};
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        {TimeRole, "time"},
        {LevelRole, "level"},
        {LevelNameRole, "levelName"},
        {CategoryRole, "category"},
        {MessageRole, "message"},
        {FileRole, "file"},
    };
}

void LogModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

QString LogModel::toPlainText() const
{
    QString out;
    for (const Row& r : m_rows) {
        out += r.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        out += QStringLiteral(" [") + levelName(r.level) + QStringLiteral("] ");
        out += QStringLiteral("[") + r.category + QStringLiteral("] ");
        out += r.message;
        out += QLatin1Char('\n');
    }
    return out;
}

void LogModel::copyToClipboard() const
{
    if (auto* clipboard = QGuiApplication::clipboard())
        clipboard->setText(toPlainText());
}

void LogModel::append(LogLevel level, const QString& category, const QString& message, const QString& file)
{
    Row row{QDateTime::currentDateTime(), level, category, message, file};
    const int rowIndex = int(m_rows.size());
    beginInsertRows({}, rowIndex, rowIndex);
    m_rows.append(row);
    if (m_rows.size() > kMaxRows) {
        beginRemoveRows({}, 0, 0);
        m_rows.removeFirst();
        endRemoveRows();
    }
    endInsertRows();
    emit appended();
}

Logger::Logger() = default;

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, const QString& category, const QString& message)
{
    if (level < m_minLevel)
        return;
    m_model.append(level, category, message, m_logFile);
    if (!m_logFile.isEmpty()) {
        QFile f(m_logFile);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " [" << levelName(level)
               << "] [" << category << "] " << message << '\n';
        }
    }
    emit messageLogged(int(level), category, message);
}

void Logger::setLogFile(const QString& path)
{
    m_logFile = path;
    QDir().mkpath(QFileInfo(path).absolutePath());
}

} // namespace gf
