#include "Firehose.h"

#include "usb/UsbIo.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QString>

#include <stdexcept>

namespace gf::qcom::firehose {

namespace {

constexpr qsizetype kResponseReadChunk = 64 * 1024;

QString xmlEscape(const QString& text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

QString xmlUnescape(const QString& text)
{
    QString out = text;
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    out.replace(QStringLiteral("&#10;"), QStringLiteral("\n"));
    out.replace(QStringLiteral("&#13;"), QStringLiteral("\r"));
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return out;
}

} // namespace

QString wrapCommand(const QString& command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.startsWith(QStringLiteral("<?xml")) || trimmed.startsWith(QStringLiteral("<data")))
        return trimmed;
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?><data>") + trimmed
           + QStringLiteral("</data>");
}

QString buildConfigureXml(const StorageConfig& config)
{
    return QStringLiteral(
               "<configure MemoryName=\"%1\" Verbose=\"%2\" AlwaysValidate=\"%3\" "
               "MaxPayloadSizeToTargetInBytes=\"%4\" "
               "MaxPayloadSizeToTargetInBytesSupported=\"%4\" ZlpAwareHost=\"%5\" "
               "SkipStorageInit=\"%6\" />")
        .arg(xmlEscape(config.memoryName))
        .arg(config.verbose ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(config.alwaysValidate ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(config.maxPayloadBytes)
        .arg(config.zlpAwareHost ? 1 : 0)
        .arg(config.skipStorageInit ? QStringLiteral("true") : QStringLiteral("false"));
}

QString buildReadXml(const QString& storageType, int physicalPartition, quint64 startSector,
                     quint64 numSectors, int sectorSize)
{
    Q_UNUSED(storageType); // storage is selected by <configure MemoryName="...">
    return QStringLiteral(
               "<read SECTOR_SIZE_IN_BYTES=\"%1\" physical_partition_number=\"%2\" "
               "start_sector=\"%3\" num_sectors=\"%4\" />")
        .arg(sectorSize)
        .arg(physicalPartition)
        .arg(startSector)
        .arg(numSectors);
}

QString buildProgramXml(const QString& storageType, int physicalPartition, quint64 startSector,
                        quint64 numSectors, int sectorSize, const QString& filename)
{
    Q_UNUSED(storageType);
    return QStringLiteral(
               "<program SECTOR_SIZE_IN_BYTES=\"%1\" filename=\"%2\" "
               "physical_partition_number=\"%3\" start_sector=\"%4\" "
               "num_partial_sectors=\"%5\" />")
        .arg(sectorSize)
        .arg(xmlEscape(filename))
        .arg(physicalPartition)
        .arg(startSector)
        .arg(numSectors);
}

QString buildEraseXml(const QString& storageType, int physicalPartition, quint64 startSector,
                      quint64 numSectors, int sectorSize)
{
    Q_UNUSED(storageType);
    return QStringLiteral(
               "<erase SECTOR_SIZE_IN_BYTES=\"%1\" physical_partition_number=\"%2\" "
               "start_sector=\"%3\" num_sectors=\"%4\" />")
        .arg(sectorSize)
        .arg(physicalPartition)
        .arg(startSector)
        .arg(numSectors);
}

QString buildPowerXml(const QString& mode)
{
    return QStringLiteral("<power value=\"%1\" />").arg(xmlEscape(mode));
}

QString buildPatchXml(const Patch& patch)
{
    return QStringLiteral(
               "<patch SECTOR_SIZE_IN_BYTES=\"%1\" byte_offset=\"%2\" "
               "physical_partition_number=\"%3\" size_in_bytes=\"%4\" "
               "start_sector=\"%5\" value=\"0x%6\" />")
        .arg(patch.sectorSize)
        .arg(patch.byteOffset)
        .arg(patch.physicalPartition)
        .arg(patch.value.size())
        .arg(patch.startSector)
        .arg(QString::fromLatin1(patch.value.toHex()));
}

Response parseResponseXml(const QString& raw)
{
    Response response;
    response.raw = raw;
    response.received = false;

    static const QRegularExpression responsePattern(QStringLiteral("<response\\b([^>]*)>"),
                                                   QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression valuePattern(
        QStringLiteral("value\\s*=\\s*\"([^\"]*)\""), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression logPattern(
        QStringLiteral("<log\\b[^>]*value\\s*=\\s*\"([^\"]*)\""),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = responsePattern.match(raw);
    if (match.hasMatch()) {
        response.received = true;
        const auto valueMatch = valuePattern.match(match.captured(1));
        if (valueMatch.hasMatch()) {
            response.value = xmlUnescape(valueMatch.captured(1));
            response.acknowledged = response.value.compare(QStringLiteral("ACK"),
                                                           Qt::CaseInsensitive)
                                    == 0;
            response.nak = response.value.compare(QStringLiteral("NAK"), Qt::CaseInsensitive) == 0;
        }
    }

    auto logIterator = logPattern.globalMatch(raw);
    while (logIterator.hasNext()) {
        const auto logMatch = logIterator.next();
        response.logs.append(xmlUnescape(logMatch.captured(1)).trimmed());
    }

    if (response.received && !response.acknowledged && !response.nak)
        response.error = QStringLiteral("Firehose 响应没有可识别的 value 属性：%1").arg(response.raw);
    return response;
}

bool responseComplete(const QByteArray& raw)
{
    const int start = raw.indexOf("<response");
    if (start < 0)
        return false;
    const int tagEnd = raw.indexOf('>', start);
    if (tagEnd < 0)
        return false;

    int lastNonSpace = tagEnd - 1;
    while (lastNonSpace > start && (raw[lastNonSpace] == ' ' || raw[lastNonSpace] == '\t'
                                    || raw[lastNonSpace] == '\r' || raw[lastNonSpace] == '\n'))
        --lastNonSpace;
    if (raw[lastNonSpace] == '/')
        return true; // <response ... />
    return raw.indexOf("</response>", tagEnd) >= 0;
}

Client::Client(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter))
{
}

QString Client::readResponseXml(int timeoutMs)
{
    QByteArray raw;
    QElapsedTimer timer;
    timer.start();
    while (true) {
        m_reporter.checkCanceled();
        const qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0)
            break;
        const QByteArray chunk =
            m_transport.read(kResponseReadChunk, int(qMax<qint64>(remaining, 1)));
        if (chunk.isEmpty())
            break; // transport waited `remaining` ms and saw nothing
        raw.append(chunk);
        if (responseComplete(raw))
            break;
        if (raw.size() > 2 * 1024 * 1024)
            break; // runaway device, let the parser report a broken response
    }
    return QString::fromUtf8(raw);
}

void Client::writeData(const QByteArray& data, int timeoutMs)
{
    constexpr qsizetype kWriteChunk = 1024 * 1024;
    for (qsizetype offset = 0; offset < data.size(); offset += kWriteChunk) {
        m_reporter.checkCanceled();
        usb::writeAll(m_transport, data.mid(offset, kWriteChunk), timeoutMs, m_reporter);
    }
}

Response Client::sendCommand(const QString& xmlCommand, int timeoutMs)
{
    const QString xml = wrapCommand(xmlCommand);
    m_reporter.debug(QStringLiteral("Firehose → %1").arg(xml));
    usb::writeAll(m_transport, xml.toUtf8(), timeoutMs, m_reporter);

    m_lastRaw = readResponseXml(timeoutMs);
    Response response = parseResponseXml(m_lastRaw);
    for (const auto& log : response.logs)
        m_reporter.debug(QStringLiteral("Firehose ← %1").arg(log));
    if (!response.received)
        response.error = QStringLiteral("设备未返回 Firehose 响应（超时或连接中断）");
    return response;
}

Response Client::configure(const StorageConfig& config, int timeoutMs)
{
    m_reporter.status(QStringLiteral("Firehose 配置存储类型 %1").arg(config.memoryName));
    return sendCommand(buildConfigureXml(config), timeoutMs);
}

Response Client::getStorageInfo(int timeoutMs)
{
    return sendCommand(QStringLiteral("<getstorageinfo physical_partition_number=\"0\" />"),
                       timeoutMs);
}

Response Client::nop(int timeoutMs)
{
    return sendCommand(QStringLiteral("<nop />"), timeoutMs);
}

Response Client::readSectors(const QString& storageType, int physicalPartition,
                             quint64 startSector, quint64 numSectors, int sectorSize,
                             QByteArray* out, int timeoutMs, bool trailingAck)
{
    if (sectorSize <= 0)
        throw std::runtime_error("Firehose: 扇区大小必须大于 0");
    if (numSectors == 0)
        throw std::runtime_error("Firehose: 读取扇区数不能为 0");

    m_reporter.status(QStringLiteral("Firehose 读取 %1 扇区（起始 %2，%3 字节/扇区）")
                          .arg(numSectors)
                          .arg(startSector)
                          .arg(sectorSize));
    Response response = sendCommand(
        buildReadXml(storageType, physicalPartition, startSector, numSectors, sectorSize),
        timeoutMs);
    if (!response.ok())
        return response;

    const qint64 expected = qint64(numSectors) * sectorSize;
    QByteArray data = usb::readExact(m_transport, expected, timeoutMs, m_reporter);
    if (out)
        *out = data;
    else
        m_reporter.debug(QStringLiteral("Firehose 读取数据已丢弃（%1 字节）").arg(data.size()));

    if (trailingAck) {
        const QString trailing = readResponseXml(timeoutMs);
        if (!trailing.isEmpty()) {
            const Response trailingResponse = parseResponseXml(trailing);
            response.raw += trailing;
            if (!trailingResponse.ok()) {
                response.error = trailingResponse.error.isEmpty()
                                     ? QStringLiteral("Firehose 读取尾随响应不是 ACK：%1")
                                           .arg(trailingResponse.value)
                                     : trailingResponse.error;
            }
        }
    }
    return response;
}

Response Client::programSectors(const QString& storageType, int physicalPartition,
                                quint64 startSector, const QByteArray& data, int sectorSize,
                                const QString& filename, int chunkBytes, bool ackAfterData,
                                int timeoutMs)
{
    if (sectorSize <= 0)
        throw std::runtime_error("Firehose: 扇区大小必须大于 0");
    if (data.isEmpty())
        throw std::runtime_error("Firehose: 写入数据为空");

    // The programmer always consumes whole sectors; pad the tail with zeroes.
    QByteArray padded = data;
    const int remainder = padded.size() % sectorSize;
    if (remainder != 0)
        padded.append(QByteArray(sectorSize - remainder, '\0'));

    int chunk = qMax(sectorSize, (chunkBytes / sectorSize) * sectorSize);
    if (chunk <= 0)
        chunk = sectorSize;

    const qint64 totalSectors = padded.size() / sectorSize;
    m_reporter.status(QStringLiteral("Firehose 写入 %1 字节（%2 扇区）").arg(padded.size()).arg(
        totalSectors));

    Response last;
    for (qint64 offsetSectors = 0; offsetSectors < totalSectors; offsetSectors += chunk / sectorSize) {
        m_reporter.checkCanceled();
        const qint64 sectorsThisChunk = qMin<qint64>(chunk / sectorSize, totalSectors - offsetSectors);
        const qint64 byteOffset = offsetSectors * sectorSize;
        const qint64 byteCount = sectorsThisChunk * sectorSize;

        const QString xml = buildProgramXml(storageType, physicalPartition,
                                            startSector + quint64(offsetSectors), quint64(sectorsThisChunk),
                                            sectorSize, filename);
        Response response = sendCommand(xml, timeoutMs);
        if (!response.ok())
            return response;

        writeData(padded.mid(qsizetype(byteOffset), qsizetype(byteCount)), timeoutMs);

        if (ackAfterData) {
            const QString raw = readResponseXml(timeoutMs);
            const Response dataResponse = parseResponseXml(raw);
            if (!dataResponse.received || !dataResponse.ok()) {
                Response failure = dataResponse;
                failure.error = dataResponse.error.isEmpty()
                                    ? QStringLiteral("Firehose 数据块写入后未收到 ACK（%1）")
                                          .arg(dataResponse.value)
                                    : dataResponse.error;
                return failure;
            }
            response.raw += raw;
            last = response;
        } else {
            last = response;
        }

        m_reporter.progress(int(90.0 * double(offsetSectors + sectorsThisChunk) / double(totalSectors)));
    }
    return last;
}

Response Client::eraseSectors(const QString& storageType, int physicalPartition,
                              quint64 startSector, quint64 numSectors, int sectorSize,
                              int timeoutMs)
{
    m_reporter.status(
        QStringLiteral("Firehose 擦除 %1 扇区（起始 %2）").arg(numSectors).arg(startSector));
    return sendCommand(
        buildEraseXml(storageType, physicalPartition, startSector, numSectors, sectorSize),
        timeoutMs);
}

Response Client::power(const QString& mode, int timeoutMs)
{
    m_reporter.status(QStringLiteral("Firehose 电源操作：%1").arg(mode));
    return sendCommand(buildPowerXml(mode), timeoutMs);
}

Response Client::patch(const Patch& patch, int timeoutMs)
{
    if (patch.value.isEmpty())
        throw std::runtime_error("Firehose: patch 数据为空");
    m_reporter.status(QStringLiteral("Firehose 补丁：扇区 %1 偏移 %2，%3 字节")
                          .arg(patch.startSector)
                          .arg(patch.byteOffset)
                          .arg(patch.value.size()));
    return sendCommand(buildPatchXml(patch), timeoutMs);
}

} // namespace gf::qcom::firehose
