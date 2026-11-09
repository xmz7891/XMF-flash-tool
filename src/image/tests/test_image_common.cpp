#include <QtTest>

#include "TestRegistry.h"

#include "ImageCommon.h"

#include <QTemporaryDir>

class TestImageCommon : public QObject
{
    Q_OBJECT

private slots:
    void crc32KnownVector()
    {
        QCOMPARE(gf::img::crc32(QByteArray("123456789")), 0xCBF43926u);
        QCOMPARE(gf::img::crc32(QByteArray()), 0u);
        // 增量计算必须与一次性计算一致。
        quint32 state = gf::img::crc32Init();
        state = gf::img::crc32Update(state, "1234", 4);
        state = gf::img::crc32Update(state, "56789", 5);
        QCOMPARE(gf::img::crc32Final(state), 0xCBF43926u);
    }

    void adler32KnownVector()
    {
        QCOMPARE(gf::img::adler32(QByteArray("Wikipedia")), 0x11E60398u);
        QCOMPARE(gf::img::adler32(QByteArray()), 1u);
        quint32 state = gf::img::adler32Update(1, "Wiki", 4);
        state = gf::img::adler32Update(state, "pedia", 5);
        QCOMPARE(state, 0x11E60398u);
    }

    void parseNumber_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<quint64>("value");

        QTest::newRow("hex") << QStringLiteral("0x1000") << true << quint64(0x1000);
        QTest::newRow("hex-upper") << QStringLiteral("0X1F") << true << quint64(31);
        QTest::newRow("decimal") << QStringLiteral("4096") << true << quint64(4096);
        QTest::newRow("kilo") << QStringLiteral("4K") << true << quint64(4096);
        QTest::newRow("mega") << QStringLiteral("8M") << true << quint64(8 * 1024 * 1024);
        QTest::newRow("giga") << QStringLiteral("1G") << true << quint64(1024ull * 1024 * 1024);
        QTest::newRow("mb") << QStringLiteral("64MB") << true << quint64(64 * 1024 * 1024);
        QTest::newRow("empty") << QString() << false << quint64(0);
        QTest::newRow("garbage") << QStringLiteral("abcxyz") << false << quint64(0);
    }

    void parseNumber()
    {
        QFETCH(QString, text);
        QFETCH(bool, ok);
        QFETCH(quint64, value);
        bool parsed = false;
        const quint64 result = gf::img::parseNumber(text, &parsed);
        QCOMPARE(parsed, ok);
        if (ok)
            QCOMPARE(result, value);
    }

    void numberToHex()
    {
        QCOMPARE(gf::img::numberToHex(0x1F, 4, true), QStringLiteral("0x001F"));
        QCOMPARE(gf::img::numberToHex(255, 0, false), QStringLiteral("FF"));
    }

    void formatBytes()
    {
        QVERIFY(gf::img::formatBytes(0).contains(QLatin1Char('0')));
        QVERIFY(gf::img::formatBytes(1024).contains(QStringLiteral("KB")));
        QVERIFY(gf::img::formatBytes(1024 * 1024).contains(QStringLiteral("MB")));
    }

    void padAndZero()
    {
        QByteArray data("abc");
        gf::img::padTo4(data);
        QCOMPARE(data.size(), 4);
        data = "ab";
        gf::img::alignTo(data, 4);
        QCOMPARE(data.size(), 4);
        QVERIFY(gf::img::isZero(QByteArray(16, '\0')));
        QVERIFY(!gf::img::isZero(QByteArray("x")));
        QVERIFY(gf::img::isZeroRange(QByteArray("ab\0\0", 4), 2, 2));
    }

    void guidAndUtf16()
    {
        QByteArray raw(16, '\0');
        raw[0] = char(0x78);
        raw[1] = char(0x56);
        raw[2] = char(0x34);
        raw[3] = char(0x12);
        raw[4] = char(0xBC);
        raw[5] = char(0x9A);
        raw[6] = char(0xF0);
        raw[7] = char(0xDE);
        QCOMPARE(gf::img::guidString(raw), QStringLiteral("12345678-9ABC-DEF0-0000-000000000000"));
        const QString text = QStringLiteral("XMF_test");
        QCOMPARE(gf::img::utf16LeToString(gf::img::stringToUtf16Le(text)), text);
    }

    void hexDumpLooksRight()
    {
        const QByteArray data = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
        const QString dump = gf::img::hexDump(data, 0, 16);
        QVERIFY(dump.contains(QStringLiteral("00000000")));
        QVERIFY(dump.contains(QStringLiteral("00 01 02 03")));
    }

    void fileHelpers()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = gf::img::joinPath(dir.path(), QStringLiteral("sub/dir/file.bin"));
        gf::img::writeFileOrThrow(path, QByteArray("payload-data"));
        QCOMPARE(gf::img::readFileOrThrow(path), QByteArray("payload-data"));
        QVERIFY(gf::img::isZero(QByteArray(32, '\0')));
        // 缺失文件必须抛中文异常
        bool threw = false;
        try {
            gf::img::readFileOrThrow(gf::img::joinPath(dir.path(), QStringLiteral("missing")));
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void readerWriter()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = gf::img::joinPath(dir.path(), QStringLiteral("rw.bin"));
        {
            gf::img::FileWriter writer(path, true);
            writer.resize(64);
            writer.writeAt(8, QByteArray("hello"));
        }
        gf::img::FileReader reader(path);
        QCOMPARE(reader.size(), qint64(64));
        QCOMPARE(reader.readAt(8, 5), QByteArray("hello"));
        QVERIFY(gf::img::isZero(reader.readAt(0, 8)));
        bool threw = false;
        try {
            reader.readAt(60, 100);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }
};

#include "test_image_common.moc"
GF_TEST_CLASS(TestImageCommon)
