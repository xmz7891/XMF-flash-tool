#include "ProtocolSupport.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <memory>

namespace gf::proto {

void Reporter::progress(int percent, const QString& status) const
{
    if (m_progress)
        m_progress(percent, status);
}

void Reporter::debug(const QString& message) const
{
    if (m_log)
        m_log(Level::Debug, message);
}

void Reporter::info(const QString& message) const
{
    if (m_log)
        m_log(Level::Info, message);
}

void Reporter::warn(const QString& message) const
{
    if (m_log)
        m_log(Level::Warn, message);
}

void Reporter::error(const QString& message) const
{
    if (m_log)
        m_log(Level::Error, message);
}

bool Reporter::isCanceled() const
{
    return m_canceled && m_canceled();
}

void Reporter::checkCanceled() const
{
    if (isCanceled())
        throw Canceled();
}

Reporter Reporter::silent()
{
    return Reporter();
}

QString platformName(usb::ChipPlatform platform)
{
    switch (platform) {
    case usb::ChipPlatform::Qualcomm:
        return QStringLiteral("高通 9008");
    case usb::ChipPlatform::MediaTek:
        return QStringLiteral("联发科 BROM");
    case usb::ChipPlatform::Spreadtrum:
        return QStringLiteral("紫光展锐 U2S");
    case usb::ChipPlatform::Rockchip:
        return QStringLiteral("瑞芯微 RockUSB");
    case usb::ChipPlatform::Allwinner:
        return QStringLiteral("全志 FEL");
    case usb::ChipPlatform::Unknown:
        break;
    }
    return QStringLiteral("未知平台");
}

QString platformKey(usb::ChipPlatform platform)
{
    switch (platform) {
    case usb::ChipPlatform::Qualcomm:
        return QStringLiteral("qcom");
    case usb::ChipPlatform::MediaTek:
        return QStringLiteral("mtk");
    case usb::ChipPlatform::Spreadtrum:
        return QStringLiteral("sprd");
    case usb::ChipPlatform::Rockchip:
        return QStringLiteral("rk");
    case usb::ChipPlatform::Allwinner:
        return QStringLiteral("aw");
    case usb::ChipPlatform::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString platformChipHint(usb::ChipPlatform platform)
{
    switch (platform) {
    case usb::ChipPlatform::Qualcomm:
        return QStringLiteral("qcom-edl");
    case usb::ChipPlatform::MediaTek:
        return QStringLiteral("mtk-brom");
    case usb::ChipPlatform::Spreadtrum:
        return QStringLiteral("sprd-fdl");
    case usb::ChipPlatform::Rockchip:
        return QStringLiteral("rk");
    case usb::ChipPlatform::Allwinner:
        return QStringLiteral("aw-fel");
    case usb::ChipPlatform::Unknown:
        break;
    }
    return {};
}

QString noDeviceMessage(usb::ChipPlatform platform)
{
    switch (platform) {
    case usb::ChipPlatform::Qualcomm:
        return QStringLiteral("未检测到 9008 端口，请确认设备已进入 EDL 模式并安装驱动");
    case usb::ChipPlatform::MediaTek:
        return QStringLiteral("未检测到 MTK BROM/PreLoader 端口，请确认设备已进入 BROM 模式"
                              "（关机后按住音量键连接）并安装驱动");
    case usb::ChipPlatform::Spreadtrum:
        return QStringLiteral("未检测到展锐 U2S/FDL 端口，请确认设备已进入下载模式并安装驱动");
    case usb::ChipPlatform::Rockchip:
        return QStringLiteral("未检测到 Rockchip MaskROM/Loader 端口（VID 2207），"
                              "请确认设备已进入 Loader 模式并安装驱动");
    case usb::ChipPlatform::Allwinner:
        return QStringLiteral("未检测到全志 FEL 端口（VID 1F3A），请确认设备已进入 FEL 模式并安装驱动");
    case usb::ChipPlatform::Unknown:
        break;
    }
    return QStringLiteral("未检测到可用的刷机端口，请确认设备已进入对应下载模式并安装驱动");
}

QList<usb::PortInfo> portsFor(usb::ChipPlatform platform)
{
    QList<usb::PortInfo> out;
    const QString hint = platformChipHint(platform);
    for (const auto& port : usb::enumerateFlashPorts()) {
        if (port.chipHint == hint || usb::platformForHint(port.chipHint) == platform)
            out.append(port);
    }
    return out;
}

usb::PortInfo requirePort(usb::ChipPlatform platform, const QString& preferredDevicePath,
                          const Reporter& reporter)
{
    if (!preferredDevicePath.trimmed().isEmpty()) {
        usb::PortInfo port;
        port.devicePath = preferredDevicePath.trimmed();
        port.portName = QFileInfo(port.devicePath).fileName();
        port.description = QStringLiteral("用户指定的设备接口");
        port.chipHint = platformChipHint(platform);
        reporter.info(QStringLiteral("使用指定设备接口：%1").arg(port.devicePath));
        return port;
    }

    const QList<usb::PortInfo> ports = portsFor(platform);
    if (ports.isEmpty())
        throw std::runtime_error(noDeviceMessage(platform).toStdString());

    if (ports.size() > 1) {
        reporter.warn(QStringLiteral("检测到 %1 个 %2 端口，默认使用第一个：%3")
                          .arg(ports.size())
                          .arg(platformName(platform))
                          .arg(ports.first().devicePath));
    }
    reporter.info(QStringLiteral("已发现 %1：VID=%2 PID=%3 %4")
                      .arg(platformName(platform))
                      .arg(ports.first().vid, 4, 16, QLatin1Char('0'))
                      .arg(ports.first().pid, 4, 16, QLatin1Char('0'))
                      .arg(ports.first().description));
    return ports.first();
}

std::unique_ptr<usb::IUsbTransport> openTransport(usb::ChipPlatform platform,
                                                  const QString& preferredDevicePath,
                                                  const Reporter& reporter)
{
    const usb::PortInfo port = requirePort(platform, preferredDevicePath, reporter);
    std::unique_ptr<usb::IUsbTransport> transport(usb::createTransport(port));
    if (!transport) {
        throw std::runtime_error(
            QStringLiteral("当前系统未提供 %1 的 USB 传输实现（仅 Windows 支持 WinUSB）")
                .arg(platformName(platform))
                .toStdString());
    }
    reporter.status(QStringLiteral("打开 %1 端口…").arg(platformName(platform)));
    if (!transport->open(port)) {
        const QString reason = transport->errorString();
        throw std::runtime_error(
            QStringLiteral("打开 %1 端口失败：%2").arg(platformName(platform), reason).toStdString());
    }
    return transport;
}

QByteArray u16le(quint16 value)
{
    QByteArray out(2, Qt::Uninitialized);
    out[0] = char(value & 0xFF);
    out[1] = char((value >> 8) & 0xFF);
    return out;
}

QByteArray u32le(quint32 value)
{
    QByteArray out(4, Qt::Uninitialized);
    for (int i = 0; i < 4; ++i)
        out[i] = char((value >> (8 * i)) & 0xFF);
    return out;
}

QByteArray u64le(quint64 value)
{
    QByteArray out(8, Qt::Uninitialized);
    for (int i = 0; i < 8; ++i)
        out[i] = char((value >> (8 * i)) & 0xFF);
    return out;
}

QByteArray u16be(quint16 value)
{
    QByteArray out(2, Qt::Uninitialized);
    out[0] = char((value >> 8) & 0xFF);
    out[1] = char(value & 0xFF);
    return out;
}

QByteArray u32be(quint32 value)
{
    QByteArray out(4, Qt::Uninitialized);
    for (int i = 0; i < 4; ++i)
        out[i] = char((value >> (8 * (3 - i))) & 0xFF);
    return out;
}

QByteArray u64be(quint64 value)
{
    QByteArray out(8, Qt::Uninitialized);
    for (int i = 0; i < 8; ++i)
        out[i] = char((value >> (8 * (7 - i))) & 0xFF);
    return out;
}

namespace {
bool sliceOk(const QByteArray& data, qsizetype offset, qsizetype width)
{
    return offset >= 0 && width > 0 && offset + width <= data.size();
}
} // namespace

quint16 readU16le(const QByteArray& data, qsizetype offset, bool* ok)
{
    if (!sliceOk(data, offset, 2)) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    return quint16(quint8(data[offset]) | (quint16(quint8(data[offset + 1])) << 8));
}

quint32 readU32le(const QByteArray& data, qsizetype offset, bool* ok)
{
    if (!sliceOk(data, offset, 4)) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    quint32 value = 0;
    for (int i = 0; i < 4; ++i)
        value |= quint32(quint8(data[offset + i])) << (8 * i);
    return value;
}

quint64 readU64le(const QByteArray& data, qsizetype offset, bool* ok)
{
    if (!sliceOk(data, offset, 8)) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    quint64 value = 0;
    for (int i = 0; i < 8; ++i)
        value |= quint64(quint8(data[offset + i])) << (8 * i);
    return value;
}

quint16 readU16be(const QByteArray& data, qsizetype offset, bool* ok)
{
    if (!sliceOk(data, offset, 2)) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    return quint16((quint16(quint8(data[offset])) << 8) | quint8(data[offset + 1]));
}

quint32 readU32be(const QByteArray& data, qsizetype offset, bool* ok)
{
    if (!sliceOk(data, offset, 4)) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    quint32 value = 0;
    for (int i = 0; i < 4; ++i)
        value = (value << 8) | quint32(quint8(data[offset + i]));
    return value;
}

quint64 readU64be(const QByteArray& data, qsizetype offset, bool* ok)
{
    if (!sliceOk(data, offset, 8)) {
        if (ok)
            *ok = false;
        return 0;
    }
    if (ok)
        *ok = true;
    quint64 value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | quint64(quint8(data[offset + i]));
    return value;
}

} // namespace gf::proto
