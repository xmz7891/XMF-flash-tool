#include "Lz4Codec.h"

#include <QtGlobal>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace gf::extras::lz4 {

namespace {

constexpr quint32 kModernMagic = 0x184D2204u;
constexpr quint32 kLegacyMagic = 0x184C2102u;

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toStdString());
}

quint32 readLe32(const QByteArray& data, qsizetype offset)
{
    return quint32(quint8(data.at(offset))) | (quint32(quint8(data.at(offset + 1))) << 8)
           | (quint32(quint8(data.at(offset + 2))) << 16)
           | (quint32(quint8(data.at(offset + 3))) << 24);
}

void appendLe32(QByteArray& out, quint32 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}

// --- xxHash32 (only needed for the LZ4 modern frame header checksum) -------
constexpr quint32 kPrime1 = 2654435761u;
constexpr quint32 kPrime2 = 2246822519u;
constexpr quint32 kPrime3 = 3266489917u;
constexpr quint32 kPrime4 = 668265263u;
constexpr quint32 kPrime5 = 374761393u;

quint32 rotl32(quint32 value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

quint32 readLe32Raw(const void* pointer)
{
    quint32 value = 0;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}

quint32 xxh32(const void* input, qsizetype length, quint32 seed = 0)
{
    const auto* p = static_cast<const unsigned char*>(input);
    qsizetype remaining = length;
    quint32 hash = 0;
    if (remaining >= 16) {
        quint32 v1 = seed + kPrime1 + kPrime2;
        quint32 v2 = seed + kPrime2;
        quint32 v3 = seed + 0;
        quint32 v4 = seed - kPrime1;
        const auto round = [](quint32 acc, quint32 value) {
            acc += value * kPrime2;
            acc = rotl32(acc, 13);
            return acc * kPrime1;
        };
        qsizetype index = 0;
        while (index <= remaining - 16) {
            v1 = round(v1, readLe32Raw(p + index));
            v2 = round(v2, readLe32Raw(p + index + 4));
            v3 = round(v3, readLe32Raw(p + index + 8));
            v4 = round(v4, readLe32Raw(p + index + 12));
            index += 16;
        }
        hash = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
        p += index;
        remaining -= index;
    } else {
        hash = seed + kPrime5;
    }
    hash += quint32(length);
    while (remaining >= 4) {
        hash += readLe32Raw(p) * kPrime3;
        hash = rotl32(hash, 17) * kPrime4;
        p += 4;
        remaining -= 4;
    }
    while (remaining > 0) {
        hash += (*p) * kPrime5;
        hash = rotl32(hash, 11) * kPrime1;
        ++p;
        --remaining;
    }
    hash ^= hash >> 15;
    hash *= kPrime2;
    hash ^= hash >> 13;
    hash *= kPrime3;
    hash ^= hash >> 16;
    return hash;
}

// --- block decode ----------------------------------------------------------
QByteArray decodeBlock(const QByteArray& block)
{
    if (block.isEmpty())
        return {};
    QByteArray out;
    out.reserve(qsizetype(block.size()) * 4);
    const auto* data = reinterpret_cast<const unsigned char*>(block.constData());
    const qsizetype size = block.size();
    qsizetype index = 0;
    while (index < size) {
        const quint8 token = data[index++];
        qsizetype literalLength = token >> 4;
        if (literalLength == 15) {
            quint8 extra = 0;
            do {
                if (index >= size)
                    fail(QStringLiteral("LZ4 数据损坏：字面量长度越界"));
                extra = data[index++];
                literalLength += extra;
            } while (extra == 255);
        }
        if (index + literalLength > size)
            fail(QStringLiteral("LZ4 数据损坏：字面量超出数据块"));
        out.append(reinterpret_cast<const char*>(data + index), literalLength);
        index += literalLength;
        if (index == size)
            break; // last sequence has no match part
        if (index + 2 > size)
            fail(QStringLiteral("LZ4 数据损坏：缺少匹配偏移"));
        const quint16 offset = quint16(data[index]) | (quint16(data[index + 1]) << 8);
        index += 2;
        if (offset == 0 || offset > out.size())
            fail(QStringLiteral("LZ4 数据损坏：匹配偏移非法"));
        qsizetype matchLength = token & 0x0F;
        if (matchLength == 15) {
            quint8 extra = 0;
            do {
                if (index >= size)
                    fail(QStringLiteral("LZ4 数据损坏：匹配长度越界"));
                extra = data[index++];
                matchLength += extra;
            } while (extra == 255);
        }
        matchLength += 4;
        const qsizetype from = out.size() - offset;
        for (qsizetype i = 0; i < matchLength; ++i) {
            const char value = out.at(from + i);
            out.append(value);
        }
    }
    return out;
}

// --- block encode ----------------------------------------------------------
QByteArray encodeBlock(const QByteArray& data)
{
    const qsizetype size = data.size();
    if (size == 0)
        return {};
    const auto* src = reinterpret_cast<const unsigned char*>(data.constData());
    QByteArray out;
    out.reserve(size + size / 64 + 16);

    const auto appendLength = [&out](qsizetype length) {
        while (length >= 255) {
            out.append(char(255));
            length -= 255;
        }
        out.append(char(quint8(length)));
    };
    const auto hash4 = [src](qsizetype position) -> quint32 {
        quint32 value = 0;
        std::memcpy(&value, src + position, sizeof(value));
        return (value * kPrime1) >> 16; // 16-bit hash table
    };

    std::vector<qsizetype> table(1u << 16, -1);
    qsizetype anchor = 0;
    qsizetype index = 0;
    while (index + 4 <= size) {
        const quint32 hash = hash4(index);
        const qsizetype reference = table[hash];
        table[hash] = index;
        const bool match = reference >= 0 && index - reference <= 65535
                           && std::memcmp(src + reference, src + index, 4) == 0;
        if (!match) {
            ++index;
            continue;
        }
        qsizetype matchLength = 4;
        while (index + matchLength < size && src[reference + matchLength] == src[index + matchLength])
            ++matchLength;
        const qsizetype literalLength = index - anchor;
        const quint8 token = quint8((literalLength < 15 ? literalLength : 15) << 4)
                             | quint8((matchLength - 4 < 15 ? matchLength - 4 : 15));
        out.append(char(token));
        if (literalLength >= 15)
            appendLength(literalLength - 15);
        if (literalLength > 0)
            out.append(reinterpret_cast<const char*>(src + anchor), literalLength);
        const quint16 offset = quint16(index - reference);
        out.append(char(offset & 0xFF));
        out.append(char((offset >> 8) & 0xFF));
        if (matchLength - 4 >= 15)
            appendLength(matchLength - 4 - 15);
        index += matchLength;
        anchor = index;
    }
    const qsizetype literalLength = size - anchor;
    if (literalLength > 0) {
        const quint8 token = quint8((literalLength < 15 ? literalLength : 15) << 4);
        out.append(char(token));
        if (literalLength >= 15)
            appendLength(literalLength - 15);
        out.append(reinterpret_cast<const char*>(src + anchor), literalLength);
    }
    return out;
}

QByteArray decodeModernFrame(const QByteArray& data)
{
    qsizetype position = 4;
    if (data.size() < 7)
        fail(QStringLiteral("LZ4 帧头不完整"));
    const quint8 flags = quint8(data.at(4));
    position = 6;
    const int version = (flags >> 6) & 0x03;
    if (version != 1)
        fail(QStringLiteral("不支持的 LZ4 帧版本：%1").arg(version));
    const bool blockChecksum = (flags & 0x10) != 0;
    const bool contentSize = (flags & 0x08) != 0;
    const bool contentChecksum = (flags & 0x04) != 0;
    const bool dictionary = (flags & 0x01) != 0;
    if (contentSize)
        position += 8;
    if (dictionary)
        fail(QStringLiteral("不支持带字典的 LZ4 帧，请先用 lz4 命令行工具解压"));
    position += 1; // header checksum
    if (position > data.size())
        fail(QStringLiteral("LZ4 帧头不完整"));

    QByteArray out;
    while (position + 4 <= data.size()) {
        const quint32 rawSize = readLe32(data, position);
        position += 4;
        if (rawSize == 0)
            break; // end mark
        const bool uncompressed = (rawSize & 0x80000000u) != 0;
        const qsizetype blockSize = qsizetype(rawSize & 0x7FFFFFFFu);
        if (blockSize <= 0 || position + blockSize > data.size())
            fail(QStringLiteral("LZ4 数据块越界"));
        const QByteArray block = data.mid(position, blockSize);
        position += blockSize;
        out.append(uncompressed ? block : decodeBlock(block));
        if (blockChecksum)
            position += 4;
    }
    if (contentChecksum)
        position += 4;
    return out;
}

QByteArray decodeLegacyFrame(const QByteArray& data)
{
    qsizetype position = 4;
    QByteArray out;
    while (position + 4 <= data.size()) {
        const quint32 blockSize = readLe32(data, position);
        position += 4;
        if (blockSize == 0)
            break;
        if (position + qsizetype(blockSize) > data.size())
            fail(QStringLiteral("LZ4 legacy 数据块越界"));
        out.append(decodeBlock(data.mid(position, qsizetype(blockSize))));
        position += blockSize;
    }
    return out;
}

QByteArray encodeLegacyFrame(const QByteArray& data)
{
    QByteArray out;
    appendLe32(out, kLegacyMagic);
    constexpr qsizetype kChunk = 64 * 1024;
    for (qsizetype position = 0; position < data.size(); position += kChunk) {
        const QByteArray block = encodeBlock(data.mid(position, kChunk));
        appendLe32(out, quint32(block.size()));
        out.append(block);
    }
    return out;
}

QByteArray encodeModernFrame(const QByteArray& data)
{
    QByteArray out;
    appendLe32(out, kModernMagic);
    // FLG: version=01, block independence=1, no checksums / content size / dict.
    const quint8 flags = 0x60;
    const quint8 blockDescriptor = 0x40; // 64 KiB max block size
    QByteArray headerChecksumInput;
    headerChecksumInput.append(char(flags));
    headerChecksumInput.append(char(blockDescriptor));
    out.append(char(flags));
    out.append(char(blockDescriptor));
    out.append(char(quint8((xxh32(headerChecksumInput.constData(), headerChecksumInput.size()) >> 8) & 0xFF)));

    constexpr qsizetype kChunk = 64 * 1024;
    for (qsizetype position = 0; position < data.size(); position += kChunk) {
        const QByteArray chunk = data.mid(position, kChunk);
        const QByteArray compressed = encodeBlock(chunk);
        if (compressed.size() >= chunk.size()) {
            appendLe32(out, quint32(chunk.size()) | 0x80000000u);
            out.append(chunk);
        } else {
            appendLe32(out, quint32(compressed.size()));
            out.append(compressed);
        }
    }
    appendLe32(out, 0); // end mark
    return out;
}

} // namespace

QString frameName(Frame frame)
{
    switch (frame) {
    case Frame::RawBlock: return QStringLiteral("LZ4 block");
    case Frame::Legacy: return QStringLiteral("LZ4 legacy");
    case Frame::Modern: return QStringLiteral("LZ4 帧");
    case Frame::Unknown: break;
    }
    return QStringLiteral("非 LZ4");
}

Frame detect(const QByteArray& data)
{
    if (data.size() >= 4) {
        const quint32 magic = readLe32(data, 0);
        if (magic == kModernMagic)
            return Frame::Modern;
        if (magic == kLegacyMagic)
            return Frame::Legacy;
    }
    return data.isEmpty() ? Frame::Unknown : Frame::RawBlock;
}

QByteArray decompress(const QByteArray& data)
{
    switch (detect(data)) {
    case Frame::Modern:
        return decodeModernFrame(data);
    case Frame::Legacy:
        return decodeLegacyFrame(data);
    case Frame::RawBlock:
        return decodeBlock(data);
    case Frame::Unknown:
        break;
    }
    fail(QStringLiteral("LZ4 数据为空，无法解压"));
}

QByteArray decompressBlock(const QByteArray& block)
{
    return decodeBlock(block);
}

QByteArray compressBlock(const QByteArray& data)
{
    return encodeBlock(data);
}

QByteArray compress(const QByteArray& data, Frame frame)
{
    switch (frame) {
    case Frame::Legacy:
        return encodeLegacyFrame(data);
    case Frame::Modern:
        return encodeModernFrame(data);
    case Frame::RawBlock:
    case Frame::Unknown:
        return encodeBlock(data);
    }
    return encodeBlock(data);
}

} // namespace gf::extras::lz4
