#pragma once

// Generic partition operations shared by every platform flow.
//
// A flow only has to provide a PartitionIo implementation (Firehose, MTK DA,
// SPRD FDL, RockUSB, FEL). Backup / restore / erase / checksum / file naming
// and progress reporting are implemented once here, so all platforms behave
// identically and can be tested with MemoryPartitionIo.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "PartitionTable.h"
#include "usb/ProtocolSupport.h"

namespace gf::flows {

/// Read/write/erase access to the storage of one platform session.
class PartitionIo {
public:
    virtual ~PartitionIo() = default;

    virtual QString platform() const = 0;
    /// Partition table of this session (from GPT/scatter/PAC/map).
    virtual PartitionTable table() = 0;

    virtual QByteArray readPartition(const QString& name, const proto::Reporter& reporter) = 0;
    virtual void writePartition(const QString& name, const QByteArray& data,
                                const proto::Reporter& reporter) = 0;
    virtual void erasePartition(const QString& name, const proto::Reporter& reporter) = 0;

    virtual QByteArray readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) = 0;
    virtual void writeRaw(quint64 offset, const QByteArray& data,
                          const proto::Reporter& reporter) = 0;
    virtual void eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) = 0;

    virtual quint32 sectorSize() const = 0;
};

struct BackupFile {
    QString partition;
    QString path;
    qint64 bytes = 0;
};

/// Backs up `names` (empty = every partition with a non-zero size) into
/// `directory` as `<platform>_<partition>_<date>.img`.
QList<BackupFile> backupPartitions(PartitionIo& io, const QString& directory,
                                   const QStringList& names, const proto::Reporter& reporter);

/// Restores partitions from previously written backups. When `names` is empty
/// every backup for the platform found in `directory` is restored.
QList<BackupFile> restorePartitions(PartitionIo& io, const QString& directory,
                                    const QStringList& names, const proto::Reporter& reporter);

void erasePartitions(PartitionIo& io, const QStringList& names, const proto::Reporter& reporter);

QString readPartitionToFile(PartitionIo& io, const QString& name, const QString& file,
                            const proto::Reporter& reporter);
void writeFileToPartition(PartitionIo& io, const QString& name, const QString& file,
                          const proto::Reporter& reporter);

QString partitionChecksum(PartitionIo& io, const QString& name, bool sha256,
                          const proto::Reporter& reporter);

/// Human readable one-line summary used by tools: "userdata 0x… 4.0 GB".
QString describePartition(const PartitionInfo& partition);

/// In-memory PartitionIo for unit tests and offline dry-runs.
class MemoryPartitionIo final : public PartitionIo {
public:
    MemoryPartitionIo(PartitionTable table, quint64 totalSize = 0, QString platform = {});

    QString platform() const override;
    PartitionTable table() override;
    QByteArray readPartition(const QString& name, const proto::Reporter& reporter) override;
    void writePartition(const QString& name, const QByteArray& data,
                        const proto::Reporter& reporter) override;
    void erasePartition(const QString& name, const proto::Reporter& reporter) override;
    QByteArray readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) override;
    void writeRaw(quint64 offset, const QByteArray& data,
                  const proto::Reporter& reporter) override;
    void eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) override;
    quint32 sectorSize() const override { return m_table.sectorSize; }

    const QByteArray& image() const { return m_image; }
    void setPartitionTable(const PartitionTable& table);

private:
    PartitionTable m_table;
    QByteArray m_image;
    QString m_platform;
};

} // namespace gf::flows
