#include "UsbIo.h"

#include <QElapsedTimer>
#include <QString>

namespace gf::usb {

void writeAll(IUsbTransport& transport, const QByteArray& data, int timeoutMs,
              const proto::Reporter& reporter)
{
    qint64 offset = 0;
    while (offset < data.size()) {
        reporter.checkCanceled();
        const QByteArray chunk = data.mid(int(offset), int(data.size() - offset));
        const qint64 written = transport.write(chunk, timeoutMs);
        if (written <= 0) {
            const QString reason = transport.errorString();
            throw std::runtime_error(
                QStringLiteral("USB 写入失败（已写 %1/%2 字节）%3")
                    .arg(offset)
                    .arg(data.size())
                    .arg(reason.isEmpty() ? QString() : QStringLiteral("：") + reason)
                    .toStdString());
        }
        offset += written;
    }
}

QByteArray readExact(IUsbTransport& transport, qint64 size, int timeoutMs,
                     const proto::Reporter& reporter)
{
    if (size < 0)
        throw std::runtime_error("读取长度不能为负数");

    QByteArray out;
    out.reserve(int(size));
    QElapsedTimer timer;
    timer.start();
    int remaining = timeoutMs;
    while (out.size() < size) {
        reporter.checkCanceled();
        const qint64 want = size - out.size();
        const QByteArray chunk = transport.read(want, remaining);
        if (chunk.isEmpty()) {
            const QString reason = transport.errorString();
            throw std::runtime_error(
                QStringLiteral("USB 读取超时（需要 %1 字节，已收到 %2 字节）%3")
                    .arg(size)
                    .arg(out.size())
                    .arg(reason.isEmpty() ? QString() : QStringLiteral("：") + reason)
                    .toStdString());
        }
        out.append(chunk);
        if (timeoutMs > 0) {
            remaining = int(qMax<qint64>(0, timeoutMs - timer.elapsed()));
            if (remaining == 0)
                remaining = 1;
        }
    }
    return out;
}

QByteArray readSome(IUsbTransport& transport, qint64 maxSize, int timeoutMs)
{
    return transport.read(maxSize, timeoutMs);
}

QByteArray readUntil(IUsbTransport& transport, const QByteArray& terminator, int timeoutMs,
                     qsizetype maxSize)
{
    QByteArray out;
    QElapsedTimer timer;
    timer.start();
    while (out.size() < maxSize) {
        if (out.endsWith(terminator))
            break;
        const qint64 remaining = qMax<qint64>(0, timeoutMs - timer.elapsed());
        if (remaining <= 0 && !out.isEmpty())
            break;
        const QByteArray chunk = transport.read(qMin<qint64>(8192, maxSize - out.size()),
                                                int(qMax<qint64>(remaining, 1)));
        if (chunk.isEmpty())
            break;
        out.append(chunk);
    }
    return out;
}

void drainInput(IUsbTransport& transport, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const QByteArray chunk = transport.read(4096, 20);
        if (chunk.isEmpty())
            break;
    }
}

QByteArray readPacket(IUsbTransport& transport, qint64 size, int timeoutMs)
{
    const QByteArray packet = readExact(transport, size, timeoutMs);
    if (packet.size() != size)
        throw std::runtime_error("USB 数据包长度不完整");
    return packet;
}

} // namespace gf::usb
