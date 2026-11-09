#include "Compression.h"

#include <QVector>

#include <cstring>

namespace gf::img::compress {

namespace {

// ======================= DEFLATE (RFC 1951) 解码 =========================

class BitReader {
public:
    explicit BitReader(const QByteArray& data, qsizetype pos = 0)
        : m_data(data)
        , m_pos(pos)
    {
    }

    int bit()
    {
        if (m_bitCount == 0) {
            if (m_pos >= m_data.size())
                fail(QStringLiteral("deflate 数据提前结束"));
            m_bitBuffer = quint8(m_data.at(m_pos++));
            m_bitCount = 8;
        }
        const int value = m_bitBuffer & 1;
        m_bitBuffer >>= 1;
        --m_bitCount;
        return value;
    }

    int bits(int count)
    {
        int value = 0;
        for (int i = 0; i < count; ++i)
            value |= bit() << i;
        return value;
    }

    void alignToByte() { m_bitCount = 0; }
    qsizetype bytePos() const { return m_pos; }
    void skipBytes(qsizetype count) { m_pos += count; }
    qsizetype remaining() const { return m_data.size() - m_pos; }

private:
    const QByteArray& m_data;
    qsizetype m_pos = 0;
    quint8 m_bitBuffer = 0;
    int m_bitCount = 0;
};

struct Huffman {
    int counts[16] = {};
    QVector<int> symbols;

    bool build(const QVector<int>& lengths)
    {
        for (int i = 0; i < 16; ++i)
            counts[i] = 0;
        for (int len : lengths) {
            if (len < 0 || len > 15)
                return false;
            ++counts[len];
        }
        symbols.resize(int(lengths.size()));
        symbols.fill(0);
        if (counts[0] == int(lengths.size()))
            return true; // 全部为空码表
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= counts[len];
            if (left < 0)
                return false;
        }
        int offsets[16] = {};
        for (int len = 1; len < 15; ++len)
            offsets[len + 1] = offsets[len] + counts[len];
        for (int i = 0; i < int(lengths.size()); ++i) {
            if (lengths[i] != 0)
                symbols[offsets[lengths[i]]++] = i;
        }
        return true;
    }

    int decode(BitReader& reader) const
    {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= reader.bit();
            const int count = counts[len];
            if (code - first < count)
                return symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

const int kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
                           33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
                           1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void buildFixedTrees(Huffman& lit, Huffman& dist)
{
    QVector<int> litLengths(288, 0);
    for (int i = 0; i < 144; ++i)
        litLengths[i] = 8;
    for (int i = 144; i < 256; ++i)
        litLengths[i] = 9;
    for (int i = 256; i < 280; ++i)
        litLengths[i] = 7;
    for (int i = 280; i < 288; ++i)
        litLengths[i] = 8;
    if (!lit.build(litLengths))
        fail(QStringLiteral("deflate 固定码表构建失败"));
    QVector<int> distLengths(32, 5);
    if (!dist.build(distLengths))
        fail(QStringLiteral("deflate 固定距离码表构建失败"));
}

void decodeDynamicTrees(BitReader& reader, Huffman& lit, Huffman& dist)
{
    static const int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int hlit = reader.bits(5) + 257;
    const int hdist = reader.bits(5) + 1;
    const int hclen = reader.bits(4) + 4;
    if (hlit > 286 || hdist > 30)
        fail(QStringLiteral("deflate 动态码表数量非法"));

    QVector<int> codeLengths(19, 0);
    for (int i = 0; i < hclen; ++i)
        codeLengths[kOrder[i]] = reader.bits(3);
    Huffman codeTree;
    if (!codeTree.build(codeLengths))
        fail(QStringLiteral("deflate 码长码表非法"));

    QVector<int> lengths(hlit + hdist, 0);
    int index = 0;
    while (index < hlit + hdist) {
        const int symbol = codeTree.decode(reader);
        if (symbol < 0)
            fail(QStringLiteral("deflate 码长解码失败"));
        if (symbol < 16) {
            lengths[index++] = symbol;
        } else if (symbol == 16) {
            if (index == 0)
                fail(QStringLiteral("deflate 重复码缺少前值"));
            const int repeat = reader.bits(2) + 3;
            if (index + repeat > hlit + hdist)
                fail(QStringLiteral("deflate 码长重复越界"));
            const int previous = lengths[index - 1];
            for (int i = 0; i < repeat; ++i)
                lengths[index++] = previous;
        } else if (symbol == 17) {
            const int repeat = reader.bits(3) + 3;
            if (index + repeat > hlit + hdist)
                fail(QStringLiteral("deflate 码长重复越界"));
            index += repeat;
        } else if (symbol == 18) {
            const int repeat = reader.bits(7) + 11;
            if (index + repeat > hlit + hdist)
                fail(QStringLiteral("deflate 码长重复越界"));
            index += repeat;
        } else {
            fail(QStringLiteral("deflate 码长符号非法"));
        }
    }
    if (!lit.build(lengths.mid(0, hlit)))
        fail(QStringLiteral("deflate 字面量码表非法"));
    if (!dist.build(lengths.mid(hlit, hdist)))
        fail(QStringLiteral("deflate 距离码表非法"));
}

QByteArray inflateData(const QByteArray& data)
{
    QByteArray out;
    out.reserve(qMax<qsizetype>(data.size() * 2, 64));
    BitReader reader(data);
    bool last = false;
    while (!last) {
        last = reader.bit() == 1;
        const int type = reader.bits(2);
        if (type == 0) {
            reader.alignToByte();
            const qsizetype pos = reader.bytePos();
            if (pos + 4 > data.size())
                fail(QStringLiteral("deflate 存储块头被截断"));
            const int len = quint8(data.at(pos)) | (quint8(data.at(pos + 1)) << 8);
            const int nlen = quint8(data.at(pos + 2)) | (quint8(data.at(pos + 3)) << 8);
            if (((len ^ 0xFFFF) & 0xFFFF) != nlen)
                fail(QStringLiteral("deflate 存储块长度校验失败"));
            if (pos + 4 + len > data.size())
                fail(QStringLiteral("deflate 存储块数据被截断"));
            out.append(data.mid(pos + 4, len));
            reader.skipBytes(4 + len);
            continue;
        }
        Huffman lit;
        Huffman dist;
        if (type == 1) {
            buildFixedTrees(lit, dist);
        } else if (type == 2) {
            decodeDynamicTrees(reader, lit, dist);
        } else {
            fail(QStringLiteral("deflate 块类型非法：%1").arg(type));
        }
        while (true) {
            const int symbol = lit.decode(reader);
            if (symbol < 0)
                fail(QStringLiteral("deflate 字面量解码失败"));
            if (symbol < 256) {
                out.append(char(symbol));
                continue;
            }
            if (symbol == 256)
                break;
            const int lengthIndex = symbol - 257;
            if (lengthIndex < 0 || lengthIndex >= 29)
                fail(QStringLiteral("deflate 长度符号非法"));
            const int length = kLengthBase[lengthIndex] + reader.bits(kLengthExtra[lengthIndex]);
            const int distSymbol = dist.decode(reader);
            if (distSymbol < 0 || distSymbol >= 30)
                fail(QStringLiteral("deflate 距离符号非法"));
            const qsizetype distance = kDistBase[distSymbol] + reader.bits(kDistExtra[distSymbol]);
            if (distance <= 0 || distance > out.size())
                fail(QStringLiteral("deflate 回溯距离越界：%1").arg(distance));
            for (int i = 0; i < length; ++i)
                out.append(out.at(out.size() - distance));
        }
    }
    return out;
}

quint32 rd32be(const QByteArray& d, qsizetype at)
{
    return (quint32(quint8(d.at(at))) << 24) | (quint32(quint8(d.at(at + 1))) << 16) |
           (quint32(quint8(d.at(at + 2))) << 8) | quint32(quint8(d.at(at + 3)));
}

quint32 rd32le(const QByteArray& d, qsizetype at)
{
    return quint32(quint8(d.at(at))) | (quint32(quint8(d.at(at + 1))) << 8) |
           (quint32(quint8(d.at(at + 2))) << 16) | (quint32(quint8(d.at(at + 3))) << 24);
}

void append32le(QByteArray& out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

void append32be(QByteArray& out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

// ======================= LZO1X 解码 =======================================

constexpr qsizetype kM2MaxOffset = 0x0800;

} // namespace

Format detect(const QByteArray& data)
{
    if (data.size() >= 4 && quint8(data.at(0)) == 0x1F && quint8(data.at(1)) == 0x8B)
        return Format::Gzip;
    if (data.size() >= 9 && quint8(data.at(0)) == 0x89 && data.mid(1, 3) == QByteArray("LZO", 3))
        return Format::Lzop;
    if (data.size() >= 4 && quint8(data.at(0)) == 0x04 && quint8(data.at(1)) == 0x22 &&
        quint8(data.at(2)) == 0x4D && quint8(data.at(3)) == 0x18)
        return Format::Lz4;
    if (data.size() >= 6 && quint8(data.at(0)) == 0xFD && data.mid(1, 5) == QByteArray("7zXZ\0", 5))
        return Format::Lzma;
    if (data.size() >= 3 && data.left(3) == QByteArray("BZh", 3))
        return Format::Bzip2;
    if (data.size() >= 4 && data.left(4) == QByteArray("PK\x03\x04", 4))
        return Format::Zip;
    return Format::Unknown;
}

QString formatName(Format format)
{
    switch (format) {
    case Format::Gzip:
        return QStringLiteral("gzip");
    case Format::Lzop:
        return QStringLiteral("lzop (LZO1X)");
    case Format::Lz4:
        return QStringLiteral("lz4");
    case Format::Lzma:
        return QStringLiteral("xz/lzma");
    case Format::Bzip2:
        return QStringLiteral("bzip2");
    case Format::Zip:
        return QStringLiteral("zip");
    case Format::Unknown:
        break;
    }
    return QStringLiteral("未知");
}

bool isGzip(const QByteArray& data)
{
    return detect(data) == Format::Gzip;
}

bool isLzop(const QByteArray& data)
{
    return detect(data) == Format::Lzop;
}

QByteArray deflateRaw(const QByteArray& data, int level)
{
    // 空数据的合法 deflate 流（fixed Huffman、空块、结束符）。
    if (data.isEmpty())
        return QByteArray::fromHex("0300");
    const QByteArray wrapped = qCompress(data, level);
    // qCompress: [4 字节大端原始长度][2 字节 zlib 头][deflate 流][4 字节 adler32]
    if (wrapped.size() < 10)
        fail(QStringLiteral("deflate 压缩失败（Qt 返回值异常）"));
    return wrapped.mid(6, wrapped.size() - 10);
}

QByteArray inflateRaw(const QByteArray& data)
{
    return inflateData(data);
}

QByteArray gzipCompress(const QByteArray& data, int level)
{
    QByteArray out;
    out.append(char(0x1F));
    out.append(char(0x8B));
    out.append(char(0x08)); // CM = deflate
    out.append(char(0x00)); // FLG
    out.append(4, '\0');    // MTIME
    out.append(char(0x00)); // XFL
    out.append(char(0xFF)); // OS unknown
    out.append(deflateRaw(data, level));
    append32le(out, crc32(data));
    append32le(out, quint32(quint64(data.size()) & 0xFFFFFFFFull));
    return out;
}

QByteArray gzipDecompress(const QByteArray& data)
{
    QByteArray out;
    qsizetype pos = 0;
    if (data.size() < 18)
        fail(QStringLiteral("gzip 数据太短"));
    bool any = false;
    while (pos + 18 <= data.size()) {
        if (quint8(data.at(pos)) != 0x1F || quint8(data.at(pos + 1)) != 0x8B)
            fail(QStringLiteral("不是 gzip 数据（magic 不匹配）"));
        const quint8 method = quint8(data.at(pos + 2));
        if (method != 8)
            fail(QStringLiteral("gzip 压缩方式不受支持：%1").arg(method));
        const quint8 flags = quint8(data.at(pos + 3));
        qsizetype header = pos + 10;
        if (flags & 0x04) { // FEXTRA
            if (header + 2 > data.size())
                fail(QStringLiteral("gzip FEXTRA 被截断"));
            const int xlen = quint8(data.at(header)) | (quint8(data.at(header + 1)) << 8);
            header += 2 + xlen;
        }
        if (flags & 0x08) { // FNAME
            while (header < data.size() && quint8(data.at(header)) != 0)
                ++header;
            ++header;
        }
        if (flags & 0x10) { // FCOMMENT
            while (header < data.size() && quint8(data.at(header)) != 0)
                ++header;
            ++header;
        }
        if (flags & 0x02) // FHCRC
            header += 2;
        if (header + 8 > data.size())
            fail(QStringLiteral("gzip 头部被截断"));

        // 找到成员结尾：用 inflateRaw 会自行忽略尾部多余字节，这里取到文件末尾再校验长度。
        const QByteArray body = data.mid(header, data.size() - header - 8);
        const QByteArray chunk = inflateData(body);
        const quint32 expectCrc = rd32le(data, data.size() - 8);
        const quint32 expectSize = rd32le(data, data.size() - 4);
        if (crc32(chunk) != expectCrc)
            fail(QStringLiteral("gzip CRC32 校验失败（数据可能损坏）"));
        if ((quint64(chunk.size()) & 0xFFFFFFFFull) != expectSize)
            fail(QStringLiteral("gzip 原始长度校验失败"));
        out.append(chunk);
        any = true;
        pos = data.size(); // 不支持多成员拼接（少见），已消费完
    }
    if (!any)
        fail(QStringLiteral("gzip 数据不完整"));
    return out;
}

QByteArray lzo1xDecompress(const QByteArray& data, qsizetype expectedSize)
{
    const quint8* ip = reinterpret_cast<const quint8*>(data.constData());
    const quint8* ipEnd = ip + data.size();
    QByteArray out;
    out.reserve(expectedSize > 0 ? expectedSize : qMax<qsizetype>(data.size() * 2, 1024));

    auto need = [&](qsizetype n) {
        if (ip + n > ipEnd)
            fail(QStringLiteral("LZO 数据被截断（需要 %1 字节）").arg(n));
    };
    auto copyLiterals = [&](qsizetype n) {
        need(n);
        out.append(reinterpret_cast<const char*>(ip), n);
        ip += n;
    };
    auto copyMatch = [&](qsizetype distance, int length) {
        if (distance <= 0 || distance > out.size())
            fail(QStringLiteral("LZO 回溯距离非法：%1（已解出 %2 字节）").arg(distance).arg(out.size()));
        for (int i = 0; i < length; ++i)
            out.append(out.at(out.size() - distance));
    };

    enum class State { Start, LiteralRun, FirstLiteral, Match, MatchDone };
    State state = State::Start;
    int t = 0;
    bool haveToken = false;
    bool finished = false;
    int guard = 0;
    const int maxIterations = int(data.size()) * 8 + 1024;

    while (!finished) {
        if (++guard > maxIterations)
            fail(QStringLiteral("LZO 解码陷入循环（数据损坏）"));
        switch (state) {
        case State::Start: {
            need(1);
            t = *ip++;
            if (t > 17) {
                const int literals = t - 17;
                if (literals < 4) {
                    copyLiterals(literals);
                    need(1);
                    t = *ip++;
                    haveToken = true;
                    state = State::Match;
                } else {
                    copyLiterals(literals);
                    state = State::FirstLiteral;
                }
            } else {
                haveToken = true; // t 已读取，交给 LiteralRun 处理
                state = State::LiteralRun;
            }
            break;
        }
        case State::LiteralRun: {
            if (!haveToken) {
                need(1);
                t = *ip++;
            }
            haveToken = false;
            if (t >= 16) {
                state = State::Match;
                break;
            }
            if (t == 0) {
                int sum = 0;
                while (true) {
                    need(1);
                    if (*ip != 0)
                        break;
                    sum += 255;
                    ++ip;
                }
                need(1);
                t = sum + 15 + *ip++;
            }
            copyLiterals(qsizetype(t) + 3);
            state = State::FirstLiteral;
            break;
        }
        case State::FirstLiteral: {
            need(1);
            t = *ip++;
            if (t >= 16) {
                state = State::Match;
                break;
            }
            qsizetype mpos = out.size() - (1 + kM2MaxOffset);
            mpos -= qsizetype(t) >> 2;
            need(1);
            mpos -= qsizetype(*ip++) << 2;
            copyMatch(out.size() - mpos, 3);
            state = State::MatchDone;
            break;
        }
        case State::Match: {
            haveToken = false;
            if (t >= 64) {
                qsizetype mpos = out.size() - 1;
                mpos -= qsizetype((t >> 2) & 7);
                need(1);
                mpos -= qsizetype(*ip++) << 3;
                copyMatch(out.size() - mpos, ((t >> 5) - 1) + 2);
            } else if (t >= 32) {
                int length = t & 31;
                if (length == 0) {
                    int sum = 0;
                    while (true) {
                        need(1);
                        if (*ip != 0)
                            break;
                        sum += 255;
                        ++ip;
                    }
                    need(1);
                    length = sum + 31 + *ip++;
                }
                need(2);
                qsizetype mpos = out.size() - 1;
                mpos -= qsizetype((ip[0] >> 2) + (ip[1] << 6));
                ip += 2;
                copyMatch(out.size() - mpos, length + 2);
            } else if (t >= 16) {
                const int high = (t & 8) << 11;
                int length = t & 7;
                qsizetype mpos = out.size() - qsizetype(high);
                if (length == 0) {
                    int sum = 0;
                    while (true) {
                        need(1);
                        if (*ip != 0)
                            break;
                        sum += 255;
                        ++ip;
                    }
                    need(1);
                    length = sum + 7 + *ip++;
                }
                need(2);
                mpos -= qsizetype((ip[0] >> 2) + (ip[1] << 6));
                ip += 2;
                if (mpos == qsizetype(out.size())) {
                    finished = true;
                    break;
                }
                mpos -= 0x4000;
                copyMatch(out.size() - mpos, length + 2);
            } else {
                qsizetype mpos = out.size() - 1;
                mpos -= qsizetype(t) >> 2;
                need(1);
                mpos -= qsizetype(*ip++) << 2;
                copyMatch(out.size() - mpos, 2);
            }
            state = State::MatchDone;
            break;
        }
        case State::MatchDone: {
            const int trailer = int(ip[-2] & 3);
            if (trailer == 0) {
                state = State::LiteralRun;
            } else {
                copyLiterals(trailer);
                need(1);
                t = *ip++;
                haveToken = true;
                state = State::Match;
            }
            break;
        }
        }
    }
    if (expectedSize >= 0 && out.size() != expectedSize)
        fail(QStringLiteral("LZO 解压长度不符：期望 %1，实际 %2").arg(expectedSize).arg(out.size()));
    return out;
}

QByteArray lzo1xCompress(const QByteArray& data)
{
    QByteArray out;
    out.reserve(data.size() + 8);
    const qsizetype n = data.size();
    static const char kEof[3] = {0x11, 0x00, 0x00};
    if (n == 0) {
        out.append(kEof, 3);
        return out;
    }
    if (n <= 238) {
        out.append(char(17 + n));
        out.append(data);
        out.append(kEof, 3);
        return out;
    }
    // 长字面量段编码：[0x00][若干 0x00 扩展][末字节] + 字面量
    qsizetype rem = n - 18;
    qsizetype zeros = rem / 255;
    int last = int(rem % 255);
    if (last == 0) {
        --zeros;
        last = 255;
    }
    if (zeros < 0) {
        zeros = 0;
        last = int(n - 18);
    }
    out.append('\0');
    for (qsizetype i = 0; i < zeros; ++i)
        out.append('\0');
    out.append(char(last));
    out.append(data);
    out.append(kEof, 3);
    return out;
}

QByteArray lzopCompress(const QByteArray& data, const QString& fileName)
{
    const QByteArray name = fileName.toUtf8();
    QByteArray out;
    out.append(char(0x89));
    out.append("LZO", 3);
    out.append(char(0x00));
    out.append(char(0x0D));
    out.append(char(0x0A));
    out.append(char(0x1A));
    out.append(char(0x0A));
    const qsizetype headerStart = out.size();
    append32be(out, 0x00001030u); // version
    append32be(out, 0x00002050u); // lib version
    append32be(out, 0x00000940u); // version needed
    out.append(char(1));          // method = M_LZO1X_1
    out.append(char(5));          // level
    const quint32 flags = 0x00001003u; // F_ADLER32_D | F_ADLER32_C | F_H_CRC32
    append32be(out, flags);
    append32be(out, 0x000081A4u); // mode 0644 普通文件
    append32be(out, 0);           // mtime low
    append32be(out, 0);           // mtime high
    out.append(char(name.size()));
    out.append(name);
    append32be(out, crc32(out.mid(headerStart, out.size() - headerStart)));

    constexpr int kChunk = 256 * 1024;
    for (qsizetype offset = 0; offset < data.size(); offset += kChunk) {
        const QByteArray chunk = data.mid(offset, kChunk);
        QByteArray compressed = lzo1xCompress(chunk);
        if (compressed.size() >= chunk.size()) {
            // lzop 约定：compressed_size == uncompressed_size 表示原样存储。
            append32be(out, quint32(chunk.size()));
            append32be(out, quint32(chunk.size()));
            append32be(out, adler32(chunk));
            append32be(out, adler32(chunk));
            out.append(chunk);
        } else {
            append32be(out, quint32(chunk.size()));
            append32be(out, quint32(compressed.size()));
            append32be(out, adler32(chunk));
            append32be(out, adler32(compressed));
            out.append(compressed);
        }
    }
    append32be(out, 0);
    append32be(out, 0);
    return out;
}

QByteArray lzopDecompress(const QByteArray& data)
{
    static const unsigned char kMagic[9] = {0x89, 'L', 'Z', 'O', 0x00, 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() < 9 || std::memcmp(data.constData(), kMagic, 9) != 0)
        fail(QStringLiteral("不是 lzop 文件（magic 不匹配）"));
    qsizetype pos = 9;
    if (pos + 24 > data.size())
        fail(QStringLiteral("lzop 文件头被截断"));
    const quint32 version = rd32be(data, pos);
    pos += 4;
    const quint32 libVersion = rd32be(data, pos);
    pos += 4;
    const quint32 versionNeeded = rd32be(data, pos);
    pos += 4;
    const quint8 method = quint8(data.at(pos++));
    const quint8 level = quint8(data.at(pos++));
    const quint32 flags = rd32be(data, pos);
    pos += 4;
    pos += 4; // mode
    pos += 4; // mtime low
    const bool hasHighMtime = version >= 0x0940u;
    if (hasHighMtime)
        pos += 4; // mtime high
    if (pos >= data.size())
        fail(QStringLiteral("lzop 文件头被截断"));
    const int nameLen = quint8(data.at(pos++));
    if (pos + nameLen + 4 > data.size())
        fail(QStringLiteral("lzop 文件名被截断"));
    pos += nameLen;
    const quint32 headerCrc = rd32be(data, pos);
    pos += 4;
    const QByteArray headerBytes = data.mid(9, pos - 4 - 9);
    const quint32 actualCrc = (flags & 0x00001000u) ? crc32(headerBytes) : adler32(headerBytes);
    if (headerCrc != actualCrc)
        fail(QStringLiteral("lzop 文件头校验失败"));
    if (method == 0 || method > 3)
        fail(QStringLiteral("lzop 压缩方法不受支持：%1").arg(method));
    Q_UNUSED(level);
    Q_UNUSED(libVersion);
    Q_UNUSED(versionNeeded);

    QByteArray out;
    while (true) {
        if (pos + 8 > data.size())
            fail(QStringLiteral("lzop 数据块头被截断"));
        const quint32 uncompressedSize = rd32be(data, pos);
        pos += 4;
        const quint32 compressedSize = rd32be(data, pos);
        pos += 4;
        if (uncompressedSize == 0)
            break;
        if (flags & 0x00000001u) { // F_ADLER32_D
            if (pos + 4 > data.size())
                fail(QStringLiteral("lzop 校验和缺失"));
            pos += 4;
        }
        if (flags & 0x00000002u) { // F_ADLER32_C
            if (pos + 4 > data.size())
                fail(QStringLiteral("lzop 压缩校验和缺失"));
            pos += 4;
        }
        if (pos + compressedSize > data.size())
            fail(QStringLiteral("lzop 数据块被截断"));
        const QByteArray block = data.mid(pos, compressedSize);
        pos += compressedSize;
        if (compressedSize == uncompressedSize) {
            out.append(block);
        } else {
            out.append(lzo1xDecompress(block, qsizetype(uncompressedSize)));
        }
    }
    return out;
}

} // namespace gf::img::compress
