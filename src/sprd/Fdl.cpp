#include "Fdl.h"

#include "usb/UsbIo.h"

#include <QElapsedTimer>
#include <QThread>

#include <stdexcept>

namespace gf::sprd::fdl {

QByteArray encodeAddressSize(quint32 address, quint32 size)
{
    return proto::u32be(address) + proto::u32be(size);
}

Client::Client(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_link(transport, reporter), m_reporter(std::move(reporter))
{
}

u2s::Frame Client::command(quint8 type, const QByteArray& payload, int timeoutMs)
{
    m_reporter.checkCanceled();
    const u2s::Frame frame = m_link.transact(type, payload, timeoutMs);
    if (!frame.valid) {
        throw std::runtime_error(
            QStringLiteral("SPRD FDL 命令 0x%1 失败：%2")
                .arg(type, 2, 16, QLatin1Char('0'))
                .arg(frame.error)
                .toStdString());
    }
    return frame;
}

void Client::expectOk(quint8 command, const u2s::Frame& frame, const QString& action)
{
    Q_UNUSED(command);
    if (frame.payload.isEmpty())
        return; // some FDL revisions answer an empty status frame
    const quint8 status = quint8(frame.payload.at(0));
    if (status == 0)
        return;
    throw std::runtime_error(
        QStringLiteral("SPRD %1 失败：状态 0x%2 %3")
            .arg(action)
            .arg(status, 2, 16, QLatin1Char('0'))
            .arg(frame.payload.size() > 1
                     ? QStringLiteral("（%1）")
                           .arg(QString::fromLatin1(frame.payload.mid(1).toHex(' ')))
                     : QString())
            .toStdString());
}

void Client::connect(int timeoutMs)
{
    m_reporter.status(QStringLiteral("U2S/FDL 连接中…"));
    const u2s::Frame frame = command(kConnect, {}, timeoutMs);
    expectOk(kConnect, frame, QStringLiteral("CONNECT"));
    m_reporter.info(QStringLiteral("U2S 连接成功"));
}

QString Client::readVersion(int timeoutMs)
{
    const u2s::Frame frame = command(kReadVersion, {}, timeoutMs);
    QString version = QString::fromLatin1(frame.payload);
    version.remove(QChar('\0'));
    version = version.trimmed();
    if (version.isEmpty()) {
        version = QString::fromLatin1(frame.payload.toHex(' '));
        if (version.isEmpty())
            throw std::runtime_error("SPRD FDL 未返回版本信息");
    }
    return version;
}

quint32 Client::readChipType(int timeoutMs)
{
    const u2s::Frame frame = command(kReadChipType, {}, timeoutMs);
    if (frame.payload.size() < 4)
        throw std::runtime_error("SPRD FDL 芯片类型响应过短");
    return proto::readU32be(frame.payload, 0);
}

void Client::startData(quint32 address, quint32 size, quint8 dataType, int timeoutMs)
{
    QByteArray payload = encodeAddressSize(address, size);
    payload.append(char(dataType));
    const u2s::Frame frame = command(kStartData, payload, timeoutMs);
    expectOk(kStartData, frame, QStringLiteral("START_DATA"));
}

void Client::midstData(const QByteArray& chunk, int timeoutMs)
{
    if (chunk.size() > u2s::kMaxPayload)
        throw std::runtime_error("SPRD FDL 数据块超过 65535 字节");
    m_link.send(kMidstData, chunk, timeoutMs);
}

void Client::endData(quint32 checksum, int timeoutMs)
{
    const QByteArray payload = checksum == 0 ? QByteArray() : proto::u32be(checksum);
    const u2s::Frame frame = command(kEndData, payload, timeoutMs);
    expectOk(kEndData, frame, QStringLiteral("END_DATA"));
}

void Client::execData(quint32 address, int timeoutMs)
{
    const u2s::Frame frame = command(kExecData, proto::u32be(address), timeoutMs);
    expectOk(kExecData, frame, QStringLiteral("EXEC_DATA"));
}

void Client::eraseFlash(quint32 address, quint32 size, int timeoutMs)
{
    const u2s::Frame frame = command(kEraseFlash, encodeAddressSize(address, size), timeoutMs);
    expectOk(kEraseFlash, frame, QStringLiteral("ERASE_FLASH"));
}

QByteArray Client::readFlash(quint32 address, quint32 size, int timeoutMs)
{
    m_reporter.status(QStringLiteral("读取 Flash 0x%1，%2 字节").arg(address, 0, 16).arg(size));
    const u2s::Frame frame = command(kReadFlash, encodeAddressSize(address, size), timeoutMs);
    expectOk(kReadFlash, frame, QStringLiteral("READ_FLASH"));
    // Some FDL revisions return the data inside the response frame, others
    // stream raw bytes afterwards. Prefer the framed payload when it is big
    // enough, otherwise read the raw stream.
    if (frame.payload.size() >= int(size))
        return frame.payload.left(int(size));
    return usb::readExact(m_transport, size, timeoutMs, m_reporter);
}

void Client::normalReset(int timeoutMs)
{
    m_reporter.info(QStringLiteral("发送 FDL 重启命令"));
    m_link.send(kNormalReset, {}, timeoutMs);
}

void Client::downloadFdl(quint32 address, const QByteArray& image, quint8 dataType,
                         int progressBase, int progressSpan)
{
    if (image.isEmpty())
        throw std::runtime_error("SPRD FDL 镜像为空");
    m_reporter.status(QStringLiteral("下载 FDL 到 0x%1（%2 字节）")
                          .arg(address, 0, 16)
                          .arg(image.size()));
    startData(address, quint32(image.size()), dataType);

    for (qsizetype offset = 0; offset < image.size(); offset += kDataChunk) {
        m_reporter.checkCanceled();
        const QByteArray chunk = image.mid(offset, kDataChunk);
        midstData(chunk);
        if (progressSpan > 0) {
            const int percent =
                progressBase + int(progressSpan * double(offset + chunk.size()) / image.size());
            m_reporter.progress(qMin(percent, progressBase + progressSpan));
        }
    }

    endData(0);
    m_reporter.status(QStringLiteral("启动 FDL…"));
    execData(address);
}

u2s::Frame Client::rawCommand(quint8 type, const QByteArray& payload, int timeoutMs)
{
    return command(type, payload, timeoutMs);
}

usb::PortInfo waitForPort(int timeoutMs, const proto::Reporter& reporter)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        reporter.checkCanceled();
        for (const auto& port : usb::enumerateFlashPorts()) {
            if (usb::platformForHint(port.chipHint) == usb::ChipPlatform::Spreadtrum)
                return port;
        }
        QThread::msleep(250);
    }
    return {};
}

} // namespace gf::sprd::fdl
