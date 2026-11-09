#include <QtTest>

#include "TestRegistry.h"
#include "qcom/Elf.h"
#include "usb/tests/TestData.h"

class TestQcomElf : public QObject {
    Q_OBJECT

private slots:
    void parseRealElf64Programmer()
    {
        const QByteArray data =
            xmf_test::repoBytes(QStringLiteral("_research/extract/package_qualcomm_edl-master/"
                                               "Common/prog_ufs_firehose_sdm845.elf"));
        if (data.isEmpty())
            QSKIP("缺少 devprg ELF 样本，跳过");

        const gf::qcom::ElfImage image = gf::qcom::ElfImage::parse(data);
        QVERIFY2(image.valid, qPrintable(image.error));
        QVERIFY(image.klass == gf::qcom::ElfImage::Class::Elf64);
        QCOMPARE(image.machine, quint16(0xB7)); // AArch64
        QVERIFY(image.entry != 0);
        QVERIFY(!image.segments.isEmpty());
        const auto loadable = image.loadableSegments();
        QVERIFY(!loadable.isEmpty());
        QVERIFY(image.totalLoadSize() > 0);
        QVERIFY(image.totalLoadSize() <= quint64(data.size()));
        for (const auto& segment : loadable) {
            QVERIFY(segment.offset + segment.fileSize <= quint64(data.size()));
            QVERIFY(segment.fileSize <= segment.memorySize);
        }
        QVERIFY(gf::qcom::describeElf(image).contains(QStringLiteral("ELF64")));
    }

    void parseRealElf32Mbn()
    {
        const QByteArray data = xmf_test::repoBytes(
            QStringLiteral("_research/extract/package_qualcomm_edl-master/Common/8909.mbn"));
        if (data.isEmpty())
            QSKIP("缺少 MBN 样本，跳过");

        const gf::qcom::ElfImage image = gf::qcom::ElfImage::parse(data);
        QVERIFY2(image.valid, qPrintable(image.error));
        QVERIFY(image.klass == gf::qcom::ElfImage::Class::Elf32);
        QCOMPARE(image.machine, quint16(0x28)); // ARM
        QVERIFY(!image.loadableSegments().isEmpty());
    }

    void parseRealSaharaMbn()
    {
        const QByteArray data = xmf_test::repoBytes(
            QStringLiteral("_research/extract/package_qualcomm_edl-master/Common/MSM8996.mbn"));
        if (data.isEmpty())
            QSKIP("缺少 MBN 样本，跳过");
        const gf::qcom::ElfImage image = gf::qcom::ElfImage::parse(data);
        QVERIFY2(image.valid, qPrintable(image.error));
        QVERIFY(!image.loadableSegments().isEmpty());
        QVERIFY(image.entry != 0);
    }

    void rejectNonElf()
    {
        const gf::qcom::ElfImage image = gf::qcom::ElfImage::parse(QByteArrayLiteral("not an elf"));
        QVERIFY(!image.valid);
        QVERIFY(!image.error.isEmpty());
        QVERIFY(!gf::qcom::ElfImage::looksLikeElf(QByteArrayLiteral("not an elf")));
    }

    void rejectTruncatedElf()
    {
        QByteArray data = QByteArrayLiteral("\x7F"
                                            "ELF");
        data.append(QByteArray(20, '\0'));
        const gf::qcom::ElfImage image = gf::qcom::ElfImage::parse(data);
        QVERIFY(!image.valid);
    }

    void rejectBigEndian()
    {
        QByteArray data(64, '\0');
        data[0] = char(0x7F);
        data[1] = 'E';
        data[2] = 'L';
        data[3] = 'F';
        data[4] = 1; // ELF32
        data[5] = 2; // big endian
        data[0x10] = 2;
        const gf::qcom::ElfImage image = gf::qcom::ElfImage::parse(data);
        QVERIFY(!image.valid);
        QVERIFY(image.error.contains(QStringLiteral("小端")));
    }
};

#include "test_qcom_elf.moc"
GF_TEST_CLASS(TestQcomElf)
