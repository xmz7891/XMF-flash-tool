#include "U2s.h"

#include "usb/UsbIo.h"

#include <QElapsedTimer>
#include <QString>
#include <stdexcept>

namespace gf::sprd::u2s {

quint16 crc16(const QByteArray& data)
{
    quint16 crc = 0xFFFF;
    for (const char byte : data) {
        crc ^= quint16(quint8(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000)
                crc = quint16((crc << 1) ^ 0x1021);
            else
                crc = quint16(crc << 1);
        }
    }
    return crc;
}

QByteArray escape(const QByteArray& data)
{
    QByteArray out;
    out.reserve(data.size() * 2);
    for (const char byte : data) {
        const quint8 value = quint8(byte);
        if (value == kFlag || value == kEscape) {
            out.append(char(kEscape));
            out.append(char(value ^ kEscapeMask));
        } else {
            out.append(byte);
        }
    }
    return out;
}

QByteArray unescape(const QByteArray& data)
{
    QByteArray out;
    out.reserve(data.size());
    for (qsizetype i = 0; i < data.size(); ++i) {
        const quint8 value = quint8(data[i]);
        if (value != kEscape) {
            out.append(data[i]);
            continue;
        }
        if (i + 1 >= data.size()) {
            out.append(data[i]); // dangling escape, let CRC validation fail
            break;
        }
        out.append(char(quint8(data[++i]) ^ kEscapeMask));
    }
    return out;
}

QByteArray encodeFrame(quint8 type, const QByteArray& payload)
{
    if (payload.size() > kMaxPayload)
        throw std::runtime_error("U2S 帧负载超过 65535 字节");
    QByteArray body;
    body.append(char(type));
    body.append(proto::u16be(quint16(payload.size())));
    body.append(payload);
    body.append(proto::u16be(crc16(body)));

    QByteArray frame;
    frame.reserve(body.size() + 2);
    frame.append(char(kFlag));
    frame.append(escape(body));
    frame.append(char(kFlag));
    return frame;
}

Frame decodeFrame(const QByteArray& raw)
{
    Frame frame;
    const int start = raw.indexOf(char(kFlag));
    if (start < 0) {
        frame.error = QStringLiteral("U2S 帧缺少起始 0x7E");
        return frame;
    }
    const int end = raw.indexOf(char(kFlag), start + 1);
    if (end < 0) {
        frame.error = QStringLiteral("U2S 帧缺少结束 0x7E");
        return frame;
    }
    const QByteArray body = unescape(raw.mid(start + 1, end - start - 1));
    if (body.size() < 5) {
        frame.error = QStringLiteral("U2S 帧太短（%1 字节）").arg(body.size());
        return frame;
    }
    frame.type = quint8(body[0]);
    const quint16 payloadSize = proto::readU16be(body, 1);
    if (qsizetype(payloadSize) + 5 != body.size()) {
        frame.error = QStringLiteral("U2S 帧长度不一致：声明 %1，实际 %2")
                          .arg(payloadSize)
                          .arg(body.size() - 5);
        return frame;
    }
    const quint16 expectedCrc = proto::readU16be(body, body.size() - 2);
    const QByteArray crcRegion = body.left(body.size() - 2);
    if (crc16(crcRegion) != expectedCrc) {
        frame.error = QStringLiteral("U2S CRC 校验失败（期望 0x%1）")
                          .arg(expectedCrc, 4, 16, QLatin1Char('0'));
        return frame;
    }
    frame.payload = body.mid(3, payloadSize);
    frame.valid = true;
    return frame;
}

Link::Link(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter))
{
}

void Link::send(quint8 type, const QByteArray& payload, int timeoutMs)
{
    usb::writeAll(m_transport, encodeFrame(type, payload), timeoutMs, m_reporter);
}

Frame Link::receive(int timeoutMs)
{
    Frame frame;
    QElapsedTimer timer;
    timer.start();

    while (true) {
        m_reporter.checkCanceled();
        const int start = m_buffer.indexOf(char(kFlag));
        if (start >= 0) {
            const int end = m_buffer.indexOf(char(kFlag), start + 1);
            if (end >= 0) {
                const QByteArray raw = m_buffer.mid(start, end - start + 1);
                m_buffer.remove(0, end + 1);
                return decodeFrame(raw);
            }
        }
        const qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0) {
            frame.error = QStringLiteral("等待 U2S 帧超时（%1 ms）").arg(timeoutMs);
            return frame;
        }
        const QByteArray chunk = m_transport.read(4096, int(qMax<qint64>(remaining, 1)));
        if (chunk.isEmpty()) {
            frame.error = QStringLiteral("U2S 接收超时（%1 ms）").arg(timeoutMs);
            return frame;
        }
        m_buffer.append(chunk);
        if (m_buffer.size() > kMaxFrame) {
            frame.error = QStringLiteral("U2S 接收缓冲溢出");
            m_buffer.clear();
            return frame;
        }
    }
}

Frame Link::transact(quint8 type, const QByteArray& payload, int timeoutMs)
{
    send(type, payload, timeoutMs);
    return receive(timeoutMs);
}

} // namespace gf::sprd::u2s
