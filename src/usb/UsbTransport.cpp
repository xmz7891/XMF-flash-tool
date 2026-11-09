#include "UsbTransport.h"

#include <QList>
#include <QString>

#ifdef _WIN32
#    include "WinUsbTransport.h"
#endif

namespace gf::usb {

namespace {

struct UsbIdRule {
    quint16 vid;
    quint16 pid;      // 0xFFFF = any PID for this vendor
    const char* hint;
    const char* note;
};

// USB IDs used by the supported flashing modes. A zero (0x0000/0x0000) pair is
// reported by some hosts when the descriptor could not be read and never maps
// to a chip. Keep these in sync with the tool messages in ProtocolSupport.cpp.
constexpr UsbIdRule kIdRules[] = {
    // Qualcomm EDL (Sahara): "Qualcomm HS-USB QDLoader 9008" and siblings.
    {0x05C6, 0x9008, "qcom-edl", "Qualcomm EDL 9008"},
    {0x05C6, 0x900E, "qcom-edl", "Qualcomm EDL 900E (memory debug)"},
    {0x05C6, 0x901D, "qcom-edl", "Qualcomm EDL 901D"},
    {0x05C6, 0x9091, "qcom-edl", "Qualcomm EDL 9091"},
    {0x05C6, 0x9006, "qcom-edl", "Qualcomm EDL 9006"},

    // MediaTek: BROM is 0E8D:0003; PreLoader/DA enumerate through the MTK USB
    // port driver as 0E8D:2000 / 0E8D:2001.
    {0x0E8D, 0x0003, "mtk-brom", "MediaTek BROM"},
    {0x0E8D, 0x2000, "mtk-preloader", "MediaTek PreLoader/DA"},
    {0x0E8D, 0x2001, "mtk-da", "MediaTek DA"},

    // Unisoc / Spreadtrum U2S (FDL download mode): vendor 1782.
    {0x1782, 0xFFFF, "sprd-fdl", "Spreadtrum U2S/FDL"},

    // Rockchip: MaskROM / Loader mode is vendor 2207 with model specific PIDs.
    {0x2207, 0xFFFF, "rk", "Rockchip MaskROM/Loader"},

    // Allwinner FEL: 1F3A:EFE8 ("efe8") is the classic FEL id.
    {0x1F3A, 0xEFE8, "aw-fel", "Allwinner FEL"},
    {0x1F3A, 0xFFFF, "aw-fel", "Allwinner FEL"},
};

} // namespace

QString chipHintForIds(quint16 vid, quint16 pid)
{
    if (vid == 0 && pid == 0)
        return {};

    for (const auto& rule : kIdRules) {
        if (rule.vid != vid)
            continue;
        if (rule.pid != 0xFFFF && rule.pid != pid)
            continue;
        return QString::fromLatin1(rule.hint);
    }
    return {};
}

ChipPlatform platformForHint(const QString& hint)
{
    if (hint == QLatin1String("qcom-edl"))
        return ChipPlatform::Qualcomm;
    if (hint == QLatin1String("mtk-brom") || hint == QLatin1String("mtk-preloader")
        || hint == QLatin1String("mtk-da") || hint.startsWith(QLatin1String("mtk-")))
        return ChipPlatform::MediaTek;
    if (hint == QLatin1String("sprd-fdl"))
        return ChipPlatform::Spreadtrum;
    if (hint == QLatin1String("rk"))
        return ChipPlatform::Rockchip;
    if (hint == QLatin1String("aw-fel"))
        return ChipPlatform::Allwinner;
    return ChipPlatform::Unknown;
}

IUsbTransport* createTransport(const PortInfo& port)
{
#ifdef _WIN32
    // The transport is created closed; the caller invokes open(port) so a
    // missing device can be reported through a normal error path.
    if (port.devicePath.isEmpty() && port.chipHint.isEmpty())
        return nullptr;
    return new WinUsbTransport();
#else
    Q_UNUSED(port);
    return nullptr;
#endif
}

#ifndef _WIN32
QList<PortInfo> enumerateFlashPorts()
{
    // Enumeration is Windows-only in this product. Returning an empty list on
    // other hosts keeps every protocol layer testable with MockTransport.
    return {};
}
#endif

} // namespace gf::usb
