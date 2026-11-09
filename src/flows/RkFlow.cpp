#include "RkFlow.h"

#include "Util.h"

#include <stdexcept>

namespace gf::flows {

namespace {

const PartitionInfo& lookup(const PartitionTable& table, const QString& name)
{
    const int index = table.indexOf(name);
    if (index < 0)
        throw std::runtime_error(QStringLiteral("分区不存在：%1").arg(name).toStdString());
    return table.partitions[index];
}

} // namespace

RkSession::RkSession(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter)), m_client(transport, m_reporter)
{
}

void RkSession::open(const RkOptions& options)
{
    m_options = options;
    m_sectorSize = options.sectorSize ? options.sectorSize : 512;
    m_reporter.progress(5, QStringLiteral("读取 RockUSB 设备信息…"));
    const rk::DeviceInfo info = m_client.deviceInfo();
    m_reporter.info(QStringLiteral("RockUSB Loader 版本 0x%1%2")
                        .arg(info.version, 8, 16, QLatin1Char('0'))
                        .arg(info.chip.isEmpty() ? QString()
                                                 : QStringLiteral("，芯片 %1").arg(info.chip)));

    if (!m_options.parameterFile.trimmed().isEmpty()) {
        const QByteArray text = util::readFile(m_options.parameterFile);
        if (text.isEmpty()) {
            throw std::runtime_error(
                QStringLiteral("无法读取 parameter 文件：%1").arg(m_options.parameterFile)
                    .toStdString());
        }
        m_table = PartitionTable::fromParameterText(QString::fromUtf8(text),
                                                    QStringLiteral("rk"));
        if (!m_table.valid) {
            throw std::runtime_error(
                QStringLiteral("parameter 文件解析失败：%1").arg(m_table.error).toStdString());
        }
        m_tableLoaded = true;
        m_reporter.info(QStringLiteral("parameter 分区表：%1 个分区").arg(m_table.partitions.size()));
    }
    m_opened = true;
    m_reporter.progress(15, QStringLiteral("RockUSB 就绪"));
}

const PartitionTable& RkSession::partitionTable()
{
    if (!m_opened)
        throw std::runtime_error("RockUSB 会话尚未建立");
    if (m_tableLoaded)
        return m_table;

    m_reporter.status(QStringLiteral("读取设备 GPT…"));
    // 64 sectors of 512 bytes covers MBR + primary GPT header + 16 KiB entries.
    const QByteArray prefix = m_client.readSectors(0, 64, 512);
    m_table = PartitionTable::fromGptData(prefix, QStringLiteral("rk"));
    if (!m_table.valid) {
        throw std::runtime_error(
            QStringLiteral("无法读取 Rockchip GPT：%1。可改填 parameter.txt")
                .arg(m_table.error)
                .toStdString());
    }
    m_sectorSize = m_table.sectorSize;
    m_tableLoaded = true;
    m_reporter.info(QStringLiteral("GPT 分区表：%1 个分区").arg(m_table.partitions.size()));
    return m_table;
}

void RkSession::refreshPartitionTable()
{
    m_tableLoaded = false;
    m_table = PartitionTable();
    partitionTable();
}

QByteArray RkSession::readRaw(quint64 offset, quint64 size)
{
    if (size == 0)
        return {};
    const quint32 ss = m_sectorSize;
    const quint64 startSector = offset / ss;
    const quint64 lead = offset % ss;
    const quint64 total = lead + size;
    const quint64 count = (total + ss - 1) / ss;
    const QByteArray data = m_client.readSectors(quint32(startSector), quint32(count), int(ss));
    if (quint64(data.size()) < total) {
        throw std::runtime_error(
            QStringLiteral("RockUSB 返回数据不足：期望 %1，实际 %2").arg(total).arg(data.size())
                .toStdString());
    }
    return data.mid(int(lead), int(size));
}

void RkSession::writeRaw(quint64 offset, const QByteArray& data)
{
    if (data.isEmpty())
        throw std::runtime_error("写入数据为空");
    const quint32 ss = m_sectorSize;
    const quint64 startSector = offset / ss;
    const quint64 lead = offset % ss;
    const quint64 total = lead + quint64(data.size());
    const quint64 count = (total + ss - 1) / ss;
    QByteArray buffer = readRaw(startSector * ss, count * ss);
    for (qsizetype i = 0; i < data.size(); ++i)
        buffer[qsizetype(lead) + i] = data[i];
    m_reporter.status(QStringLiteral("RockUSB 写入 0x%1，%2 字节")
                          .arg(offset, 0, 16)
                          .arg(data.size()));
    m_client.writeSectors(quint32(startSector), buffer, int(ss));
}

void RkSession::eraseRaw(quint64 offset, quint64 size, const QString& label)
{
    if (size == 0)
        return;
    const quint32 ss = m_sectorSize;
    const quint64 startSector = offset / ss;
    const quint64 count = (offset % ss + size + ss - 1) / ss;
    m_reporter.status(QStringLiteral("RockUSB 擦除 %1（%2 扇区）")
                          .arg(label.isEmpty() ? QStringLiteral("0x%1").arg(offset, 0, 16) : label)
                          .arg(count));
    m_client.eraseSectors(quint32(startSector), quint32(count), int(ss));
}

const PartitionInfo& RkSession::requirePartition(const QString& name) const
{
    return lookup(m_table, name);
}

QByteArray RkSession::readPartition(const QString& name)
{
    partitionTable();
    const PartitionInfo& partition = requirePartition(name);
    m_reporter.status(QStringLiteral("RockUSB 读取分区 %1（%2）")
                          .arg(partition.name, util::formatBytes(qint64(partition.size))));
    return readRaw(partition.offset, partition.size);
}

void RkSession::writePartition(const QString& name, const QByteArray& data)
{
    partitionTable();
    const PartitionInfo& partition = requirePartition(name);
    writeRaw(partition.offset, data);
}

void RkSession::erasePartition(const QString& name)
{
    partitionTable();
    const PartitionInfo& partition = requirePartition(name);
    eraseRaw(partition.offset, partition.size, partition.name);
}

void RkSession::rebootToLoader()
{
    m_client.rebootToLoader();
    m_reporter.info(QStringLiteral("已发送重启到 Loader 命令"));
}

void RkSession::reset()
{
    m_client.reset(0);
    m_reporter.info(QStringLiteral("已发送普通重启命令"));
}

QByteArray RkStorageIo::readPartition(const QString& name, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("读取分区 %1").arg(name));
    return m_session.readPartition(name);
}

void RkStorageIo::writePartition(const QString& name, const QByteArray& data,
                                 const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("写入分区 %1（%2）")
                        .arg(name, util::formatBytes(data.size())));
    m_session.writePartition(name, data);
}

void RkStorageIo::erasePartition(const QString& name, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("擦除分区 %1").arg(name));
    m_session.erasePartition(name);
}

QByteArray RkStorageIo::readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("读取 0x%1，%2 字节").arg(offset, 0, 16).arg(size));
    return m_session.readRaw(offset, size);
}

void RkStorageIo::writeRaw(quint64 offset, const QByteArray& data,
                           const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("写入 0x%1，%2 字节").arg(offset, 0, 16).arg(data.size()));
    m_session.writeRaw(offset, data);
}

void RkStorageIo::eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("擦除 0x%1，%2 字节").arg(offset, 0, 16).arg(size));
    m_session.eraseRaw(offset, size);
}

} // namespace gf::flows
