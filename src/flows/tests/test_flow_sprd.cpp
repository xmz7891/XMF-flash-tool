#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "TestRegistry.h"
#include "flows/SprdFlow.h"
#include "flows/tests/FlowTestData.h"
#include "sprd/U2s.h"
#include "usb/MockTransport.h"

namespace {

QByteArray u2sResponse(quint8 type, const QByteArray& payload = {})
{
    return gf::sprd::u2s::encodeFrame(type, payload);
}

QByteArray statusPayload(quint8 status = 0)
{
    return QByteArray(1, char(status));
}

} // namespace

class TestFlowSprd : public QObject {
    Q_OBJECT

private slots:
    void loadPacAndBuildTable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray pac = flow_test::makePac({
            {QStringLiteral("FDL1"), 1, QByteArray(64, '\x11')},
            {QStringLiteral("boot"), 0, QByteArray(128, '\x22')},
        });
        const QString path = dir.filePath(QStringLiteral("firmware.pac"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pac);
        file.close();

        const auto loaded = gf::flows::loadPacFile(path);
        QVERIFY2(loaded.valid, qPrintable(loaded.error));
        QCOMPARE(loaded.table.partitions.size(), 2);
        QCOMPARE(loaded.table.partitions[0].name, QStringLiteral("FDL1"));
        QVERIFY(!loaded.table.partitions[0].hasAddress);
    }

    void addressMapFillsOffsets()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray pac = flow_test::makePac({
            {QStringLiteral("boot"), 0, QByteArray(16, '\x22')},
            {QStringLiteral("system"), 0, QByteArray(16, '\x33')},
        });
        const QString path = dir.filePath(QStringLiteral("firmware.pac"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pac);
        file.close();

        auto loaded = gf::flows::loadPacFile(path);
        QVERIFY(loaded.valid);
        gf::flows::applyAddressMap(loaded.table,
                                   QStringLiteral("boot,0x100000,0x1000\n"
                                                  "system,0x200000,0x2000\n"));
        QVERIFY(loaded.table.partitions[0].hasAddress);
        QCOMPARE(loaded.table.partitions[0].offset, quint64(0x100000));
        QCOMPARE(loaded.table.partitions[1].offset, quint64(0x200000));
    }

    void writeRawSendsFdlWriteFrame()
    {
        gf::usb::MockTransport usb;
        const auto table = gf::flows::PartitionTable::fromPartitionMapText(
            QStringLiteral("boot,0x100000,0x1000\n"), QStringLiteral("sprd"));
        gf::flows::SprdFdlStorageIo io(usb, table);
        usb.queueRead(u2sResponse(0x0E, statusPayload()));

        io.writeRaw(0x100000, QByteArrayLiteral("DATA"), gf::proto::Reporter::silent());
        const auto frame = gf::sprd::u2s::decodeFrame(usb.writes().first());
        QVERIFY2(frame.valid, qPrintable(frame.error));
        QCOMPARE(frame.type, quint8(0x0E));
        QCOMPARE(gf::proto::readU32be(frame.payload, 0), quint32(0x100000));
        QCOMPARE(gf::proto::readU32be(frame.payload, 4), quint32(4));
        QCOMPARE(frame.payload.mid(8), QByteArrayLiteral("DATA"));
    }

    void readRawReturnsFramedData()
    {
        gf::usb::MockTransport usb;
        const auto table = gf::flows::PartitionTable::fromPartitionMapText(
            QStringLiteral("boot,0x100000,0x40\n"), QStringLiteral("sprd"));
        gf::flows::SprdFdlStorageIo io(usb, table);
        usb.queueRead(u2sResponse(0x0D, QByteArray(64, '\x5A')));

        const QByteArray data = io.readRaw(0x100000, 64, gf::proto::Reporter::silent());
        QCOMPARE(data, QByteArray(64, '\x5A'));
        const auto frame = gf::sprd::u2s::decodeFrame(usb.writes().first());
        QCOMPARE(frame.type, quint8(0x0D));
    }

    void erasePartitionSendsEraseFrame()
    {
        gf::usb::MockTransport usb;
        const auto table = gf::flows::PartitionTable::fromPartitionMapText(
            QStringLiteral("boot,0x100000,0x1000\n"), QStringLiteral("sprd"));
        gf::flows::SprdFdlStorageIo io(usb, table);
        usb.queueRead(u2sResponse(0x0A, statusPayload()));
        io.erasePartition(QStringLiteral("boot"), gf::proto::Reporter::silent());
        const auto frame = gf::sprd::u2s::decodeFrame(usb.writes().first());
        QCOMPARE(frame.type, quint8(0x0A));
        QCOMPARE(gf::proto::readU32be(frame.payload, 0), quint32(0x100000));
        QCOMPARE(gf::proto::readU32be(frame.payload, 4), quint32(0x1000));
    }

    void flashPacWritesImages()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray pacData = flow_test::makePac({
            {QStringLiteral("boot"), 0, QByteArray(32, '\x11')},
            {QStringLiteral("system"), 0, QByteArray(32, '\x22')},
        });
        const QString path = dir.filePath(QStringLiteral("firmware.pac"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pacData);
        file.close();

        auto loaded = gf::flows::loadPacFile(path);
        QVERIFY(loaded.valid);
        gf::flows::applyAddressMap(loaded.table,
                                   QStringLiteral("boot,0x100000,0x1000\n"
                                                  "system,0x200000,0x1000\n"));

        gf::usb::MockTransport usb;
        gf::flows::SprdFdlStorageIo io(usb, loaded.table);
        usb.queueRead(u2sResponse(0x0E, statusPayload()));
        usb.queueRead(u2sResponse(0x0E, statusPayload()));
        const int written = gf::flows::flashPac(io, loaded, {}, gf::proto::Reporter::silent());
        QCOMPARE(written, 2);
    }

    void backupPartitionViaFdl()
    {
        gf::usb::MockTransport usb;
        const auto table = gf::flows::PartitionTable::fromPartitionMapText(
            QStringLiteral("boot,0x100000,0x40\n"), QStringLiteral("sprd"));
        gf::flows::SprdFdlStorageIo io(usb, table);
        const QByteArray data(0x40, '\x77');
        usb.queueRead(u2sResponse(0x0D, data));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto files = gf::flows::backupPartitions(io, dir.path(),
                                                       {QStringLiteral("boot")},
                                                       gf::proto::Reporter::silent());
        QCOMPARE(files.size(), 1);
        QVERIFY(QFile::exists(files.first().path));
        QVERIFY(QFileInfo(files.first().path).fileName().startsWith(QStringLiteral("sprd_boot_")));
    }

    void flashPacWithoutAddressesFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray pacData =
            flow_test::makePac({{QStringLiteral("boot"), 0, QByteArray(16, '\x11')}});
        const QString path = dir.filePath(QStringLiteral("firmware.pac"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pacData);
        file.close();

        const auto loaded = gf::flows::loadPacFile(path);
        QVERIFY(loaded.valid);
        gf::usb::MockTransport usb;
        gf::flows::SprdFdlStorageIo io(usb, loaded.table);
        bool threw = false;
        try {
            gf::flows::flashPac(io, loaded, {}, gf::proto::Reporter::silent());
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("地址")));
        }
        QVERIFY(threw);
    }
};

#include "test_flow_sprd.moc"
GF_TEST_CLASS(TestFlowSprd)
