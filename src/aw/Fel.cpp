#include "Fel.h"

#include "usb/UsbIo.h"

#include <QString>
#include <stdexcept>

namespace gf::aw {

QByteArray Request::encode() const
{
    return proto::u32le(request) + proto::u32le(address) + proto::u32le(length)
           + proto::u32le(reserved);
}

Request Request::decode(const QByteArray& raw)
{
    Request request;
    if (raw.size() < int(kRequestSize))
        return request;
    request.request = proto::readU32le(raw, 0);
    request.address = proto::readU32le(raw, 4);
    request.length = proto::readU32le(raw, 8);
    request.reserved = proto::readU32le(raw, 12);
    return request;
}

Response Response::decode(const QByteArray& raw)
{
    Response response;
    response.raw = raw;
    if (raw.size() < 4) {
        return response;
    }
    response.status = proto::readU32le(raw, 0);
    response.tag = raw.size() >= 8 ? proto::readU32le(raw, 4) : 0;
    response.valid = true;
    return response;
}

QByteArray encodeRequest(quint32 request, quint32 length, quint32 address)
{
    Request req;
    req.request = request;
    req.length = length;
    req.address = address;
    return req.encode();
}

VersionInfo parseVersion(const QByteArray& payload)
{
    VersionInfo info;
    if (payload.size() < 16) {
        info.error = QStringLiteral("FEL 版本信息过短（%1 字节）").arg(payload.size());
        return info;
    }
    info.magic = QString::fromLatin1(payload.left(8)).remove(QChar('\0')).trimmed();
    // AWUSBFEX layout (sunxi): soc_id is encoded in the high 16 bits at 8,
    // protocol version is at 16, scratchpad at 20.
    info.id = (proto::readU32le(payload, 8) >> 8) & 0xffffu;
    info.version = proto::readU32le(payload, 16) & 0xffffu;
    info.firmware = payload.mid(20);
    if (!info.magic.startsWith(QLatin1String("AWUSBFEX"))) {
        info.error = QStringLiteral("FEL 版本魔数错误：%1").arg(info.magic);
        return info;
    }
    info.valid = true;
    return info;
}

QString socName(quint32 id)
{
    // Common Allwinner SoC ids as reported by the BROM. Unknown ids are shown
    // in hex; the list is not exhaustive.
    switch (id) {
    case 0x1623:
        return QStringLiteral("A10");
    case 0x1625:
        return QStringLiteral("A13");
    case 0x1633:
        return QStringLiteral("A31");
    case 0x1639:
        return QStringLiteral("A80");
    case 0x1651:
        return QStringLiteral("A20");
    case 0x1667:
        return QStringLiteral("A33");
    case 0x1673:
        return QStringLiteral("A83T");
    case 0x1680:
        return QStringLiteral("H3");
    case 0x1689:
        return QStringLiteral("A64");
    case 0x1701:
        return QStringLiteral("R40");
    case 0x1718:
        return QStringLiteral("H5");
    case 0x1728:
        return QStringLiteral("H6");
    default:
        return QStringLiteral("SoC_0x%1").arg(id, 8, 16, QLatin1Char('0'));
    }
}

namespace {
QByteArray u32leBytes(quint32 v)
{
    QByteArray b(4, 0);
    b[0] = char(v & 0xff);
    b[1] = char((v >> 8) & 0xff);
    b[2] = char((v >> 16) & 0xff);
    b[3] = char((v >> 24) & 0xff);
    return b;
}
} // namespace

Client::Client(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter))
{
}

void Client::sendUsbRequest(quint8 type, quint32 length, int timeoutMs)
{
    // sunxi aw_usb_request: 8-byte magic, header length, fixed address word,
    // request type, data length, 10 bytes padding. 32 bytes packed.
    QByteArray req;
    req.reserve(kUsbRequestSize);
    req.append(QByteArrayLiteral("AWUC"));
    req.append(4, '\0');
    req.append(u32leBytes(length));
    req.append(QByteArray::fromHex("0000000c")); // 0x0c000000, little endian
    req.append(char(type));
    req.append('\0');
    req.append(u32leBytes(length));
    req.append(10, '\0');
    if (req.size() != int(kUsbRequestSize))
        throw std::runtime_error("FEL USB request 长度错误");
    usb::writeAll(m_transport, req, timeoutMs, m_reporter);
}

void Client::readAck(int timeoutMs)
{
    for (int attempt = 0; attempt < 6; ++attempt) {
        const QByteArray packet = m_rx.isEmpty() ? m_transport.read(4096, timeoutMs) : m_rx;
        m_rx.clear();
        if (packet.isEmpty()) {
            const QString reason = m_transport.errorString();
            throw std::runtime_error(
                QStringLiteral("FEL 等待 AWUS 应答超时%1")
                    .arg(reason.isEmpty() ? QString() : QStringLiteral("：") + reason)
                    .toStdString());
        }
        if (packet.size() >= int(kAckSize) && packet.left(4) == QByteArrayLiteral("AWUS")) {
            if (packet.size() > int(kAckSize))
                m_rx = packet.mid(kAckSize);
            return;
        }
        // Unexpected data before the ACK: discard it and read the next packet.
        // Every write/read phase must end with exactly one 13-byte AWUS packet.
        m_rx.clear();
    }
    throw std::runtime_error("FEL 未收到有效的 AWUS 应答");
}

QByteArray Client::readData(quint32 length, int timeoutMs, bool resyncVersion)
{
    QByteArray out;
    out.reserve(int(length));
    while (out.size() < int(length)) {
        QByteArray packet = m_rx.isEmpty() ? m_transport.read(4096, timeoutMs) : m_rx;
        m_rx.clear();
        if (packet.isEmpty()) {
            const QString reason = m_transport.errorString();
            throw std::runtime_error(
                QStringLiteral("FEL 读取超时（需要 %1 字节，已收到 %2 字节）%3")
                    .arg(length).arg(out.size())
                    .arg(reason.isEmpty() ? QString() : QStringLiteral("：") + reason)
                    .toStdString());
        }
        if (packet.size() >= int(kAckSize) && packet.left(4) == QByteArrayLiteral("AWUS")) {
            if (packet.size() > int(kAckSize))
                m_rx = packet.mid(kAckSize);
            continue;
        }
        if (resyncVersion && out.isEmpty() && !packet.startsWith(QByteArrayLiteral("AWUSBFEX"))) {
            throw std::runtime_error(
                QStringLiteral("FEL 版本应答已降级（收到 %1 字节非 AWUSBFEX 数据）；"
                               "请断开设备 DC 电源后重新进入 FEL")
                    .arg(packet.size()).toStdString());
        }
        out.append(packet);
        if (out.size() > int(length)) {
            m_rx = out.mid(int(length)) + m_rx;
            out.truncate(int(length));
        }
    }
    if (resyncVersion) {
        const int at = out.indexOf(QByteArrayLiteral("AWUSBFEX"));
        if (at < 0)
            throw std::runtime_error("FEL 版本数据中找不到 AWUSBFEX");
        out = out.mid(at);
    }
    return out.left(int(length));
}

void Client::sendFelRequest(quint32 request, quint32 address, quint32 length, int timeoutMs)
{
    Request req;
    req.request = request;
    req.address = address;
    req.length = length;
    // aw_send_fel_request is an AW_USB_WRITE of the 16-byte fel request.
    sendUsbRequest(kUsbWrite, kRequestSize, timeoutMs);
    usb::writeAll(m_transport, req.encode(), timeoutMs, m_reporter);
    readAck(timeoutMs);
}

QByteArray Client::readUsb(quint32 length, int timeoutMs)
{
    sendUsbRequest(kUsbRead, length, timeoutMs);
    const QByteArray data = readData(length, timeoutMs, length == kVersionSize);
    readAck(timeoutMs);
    return data;
}

void Client::writeUsb(const QByteArray& data, int timeoutMs)
{
    sendUsbRequest(kUsbWrite, quint32(data.size()), timeoutMs);
    usb::writeAll(m_transport, data, timeoutMs, m_reporter);
    readAck(timeoutMs);
}

void Client::readFelStatus(int timeoutMs)
{
    const QByteArray status = readUsb(kResponseSize, timeoutMs);
    if (status.size() >= 4 && proto::readU32le(status, 0) != 0) {
        throw std::runtime_error(
            QStringLiteral("FEL 命令失败：状态 0x%1")
                .arg(proto::readU32le(status, 0), 0, 16)
                .toStdString());
    }
}

VersionInfo Client::hello(int timeoutMs)
{
    m_reporter.status(QStringLiteral("FEL 握手（FEL_VER）…"));
    try {
        // Drain a stale ACK left by a previous interrupted transaction; otherwise
        // the BROM refuses the next OUT transfer.
        usb::drainInput(m_transport, 100);
        sendFelRequest(kHello, 0, 0, timeoutMs);
        // FEL_VERSION returns the 32-byte AWUSBFEX payload directly after the
        // request ACK; there is no separate AW_USB_READ header and no trailing
        // 8-byte status packet on this BROM. Sending another read header here
        // makes A133 return a degraded 16-byte zero payload.
        const QByteArray payload = readData(kVersionSize, timeoutMs, true);
        readAck(timeoutMs);
        const VersionInfo info = parseVersion(payload);
        if (!info.valid)
            throw std::runtime_error(QStringLiteral("FEL 握手失败：%1").arg(info.error).toStdString());
        m_reporter.info(QStringLiteral("FEL BootROM：%1，SoC %2（0x%3），版本 0x%4")
                            .arg(info.magic)
                            .arg(socName(info.id))
                            .arg(info.id, 8, 16, QLatin1Char('0'))
                            .arg(info.version, 8, 16, QLatin1Char('0')));
        return info;
    } catch (const std::exception& error) {
        throw std::runtime_error(
            QStringLiteral("%1；FEL 事务可能已中断，请给设备断电重启后重试")
                .arg(QString::fromUtf8(error.what())).toStdString());
    }
}

QByteArray Client::read(quint32 address, quint32 length, int timeoutMs)
{
    if (length == 0)
        return {};
    m_reporter.status(QStringLiteral("FEL 读取 0x%1，%2 字节").arg(address, 0, 16).arg(length));
    QByteArray all;
    quint32 offset = 0;
    while (offset < length) {
        m_reporter.checkCanceled();
        const quint32 chunk = quint32(qMin<quint32>(length - offset, quint32(kMaxChunk)));
        sendFelRequest(kRead, address + offset, chunk, timeoutMs);
        const QByteArray data = readUsb(chunk, timeoutMs);
        readFelStatus(timeoutMs);
        if (data.size() != int(chunk)) {
            throw std::runtime_error(
                QStringLiteral("FEL 读取数据长度不符：期望 %1，实际 %2")
                    .arg(chunk).arg(data.size()).toStdString());
        }
        all.append(data);
        offset += chunk;
    }
    return all;
}

void Client::write(quint32 address, const QByteArray& data, qsizetype chunkSize,
                   int progressBase, int progressSpan, int timeoutMs)
{
    if (data.isEmpty())
        return;
    if (chunkSize <= 0 || chunkSize > kMaxChunk)
        chunkSize = kMaxChunk;
    m_reporter.status(QStringLiteral("FEL 写入 0x%1，%2 字节").arg(address, 0, 16).arg(data.size()));
    for (qsizetype offset = 0; offset < data.size(); offset += chunkSize) {
        m_reporter.checkCanceled();
        const QByteArray chunk = data.mid(offset, chunkSize);
        sendFelRequest(kWrite, address + quint32(offset), quint32(chunk.size()), timeoutMs);
        writeUsb(chunk, timeoutMs);
        readFelStatus(timeoutMs);
        if (progressSpan > 0) {
            const int percent = progressBase
                + int(progressSpan * double(offset + chunk.size()) / data.size());
            m_reporter.progress(qMin(percent, progressBase + progressSpan));
        }
    }
}

void Client::exec(quint32 address, int timeoutMs)
{
    m_reporter.status(QStringLiteral("FEL 执行 0x%1").arg(address, 0, 16));
    sendFelRequest(kExec, address, 0, timeoutMs);
    readFelStatus(timeoutMs);
    m_reporter.info(QStringLiteral("FEL EXEC 已发送"));
}

void Client::run(quint32 address, const QByteArray& image, int timeoutMs)
{
    write(address, image, kMaxChunk, 10, 80, timeoutMs);
    exec(address, timeoutMs);
}

} // namespace gf::aw
