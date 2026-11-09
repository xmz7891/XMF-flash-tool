#include <QtTest>

#include "TestRegistry.h"
#include "aw/Fel.h"
#include "usb/MockTransport.h"

namespace {

QByteArray okResponse()
{
    return QByteArray(8, '\0');
}

QByteArray versionPayload(quint32 version = 0x00160000, quint32 id = 0x1689)
{
    QByteArray payload;
    payload += QByteArrayLiteral("AWUSBFEX");
    payload += gf::proto::u32le(version);
    payload += gf::proto::u32le(id);
    payload += QByteArray(16, '\0'); // firmware words
    return payload;
}

} // namespace

class TestFel : public QObject {
    Q_OBJECT

private slots:
    void requestEncodeLayout()
    {
        const QByteArray raw = gf::aw::encodeRequest(gf::aw::kWrite, 0x1000, 0x40000000);
        QCOMPARE(raw.size(), int(gf::aw::kRequestSize));
        const gf::aw::Request request = gf::aw::Request::decode(raw);
        QCOMPARE(request.request, gf::aw::kWrite);
        QCOMPARE(request.length, quint32(0x1000));
        QCOMPARE(request.address, quint32(0x40000000));
        QCOMPARE(request.reserved, quint32(0));
    }

    void responseDecode()
    {
        const QByteArray raw = gf::proto::u32le(0) + gf::proto::u32le(7);
        const auto response = gf::aw::Response::decode(raw);
        QVERIFY(response.valid);
        QVERIFY(response.ok());
        QCOMPARE(response.tag, quint32(7));

        const auto failure = gf::aw::Response::decode(gf::proto::u32le(5));
        QVERIFY(failure.valid);
        QVERIFY(!failure.ok());
        QCOMPARE(failure.status, quint32(5));

        QVERIFY(!gf::aw::Response::decode(QByteArray(2, '\0')).valid);
    }

    void versionParsing()
    {
        const auto info = gf::aw::parseVersion(versionPayload());
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.magic, QStringLiteral("AWUSBFEX"));
        QCOMPARE(info.version, quint32(0x00160000));
        QCOMPARE(info.id, quint32(0x1689));
        QCOMPARE(gf::aw::socName(info.id), QStringLiteral("A64"));

        auto bad = versionPayload();
        bad[0] = 'X';
        const auto invalid = gf::aw::parseVersion(bad);
        QVERIFY(!invalid.valid);
        QVERIFY(!invalid.error.isEmpty());
    }

    void helloFlow()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(versionPayload());
        transport.queueRead(okResponse());

        gf::aw::Client client(transport);
        const auto info = client.hello();
        QVERIFY(info.valid);
        QCOMPARE(info.id, quint32(0x1689));

        QCOMPARE(transport.writes().size(), 1);
        const auto request = gf::aw::Request::decode(transport.writes().first());
        QCOMPARE(request.request, gf::aw::kHello);
        QCOMPARE(request.length, gf::aw::kVersionSize);
    }

    void readFlow()
    {
        gf::usb::MockTransport transport;
        const QByteArray memory(64, '\x42');
        transport.queueRead(memory);
        transport.queueRead(okResponse());

        gf::aw::Client client(transport);
        const QByteArray data = client.read(0x40000000, 64);
        QCOMPARE(data, memory);
        const auto request = gf::aw::Request::decode(transport.writes().first());
        QCOMPARE(request.request, gf::aw::kRead);
        QCOMPARE(request.length, quint32(64));
        QCOMPARE(request.address, quint32(0x40000000));
    }

    void writeChunkingFlow()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(okResponse());
        transport.queueRead(okResponse());

        const QByteArray data(int(gf::aw::kMaxChunk) + 100, '\x55');
        gf::aw::Client client(transport);
        client.write(0x50000000, data, gf::aw::kMaxChunk);

        // Two chunks => two (request, data) pairs.
        QCOMPARE(transport.writes().size(), 4);
        const auto first = gf::aw::Request::decode(transport.writes()[0]);
        QCOMPARE(first.request, gf::aw::kWrite);
        QCOMPARE(first.length, quint32(gf::aw::kMaxChunk));
        QCOMPARE(first.address, quint32(0x50000000));
        QCOMPARE(transport.writes()[1], data.left(int(gf::aw::kMaxChunk)));

        const auto second = gf::aw::Request::decode(transport.writes()[2]);
        QCOMPARE(second.request, gf::aw::kWrite);
        QCOMPARE(second.length, quint32(100));
        QCOMPARE(second.address, quint32(0x50000000 + gf::aw::kMaxChunk));
        QCOMPARE(transport.writes()[3], data.mid(int(gf::aw::kMaxChunk)));
    }

    void execFlow()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(okResponse());
        gf::aw::Client client(transport);
        client.exec(0x40000000);
        const auto request = gf::aw::Request::decode(transport.writes().first());
        QCOMPARE(request.request, gf::aw::kExec);
        QCOMPARE(request.address, quint32(0x40000000));
    }

    void failedStatusThrows()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::proto::u32le(9));
        gf::aw::Client client(transport);
        bool threw = false;
        try {
            client.exec(0);
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("FEL 命令")));
        }
        QVERIFY(threw);
    }

    void missingResponseIsReadable()
    {
        gf::usb::MockTransport transport;
        gf::aw::Client client(transport);
        bool threw = false;
        try {
            client.exec(0);
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("FEL")));
        }
        QVERIFY(threw);
    }

    void unknownSocNameIsHex()
    {
        const QString name = gf::aw::socName(0xDEADBEEF);
        QVERIFY(name.contains(QStringLiteral("deadbeef")));
    }
};

#include "test_fel.moc"
GF_TEST_CLASS(TestFel)
