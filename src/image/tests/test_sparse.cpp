#include <QtTest>

#include "TestRegistry.h"

#include "Sparse.h"

#include <QTemporaryDir>

namespace {

QByteArray makeRoundRaw()
{
    constexpr int block = 4096;
    QByteArray raw;
    raw.append(QByteArray(block * 2, '\0'));          // 两个全零块
    raw.append(QByteArray(block, char(0xAB)));         // 一个 fill 块
    QByteArray pattern(block, '\0');                   // 一个普通数据块
    for (int i = 0; i < block; ++i)
        pattern[i] = char((i * 37 + 11) & 0xFF);
    raw.append(pattern);
    raw.append(QByteArray(block, '\0'));               // 再一个全零块
    return raw;
}

} // namespace

class TestSparse : public QObject
{
    Q_OBJECT

private slots:
    void chunkTypesDetected()
    {
        const QByteArray raw = makeRoundRaw();
        const gf::img::sparse::Image image = gf::img::sparse::fromRaw(raw, 4096, true, true);
        QCOMPARE(image.blockSize, 4096u);
        QCOMPARE(image.totalBlocks, quint32(raw.size() / 4096));
        QVERIFY(!image.chunks.isEmpty());

        int dontCare = 0;
        int fill = 0;
        int rawChunks = 0;
        for (const auto& chunk : image.chunks) {
            switch (chunk.type) {
            case gf::img::sparse::ChunkType::DontCare:
                dontCare += 1;
                break;
            case gf::img::sparse::ChunkType::Fill:
                ++fill;
                QCOMPARE(chunk.data, QByteArray::fromHex("ABABABAB"));
                break;
            case gf::img::sparse::ChunkType::Raw:
                ++rawChunks;
                break;
            default:
                break;
            }
        }
        QCOMPARE(dontCare, 2); // 头部连续两个零块合并为一个，尾部一个零块
        QCOMPARE(fill, 1);
        QCOMPARE(rawChunks, 1);
        QCOMPARE(gf::img::sparse::toRaw(image), raw);
    }

    void serializeParseRoundTrip()
    {
        const QByteArray raw = makeRoundRaw();
        const gf::img::sparse::Image image = gf::img::sparse::fromRaw(raw, 4096, true, true);
        const QByteArray encoded = gf::img::sparse::serialize(image);
        QVERIFY(gf::img::sparse::isSparse(encoded));

        const gf::img::sparse::Image parsed = gf::img::sparse::parse(encoded);
        QCOMPARE(parsed.blockSize, image.blockSize);
        QCOMPARE(parsed.totalBlocks, image.totalBlocks);
        QCOMPARE(parsed.chunks.size(), image.chunks.size());
        for (int i = 0; i < image.chunks.size(); ++i) {
            QCOMPARE(int(parsed.chunks.at(i).type), int(image.chunks.at(i).type));
            QCOMPARE(parsed.chunks.at(i).blockCount, image.chunks.at(i).blockCount);
            QCOMPARE(parsed.chunks.at(i).data, image.chunks.at(i).data);
        }
        // 重新序列化必须字节一致。
        QCOMPARE(gf::img::sparse::serialize(parsed), encoded);
        QCOMPARE(gf::img::sparse::toRaw(parsed), raw);
    }

    void crc32ChunkSupport()
    {
        gf::img::sparse::Image image;
        image.blockSize = 512;
        gf::img::sparse::Chunk rawChunk;
        rawChunk.type = gf::img::sparse::ChunkType::Raw;
        rawChunk.blockCount = 1;
        rawChunk.data = QByteArray(512, 'Z');
        image.chunks.append(rawChunk);
        gf::img::sparse::Chunk crc;
        crc.type = gf::img::sparse::ChunkType::Crc32;
        crc.blockCount = 0; // CRC32 chunk 不映射到输出块
        crc.data = QByteArray::fromHex("12345678");
        image.chunks.append(crc);
        gf::img::sparse::Chunk fill;
        fill.type = gf::img::sparse::ChunkType::Fill;
        fill.blockCount = 2;
        fill.data = QByteArray::fromHex("00000000");
        image.chunks.append(fill);

        const QByteArray encoded = gf::img::sparse::serialize(image);
        const gf::img::sparse::Image parsed = gf::img::sparse::parse(encoded);
        QCOMPARE(parsed.chunks.size(), 3);
        QCOMPARE(int(parsed.chunks.at(1).type), int(gf::img::sparse::ChunkType::Crc32));
        const QByteArray raw = gf::img::sparse::toRaw(parsed);
        QCOMPARE(raw.size(), 512 * 3);
        QCOMPARE(raw.left(512), QByteArray(512, 'Z'));
        QVERIFY(gf::img::isZero(raw.mid(512, 512)));
        QVERIFY(gf::img::isZero(raw.mid(1024, 512)));
    }

    void filePackUnpack()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // 用 512 字节块、超过一批（1024 块）的镜像验证流式路径。
        QByteArray raw;
        raw.append(QByteArray(512 * 1100, '\0'));
        QByteArray body(512 * 300, '\0');
        for (int i = 0; i < body.size(); ++i)
            body[i] = char((i * 13 + 7) & 0xFF);
        raw.append(body);
        raw.append(QByteArray(512 * 1200, char(0x5A)));

        const QString rawPath = gf::img::joinPath(dir.path(), QStringLiteral("raw.img"));
        const QString sparsePath = gf::img::joinPath(dir.path(), QStringLiteral("sparse.img"));
        const QString backPath = gf::img::joinPath(dir.path(), QStringLiteral("back.img"));
        gf::img::writeFileOrThrow(rawPath, raw);

        gf::img::sparse::packFile(rawPath, sparsePath, 512, true, true);
        const gf::img::sparse::FileInfo info = gf::img::sparse::inspectFile(sparsePath);
        QCOMPARE(info.blockSize, 512u);
        QCOMPARE(info.rawSize, quint64(raw.size()));
        QCOMPARE(info.totalBlocks, quint32(raw.size() / 512));
        QVERIFY(info.fileSize < raw.size());

        gf::img::sparse::unpackFile(sparsePath, backPath);
        QCOMPARE(gf::img::readFileOrThrow(backPath), raw);
    }

    void rejectsInvalidInput()
    {
        bool threw = false;
        try {
            gf::img::sparse::parse(QByteArray("definitely not sparse"));
        } catch (const std::exception& e) {
            threw = QString::fromUtf8(e.what()).contains(QStringLiteral("sparse"));
        }
        QVERIFY(threw);
        QVERIFY(!gf::img::sparse::isSparse(QByteArray("1234567890123456789012345678")));
    }
};

#include "test_sparse.moc"
GF_TEST_CLASS(TestSparse)
