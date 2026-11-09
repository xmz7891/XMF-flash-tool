#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "TestRegistry.h"
#include "flows/MtkFlow.h"
#include "flows/tests/FlowTestData.h"
#include "mtk/Scatter.h"
#include "usb/MockTransport.h"

namespace {

QByteArray deviceResponse(quint8 status, const QByteArray& data = {})
{
    return QByteArray(1, char(status)) + gf::proto::u32be(quint32(data.size())) + data;
}

gf::flows::PartitionTable scatterTable()
{
    const auto scatter = gf::mtk::ScatterInfo::parse(flow_test::makeScatterText());
    return gf::flows::PartitionTable::fromScatter(scatter);
}

QString writeTempFile(const QTemporaryDir& dir, const QString& name, const QByteArray& data)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(data);
    file.close();
    return path;
}

} // namespace

class TestFlowMtk : public QObject {
    Q_OBJECT

private slots:
    void scatterTableHasOffsets()
    {
        const auto table = scatterTable();
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.partitions.size(), 3);
        QCOMPARE(table.partitions[0].name, QStringLiteral("preloader"));
        QCOMPARE(table.partitions[0].offset, quint64(0x0));
        QCOMPARE(table.partitions[1].offset, quint64(0x40000));
        QCOMPARE(table.partitions[1].size, quint64(0x200000));
    }

    void readRawSendsDaReadFrame()
    {
        gf::usb::MockTransport usb;
        usb.queueRead(deviceResponse(0, QByteArrayLiteral("0123456789abcdef")));
        gf::flows::MtkDaStorageIo io(usb, scatterTable());

        const QByteArray data = io.readRaw(0x40000, 16, gf::proto::Reporter::silent());
        QCOMPARE(data, QByteArrayLiteral("0123456789abcdef"));

        const QByteArray expected = QByteArray(1, char(gf::flows::MtkDaStorageIo::kCmdRead))
                                    + gf::proto::u32be(12)
                                    + gf::proto::u64be(0x40000) + gf::proto::u32be(16);
        QCOMPARE(usb.writes().first(), expected);
    }

    void writeRawSendsPayloadAndData()
    {
        gf::usb::MockTransport usb;
        usb.queueRead(deviceResponse(0));
        gf::flows::MtkDaStorageIo io(usb, scatterTable());

        const QByteArray data(16, '\xA5');
        io.writeRaw(0x40000, data, gf::proto::Reporter::silent());
        QCOMPARE(usb.writeCount(), 2);
        const QByteArray header = usb.writes()[0];
        QCOMPARE(quint8(header.at(0)), gf::flows::MtkDaStorageIo::kCmdWrite);
        QCOMPARE(usb.writes()[1], data);
    }

    void erasePartitionSendsEraseFrame()
    {
        gf::usb::MockTransport usb;
        usb.queueRead(deviceResponse(0));
        gf::flows::MtkDaStorageIo io(usb, scatterTable());
        io.erasePartition(QStringLiteral("userdata"), gf::proto::Reporter::silent());
        const QByteArray frame = usb.writes().first();
        QCOMPARE(quint8(frame.at(0)), gf::flows::MtkDaStorageIo::kCmdErase);
        QCOMPARE(gf::proto::readU32be(frame, 1), quint32(12));
        QCOMPARE(gf::proto::readU64be(frame, 5), quint64(0x240000));
        QCOMPARE(gf::proto::readU32be(frame, 13), quint32(0x100000));
    }

    void errorStatusIsReported()
    {
        gf::usb::MockTransport usb;
        usb.queueRead(deviceResponse(0x05));
        gf::flows::MtkDaStorageIo io(usb, scatterTable());
        bool threw = false;
        try {
            io.erasePartition(QStringLiteral("boot"), gf::proto::Reporter::silent());
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("状态")));
        }
        QVERIFY(threw);
    }

    void backupPreloaderWritesDatedFile()
    {
        gf::usb::MockTransport usb;
        const QByteArray preloader(0x40000, '\x5A');
        usb.queueRead(deviceResponse(0, preloader));
        gf::flows::MtkDaStorageIo io(usb, scatterTable());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto files = gf::flows::backupPartitions(io, dir.path(),
                                                       {QStringLiteral("preloader")},
                                                       gf::proto::Reporter::silent());
        QCOMPARE(files.size(), 1);
        QVERIFY(QFile::exists(files.first().path));
        QVERIFY(QFileInfo(files.first().path).fileName().startsWith(QStringLiteral("mtk_preloader_")));
        QCOMPARE(QFile(files.first().path).size(), qint64(preloader.size()));
    }

    void scatterFlashPlanResolvesFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scatterPath = writeTempFile(dir, QStringLiteral("scatter.txt"),
                                                  flow_test::makeScatterText().toUtf8());
        QVERIFY(!scatterPath.isEmpty());
        QVERIFY(!writeTempFile(dir, QStringLiteral("preloader.bin"),
                               QByteArray(0x40000, '\x11')).isEmpty());
        QVERIFY(!writeTempFile(dir, QStringLiteral("boot.img"),
                               QByteArray(0x200000, '\x22')).isEmpty());

        const auto plan = gf::flows::buildScatterFlashPlan(scatterPath, dir.path());
        QVERIFY2(plan.valid, qPrintable(plan.error));
        QCOMPARE(plan.items.size(), 2);
        QCOMPARE(plan.items[0].partition, QStringLiteral("preloader"));
        QVERIFY(plan.items[0].exists);
        QVERIFY(plan.missingFiles.isEmpty());
    }

    void scatterFlashPlanReportsMissingFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scatterPath = writeTempFile(dir, QStringLiteral("scatter.txt"),
                                                  flow_test::makeScatterText().toUtf8());
        const auto plan = gf::flows::buildScatterFlashPlan(scatterPath, dir.path());
        QVERIFY(plan.valid);
        QCOMPARE(plan.missingFiles.size(), 2);
        bool threw = false;
        try {
            gf::usb::MockTransport usb;
            gf::flows::MtkDaStorageIo io(usb, scatterTable());
            gf::flows::flashScatterPlan(io, plan, gf::proto::Reporter::silent());
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("缺失")));
        }
        QVERIFY(threw);
    }

    void flashScatterPlanWritesAllPartitions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scatterPath = writeTempFile(dir, QStringLiteral("scatter.txt"),
                                                  flow_test::makeScatterText().toUtf8());
        writeTempFile(dir, QStringLiteral("preloader.bin"), QByteArray(0x40000, '\x11'));
        writeTempFile(dir, QStringLiteral("boot.img"), QByteArray(0x200000, '\x22'));

        gf::usb::MockTransport usb;
        usb.queueRead(deviceResponse(0)); // preloader (256 KiB -> one chunk)
        usb.queueRead(deviceResponse(0)); // boot chunk 1
        usb.queueRead(deviceResponse(0)); // boot chunk 2
        gf::flows::MtkDaStorageIo io(usb, scatterTable());

        const auto plan = gf::flows::buildScatterFlashPlan(scatterPath, dir.path());
        QVERIFY(plan.valid);
        const int written = gf::flows::flashScatterPlan(io, plan, gf::proto::Reporter::silent());
        QCOMPARE(written, 2);

        QByteArray allWrites;
        for (const auto& write : usb.writes())
            allWrites += write;
        QVERIFY(allWrites.size() > 0x40000 + 0x200000);
    }
};

#include "test_flow_mtk.moc"
GF_TEST_CLASS(TestFlowMtk)
