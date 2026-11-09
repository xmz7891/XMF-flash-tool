#pragma once

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "Log.h"
#include "Settings.h"
#include "FirmwareBrowser.h"
#include "FirmwareWorkspace.h"
#include "EmbeddedApp.h"
#include "OfficialTools.h"
#include "device/PortsModel.h"
#include "packets/PacketStore.h"

namespace gf {

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(gf::LogModel* logs READ logs CONSTANT)
    Q_PROPERTY(gf::Settings* settings READ settings CONSTANT)
    Q_PROPERTY(gf::packets::PacketStore* packets READ packets CONSTANT)
    Q_PROPERTY(gf::device::PortsModel* ports READ ports CONSTANT)
    Q_PROPERTY(gf::FirmwareWorkspace* workspace READ workspace CONSTANT)
    Q_PROPERTY(gf::FirmwareBrowser* firmware READ firmware CONSTANT)
    Q_PROPERTY(QVariantMap batchState READ batchState NOTIFY batchStateChanged)
    Q_PROPERTY(gf::OfficialTools* officialTools READ officialTools CONSTANT)
    Q_PROPERTY(gf::EmbeddedApp* embedded READ embedded CONSTANT)
    Q_PROPERTY(QVariantList adbDevices READ adbDevices NOTIFY adbDevicesChanged)
    Q_PROPERTY(QVariantList fastbootDevices READ fastbootDevices NOTIFY adbDevicesChanged)
    Q_PROPERTY(QVariantList flashPorts READ flashPorts NOTIFY flashPortsChanged)
    Q_PROPERTY(bool adbAvailable READ adbAvailable NOTIFY adbDevicesChanged)
    Q_PROPERTY(QVariantMap deviceInfo READ deviceInfo NOTIFY deviceInfoChanged)
    Q_PROPERTY(QVariantList tools READ tools NOTIFY toolsChanged)
    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    Q_PROPERTY(QVariantList activeTasks READ activeTasks NOTIFY tasksChanged)
    Q_PROPERTY(QVariantList recentTasks READ recentTasks NOTIFY tasksChanged)
    Q_PROPERTY(QString productName READ productName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString dataPath READ dataPath CONSTANT)
    Q_PROPERTY(bool admin READ isAdmin CONSTANT)
    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY darkThemeChanged)
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY darkThemeChanged)
    Q_PROPERTY(bool disclaimerAccepted READ disclaimerAccepted NOTIFY disclaimerAcceptedChanged)
    Q_PROPERTY(QVariantMap deviceMode READ deviceMode NOTIFY deviceModeChanged)
    Q_PROPERTY(QString guideFile READ guideFile WRITE setGuideFile NOTIFY guideFileChanged)

public:
    static AppController& instance();

    void initialize();

    LogModel* logs() { return Logger::instance().model(); }
    Settings* settings() { return &m_settings; }
    packets::PacketStore* packets() { return &packets::PacketStore::instance(); }
    device::PortsModel* ports() { return &m_ports; }
    FirmwareWorkspace* workspace() { return &m_workspace; }
    FirmwareBrowser* firmware() { return &m_firmware; }
    QVariantMap batchState() const { return m_batchState; }
    OfficialTools* officialTools() { return &m_officialTools; }
    EmbeddedApp* embedded() { return &m_embedded; }
    QVariantList adbDevices() const { return m_adbDevices; }
    QVariantList fastbootDevices() const { return m_fastbootDevices; }
    QVariantList flashPorts() const;
    QVariantMap deviceInfo() const { return m_deviceInfo; }
    bool adbAvailable() const;
    QVariantList tools() const;
    QVariantList categories() const;
    QVariantList activeTasks() const;
    QVariantList recentTasks() const;
    QString productName() const { return QStringLiteral("XMF Flash Tool"); }
    QString version() const;
    QString dataPath() const;
    bool isAdmin() const;
    bool darkTheme() const;
    bool systemDark() const;
    bool disclaimerAccepted() const;
    /// Detected current device mode: fastboot / sideload / edl9008 / mtk-brom /
    /// sprd-fdl / rk-maskrom / aw-fel / adb / none. Includes label + platform.
    QVariantMap deviceMode() const;
    /// Currently selected deep-flash guide/loader file (local or from a packet).
    QString guideFile() const;
    void setGuideFile(const QString& path);

    Q_INVOKABLE int runTool(const QString& id, const QVariantMap& options);
    Q_INVOKABLE QVariantMap toolInfo(const QString& id) const;
    Q_INVOKABLE void cancelTask(int id);
    /// Cancels every running task (used when closing with active work).
    Q_INVOKABLE void cancelAllTasks();
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void notify(const QString& type, const QString& message);
    /// Records that the user accepted the first-run disclaimer (persisted).
    Q_INVOKABLE void acceptDisclaimer();
    /// Headless/test runs skip the disclaimer without touching the saved setting.
    Q_INVOKABLE void skipDisclaimerForAutomation();
    /// Forces the disclaimer to show again (used with --preview-disclaimer).
    Q_INVOKABLE void previewDisclaimer();
    /// Recomputes the detected device mode and emits deviceModeChanged when it changes.
    Q_INVOKABLE void refreshDeviceMode();
    /// Extracts a guide file (Loader/DA/FDL/Preloader) from a packet for a model.
    Q_INVOKABLE QString pickGuideFromPacket(const QString& packetId, const QString& model,
                                            const QString& attribute);

    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    Q_INVOKABLE QString formatDuration(qint64 ms) const;
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE void openUrl(const QString& url) const;
    Q_INVOKABLE void openPath(const QString& path) const;
    /// Opens a bundled documentation file (USER_GUIDE.md, CHANGELOG.md, ...).
    /// Returns the resolved path, or an empty string when missing.
    Q_INVOKABLE QString openDocs(const QString& fileName) const;
    /// Runs one adb shell command synchronously for the console page.
    Q_INVOKABLE QVariantMap runAdbCommand(const QString& serial, const QString& command);
    /// Runs one fastboot command synchronously for the console page.
    Q_INVOKABLE QVariantMap runFastbootCommand(const QString& serial, const QString& arguments);
    Q_INVOKABLE void revealPath(const QString& path) const;

    Q_INVOKABLE void scanPorts();
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE QVariantList driverStatus();
    Q_INVOKABLE QString driverSummaryText();
    Q_INVOKABLE bool installDrivers();
    /// Queue one tool run per file (options[pathField] = file). Returns the
    /// number of queued items. Progress is reported through batchState.
    Q_INVOKABLE int startBatch(const QString& toolId, const QString& pathField,
                               const QVariantList& files, const QVariantMap& baseOptions,
                               int parallel = 2);
    Q_INVOKABLE void cancelBatch();
    /// Writes the visible log to a UTF-8 text file; returns the path (empty on error).
    Q_INVOKABLE QString saveLogs(const QString& path);
    /// Writes an environment/diagnostics report to the log directory.
    Q_INVOKABLE QString writeDiagnostics();
    /// Size of the packet extraction cache in bytes.
    Q_INVOKABLE qint64 cacheSize() const;
    /// Clears the packet extraction cache; returns true on success.
    Q_INVOKABLE bool clearCache();
    /// File list helper for batch pages (e.g. all *.img in a folder).
    Q_INVOKABLE QVariantList filesInDirectory(const QString& dir, const QString& filterCsv) const;

signals:
    void batchStateChanged();
    void toolsChanged();
    void tasksChanged();
    void adbDevicesChanged();
    void flashPortsChanged();
    void deviceInfoChanged();
    void darkThemeChanged();
    void disclaimerAcceptedChanged();
    void deviceModeChanged();
    void guideFileChanged();
    void notifyRequested(const QString& type, const QString& message);

private:
    AppController();
    Settings m_settings;
    bool m_disclaimerAccepted = false;
    QVariantMap m_deviceMode;
    QString m_guideFile;
    device::PortsModel m_ports;
    FirmwareWorkspace m_workspace;
    FirmwareBrowser m_firmware;
    OfficialTools m_officialTools;
    EmbeddedApp m_embedded;
    QVariantList m_adbDevices;
    QVariantList m_fastbootDevices;
    QVariantMap m_deviceInfo;
    QVariantList m_driverStatus;
    bool m_driverStatusLoaded = false;
    bool m_refreshingDevices = false;
    bool m_initialized = false;

    struct BatchEntry {
        QString label;
        QVariantMap options;
    };
    QVariantMap m_batchState;
    QList<BatchEntry> m_batchQueue;
    QString m_batchToolId;
    QHash<int, QString> m_batchActiveTasks;
    int m_batchParallel = 2;
    int m_batchTotal = 0;
    int m_batchDone = 0;
    int m_batchFailed = 0;
    bool m_batchActive = false;
    void batchAdvance();
    void updateBatchState(const QString& message, bool appendLog = true);
};

} // namespace gf

