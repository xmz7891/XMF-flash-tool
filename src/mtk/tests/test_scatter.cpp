#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "TestRegistry.h"
#include "mtk/Scatter.h"

namespace {

const char* kScatterText = R"(############################################################################
#
#  General Setting
#
############################################################################
- general: MTK_PLATFORM_CFG
  info:
    - config_version: V1.1.2
      platform: MT6768
      project: k62v1_64_bsp
      storage: EMMC
      boot_channel: MSDC_0
      block_size: 0x20000

- partition_index: SYS0
  partition_name: preloader
  file_name: preloader_k62v1_64_bsp.bin
  is_download: true
  type: SV5_BL_BIN
  linear_start_addr: 0x0
  physical_start_addr: 0x0
  partition_size: 0x40000
  region: EMMC_BOOT_1
  storage: HW_STORAGE_EMMC
  boundary_check: true
  is_reserved: false
  operation_type: BOOTLOADERS
  reserve: 0x00

- partition_index: SYS1
  partition_name: "boot"
  file_name: boot.img
  is_download: true
  type: NORMAL_ROM
  linear_start_addr: 0x40000
  physical_start_addr: 0x40000
  partition_size: 0x2000000
  region: EMMC_USER
  storage: HW_STORAGE_EMMC
  boundary_check: true
  is_reserved: false
  operation_type: UPDATE

- partition_index: SYS2
  partition_name: userdata
  file_name: NONE
  is_download: false
  type: EXT4_IMG
  linear_start_addr: 0x2240000
  physical_start_addr: 0x2240000
  partition_size: 0x10000000
  region: EMMC_USER
  storage: HW_STORAGE_EMMC
  boundary_check: true
  is_reserved: false
  operation_type: FORMAT
)";

} // namespace

class TestMtkScatter : public QObject {
    Q_OBJECT

private slots:
    void parseTypicalScatter()
    {
        const gf::mtk::ScatterInfo info =
            gf::mtk::ScatterInfo::parse(QString::fromUtf8(kScatterText));
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.platform, QStringLiteral("MT6768"));
        QCOMPARE(info.project, QStringLiteral("k62v1_64_bsp"));
        QCOMPARE(info.storage, QStringLiteral("EMMC"));
        QCOMPARE(info.bootChannel, QStringLiteral("MSDC_0"));
        QCOMPARE(info.configVersion, QStringLiteral("V1.1.2"));
        QCOMPARE(info.blockSize, quint64(0x20000));
        QCOMPARE(info.partitions.size(), 3);

        const auto& preloader = info.partitions[0];
        QCOMPARE(preloader.index, QStringLiteral("SYS0"));
        QCOMPARE(preloader.name, QStringLiteral("preloader"));
        QCOMPARE(preloader.fileName, QStringLiteral("preloader_k62v1_64_bsp.bin"));
        QCOMPARE(preloader.type, QStringLiteral("SV5_BL_BIN"));
        QVERIFY(preloader.isDownload);
        QVERIFY(preloader.boundaryCheck);
        QVERIFY(!preloader.isReserved);
        QCOMPARE(preloader.linearStart, quint64(0));
        QCOMPARE(preloader.physicalStart, quint64(0));
        QCOMPARE(preloader.size, quint64(0x40000));
        QCOMPARE(preloader.region, QStringLiteral("EMMC_BOOT_1"));
        QCOMPARE(preloader.operationType, QStringLiteral("BOOTLOADERS"));

        const auto& boot = info.partitions[1];
        QCOMPARE(boot.name, QStringLiteral("boot")); // quoted value is unquoted
        QCOMPARE(boot.size, quint64(0x2000000));

        QVERIFY(info.hasPartition(QStringLiteral("PRELOADER")));
        QVERIFY(!info.hasPartition(QStringLiteral("system")));
        QVERIFY(gf::mtk::describeScatter(info).contains(QStringLiteral("MT6768")));
    }

    void rejectEmpty()
    {
        const gf::mtk::ScatterInfo info = gf::mtk::ScatterInfo::parse(QString());
        QVERIFY(!info.valid);
        QVERIFY(!info.error.isEmpty());
    }

    void rejectBinaryScatter()
    {
        QString text = QStringLiteral("platform: MT6768");
        text.append(QChar(u'\0'));
        const gf::mtk::ScatterInfo info = gf::mtk::ScatterInfo::parse(text);
        QVERIFY(!info.valid);
        QVERIFY(info.error.contains(QStringLiteral("加密")));
    }

    void rejectWithoutPartitions()
    {
        const gf::mtk::ScatterInfo info = gf::mtk::ScatterInfo::parse(
            QStringLiteral("- general: MTK_PLATFORM_CFG\n  info:\n    - platform: MT6768\n"));
        QVERIFY(!info.valid);
        QVERIFY(info.error.contains(QStringLiteral("分区")));
    }

    void parseFileRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("scatter.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(kScatterText));
        file.close();

        QString error;
        const gf::mtk::ScatterInfo info = gf::mtk::ScatterInfo::parseFile(path, &error);
        QVERIFY2(info.valid, qPrintable(error));
        QCOMPARE(info.partitions.size(), 3);
    }

    void parseFileMissingReportsError()
    {
        QString error;
        const gf::mtk::ScatterInfo info =
            gf::mtk::ScatterInfo::parseFile(QStringLiteral("Z:/definitely/not/here.txt"), &error);
        QVERIFY(!info.valid);
        QVERIFY(!error.isEmpty());
    }
};

#include "test_scatter.moc"
GF_TEST_CLASS(TestMtkScatter)
