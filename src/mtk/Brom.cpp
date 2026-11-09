#include "Brom.h"

#include "usb/UsbIo.h"

#include <QString>
#include <stdexcept>

namespace gf::mtk::brom {

namespace {

constexpr qsizetype kSendDaChunk = 64 * 1024;

void requireEcho(const QByteArray& actual, const QByteArray& expected, const char* what)
{
    if (actual == expected)
        return;
    throw std::runtime_error(
        QStringLiteral("MTK BROM %1 回显不一致：写入 %2，读回 %3")
            .arg(QString::fromLatin1(what),
                 QString::fromLatin1(expected.toHex(' ')),
                 QString::fromLatin1(actual.toHex(' ')))
            .toStdString());
}

} // namespace

QByteArray frame(quint8 command, const QByteArray& payload)
{
    QByteArray out;
    out.reserve(int(payload.size()) + 1);
    out.append(char(command));
    out.append(payload);
    return out;
}

Client::Client(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter))
{
}

void Client::sendCommand(quint8 command, const QByteArray& payload, int timeoutMs)
{
    usb::writeAll(m_transport, frame(command, payload), timeoutMs, m_reporter);
}

QByteArray Client::readBytes(qint64 count, int timeoutMs)
{
    return usb::readExact(m_transport, count, timeoutMs, m_reporter);
}

void Client::handshake(int timeoutMs)
{
    m_reporter.status(QStringLiteral("MTK BROM 握手…"));
    usb::writeAll(m_transport, QByteArray(1, char(kHandshakeCommand)), timeoutMs, m_reporter);
    const QByteArray reply = readBytes(1, timeoutMs);
    const quint8 value = quint8(reply.at(0));
    if (value != kHandshakeReply) {
        throw std::runtime_error(
            QStringLiteral("MTK BROM 握手失败：期望 0x5A，实际 0x%1。"
                           "请确认设备已进入 BROM 模式并安装 MTK USB 驱动")
                .arg(value, 2, 16, QLatin1Char('0'))
                .toStdString());
    }
    m_reporter.info(QStringLiteral("MTK BROM 握手成功"));
}

quint16 Client::read16(quint32 address, int timeoutMs)
{
    sendCommand(kCmdRead16, proto::u32be(address), timeoutMs);
    const QByteArray data = readBytes(2, timeoutMs);
    return proto::readU16be(data, 0);
}

quint32 Client::read32(quint32 address, int timeoutMs)
{
    sendCommand(kCmdRead32, proto::u32be(address), timeoutMs);
    const QByteArray data = readBytes(4, timeoutMs);
    return proto::readU32be(data, 0);
}

void Client::write16(quint32 address, quint16 value, int timeoutMs)
{
    const QByteArray expected = proto::u16be(value);
    sendCommand(kCmdWrite16, proto::u32be(address) + expected, timeoutMs);
    requireEcho(readBytes(2, timeoutMs), expected, "write16");
}

void Client::write32(quint32 address, quint32 value, int timeoutMs)
{
    const QByteArray expected = proto::u32be(value);
    sendCommand(kCmdWrite32, proto::u32be(address) + expected, timeoutMs);
    requireEcho(readBytes(4, timeoutMs), expected, "write32");
}

quint32 Client::echo(quint32 value, int timeoutMs)
{
    const QByteArray expected = proto::u32be(value);
    sendCommand(kCmdEcho, expected, timeoutMs);
    const QByteArray actual = readBytes(4, timeoutMs);
    requireEcho(actual, expected, "echo");
    return proto::readU32be(actual, 0);
}

QByteArray Client::getTargetConfig(int timeoutMs)
{
    sendCommand(kCmdGetTargetConfig, {}, timeoutMs);
    return readBytes(8, timeoutMs);
}

void Client::sendDa(quint32 address, const QByteArray& data, quint32 signatureLength, int timeoutMs,
                    int progressBase, int progressSpan)
{
    if (data.isEmpty())
        throw std::runtime_error("MTK BROM: 要发送的 DA 镜像为空");

    m_reporter.status(
        QStringLiteral("发送 DA 到 0x%1（%2 字节）").arg(address, 0, 16).arg(data.size()));
    QByteArray header;
    header += proto::u32be(address);
    header += proto::u32be(quint32(data.size()));
    header += proto::u32be(signatureLength);
    sendCommand(kCmdSendDa, header, timeoutMs);

    for (qsizetype offset = 0; offset < data.size(); offset += kSendDaChunk) {
        m_reporter.checkCanceled();
        const QByteArray chunk = data.mid(offset, kSendDaChunk);
        usb::writeAll(m_transport, chunk, timeoutMs, m_reporter);
        if (progressSpan > 0) {
            const int percent =
                progressBase + int(progressSpan * double(offset + chunk.size()) / data.size());
            m_reporter.progress(qMin(percent, progressBase + progressSpan));
        }
    }

    const QByteArray status = readBytes(2, timeoutMs);
    if (status != QByteArray(2, '\0')) {
        throw std::runtime_error(
            QStringLiteral("MTK BROM SEND_DA 返回状态 %1（期望 0x0000）")
                .arg(QString::fromLatin1(status.toHex(' ')))
                .toStdString());
    }
}

void Client::jumpDa(quint32 address, int timeoutMs)
{
    m_reporter.info(QStringLiteral("跳转到 DA：0x%1").arg(address, 0, 16));
    sendCommand(kCmdJumpDa, proto::u32be(address), timeoutMs);
}

void Client::jumpBl(quint32 address, int timeoutMs)
{
    m_reporter.info(QStringLiteral("跳转到 BootLoader：0x%1").arg(address, 0, 16));
    sendCommand(kCmdJumpBl, proto::u32be(address), timeoutMs);
}

void Client::jump(quint32 address, int timeoutMs)
{
    m_reporter.info(QStringLiteral("跳转到地址：0x%1").arg(address, 0, 16));
    sendCommand(kCmdJump, proto::u32be(address), timeoutMs);
}

} // namespace gf::mtk::brom
