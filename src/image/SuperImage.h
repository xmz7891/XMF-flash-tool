#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::superimg {

constexpr quint32 kGeometryMagic = 0x616c4467u; // "gDla"
constexpr quint32 kHeaderMagic = 0x414C5030u;   // "LP0A"
constexpr quint32 kReservedBytes = 4096;
constexpr quint32 kDefaultMetadataMaxSize = 64 * 1024;
constexpr quint32 kLogicalBlockSize = 4096;
constexpr quint32 kHeaderSize = 216;
constexpr quint64 kFirstLogicalSector = 2048; // 1 MiB
constexpr quint64 kAlignmentBytes = 1024 * 1024;

enum class TargetType : quint32 { Linear = 0, Zero = 1 };

struct Geometry {
    quint32 magic = 0;
    quint32 structSize = 0;
    quint32 checksum = 0;
    quint32 metadataMaxSize = 0;
    quint32 metadataSlotCount = 0;
    quint32 logicalBlockSize = 0;
    bool checksumValid = false;
};

struct PartitionEntry {
    QString name;
    quint32 attributes = 0;
    quint32 firstExtentIndex = 0;
    quint32 numExtents = 0;
    quint32 groupIndex = 0;
};

struct ExtentEntry {
    quint64 numSectors = 0;
    quint32 targetType = 0;
    quint64 targetData = 0;
    quint32 targetSource = 0;
    QString typeText() const;
};

struct GroupEntry {
    QString name;
    quint32 flags = 0;
    quint64 maximumSize = 0;
};

struct BlockDeviceEntry {
    quint64 firstLogicalSector = 0;
    quint32 alignment = 0;
    quint32 alignmentOffset = 0;
    quint64 size = 0;
    QString partitionName;
    quint32 flags = 0;
};

struct Metadata {
    quint32 slot = 0;
    quint32 majorVersion = 0;
    quint32 minorVersion = 0;
    quint32 headerSize = 0;
    quint32 flags = 0;
    quint32 tablesSize = 0;
    quint32 headerChecksum = 0;
    quint32 tablesChecksum = 0;
    bool headerChecksumValid = false;
    bool tablesChecksumValid = false;
    QVector<PartitionEntry> partitions;
    QVector<ExtentEntry> extents;
    QVector<GroupEntry> groups;
    QVector<BlockDeviceEntry> blockDevices;

    QByteArray serialize() const;
    quint64 dataEndSector() const;
};

struct SuperInfo {
    Geometry geometry;
    QVector<Metadata> metadataSlots;
    int activeSlot = -1;
    qint64 fileSize = 0;
    bool sparseWrapped = false;
    quint32 sectorSize = 512;
    QStringList warnings;

    const Metadata* active() const;
};

/// 解析 super.img（自动识别 sparse 包装；sparse 时只读前置 metadata 区域）。
SuperInfo inspectFile(const QString& path);
/// 解析 metadata 缓冲区（前 4096 字节为 geometry）。
SuperInfo inspectBuffer(const QByteArray& data);
/// 校验 geometry / header / tables 的 CRC 并返回可读描述。
QStringList validate(const SuperInfo& info);

/// 拆分 super.img 到目录（每个分区输出 <名字>.img）。
void split(const QString& superPath, const SuperInfo& info, const QString& outDir,
           const QStringList& onlyPartitions, bool sparseOutput, const ProgressFn& progress = {},
           const CancelFn& cancel = {});

/// 把目录中的分区镜像合并成 super.img。
void merge(const QString& sourceDir, const QStringList& partitionNames, const QString& outPath,
           quint64 superSize, quint32 metadataSlotCount, bool sparseOutput,
           const ProgressFn& progress = {}, const CancelFn& cancel = {});

/// 按 group 限制扩容/缩容后重写 super.img（仅调整镜像尾部大小）。
void resize(const QString& superPath, const QString& outPath, quint64 newSize,
            const ProgressFn& progress = {}, const CancelFn& cancel = {});

} // namespace gf::img::superimg
