#include "DriverManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "core/Util.h"

namespace gf::device {

namespace {

bool containsAny(const QString& haystack, std::initializer_list<const char*> needles)
{
    for (const char* needle : needles) {
        if (haystack.contains(QString::fromLatin1(needle), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QString searchableText(const RawDevice& device)
{
    return (device.description + QLatin1Char(' ') + device.friendlyName + QLatin1Char(' ')
            + device.service + QLatin1Char(' ') + device.searchableIds() + QLatin1Char(' ')
            + device.className)
        .toLower();
}

void addUnique(QStringList& list, const QString& value)
{
    if (value.isEmpty())
        return;
    if (!list.contains(value))
        list.append(value);
}

} // namespace

QString driverKindId(DriverKind kind)
{
    switch (kind) {
    case DriverKind::QdLoader: return QStringLiteral("qdloader");
    case DriverKind::Mtk: return QStringLiteral("mtk");
    case DriverKind::Sprd: return QStringLiteral("sprd");
    case DriverKind::Rockchip: return QStringLiteral("rk");
    case DriverKind::Allwinner: return QStringLiteral("aw");
    case DriverKind::Adb: return QStringLiteral("adb");
    }
    return QStringLiteral("unknown");
}

QString driverKindName(DriverKind kind)
{
    switch (kind) {
    case DriverKind::QdLoader: return QStringLiteral("高通 QDLoader 9008");
    case DriverKind::Mtk: return QStringLiteral("联发科 MTK Preloader/BROM");
    case DriverKind::Sprd: return QStringLiteral("展锐 SPRD/Unisoc");
    case DriverKind::Rockchip: return QStringLiteral("瑞芯微 Rockchip");
    case DriverKind::Allwinner: return QStringLiteral("全志 Allwinner");
    case DriverKind::Adb: return QStringLiteral("ADB 调试接口");
    }
    return QStringLiteral("未知驱动");
}

QString driverKindHint(DriverKind kind)
{
    switch (kind) {
    case DriverKind::QdLoader:
        return QStringLiteral("请安装 Qualcomm HS-USB QDLoader 9008 驱动（qcusb.inf / qdloader.inf）。");
    case DriverKind::Mtk:
        return QStringLiteral("请安装 MediaTek USB VCOM / Preloader 驱动（mtk_usb.inf）。");
    case DriverKind::Sprd:
        return QStringLiteral("请安装 Spreadtrum/Unisoc USB 驱动（sprd_usb.inf）。");
    case DriverKind::Rockchip:
        return QStringLiteral("请安装 Rockchip USB 驱动（rockusb.inf）。");
    case DriverKind::Allwinner:
        return QStringLiteral("请安装 Allwinner USB 驱动（aw_usb.inf）。");
    case DriverKind::Adb:
        return QStringLiteral("请安装 Google ADB 驱动（android_winusb.inf），或打开设备管理器手动更新驱动。");
    }
    return {};
}

QString driverStateId(DriverState state)
{
    switch (state) {
    case DriverState::Bound: return QStringLiteral("bound");
    case DriverState::Missing: return QStringLiteral("missing");
    case DriverState::Absent: return QStringLiteral("absent");
    }
    return QStringLiteral("absent");
}

QList<DriverMatch> matchDriverBindings(const RawDevice& device)
{
    QList<DriverMatch> matches;
    const QString text = searchableText(device);
    auto add = [&matches, &device](DriverKind kind) {
        for (const auto& existing : matches) {
            if (existing.kind == kind)
                return;
        }
        matches.append({kind, device.service});
    };

    const bool qdloader = (device.vid == 0x05C6 && device.pid >= 0x9000 && device.pid <= 0x90FF)
                          || containsAny(text, {"qdloader", "qcusb", "qualcomm hs-usb",
                                                "9008", "sahara", "firehose"});
    if (qdloader)
        add(DriverKind::QdLoader);

    const bool mtk = device.vid == 0x0E8D
                     || containsAny(text, {"mediatek", "mtk usb", "mtk_usb", "preloader", "brom"});
    if (mtk)
        add(DriverKind::Mtk);

    const bool sprd = device.vid == 0x1782
                      || containsAny(text, {"spreadtrum", "sprd", "unisoc"});
    if (sprd)
        add(DriverKind::Sprd);

    const bool rk = device.vid == 0x2207
                    || containsAny(text, {"rockchip", "rockusb", "rk3"});
    if (rk)
        add(DriverKind::Rockchip);

    const bool aw = device.vid == 0x1F3A
                    || containsAny(text, {"allwinner", "aw_usb", "aw usb", "sunxi"});
    if (aw)
        add(DriverKind::Allwinner);

    const bool adb = containsAny(text, {"adb interface", "android composite adb", "adb winusb",
                                        "android adb"})
                     || device.searchableIds().contains(
                            QStringLiteral("class_ff&subclass_42&prot_01"))
                     || (device.searchableIds().contains(QStringLiteral("class_ff&subclass_42"))
                         && text.contains(QStringLiteral("android")));
    if (adb)
        add(DriverKind::Adb);

    return matches;
}

QVariantMap DriverStatus::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("kind"), driverKindId(kind));
    map.insert(QStringLiteral("name"), driverKindName(kind));
    map.insert(QStringLiteral("state"), driverStateId(state));
    map.insert(QStringLiteral("bound"), bound());
    map.insert(QStringLiteral("devices"), devices);
    map.insert(QStringLiteral("services"), services);
    map.insert(QStringLiteral("hardwareIds"), hardwareIds);
    map.insert(QStringLiteral("message"), message);
    map.insert(QStringLiteral("requiresAdmin"), requiresAdmin);
    return map;
}

QList<DriverStatus> detectDrivers()
{
    const QList<RawDevice> devices = enumeratePresentDevices();

    QList<DriverStatus> statuses;
    for (DriverKind kind : {DriverKind::QdLoader, DriverKind::Mtk, DriverKind::Sprd,
                            DriverKind::Rockchip, DriverKind::Allwinner, DriverKind::Adb}) {
        DriverStatus status;
        status.kind = kind;
        status.requiresAdmin = false;
        statuses.append(status);
    }
    auto statusFor = [&statuses](DriverKind kind) -> DriverStatus& {
        for (auto& status : statuses) {
            if (status.kind == kind)
                return status;
        }
        statuses.append(DriverStatus{kind, DriverState::Absent, {}, {}, {}, {}, false});
        return statuses.last();
    };

    for (const RawDevice& device : devices) {
        for (const DriverMatch& match : matchDriverBindings(device)) {
            DriverStatus& status = statusFor(match.kind);
            addUnique(status.devices, device.displayName());
            addUnique(status.services, device.service);
            addUnique(status.hardwareIds,
                      formatVidPid(device.vid, device.pid).isEmpty()
                          ? device.primaryHardwareId()
                          : formatVidPid(device.vid, device.pid));
            const bool driverProblem = device.hasProblem && device.problemCode == 28;
            const bool bound = !device.service.isEmpty() && !driverProblem;
            if (bound)
                status.state = DriverState::Bound;
            else if (status.state != DriverState::Bound)
                status.state = DriverState::Missing;
        }
    }

    for (auto& status : statuses) {
        switch (status.state) {
        case DriverState::Bound:
            status.message = QStringLiteral("已绑定驱动（%1 个设备）").arg(status.devices.size());
            break;
        case DriverState::Missing:
            status.message = QStringLiteral("设备存在但驱动未安装/未绑定，%1")
                                 .arg(driverKindHint(status.kind));
            status.requiresAdmin = true;
            break;
        case DriverState::Absent:
            status.message = QStringLiteral("未检测到设备（未连接或驱动未安装）。%1")
                                 .arg(driverKindHint(status.kind));
            break;
        }
    }
    return statuses;
}

QString driverStatusLine(const DriverStatus& status)
{
    QString stateText;
    switch (status.state) {
    case DriverState::Bound: stateText = QStringLiteral("已绑定"); break;
    case DriverState::Missing: stateText = QStringLiteral("驱动未安装"); break;
    case DriverState::Absent: stateText = QStringLiteral("未检测到设备"); break;
    }
    QString line = QStringLiteral("%1：%2").arg(driverKindName(status.kind), stateText);
    if (!status.devices.isEmpty())
        line += QStringLiteral("（%1）").arg(status.devices.join(QStringLiteral("、")));
    if (!status.services.isEmpty())
        line += QStringLiteral(" 服务=%1").arg(status.services.join(QStringLiteral(", ")));
    return line;
}

QString driverSummary(const QList<DriverStatus>& statuses)
{
    QStringList lines;
    lines.append(QStringLiteral("驱动状态检查（管理员权限：%1）")
                     .arg(util::isAdmin() ? QStringLiteral("是") : QStringLiteral("否")));
    int bound = 0;
    for (const auto& status : statuses) {
        lines.append(QStringLiteral("  • %1").arg(driverStatusLine(status)));
        if (status.state == DriverState::Bound)
            ++bound;
    }
    lines.append(QStringLiteral("共 %1 类驱动，已绑定 %2 类。").arg(statuses.size()).arg(bound));
    if (bound < statuses.size()) {
        lines.append(QStringLiteral("如需安装驱动，请把驱动包放到 %1 后使用「安装驱动」工具，"
                                    "安装需要管理员权限。")
                         .arg(driversDir()));
    }
    return lines.join(QLatin1Char('\n'));
}

QString driversDir()
{
    return QDir::toNativeSeparators(util::dataDir() + QStringLiteral("/drivers"));
}

QStringList findDriverInfs(const QString& dir)
{
    const QString root = dir.trimmed().isEmpty() ? driversDir() : dir.trimmed();
    QStringList infs;
    QDirIterator it(root, {QStringLiteral("*.inf")}, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        infs.append(QDir::toNativeSeparators(it.next()));
    infs.sort(Qt::CaseInsensitive);
    return infs;
}

bool installDriverInf(const QString& infPath, QString* output, QString* error)
{
    QStringList log;
    if (!util::isAdmin()) {
        if (error)
            *error = QStringLiteral("安装驱动需要管理员权限。请以管理员身份重新运行本程序后再试。");
        return false;
    }
    if (!QFileInfo::exists(infPath)) {
        if (error)
            *error = QStringLiteral("驱动文件不存在：%1").arg(infPath);
        return false;
    }
    const util::ProcessResult proc =
        util::runProcess(QStringLiteral("pnputil.exe"),
                         {QStringLiteral("/add-driver"), QDir::toNativeSeparators(infPath),
                          QStringLiteral("/install")},
                         300000);
    const QString text = (QString::fromLocal8Bit(proc.stdoutData) + QLatin1Char('\n')
                          + QString::fromLocal8Bit(proc.stderrData))
                             .trimmed();
    if (output)
        *output = text;
    if (!proc.started) {
        if (error)
            *error = QStringLiteral("无法启动 pnputil.exe：%1")
                         .arg(QString::fromLocal8Bit(proc.stderrData).trimmed());
        return false;
    }
    if (proc.timedOut) {
        if (error)
            *error = QStringLiteral("pnputil 安装驱动超时：%1").arg(infPath);
        return false;
    }
    // 0 = success, 3010 = success but reboot pending.
    if (proc.exitCode == 0 || proc.exitCode == 3010) {
        if (error)
            error->clear();
        return true;
    }
    if (error) {
        *error = QStringLiteral("pnputil 安装驱动失败（退出码 %1）：%2")
                     .arg(proc.exitCode)
                     .arg(text.right(800));
    }
    return false;
}

int installAllDriverInfs(const QString& dir, QStringList* log, QString* error)
{
    const QString root = dir.trimmed().isEmpty() ? driversDir() : dir.trimmed();
    const QStringList infs = findDriverInfs(root);
    if (infs.isEmpty()) {
        if (error)
            *error = QStringLiteral("驱动目录中没有 .inf 文件：%1\n请把驱动包解压到该目录后重试。").arg(root);
        return 0;
    }
    int installed = 0;
    QStringList failures;
    for (const QString& inf : infs) {
        QString output;
        QString failure;
        if (installDriverInf(inf, &output, &failure)) {
            ++installed;
            if (log)
                log->append(QStringLiteral("✓ 已安装 %1").arg(QFileInfo(inf).fileName()));
        } else {
            failures.append(QStringLiteral("%1：%2").arg(QFileInfo(inf).fileName(), failure));
            if (log)
                log->append(QStringLiteral("✗ %1").arg(failures.last()));
        }
    }
    if (error) {
        if (failures.isEmpty())
            error->clear();
        else
            *error = QStringLiteral("%1 个驱动安装失败：\n%2")
                         .arg(failures.size())
                         .arg(failures.join(QLatin1Char('\n')));
    }
    return installed;
}

} // namespace gf::device
