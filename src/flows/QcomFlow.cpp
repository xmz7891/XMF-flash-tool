#include "QcomFlow.h"

#include "Util.h"
#include "packets/PacketStore.h"
#include "qcom/Elf.h"

#include <stdexcept>

namespace gf::flows {

namespace {

constexpr quint64 kGptProbeBytes = 64 * 1024;
constexpr qsizetype kSlotSuffixOffset = 2048; // bootloader_message_ab.slot_suffix
constexpr qsizetype kSlotSuffixSize = 32;

void ensureOk(const qcom::firehose::Response& response, const QString& action)
{
    if (response.ok())
        return;
    QString reason = response.error;
    if (reason.isEmpty())
        reason = response.nak ? QStringLiteral("设备返回 NAK") : response.value;
    throw std::runtime_error(QStringLiteral("%1失败：%2").arg(action, reason).toStdString());
}

} // namespace

QcomSession::QcomSession(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport),
      m_reporter(std::move(reporter)),
      m_sahara(transport, m_reporter),
      m_firehose(transport, m_reporter)
{
}

void QcomSession::open(const QByteArray& programmer, const QcomOptions& options)
{
    if (programmer.isEmpty())
        throw std::runtime_error("请提供 Firehose programmer 文件（devprg .elf/.mbn）");

    const qcom::ElfImage elf = qcom::ElfImage::parse(programmer);
    if (!elf.valid) {
        throw std::runtime_error(
            QStringLiteral("programmer 不是有效的 ELF/MBN：%1").arg(elf.error).toStdString());
    }

    m_options = options;
    m_sectorSize = (m_options.sectorSize > 0) ? m_options.sectorSize : 4096;
    if (m_options.memoryName.compare(QLatin1String("emmc"), Qt::CaseInsensitive) == 0
        || m_options.memoryName.compare(QLatin1String("spinor"), Qt::CaseInsensitive) == 0) {
        m_sectorSize = 512;
    }
    m_reporter.info(qcom::describeElf(elf));

    m_reporter.progress(5, QStringLiteral("Sahara 握手…"));
    m_sahara.handshake();
    m_reporter.progress(15, QStringLiteral("上传 programmer…"));
    const auto upload = m_sahara.upload(programmer, 15000, 15, 55);
    if (!upload.success) {
        throw std::runtime_error(
            QStringLiteral("Sahara 上传 programmer 失败：%1").arg(upload.error).toStdString());
    }

    qcom::firehose::StorageConfig config;
    config.memoryName = m_options.memoryName;
    config.maxPayloadBytes = m_options.maxPayloadBytes;
    config.verbose = m_options.verbose;
    m_reporter.progress(75, QStringLiteral("Firehose 配置存储类型 %1…").arg(config.memoryName));
    ensureOk(m_firehose.configure(config), QStringLiteral("Firehose configure"));
    m_opened = true;
    m_reporter.progress(80, QStringLiteral("Firehose 就绪"));
}

const PartitionTable& QcomSession::partitionTable()
{
    if (!m_opened)
        throw std::runtime_error("QCOM 会话尚未建立，无法读取分区表");
    if (m_tableLoaded)
        return m_table;

    m_reporter.status(QStringLiteral("读取 GPT 分区表…"));
    PartitionTable table = PartitionTable::fromGptData(readWithSectorSize(0, kGptProbeBytes, 4096),
                                                       QStringLiteral("qcom"),
                                                       m_options.memoryName);
    if (!table.valid) {
        table = PartitionTable::fromGptData(readWithSectorSize(0, kGptProbeBytes, 512),
                                            QStringLiteral("qcom"), m_options.memoryName);
    }
    if (!table.valid) {
        throw std::runtime_error(
            QStringLiteral("无法读取设备 GPT：%1").arg(table.error).toStdString());
    }
    m_sectorSize = table.sectorSize;
    m_table = table;
    m_tableLoaded = true;
    m_reporter.info(QStringLiteral("GPT 分区表：%1 个分区，扇区大小 %2")
                        .arg(table.partitions.size())
                        .arg(table.sectorSize));
    return m_table;
}

void QcomSession::refreshPartitionTable()
{
    m_tableLoaded = false;
    m_table = PartitionTable();
    partitionTable();
}

QByteArray QcomSession::readWithSectorSize(quint64 offset, quint64 size, quint32 sectorSize)
{
    if (size == 0)
        return {};
    const quint64 startSector = offset / sectorSize;
    const quint64 lead = offset % sectorSize;
    const quint64 total = lead + size;
    const quint64 sectorCount = (total + sectorSize - 1) / sectorSize;
    QByteArray data;
    const auto response = m_firehose.readSectors(m_options.memoryName, 0, startSector, sectorCount,
                                                 int(sectorSize), &data);
    ensureOk(response, QStringLiteral("Firehose 读取 0x%1").arg(offset, 0, 16));
    if (quint64(data.size()) < total) {
        throw std::runtime_error(
            QStringLiteral("Firehose 返回数据不足：期望 %1，实际 %2").arg(total).arg(data.size())
                .toStdString());
    }
    return data.mid(int(lead), int(size));
}

QByteArray QcomSession::readRaw(quint64 offset, quint64 size)
{
    return readWithSectorSize(offset, size, m_sectorSize);
}

void QcomSession::writeRaw(quint64 offset, const QByteArray& data)
{
    if (data.isEmpty())
        throw std::runtime_error("写入数据为空");
    const quint32 ss = m_sectorSize;
    const quint64 startSector = offset / ss;
    const quint64 lead = offset % ss;
    const quint64 total = lead + quint64(data.size());
    const quint64 sectorCount = (total + ss - 1) / ss;

    // Read-modify-write so unaligned writes keep the surrounding bytes intact.
    QByteArray buffer = readWithSectorSize(startSector * ss, sectorCount * ss, ss);
    for (qsizetype i = 0; i < data.size(); ++i)
        buffer[qsizetype(lead) + i] = data[i];

    m_reporter.status(QStringLiteral("写入 0x%1，%2 字节").arg(offset, 0, 16).arg(data.size()));
    const auto response = m_firehose.programSectors(m_options.memoryName, 0, startSector, buffer,
                                                    int(ss), QStringLiteral("flash.bin"));
    ensureOk(response, QStringLiteral("Firehose 写入"));
}

void QcomSession::eraseRaw(quint64 offset, quint64 size, const QString& label)
{
    if (size == 0)
        return;
    const quint32 ss = m_sectorSize;
    const quint64 startSector = offset / ss;
    const quint64 sectorCount = (offset % ss + size + ss - 1) / ss;
    m_reporter.status(QStringLiteral("擦除 %1（起始扇区 %2，%3 扇区）")
                          .arg(label.isEmpty() ? QStringLiteral("0x%1").arg(offset, 0, 16) : label)
                          .arg(startSector)
                          .arg(sectorCount));
    const auto response =
        m_firehose.eraseSectors(m_options.memoryName, 0, startSector, sectorCount, int(ss));
    ensureOk(response, QStringLiteral("Firehose 擦除"));
}

const PartitionInfo& QcomSession::requirePartition(const QString& name) const
{
    const int index = m_table.indexOf(name);
    if (index < 0) {
        throw std::runtime_error(
            QStringLiteral("GPT 中没有分区 %1").arg(name).toStdString());
    }
    return m_table.partitions[index];
}

QByteArray QcomSession::readPartition(const QString& name)
{
    partitionTable();
    const PartitionInfo& partition = requirePartition(name);
    if (partition.size == 0)
        throw std::runtime_error(
            QStringLiteral("分区 %1 大小为 0").arg(partition.name).toStdString());
    m_reporter.status(QStringLiteral("读取分区 %1（%2）")
                          .arg(partition.name, util::formatBytes(qint64(partition.size))));
    return readRaw(partition.offset, partition.size);
}

void QcomSession::writePartition(const QString& name, const QByteArray& data)
{
    partitionTable();
    const PartitionInfo& partition = requirePartition(name);
    if (partition.size > 0 && quint64(data.size()) > partition.size) {
        m_reporter.warn(QStringLiteral("镜像 %1 大于分区 %2（%3 > %4）")
                            .arg(name)
                            .arg(partition.name)
                            .arg(data.size())
                            .arg(partition.size));
    }
    writeRaw(partition.offset, data);
}

void QcomSession::erasePartition(const QString& name)
{
    partitionTable();
    const PartitionInfo& partition = requirePartition(name);
    eraseRaw(partition.offset, partition.size, partition.name);
}

void QcomSession::setActiveSlot(const QString& slot)
{
    QString normalized = slot.trimmed().toLower();
    normalized.remove(QLatin1Char('_'));
    if (normalized != QLatin1String("a") && normalized != QLatin1String("b")) {
        throw std::runtime_error("启动槽只能是 a 或 b");
    }
    partitionTable();
    const PartitionInfo& misc = requirePartition(QStringLiteral("misc"));
    const quint64 readSize = qMin<quint64>(misc.size ? misc.size : 4096,
                                           quint64(kSlotSuffixOffset + kSlotSuffixSize));
    QByteArray data = readRaw(misc.offset, readSize);
    if (data.size() < int(kSlotSuffixOffset + kSlotSuffixSize)) {
        throw std::runtime_error("misc 分区太小，无法写入 A/B 启动槽");
    }
    QByteArray suffix(kSlotSuffixSize, '\0');
    suffix[0] = '_';
    suffix[1] = normalized.at(0).toLatin1();
    for (qsizetype i = 0; i < suffix.size(); ++i)
        data[qsizetype(kSlotSuffixOffset) + i] = suffix[i];
    writeRaw(misc.offset, data);
    m_reporter.info(QStringLiteral("已将 A/B 启动槽设置为 _%1（写入 misc 偏移 0x%2）")
                        .arg(normalized)
                        .arg(kSlotSuffixOffset, 0, 16));
}

QString QcomSession::programmerFromPacket(const QString& packetId, const QString& modelName)
{
    if (packetId.trimmed().isEmpty())
        return {};
    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();
    const QString path = store.modelResource(packetId, modelName, QStringLiteral("Loader"));
    if (!path.isEmpty())
        return path;
    return store.modelResource(packetId, modelName, QStringLiteral("DA1"));
}

QString QcomSession::memoryTypeFromPacket(const QString& packetId, const QString& modelName)
{
    if (packetId.trimmed().isEmpty())
        return {};
    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();
    const QVariantMap model = store.findModel(packetId, modelName);
    const QString storage = model.value(QStringLiteral("Storage")).toString().toUpper();
    if (storage == QLatin1String("EMMC"))
        return QStringLiteral("emmc");
    if (storage == QLatin1String("SPINOR"))
        return QStringLiteral("spinor");
    if (storage == QLatin1String("UFS"))
        return QStringLiteral("ufs");
    return {};
}

// ---------------------------------------------------------------------------
// QcomFirehoseIo
// ---------------------------------------------------------------------------

QByteArray QcomFirehoseIo::readPartition(const QString& name, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("读取分区 %1").arg(name));
    return m_session.readPartition(name);
}

void QcomFirehoseIo::writePartition(const QString& name, const QByteArray& data,
                                    const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("写入分区 %1（%2）")
                        .arg(name, util::formatBytes(data.size())));
    m_session.writePartition(name, data);
}

void QcomFirehoseIo::erasePartition(const QString& name, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("擦除分区 %1").arg(name));
    m_session.erasePartition(name);
}

QByteArray QcomFirehoseIo::readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("读取 0x%1，%2 字节").arg(offset, 0, 16).arg(size));
    return m_session.readRaw(offset, size);
}

void QcomFirehoseIo::writeRaw(quint64 offset, const QByteArray& data,
                              const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("写入 0x%1，%2 字节").arg(offset, 0, 16).arg(data.size()));
    m_session.writeRaw(offset, data);
}

void QcomFirehoseIo::eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("擦除 0x%1，%2 字节").arg(offset, 0, 16).arg(size));
    m_session.eraseRaw(offset, size);
}

} // namespace gf::flows
