#include "Sahara.h"

#include "usb/UsbIo.h"

#include <QString>
#include <stdexcept>

namespace gf::qcom::sahara {

namespace {

QByteArray idStatusPayload(quint32 imageId, quint32 status)
{
    return proto::u32le(imageId) + proto::u32le(status);
}

QString hex(quint32 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

} // namespace

QByteArray buildPacket(quint32 command, const QByteArray& payload)
{
    return proto::u32le(command) + proto::u32le(quint32(8 + payload.size())) + payload;
}

QByteArray buildHelloPacket(quint32 version, quint32 minVersion, quint32 maxCommandLength,
                            quint32 mode)
{
    QByteArray payload;
    payload += proto::u32le(version);
    payload += proto::u32le(minVersion);
    payload += proto::u32le(maxCommandLength);
    payload += proto::u32le(mode);
    for (int i = 0; i < 6; ++i)
        payload += proto::u32le(0); // reserved
    return buildPacket(kCmdHello, payload); // total 48 bytes
}

QByteArray buildHelloResponse(const DeviceHello& hello)
{
    QByteArray payload;
    payload += proto::u32le(hello.version);
    payload += proto::u32le(hello.minVersion);
    payload += proto::u32le(hello.maxCommandLength);
    payload += proto::u32le(hello.mode);
    for (int i = 0; i < 6; ++i)
        payload += proto::u32le(0);
    return buildPacket(kCmdHelloResponse, payload);
}

QByteArray buildReadDataRequest(quint32 byteCount, bool is64)
{
    // Special case of the Sahara header: `length` carries the requested number
    // of image bytes, the packet itself has no payload.
    return proto::u32le(is64 ? kCmdReadData64 : kCmdReadData) + proto::u32le(byteCount);
}

QByteArray buildEndImageTransfer(quint32 imageId, bool is64)
{
    return buildPacket(is64 ? kCmdEndImageTransfer64 : kCmdEndImageTransfer,
                       idStatusPayload(imageId, 0));
}

QByteArray buildDonePacket(quint32 imageId, quint32 status, bool is64)
{
    return buildPacket(is64 ? kCmdDone64 : kCmdDone, idStatusPayload(imageId, status));
}

QByteArray buildDoneResponse(quint32 imageId, quint32 status, bool is64)
{
    return buildPacket(is64 ? kCmdDoneResponse64 : kCmdDoneResponse,
                       idStatusPayload(imageId, status));
}

QByteArray buildExecutePacket(quint64 address, quint32 imageId, bool is64)
{
    QByteArray payload = proto::u32le(imageId);
    payload += is64 ? proto::u64le(address) : proto::u32le(quint32(address));
    return buildPacket(is64 ? kCmdExecute64 : kCmdExecute, payload);
}

QByteArray buildExecuteResponsePacket(quint32 imageId, quint32 status, bool is64)
{
    return buildPacket(is64 ? kCmdExecuteResponse64 : kCmdExecuteResponse,
                       idStatusPayload(imageId, status));
}

PacketHeader parseHeader(const QByteArray& bytes)
{
    PacketHeader header;
    if (bytes.size() < 8)
        return header;
    header.command = proto::readU32le(bytes, 0);
    header.length = proto::readU32le(bytes, 4);
    return header;
}

Client::Client(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter))
{
}

PacketHeader Client::readHeader(int timeoutMs)
{
    return parseHeader(usb::readExact(m_transport, 8, timeoutMs, m_reporter));
}

QByteArray Client::readBody(quint32 totalLength, int timeoutMs)
{
    if (totalLength < 8)
        throw std::runtime_error("Sahara 数据包长度非法（小于 8 字节头）");
    return usb::readExact(m_transport, qint64(totalLength) - 8, timeoutMs, m_reporter);
}

void Client::sendPacket(quint32 command, const QByteArray& payload, int timeoutMs)
{
    usb::writeAll(m_transport, buildPacket(command, payload), timeoutMs, m_reporter);
}

void Client::sendEndImageTransfer(quint32 imageId, bool is64)
{
    usb::writeAll(m_transport, buildEndImageTransfer(imageId, is64), 5000, m_reporter);
}

void Client::sendDoneResponse(quint32 imageId, quint32 status, bool is64)
{
    usb::writeAll(m_transport, buildDoneResponse(imageId, status, is64), 5000, m_reporter);
}

DeviceHello Client::handshake(int timeoutMs)
{
    m_reporter.status(QStringLiteral("等待 Sahara HELLO…"));
    const PacketHeader header = readHeader(timeoutMs);
    if (header.command != kCmdHello) {
        throw std::runtime_error(
            QStringLiteral("Sahara 握手失败：期望 HELLO(0x01)，实际收到 %1。"
                           "请确认设备处于 9008/EDL 模式")
                .arg(hex(header.command))
                .toStdString());
    }
    if (header.length < 8 || header.length > 1024) {
        throw std::runtime_error(
            QStringLiteral("Sahara HELLO 长度异常：%1").arg(header.length).toStdString());
    }

    const QByteArray body = readBody(header.length, timeoutMs);
    DeviceHello hello;
    hello.valid = true;
    if (body.size() >= 16) {
        hello.version = proto::readU32le(body, 0);
        hello.minVersion = proto::readU32le(body, 4);
        hello.maxCommandLength = proto::readU32le(body, 8);
        hello.mode = proto::readU32le(body, 12);
    } else {
        // Very old targets send a bare HELLO; assume the v2 defaults.
        hello.version = 2;
        hello.minVersion = 1;
        hello.maxCommandLength = kDefaultMaxCommandLength;
        hello.mode = kModeImageTxPending;
    }
    if (hello.version == 0 || hello.version > 16) {
        throw std::runtime_error(
            QStringLiteral("Sahara HELLO 版本异常：%1").arg(hello.version).toStdString());
    }

    DeviceHello response;
    response.valid = true;
    response.version = qMin<quint32>(hello.version, 3); // host implements up to v3
    if (hello.minVersion > response.version)
        response.version = hello.minVersion;
    response.minVersion = hello.minVersion;
    response.maxCommandLength =
        qMin<quint32>(hello.maxCommandLength ? hello.maxCommandLength : kDefaultMaxCommandLength,
                      kHostMaxCommandLength);
    response.mode = hello.mode;

    m_reporter.info(QStringLiteral("Sahara 目标信息：协议版本 %1（最低 %2），最大命令长度 %3，模式 %4")
                        .arg(hello.version)
                        .arg(hello.minVersion)
                        .arg(hello.maxCommandLength)
                        .arg(hello.mode));
    usb::writeAll(m_transport, buildHelloResponse(response), timeoutMs, m_reporter);
    m_device = hello;
    m_handshakeDone = true;
    m_reporter.status(QStringLiteral("Sahara 握手完成"));
    return hello;
}

Client::UploadResult Client::upload(const QByteArray& image, int timeoutMs, int progressBase,
                                    int progressSpan)
{
    UploadResult result;
    try {
        if (!m_handshakeDone)
            handshake(timeoutMs);
        if (image.isEmpty()) {
            result.error = QStringLiteral("programmer 镜像为空");
            return result;
        }

        m_image = image;
        const qint64 total = image.size();
        qint64 offset = 0;
        m_reporter.status(QStringLiteral("通过 Sahara 上传 programmer（%1 字节）").arg(total));

        while (true) {
            m_reporter.checkCanceled();
            const PacketHeader header = readHeader(timeoutMs);

            if (header.command == kCmdReadData || header.command == kCmdReadData64) {
                const bool is64 = header.command == kCmdReadData64;
                if (header.length == 0) {
                    result.error = QStringLiteral("Sahara 设备请求了 0 字节数据");
                    return result;
                }
                if (offset >= total) {
                    result.error = QStringLiteral("Sahara 设备请求的数据超出镜像大小（已发送 %1/%2）")
                                       .arg(offset)
                                       .arg(total);
                    return result;
                }
                const qint64 chunk = qMin<qint64>(header.length, total - offset);
                usb::writeAll(m_transport, image.mid(int(offset), int(chunk)), timeoutMs,
                              m_reporter);
                offset += chunk;
                sendEndImageTransfer(0, is64);
                result.bytesTransferred = offset;
                m_reporter.progress(progressBase + int(progressSpan * (double(offset) / total)));
                continue;
            }

            if (header.command == kCmdDone || header.command == kCmdDone64) {
                const bool is64 = header.command == kCmdDone64;
                const QByteArray body = readBody(header.length, timeoutMs);
                quint32 imageId = 0;
                quint32 status = 0;
                if (body.size() >= 8) {
                    imageId = proto::readU32le(body, 0);
                    status = proto::readU32le(body, 4);
                }
                sendDoneResponse(imageId, status, is64);
                result.imageId = imageId;
                result.status = status;
                result.success = (status == 0);
                result.bytesTransferred = offset;
                if (!result.success)
                    result.error = QStringLiteral("Sahara 返回失败状态 %1").arg(hex(status));
                else
                    m_reporter.info(QStringLiteral("Sahara 镜像传输完成（%1 字节）").arg(offset));
                return result;
            }

            if (header.command == kCmdExecuteResponse || header.command == kCmdExecuteResponse64) {
                const bool is64 = header.command == kCmdExecuteResponse64;
                Q_UNUSED(is64);
                const QByteArray body = readBody(header.length, timeoutMs);
                quint32 status = body.size() >= 8 ? proto::readU32le(body, 4) : 0;
                result.success = (status == 0);
                result.status = status;
                result.bytesTransferred = offset;
                if (!result.success)
                    result.error = QStringLiteral("Sahara 执行返回状态 %1").arg(hex(status));
                return result;
            }

            if (header.command == kCmdReadyForImage) {
                // Informational packet on some targets; no reply expected.
                if (header.length > 8)
                    readBody(header.length, timeoutMs);
                continue;
            }

            result.error = QStringLiteral("Sahara 未预期的命令 %1（期望 READ_DATA/DONE）")
                               .arg(hex(header.command));
            return result;
        }
    } catch (const proto::Canceled&) {
        throw;
    } catch (const std::exception& error) {
        result.error = QString::fromUtf8(error.what());
        return result;
    }
}

void Client::execute(quint64 address, quint32 imageId, int timeoutMs)
{
    const bool is64 = address > 0xFFFFFFFFull || m_device.version >= 3;
    m_reporter.info(QStringLiteral("Sahara EXECUTE 地址 0x%1").arg(address, 0, 16));
    usb::writeAll(m_transport, buildExecutePacket(address, imageId, is64), timeoutMs, m_reporter);

    const PacketHeader header = readHeader(timeoutMs);
    const quint32 expected = is64 ? kCmdExecuteResponse64 : kCmdExecuteResponse;
    if (header.command != expected) {
        throw std::runtime_error(
            QStringLiteral("Sahara EXECUTE 响应异常：期望 %1，实际 %2")
                .arg(hex(expected), hex(header.command))
                .toStdString());
    }
    const QByteArray body = readBody(header.length, timeoutMs);
    const quint32 status = body.size() >= 8 ? proto::readU32le(body, 4) : 0;
    if (status != 0) {
        throw std::runtime_error(
            QStringLiteral("Sahara EXECUTE 失败，状态 %1").arg(hex(status)).toStdString());
    }
}

void Client::reset(bool waitForResponse, int timeoutMs)
{
    sendPacket(kCmdReset, {}, timeoutMs);
    m_handshakeDone = false;
    if (!waitForResponse)
        return;
    const PacketHeader header = readHeader(timeoutMs);
    if (header.command != kCmdResetResponse) {
        throw std::runtime_error(
            QStringLiteral("Sahara RESET 响应异常：收到 %1").arg(hex(header.command)).toStdString());
    }
}

} // namespace gf::qcom::sahara
