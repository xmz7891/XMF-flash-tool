#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "TestRegistry.h"
#include "flows/QcomFlow.h"
#include "flows/tests/FlowTestData.h"
#include "qcom/Sahara.h"
#include "usb/MockTransport.h"

namespace {

QByteArray ack()
{
    return QByteArrayLiteral("<response value=\"ACK\" rawmode=\"false\" />");
}

/// Queues HELLO + one READ_DATA request for the whole programmer + DONE + the
/// Firehose configure ACK consumed by QcomSession::open().
void queueOpen(gf::usb::MockTransport& usb, const QByteArray& elf)
{
    using namespace gf::qcom::sahara;
    usb.queueRead(buildHelloPacket(2, 1, 0x1000, 0));
    usb.queueRead(buildReadDataRequest(quint32(elf.size())));
    usb.queueRead(buildDonePacket(1, 0));
    usb.queueRead(ack()); // configure
}

/// Queues the GPT read issued by QcomSession::partitionTable() (64 KiB @ 4096).
void queueGpt(gf::usb::MockTransport& usb)
{
    usb.queueRead(ack());
    usb.queueRead(flow_test::makeBootTableGpt(4096, 64 * 1024));
}

gf::flows::QcomOptions options()
{
    gf::flows::QcomOptions opts;
    opts.memoryName = QStringLiteral("ufs");
    opts.sectorSize = 4096;
    return opts;
}

} // namespace

class TestFlowQcom : public QObject {
    Q_OBJECT

private slots:
    void openAndReadPartitionTable()
    {
        const QByteArray elf = flow_test::makeMinimalElf64();
        gf::usb::MockTransport usb;
        queueOpen(usb, elf);
        queueGpt(usb);

        gf::flows::QcomSession session(usb);
        session.open(elf, options());
        const gf::flows::PartitionTable& table = session.partitionTable();
        QVERIFY2(table.valid, qPrintable(table.error));
        QCOMPARE(table.platform, QStringLiteral("qcom"));
        QCOMPARE(table.sectorSize, quint32(4096));
        QCOMPARE(table.partitions.size(), 2);
        QCOMPARE(table.partitions[0].name, QStringLiteral("boot"));
        QCOMPARE(table.partitions[0].offset, quint64(8 * 4096));
        QCOMPARE(table.partitions[1].name, QStringLiteral("misc"));
    }

    void readPartitionReturnsGptSizedData()
    {
        const QByteArray elf = flow_test::makeMinimalElf64();
        gf::usb::MockTransport usb;
        queueOpen(usb, elf);
        queueGpt(usb);
        const QByteArray bootData(8 * 4096, '\x42');
        usb.queueRead(ack());
        usb.queueRead(bootData);

        gf::flows::QcomSession session(usb);
        session.open(elf, options());
        session.partitionTable();
        QCOMPARE(session.readPartition(QStringLiteral("boot")), bootData);
    }

    void backupSinglePartitionWritesDatedFile()
    {
        const QByteArray elf = flow_test::makeMinimalElf64();
        gf::usb::MockTransport usb;
        queueOpen(usb, elf);
        queueGpt(usb);
        const QByteArray bootData(8 * 4096, '\x37');
        usb.queueRead(ack());
        usb.queueRead(bootData);

        gf::flows::QcomSession session(usb);
        session.open(elf, options());
        gf::flows::QcomFirehoseIo io(session);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto files = gf::flows::backupPartitions(io, dir.path(),
                                                       {QStringLiteral("boot")},
                                                       gf::proto::Reporter::silent());
        QCOMPARE(files.size(), 1);
        QVERIFY(QFile::exists(files.first().path));
        QVERIFY(QFileInfo(files.first().path).fileName().startsWith(QStringLiteral("qcom_boot_")));
        QCOMPARE(QFile(files.first().path).size(), qint64(bootData.size()));
    }

    void writePartitionProgramsSectors()
    {
        const QByteArray elf = flow_test::makeMinimalElf64();
        gf::usb::MockTransport usb;
        queueOpen(usb, elf);
        queueGpt(usb);
        // RMW read of the target sector, then program command ACK + data ACK.
        usb.queueRead(ack());
        usb.queueRead(QByteArray(4096, '\0'));
        usb.queueRead(ack());
        usb.queueRead(ack());

        gf::flows::QcomSession session(usb);
        session.open(elf, options());
        session.partitionTable();
        const QByteArray image(4096, '\xAB');
        session.writePartition(QStringLiteral("boot"), image);

        const auto writes = usb.writes();
        QVERIFY(writes.size() >= 2);
        bool sawProgram = false;
        for (const auto& write : writes) {
            if (write.contains("<program"))
                sawProgram = true;
        }
        QVERIFY(sawProgram);
        QCOMPARE(writes.last(), image);
    }

    void erasePartitionSendsEraseCommand()
    {
        const QByteArray elf = flow_test::makeMinimalElf64();
        gf::usb::MockTransport usb;
        queueOpen(usb, elf);
        queueGpt(usb);
        usb.queueRead(ack());

        gf::flows::QcomSession session(usb);
        session.open(elf, options());
        session.erasePartition(QStringLiteral("boot"));
        bool sawErase = false;
        for (const auto& write : usb.writes()) {
            if (write.contains("<erase"))
                sawErase = true;
        }
        QVERIFY(sawErase);
    }

    void setActiveSlotPatchesMisc()
    {
        const QByteArray elf = flow_test::makeMinimalElf64();
        gf::usb::MockTransport usb;
        queueOpen(usb, elf);
        queueGpt(usb);
        // read misc (4096 bytes, RMW read for write, program ACK + data ACK)
        usb.queueRead(ack());
        usb.queueRead(QByteArray(4096, '\0'));
        usb.queueRead(ack());
        usb.queueRead(QByteArray(4096, '\0'));
        usb.queueRead(ack());
        usb.queueRead(ack());

        gf::flows::QcomSession session(usb);
        session.open(elf, options());
        session.partitionTable();
        session.setActiveSlot(QStringLiteral("b"));

        const QByteArray programmed = usb.writes().last();
        QCOMPARE(programmed.size(), 4096);
        QCOMPARE(programmed.mid(2048, 2), QByteArrayLiteral("_b"));
    }

    void missingProgrammerIsReadable()
    {
        gf::usb::MockTransport usb;
        gf::flows::QcomSession session(usb);
        bool threw = false;
        try {
            session.open(QByteArray(), options());
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("programmer")));
        }
        QVERIFY(threw);
    }
};

#include "test_flow_qcom.moc"
GF_TEST_CLASS(TestFlowQcom)
