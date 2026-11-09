#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

namespace gf {

/// Small typed wrapper around QSettings. Every key is also reachable from QML
/// through get()/set() so new options do not require C++ changes.
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY accentColorChanged)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool animations READ animations WRITE setAnimations NOTIFY animationsChanged)
    Q_PROPERTY(bool glass READ glass WRITE setGlass NOTIFY glassChanged)
    Q_PROPERTY(bool confirmDangerous READ confirmDangerous WRITE setConfirmDangerous NOTIFY confirmDangerousChanged)
    Q_PROPERTY(QString downloadDir READ downloadDir WRITE setDownloadDir NOTIFY downloadDirChanged)
    Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
    Q_PROPERTY(bool autoRefreshPorts READ autoRefreshPorts WRITE setAutoRefreshPorts NOTIFY autoRefreshPortsChanged)
    Q_PROPERTY(bool disclaimerAccepted READ disclaimerAccepted WRITE setDisclaimerAccepted NOTIFY disclaimerAcceptedChanged)

public:
    explicit Settings(QObject* parent = nullptr);

    Q_INVOKABLE QVariant get(const QString& key, const QVariant& fallback = {}) const;
    Q_INVOKABLE void set(const QString& key, const QVariant& value);

    QString accentColor() const;
    void setAccentColor(const QString& value);
    /// "dark" | "light" | "auto" (follow the Windows app theme).
    QString themeMode() const;
    void setThemeMode(const QString& value);
    bool animations() const;
    void setAnimations(bool value);
    bool glass() const;
    void setGlass(bool value);
    bool confirmDangerous() const;
    void setConfirmDangerous(bool value);
    QString downloadDir() const;
    void setDownloadDir(const QString& value);
    QString logLevel() const;
    void setLogLevel(const QString& value);
    bool autoRefreshPorts() const;
    void setAutoRefreshPorts(bool value);
    bool disclaimerAccepted() const;
    void setDisclaimerAccepted(bool value);

signals:
    void accentColorChanged();
    void themeModeChanged();
    void animationsChanged();
    void glassChanged();
    void confirmDangerousChanged();
    void downloadDirChanged();
    void logLevelChanged();
    void autoRefreshPortsChanged();
    void disclaimerAcceptedChanged();
    void changed(const QString& key);

private:
    QVariant raw(const QString& key, const QVariant& fallback) const;
    void put(const QString& key, const QVariant& value, const char* notifySignal = nullptr);

    mutable QSettings m_settings;
};

} // namespace gf
