#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "TestRegistry.h"
#include "flows/FlowOps.h"
#include "flows/PartitionTable.h"
#include "flows/tests/FlowTestData.h"
#include "mtk/Scatter.h"
#include "sprd/Pac.h"
#include "usb/ProtocolSupport.h"

using namespace gf::flows;
using namespace gf::proto;

class TestFlowPartitions : public QObject {
    Q_OBJECT

private slots:
    void gpt512Parsing()
    {
        const QByteArray prefix = flow_test::makeBootTableGpt(512);
        const PartitionTable table = parseGptPrefix(prefix, QStringLiteral("qcom"),
                                                    QStringLiteral("emmc"));
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.sectorSize, quint32(512));
        QCOMPARE(table.partitions.size(), 2);
        QCOMPARE(table.partitions[0].name, QStringLiteral("boot"));
        QCOMPARE(table.partitions[0].offset, quint64(8 * 512));
        QCOMPARE(table.partitions[0].size, quint64(8 * 512));
        QCOMPARE(table.partitions[1].name, QStringLiteral("misc"));
        QVERIFY(table.partitions[1].offset > table.partitions[0].offset);
    }

    void gpt4096Parsing()
    {
        const QByteArray prefix = flow_test::makeBootTableGpt(4096);
        const PartitionTable table = parseGptPrefix(prefix, QStringLiteral("qcom"),
                                                    QStringLiteral("ufs"));
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.sectorSize, quint32(4096));
        QCOMPARE(table.partitions[0].offset, quint64(8 * 4096));
        QCOMPARE(table.partitions[0].size, quint64(8 * 4096));
    }

    void gptRejectsGarbage()
    {
        const PartitionTable table = parseGptPrefix(QByteArray(4096, '\0'),
                                                    QStringLiteral("qcom"));
        QVERIFY(!table.valid);
        QVERIFY(!table.error.isEmpty());
    }

    void scatterTable()
    {
        const auto scatter = gf::mtk::ScatterInfo::parse(flow_test::makeScatterText());
        QVERIFY(scatter.valid);
        const PartitionTable table = PartitionTable::fromScatter(scatter);
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.source, QStringLiteral("scatter"));
        QCOMPARE(table.partitions.size(), 3);
        QCOMPARE(table.partitions[1].name, QStringLiteral("boot"));
        QCOMPARE(table.partitions[1].offset, quint64(0x40000));
        QCOMPARE(table.partitions[1].size, quint64(0x200000));
        QCOMPARE(table.partitions[1].fileName, QStringLiteral("boot.img"));
        QVERIFY(table.partitions[0].isDownload);
        QVERIFY(!table.partitions[2].isDownload);
        QVERIFY(table.toVariantList().first().toMap().contains(QStringLiteral("sizeHex")));
    }

    void parameterTable()
    {
        const PartitionTable table =
            PartitionTable::fromParameterText(flow_test::makeParameterText());
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.partitions.size(), 4);
        QCOMPARE(table.partitions[0].name, QStringLiteral("uboot"));
        QCOMPARE(table.partitions[0].offset, quint64(0x4000 * 512));
        QCOMPARE(table.partitions[0].size, quint64(0x2000 * 512));
        QCOMPARE(table.partitions[3].name, QStringLiteral("system"));
    }

    void partitionMapTable()
    {
        const PartitionTable table = PartitionTable::fromPartitionMapText(
            flow_test::makePartitionMapText(), QStringLiteral("aw"));
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.partitions.size(), 3);
        QCOMPARE(table.partitions[0].offset, quint64(0x100000));
        QCOMPARE(table.partitions[2].size, quint64(0x8000000));
        QVERIFY(table.partitions[0].hasAddress);
    }

    void pacTable()
    {
        const QByteArray pacData = flow_test::makePac({
            {QStringLiteral("FDL1"), 1, QByteArray(64, '\x11')},
            {QStringLiteral("boot"), 0, QByteArray(128, '\x22')},
        });
        const auto pac = gf::sprd::PacInfo::parse(pacData);
        QVERIFY2(pac.valid, qPrintable(pac.error));
        const PartitionTable table = PartitionTable::fromPac(pac);
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.partitions.size(), 2);
        QCOMPARE(table.partitions[1].name, QStringLiteral("boot"));
        QVERIFY(!table.partitions[0].hasAddress);
        QCOMPARE(table.partitions[0].size, quint64(64));
    }

    void nameLookupHandlesSlots()
    {
        QList<flow_test::GptEntrySpec> entries;
        entries.append({QStringLiteral("boot_a"), 8, 15});
        entries.append({QStringLiteral("boot_b"), 16, 23});
        const QByteArray prefix = flow_test::makeGptPrefix(512, entries);
        const PartitionTable table = parseGptPrefix(prefix, QStringLiteral("qcom"));
        QVERIFY(table.valid);
        QCOMPARE(table.indexOf(QStringLiteral("boot_a")), 0);
        QCOMPARE(table.indexOf(QStringLiteral("boot")), 0); // falls back to first base match
        QCOMPARE(table.indexOf(QStringLiteral("missing")), -1);
        QVERIFY(table.find(QStringLiteral("boot_b")) != nullptr);
        QCOMPARE(table.toVariantList().at(0).toMap().value(QStringLiteral("slot")).toString(),
                 QStringLiteral("a"));
    }

    void backupFileNaming()
    {
        const QDateTime now(QDate(2026, 11, 8), QTime(14, 30, 15));
        QCOMPARE(backupFileName(QStringLiteral("qcom"), QStringLiteral("boot"), now),
                 QStringLiteral("qcom_boot_20261108_143015.img"));
        QCOMPARE(backupFileName(QStringLiteral("rk"), QStringLiteral("system_a"), now),
                 QStringLiteral("rk_system_a_20261108_143015.img"));
        // Path-hostile characters are sanitised.
        QVERIFY(backupFileName(QStringLiteral("mtk"), QStringLiteral("a/b"), now)
                    .contains(QStringLiteral("a_b")));
    }

    void memoryIoBackupRestoreErase()
    {
        QList<flow_test::GptEntrySpec> entries;
        entries.append({QStringLiteral("boot"), 8, 15});
        const PartitionTable table = parseGptPrefix(flow_test::makeGptPrefix(512, entries),
                                                    QStringLiteral("qcom"));
        QVERIFY(table.valid);
        MemoryPartitionIo io(table, 128 * 1024);

        const QByteArray image(4096, '\x5A');
        io.writePartition(QStringLiteral("boot"), image, gf::proto::Reporter::silent());
        QCOMPARE(io.readPartition(QStringLiteral("boot"), gf::proto::Reporter::silent()), image);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto files = backupPartitions(io, dir.path(), {QStringLiteral("boot")},
                                            gf::proto::Reporter::silent());
        QCOMPARE(files.size(), 1);
        QVERIFY(QFile::exists(files.first().path));
        QVERIFY(QFileInfo(files.first().path).fileName().startsWith(QStringLiteral("qcom_boot_")));

        io.erasePartition(QStringLiteral("boot"), gf::proto::Reporter::silent());
        QCOMPARE(io.readPartition(QStringLiteral("boot"), gf::proto::Reporter::silent()),
                 QByteArray(4096, '\0'));

        const auto restored = restorePartitions(io, dir.path(), {QStringLiteral("boot")},
                                                gf::proto::Reporter::silent());
        QCOMPARE(restored.size(), 1);
        QCOMPARE(io.readPartition(QStringLiteral("boot"), gf::proto::Reporter::silent()), image);
    }

    void memoryIoRejectsMissingPartition()
    {
        const PartitionTable table =
            PartitionTable::fromPartitionMapText(flow_test::makePartitionMapText(),
                                                 QStringLiteral("aw"));
        MemoryPartitionIo io(table, 256 * 1024 * 1024);
        bool threw = false;
        try {
            io.readPartition(QStringLiteral("nope"), gf::proto::Reporter::silent());
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("分区不存在")));
        }
        QVERIFY(threw);
    }

    void checksumMatchesCrc32()
    {
        const PartitionTable table = PartitionTable::fromPartitionMapText(
            QStringLiteral("boot,0,9\n"), QStringLiteral("aw"));
        QVERIFY(table.valid);
        MemoryPartitionIo io(table, 64);
        io.writePartition(QStringLiteral("boot"), QByteArrayLiteral("123456789"),
                          gf::proto::Reporter::silent());
        const QString crc = partitionChecksum(io, QStringLiteral("boot"), false,
                                              gf::proto::Reporter::silent());
        // CRC32 of "123456789" is 0xCBF43926.
        QCOMPARE(crc, QStringLiteral("CBF43926"));
    }
};

#include "test_flow_partitions.moc"
GF_TEST_CLASS(TestFlowPartitions)
