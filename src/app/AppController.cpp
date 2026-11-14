#include "AppController.h"

#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QFileInfo>

#include "NativeWindow.h"
#include "ToolModules.h"
#include "ToolRegistry.h"
#include "Util.h"
#include "device/AdbClient.h"
#include "device/FastbootClient.h"
#include "device/DriverManager.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QMetaEnum>
#include <QStyleHints>
#include <QRunnable>
#include <QThreadPool>

namespace gf {

AppController& AppController::instance()
{
    static AppController controller;
    return controller;
}

AppController::AppController()
{
    auto& tm = TaskManager::instance();
    connect(&tm, &TaskManager::activeTasksChanged, this, &AppController::tasksChanged);
    connect(&tm, &TaskManager::recentTasksChanged, this, &AppController::tasksChanged);
    connect(&tm, &TaskManager::taskFinished, this, [this](int, bool ok, const QString& summary) {
        emit notifyRequested(ok ? QStringLiteral("success") : QStringLiteral("warning"), summary);
    });
    connect(&tm, &TaskManager::taskLog, this, [](int, int level, const QString& message) {
        if (level >= int(LogLevel::Warning))
            GF_LOG(QStringLiteral("tool"), LogLevel(level), message);
    });

    connect(&m_ports, &device::PortsModel::countChanged, this, &AppController::flashPortsChanged);
    connect(&m_ports, &device::PortsModel::refreshed, this, &AppController::flashPortsChanged);
    connect(&m_settings, &Settings::logLevelChanged, this, [this]() {
        Logger::instance().setMinLevel(levelFromName(m_settings.logLevel()));
    });
    connect(&m_settings, &Settings::changed, this, [this](const QString& key) {
        if (key == QLatin1String("ui/theme"))
            emit darkThemeChanged();
    });
    if (auto* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() { emit darkThemeChanged(); });
    }
}

bool AppController::systemDark() const
{
    if (auto* hints = QGuiApplication::styleHints())
        return hints->colorScheme() == Qt::ColorScheme::Dark;
    return true;
}

bool AppController::darkTheme() const
{
    const QString mode = m_settings.get(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString();
    if (mode == QLatin1String("light"))
        return false;
    if (mode == QLatin1String("auto"))
        return systemDark();
    return true;
}

void AppController::initialize()
{
    m_disclaimerAccepted = m_settings.disclaimerAccepted();
    m_guideFile = m_settings.get(QStringLiteral("flash/guideFile"), QString()).toString();
    if (m_initialized)
        return;
    m_initialized = true;

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    Logger::instance().setLogFile(util::logsDir() + QStringLiteral("/xmf-%1.log").arg(stamp));
    Logger::instance().setMinLevel(levelFromName(m_settings.logLevel()));

    packets::PacketStore::instance().initialize();

    registerSystemTools();
    registerImageTools();
    registerPacketTools();
    registerDeviceTools();
    registerQcomTools();
    registerMtkTools();
    registerSprdTools();
    registerRkTools();
    registerAwTools();
    registerSamsungTools();
    registerFlowTools();
    registerRootTools();
    registerBootloaderTools();
    registerUnlockTools();
    registerFirmwareTools();
    registerBatchTools();
    registerBypassTools();

    GF_INFO(QStringLiteral("app"), QStringLiteral("XMF Flash Tool %1 started (admin=%2)")
                                        .arg(version(), isAdmin() ? QStringLiteral("yes") : QStringLiteral("no")));
    GF_INFO(QStringLiteral("device"), QStringLiteral("检测到 %1 个端口，ADB %2")
                                          .arg(m_ports.count())
                                          .arg(device::AdbClient::available() ? QStringLiteral("可用")
                                                                              : QStringLiteral("未找到")));
    connect(&TaskManager::instance(), &TaskManager::taskFinished, this,
            [this](int id, bool ok, const QString& summary) {
                if (!m_batchActive || !m_batchActiveTasks.contains(id))
                    return;
                m_batchActiveTasks.remove(id);
                ++m_batchDone;
                if (!ok) {
                    ++m_batchFailed;
                    const QString label = m_batchState.value(QStringLiteral("current")).toString();
                    updateBatchState(QStringLiteral("%1 失败：%2").arg(label, summary));
                } else {
                    updateBatchState(QStringLiteral("%1 完成").arg(summary));
                }
                batchAdvance();
            });
    refreshDevices();
    emit toolsChanged();
}

bool AppController::adbAvailable() const
{
    return device::AdbClient::available();
}

void AppController::scanPorts()
{
    m_ports.refresh();
    refreshDeviceMode();
    emit adbDevicesChanged();
}

void AppController::refreshDevices()
{
    if (m_refreshingDevices)
        return;
    m_refreshingDevices = true;
    QThreadPool::globalInstance()->start(QRunnable::create([this]() {
        QString error;
        const auto devices = device::AdbClient::devices(&error);
        QVariantList list;
        list.reserve(devices.size());
        for (const auto& device : devices)
            list.append(device.toVariant());

        QString fastbootError;
        const auto fastbootDevices = device::FastbootClient::devices(&fastbootError);
        QVariantList fastbootList;
        fastbootList.reserve(fastbootDevices.size());
        for (const auto& device : fastbootDevices)
            fastbootList.append(device.toVariant());

        QVariantMap info;
        info.insert(QStringLiteral("connected"), false);
        for (const auto& device : devices) {
            if (!device.isOnline())
                continue;
            QString infoError;
            const auto details = device::AdbClient::deviceInfo(device.serial, &infoError);
            info.insert(QStringLiteral("connected"), true);
            info.insert(QStringLiteral("serial"), device.serial);
            info.insert(QStringLiteral("model"), details.model);
            info.insert(QStringLiteral("brand"), details.brand);
            info.insert(QStringLiteral("android"), details.androidVersion);
            info.insert(QStringLiteral("bootloader"), details.bootloader);
            info.insert(QStringLiteral("baseband"), details.baseband);
            info.insert(QStringLiteral("fingerprint"), details.fingerprint);
            const QString slot = device::AdbClient::getProp(device.serial, QStringLiteral("ro.boot.slot_suffix"))
                                     .output.trimmed();
            info.insert(QStringLiteral("slot"), slot);
            info.insert(QStringLiteral("mode"), QStringLiteral("adb"));
            break;
        }
        // fastboot-mode device: read a few variables for the status bar with a
        // short timeout so a hung bootloader can never block the refresh.
        if (!info.value(QStringLiteral("connected")).toBool() && !fastbootDevices.isEmpty()) {
            const QString serial = fastbootDevices.first().serial;
            info.insert(QStringLiteral("connected"), true);
            info.insert(QStringLiteral("mode"), QStringLiteral("fastboot"));
            info.insert(QStringLiteral("serial"), serial);
            info.insert(QStringLiteral("model"), fastbootDevices.first().serial);
            if (!fastbootDevices.first().mode.isEmpty())
                info.insert(QStringLiteral("bootloader"), fastbootDevices.first().mode);

            const device::FastbootResult getvar =
                device::FastbootClient::runFor(serial, {QStringLiteral("getvar"),
                                                        QStringLiteral("all")},
                                               4000);
            const QMap<QString, QString> vars =
                device::parseGetvarOutput(getvar.output + QLatin1Char('\n') + getvar.error);
            if (vars.size() >= 2) {
                if (!vars.value(QStringLiteral("product")).isEmpty())
                    info.insert(QStringLiteral("model"), vars.value(QStringLiteral("product")));
                if (!vars.value(QStringLiteral("current-slot")).isEmpty())
                    info.insert(QStringLiteral("slot"), vars.value(QStringLiteral("current-slot")));
                if (!vars.value(QStringLiteral("version-bootloader")).isEmpty())
                    info.insert(QStringLiteral("bootloader"), vars.value(QStringLiteral("version-bootloader")));
                else if (!vars.value(QStringLiteral("unlocked")).isEmpty())
                    info.insert(QStringLiteral("bootloader"),
                                vars.value(QStringLiteral("unlocked")) == QLatin1String("yes")
                                    ? QStringLiteral("已解锁")
                                    : QStringLiteral("未解锁"));
                if (!vars.value(QStringLiteral("version-baseband")).isEmpty())
                    info.insert(QStringLiteral("baseband"), vars.value(QStringLiteral("version-baseband")));
                if (!vars.value(QStringLiteral("battery-voltage")).isEmpty())
                    info.insert(QStringLiteral("battery"), vars.value(QStringLiteral("battery-voltage")));
                info.insert(QStringLiteral("batteryOk"),
                            vars.value(QStringLiteral("battery-soc-ok")).trimmed().toLower()
                                != QLatin1String("no"));
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, list, fastbootList, info, error]() {
                m_refreshingDevices = false;
                m_adbDevices = list;
                m_fastbootDevices = fastbootList;
                m_deviceInfo = info;
                refreshDeviceMode();
                emit adbDevicesChanged();
                emit deviceInfoChanged();
                if (!error.isEmpty())
                    GF_WARN(QStringLiteral("adb"), error);
            },
            Qt::QueuedConnection);
    }));
}

QVariantList AppController::driverStatus()
{
    if (!m_driverStatusLoaded) {
        const auto statuses = device::detectDrivers();
        m_driverStatus.clear();
        m_driverStatus.reserve(statuses.size());
        for (const auto& status : statuses)
            m_driverStatus.append(status.toVariant());
        m_driverStatusLoaded = true;
    }
    return m_driverStatus;
}

QString AppController::driverSummaryText()
{
    const QVariantList statuses = driverStatus();
    if (statuses.isEmpty())
        return QStringLiteral("驱动状态未知");
    int bound = 0;
    int missing = 0;
    for (const QVariant& entry : statuses) {
        const QString state = entry.toMap().value(QStringLiteral("state")).toString();
        if (state == QLatin1String("bound"))
            ++bound;
        else if (state == QLatin1String("missing"))
            ++missing;
    }
    return QStringLiteral("驱动 %1/%2 就绪").arg(bound).arg(statuses.size())
           + (missing > 0 ? QStringLiteral(" · %1 个待安装").arg(missing) : QString());
}

bool AppController::installDrivers()
{
    if (!util::isAdmin()) {
        notify(QStringLiteral("warning"), QStringLiteral("安装驱动需要管理员权限，请以管理员身份重新启动"));
        return false;
    }
    const QStringList infs = device::findDriverInfs();
    if (infs.isEmpty()) {
        notify(QStringLiteral("warning"), QStringLiteral("没有找到驱动包，请把 .inf 驱动放到 data/drivers 目录"));
        return false;
    }
    QString error;
    QStringList log;
    const int installed = device::installAllDriverInfs(device::driversDir(), &log, &error);
    m_driverStatusLoaded = false;
    if (installed <= 0) {
        notify(QStringLiteral("error"), QStringLiteral("驱动安装失败: ") + error);
        return false;
    }
    notify(QStringLiteral("success"), QStringLiteral("已安装 %1 个驱动包").arg(installed));
    return true;
}

QVariantList AppController::flashPorts() const
{
    // Only ports that are actually useful for flashing / device work. Generic
    // USB devices (webcams, hubs, card readers) would otherwise flood the UI.
    static const QSet<QString> keep = {
        QStringLiteral("qcom-edl"),  QStringLiteral("mtk-brom"), QStringLiteral("mtk-preloader"),
        QStringLiteral("mtk-da"),    QStringLiteral("sprd-fdl"), QStringLiteral("rk"),
        QStringLiteral("aw-fel"),    QStringLiteral("adb"),      QStringLiteral("fastboot"),
        QStringLiteral("fastbootd")};
    QVariantList out;
    for (const QVariant& entry : m_ports.ports()) {
        const QVariantMap port = entry.toMap();
        const QString kind = port.value(QStringLiteral("kind")).toString();
        if (keep.contains(kind) || port.value(QStringLiteral("hasProblem")).toBool())
            out.append(port);
    }
    return out;
}

QVariantList AppController::tools() const
{
    return ToolRegistry::instance().tools();
}

QVariantList AppController::categories() const
{
    return ToolRegistry::instance().categories();
}

QVariantList AppController::activeTasks() const
{
    return TaskManager::instance().activeTasks();
}

QVariantList AppController::recentTasks() const
{
    return TaskManager::instance().recentTasks();
}

QString AppController::version() const
{
#ifdef XMF_VERSION
    return QStringLiteral(XMF_VERSION);
#else
    return QStringLiteral("0.1.0");
#endif
}

QString AppController::dataPath() const
{
    return util::dataDir();
}

bool AppController::isAdmin() const
{
    return util::isAdmin();
}

QVariantMap AppController::toolInfo(const QString& id) const
{
    return ToolRegistry::instance().tool(id);
}

bool AppController::disclaimerAccepted() const
{
    return m_disclaimerAccepted;
}

void AppController::acceptDisclaimer()
{
    m_settings.setDisclaimerAccepted(true);
    if (!m_disclaimerAccepted) {
        m_disclaimerAccepted = true;
        emit disclaimerAcceptedChanged();
    }
}

void AppController::skipDisclaimerForAutomation()
{
    if (!m_disclaimerAccepted) {
        m_disclaimerAccepted = true;
        emit disclaimerAcceptedChanged();
    }
}

void AppController::previewDisclaimer()
{
    m_disclaimerAccepted = false;
    emit disclaimerAcceptedChanged();
}

QVariantMap AppController::deviceMode() const
{
    return m_deviceMode;
}

QString AppController::guideFile() const
{
    return m_guideFile;
}

void AppController::setGuideFile(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (m_guideFile == trimmed)
        return;
    m_guideFile = trimmed;
    m_settings.set(QStringLiteral("flash/guideFile"), m_guideFile);
    emit guideFileChanged();
}

QString AppController::pickGuideFromPacket(const QString& packetId, const QString& model,
                                           const QString& attribute)
{
    const QString path = packets()->modelResource(packetId, model, attribute);
    if (path.isEmpty()) {
        notify(QStringLiteral("error"),
               QStringLiteral("引导包中没有找到该机型的 %1 文件").arg(attribute));
        return {};
    }
    setGuideFile(path);
    notify(QStringLiteral("success"), QStringLiteral("已从引导包提取 %1：%2").arg(attribute, path));
    return path;
}

void AppController::refreshDeviceMode()
{
    QVariantMap mode;
    mode.insert(QStringLiteral("mode"), QStringLiteral("none"));
    mode.insert(QStringLiteral("label"), QStringLiteral("未检测到设备"));
    mode.insert(QStringLiteral("platform"), QString());
    mode.insert(QStringLiteral("detail"), QStringLiteral("连接设备或让它进入刷机模式"));

    // 1) ADB states (sideload is the recovery flashing state).
    for (const QVariant& entry : m_adbDevices) {
        const QVariantMap device = entry.toMap();
        const QString state = device.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("sideload")) {
            mode.insert(QStringLiteral("mode"), QStringLiteral("sideload"));
            mode.insert(QStringLiteral("label"), QStringLiteral("Recovery Sideload 卡刷"));
            mode.insert(QStringLiteral("platform"), QStringLiteral("qcom"));
            mode.insert(QStringLiteral("detail"), device.value(QStringLiteral("serial")));
            m_deviceMode = mode;
            emit deviceModeChanged();
            return;
        }
        if (state == QLatin1String("device")) {
            mode.insert(QStringLiteral("mode"), QStringLiteral("adb"));
            mode.insert(QStringLiteral("label"), QStringLiteral("ADB 系统模式"));
            mode.insert(QStringLiteral("platform"), QString());
            mode.insert(QStringLiteral("detail"), device.value(QStringLiteral("serial")));
            m_deviceMode = mode;
            emit deviceModeChanged();
            return;
        }
        if (state == QLatin1String("unauthorized")) {
            mode.insert(QStringLiteral("mode"), QStringLiteral("unauthorized"));
            mode.insert(QStringLiteral("label"), QStringLiteral("ADB 未授权"));
            mode.insert(QStringLiteral("detail"), QStringLiteral("请在设备上允许 USB 调试"));
            m_deviceMode = mode;
            emit deviceModeChanged();
            return;
        }
    }

    // 2) Fastboot device.
    if (!m_fastbootDevices.isEmpty()) {
        const QVariantMap device = m_fastbootDevices.first().toMap();
        const QString vidPid = device.value(QStringLiteral("vidPid")).toString().toUpper();
        QString platform = QStringLiteral("qcom");
        if (vidPid.contains(QStringLiteral("0E8D")))
            platform = QStringLiteral("mtk");
        else if (vidPid.contains(QStringLiteral("1782")))
            platform = QStringLiteral("sprd");
        else if (vidPid.contains(QStringLiteral("2207")))
            platform = QStringLiteral("rk");
        else if (vidPid.contains(QStringLiteral("1F3A")))
            platform = QStringLiteral("aw");
        mode.insert(QStringLiteral("mode"), QStringLiteral("fastboot"));
        mode.insert(QStringLiteral("label"), QStringLiteral("Fastboot 模式"));
        mode.insert(QStringLiteral("platform"), platform);
        mode.insert(QStringLiteral("detail"), device.value(QStringLiteral("serial")));
        m_deviceMode = mode;
        emit deviceModeChanged();
        return;
    }

    // 3) Raw USB ports -> deep flash modes.
    const QVariantList ports = m_ports.ports();
    for (const QVariant& entry : ports) {
        const QVariantMap port = entry.toMap();
        const QString kind = port.value(QStringLiteral("kind")).toString();
        const QString text = (port.value(QStringLiteral("description")).toString() + QLatin1Char(' ')
                              + port.value(QStringLiteral("chipLabel")).toString() + QLatin1Char(' ')
                              + port.value(QStringLiteral("vidPid")).toString())
                                 .toUpper();
        QString modeId;
        QString label;
        QString platform;
        if (kind == QLatin1String("qcom-edl")
            || text.contains(QStringLiteral("9008")) || text.contains(QStringLiteral("QDLOADER"))
            || text.contains(QStringLiteral("05C6"))) {
            modeId = QStringLiteral("edl9008");
            label = QStringLiteral("Qualcomm 9008 (EDL)");
            platform = QStringLiteral("qcom");
        } else if (kind == QLatin1String("mtk-brom") || kind == QLatin1String("mtk-preloader")
                   || text.contains(QStringLiteral("BROM"))
                   || text.contains(QStringLiteral("PRELOADER")) || text.contains(QStringLiteral("0E8D"))) {
            modeId = QStringLiteral("mtk-brom");
            label = QStringLiteral("MediaTek BROM / Preloader");
            platform = QStringLiteral("mtk");
        } else if (kind == QLatin1String("sprd-fdl") || text.contains(QStringLiteral("SPRD"))
                   || text.contains(QStringLiteral("FDL")) || text.contains(QStringLiteral("1782"))) {
            modeId = QStringLiteral("sprd-fdl");
            label = QStringLiteral("Unisoc FDL / U2S");
            platform = QStringLiteral("sprd");
        } else if (kind == QLatin1String("rk") || text.contains(QStringLiteral("MASKROM"))
                   || text.contains(QStringLiteral("2207"))) {
            modeId = QStringLiteral("rk-maskrom");
            label = QStringLiteral("Rockchip MaskROM / Loader");
            platform = QStringLiteral("rk");
        } else if (kind == QLatin1String("allwinner") || text.contains(QStringLiteral("FEL"))
                   || text.contains(QStringLiteral("1F3A"))) {
            modeId = QStringLiteral("aw-fel");
            label = QStringLiteral("Allwinner FEL");
            platform = QStringLiteral("aw");
        }
        if (!modeId.isEmpty()) {
            mode.insert(QStringLiteral("mode"), modeId);
            mode.insert(QStringLiteral("label"), label);
            mode.insert(QStringLiteral("platform"), platform);
            mode.insert(QStringLiteral("detail"), port.value(QStringLiteral("portName")));
            m_deviceMode = mode;
            emit deviceModeChanged();
            return;
        }
    }

    m_deviceMode = mode;
    emit deviceModeChanged();
}

int AppController::runTool(const QString& id, const QVariantMap& options)
{
    auto& registry = ToolRegistry::instance();
    const QVariantMap spec = registry.tool(id);
    if (spec.isEmpty()) {
        notify(QStringLiteral("error"), QStringLiteral("未知工具: ") + id);
        return -1;
    }
    auto handler = registry.handler(id);
    if (!handler) {
        notify(QStringLiteral("info"), QStringLiteral("「%1」的后端仍在开发中，敬请期待")
                                           .arg(spec.value(QStringLiteral("name")).toString()));
        return -1;
    }
    const QVariantList fields = spec.value(QStringLiteral("fields")).toList();
    for (const QVariant& fv : fields) {
        const QVariantMap field = fv.toMap();
        if (!field.value(QStringLiteral("required"), false).toBool())
            continue;
        const QString key = field.value(QStringLiteral("key")).toString();
        const QVariant value = options.value(key);
        if (!value.isValid() || (value.canConvert<QString>() && value.toString().trimmed().isEmpty())) {
            notify(QStringLiteral("error"),
                   QStringLiteral("请先填写「%1」").arg(field.value(QStringLiteral("label")).toString()));
            return -1;
        }
    }

    const QString title = spec.value(QStringLiteral("name")).toString();
    GF_INFO(QStringLiteral("tool"), QStringLiteral("开始执行: %1 (%2)").arg(title, id));
    return TaskManager::instance().submit(title, id, [handler, options](TaskContext& ctx) { handler(ctx, options); });
}

void AppController::cancelAllTasks()
{
    for (const QVariant& entry : TaskManager::instance().activeTasks()) {
        const QVariantMap task = entry.toMap();
        cancelTask(task.value(QStringLiteral("id")).toInt());
    }
    GF_INFO(QStringLiteral("task"), QStringLiteral("已请求取消全部运行中任务"));
}

void AppController::cancelTask(int id)
{
    TaskManager::instance().cancel(id);
}

void AppController::clearLogs()
{
    Logger::instance().model()->clear();
}

void AppController::notify(const QString& type, const QString& message)
{
    emit notifyRequested(type, message);
}

int AppController::startBatch(const QString& toolId, const QString& pathField,
                              const QVariantList& files, const QVariantMap& baseOptions,
                              int parallel)
{
    if (files.isEmpty() || toolId.isEmpty() || pathField.isEmpty())
        return 0;
    m_batchQueue.clear();
    m_batchActiveTasks.clear();
    m_batchToolId = toolId;
    m_batchTotal = int(files.size());
    m_batchDone = 0;
    m_batchFailed = 0;
    m_batchActive = true;
    m_batchParallel = qBound(1, parallel, 8);
    // Tools declare their file option either as a single "path" or a multi-file
    // "paths" list; feed the handler the shape it expects.
    QString fieldType = QStringLiteral("path");
    for (const QVariant& field : ToolRegistry::instance().fieldsOf(toolId)) {
        const QVariantMap map = field.toMap();
        if (map.value(QStringLiteral("key")).toString() == pathField) {
            fieldType = map.value(QStringLiteral("type")).toString();
            break;
        }
    }
    for (const QVariant& file : files) {
        BatchEntry entry;
        entry.label = QFileInfo(file.toString()).fileName();
        entry.options = baseOptions;
        if (fieldType == QLatin1String("paths"))
            entry.options.insert(pathField, QVariantList{file.toString()});
        else
            entry.options.insert(pathField, file.toString());
        m_batchQueue.append(entry);
    }
    m_batchState.clear();
    m_batchState.insert(QStringLiteral("active"), true);
    m_batchState.insert(QStringLiteral("total"), m_batchTotal);
    m_batchState.insert(QStringLiteral("done"), 0);
    m_batchState.insert(QStringLiteral("failed"), 0);
    m_batchState.insert(QStringLiteral("percent"), 0);
    m_batchState.insert(QStringLiteral("current"), QString());
    m_batchState.insert(QStringLiteral("parallel"), m_batchParallel);
    m_batchState.insert(QStringLiteral("running"), 0);
    m_batchState.insert(QStringLiteral("lines"), QVariantList());
    updateBatchState(QStringLiteral("开始批量任务：%1 个文件 · 并行 %2").arg(m_batchTotal).arg(m_batchParallel));
    batchAdvance();
    return m_batchTotal;
}

void AppController::batchAdvance()
{
    if (!m_batchActive)
        return;

    // Keep up to m_batchParallel tool tasks in flight; TaskManager already runs
    // each task on its own worker thread.
    while (!m_batchQueue.isEmpty() && m_batchActiveTasks.size() < m_batchParallel) {
        const BatchEntry entry = m_batchQueue.takeFirst();
        m_batchState.insert(QStringLiteral("current"), entry.label);
        updateBatchState(QStringLiteral("执行 %1").arg(entry.label));
        const int taskId = runTool(m_batchToolId, entry.options);
        if (taskId <= 0) {
            ++m_batchDone;
            ++m_batchFailed;
            updateBatchState(QStringLiteral("%1 无法启动").arg(entry.label));
            continue;
        }
        m_batchActiveTasks.insert(taskId, entry.label);
    }

    if (m_batchQueue.isEmpty() && m_batchActiveTasks.isEmpty()) {
        m_batchActive = false;
        m_batchState.insert(QStringLiteral("active"), false);
        m_batchState.insert(QStringLiteral("percent"), 100);
        updateBatchState(QStringLiteral("批量结束：成功 %1 / 失败 %2")
                             .arg(m_batchDone - m_batchFailed)
                             .arg(m_batchFailed));
        return;
    }
    m_batchState.insert(QStringLiteral("running"), m_batchActiveTasks.size());
    m_batchState.insert(QStringLiteral("percent"),
                        m_batchTotal > 0 ? int(100.0 * m_batchDone / m_batchTotal) : 0);
    emit batchStateChanged();
}

void AppController::cancelBatch()
{
    if (!m_batchActive)
        return;
    m_batchActive = false;
    m_batchQueue.clear();
    for (auto it = m_batchActiveTasks.constBegin(); it != m_batchActiveTasks.constEnd(); ++it)
        cancelTask(it.key());
    m_batchActiveTasks.clear();
    m_batchState.insert(QStringLiteral("active"), false);
    m_batchState.insert(QStringLiteral("running"), 0);
    updateBatchState(QStringLiteral("批量任务已取消"));
}

void AppController::updateBatchState(const QString& message, bool appendLog)
{
    QVariantList lines = m_batchState.value(QStringLiteral("lines")).toList();
    if (appendLog) {
        lines.append(message);
        while (lines.size() > 200)
            lines.removeFirst();
    }
    m_batchState.insert(QStringLiteral("lines"), lines);
    m_batchState.insert(QStringLiteral("done"), m_batchDone);
    m_batchState.insert(QStringLiteral("failed"), m_batchFailed);
    m_batchState.insert(QStringLiteral("active"), m_batchActive);
    m_batchState.insert(QStringLiteral("running"), m_batchActiveTasks.size());
    if (m_batchTotal > 0)
        m_batchState.insert(QStringLiteral("percent"),
                            int(100.0 * m_batchDone / m_batchTotal));
    emit batchStateChanged();
    GF_INFO(QStringLiteral("batch"), message);
}

QString AppController::saveLogs(const QString& path)
{
    const QString text = Logger::instance().model()->toPlainText();
    if (path.isEmpty()) {
        const QString target = util::logsDir() + QStringLiteral("/xmf-export-%1.log")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        if (!util::ensureDir(QFileInfo(target).absolutePath()))
            return {};
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return {};
        file.write(text.toUtf8());
        return target;
    }
    if (!util::ensureDir(QFileInfo(path).absolutePath()))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    file.write(text.toUtf8());
    return path;
}

namespace {

qint64 directorySize(const QString& path)
{
    qint64 total = 0;
    QDirIterator iterator(path, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        total += iterator.fileInfo().size();
    }
    return total;
}

} // namespace

qint64 AppController::cacheSize() const
{
    return directorySize(util::cacheDir());
}

bool AppController::clearCache()
{
    const QString cache = util::cacheDir();
    QDir dir(cache);
    if (!dir.exists())
        return true;
    bool ok = true;
    for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                    QDir::Name)) {
        if (entry.isDir())
            ok = QDir(entry.absoluteFilePath()).removeRecursively() && ok;
        else
            ok = QFile::remove(entry.absoluteFilePath()) && ok;
    }
    util::ensureDir(cache);
    if (ok)
        GF_INFO(QStringLiteral("app"), QStringLiteral("缓存已清理：%1").arg(cache));
    return ok;
}

QString AppController::writeDiagnostics()
{
    QStringList lines;
    lines << QStringLiteral("XMF Flash Tool 诊断报告")
          << QStringLiteral("生成时间: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate))
          << QStringLiteral("版本: %1").arg(version())
          << QStringLiteral("操作系统: %1").arg(util::osVersion())
          << QStringLiteral("管理员: %1").arg(isAdmin() ? QStringLiteral("是") : QStringLiteral("否"))
          << QStringLiteral("数据目录: %1").arg(dataPath())
          << QStringLiteral("工具数量: %1").arg(ToolRegistry::instance().tools().size())
          << QStringLiteral("端口数量: %1").arg(m_ports.count())
          << QStringLiteral("ADB 设备: %1").arg(m_adbDevices.size())
          << QStringLiteral("Fastboot 设备: %1").arg(m_fastbootDevices.size())
          << QStringLiteral("驱动状态: %1").arg(driverSummaryText())
          << QStringLiteral("引导包: %1 个 / %2 机型")
                 .arg(packets()->packets().size())
                 .arg(packets()->modelCount());

    lines << QStringLiteral("")
          << QStringLiteral("== 端口 ==");
    for (const QVariant& entry : m_ports.ports()) {
        const QVariantMap port = entry.toMap();
        lines << QStringLiteral("%1 | %2 | %3 | %4")
                     .arg(port.value(QStringLiteral("portName")).toString(),
                          port.value(QStringLiteral("description")).toString(),
                          port.value(QStringLiteral("kindLabel")).toString(),
                          port.value(QStringLiteral("vidPid")).toString());
    }

    lines << QStringLiteral("")
          << QStringLiteral("== ADB 设备 ==");
    for (const QVariant& entry : m_adbDevices) {
        const QVariantMap device = entry.toMap();
        lines << QStringLiteral("%1 | %2 | %3")
                     .arg(device.value(QStringLiteral("serial")).toString(),
                          device.value(QStringLiteral("state")).toString(),
                          device.value(QStringLiteral("model")).toString());
    }
    lines << QStringLiteral("")
          << QStringLiteral("== Fastboot 设备 ==");
    for (const QVariant& entry : m_fastbootDevices) {
        const QVariantMap device = entry.toMap();
        lines << QStringLiteral("%1 | %2")
                     .arg(device.value(QStringLiteral("serial")).toString(),
                          device.value(QStringLiteral("mode")).toString());
    }

    lines << QStringLiteral("")
          << QStringLiteral("== 驱动 ==");
    for (const QVariant& entry : driverStatus()) {
        const QVariantMap driver = entry.toMap();
        lines << QStringLiteral("%1 | %2 | %3")
                     .arg(driver.value(QStringLiteral("name")).toString(),
                          driver.value(QStringLiteral("state")).toString(),
                          driver.value(QStringLiteral("detail")).toString());
    }

    const QString target = util::logsDir() + QStringLiteral("/diagnostics-%1.txt")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    if (!util::ensureDir(util::logsDir()))
        return {};
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    file.write(lines.join(QLatin1Char('\n')).toUtf8());
    file.close();
    GF_INFO(QStringLiteral("app"), QStringLiteral("诊断报告已导出：%1").arg(target));
    return target;
}

QVariantList AppController::filesInDirectory(const QString& dir, const QString& filterCsv) const
{
    QVariantList list;
    QStringList filters;
    for (const QString& part : filterCsv.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        filters << part.trimmed();
    if (filters.isEmpty())
        filters << QStringLiteral("*.*");
    QDir directory(dir);
    const QFileInfoList files = directory.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& file : files) {
        QVariantMap map;
        map.insert(QStringLiteral("path"), file.absoluteFilePath());
        map.insert(QStringLiteral("name"), file.fileName());
        map.insert(QStringLiteral("size"), file.size());
        map.insert(QStringLiteral("sizeText"), formatBytes(file.size()));
        list.append(map);
    }
    return list;
}

QString AppController::formatBytes(qint64 bytes) const
{
    return util::formatBytes(bytes);
}

QString AppController::formatDuration(qint64 ms) const
{
    return util::formatDuration(ms);
}

void AppController::copyText(const QString& text) const
{
    if (auto* clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void AppController::openUrl(const QString& url) const
{
    util::openUrl(url);
}

void AppController::openPath(const QString& path) const
{
    util::openPath(path);
}

QVariantMap AppController::runAdbCommand(const QString& serial, const QString& command)
{
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    if (command.trimmed().isEmpty()) {
        result.insert(QStringLiteral("error"), QStringLiteral("请输入 ADB 命令"));
        return result;
    }
    QString resolvedSerial = serial.trimmed();
    QString error;
    if (resolvedSerial.isEmpty())
        resolvedSerial = device::AdbClient::resolveSerial(QString(), &error);
    if (resolvedSerial.isEmpty()) {
        result.insert(QStringLiteral("error"),
                      error.isEmpty() ? QStringLiteral("没有检测到 ADB 设备") : error);
        return result;
    }
    const device::AdbResult adb = device::AdbClient::shell(resolvedSerial, command);
    result.insert(QStringLiteral("ok"), adb.ok);
    result.insert(QStringLiteral("output"), adb.output);
    if (!adb.ok)
        result.insert(QStringLiteral("error"), adb.error.isEmpty() ? adb.output : adb.error);
    else
        result.insert(QStringLiteral("error"), QString());
    result.insert(QStringLiteral("serial"), resolvedSerial);
    return result;
}

QVariantMap AppController::runFastbootCommand(const QString& serial, const QString& arguments)
{
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    QStringList args = QProcess::splitCommand(arguments.trimmed());
    if (!args.isEmpty() && args.first().compare(QStringLiteral("fastboot"), Qt::CaseInsensitive) == 0)
        args.removeFirst();
    if (args.isEmpty()) {
        result.insert(QStringLiteral("error"), QStringLiteral("请输入 fastboot 命令，例如 getvar all"));
        return result;
    }
    QString resolvedSerial = serial.trimmed();
    QString error;
    if (resolvedSerial.isEmpty())
        resolvedSerial = device::FastbootClient::resolveSerial(QString(), &error);
    if (resolvedSerial.isEmpty()) {
        result.insert(QStringLiteral("error"),
                      error.isEmpty() ? QStringLiteral("没有检测到 Fastboot 设备") : error);
        return result;
    }
    const device::FastbootResult fb = device::FastbootClient::runFor(resolvedSerial, args, 120000);
    result.insert(QStringLiteral("ok"), fb.ok);
    result.insert(QStringLiteral("output"), fb.output);
    result.insert(QStringLiteral("error"),
                  fb.ok ? QString() : (fb.error.isEmpty() ? fb.output : fb.error));
    result.insert(QStringLiteral("serial"), resolvedSerial);
    return result;
}

QString AppController::openDocs(const QString& fileName) const
{
    const QStringList candidates = {
        util::appDir() + QStringLiteral("/docs/") + fileName,
        util::dataDir() + QStringLiteral("/../docs/") + fileName,
        util::appDir() + QStringLiteral("/../docs/") + fileName,
    };
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            util::openPath(info.absoluteFilePath());
            return info.absoluteFilePath();
        }
    }
    return {};
}

void AppController::revealPath(const QString& path) const
{
    util::revealInExplorer(path);
}

} // namespace gf
