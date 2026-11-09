#include "Fel.h"

#include "usb/UsbIo.h"

#include <QString>
#include <stdexcept>

namespace gf::aw {

QByteArray Request::encode() const
{
    return proto::u32le(request) + proto::u32le(length) + proto::u32le(address)
           + proto::u32le(reserved);
}

Request Request::decode(const QByteArray& raw)
{
    Request request;
    if (raw.size() < int(kRequestSize))
        return request;
    request.request = proto::readU32le(raw, 0);
    request.length = proto::readU32le(raw, 4);
    request.address = proto::readU32le(raw, 8);
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
    info.version = proto::readU32le(payload, 8);
    info.id = proto::readU32le(payload, 12);
    info.firmware = payload.mid(16);
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

Client::Client(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter))
{
}

Response Client::readResponse(int timeoutMs)
{
    // One bulk transfer is enough for the 8-byte response; accepting a short
    // 4-byte status keeps compatibility with older BROM builds.
    const QByteArray raw = m_transport.read(kResponseSize, timeoutMs);
    Response response = Response::decode(raw);
    if (!response.valid) {
        const QString reason = m_transport.errorString();
        throw std::runtime_error(
            QStringLiteral("FEL 未收到响应（%1 ms）%2")
                .arg(timeoutMs)
                .arg(reason.isEmpty() ? QString() : QStringLiteral("：") + reason)
                .toStdString());
    }
    return response;
}

Response Client::transfer(const Request& request, const QByteArray& outData, QByteArray* inData,
                          int timeoutMs)
{
    m_reporter.checkCanceled();
    usb::writeAll(m_transport, request.encode(), timeoutMs, m_reporter);

    if (request.request == kWrite && !outData.isEmpty())
        usb::writeAll(m_transport, outData, timeoutMs, m_reporter);

    QByteArray data;
    if (request.request == kRead && request.length > 0)
        data = usb::readExact(m_transport, request.length, timeoutMs, m_reporter);
    else if (inData && request.length > 0)
        data = usb::readExact(m_transport, request.length, timeoutMs, m_reporter);

    const Response response = readResponse(timeoutMs);
    if (!response.ok()) {
        throw std::runtime_error(
            QStringLiteral("FEL 命令 0x%1 失败：状态 0x%2")
                .arg(request.request, 0, 16)
                .arg(response.status, 0, 16)
                .toStdString());
    }
    if (inData)
        *inData = data;
    return response;
}

VersionInfo Client::hello(int timeoutMs)
{
    m_reporter.status(QStringLiteral("FEL 握手（FEL_VER）…"));
    Request request;
    request.request = kHello;
    request.length = kVersionSize;

    QByteArray payload;
    transfer(request, {}, &payload, timeoutMs);
    const VersionInfo info = parseVersion(payload);
    if (!info.valid)
        throw std::runtime_error(QStringLiteral("FEL 握手失败：%1").arg(info.error).toStdString());
    m_reporter.info(QStringLiteral("FEL BootROM：%1，SoC %2（0x%3），版本 0x%4")
                        .arg(info.magic)
                        .arg(socName(info.id))
                        .arg(info.id, 8, 16, QLatin1Char('0'))
                        .arg(info.version, 8, 16, QLatin1Char('0')));
    return info;
}

QByteArray Client::read(quint32 address, quint32 length, int timeoutMs)
{
    if (length == 0)
        return {};
    m_reporter.status(
        QStringLiteral("FEL 读取 0x%1，%2 字节").arg(address, 0, 16).arg(length));
    QByteArray all;
    quint32 offset = 0;
    while (offset < length) {
        m_reporter.checkCanceled();
        const quint32 chunk = quint32(qMin<quint32>(length - offset, quint32(kMaxChunk)));
        Request request;
        request.request = kRead;
        request.length = chunk;
        request.address = address + offset;
        QByteArray data;
        transfer(request, {}, &data, timeoutMs);
        if (data.size() != int(chunk)) {
            throw std::runtime_error(
                QStringLiteral("FEL 读取数据长度不符：期望 %1，实际 %2")
                    .arg(chunk)
                    .arg(data.size())
                    .toStdString());
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
        Request request;
        request.request = kWrite;
        request.length = quint32(chunk.size());
        request.address = address + quint32(offset);
        transfer(request, chunk, nullptr, timeoutMs);
        if (progressSpan > 0) {
            const int percent =
                progressBase + int(progressSpan * double(offset + chunk.size()) / data.size());
            m_reporter.progress(qMin(percent, progressBase + progressSpan));
        }
    }
}

void Client::exec(quint32 address, int timeoutMs)
{
    m_reporter.status(QStringLiteral("FEL 执行 0x%1").arg(address, 0, 16));
    Request request;
    request.request = kExec;
    request.address = address;
    transfer(request, {}, nullptr, timeoutMs);
    m_reporter.info(QStringLiteral("FEL EXEC 已发送"));
}

void Client::run(quint32 address, const QByteArray& image, int timeoutMs)
{
    write(address, image, kMaxChunk, 10, 80, timeoutMs);
    exec(address, timeoutMs);
}

} // namespace gf::aw
