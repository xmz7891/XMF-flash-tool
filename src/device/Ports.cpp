#include "Ports.h"

#include <algorithm>

namespace gf::device {

namespace {

struct VidPidEntry {
    quint16 vid;
    quint16 pid;
    PortKind kind;
    const char* chipHint;
    const char* label;
};

// Known boot-mode USB ids. A single vendor usually owns a small PID range; we
// only list ids that are widely used so unrelated devices are not mislabeled.
const VidPidEntry kVidPidTable[] = {
    // Qualcomm EDL / Sahara / Firehose
    {0x05C6, 0x9008, PortKind::QdLoader, "qcom-edl", "高通 EDL 9008"},
    {0x05C6, 0x9006, PortKind::QdLoader, "qcom-edl", "高通 EDL 9006"},
    {0x05C6, 0x900E, PortKind::QdLoader, "qcom-edl", "高通 EDL 900E"},
    {0x05C6, 0x901D, PortKind::QdLoader, "qcom-edl", "高通 EDL 901D"},
    {0x05C6, 0x9091, PortKind::QdLoader, "qcom-edl", "高通 Sahara 9091"},
    // MediaTek BROM / preloader
    {0x0E8D, 0x0003, PortKind::MtkBrom, "mtk-brom", "联发科 BROM"},
    {0x0E8D, 0x0004, PortKind::MtkBrom, "mtk-brom", "联发科 BROM"},
    {0x0E8D, 0x0006, PortKind::MtkBrom, "mtk-brom", "联发科 BROM"},
    {0x0E8D, 0x2000, PortKind::MtkPreloader, "mtk-preloader", "联发科 Preloader"},
    {0x0E8D, 0x2001, PortKind::MtkPreloader, "mtk-preloader", "联发科 Preloader"},
    {0x0E8D, 0x201C, PortKind::MtkPreloader, "mtk-preloader", "联发科 Preloader"},
    // Spreadtrum / Unisoc
    {0x1782, 0x4D00, PortKind::SprdFdl, "sprd-fdl", "展锐 FDL"},
    {0x1782, 0x4D01, PortKind::SprdFdl, "sprd-fdl", "展锐 FDL"},
    {0x1782, 0x5D05, PortKind::SprdFdl, "sprd-fdl", "展锐 FDL"},
    {0x1782, 0x5D06, PortKind::SprdFdl, "sprd-fdl", "展锐 FDL"},
    // Rockchip loader / maskrom
    {0x2207, 0x110A, PortKind::Rockchip, "rk", "瑞芯微 Loader"},
    {0x2207, 0x300A, PortKind::Rockchip, "rk", "瑞芯微 Maskrom"},
    {0x2207, 0x310B, PortKind::Rockchip, "rk", "瑞芯微 Loader"},
    {0x2207, 0x320A, PortKind::Rockchip, "rk", "瑞芯微 Loader"},
    {0x2207, 0x330A, PortKind::Rockchip, "rk", "瑞芯微 Loader"},
    {0x2207, 0x350A, PortKind::Rockchip, "rk", "瑞芯微 Loader"},
    // Allwinner FEL
    {0x1F3A, 0xEFE8, PortKind::Allwinner, "aw-fel", "全志 FEL"},
    {0x1F3A, 0xEFE9, PortKind::Allwinner, "aw-fel", "全志 FEL"},
    // Android bootloader in fastboot mode (Google / common reference ids)
    {0x18D1, 0x4EE0, PortKind::Fastboot, "fastboot", "Fastboot 引导"},
    {0x18D1, 0xD00D, PortKind::Fastboot, "fastboot", "Fastboot 引导"},
    {0x18D1, 0x4EE7, PortKind::Adb, "adb", "Android ADB 接口"},
    {0x18D1, 0x4EE2, PortKind::Mtp, "", "Android MTP"},
    {0x18D1, 0x4EE1, PortKind::Mtp, "", "Android MTP"},
};

const VidPidEntry* lookupVidPid(quint16 vid, quint16 pid)
{
    for (const auto& entry : kVidPidTable) {
        if (entry.vid == vid && entry.pid == pid)
            return &entry;
    }
    return nullptr;
}

bool containsAny(const QString& haystack, std::initializer_list<const char*> needles)
{
    for (const char* needle : needles) {
        if (haystack.contains(QString::fromLatin1(needle), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

} // namespace

QString chipHintForVidPid(quint16 vid, quint16 pid)
{
    if (const VidPidEntry* entry = lookupVidPid(vid, pid))
        return QString::fromLatin1(entry->chipHint);
    return {};
}

QString portKindId(PortKind kind)
{
    switch (kind) {
    case PortKind::Serial: return QStringLiteral("serial");
    case PortKind::Adb: return QStringLiteral("adb");
    case PortKind::Fastboot: return QStringLiteral("fastboot");
    case PortKind::FastbootD: return QStringLiteral("fastbootd");
    case PortKind::Mtp: return QStringLiteral("mtp");
    case PortKind::QdLoader: return QStringLiteral("qcom-edl");
    case PortKind::MtkBrom: return QStringLiteral("mtk-brom");
    case PortKind::MtkPreloader: return QStringLiteral("mtk-preloader");
    case PortKind::SprdFdl: return QStringLiteral("sprd-fdl");
    case PortKind::Rockchip: return QStringLiteral("rk");
    case PortKind::Allwinner: return QStringLiteral("aw-fel");
    case PortKind::Unknown: break;
    }
    return QStringLiteral("unknown");
}

QString portKindLabel(PortKind kind)
{
    switch (kind) {
    case PortKind::Serial: return QStringLiteral("串口设备");
    case PortKind::Adb: return QStringLiteral("ADB 设备");
    case PortKind::Fastboot: return QStringLiteral("Fastboot 设备");
    case PortKind::FastbootD: return QStringLiteral("Fastbootd 设备");
    case PortKind::Mtp: return QStringLiteral("Android 媒体设备");
    case PortKind::QdLoader: return QStringLiteral("高通 EDL 9008");
    case PortKind::MtkBrom: return QStringLiteral("联发科 BROM");
    case PortKind::MtkPreloader: return QStringLiteral("联发科 Preloader");
    case PortKind::SprdFdl: return QStringLiteral("展锐 FDL");
    case PortKind::Rockchip: return QStringLiteral("瑞芯微 LOADER");
    case PortKind::Allwinner: return QStringLiteral("全志 FEL");
    case PortKind::Unknown: break;
    }
    return QStringLiteral("未知 USB 设备");
}

QString chipHintLabel(const QString& chipHint)
{
    if (chipHint == QLatin1String("qcom-edl")) return QStringLiteral("高通 9008");
    if (chipHint == QLatin1String("mtk-brom")) return QStringLiteral("联发科 BROM");
    if (chipHint == QLatin1String("mtk-preloader")) return QStringLiteral("联发科 Preloader");
    if (chipHint == QLatin1String("sprd-fdl")) return QStringLiteral("展锐 FDL");
    if (chipHint == QLatin1String("rk")) return QStringLiteral("瑞芯微");
    if (chipHint == QLatin1String("aw-fel")) return QStringLiteral("全志 FEL");
    if (chipHint == QLatin1String("adb")) return QStringLiteral("ADB");
    if (chipHint == QLatin1String("fastboot")) return QStringLiteral("Fastboot");
    return {};
}

PortIdentity classifyPort(const RawDevice& device)
{
    PortIdentity identity;
    const QString description = device.description.toLower();
    const QString friendly = device.friendlyName.toLower();
    const QString service = device.service.toLower();
    const QString ids = device.searchableIds();
    const QString text = description + QLatin1Char(' ') + friendly + QLatin1Char(' ') + service
                         + QLatin1Char(' ') + ids;

    // Bootloader interfaces must be detected before ADB: both use the vendor
    // specific USB class 0xFF / subclass 0x42, only the protocol byte differs
    // (0x01 = adb, 0x03 = fastboot), and some drivers report only the subclass.
    const bool fastbootByText = containsAny(text, {"android bootloader interface",
                                                   "fastboot interface", "bootloader interface"});
    const bool fastbootdByText = containsAny(text, {"fastbootd"});
    const bool fastbootByClass = ids.contains(QStringLiteral("class_ff&subclass_42&prot_03"));
    if (fastbootdByText) {
        identity.kind = PortKind::FastbootD;
        identity.chipHint = QStringLiteral("fastboot");
        identity.label = portKindLabel(PortKind::FastbootD);
        identity.isFastboot = true;
        return identity;
    }
    if (fastbootByText || fastbootByClass || text.contains(QStringLiteral("fastboot"))) {
        identity.kind = PortKind::Fastboot;
        identity.chipHint = QStringLiteral("fastboot");
        identity.label = portKindLabel(PortKind::Fastboot);
        identity.isFastboot = true;
        return identity;
    }

    const bool adbByName = containsAny(text, {"adb interface", "android composite adb", "adb winusb"})
                           || (description.contains(QStringLiteral("adb"))
                               && !description.contains(QStringLiteral("fastboot")))
                           || friendly.contains(QStringLiteral("adb"));
    const bool adbByClass = ids.contains(QStringLiteral("class_ff&subclass_42&prot_01"))
                            || ids.contains(QStringLiteral("android adb"));
    if (adbByName || adbByClass) {
        identity.kind = PortKind::Adb;
        identity.chipHint = QStringLiteral("adb");
        identity.label = portKindLabel(PortKind::Adb);
        identity.isAdb = true;
        return identity;
    }

    if (const VidPidEntry* entry = lookupVidPid(device.vid, device.pid)) {
        identity.kind = entry->kind;
        identity.chipHint = QString::fromLatin1(entry->chipHint);
        identity.label = QString::fromUtf8(entry->label);
        identity.isFlashing = entry->kind == PortKind::QdLoader || entry->kind == PortKind::MtkBrom
                              || entry->kind == PortKind::MtkPreloader
                              || entry->kind == PortKind::SprdFdl || entry->kind == PortKind::Rockchip
                              || entry->kind == PortKind::Allwinner;
        identity.isAdb = entry->kind == PortKind::Adb;
        identity.isFastboot = entry->kind == PortKind::Fastboot;
        return identity;
    }

    // Vendor description/service keywords when VID/PID is not in the table
    // (many brands use their own USB ids for the same boot roms).
    const bool looksQcom = containsAny(description + QLatin1Char(' ') + service,
                                       {"qdloader", "qcusb", "qualcomm hs-usb", "qcser"});
    const bool looksMtk = containsAny(description + QLatin1Char(' ') + service,
                                      {"mediatek", "mtk usb", "preloader"});
    const bool looksSprd = containsAny(description + QLatin1Char(' ') + service,
                                       {"spreadtrum", "sprd", "unisoc"});
    const bool looksRk = containsAny(description + QLatin1Char(' ') + service,
                                     {"rockchip", "rockusb", "rk3"});
    const bool looksAw = containsAny(description + QLatin1Char(' ') + service,
                                     {"allwinner", " aw fel", "aw-usb"});

    if (looksQcom) {
        identity.kind = PortKind::QdLoader;
        identity.chipHint = QStringLiteral("qcom-edl");
        identity.isFlashing = true;
    } else if (looksMtk) {
        const bool preloader = description.contains(QStringLiteral("preloader"));
        identity.kind = preloader ? PortKind::MtkPreloader : PortKind::MtkBrom;
        identity.chipHint = preloader ? QStringLiteral("mtk-preloader") : QStringLiteral("mtk-brom");
        identity.isFlashing = true;
    } else if (looksSprd) {
        identity.kind = PortKind::SprdFdl;
        identity.chipHint = QStringLiteral("sprd-fdl");
        identity.isFlashing = true;
    } else if (looksRk) {
        identity.kind = PortKind::Rockchip;
        identity.chipHint = QStringLiteral("rk");
        identity.isFlashing = true;
    } else if (looksAw) {
        identity.kind = PortKind::Allwinner;
        identity.chipHint = QStringLiteral("aw-fel");
        identity.isFlashing = true;
    } else if (device.comPort > 0) {
        identity.kind = PortKind::Serial;
        identity.label = QStringLiteral("串口 COM%1").arg(device.comPort);
    } else if (containsAny(text, {"mtp", "media transfer", "android composite"})) {
        identity.kind = PortKind::Mtp;
        identity.label = portKindLabel(PortKind::Mtp);
    }

    if (identity.label.isEmpty())
        identity.label = portKindLabel(identity.kind);
    return identity;
}

QString PortRecord::portName() const
{
    if (raw.comPort > 0)
        return QStringLiteral("COM%1").arg(raw.comPort);
    if (!raw.instanceId.isEmpty())
        return raw.instanceId;
    return raw.displayName();
}

QVariantMap PortRecord::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("portName"), portName());
    map.insert(QStringLiteral("comPort"), raw.comPort);
    map.insert(QStringLiteral("description"), raw.displayName());
    map.insert(QStringLiteral("service"), raw.service);
    map.insert(QStringLiteral("instanceId"), raw.instanceId);
    map.insert(QStringLiteral("manufacturer"), raw.manufacturer);
    map.insert(QStringLiteral("location"), raw.locationInfo);
    map.insert(QStringLiteral("vid"), int(raw.vid));
    map.insert(QStringLiteral("pid"), int(raw.pid));
    map.insert(QStringLiteral("vidPid"), formatVidPid(raw.vid, raw.pid));
    map.insert(QStringLiteral("bus"), int(raw.busNumber));
    map.insert(QStringLiteral("address"), int(raw.address));
    map.insert(QStringLiteral("kind"), portKindId(identity.kind));
    map.insert(QStringLiteral("kindLabel"), identity.label);
    map.insert(QStringLiteral("chipHint"), identity.chipHint);
    map.insert(QStringLiteral("chipLabel"), chipHintLabel(identity.chipHint));
    map.insert(QStringLiteral("isFlashing"), identity.isFlashing);
    map.insert(QStringLiteral("isAdb"), identity.isAdb);
    map.insert(QStringLiteral("isFastboot"), identity.isFastboot);
    map.insert(QStringLiteral("hasProblem"), raw.hasProblem);
    map.insert(QStringLiteral("problemCode"), int(raw.problemCode));
    return map;
}

QList<PortRecord> enumeratePorts()
{
    QList<PortRecord> ports;
    const QList<RawDevice> devices = enumeratePresentDevices();
    ports.reserve(devices.size());
    for (const RawDevice& device : devices) {
        PortRecord record;
        record.raw = device;
        record.identity = classifyPort(device);
        // Only surface nodes that are USB / serial / known boot modes.
        const bool relevant = device.isUsb() || device.comPort > 0
                              || record.identity.kind != PortKind::Unknown
                              || device.className.contains(QStringLiteral("Android"), Qt::CaseInsensitive);
        if (!relevant)
            continue;
        ports.append(record);
    }

    std::stable_sort(ports.begin(), ports.end(), [](const PortRecord& a, const PortRecord& b) {
        auto rank = [](const PortRecord& p) {
            if (p.identity.isFlashing)
                return 0;
            if (p.identity.isAdb || p.identity.isFastboot)
                return 1;
            if (p.raw.comPort > 0)
                return 2;
            return 3;
        };
        const int ra = rank(a);
        const int rb = rank(b);
        if (ra != rb)
            return ra < rb;
        return a.portName().compare(b.portName(), Qt::CaseInsensitive) < 0;
    });
    return ports;
}

QList<PortRecord> flashingPorts(const QList<PortRecord>& ports)
{
    QList<PortRecord> out;
    for (const auto& port : ports) {
        if (port.identity.isFlashing)
            out.append(port);
    }
    return out;
}

QList<PortRecord> adbPorts(const QList<PortRecord>& ports)
{
    QList<PortRecord> out;
    for (const auto& port : ports) {
        if (port.identity.isAdb)
            out.append(port);
    }
    return out;
}

QList<PortRecord> fastbootPorts(const QList<PortRecord>& ports)
{
    QList<PortRecord> out;
    for (const auto& port : ports) {
        if (port.identity.isFastboot)
            out.append(port);
    }
    return out;
}

QString portSummaryLine(const PortRecord& port)
{
    const QString hint = port.identity.chipHint.isEmpty()
                             ? QString()
                             : QStringLiteral("（%1）").arg(port.identity.chipHint);
    QString line = QStringLiteral("%1 · %2%3").arg(port.portName(), port.identity.label, hint);
    if (port.raw.hasProblem) {
        if (port.raw.problemCode == 28)
            line += QStringLiteral(" ⚠ 驱动未安装");
        else
            line += QStringLiteral(" ⚠ 设备异常（代码 %1）").arg(port.raw.problemCode);
    }
    return line;
}

QString summarizePorts(const QList<PortRecord>& ports)
{
    if (ports.isEmpty())
        return QStringLiteral("未检测到 USB / 串口设备。请检查数据线连接、USB 调试或设备是否处于刷机模式。");

    QStringList lines;
    lines.append(QStringLiteral("共检测到 %1 个端口：").arg(ports.size()));
    for (const auto& port : ports)
        lines.append(QStringLiteral("  • %1").arg(portSummaryLine(port)));
    return lines.join(QLatin1Char('\n'));
}

} // namespace gf::device
