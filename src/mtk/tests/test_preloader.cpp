#include <QtTest>

#include "TestRegistry.h"
#include "mtk/Preloader.h"
#include "usb/ProtocolSupport.h"
#include "usb/tests/TestData.h"

namespace {

void putU32(QByteArray& data, int offset, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        data[offset + i] = char((value >> (8 * i)) & 0xFF);
}

QByteArray makeSection(quint16 type, const QByteArray& payload)
{
    QByteArray section;
    section += gf::proto::u32le(gf::mtk::kPreloaderMagic);
    section += gf::proto::u16le(quint16(8 + payload.size()));
    section += gf::proto::u16le(type);
    section += payload;
    return section;
}

QByteArray makePreloader()
{
    QByteArray body;
    body += makeSection(1, QByteArray(4, '\0'));
    body += makeSection(7, QByteArray(92, '\x11'));
    body += makeSection(2, QByteArray(12, '\x22'));
    body += makeSection(8, QByteArray(40, '\x33'));
    body += QByteArray(64, '\x44'); // boot code without a tag

    QByteArray data(int(gf::mtk::kPreloaderHeaderSize) + body.size(), '\0');
    putU32(data, 0, gf::mtk::kPreloaderMagic);
    putU32(data, 4, gf::mtk::kPreloaderHeaderSize);
    const QByteArray name = QByteArrayLiteral("FILE_INFO");
    for (int i = 0; i < name.size(); ++i)
        data[8 + i] = name[i];
    putU32(data, 0x20, quint32(data.size()));
    putU32(data, 0x24, 0x00040000);
    data.replace(int(gf::mtk::kPreloaderHeaderSize), body.size(), body);
    return data;
}

} // namespace

class TestMtkPreloader : public QObject {
    Q_OBJECT

private slots:
    void parseSyntheticPreloader()
    {
        const QByteArray data = makePreloader();
        const gf::mtk::PreloaderInfo info = gf::mtk::PreloaderInfo::parse(data);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.magic, quint32(0x014D4D4D));
        QCOMPARE(info.headerSize, quint32(0x38));
        QCOMPARE(info.fileInfoName, QStringLiteral("FILE_INFO"));
        QCOMPARE(info.imageSize, quint32(data.size()));
        QCOMPARE(info.sections.size(), 4);
        QCOMPARE(info.sections.first().type, quint16(1));
        QCOMPARE(info.sections.last().type, quint16(8));
        QVERIFY(info.sectionsEnd > info.headerSize);
        QCOMPARE(info.sectionsEnd, quint32(0x38 + 12 + 100 + 20 + 48));
        QVERIFY(!info.hasAppendedSignature);
    }

    void appendedSignatureIsDetected()
    {
        QByteArray data = makePreloader();
        data.append(QByteArray(256, '\xAA'));
        putU32(data, 0x20, quint32(data.size() - 256));
        const gf::mtk::PreloaderInfo info = gf::mtk::PreloaderInfo::parse(data);
        QVERIFY2(info.valid, qPrintable(info.error));
        QVERIFY(info.hasAppendedSignature);
        QVERIFY(!info.warnings.isEmpty());
    }

    void rejectNonPreloader()
    {
        const QByteArray data(256, '\x00');
        QVERIFY(!gf::mtk::PreloaderInfo::looksLikePreloader(data));
        const gf::mtk::PreloaderInfo info = gf::mtk::PreloaderInfo::parse(data);
        QVERIFY(!info.valid);
        QVERIFY(info.error.contains(QStringLiteral("magic")));
    }

    void rejectTruncated()
    {
        const gf::mtk::PreloaderInfo info =
            gf::mtk::PreloaderInfo::parse(QByteArrayLiteral("\x4D\x4D\x4D\x01"));
        QVERIFY(!info.valid);
    }

    void parseRealPreloader_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("Redmi9A")
            << QStringLiteral("_research/extract/package_mtk_edl-master/PreLoader/Xiaomi/"
                              "Redmi9A_preloader_k62v1_64_bsp.bin");
        QTest::newRow("OPPOA92S")
            << QStringLiteral("_research/extract/package_mtk_edl-master/PreLoader/OPLUS/"
                              "OPPOA92S_5G_preloader_k6873v1_64.bin");
        QTest::newRow("OPPOR7")
            << QStringLiteral("_research/extract/package_mtk_edl-master/PreLoader/OPLUS/"
                              "OPPOR7_preloader_oppo6752_15011.bin");
    }

    void parseRealPreloader()
    {
        QFETCH(QString, path);
        const QByteArray data = xmf_test::repoBytes(path);
        if (data.isEmpty())
            QSKIP("缺少 preloader 样本，跳过");

        const gf::mtk::PreloaderInfo info = gf::mtk::PreloaderInfo::parse(data);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.magic, quint32(0x014D4D4D));
        QCOMPARE(info.headerSize, quint32(0x38));
        QCOMPARE(info.fileInfoName, QStringLiteral("FILE_INFO"));
        QCOMPARE(info.imageSize, quint32(data.size()));
        QVERIFY(!info.sections.isEmpty());
        QCOMPARE(info.sections.first().type, quint16(1));
        QVERIFY(info.sectionsEnd > info.headerSize);
        QVERIFY(gf::mtk::describePreloader(info).contains(QStringLiteral("Preloader")));
    }

    void rejectRealEncryptedVariant()
    {
        const QByteArray data = xmf_test::repoBytes(
            QStringLiteral("_research/extract/package_mtk_edl-master/PreLoader/Huawei_Honor/"
                           "HuaweiHiEnjoy70Plus_preloader.bin"));
        if (data.isEmpty())
            QSKIP("缺少样本，跳过");
        const gf::mtk::PreloaderInfo info = gf::mtk::PreloaderInfo::parse(data);
        QVERIFY(!info.valid);
        QVERIFY(!info.error.isEmpty());
    }
};

#include "test_preloader.moc"
GF_TEST_CLASS(TestMtkPreloader)
