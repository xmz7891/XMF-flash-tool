#include <QtTest>

#include "TestRegistry.h"

#include "Compression.h"

namespace {

QByteArray sampleData(int size, int seed = 0)
{
    QByteArray data(size, '\0');
    quint32 state = quint32(seed * 2654435761u + 1);
    for (int i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        // 一半可压缩、一半伪随机，覆盖不同 Huffman 编码路径。
        data[i] = char((i % 3 == 0) ? (i & 0xFF) : int(state >> 24));
    }
    return data;
}

} // namespace

class TestCompression : public QObject
{
    Q_OBJECT

private slots:
    void detectFormats()
    {
        QCOMPARE(int(gf::img::compress::detect(gf::img::compress::gzipCompress("x"))),
                 int(gf::img::compress::Format::Gzip));
        QCOMPARE(int(gf::img::compress::detect(gf::img::compress::lzopCompress("x"))),
                 int(gf::img::compress::Format::Lzop));
        QCOMPARE(int(gf::img::compress::detect(QByteArray("plain"))),
                 int(gf::img::compress::Format::Unknown));
        QVERIFY(!gf::img::compress::formatName(gf::img::compress::Format::Gzip).isEmpty());
    }

    void gzipRoundTrip_data()
    {
        QTest::addColumn<int>("size");
        QTest::newRow("empty") << 0;
        QTest::newRow("one") << 1;
        QTest::newRow("small") << 100;
        QTest::newRow("block") << 4096;
        QTest::newRow("large") << 200000;
    }

    void gzipRoundTrip()
    {
        QFETCH(int, size);
        const QByteArray data = sampleData(size, size + 1);
        const QByteArray gz = gf::img::compress::gzipCompress(data);
        QVERIFY(gz.size() > 18);
        QCOMPARE(quint8(gz.at(0)), quint8(0x1F));
        QCOMPARE(quint8(gz.at(1)), quint8(0x8B));
        const QByteArray out = gf::img::compress::gzipDecompress(gz);
        QCOMPARE(out, data);
    }

    void gzipCompressesRepetitiveData()
    {
        QByteArray data(200000, 'A');
        for (int i = 0; i < 2000; ++i)
            data[i * 100] = char(i & 0xFF);
        const QByteArray gz = gf::img::compress::gzipCompress(data);
        QVERIFY(gz.size() < data.size() / 10);
        QCOMPARE(gf::img::compress::gzipDecompress(gz), data);
    }

    void deflateStoredAndDynamicBlocks()
    {
        const QByteArray data = sampleData(70000, 7);
        // level 0 强制 deflate 使用存储块；level 6 使用动态 Huffman，两条路径都要能解。
        for (int level : {0, 1, 6, 9}) {
            const QByteArray compressed = gf::img::compress::deflateRaw(data, level);
            QCOMPARE(gf::img::compress::inflateRaw(compressed), data);
        }
    }

    void gzipRejectsCorruption()
    {
        const QByteArray gz = gf::img::compress::gzipCompress(sampleData(5000, 3));
        QByteArray broken = gz;
        broken[broken.size() / 2] = char(quint8(broken.at(broken.size() / 2)) ^ 0xFF);
        bool threw = false;
        try {
            gf::img::compress::gzipDecompress(broken);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);

        threw = false;
        try {
            gf::img::compress::inflateRaw(gz.mid(10, 8));
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void lzo1xRoundTrip_data()
    {
        QTest::addColumn<int>("size");
        for (int size : {0, 1, 2, 3, 4, 17, 18, 100, 238, 239, 273, 274, 1000, 65536, 300000})
            QTest::newRow(qPrintable(QString::number(size))) << size;
    }

    void lzo1xRoundTrip()
    {
        QFETCH(int, size);
        const QByteArray data = sampleData(size, size + 11);
        const QByteArray compressed = gf::img::compress::lzo1xCompress(data);
        QVERIFY(!compressed.isEmpty());
        const QByteArray out = gf::img::compress::lzo1xDecompress(compressed, data.size());
        QCOMPARE(out, data);
    }

    void lzopRoundTrip()
    {
        for (int size : {0, 1, 500, 300000}) {
            const QByteArray data = sampleData(size, size + 5);
            const QByteArray lzo = gf::img::compress::lzopCompress(data, QStringLiteral("boot.img"));
            QVERIFY(gf::img::compress::isLzop(lzo));
            const QByteArray out = gf::img::compress::lzopDecompress(lzo);
            QCOMPARE(out, data);
        }
    }

    void lzopRejectsCorruption()
    {
        const QByteArray lzo = gf::img::compress::lzopCompress(sampleData(1000, 2));
        QByteArray broken = lzo;
        broken[12] = char(quint8(broken.at(12)) ^ 0x01);
        bool threw = false;
        try {
            gf::img::compress::lzopDecompress(broken);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
        threw = false;
        try {
            gf::img::compress::lzopDecompress(QByteArray("not lzop at all"));
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void lzoLengthMismatchDetected()
    {
        const QByteArray data = sampleData(300, 9);
        const QByteArray compressed = gf::img::compress::lzo1xCompress(data);
        bool threw = false;
        try {
            gf::img::compress::lzo1xDecompress(compressed, data.size() + 1);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }
};

#include "test_compression.moc"
GF_TEST_CLASS(TestCompression)
