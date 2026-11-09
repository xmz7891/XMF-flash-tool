#include "Payload.h"

#include "Proto.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <vector>

namespace gf::img::payload {

namespace {

quint32 rd32be(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        fail(QStringLiteral("payload 数据被截断"));
    return (quint32(quint8(d.at(at))) << 24) | (quint32(quint8(d.at(at + 1))) << 16) |
           (quint32(quint8(d.at(at + 2))) << 8) | quint32(quint8(d.at(at + 3)));
}

quint64 rd64be(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 8 > d.size())
        fail(QStringLiteral("payload 数据被截断"));
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | quint64(quint8(d.at(at + i)));
    return v;
}

void wr32be(QByteArray& out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

void wr64be(QByteArray& out, quint64 v)
{
    for (int i = 7; i >= 0; --i)
        out.append(char((v >> (8 * i)) & 0xFF));
}

QByteArray sha256(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray sha256File(const QString& path, const ProgressFn& progress, int percentBase)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail(QStringLiteral("无法读取文件：%1").arg(path));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(4 * 1024 * 1024);
        if (chunk.isEmpty() && f.error() != QFile::NoError)
            fail(QStringLiteral("读取文件失败：%1").arg(path));
        hash.addData(chunk);
    }
    reportProgress(progress, percentBase, QStringLiteral("计算哈希 %1").arg(QFileInfo(path).fileName()));
    return hash.result();
}

Extent decodeExtent(const QByteArray& data)
{
    Extent extent;
    proto::Reader reader(data);
    proto::Field field;
    while (reader.next(field)) {
        if (field.number == 1 && field.wire == 0)
            extent.startBlock = field.varint;
        else if (field.number == 2 && field.wire == 0)
            extent.numBlocks = field.varint;
    }
    return extent;
}

QByteArray encodeExtent(const Extent& extent)
{
    QByteArray out;
    proto::writeVarintField(out, 1, extent.startBlock);
    proto::writeVarintField(out, 2, extent.numBlocks);
    return out;
}

PartitionInfo decodePartitionInfo(const QByteArray& data)
{
    PartitionInfo info;
    proto::Reader reader(data);
    proto::Field field;
    while (reader.next(field)) {
        if (field.number == 1 && field.wire == 0)
            info.size = field.varint;
        else if (field.number == 2 && field.wire == 2)
            info.hash = field.bytes;
    }
    return info;
}

Operation decodeOperation(const QByteArray& data)
{
    Operation op;
    proto::Reader reader(data);
    proto::Field field;
    while (reader.next(field)) {
        switch (field.number) {
        case 1:
            op.type = OperationType(qBound<quint64>(0, field.varint, 13));
            break;
        case 2:
            op.dataOffset = field.varint;
            break;
        case 3:
            op.dataLength = field.varint;
            break;
        case 4:
            if (field.wire == 2)
                op.srcExtents.append(decodeExtent(field.bytes));
            break;
        case 5:
            op.srcLength = field.varint;
            break;
        case 6:
            if (field.wire == 2)
                op.dstExtents.append(decodeExtent(field.bytes));
            break;
        case 7:
            op.dstLength = field.varint;
            break;
        case 8:
            if (field.wire == 2)
                op.dataSha256 = field.bytes;
            break;
        default:
            break;
        }
    }
    return op;
}

PartitionUpdate decodePartition(const QByteArray& data)
{
    PartitionUpdate update;
    proto::Reader reader(data);
    proto::Field field;
    while (reader.next(field)) {
        switch (field.number) {
        case 1:
            update.name = QString::fromUtf8(field.bytes);
            break;
        case 2:
            update.runPostinstall = field.varint != 0;
            break;
        case 4:
            update.filesystemType = QString::fromUtf8(field.bytes);
            break;
        case 5:
            if (field.wire == 2)
                update.operations.append(decodeOperation(field.bytes));
            break;
        case 6:
            if (field.wire == 2)
                update.newInfo = decodePartitionInfo(field.bytes);
            break;
        default:
            break;
        }
    }
    return update;
}

QByteArray encodeOperation(const Operation& op)
{
    QByteArray out;
    proto::writeVarintField(out, 1, quint64(op.type));
    proto::writeVarintField(out, 2, op.dataOffset);
    proto::writeVarintField(out, 3, op.dataLength);
    for (const auto& extent : op.srcExtents)
        proto::writeBytesField(out, 4, encodeExtent(extent));
    if (op.srcLength != 0)
        proto::writeVarintField(out, 5, op.srcLength);
    for (const auto& extent : op.dstExtents)
        proto::writeBytesField(out, 6, encodeExtent(extent));
    if (op.dstLength != 0)
        proto::writeVarintField(out, 7, op.dstLength);
    if (!op.dataSha256.isEmpty())
        proto::writeBytesField(out, 8, op.dataSha256);
    return out;
}

QByteArray encodePartition(const PartitionUpdate& update)
{
    QByteArray out;
    proto::writeStringField(out, 1, update.name);
    if (!update.filesystemType.isEmpty())
        proto::writeStringField(out, 4, update.filesystemType);
    for (const auto& op : update.operations)
        proto::writeBytesField(out, 5, encodeOperation(op));
    QByteArray info;
    proto::writeVarintField(info, 1, update.newInfo.size);
    if (!update.newInfo.hash.isEmpty())
        proto::writeBytesField(info, 2, update.newInfo.hash);
    proto::writeBytesField(out, 6, info);
    return out;
}

QString operationSupportText(OperationType type)
{
    switch (type) {
    case OperationType::Replace:
        return QStringLiteral("完整替换（支持）");
    case OperationType::Zero:
    case OperationType::Discard:
        return QStringLiteral("填充零（支持）");
    case OperationType::ReplaceBz:
        return QStringLiteral("bzip2 压缩块（未支持：构建环境无 bz2 库）");
    case OperationType::ReplaceXz:
        return QStringLiteral("xz 压缩块（未支持：构建环境无 lzma 库）");
    case OperationType::Move:
    case OperationType::SourceCopy:
    case OperationType::Bsdiff:
    case OperationType::SourceBsdiff:
    case OperationType::Puffdiff:
    case OperationType::BrotliBsdiff:
    case OperationType::Lz4diffBsdiff:
    case OperationType::Lz4diff:
    case OperationType::Zucchini:
        return QStringLiteral("增量/差分操作（不支持，需要原始镜像与差分算法）");
    }
    return QStringLiteral("未知操作");
}

bool operationExtractable(OperationType type)
{
    return type == OperationType::Replace || type == OperationType::Zero ||
           type == OperationType::Discard;
}

void writeZerosAt(FileWriter& writer, quint64 offset, quint64 length)
{
    static const QByteArray zeros(1024 * 1024, '\0');
    while (length > 0) {
        const qsizetype n = qsizetype(qMin<quint64>(length, quint64(zeros.size())));
        writer.writeAt(qint64(offset), QByteArray(zeros.constData(), n));
        offset += quint64(n);
        length -= quint64(n);
    }
}

} // namespace

QString operationTypeText(OperationType type)
{
    switch (type) {
    case OperationType::Replace:
        return QStringLiteral("REPLACE");
    case OperationType::ReplaceBz:
        return QStringLiteral("REPLACE_BZ");
    case OperationType::Move:
        return QStringLiteral("MOVE");
    case OperationType::Bsdiff:
        return QStringLiteral("BSDIFF");
    case OperationType::SourceCopy:
        return QStringLiteral("SOURCE_COPY");
    case OperationType::SourceBsdiff:
        return QStringLiteral("SOURCE_BSDIFF");
    case OperationType::Zero:
        return QStringLiteral("ZERO");
    case OperationType::Discard:
        return QStringLiteral("DISCARD");
    case OperationType::ReplaceXz:
        return QStringLiteral("REPLACE_XZ");
    case OperationType::Puffdiff:
        return QStringLiteral("PUFFDIFF");
    case OperationType::BrotliBsdiff:
        return QStringLiteral("BROTLI_BSDIFF");
    case OperationType::Lz4diffBsdiff:
        return QStringLiteral("LZ4DIFF_BSDIFF");
    case OperationType::Lz4diff:
        return QStringLiteral("LZ4DIFF");
    case OperationType::Zucchini:
        return QStringLiteral("ZUCCHINI");
    }
    return QStringLiteral("UNKNOWN");
}

QString supportSummary()
{
    return QStringLiteral("支持：REPLACE / ZERO / DISCARD；不支持：REPLACE_BZ、REPLACE_XZ 以及所有增量差分操作。");
}

Manifest decodeManifest(const QByteArray& data)
{
    Manifest manifest;
    proto::Reader reader(data);
    proto::Field field;
    while (reader.next(field)) {
        switch (field.number) {
        case 3:
            if (field.wire == 0)
                manifest.blockSize = quint32(field.varint);
            break;
        case 4:
            if (field.wire == 0)
                manifest.signaturesOffset = field.varint;
            break;
        case 5:
            if (field.wire == 0)
                manifest.signaturesSize = field.varint;
            break;
        case 12:
            if (field.wire == 0)
                manifest.minorVersion = quint32(field.varint);
            break;
        case 13:
            if (field.wire == 2)
                manifest.partitions.append(decodePartition(field.bytes));
            break;
        case 14:
            if (field.wire == 0)
                manifest.maxTimestamp = field.varint;
            break;
        case 16:
            if (field.wire == 0)
                manifest.partialUpdate = field.varint != 0;
            break;
        default:
            break;
        }
    }
    if (manifest.blockSize == 0)
        fail(QStringLiteral("payload manifest 中 block_size 为 0"));
    return manifest;
}

QByteArray encodeManifest(const Manifest& manifest)
{
    QByteArray out;
    proto::writeVarintField(out, 3, manifest.blockSize);
    proto::writeVarintField(out, 4, manifest.signaturesOffset);
    proto::writeVarintField(out, 5, manifest.signaturesSize);
    proto::writeVarintField(out, 12, manifest.minorVersion);
    for (const auto& partition : manifest.partitions)
        proto::writeBytesField(out, 13, encodePartition(partition));
    proto::writeVarintField(out, 14, manifest.maxTimestamp);
    if (manifest.partialUpdate)
        proto::writeVarintField(out, 16, 1);
    return out;
}

Payload parse(const QByteArray& data)
{
    if (data.size() < 20)
        fail(QStringLiteral("payload.bin 太小（%1 字节）").arg(data.size()));
    if (data.left(4) != QByteArray("CrAU"))
        fail(QStringLiteral("不是 payload.bin（magic 不匹配）"));
    Payload payload;
    payload.header.magic = QStringLiteral("CrAU");
    payload.header.version = rd64be(data, 4);
    payload.header.manifestSize = rd64be(data, 12);
    if (payload.header.version > 2)
        fail(QStringLiteral("不支持的 payload 格式版本：%1").arg(payload.header.version));
    qsizetype pos = 20;
    if (payload.header.version >= 2) {
        if (data.size() < 24)
            fail(QStringLiteral("payload 头被截断"));
        payload.header.metadataSignatureSize = rd32be(data, 20);
        pos = 24;
    }
    if (payload.header.manifestSize > quint64(data.size() - pos))
        fail(QStringLiteral("payload manifest 大小（%1）超出文件剩余大小（%2）")
                 .arg(payload.header.manifestSize)
                 .arg(data.size() - pos));
    payload.header.manifestOffset = pos;
    payload.header.dataOffset = pos + qint64(payload.header.manifestSize) +
                                qint64(payload.header.metadataSignatureSize);
    payload.manifest = decodeManifest(data.mid(pos, qsizetype(payload.header.manifestSize)));
    return payload;
}

Payload parseFile(const QString& path)
{
    FileReader reader(path);
    if (reader.size() < 24)
        fail(QStringLiteral("payload.bin 太小：%1").arg(path));
    const QByteArray head = reader.readAt(0, 24);
    if (head.left(4) != QByteArray("CrAU"))
        fail(QStringLiteral("不是 payload.bin（magic 不匹配）：%1").arg(path));
    const quint64 version = rd64be(head, 4);
    const quint64 manifestSize = rd64be(head, 12);
    qsizetype prefix = version >= 2 ? 24 : 20;
    if (quint64(prefix) + manifestSize > quint64(reader.size()))
        fail(QStringLiteral("payload manifest 超出文件大小：%1").arg(path));
    const QByteArray buffer = reader.readAt(0, prefix + qint64(manifestSize));
    return parse(buffer);
}

void extractAll(const QString& payloadPath, const Payload& payload, const QString& outDir,
                const QStringList& onlyPartitions, bool verifyHashes, const ProgressFn& progress,
                const CancelFn& cancel)
{
    FileReader reader(payloadPath);
    const Manifest& manifest = payload.manifest;
    QVector<const PartitionUpdate*> selected;
    if (onlyPartitions.isEmpty()) {
        for (const auto& partition : manifest.partitions)
            selected.append(&partition);
    } else {
        for (const auto& name : onlyPartitions) {
            const PartitionUpdate* found = nullptr;
            for (const auto& partition : manifest.partitions) {
                if (partition.name.compare(name, Qt::CaseInsensitive) == 0) {
                    found = &partition;
                    break;
                }
            }
            if (found == nullptr)
                fail(QStringLiteral("payload 中不存在分区：%1").arg(name));
            selected.append(found);
        }
    }
    if (selected.isEmpty())
        fail(QStringLiteral("payload 中没有任何分区"));
    ensureParentDirectory(gf::img::joinPath(outDir, QStringLiteral("x")));

    qint64 totalOps = 0;
    for (const auto* partition : selected)
        totalOps += partition->operations.size();
    qint64 doneOps = 0;

    for (const auto* partition : selected) {
        checkCancel(cancel);
        QString safeName = partition->name;
        safeName.replace(QLatin1Char('/'), QLatin1Char('_'));
        safeName.replace(QLatin1Char('\\'), QLatin1Char('_'));
        const QString outPath = gf::img::joinPath(outDir, safeName + QStringLiteral(".img"));
        FileWriter writer(outPath, true);

        quint64 finalSize = partition->newInfo.size;
        if (finalSize == 0) {
            for (const auto& op : partition->operations) {
                for (const auto& extent : op.dstExtents)
                    finalSize = qMax(finalSize, extent.byteOffset(manifest.blockSize) +
                                                    extent.byteLength(manifest.blockSize));
            }
        }
        if (finalSize == 0)
            finalSize = 1;
        writer.resize(qint64(finalSize));

        for (const auto& op : partition->operations) {
            checkCancel(cancel);
            ++doneOps;
            reportProgress(progress,
                           totalOps > 0 ? int(100.0 * double(doneOps) / double(totalOps)) : 50,
                           QStringLiteral("导出 %1（%2）").arg(partition->name, operationTypeText(op.type)));
            if (!operationExtractable(op.type))
                fail(QStringLiteral("分区 %1 包含不支持的操作 %2：%3")
                         .arg(partition->name, operationTypeText(op.type),
                              operationSupportText(op.type)));

            quint64 dstBytes = 0;
            for (const auto& extent : op.dstExtents)
                dstBytes += extent.byteLength(manifest.blockSize);
            if (op.dstLength != 0 && op.dstLength > dstBytes)
                fail(QStringLiteral("分区 %1 操作目标长度不一致（%2 > %3）")
                         .arg(partition->name)
                         .arg(op.dstLength)
                         .arg(dstBytes));

            if (op.type == OperationType::Zero || op.type == OperationType::Discard) {
                for (const auto& extent : op.dstExtents)
                    writeZerosAt(writer, extent.byteOffset(manifest.blockSize),
                                 extent.byteLength(manifest.blockSize));
                continue;
            }

            if (op.dataLength == 0)
                fail(QStringLiteral("分区 %1 的 REPLACE 操作没有数据块").arg(partition->name));
            const QByteArray blob = reader.readAt(qint64(op.dataOffset), qint64(op.dataLength));
            if (verifyHashes && !op.dataSha256.isEmpty() && sha256(blob) != op.dataSha256)
                fail(QStringLiteral("分区 %1 数据块 SHA256 校验失败").arg(partition->name));

            // REPLACE 的数据长度可能小于目标区域（最后一个块按实际镜像大小截断）。
            qsizetype cursor = 0;
            for (const auto& extent : op.dstExtents) {
                if (cursor >= blob.size())
                    break;
                const qsizetype length = qMin<qsizetype>(
                    qsizetype(extent.byteLength(manifest.blockSize)), blob.size() - cursor);
                writer.writeAt(qint64(extent.byteOffset(manifest.blockSize)),
                               blob.mid(cursor, length));
                cursor += length;
            }
            if (cursor != blob.size())
                fail(QStringLiteral("分区 %1 数据块（%2 字节）超出目标区域（%3 字节）")
                         .arg(partition->name)
                         .arg(blob.size())
                         .arg(dstBytes));
        }
        writer.close();
        if (verifyHashes && !partition->newInfo.hash.isEmpty()) {
            QFile f(outPath);
            if (!f.open(QIODevice::ReadOnly))
                fail(QStringLiteral("无法读取导出的镜像：%1").arg(outPath));
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&f))
                fail(QStringLiteral("计算导出镜像哈希失败：%1").arg(outPath));
            if (hash.result() != partition->newInfo.hash)
                fail(QStringLiteral("分区 %1 整镜像哈希校验失败").arg(partition->name));
        }
    }
    reportProgress(progress, 100, QStringLiteral("payload 导出完成"));
}

void packDirectory(const QString& sourceDir, const QStringList& partitionNames,
                   const QString& outputPath, quint32 blockSize, const ProgressFn& progress,
                   const CancelFn& cancel)
{
    if (blockSize == 0)
        fail(QStringLiteral("payload 块大小不能为 0"));
    if (partitionNames.isEmpty())
        fail(QStringLiteral("没有指定要打包的分区"));

    struct Source {
        QString name;
        QString path;
        quint64 size = 0;
        QByteArray hash;
        quint64 dataOffset = 0;
        quint64 dataLength = 0;
    };

    QVector<Source> sources;
    int index = 0;
    for (const auto& name : partitionNames) {
        checkCancel(cancel);
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
        source.hash = sha256File(path, progress, int(100.0 * index / partitionNames.size() / 2));
        sources.append(source);
        ++index;
    }

    Manifest manifest;
    manifest.blockSize = blockSize;
    manifest.minorVersion = 0;
    manifest.partialUpdate = false;
    manifest.signaturesOffset = 0;
    manifest.signaturesSize = 0;
    for (const auto& source : sources) {
        PartitionUpdate update;
        update.name = source.name;
        update.newInfo.size = source.size;
        update.newInfo.hash = source.hash;
        Operation op;
        op.type = OperationType::Replace;
        op.dataLength = source.size;
        op.dstLength = source.size;
        op.dataSha256 = source.hash;
        Extent extent;
        extent.startBlock = 0;
        extent.numBlocks = (source.size + blockSize - 1) / blockSize;
        op.dstExtents.append(extent);
        update.operations.append(op);
        manifest.partitions.append(update);
    }

    // 迭代求 dataOffset：manifest 大小取决于其中的 varint 长度。
    quint64 base = 24;
    for (int iteration = 0; iteration < 8; ++iteration) {
        quint64 cursor = base;
        for (auto& source : sources) {
            source.dataOffset = cursor;
            cursor += source.size;
        }
        for (int i = 0; i < manifest.partitions.size(); ++i)
            manifest.partitions[i].operations[0].dataOffset = sources.at(i).dataOffset;
        const QByteArray encoded = encodeManifest(manifest);
        const quint64 newBase = 24 + quint64(encoded.size());
        if (newBase == base)
            break;
        base = newBase;
    }

    QByteArray finalManifest;
    {
        quint64 cursor = base;
        for (auto& source : sources)
            source.dataOffset = cursor, cursor += source.size;
        for (int i = 0; i < manifest.partitions.size(); ++i)
            manifest.partitions[i].operations[0].dataOffset = sources.at(i).dataOffset;
        finalManifest = encodeManifest(manifest);
        if (24 + quint64(finalManifest.size()) != base)
            fail(QStringLiteral("payload manifest 大小计算不稳定，打包中止"));
    }

    FileWriter writer(outputPath, true);
    QByteArray header;
    header.append("CrAU", 4);
    wr64be(header, 2);
    wr64be(header, quint64(finalManifest.size()));
    wr32be(header, 0); // 无 metadata 签名
    writer.write(header);
    writer.write(finalManifest);

    for (int i = 0; i < sources.size(); ++i) {
        checkCancel(cancel);
        reportProgress(progress,
                       50 + int(50.0 * (i + 1) / sources.size()),
                       QStringLiteral("写入 %1").arg(sources.at(i).name));
        QFile file(sources.at(i).path);
        if (!file.open(QIODevice::ReadOnly))
            fail(QStringLiteral("无法读取分区镜像：%1").arg(sources.at(i).path));
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(4 * 1024 * 1024);
            if (chunk.isEmpty() && file.error() != QFile::NoError)
                fail(QStringLiteral("读取分区镜像失败：%1").arg(sources.at(i).path));
            writer.write(chunk);
        }
    }
    writer.close();
}

} // namespace gf::img::payload
