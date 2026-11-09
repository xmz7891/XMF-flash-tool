#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "TestRegistry.h"
#include "aw/Fel.h"
#include "flows/AwFlow.h"
#include "usb/MockTransport.h"

namespace {

QByteArray okResponse()
{
    return QByteArray(8, '\0');
}

QByteArray versionPayload()
{
    QByteArray payload;
    payload += QByteArrayLiteral("AWUSBFEX");
    payload += gf::proto::u32le(0x00160000);
    payload += gf::proto::u32le(0x1689); // A64
    payload += QByteArray(16, '\0');
    return payload;
}

void queueOpen(gf::usb::MockTransport& usb)
{
    usb.queueRead(versionPayload());
    usb.queueRead(okResponse());
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

class TestFlowAw : public QObject {
    Q_OBJECT

private slots:
    void helloReadsVersion()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        gf::flows::AwSession session(usb);
        const gf::aw::VersionInfo info = session.open(gf::flows::AwOptions{});
        QVERIFY(info.valid);
        QCOMPARE(info.id, quint32(0x1689));
        QCOMPARE(gf::aw::socName(info.id), QStringLiteral("A64"));
    }

    void readRawReturnsMemory()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        const QByteArray memory(64, '\x5A');
        usb.queueRead(memory);
        usb.queueRead(okResponse());

        gf::flows::AwSession session(usb);
        session.open(gf::flows::AwOptions{});
        QCOMPARE(session.readRaw(0x40000000, 64), memory);
        const auto request = gf::aw::Request::decode(usb.writes().at(1));
        QCOMPARE(request.request, gf::aw::kRead);
        QCOMPARE(request.address, quint32(0x40000000));
    }

    void writeRawSendsData()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        usb.queueRead(okResponse());
        gf::flows::AwSession session(usb);
        session.open(gf::flows::AwOptions{});

        const QByteArray data(32, '\x33');
        session.writeRaw(0x40000000, data);
        QCOMPARE(usb.writes().last(), data);
        const auto request = gf::aw::Request::decode(usb.writes().at(1));
        QCOMPARE(request.request, gf::aw::kWrite);
        QCOMPARE(request.address, quint32(0x40000000));
    }

    void execSendsFExec()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        usb.queueRead(okResponse());
        gf::flows::AwSession session(usb);
        session.open(gf::flows::AwOptions{});
        session.execRaw(0x40000000);
        const auto request = gf::aw::Request::decode(usb.writes().at(1));
        QCOMPARE(request.request, gf::aw::kExec);
    }

    void partitionBackupUsesAddressMap()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mapPath = writeTempFile(
            dir, QStringLiteral("map.txt"), QByteArrayLiteral("boot,0x40000000,0x40\n"));
        QVERIFY(!mapPath.isEmpty());

        gf::usb::MockTransport usb;
        queueOpen(usb);
        const QByteArray data(0x40, '\x77');
        usb.queueRead(data);
        usb.queueRead(okResponse());

        gf::flows::AwOptions options;
        options.partitionMapFile = mapPath;
        gf::flows::AwSession session(usb);
        session.open(options);
        gf::flows::AwMemoryIo io(session);
        const auto files = gf::flows::backupPartitions(io, dir.path(), {QStringLiteral("boot")},
                                                       gf::proto::Reporter::silent());
        QCOMPARE(files.size(), 1);
        QVERIFY(QFile::exists(files.first().path));
        QVERIFY(QFileInfo(files.first().path).fileName().startsWith(QStringLiteral("aw_boot_")));
        QCOMPARE(QFile(files.first().path).size(), qint64(0x40));
    }

    void eraseWritesZeroes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mapPath = writeTempFile(
            dir, QStringLiteral("map.txt"), QByteArrayLiteral("boot,0x40000000,0x10\n"));
        gf::usb::MockTransport usb;
        queueOpen(usb);
        usb.queueRead(okResponse()); // write zeroes

        gf::flows::AwOptions options;
        options.partitionMapFile = mapPath;
        gf::flows::AwSession session(usb);
        session.open(options);
        gf::flows::AwMemoryIo io(session);
        io.erasePartition(QStringLiteral("boot"), gf::proto::Reporter::silent());
        QCOMPARE(usb.writes().last(), QByteArray(0x10, '\0'));
    }

    void missingMapIsReadable()
    {
        gf::usb::MockTransport usb;
        queueOpen(usb);
        gf::flows::AwSession session(usb);
        session.open(gf::flows::AwOptions{});
        bool threw = false;
        try {
            session.partitionTable();
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("分区地址表")));
        }
        QVERIFY(threw);
    }
};

#include "test_flow_aw.moc"
GF_TEST_CLASS(TestFlowAw)
