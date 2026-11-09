#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "TestRegistry.h"
#include "flows/RkFlow.h"
#include "flows/tests/FlowTestData.h"
#include "rk/RockUsb.h"
#include "usb/MockTransport.h"

namespace {

QByteArray csw(quint32 tag, quint8 status = 0)
{
    QByteArray data;
    data += gf::proto::u32le(gf::rk::kCswSignature);
    data += gf::proto::u32le(tag);
    data += gf::proto::u32le(0);
    data.append(char(status));
    return data;
}

QByteArray deviceInfoPayload()
{
    QByteArray raw(128, '\0');
    const QByteArray version = gf::proto::u32le(1);
    for (int i = 0; i < 4; ++i)
        raw[i] = version[i];
    const QByteArray chip = QByteArrayLiteral("RK3399");
    for (int i = 0; i < chip.size(); ++i)
        raw[4 + i] = chip[i];
    return raw;
}

void queueOpen(gf::usb::MockTransport& usb)
{
    usb.queueRead(deviceInfoPayload());
    usb.queueRead(csw(1));
}

void queueGpt(gf::usb::MockTransport& usb)
{
    usb.queueRead(flow_test::makeBootTableGpt(512, 32 * 1024));
    usb.queueRead(csw(2));
}

gf::flows::RkOptions options()
{
    gf::flows::RkOptions opts;
    opts.sectorSize = 512;
    return opts;
}

} // namespace

class TestFlowRk : public QObject {
    Q_OBJECT

private slots:
    void openAndReadGpt()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        queueGpt(usb);
        gf::flows::RkSession session(usb);
        session.open(options());
        const auto& table = session.partitionTable();
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.partitions.size(), 2);
        QCOMPARE(table.partitions[0].name, QStringLiteral("boot"));
        QCOMPARE(table.partitions[0].offset, quint64(8 * 512));
    }

    void parameterTableIsUsedOffline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("parameter.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(flow_test::makeParameterText().toUtf8());
        file.close();

        gf::usb::MockTransport usb;
        queueOpen(usb);
        gf::flows::RkOptions opts;
        opts.parameterFile = path;
        gf::flows::RkSession session(usb);
        session.open(opts);
        const auto& table = session.partitionTable();
        QVERIFY(table.valid);
        QCOMPARE(table.source, QStringLiteral("parameter"));
        QCOMPARE(table.partitions.size(), 4);
        QCOMPARE(table.partitions[0].name, QStringLiteral("uboot"));
    }

    void readPartitionReadsSectors()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        queueGpt(usb);
        const QByteArray bootData(8 * 512, '\x42');
        usb.queueRead(bootData);
        usb.queueRead(csw(3));

        gf::flows::RkSession session(usb);
        session.open(options());
        session.partitionTable();
        QCOMPARE(session.readPartition(QStringLiteral("boot")), bootData);
    }

    void writePartitionProgramsSectors()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        queueGpt(usb);
        usb.queueRead(QByteArray(4096, '\0')); // RMW read data
        usb.queueRead(csw(3));
        usb.queueRead(csw(4)); // write sectors CSW

        gf::flows::RkSession session(usb);
        session.open(options());
        session.partitionTable();
        const QByteArray image(4096, '\xAB');
        session.writePartition(QStringLiteral("boot"), image);

        // Last two writes: CBW + data.
        const auto writes = usb.writes();
        QVERIFY(writes.size() >= 2);
        QCOMPARE(writes.last(), image);
        const auto cbw = gf::rk::Cbw::decode(writes.at(writes.size() - 2));
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdWriteSector);
        QCOMPARE(cbw.transferLength, quint32(4096));
    }

    void erasePartitionSendsErase()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        queueGpt(usb);
        usb.queueRead(csw(3));
        gf::flows::RkSession session(usb);
        session.open(options());
        session.erasePartition(QStringLiteral("boot"));
        const auto writes = usb.writes();
        const auto cbw = gf::rk::Cbw::decode(writes.at(writes.size() - 1));
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdEraseSector);
    }

    void backupPartitionWritesDatedFile()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        queueGpt(usb);
        const QByteArray data(8 * 512, '\x66');
        usb.queueRead(data);
        usb.queueRead(csw(3));
        gf::flows::RkSession session(usb);
        session.open(options());
        gf::flows::RkStorageIo io(session);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto files = gf::flows::backupPartitions(io, dir.path(),
                                                       {QStringLiteral("boot")},
                                                       gf::proto::Reporter::silent());
        QCOMPARE(files.size(), 1);
        QVERIFY(QFile::exists(files.first().path));
        QVERIFY(QFileInfo(files.first().path).fileName().startsWith(QStringLiteral("rk_boot_")));
    }

    void switchToLoaderSendsResetFlag()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        usb.queueRead(csw(2));
        gf::flows::RkSession session(usb);
        session.open(options());
        session.rebootToLoader();
        const auto writes = usb.writes();
        const auto cbw = gf::rk::Cbw::decode(writes.at(writes.size() - 1));
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdSetResetFlag);
        QCOMPARE(quint8(cbw.cb[4]), quint8(1));
    }
};

#include "test_flow_rk.moc"
GF_TEST_CLASS(TestFlowRk)
