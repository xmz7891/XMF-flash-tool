#include <QtTest>

#include "TestRegistry.h"
#include "sprd/Fdl.h"
#include "sprd/U2s.h"
#include "usb/MockTransport.h"

namespace {

QByteArray statusFrame(quint8 command, quint8 status = 0)
{
    return gf::sprd::u2s::encodeFrame(command, QByteArray(1, char(status)));
}

} // namespace

class TestSprdU2s : public QObject {
    Q_OBJECT

private slots:
    void crc16KnownVector()
    {
        // CRC-16/CCITT-FALSE("123456789") = 0x29B1.
        QCOMPARE(gf::sprd::u2s::crc16(QByteArrayLiteral("123456789")), quint16(0x29B1));
    }

    void escapeRoundTrip()
    {
        const QByteArray raw = QByteArrayLiteral("\x01\x7E\x02\x7D\x03\x00");
        const QByteArray escaped = gf::sprd::u2s::escape(raw);
        QVERIFY(escaped.contains(QByteArrayLiteral("\x7D\x5E")));
        QVERIFY(escaped.contains(QByteArrayLiteral("\x7D\x5D")));
        QCOMPARE(gf::sprd::u2s::unescape(escaped), raw);
    }

    void frameRoundTripWithSpecialBytes()
    {
        QByteArray payload;
        payload.append(char(0x7E));
        payload.append(char(0x7D));
        payload.append(QByteArrayLiteral("SPRD"));
        payload.append(char(0x00));
        const QByteArray encoded = gf::sprd::u2s::encodeFrame(0x01, payload);
        QCOMPARE(quint8(encoded.at(0)), gf::sprd::u2s::kFlag);
        QCOMPARE(quint8(encoded.at(encoded.size() - 1)), gf::sprd::u2s::kFlag);

        const auto frame = gf::sprd::u2s::decodeFrame(encoded);
        QVERIFY2(frame.valid, qPrintable(frame.error));
        QCOMPARE(frame.type, quint8(0x01));
        QCOMPARE(frame.payload, payload);
    }

    void emptyFrameLayout()
    {
        const QByteArray encoded = gf::sprd::u2s::encodeFrame(0x00);
        // 7E + type + len(2) + crc(2) + 7E = 7 bytes
        QCOMPARE(encoded.size(), 7);
        const auto frame = gf::sprd::u2s::decodeFrame(encoded);
        QVERIFY(frame.valid);
        QCOMPARE(frame.type, quint8(0));
        QVERIFY(frame.payload.isEmpty());
    }

    void corruptCrcIsRejected()
    {
        QByteArray encoded = gf::sprd::u2s::encodeFrame(0x01, QByteArrayLiteral("DATA"));
        encoded[encoded.size() - 2] = char(quint8(encoded[encoded.size() - 2]) ^ 0xFF);
        const auto frame = gf::sprd::u2s::decodeFrame(encoded);
        QVERIFY(!frame.valid);
        QVERIFY(frame.error.contains(QStringLiteral("CRC")));
    }

    void linkTransactOverMock()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(statusFrame(gf::sprd::fdl::kConnect));
        gf::sprd::u2s::Link link(transport);
        const auto frame = link.transact(gf::sprd::fdl::kConnect);
        QVERIFY2(frame.valid, qPrintable(frame.error));
        QCOMPARE(frame.type, quint8(gf::sprd::fdl::kConnect));
        QCOMPARE(frame.payload, QByteArray(1, '\0'));
    }

    void linkHandlesFragmentedFrame()
    {
        gf::usb::MockTransport transport;
        const QByteArray encoded = statusFrame(gf::sprd::fdl::kReadVersion, 0);
        transport.queueRead(encoded.left(3));
        transport.queueRead(encoded.mid(3));
        gf::sprd::u2s::Link link(transport);
        const auto frame = link.receive(500);
        QVERIFY2(frame.valid, qPrintable(frame.error));
        QCOMPARE(frame.type, quint8(gf::sprd::fdl::kReadVersion));
    }

    void linkTimeoutIsReadable()
    {
        gf::usb::MockTransport transport;
        gf::sprd::u2s::Link link(transport);
        const auto frame = link.receive(20);
        QVERIFY(!frame.valid);
        QVERIFY(frame.error.contains(QStringLiteral("超时")));
    }

    void connectCommand()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(statusFrame(gf::sprd::fdl::kConnect));
        gf::sprd::fdl::Client client(transport);
        client.connect();
        const auto written = gf::sprd::u2s::decodeFrame(transport.writes().first());
        QVERIFY(written.valid);
        QCOMPARE(written.type, quint8(gf::sprd::fdl::kConnect));
    }

    void connectFailureStatus()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(statusFrame(gf::sprd::fdl::kConnect, 0x05));
        gf::sprd::fdl::Client client(transport);
        bool threw = false;
        try {
            client.connect();
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("CONNECT")));
        }
        QVERIFY(threw);
    }

    void readVersion()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(
            gf::sprd::u2s::encodeFrame(gf::sprd::fdl::kReadVersion, QByteArrayLiteral("FDL1_V1.0")));
        gf::sprd::fdl::Client client(transport);
        QCOMPARE(client.readVersion(), QStringLiteral("FDL1_V1.0"));
    }

    void downloadFdlFrameSequence()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(statusFrame(gf::sprd::fdl::kStartData));
        transport.queueRead(statusFrame(gf::sprd::fdl::kEndData));
        transport.queueRead(statusFrame(gf::sprd::fdl::kExecData));
        gf::sprd::fdl::Client client(transport);

        const QByteArray image(100, '\x5A');
        client.downloadFdl(0x65000800, image, 0);
        QCOMPARE(transport.writeCount(), 4);

        const auto start = gf::sprd::u2s::decodeFrame(transport.writes()[0]);
        QVERIFY(start.valid);
        QCOMPARE(start.type, quint8(gf::sprd::fdl::kStartData));
        const QByteArray startPayload = start.payload;
        QCOMPARE(startPayload.size(), 9);
        QCOMPARE(gf::proto::readU32be(startPayload, 0), quint32(0x65000800));
        QCOMPARE(gf::proto::readU32be(startPayload, 4), quint32(100));
        QCOMPARE(quint8(startPayload.at(8)), quint8(0));

        const auto midst = gf::sprd::u2s::decodeFrame(transport.writes()[1]);
        QVERIFY(midst.valid);
        QCOMPARE(midst.type, quint8(gf::sprd::fdl::kMidstData));
        QCOMPARE(midst.payload, image);

        const auto end = gf::sprd::u2s::decodeFrame(transport.writes()[2]);
        QVERIFY(end.valid);
        QCOMPARE(end.type, quint8(gf::sprd::fdl::kEndData));

        const auto exec = gf::sprd::u2s::decodeFrame(transport.writes()[3]);
        QVERIFY(exec.valid);
        QCOMPARE(exec.type, quint8(gf::sprd::fdl::kExecData));
        QCOMPARE(gf::proto::readU32be(exec.payload, 0), quint32(0x65000800));
    }

    void eraseFlashCommand()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(statusFrame(gf::sprd::fdl::kEraseFlash));
        gf::sprd::fdl::Client client(transport);
        client.eraseFlash(0x1000, 0x2000);
        const auto frame = gf::sprd::u2s::decodeFrame(transport.writes().first());
        QVERIFY(frame.valid);
        QCOMPARE(frame.type, quint8(gf::sprd::fdl::kEraseFlash));
        QCOMPARE(gf::proto::readU32be(frame.payload, 0), quint32(0x1000));
        QCOMPARE(gf::proto::readU32be(frame.payload, 4), quint32(0x2000));
    }
};

#include "test_u2s.moc"
GF_TEST_CLASS(TestSprdU2s)
