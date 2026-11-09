#include "SuperImage.h"

#include "Sparse.h"

#include "core/Util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace gf::img::superimg {

namespace {

constexpr quint32 kPartitionEntrySize = 52;
constexpr quint32 kExtentEntrySize = 24;
constexpr quint32 kExtentEntrySizePadded = 32;
constexpr quint32 kGroupEntrySize = 48;
constexpr quint32 kBlockDeviceEntrySize = 64;

quint16 rd16(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 2 > d.size())
        fail(QStringLiteral("super 元数据被截断"));
    return quint16(quint8(d.at(at)) | (quint8(d.at(at + 1)) << 8));
}

quint32 rd32(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        fail(QStringLiteral("super 元数据被截断"));
    return quint32(quint8(d.at(at))) | (quint32(quint8(d.at(at + 1))) << 8) |
           (quint32(quint8(d.at(at + 2))) << 16) | (quint32(quint8(d.at(at + 3))) << 24);
}

quint64 rd64(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 8 > d.size())
        fail(QStringLiteral("super 元数据被截断"));
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v |= quint64(quint8(d.at(at + i))) << (8 * i);
    return v;
}

void wr16(QByteArray& d, quint16 v)
{
    d.append(char(v & 0xFF));
    d.append(char((v >> 8) & 0xFF));
}

void wr32(QByteArray& d, quint32 v)
{
    d.append(char(v & 0xFF));
    d.append(char((v >> 8) & 0xFF));
    d.append(char((v >> 16) & 0xFF));
    d.append(char((v >> 24) & 0xFF));
}

void wr64(QByteArray& d, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        d.append(char((v >> (8 * i)) & 0xFF));
}

void put32(QByteArray& d, qsizetype at, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        d[at + i] = char((v >> (8 * i)) & 0xFF);
}

void put64(QByteArray& d, qsizetype at, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        d[at + i] = char((v >> (8 * i)) & 0xFF);
}

QString cstrField(const QByteArray& d, qsizetype at, qsizetype maxSize)
{
    const QByteArray raw = d.mid(at, maxSize);
    const qsizetype nul = raw.indexOf('\0');
    return QString::fromUtf8(nul < 0 ? raw : raw.left(nul));
}

void putCstr(QByteArray& d, qsizetype at, qsizetype maxSize, const QString& text)
{
    QByteArray bytes = text.toUtf8();
    if (bytes.size() > maxSize)
        bytes.truncate(maxSize);
    bytes = bytes.leftJustified(maxSize, '\0');
    d.replace(at, maxSize, bytes);
}

quint64 alignUp(quint64 value, quint64 alignment)
{
    if (alignment <= 1)
        return value;
    const quint64 rem = value % alignment;
    return rem == 0 ? value : value + (alignment - rem);
}

Geometry parseGeometry(const QByteArray& data)
{
    Geometry g;
    if (data.size() < 24)
        fail(QStringLiteral("super geometry 数据被截断"));
    g.magic = rd32(data, 0);
    if (g.magic != kGeometryMagic)
        fail(QStringLiteral("不是 Android super 镜像（geometry magic=0x%1）")
                 .arg(g.magic, 8, 16, QLatin1Char('0')));
    g.structSize = rd32(data, 4);
    g.checksum = rd32(data, 8);
    g.metadataMaxSize = rd32(data, 12);
    g.metadataSlotCount = rd32(data, 16);
    g.logicalBlockSize = rd32(data, 20);
    if (g.structSize < 24 || g.structSize > 4096)
        fail(QStringLiteral("super geometry 结构大小非法：%1").arg(g.structSize));
    if (g.metadataMaxSize == 0 || g.metadataMaxSize > 16u * 1024 * 1024)
        fail(QStringLiteral("super 元数据区大小非法：%1").arg(g.metadataMaxSize));
    if (g.metadataSlotCount == 0 || g.metadataSlotCount > 8)
        fail(QStringLiteral("super 元数据槽数量非法：%1").arg(g.metadataSlotCount));
    QByteArray crcData = data.left(qsizetype(g.structSize));
    crcData.replace(8, 4, QByteArray(4, '\0'));
    g.checksumValid = crc32(crcData) == g.checksum;
    return g;
}

struct TableDescriptor {
    quint32 offset = 0;
    quint32 numEntries = 0;
    quint32 entrySize = 0;
};

TableDescriptor readDescriptor(const QByteArray& data, qsizetype at)
{
    TableDescriptor d;
    d.offset = rd32(data, at);
    d.numEntries = rd32(data, at + 4);
    d.entrySize = rd32(data, at + 8);
    return d;
}

Metadata parseMetadataSlot(const QByteArray& data, quint32 slot, quint64 sourceOffset)
{
    Metadata m;
    m.slot = slot;
    if (data.size() < 92)
        fail(QStringLiteral("super 元数据头被截断（槽 %1）").arg(slot));
    if (rd32(data, 0) != kHeaderMagic)
        fail(QStringLiteral("super 元数据 magic 不匹配（槽 %1）").arg(slot));
    m.majorVersion = rd16(data, 4);
    m.minorVersion = rd16(data, 6);
    m.headerSize = rd32(data, 8);
    m.headerChecksum = rd32(data, 12);
    m.tablesSize = rd32(data, 16);
    m.tablesChecksum = rd32(data, 20);
    m.flags = rd32(data, 88);
    if (m.majorVersion < 10)
        fail(QStringLiteral("不支持的 super 元数据版本：%1.%2").arg(m.majorVersion).arg(m.minorVersion));
    if (m.headerSize < 92 || m.headerSize > qsizetype(data.size()))
        fail(QStringLiteral("super 元数据头大小非法：%1").arg(m.headerSize));
    if (m.headerSize + m.tablesSize > quint64(data.size()))
        fail(QStringLiteral("super 元数据表超出数据范围"));

    QByteArray headerCrc = data.left(qsizetype(m.headerSize));
    headerCrc.replace(12, 4, QByteArray(4, '\0'));
    m.headerChecksumValid = crc32(headerCrc) == m.headerChecksum;
    const QByteArray tables = data.mid(qsizetype(m.headerSize), qsizetype(m.tablesSize));
    m.tablesChecksumValid = crc32(tables) == m.tablesChecksum;

    const TableDescriptor partitions = readDescriptor(data, 24);
    const TableDescriptor extents = readDescriptor(data, 40);
    const TableDescriptor groups = readDescriptor(data, 56);
    const TableDescriptor blockDevices = readDescriptor(data, 72);

    auto validateTable = [&](const TableDescriptor& d, const QString& name) {
        if (d.numEntries == 0)
            return;
        if (d.entrySize == 0 || d.numEntries > 65536u)
            fail(QStringLiteral("super 元数据表 %1 参数非法").arg(name));
        if (quint64(d.offset) + quint64(d.numEntries) * d.entrySize > quint64(data.size()))
            fail(QStringLiteral("super 元数据表 %1 超出范围").arg(name));
    };
    validateTable(partitions, QStringLiteral("partitions"));
    validateTable(extents, QStringLiteral("extents"));
    validateTable(groups, QStringLiteral("groups"));
    validateTable(blockDevices, QStringLiteral("block_devices"));

    for (quint32 i = 0; i < partitions.numEntries; ++i) {
        if (partitions.entrySize < kPartitionEntrySize)
            fail(QStringLiteral("super 分区表项大小非法：%1").arg(partitions.entrySize));
        const qsizetype at = qsizetype(partitions.offset) + qsizetype(i) * partitions.entrySize;
        PartitionEntry p;
        p.name = cstrField(data, at, 36);
        p.attributes = rd32(data, at + 36);
        p.firstExtentIndex = rd32(data, at + 40);
        p.numExtents = rd32(data, at + 44);
        p.groupIndex = rd32(data, at + 48);
        m.partitions.append(p);
    }
    const bool paddedExtents = extents.entrySize == kExtentEntrySizePadded;
    for (quint32 i = 0; i < extents.numEntries; ++i) {
        if (extents.entrySize < kExtentEntrySize)
            fail(QStringLiteral("super extent 表项大小非法：%1").arg(extents.entrySize));
        const qsizetype at = qsizetype(extents.offset) + qsizetype(i) * extents.entrySize;
        ExtentEntry e;
        e.numSectors = rd64(data, at);
        e.targetType = rd32(data, at + 8);
        e.targetData = paddedExtents ? rd64(data, at + 16) : rd64(data, at + 12);
        e.targetSource = paddedExtents ? rd32(data, at + 24) : rd32(data, at + 20);
        m.extents.append(e);
    }
    for (quint32 i = 0; i < groups.numEntries; ++i) {
        if (groups.entrySize < kGroupEntrySize)
            fail(QStringLiteral("super group 表项大小非法：%1").arg(groups.entrySize));
        const qsizetype at = qsizetype(groups.offset) + qsizetype(i) * groups.entrySize;
        GroupEntry g;
        g.name = cstrField(data, at, 36);
        g.flags = rd32(data, at + 36);
        g.maximumSize = rd64(data, at + 40);
        m.groups.append(g);
    }
    for (quint32 i = 0; i < blockDevices.numEntries; ++i) {
        if (blockDevices.entrySize < kBlockDeviceEntrySize)
            fail(QStringLiteral("super block_device 表项大小非法：%1").arg(blockDevices.entrySize));
        const qsizetype at = qsizetype(blockDevices.offset) + qsizetype(i) * blockDevices.entrySize;
        BlockDeviceEntry b;
        b.firstLogicalSector = rd64(data, at);
        b.alignment = rd32(data, at + 8);
        b.alignmentOffset = rd32(data, at + 12);
        b.size = rd64(data, at + 16);
        b.partitionName = cstrField(data, at + 24, 36);
        b.flags = rd32(data, at + 60);
        m.blockDevices.append(b);
    }
    m.slot = slot;
    Q_UNUSED(sourceOffset);
    return m;
}

/// 读取（可能是 sparse 包装的）镜像前 needed 字节的原始数据。
QByteArray readRawPrefix(const QString& path, qint64 needed, bool* wasSparse, qint64* fileSize)
{
    FileReader reader(path);
    if (fileSize != nullptr)
        *fileSize = reader.size();
    const QByteArray head = reader.readAt(0, qMin<qint64>(28, reader.size()));
    if (sparse::isSparse(head)) {
        if (wasSparse != nullptr)
            *wasSparse = true;
        // 遍历 sparse chunk，按需拼出前 needed 字节。
        const quint32 blockSize = quint32(quint8(head.at(12))) |
                                  (quint32(quint8(head.at(13))) << 8) |
                                  (quint32(quint8(head.at(14))) << 16) |
                                  (quint32(quint8(head.at(15))) << 24);
        const quint32 totalChunks = quint32(quint8(head.at(20))) |
                                    (quint32(quint8(head.at(21))) << 8) |
                                    (quint32(quint8(head.at(22))) << 16) |
                                    (quint32(quint8(head.at(23))) << 24);
        const qsizetype headerSize = qsizetype(quint8(head.at(8)) | (quint8(head.at(9)) << 8));
        QByteArray out;
        out.reserve(qsizetype(needed));
        qint64 pos = headerSize;
        for (quint32 i = 0; i < totalChunks && out.size() < needed; ++i) {
            const QByteArray chunkHead = reader.readAt(pos, 12);
            const quint16 type = quint16(quint8(chunkHead.at(0)) | (quint8(chunkHead.at(1)) << 8));
            const quint32 blocks = quint32(quint8(chunkHead.at(4))) |
                                   (quint32(quint8(chunkHead.at(5))) << 8) |
                                   (quint32(quint8(chunkHead.at(6))) << 16) |
                                   (quint32(quint8(chunkHead.at(7))) << 24);
            const quint32 totalSize = quint32(quint8(chunkHead.at(8))) |
                                      (quint32(quint8(chunkHead.at(9))) << 8) |
                                      (quint32(quint8(chunkHead.at(10))) << 16) |
                                      (quint32(quint8(chunkHead.at(11))) << 24);
            const quint64 chunkBytes = quint64(blocks) * blockSize;
            if (type == 0xCAC1) { // raw
                const qint64 want = qMin<qint64>(qint64(chunkBytes), needed - out.size());
                out.append(reader.readAt(pos + 12, want));
            } else if (type == 0xCAC2) { // fill
                const QByteArray pattern = reader.readAt(pos + 12, 4);
                const qint64 want = qMin<qint64>(qint64(chunkBytes), needed - out.size());
                for (qint64 k = 0; k < want; ++k)
                    out.append(pattern.at(k % 4));
            } else if (type == 0xCAC3) { // dontcare
                const qint64 want = qMin<qint64>(qint64(chunkBytes), needed - out.size());
                out.append(QByteArray(qsizetype(want), '\0'));
            }
            pos += totalSize;
        }
        if (out.size() < needed)
            fail(QStringLiteral("sparse 包装的 super 镜像元数据区不完整"));
        return out;
    }
    if (wasSparse != nullptr)
        *wasSparse = false;
    return reader.readAt(0, needed);
}

QByteArray buildGeometry(const Geometry& g)
{
    QByteArray out(kReservedBytes, '\0');
    put32(out, 0, kGeometryMagic);
    put32(out, 4, 24);
    put32(out, 8, 0);
    put32(out, 12, g.metadataMaxSize);
    put32(out, 16, g.metadataSlotCount);
    put32(out, 20, g.logicalBlockSize);
    const quint32 crc = crc32(out.left(24));
    put32(out, 8, crc);
    return out;
}

Metadata makeMetadata(const QVector<PartitionEntry>& partitions, const QVector<ExtentEntry>& extents,
                      const QVector<GroupEntry>& groups, const QVector<BlockDeviceEntry>& devices,
                      quint32 slot, quint32 flags)
{
    Metadata m;
    m.slot = slot;
    m.majorVersion = 10;
    m.minorVersion = 0;
    m.headerSize = kHeaderSize;
    m.partitions = partitions;
    m.extents = extents;
    m.groups = groups;
    m.blockDevices = devices;
    m.flags = flags;
    return m;
}

} // namespace

QString ExtentEntry::typeText() const
{
    switch (TargetType(targetType)) {
    case TargetType::Linear:
        return QStringLiteral("linear");
    case TargetType::Zero:
        return QStringLiteral("zero");
    }
    return QStringLiteral("未知(%1)").arg(targetType);
}

QStringList validate(const SuperInfo& info)
{
    QStringList issues;
    if (!info.geometry.checksumValid)
        issues << QStringLiteral("geometry 校验和不匹配（可能是旧工具生成的镜像）");
    if (info.activeSlot < 0 || info.activeSlot >= info.metadataSlots.size())
        issues << QStringLiteral("没有可用的元数据槽");
    else {
        const Metadata& m = info.metadataSlots.at(info.activeSlot);
        if (!m.headerChecksumValid)
            issues << QStringLiteral("元数据头校验和不匹配");
        if (!m.tablesChecksumValid)
            issues << QStringLiteral("元数据表校验和不匹配");
        const quint64 end = m.dataEndSector();
        if (!info.sparseWrapped && info.fileSize > 0 && end * 512 > quint64(info.fileSize))
            issues << QStringLiteral("分区数据结束位置（%1）超出文件大小（%2）")
                          .arg(formatBytes(end * 512), formatBytes(quint64(info.fileSize)));
    }
    return issues;
}

const Metadata* SuperInfo::active() const
{
    if (activeSlot < 0 || activeSlot >= metadataSlots.size())
        return nullptr;
    return &metadataSlots.at(activeSlot);
}

quint64 Metadata::dataEndSector() const
{
    quint64 end = 0;
    for (const auto& e : extents) {
        if (TargetType(e.targetType) == TargetType::Linear)
            end = qMax(end, e.targetData + e.numSectors);
    }
    return end;
}

QByteArray Metadata::serialize() const
{
    QByteArray tables;
    const quint32 partitionsOffset = kHeaderSize;
    for (const auto& p : partitions) {
        QByteArray entry(kPartitionEntrySize, '\0');
        putCstr(entry, 0, 36, p.name);
        put32(entry, 36, p.attributes);
        put32(entry, 40, p.firstExtentIndex);
        put32(entry, 44, p.numExtents);
        put32(entry, 48, p.groupIndex);
        tables.append(entry);
    }
    const quint32 extentsOffset = partitionsOffset + quint32(tables.size());
    for (const auto& e : extents) {
        QByteArray entry(kExtentEntrySize, '\0');
        put64(entry, 0, e.numSectors);
        put32(entry, 8, e.targetType);
        put64(entry, 12, e.targetData);
        put32(entry, 20, e.targetSource);
        tables.append(entry);
    }
    const quint32 groupsOffset = partitionsOffset + quint32(tables.size());
    for (const auto& g : groups) {
        QByteArray entry(kGroupEntrySize, '\0');
        putCstr(entry, 0, 36, g.name);
        put32(entry, 36, g.flags);
        put64(entry, 40, g.maximumSize);
        tables.append(entry);
    }
    const quint32 devicesOffset = partitionsOffset + quint32(tables.size());
    for (const auto& b : blockDevices) {
        QByteArray entry(kBlockDeviceEntrySize, '\0');
        put64(entry, 0, b.firstLogicalSector);
        put32(entry, 8, b.alignment);
        put32(entry, 12, b.alignmentOffset);
        put64(entry, 16, b.size);
        putCstr(entry, 24, 36, b.partitionName);
        put32(entry, 60, b.flags);
        tables.append(entry);
    }

    QByteArray header(kHeaderSize, '\0');
    put32(header, 0, kHeaderMagic);
    header[4] = char(majorVersion & 0xFF);
    header[5] = char((majorVersion >> 8) & 0xFF);
    header[6] = char(minorVersion & 0xFF);
    header[7] = char((minorVersion >> 8) & 0xFF);
    put32(header, 8, kHeaderSize);
    put32(header, 12, 0); // header checksum 占位
    put32(header, 16, quint32(tables.size()));
    put32(header, 20, crc32(tables));
    put32(header, 24, partitionsOffset);
    put32(header, 28, quint32(partitions.size()));
    put32(header, 32, kPartitionEntrySize);
    put32(header, 36, 0);
    put32(header, 40, extentsOffset);
    put32(header, 44, quint32(extents.size()));
    put32(header, 48, kExtentEntrySize);
    put32(header, 52, 0);
    put32(header, 56, groupsOffset);
    put32(header, 60, quint32(groups.size()));
    put32(header, 64, kGroupEntrySize);
    put32(header, 68, 0);
    put32(header, 72, devicesOffset);
    put32(header, 76, quint32(blockDevices.size()));
    put32(header, 80, kBlockDeviceEntrySize);
    put32(header, 84, 0);
    put32(header, 88, flags);
    const quint32 headerCrc = crc32(header);
    put32(header, 12, headerCrc);
    return header + tables;
}

SuperInfo inspectBuffer(const QByteArray& data)
{
    SuperInfo info;
    info.geometry = parseGeometry(data);
    info.sectorSize = 512;
    const qint64 slotArea = qint64(info.geometry.metadataSlotCount) * info.geometry.metadataMaxSize;
    for (quint32 slot = 0; slot < info.geometry.metadataSlotCount; ++slot) {
        const qint64 offset = qint64(kReservedBytes) + qint64(slot) * info.geometry.metadataMaxSize;
        if (offset + 92 > qint64(data.size())) {
            info.warnings << QStringLiteral("元数据槽 %1 超出数据范围，已忽略").arg(slot);
            break;
        }
        const qint64 available = qMin<qint64>(info.geometry.metadataMaxSize,
                                             qint64(data.size()) - offset);
        const QByteArray area = data.mid(qsizetype(offset), qsizetype(available));
        if (area.size() < 92 || quint32(quint8(area.at(0))) != 0x30 || quint8(area.at(1)) != 0x50 ||
            quint8(area.at(2)) != 0x4C || quint8(area.at(3)) != 0x41) {
            info.warnings << QStringLiteral("元数据槽 %1 未写入（magic 缺失）").arg(slot);
            continue;
        }
        try {
            Metadata m = parseMetadataSlot(area, slot, quint64(offset));
            info.metadataSlots.append(m);
            if (info.activeSlot < 0 && m.headerChecksumValid && m.tablesChecksumValid)
                info.activeSlot = int(info.metadataSlots.size()) - 1;
        } catch (const std::exception& e) {
            info.warnings << QStringLiteral("元数据槽 %1 解析失败：%2")
                                 .arg(slot)
                                 .arg(QString::fromUtf8(e.what()));
        }
    }
    if (info.activeSlot < 0 && !info.metadataSlots.isEmpty()) {
        info.activeSlot = 0;
        info.warnings << QStringLiteral("所有元数据槽校验和均无效，仍按槽 0 解析");
    }
    Q_UNUSED(slotArea);
    return info;
}

SuperInfo inspectFile(const QString& path)
{
    SuperInfo info;
    bool sparseWrapped = false;
    qint64 fileSize = 0;
    const QByteArray geometryPrefix = readRawPrefix(path, qint64(kReservedBytes), &sparseWrapped, &fileSize);
    info.sparseWrapped = sparseWrapped;
    info.fileSize = fileSize;
    info.geometry = parseGeometry(geometryPrefix);
    const qint64 needed = qint64(kReservedBytes) +
                          qint64(info.geometry.metadataSlotCount) * info.geometry.metadataMaxSize;
    QByteArray data = geometryPrefix;
    if (needed > data.size())
        data = readRawPrefix(path, needed, &sparseWrapped, &fileSize);
    SuperInfo parsed = inspectBuffer(data);
    parsed.sparseWrapped = sparseWrapped;
    parsed.fileSize = fileSize;
    if (sparseWrapped)
        parsed.warnings << QStringLiteral("super.img 为 sparse 包装；拆分时会先解包为原始镜像");
    return parsed;
}

void split(const QString& superPath, const SuperInfo& info, const QString& outDir,
           const QStringList& onlyPartitions, bool sparseOutput, const ProgressFn& progress,
           const CancelFn& cancel)
{
    const Metadata* metadata = info.active();
    if (metadata == nullptr)
        fail(QStringLiteral("super 镜像没有可用的元数据，无法拆分"));
    ensureParentDirectory(gf::img::joinPath(outDir, QStringLiteral("x")));

    QString sourcePath = superPath;
    QString tempPath;
    if (info.sparseWrapped) {
        tempPath = QDir::tempPath() + QStringLiteral("/xmf-super-") +
                   QFileInfo(superPath).completeBaseName() + QStringLiteral(".raw");
        reportProgress(progress, 3, QStringLiteral("解包 sparse 包装的 super 镜像"));
        sparse::unpackFile(superPath, tempPath, progress, cancel);
        sourcePath = tempPath;
    }

    struct Job {
        const PartitionEntry* partition = nullptr;
        QString name;
        quint64 sizeBytes = 0;
        QVector<ExtentEntry> extents;
    };
    QVector<Job> jobs;
    for (const auto& partition : metadata->partitions) {
        if (!onlyPartitions.isEmpty()) {
            bool wanted = false;
            for (const auto& want : onlyPartitions) {
                if (partition.name.compare(want, Qt::CaseInsensitive) == 0) {
                    wanted = true;
                    break;
                }
            }
            if (!wanted)
                continue;
        }
        Job job;
        job.partition = &partition;
        job.name = partition.name;
        const quint32 first = partition.firstExtentIndex;
        const quint32 count = partition.numExtents;
        if (quint64(first) + quint64(count) > quint64(metadata->extents.size()))
            fail(QStringLiteral("分区 %1 的 extent 索引越界").arg(partition.name));
        for (quint32 i = 0; i < count; ++i)
            job.extents.append(metadata->extents.at(int(first + i)));
        for (const auto& e : job.extents) {
            if (TargetType(e.targetType) == TargetType::Linear)
                job.sizeBytes += e.numSectors * 512;
        }
        jobs.append(job);
    }
    if (jobs.isEmpty())
        fail(QStringLiteral("super 镜像中未找到指定分区"));

    FileReader reader(sourcePath);
    int index = 0;
    for (const auto& job : jobs) {
        checkCancel(cancel);
        ++index;
        reportProgress(progress, 5 + int(90.0 * index / jobs.size()),
                       QStringLiteral("导出分区 %1").arg(job.name));
        QString safeName = job.name;
        safeName.replace(QLatin1Char('/'), QLatin1Char('_'));
        const QString finalPath = gf::img::joinPath(outDir, safeName + QStringLiteral(".img"));
        const QString rawPath = sparseOutput ? finalPath + QStringLiteral(".rawtmp") : finalPath;
        FileWriter writer(rawPath, true);
        for (const auto& extent : job.extents) {
            const quint64 bytes = extent.numSectors * 512;
            if (TargetType(extent.targetType) == TargetType::Linear) {
                writer.write(reader.readAt(qint64(extent.targetData * 512), qint64(bytes)));
            } else if (TargetType(extent.targetType) == TargetType::Zero) {
                writer.write(QByteArray(qsizetype(bytes), '\0'));
            } else {
                fail(QStringLiteral("分区 %1 使用了不支持的映射类型 %2")
                         .arg(job.name)
                         .arg(extent.targetType));
            }
        }
        writer.close();
        if (sparseOutput) {
            sparse::packFile(rawPath, finalPath, 4096, true, true);
            QFile::remove(rawPath);
        }
    }
    if (!tempPath.isEmpty())
        QFile::remove(tempPath);
    reportProgress(progress, 100, QStringLiteral("super 拆分完成"));
}

void merge(const QString& sourceDir, const QStringList& partitionNames, const QString& outPath,
           quint64 superSize, quint32 metadataSlotCount, bool sparseOutput,
           const ProgressFn& progress, const CancelFn& cancel)
{
    if (partitionNames.isEmpty())
        fail(QStringLiteral("没有指定要合并的分区"));
    if (metadataSlotCount == 0)
        metadataSlotCount = 2;
    if (metadataSlotCount > 4)
        fail(QStringLiteral("元数据槽数量过多（最多 4）"));

    struct Source {
        QString name;
        QString path;
        quint64 size = 0;
        quint64 startSector = 0;
    };
    QVector<Source> sources;
    for (const auto& name : partitionNames) {
        QString path = gf::img::joinPath(sourceDir, name);
        if (!QFileInfo::exists(path)) {
            const QString withExt = path + QStringLiteral(".img");
            if (QFileInfo::exists(withExt))
                path = withExt;
            else
                fail(QStringLiteral("找不到分区镜像：%1").arg(name));
        }
        Source source;
        source.name = name;
        source.path = path;
        source.size = quint64(QFileInfo(path).size());
        sources.append(source);
    }

    QVector<PartitionEntry> partitions;
    QVector<ExtentEntry> extents;
    quint64 cursor = kFirstLogicalSector;
    for (auto& source : sources) {
        source.startSector = alignUp(cursor, kAlignmentBytes / 512);
        cursor = source.startSector + (source.size + 511) / 512;
        PartitionEntry p;
        p.name = source.name;
        p.attributes = 0;
        p.firstExtentIndex = quint32(extents.size());
        p.numExtents = 1;
        p.groupIndex = 0;
        partitions.append(p);
        ExtentEntry e;
        e.numSectors = (source.size + 511) / 512;
        e.targetType = quint32(TargetType::Linear);
        e.targetData = source.startSector;
        e.targetSource = 0;
        extents.append(e);
    }

    QVector<GroupEntry> groups;
    GroupEntry group;
    group.name = QStringLiteral("default");
    group.flags = 0;
    group.maximumSize = 0;
    groups.append(group);

    const quint64 dataEnd = alignUp(cursor * 512, kAlignmentBytes);
    quint64 totalSize = 0;
    if (superSize > 0) {
        if (superSize < dataEnd)
            fail(QStringLiteral("指定的 super 大小（%1）小于分区数据所需大小（%2）")
                     .arg(formatBytes(superSize), formatBytes(dataEnd)));
        totalSize = superSize;
    } else {
        totalSize = dataEnd;
    }
    totalSize = alignUp(totalSize, 512);

    QVector<BlockDeviceEntry> devices;
    BlockDeviceEntry device;
    device.firstLogicalSector = kFirstLogicalSector;
    device.alignment = quint32(kAlignmentBytes);
    device.alignmentOffset = 0;
    device.size = totalSize / 512;
    device.partitionName = QStringLiteral("super");
    device.flags = 0;
    devices.append(device);

    const Geometry geometry = [&] {
        Geometry g;
        g.metadataMaxSize = kDefaultMetadataMaxSize;
        g.metadataSlotCount = metadataSlotCount;
        g.logicalBlockSize = kLogicalBlockSize;
        return g;
    }();

    const QString rawPath = sparseOutput ? outPath + QStringLiteral(".rawtmp") : outPath;
    FileWriter writer(rawPath, true);
    writer.resize(qint64(totalSize));
    writer.writeAt(0, buildGeometry(geometry));

    for (quint32 slot = 0; slot < metadataSlotCount; ++slot) {
        Metadata metadata = makeMetadata(partitions, extents, groups, devices, slot, 0);
        const QByteArray bytes = metadata.serialize();
        if (bytes.size() > qsizetype(geometry.metadataMaxSize))
            fail(QStringLiteral("元数据大小（%1 字节）超出槽大小（%2 字节）")
                     .arg(bytes.size())
                     .arg(geometry.metadataMaxSize));
        const qint64 offset = qint64(kReservedBytes) + qint64(slot) * geometry.metadataMaxSize;
        writer.writeAt(offset, bytes);
    }

    for (int i = 0; i < sources.size(); ++i) {
        checkCancel(cancel);
        reportProgress(progress, 10 + int(85.0 * (i + 1) / sources.size()),
                       QStringLiteral("写入分区 %1").arg(sources.at(i).name));
        QFile file(sources.at(i).path);
        if (!file.open(QIODevice::ReadOnly))
            fail(QStringLiteral("无法读取分区镜像：%1").arg(sources.at(i).path));
        qint64 offset = qint64(sources.at(i).startSector * 512);
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(4 * 1024 * 1024);
            if (chunk.isEmpty() && file.error() != QFile::NoError)
                fail(QStringLiteral("读取分区镜像失败：%1").arg(sources.at(i).path));
            writer.writeAt(offset, chunk);
            offset += chunk.size();
        }
    }
    writer.close();
    if (sparseOutput) {
        reportProgress(progress, 96, QStringLiteral("打包 sparse 输出"));
        sparse::packFile(rawPath, outPath, 4096, true, true);
        QFile::remove(rawPath);
    }
    reportProgress(progress, 100, QStringLiteral("super 合并完成：%1").arg(formatBytes(totalSize)));
}

void resize(const QString& superPath, const QString& outPath, quint64 newSize,
            const ProgressFn& progress, const CancelFn& cancel)
{
    SuperInfo info = inspectFile(superPath);
    const Metadata* metadata = info.active();
    if (metadata == nullptr)
        fail(QStringLiteral("super 镜像没有可用的元数据，无法扩容"));
    const quint64 dataEnd = metadata->dataEndSector() * 512;
    if (newSize < dataEnd)
        fail(QStringLiteral("新大小（%1）小于已有分区数据结束位置（%2）")
                 .arg(formatBytes(newSize), formatBytes(dataEnd)));
    if (newSize < quint64(kReservedBytes) + quint64(info.geometry.metadataSlotCount) *
                                        info.geometry.metadataMaxSize)
        fail(QStringLiteral("新大小（%1）小于元数据区大小").arg(formatBytes(newSize)));
    if (newSize % 512 != 0)
        fail(QStringLiteral("super 大小必须是 512 字节的整数倍"));

    QString sourcePath = superPath;
    QString tempPath;
    if (info.sparseWrapped) {
        tempPath = QDir::tempPath() + QStringLiteral("/xmf-super-resize-") +
                   QFileInfo(superPath).completeBaseName() + QStringLiteral(".raw");
        sparse::unpackFile(superPath, tempPath, progress, cancel);
        sourcePath = tempPath;
    }

    FileReader reader(sourcePath);
    FileWriter writer(outPath, true);
    writer.resize(qint64(newSize));
    const qint64 copyBytes = qMin<qint64>(reader.size(), qint64(newSize));
    qint64 done = 0;
    while (done < copyBytes) {
        checkCancel(cancel);
        const qint64 chunk = qMin<qint64>(4 * 1024 * 1024, copyBytes - done);
        writer.writeAt(done, reader.readAt(done, chunk));
        done += chunk;
        reportProgress(progress, int(80.0 * double(done) / double(copyBytes)),
                       QStringLiteral("复制数据 %1 / %2")
                           .arg(formatBytes(quint64(done)), formatBytes(quint64(copyBytes))));
    }

    // 更新 block device 大小并重写所有元数据槽。
    Metadata updated = *metadata;
    if (!updated.blockDevices.isEmpty())
        updated.blockDevices[0].size = newSize / 512;
    for (quint32 slot = 0; slot < info.geometry.metadataSlotCount; ++slot) {
        Metadata copy = updated;
        copy.slot = slot;
        const QByteArray bytes = copy.serialize();
        if (bytes.size() > qsizetype(info.geometry.metadataMaxSize))
            fail(QStringLiteral("调整后元数据超出槽大小"));
        const qint64 offset = qint64(kReservedBytes) + qint64(slot) * info.geometry.metadataMaxSize;
        writer.writeAt(offset, bytes);
    }
    writer.close();
    if (!tempPath.isEmpty())
        QFile::remove(tempPath);
    reportProgress(progress, 100, QStringLiteral("super 大小已调整为 %1").arg(formatBytes(newSize)));
}

} // namespace gf::img::superimg
