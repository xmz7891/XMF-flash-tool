#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::gpt {

constexpr int kEntrySize = 128;
constexpr int kHeaderSize = 92;

struct Entry {
    QByteArray typeGuidRaw;   // 16 字节
    QByteArray uniqueGuidRaw; // 16 字节
    quint64 firstLba = 0;
    quint64 lastLba = 0;
    quint64 attributes = 0;
    QString name;

    bool isEmpty() const { return typeGuidRaw == QByteArray(16, '\0') || typeGuidRaw.isEmpty(); }
    QString typeGuid() const { return guidString(typeGuidRaw); }
    QString uniqueGuid() const { return guidString(uniqueGuidRaw); }
    quint64 sectorCount() const { return lastLba >= firstLba ? lastLba - firstLba + 1 : 0; }
};

struct Header {
    bool present = false;
    quint32 revision = 0;
    quint32 headerSize = 0;
    quint32 headerCrc = 0;
    quint64 myLba = 0;
    quint64 alternateLba = 0;
    quint64 firstUsableLba = 0;
    quint64 lastUsableLba = 0;
    QByteArray diskGuidRaw;
    quint64 entriesLba = 0;
    quint32 numEntries = 0;
    quint32 entrySize = 0;
    quint32 entriesCrc = 0;
    qint64 offsetBytes = 0;
    bool crcValid = false;
    bool entriesCrcValid = false;

    QString diskGuid() const { return guidString(diskGuidRaw); }
};

struct Table {
    quint32 sectorSize = 512;
    quint64 diskSectors = 0;
    bool hasMbr = false;
    bool protectiveMbr = false;
    Header primary;
    Header backup;
    QVector<Entry> entries;       // 取有效副本的分区表
    QVector<Entry> backupEntries; // 备份副本的分区表（可能为空）
    bool usedBackup = false;
    QStringList warnings;

    int partitionCount() const;
};

/// 解析整盘映像（至少包含 MBR + 主 GPT + 备份 GPT；只有一份时也能解析）。
Table parse(const QByteArray& disk, quint32 sectorSize = 512);
/// 按分区名查找（大小写不敏感），返回索引，未找到返回 -1。
int findByName(const Table& table, const QString& name, bool exact = false);
/// 分区的字节偏移。
quint64 partitionOffsetBytes(const Table& table, int index);
QString partitionSummary(const Table& table, int index);

/// 重新计算校验值并修复主/备 GPT（含缺失的保护性 MBR）。
/// 返回修复动作描述；失败抛异常。disk 必须可容纳完整的 GPT 结构。
QStringList repair(QByteArray& disk, quint32 sectorSize = 512);

/// 导出 "MBR + 主 GPT 头 + 分区表" 作为备份数据（供恢复使用）。
QByteArray exportBackup(const QByteArray& disk, quint32 sectorSize = 512);
/// 从备份恢复主 GPT，并按目标磁盘大小重建备份 GPT。
QStringList importBackup(QByteArray& disk, const QByteArray& backup, quint32 sectorSize = 512);

} // namespace gf::img::gpt
