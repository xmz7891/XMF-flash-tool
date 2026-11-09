#include "Settings.h"

#include <QDir>
#include <QStandardPaths>

namespace gf {

Settings::Settings(QObject* parent)
    : QObject(parent)
    , m_settings(QStringLiteral("XMF"), QStringLiteral("XMFTool"))
{
}

QVariant Settings::raw(const QString& key, const QVariant& fallback) const
{
    return m_settings.value(key, fallback);
}

QVariant Settings::get(const QString& key, const QVariant& fallback) const
{
    return raw(key, fallback);
}

void Settings::set(const QString& key, const QVariant& value)
{
    put(key, value);
}

void Settings::put(const QString& key, const QVariant& value, const char* notifySignal)
{
    if (m_settings.value(key) == value)
        return;
    m_settings.setValue(key, value);
    m_settings.sync();
    if (notifySignal)
        QMetaObject::invokeMethod(this, notifySignal);
    emit changed(key);
}

QString Settings::accentColor() const
{
    return raw(QStringLiteral("ui/accent"), QStringLiteral("#5B8CFF")).toString();
}

void Settings::setAccentColor(const QString& value)
{
    put(QStringLiteral("ui/accent"), value, "accentColorChanged");
}

QString Settings::themeMode() const
{
    return raw(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString();
}

void Settings::setThemeMode(const QString& value)
{
    put(QStringLiteral("ui/theme"), value, "themeModeChanged");
}

bool Settings::animations() const
{
    return raw(QStringLiteral("ui/animations"), true).toBool();
}

void Settings::setAnimations(bool value)
{
    put(QStringLiteral("ui/animations"), value, "animationsChanged");
}

bool Settings::glass() const
{
    return raw(QStringLiteral("ui/glass"), true).toBool();
}

void Settings::setGlass(bool value)
{
    put(QStringLiteral("ui/glass"), value, "glassChanged");
}

bool Settings::confirmDangerous() const
{
    return raw(QStringLiteral("flash/confirmDangerous"), true).toBool();
}

void Settings::setConfirmDangerous(bool value)
{
    put(QStringLiteral("flash/confirmDangerous"), value, "confirmDangerousChanged");
}

QString Settings::downloadDir() const
{
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                             + QStringLiteral("/XMF");
    return raw(QStringLiteral("paths/downloads"), fallback).toString();
}

void Settings::setDownloadDir(const QString& value)
{
    put(QStringLiteral("paths/downloads"), value, "downloadDirChanged");
}

QString Settings::logLevel() const
{
    return raw(QStringLiteral("log/level"), QStringLiteral("info")).toString();
}

void Settings::setLogLevel(const QString& value)
{
    put(QStringLiteral("log/level"), value, "logLevelChanged");
}

bool Settings::autoRefreshPorts() const
{
    return raw(QStringLiteral("usb/autoRefresh"), true).toBool();
}

void Settings::setAutoRefreshPorts(bool value)
{
    put(QStringLiteral("usb/autoRefresh"), value, "autoRefreshPortsChanged");
}

bool Settings::disclaimerAccepted() const
{
    return raw(QStringLiteral("ui/disclaimerAccepted"), false).toBool();
}

void Settings::setDisclaimerAccepted(bool value)
{
    put(QStringLiteral("ui/disclaimerAccepted"), value, "disclaimerAcceptedChanged");
}

} // namespace gf
