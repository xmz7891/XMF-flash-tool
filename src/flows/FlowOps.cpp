#include "FlowOps.h"

#include "Util.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <cstring>
#include <stdexcept>

namespace gf::flows {

namespace {

const PartitionInfo& requirePartition(const PartitionTable& table, const QString& name)
{
    const int index = table.indexOf(name);
    if (index < 0) {
        QStringList names = table.names();
        if (names.size() > 12)
            names = names.mid(0, 12) + QStringList{QStringLiteral("…")};
        throw std::runtime_error(
            QStringLiteral("分区不存在：%1（可用分区：%2）")
                .arg(name, names.join(QStringLiteral(", ")))
                .toStdString());
    }
    return table.partitions[index];
}

QString uniqueBackupPath(const QString& directory, const QString& platform, const QString& partition)
{
    if (!util::ensureDir(directory))
        throw std::runtime_error(
            QStringLiteral("无法创建备份目录：%1").arg(directory).toStdString());
    const QString base = directory + QLatin1Char('/') + backupFileName(platform, partition);
    return util::uniquePath(base);
}

QString findLatestBackup(const QString& directory, const QString& platform, const QString& partition)
{
    QDir dir(directory);
    if (!dir.exists())
        return {};
    const QStringList filters{
        QStringLiteral("%1_%2_*.img").arg(platform, partition),
    };
    QStringList entries = dir.entryList(filters, QDir::Files, QDir::Name);
    if (entries.isEmpty()) {
        // Partition names may contain characters that are sanitised in the file
        // name; fall back to a relaxed match.
        const QString prefix = QStringLiteral("%1_").arg(platform);
        for (const QString& entry : dir.entryList(QDir::Files, QDir::Name)) {
            if (entry.startsWith(prefix) && entry.contains(partition, Qt::CaseInsensitive))
                entries.append(entry);
        }
    }
    if (entries.isEmpty())
        return {};
    return dir.filePath(entries.last());
}

} // namespace

QString describePartition(const PartitionInfo& partition)
{
    return QStringLiteral("%1（偏移 0x%2，大小 0x%3 = %4）")
        .arg(partition.name)
        .arg(partition.offset, 0, 16)
        .arg(partition.size, 0, 16)
        .arg(util::formatBytes(qint64(partition.size)));
}

QList<BackupFile> backupPartitions(PartitionIo& io, const QString& directory,
                                   const QStringList& names, const proto::Reporter& reporter)
{
    const PartitionTable table = io.table();
    if (!table.valid)
        throw std::runtime_error(
            QStringLiteral("无法获取分区表：%1").arg(table.error).toStdString());

    QStringList targets = names;
    if (targets.isEmpty()) {
        for (const auto& partition : table.partitions) {
            if (partition.size > 0)
                targets.append(partition.name);
        }
    }
    if (targets.isEmpty())
        throw std::runtime_error("没有可备份的分区");

    QList<BackupFile> files;
    for (int i = 0; i < targets.size(); ++i) {
        reporter.checkCanceled();
        const PartitionInfo& partition = requirePartition(table, targets[i]);
        reporter.progress(int(100.0 * i / targets.size()),
                          QStringLiteral("备份 %1（%2）")
                              .arg(partition.name, util::formatBytes(qint64(partition.size))));
        const QByteArray data = io.readPartition(partition.name, reporter);
        const QString path = uniqueBackupPath(directory, io.platform(), partition.name);
        if (!util::writeFile(path, data))
            throw std::runtime_error(QStringLiteral("写入备份失败：%1").arg(path).toStdString());
        BackupFile file;
        file.partition = partition.name;
        file.path = path;
        file.bytes = data.size();
        files.append(file);
        reporter.info(QStringLiteral("已备份 %1 → %2（%3）")
                          .arg(partition.name, path, util::formatBytes(data.size())));
    }
    reporter.progress(100);
    return files;
}

QList<BackupFile> restorePartitions(PartitionIo& io, const QString& directory,
                                    const QStringList& names, const proto::Reporter& reporter)
{
    const PartitionTable table = io.table();
    if (!table.valid)
        throw std::runtime_error(
            QStringLiteral("无法获取分区表：%1").arg(table.error).toStdString());

    QStringList targets = names;
    if (targets.isEmpty()) {
        for (const auto& partition : table.partitions) {
            if (!findLatestBackup(directory, io.platform(), partition.name).isEmpty())
                targets.append(partition.name);
        }
    }
    if (targets.isEmpty())
        throw std::runtime_error(
            QStringLiteral("目录中没有 %1 分区备份：%2").arg(io.platform(), directory).toStdString());

    QList<BackupFile> files;
    for (int i = 0; i < targets.size(); ++i) {
        reporter.checkCanceled();
        const PartitionInfo& partition = requirePartition(table, targets[i]);
        const QString path = findLatestBackup(directory, io.platform(), partition.name);
        if (path.isEmpty()) {
            throw std::runtime_error(
                QStringLiteral("找不到分区 %1 的备份文件").arg(partition.name).toStdString());
        }
        const QByteArray data = util::readFile(path);
        if (data.isEmpty())
            throw std::runtime_error(QStringLiteral("备份文件为空：%1").arg(path).toStdString());
        reporter.progress(int(100.0 * i / targets.size()),
                          QStringLiteral("恢复 %1（%2）")
                              .arg(partition.name, util::formatBytes(data.size())));
        io.writePartition(partition.name, data, reporter);
        BackupFile file;
        file.partition = partition.name;
        file.path = path;
        file.bytes = data.size();
        files.append(file);
        reporter.info(QStringLiteral("已恢复 %1 ← %2").arg(partition.name, path));
    }
    reporter.progress(100);
    return files;
}

void erasePartitions(PartitionIo& io, const QStringList& names, const proto::Reporter& reporter)
{
    const PartitionTable table = io.table();
    if (!table.valid)
        throw std::runtime_error(
            QStringLiteral("无法获取分区表：%1").arg(table.error).toStdString());
    if (names.isEmpty())
        throw std::runtime_error("没有选择要擦除的分区");

    for (int i = 0; i < names.size(); ++i) {
        reporter.checkCanceled();
        const PartitionInfo& partition = requirePartition(table, names[i]);
        reporter.progress(int(100.0 * i / names.size()),
                          QStringLiteral("擦除 %1").arg(partition.name));
        io.erasePartition(partition.name, reporter);
        reporter.info(QStringLiteral("已擦除 %1（%2）")
                          .arg(partition.name, util::formatBytes(qint64(partition.size))));
    }
    reporter.progress(100);
}

QString readPartitionToFile(PartitionIo& io, const QString& name, const QString& file,
                            const proto::Reporter& reporter)
{
    const PartitionTable table = io.table();
    if (!table.valid)
        throw std::runtime_error(
            QStringLiteral("无法获取分区表：%1").arg(table.error).toStdString());
    const PartitionInfo& partition = requirePartition(table, name);
    const QByteArray data = io.readPartition(partition.name, reporter);
    if (!util::ensureDir(QFileInfo(file).absolutePath()) || !util::writeFile(file, data))
        throw std::runtime_error(QStringLiteral("写入文件失败：%1").arg(file).toStdString());
    reporter.info(QStringLiteral("已读取 %1 → %2（%3）")
                      .arg(partition.name, file, util::formatBytes(data.size())));
    return file;
}

void writeFileToPartition(PartitionIo& io, const QString& name, const QString& file,
                          const proto::Reporter& reporter)
{
    const PartitionTable table = io.table();
    if (!table.valid)
        throw std::runtime_error(
            QStringLiteral("无法获取分区表：%1").arg(table.error).toStdString());
    const PartitionInfo& partition = requirePartition(table, name);
    const QByteArray data = util::readFile(file);
    if (data.isEmpty())
        throw std::runtime_error(QStringLiteral("无法读取输入文件：%1").arg(file).toStdString());
    if (partition.size > 0 && quint64(data.size()) > partition.size) {
        reporter.warn(QStringLiteral("镜像大小 %1 超过分区 %2 大小 %3，设备可能拒绝写入")
                          .arg(data.size())
                          .arg(partition.name)
                          .arg(partition.size));
    }
    reporter.info(QStringLiteral("写入 %1 → %2（%3）")
                      .arg(file, partition.name, util::formatBytes(data.size())));
    io.writePartition(partition.name, data, reporter);
}

QString partitionChecksum(PartitionIo& io, const QString& name, bool sha256,
                          const proto::Reporter& reporter)
{
    const PartitionTable table = io.table();
    if (!table.valid)
        throw std::runtime_error(
            QStringLiteral("无法获取分区表：%1").arg(table.error).toStdString());
    const PartitionInfo& partition = requirePartition(table, name);
    const QByteArray data = io.readPartition(partition.name, reporter);
    return sha256 ? util::sha256Hex(data) : util::crc32Hex(data);
}

MemoryPartitionIo::MemoryPartitionIo(PartitionTable table, quint64 totalSize, QString platform)
    : m_table(std::move(table)), m_platform(std::move(platform))
{
    if (m_platform.isEmpty())
        m_platform = m_table.platform;
    quint64 required = totalSize;
    for (const auto& partition : m_table.partitions)
        required = qMax(required, partition.offset + partition.size);
    if (required == 0)
        required = 1;
    m_image = QByteArray(int(qMin<quint64>(required, quint64(16) * 1024 * 1024)), '\0');
}

QString MemoryPartitionIo::platform() const
{
    return m_platform;
}

PartitionTable MemoryPartitionIo::table()
{
    return m_table;
}

void MemoryPartitionIo::setPartitionTable(const PartitionTable& table)
{
    m_table = table;
}

QByteArray MemoryPartitionIo::readPartition(const QString& name, const proto::Reporter& reporter)
{
    const PartitionInfo& partition = requirePartition(m_table, name);
    return readRaw(partition.offset, partition.size, reporter);
}

void MemoryPartitionIo::writePartition(const QString& name, const QByteArray& data,
                                       const proto::Reporter& reporter)
{
    const PartitionInfo& partition = requirePartition(m_table, name);
    writeRaw(partition.offset, data, reporter);
}

void MemoryPartitionIo::erasePartition(const QString& name, const proto::Reporter& reporter)
{
    const PartitionInfo& partition = requirePartition(m_table, name);
    eraseRaw(partition.offset, partition.size, reporter);
}

QByteArray MemoryPartitionIo::readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    reporter.checkCanceled();
    if (offset + size > quint64(m_image.size()))
        throw std::runtime_error("MemoryPartitionIo: 读取超出存储范围");
    return m_image.mid(qsizetype(offset), qsizetype(size));
}

void MemoryPartitionIo::writeRaw(quint64 offset, const QByteArray& data,
                                 const proto::Reporter& reporter)
{
    reporter.checkCanceled();
    if (offset + quint64(data.size()) > quint64(m_image.size()))
        throw std::runtime_error("MemoryPartitionIo: 写入超出存储范围");
    for (qsizetype i = 0; i < data.size(); ++i)
        m_image[qsizetype(offset) + i] = data[i];
}

void MemoryPartitionIo::eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    reporter.checkCanceled();
    if (offset + size > quint64(m_image.size()))
        throw std::runtime_error("MemoryPartitionIo: 擦除超出存储范围");
    std::memset(m_image.data() + qsizetype(offset), 0, size_t(size));
}
} // namespace gf::flows