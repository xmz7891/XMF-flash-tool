#include "Da.h"

#include <QElapsedTimer>
#include <QThread>

#include <stdexcept>

namespace gf::mtk::da {

Client::Client(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_link(transport, reporter), m_reporter(std::move(reporter))
{
}

void Client::downloadDa2(const QByteArray& data, quint32 address, quint32 signatureLength,
                         bool jump, int progressBase, int progressSpan)
{
    if (data.isEmpty())
        throw std::runtime_error("MTK DA: DA2 镜像为空");
    m_reporter.status(QStringLiteral("下载 DA2（%1 字节）").arg(data.size()));
    m_link.sendDa(address, data, signatureLength, 15000, progressBase, progressSpan);
    if (jump) {
        m_reporter.status(QStringLiteral("启动 DA2…"));
        m_link.jumpDa(address);
    }
}

quint32 Client::read32(quint32 address)
{
    return m_link.read32(address);
}

quint16 Client::read16(quint32 address)
{
    return m_link.read16(address);
}

void Client::write32(quint32 address, quint32 value, bool verify)
{
    m_link.write32(address, value);
    if (verify && m_link.read32(address) != value) {
        throw std::runtime_error(
            QStringLiteral("MTK DA: 写入 0x%1 校验失败（期望 0x%2）")
                .arg(address, 0, 16)
                .arg(value, 0, 16)
                .toStdString());
    }
}

void Client::write16(quint32 address, quint16 value, bool verify)
{
    m_link.write16(address, value);
    if (verify && m_link.read16(address) != value) {
        throw std::runtime_error(
            QStringLiteral("MTK DA: 写入 0x%1 校验失败（期望 0x%2）")
                .arg(address, 0, 16)
                .arg(value, 0, 16)
                .toStdString());
    }
}

quint32 Client::echo(quint32 value)
{
    return m_link.echo(value);
}

QByteArray Client::getTargetConfig()
{
    return m_link.getTargetConfig();
}

void Client::jump(quint32 address)
{
    m_link.jumpDa(address);
}

usb::PortInfo waitForDaPort(int timeoutMs, const proto::Reporter& reporter)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        reporter.checkCanceled();
        for (const auto& port : usb::enumerateFlashPorts()) {
            if (usb::platformForHint(port.chipHint) == usb::ChipPlatform::MediaTek)
                return port;
        }
        QThread::msleep(250);
    }
    return {};
}

} // namespace gf::mtk::da
