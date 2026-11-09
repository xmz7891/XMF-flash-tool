#include "Sparse.h"

#include <cstring>

namespace gf::img::sparse {

namespace {

constexpr quint32 kMagic = 0xED26FF3Au;
constexpr int kFileHeaderSize = 28;
constexpr int kChunkHeaderSize = 12;

quint16 rd16(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 2 > d.size())
        fail(QStringLiteral("sparse 数据被截断"));
    return quint16(quint8(d.at(at)) | (quint8(d.at(at + 1)) << 8));
}

quint32 rd32(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        fail(QStringLiteral("sparse 数据被截断"));
    return quint32(quint8(d.at(at))) | (quint32(quint8(d.at(at + 1))) << 8) |
           (quint32(quint8(d.at(at + 2))) << 16) | (quint32(quint8(d.at(at + 3))) << 24);
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

QByteArray chunkHeader(ChunkType type, quint32 blocks, quint32 totalSize)
{
    QByteArray h;
    h.reserve(kChunkHeaderSize);
    wr16(h, quint16(type));
    wr16(h, 0);
    wr32(h, blocks);
    wr32(h, totalSize);
    return h;
}

void writeZeros(FileWriter& writer, quint64 bytes)
{
    static const QByteArray zeros(1024 * 1024, '\0');
    while (bytes > 0) {
        const qsizetype n = qsizetype(qMin<quint64>(bytes, quint64(zeros.size())));
        writer.write(QByteArray(zeros.constData(), n));
        bytes -= quint64(n);
    }
}

void writeRepeated(FileWriter& writer, const QByteArray& pattern4, quint64 totalBytes)
{
    if (pattern4.size() < 4)
        fail(QStringLiteral("sparse fill chunk 数据不足 4 字节"));
    QByteArray block;
    block.reserve(1024 * 1024);
    while (qsizetype(block.size()) < 1024 * 1024)
        block.append(pattern4.left(4));
    quint64 remaining = totalBytes;
    while (remaining > 0) {
        const qsizetype n = qsizetype(qMin<quint64>(remaining, quint64(block.size())));
        writer.write(QByteArray(block.constData(), n));
        remaining -= quint64(n);
    }
}

bool blockIsZero(const char* data, quint32 blockSize)
{
    for (quint32 i = 0; i < blockSize; ++i) {
        if (data[i] != '\0')
            return false;
    }
    return true;
}

/// 全块由同一个 32 位值重复组成时返回 true 并给出该值。
bool blockFillValue(const char* data, quint32 blockSize, quint32* value)
{
    if (blockSize % 4 != 0)
        return false;
    quint32 first = 0;
    std::memcpy(&first, data, 4);
    for (quint32 i = 4; i < blockSize; i += 4) {
        quint32 cur = 0;
        std::memcpy(&cur, data + i, 4);
        if (cur != first)
            return false;
    }
    if (value)
        *value = first;
    return true;
}

} // namespace

QString chunkTypeText(ChunkType type)
{
    switch (type) {
    case ChunkType::Raw:
        return QStringLiteral("raw");
    case ChunkType::Fill:
        return QStringLiteral("fill");
    case ChunkType::DontCare:
        return QStringLiteral("dontcare");
    case ChunkType::Crc32:
        return QStringLiteral("crc32");
    }
    return QStringLiteral("unknown");
}

QString Chunk::typeText() const
{
    return chunkTypeText(type);
}

bool isSparse(const QByteArray& data)
{
    if (data.size() < kFileHeaderSize)
        return false;
    return quint32(quint8(data.at(0))) == 0x3A && quint8(data.at(1)) == 0xFF &&
           quint8(data.at(2)) == 0x26 && quint8(data.at(3)) == 0xED;
}

Image parse(const QByteArray& data)
{
    if (!isSparse(data))
        fail(QStringLiteral("不是 Android sparse image（magic 不匹配）"));
    Image image;
    image.majorVersion = rd16(data, 4);
    image.minorVersion = rd16(data, 6);
    const quint16 fileHeaderSize = rd16(data, 8);
    const quint16 chunkHeaderSize = rd16(data, 10);
    image.blockSize = rd32(data, 12);
    image.totalBlocks = rd32(data, 16);
    const quint32 totalChunks = rd32(data, 20);
    image.imageChecksum = rd32(data, 24);

    if (fileHeaderSize < kFileHeaderSize)
        fail(QStringLiteral("sparse 文件头过小：%1").arg(fileHeaderSize));
    if (chunkHeaderSize < kChunkHeaderSize)
        fail(QStringLiteral("sparse chunk 头过小：%1").arg(chunkHeaderSize));
    if (image.blockSize == 0)
        fail(QStringLiteral("sparse 块大小非法：0"));
    qsizetype pos = fileHeaderSize;
    quint64 blocksSeen = 0;
    for (quint32 i = 0; i < totalChunks; ++i) {
        if (pos + chunkHeaderSize > data.size())
            fail(QStringLiteral("sparse chunk 头被截断（第 %1 个）").arg(i));
        Chunk chunk;
        chunk.type = ChunkType(rd16(data, pos));
        chunk.blockCount = rd32(data, pos + 4);
        const quint32 totalSize = rd32(data, pos + 8);
        if (totalSize < quint32(chunkHeaderSize))
            fail(QStringLiteral("sparse chunk 大小非法：%1").arg(totalSize));
        const qsizetype dataSize = qsizetype(totalSize) - qsizetype(chunkHeaderSize);
        if (pos + qsizetype(totalSize) > data.size())
            fail(QStringLiteral("sparse chunk 数据被截断（第 %1 个）").arg(i));
        switch (chunk.type) {
        case ChunkType::Raw: {
            const quint64 expected = quint64(chunk.blockCount) * image.blockSize;
            if (quint64(dataSize) != expected)
                fail(QStringLiteral("sparse raw chunk 尺寸不符（第 %1 个：期望 %2，实际 %3）")
                         .arg(i)
                         .arg(expected)
                         .arg(dataSize));
            chunk.data = data.mid(pos + chunkHeaderSize, dataSize);
            break;
        }
        case ChunkType::Fill:
        case ChunkType::Crc32:
            if (dataSize != 4)
                fail(QStringLiteral("sparse %1 chunk 数据应为 4 字节（第 %2 个）")
                         .arg(chunkTypeText(chunk.type))
                         .arg(i));
            chunk.data = data.mid(pos + chunkHeaderSize, 4);
            break;
        case ChunkType::DontCare:
            if (dataSize != 0)
                fail(QStringLiteral("sparse dontcare chunk 不应携带数据（第 %1 个）").arg(i));
            break;
        default:
            fail(QStringLiteral("未知 sparse chunk 类型 0x%1（第 %2 个）")
                     .arg(quint16(chunk.type), 4, 16, QLatin1Char('0'))
                     .arg(i));
        }
        blocksSeen += chunk.blockCount;
        image.chunks.append(chunk);
        pos += qsizetype(totalSize);
    }
    if (blocksSeen != image.totalBlocks)
        fail(QStringLiteral("sparse 块数不一致：header=%1, chunks=%2")
                 .arg(image.totalBlocks)
                 .arg(blocksSeen));
    return image;
}

QByteArray serialize(const Image& source)
{
    Image image = source;
    if (image.blockSize == 0)
        fail(QStringLiteral("sparse 块大小不能为 0"));
    image.totalBlocks = 0;
    for (const auto& c : image.chunks)
        image.totalBlocks += c.blockCount;
    QByteArray out;
    out.reserve(kFileHeaderSize + 64);
    wr32(out, kMagic);
    wr16(out, quint16(image.majorVersion));
    wr16(out, quint16(image.minorVersion));
    wr16(out, kFileHeaderSize);
    wr16(out, kChunkHeaderSize);
    wr32(out, image.blockSize);
    wr32(out, image.totalBlocks);
    wr32(out, quint32(image.chunks.size()));
    wr32(out, image.imageChecksum);
    for (const auto& c : image.chunks) {
        switch (c.type) {
        case ChunkType::Raw: {
            const quint64 expected = quint64(c.blockCount) * image.blockSize;
            if (quint64(c.data.size()) != expected)
                fail(QStringLiteral("sparse raw chunk 数据大小不符：期望 %1，实际 %2")
                         .arg(expected)
                         .arg(c.data.size()));
            out.append(chunkHeader(c.type, c.blockCount, quint32(kChunkHeaderSize + c.data.size())));
            out.append(c.data);
            break;
        }
        case ChunkType::Fill:
        case ChunkType::Crc32:
            if (c.data.size() != 4)
                fail(QStringLiteral("sparse %1 chunk 数据必须为 4 字节").arg(chunkTypeText(c.type)));
            out.append(chunkHeader(c.type, c.blockCount, quint32(kChunkHeaderSize + 4)));
            out.append(c.data);
            break;
        case ChunkType::DontCare:
            out.append(chunkHeader(c.type, c.blockCount, quint32(kChunkHeaderSize)));
            break;
        }
    }
    return out;
}

QByteArray toRaw(const Image& image)
{
    QByteArray out;
    out.reserve(qsizetype(image.rawSize()));
    for (const auto& c : image.chunks) {
        switch (c.type) {
        case ChunkType::Raw:
            out.append(c.data);
            break;
        case ChunkType::Fill: {
            const quint64 total = quint64(c.blockCount) * image.blockSize;
            QByteArray pattern = c.data.left(4);
            QByteArray block;
            block.reserve(1024 * 1024);
            while (block.size() < 1024 * 1024)
                block.append(pattern);
            quint64 remaining = total;
            while (remaining > 0) {
                const qsizetype n = qsizetype(qMin<quint64>(remaining, quint64(block.size())));
                out.append(block.constData(), n);
                remaining -= quint64(n);
            }
            break;
        }
        case ChunkType::DontCare:
            out.append(QByteArray(qsizetype(quint64(c.blockCount) * image.blockSize), '\0'));
            break;
        case ChunkType::Crc32:
            break;
        }
    }
    return out;
}

Image fromRaw(const QByteArray& raw, quint32 blockSize, bool detectFill, bool detectDontCare)
{
    if (blockSize == 0)
        fail(QStringLiteral("sparse 块大小不能为 0"));
    Image image;
    image.blockSize = blockSize;
    const qsizetype totalBlocks = raw.size() / blockSize;
    const qsizetype remainder = raw.size() % blockSize;
    image.totalBlocks = quint32(totalBlocks + (remainder ? 1 : 0));

    auto appendRaw = [&](const char* data, quint32 blocks) {
        if (blocks == 0)
            return;
        if (!image.chunks.isEmpty() && image.chunks.last().type == ChunkType::Raw) {
            image.chunks.last().data.append(data, qsizetype(blocks) * blockSize);
            image.chunks.last().blockCount += blocks;
        } else {
            Chunk c;
            c.type = ChunkType::Raw;
            c.blockCount = blocks;
            c.data = QByteArray(data, qsizetype(blocks) * blockSize);
            image.chunks.append(c);
        }
    };
    auto appendDontCare = [&](quint32 blocks) {
        if (blocks == 0)
            return;
        if (!image.chunks.isEmpty() && image.chunks.last().type == ChunkType::DontCare)
            image.chunks.last().blockCount += blocks;
        else {
            Chunk c;
            c.type = ChunkType::DontCare;
            c.blockCount = blocks;
            image.chunks.append(c);
        }
    };
    auto appendFill = [&](quint32 blocks, quint32 value) {
        if (blocks == 0)
            return;
        QByteArray pattern;
        wr32(pattern, value);
        if (!image.chunks.isEmpty() && image.chunks.last().type == ChunkType::Fill &&
            image.chunks.last().data == pattern)
            image.chunks.last().blockCount += blocks;
        else {
            Chunk c;
            c.type = ChunkType::Fill;
            c.blockCount = blocks;
            c.data = pattern;
            image.chunks.append(c);
        }
    };

    const char* p = raw.constData();
    qsizetype index = 0;
    while (index < totalBlocks) {
        const char* block = p + index * blockSize;
        if (detectDontCare && blockIsZero(block, blockSize)) {
            appendDontCare(1);
        } else if (detectFill) {
            quint32 value = 0;
            if (blockFillValue(block, blockSize, &value))
                appendFill(1, value);
            else
                appendRaw(block, 1);
        } else {
            appendRaw(block, 1);
        }
        ++index;
    }
    if (remainder) {
        QByteArray tail = raw.mid(totalBlocks * blockSize);
        tail.append(QByteArray(qsizetype(blockSize) - remainder, '\0'));
        appendRaw(tail.constData(), 1);
    }
    return image;
}

FileInfo inspectFile(const QString& path)
{
    FileReader reader(path);
    FileInfo info;
    info.fileSize = reader.size();
    const QByteArray head = reader.readAt(0, kFileHeaderSize);
    if (!isSparse(head))
        fail(QStringLiteral("不是 Android sparse image：%1").arg(path));
    info.blockSize = rd32(head, 12);
    info.totalBlocks = rd32(head, 16);
    info.totalChunks = rd32(head, 20);
    info.imageChecksum = rd32(head, 24);
    info.rawSize = quint64(info.totalBlocks) * info.blockSize;
    return info;
}

void unpackFile(const QString& sparsePath, const QString& rawPath, const ProgressFn& progress,
                const CancelFn& cancel)
{
    FileReader reader(sparsePath);
    const QByteArray head = reader.readAt(0, kFileHeaderSize);
    if (!isSparse(head))
        fail(QStringLiteral("不是 Android sparse image：%1").arg(sparsePath));
    const quint32 blockSize = rd32(head, 12);
    const quint32 totalChunks = rd32(head, 20);
    if (blockSize == 0)
        fail(QStringLiteral("sparse 块大小为 0，无法解包"));
    qsizetype pos = rd16(head, 8);
    FileWriter writer(rawPath, true);
    for (quint32 i = 0; i < totalChunks; ++i) {
        checkCancel(cancel);
        const QByteArray chunkHead = reader.readAt(pos, kChunkHeaderSize);
        const quint16 type = rd16(chunkHead, 0);
        const quint32 blocks = rd32(chunkHead, 4);
        const quint32 totalSize = rd32(chunkHead, 8);
        if (totalSize < kChunkHeaderSize)
            fail(QStringLiteral("sparse chunk 大小非法：%1").arg(totalSize));
        switch (ChunkType(type)) {
        case ChunkType::Raw:
            writer.write(reader.readAt(pos + kChunkHeaderSize,
                                       qint64(quint64(blocks) * blockSize)));
            break;
        case ChunkType::Fill: {
            const QByteArray pattern = reader.readAt(pos + kChunkHeaderSize, 4);
            writeRepeated(writer, pattern, quint64(blocks) * blockSize);
            break;
        }
        case ChunkType::DontCare:
            writeZeros(writer, quint64(blocks) * blockSize);
            break;
        case ChunkType::Crc32:
            break;
        default:
            fail(QStringLiteral("未知 sparse chunk 类型 0x%1")
                     .arg(type, 4, 16, QLatin1Char('0')));
        }
        pos += qsizetype(totalSize);
        reportProgress(progress, totalChunks ? int(100.0 * (i + 1) / totalChunks) : 100,
                       QStringLiteral("解包 sparse chunk %1/%2").arg(i + 1).arg(totalChunks));
    }
}

void packFile(const QString& rawPath, const QString& sparsePath, quint32 blockSize,
              bool detectFill, bool detectDontCare, const ProgressFn& progress,
              const CancelFn& cancel)
{
    if (blockSize == 0)
        fail(QStringLiteral("sparse 块大小不能为 0"));
    FileReader reader(rawPath);
    const quint64 rawSize = quint64(reader.size());
    const quint64 totalBlocks = (rawSize + blockSize - 1) / blockSize;
    if (totalBlocks > 0xFFFFFFFEull)
        fail(QStringLiteral("镜像过大，超出 sparse 格式上限"));

    FileWriter writer(sparsePath, true);
    QByteArray head;
    wr32(head, kMagic);
    wr16(head, 1);
    wr16(head, 0);
    wr16(head, kFileHeaderSize);
    wr16(head, kChunkHeaderSize);
    wr32(head, blockSize);
    wr32(head, quint32(totalBlocks));
    wr32(head, 0); // chunk 数量稍后回填
    wr32(head, 0); // image checksum
    writer.write(head);

    constexpr int kBatchBlocks = 1024;
    quint32 chunkCount = 0;

    // 待写出的 raw 数据缓冲（限制内存占用）。
    QByteArray rawBuffer;
    quint32 rawBlocks = 0;
    auto flushRaw = [&]() {
        if (rawBlocks == 0)
            return;
        writer.write(chunkHeader(ChunkType::Raw, rawBlocks, quint32(kChunkHeaderSize + rawBuffer.size())));
        writer.write(rawBuffer);
        rawBuffer.clear();
        rawBlocks = 0;
        ++chunkCount;
    };
    quint32 pendingDontCare = 0;
    auto flushDontCare = [&]() {
        if (pendingDontCare == 0)
            return;
        writer.write(chunkHeader(ChunkType::DontCare, pendingDontCare, kChunkHeaderSize));
        pendingDontCare = 0;
        ++chunkCount;
    };
    quint32 pendingFillBlocks = 0;
    quint32 pendingFillValue = 0;
    auto flushFill = [&]() {
        if (pendingFillBlocks == 0)
            return;
        QByteArray pattern;
        wr32(pattern, pendingFillValue);
        writer.write(chunkHeader(ChunkType::Fill, pendingFillBlocks, kChunkHeaderSize + 4));
        writer.write(pattern);
        pendingFillBlocks = 0;
        ++chunkCount;
    };

    quint64 doneBlocks = 0;
    while (doneBlocks < totalBlocks) {
        checkCancel(cancel);
        const quint32 blocks = quint32(qMin<quint64>(kBatchBlocks, totalBlocks - doneBlocks));
        QByteArray batch = reader.readAt(qint64(doneBlocks * blockSize), qint64(blocks) * blockSize);
        if (batch.size() < qint64(blocks) * blockSize)
            batch.append(QByteArray(int(qint64(blocks) * blockSize - batch.size()), '\0'));
        const char* p = batch.constData();
        for (quint32 i = 0; i < blocks; ++i) {
            const char* block = p + qsizetype(i) * blockSize;
            if (detectDontCare && blockIsZero(block, blockSize)) {
                flushRaw();
                flushFill();
                ++pendingDontCare;
                continue;
            }
            quint32 value = 0;
            if (detectFill && blockFillValue(block, blockSize, &value)) {
                flushRaw();
                flushDontCare();
                if (pendingFillBlocks > 0 && pendingFillValue != value)
                    flushFill();
                pendingFillValue = value;
                ++pendingFillBlocks;
                continue;
            }
            flushDontCare();
            flushFill();
            rawBuffer.append(block, blockSize);
            ++rawBlocks;
            if (rawBuffer.size() >= 16 * 1024 * 1024)
                flushRaw();
        }
        doneBlocks += blocks;
        reportProgress(progress,
                       totalBlocks ? int(100.0 * double(doneBlocks) / double(totalBlocks)) : 100,
                       QStringLiteral("打包 sparse：%1 / %2 块")
                           .arg(doneBlocks)
                           .arg(totalBlocks));
    }
    flushRaw();
    flushDontCare();
    flushFill();

    // 回填 chunk 数量。
    QByteArray count;
    wr32(count, chunkCount);
    writer.writeAt(20, count);
    writer.close();
}

} // namespace gf::img::sparse
