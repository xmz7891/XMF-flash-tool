#include "AwFlow.h"

#include "Util.h"

#include <stdexcept>

namespace gf::flows {

namespace {

constexpr qsizetype kEraseChunk = 64 * 1024;

const PartitionInfo& lookup(const PartitionTable& table, const QString& name)
{
    const int index = table.indexOf(name);
    if (index < 0)
        throw std::runtime_error(QStringLiteral("分区地址表中没有 %1").arg(name).toStdString());
    return table.partitions[index];
}

} // namespace

AwSession::AwSession(usb::IUsbTransport& transport, proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter)), m_client(transport, m_reporter)
{
}

aw::VersionInfo AwSession::open(const AwOptions& options)
{
    m_options = options;
    const aw::VersionInfo info = m_client.hello();
    m_reporter.info(QStringLiteral("FEL BootROM %1，SoC %2（0x%3）")
                        .arg(info.magic)
                        .arg(aw::socName(info.id))
                        .arg(info.id, 8, 16, QLatin1Char('0')));
    m_opened = true;
    return info;
}

const PartitionTable& AwSession::partitionTable()
{
    if (!m_table.valid && !m_options.partitionMapFile.trimmed().isEmpty()) {
        const QByteArray text = util::readFile(m_options.partitionMapFile);
        if (text.isEmpty()) {
            throw std::runtime_error(
                QStringLiteral("无法读取分区地址表：%1").arg(m_options.partitionMapFile)
                    .toStdString());
        }
        m_table = PartitionTable::fromPartitionMapText(QString::fromUtf8(text),
                                                      QStringLiteral("aw"));
    }
    if (!m_table.valid) {
        throw std::runtime_error(
            "FEL 没有分区表接口，请先提供分区地址表文件（格式：name,offset,size，"
            "支持 0x 十六进制）");
    }
    return m_table;
}

void AwSession::refreshPartitionTable()
{
    m_table = PartitionTable();
    partitionTable();
}

QByteArray AwSession::readRaw(quint64 address, quint64 size)
{
    if (size == 0)
        return {};
    if (address + size > 0xFFFFFFFFull)
        throw std::runtime_error("FEL 地址超出 32 位范围");
    m_reporter.status(QStringLiteral("FEL 读取 0x%1，%2 字节")
                          .arg(address, 0, 16)
                          .arg(size));
    return m_client.read(quint32(address), quint32(size));
}

void AwSession::writeRaw(quint64 address, const QByteArray& data)
{
    if (data.isEmpty())
        return;
    if (address + quint64(data.size()) > 0xFFFFFFFFull)
        throw std::runtime_error("FEL 地址超出 32 位范围");
    m_reporter.status(QStringLiteral("FEL 写入 0x%1，%2 字节")
                          .arg(address, 0, 16)
                          .arg(data.size()));
    m_client.write(quint32(address), data);
}

void AwSession::eraseRaw(quint64 address, quint64 size, const QString& label)
{
    if (size == 0)
        return;
    m_reporter.status(QStringLiteral("FEL 擦除 %1（写零模拟）")
                          .arg(label.isEmpty() ? QStringLiteral("0x%1").arg(address, 0, 16) : label));
    qint64 remaining = qint64(size);
    quint64 offset = address;
    while (remaining > 0) {
        m_reporter.checkCanceled();
        const qint64 chunk = qMin<qint64>(remaining, kEraseChunk);
        m_client.write(quint32(offset), QByteArray(int(chunk), '\0'));
        offset += quint64(chunk);
        remaining -= chunk;
    }
}

void AwSession::execRaw(quint64 address)
{
    if (address > 0xFFFFFFFFull)
        throw std::runtime_error("FEL 执行地址超出 32 位范围");
    m_client.exec(quint32(address));
}

QByteArray AwMemoryIo::readPartition(const QString& name, const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_session.partitionTable(), name);
    reporter.status(QStringLiteral("读取分区 %1").arg(partition.name));
    return m_session.readRaw(partition.offset, partition.size);
}

void AwMemoryIo::writePartition(const QString& name, const QByteArray& data,
                                const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_session.partitionTable(), name);
    reporter.status(QStringLiteral("写入分区 %1（%2）")
                        .arg(partition.name, util::formatBytes(data.size())));
    m_session.writeRaw(partition.offset, data);
}

void AwMemoryIo::erasePartition(const QString& name, const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_session.partitionTable(), name);
    reporter.status(QStringLiteral("擦除分区 %1").arg(partition.name));
    m_session.eraseRaw(partition.offset, partition.size, partition.name);
}

QByteArray AwMemoryIo::readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("FEL 读取 0x%1，%2 字节").arg(offset, 0, 16).arg(size));
    return m_session.readRaw(offset, size);
}

void AwMemoryIo::writeRaw(quint64 offset, const QByteArray& data,
                          const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("FEL 写入 0x%1，%2 字节").arg(offset, 0, 16).arg(data.size()));
    m_session.writeRaw(offset, data);
}

void AwMemoryIo::eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    reporter.status(QStringLiteral("FEL 擦除 0x%1，%2 字节").arg(offset, 0, 16).arg(size));
    m_session.eraseRaw(offset, size);
}

} // namespace gf::flows
