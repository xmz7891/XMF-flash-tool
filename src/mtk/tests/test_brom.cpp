#include <QtTest>

#include "TestRegistry.h"
#include "mtk/Brom.h"
#include "usb/MockTransport.h"

using gf::mtk::brom::Client;

namespace {

QByteArray be32(quint32 value)
{
    return gf::proto::u32be(value);
}

} // namespace

class TestMtkBrom : public QObject {
    Q_OBJECT

private slots:
    void handshakeSendsA0()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArray(1, char(gf::mtk::brom::kHandshakeReply)));
        Client client(transport);
        client.handshake();
        QCOMPARE(transport.writeCount(), 1);
        QCOMPARE(transport.writes().first(), QByteArray(1, char(0xA0)));
    }

    void handshakeFailureIsReadable()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArray(1, char(0x00)));
        Client client(transport);
        bool threw = false;
        try {
            client.handshake();
        } catch (const std::exception& error) {
            threw = true;
            const QString text = QString::fromUtf8(error.what());
            QVERIFY(text.contains(QStringLiteral("握手失败")));
            QVERIFY(text.contains(QStringLiteral("BROM")));
        }
        QVERIFY(threw);
    }

    void read32FrameAndValue()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::proto::u32be(0x12345678));
        Client client(transport);
        QCOMPARE(client.read32(0x10001000), quint32(0x12345678));
        QCOMPARE(transport.writes().first(),
                 gf::mtk::brom::frame(gf::mtk::brom::kCmdRead32, be32(0x10001000)));
    }

    void read16FrameAndValue()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::proto::u16be(0xBEEF));
        Client client(transport);
        QCOMPARE(client.read16(0x2000), quint16(0xBEEF));
        QCOMPARE(transport.writes().first(),
                 gf::mtk::brom::frame(gf::mtk::brom::kCmdRead16, be32(0x2000)));
    }

    void write32Echo()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::proto::u32be(0xDEADBEEF));
        Client client(transport);
        client.write32(0x3000, 0xDEADBEEF);
        QByteArray expected = be32(0x3000) + be32(0xDEADBEEF);
        QCOMPARE(transport.writes().first(),
                 gf::mtk::brom::frame(gf::mtk::brom::kCmdWrite32, expected));
    }

    void write32EchoMismatchThrows()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::proto::u32be(0x00000000));
        Client client(transport);
        bool threw = false;
        try {
            client.write32(0x3000, 0xDEADBEEF);
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("回显不一致")));
        }
        QVERIFY(threw);
    }

    void echoCommand()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(be32(0xCAFEBABE));
        Client client(transport);
        QCOMPARE(client.echo(0xCAFEBABE), quint32(0xCAFEBABE));
        QCOMPARE(transport.writes().first(),
                 gf::mtk::brom::frame(gf::mtk::brom::kCmdEcho, be32(0xCAFEBABE)));
    }

    void sendDaFrameAndStatus()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArray(2, '\0'));
        Client client(transport);
        client.sendDa(0x00200000, QByteArrayLiteral("DADATA"), 0);

        QCOMPARE(transport.writeCount(), 2);
        QByteArray header = be32(0x00200000) + be32(6) + be32(0);
        QCOMPARE(transport.writes()[0],
                 gf::mtk::brom::frame(gf::mtk::brom::kCmdSendDa, header));
        QCOMPARE(transport.writes()[1], QByteArrayLiteral("DADATA"));
    }

    void sendDaFailureStatusThrows()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArrayLiteral("\x12\x34"));
        Client client(transport);
        bool threw = false;
        try {
            client.sendDa(0x200000, QByteArrayLiteral("DA"));
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("SEND_DA")));
        }
        QVERIFY(threw);
    }

    void jumpDaFrame()
    {
        gf::usb::MockTransport transport;
        Client client(transport);
        client.jumpDa(0x00200000);
        QCOMPARE(transport.writes().first(),
                 gf::mtk::brom::frame(gf::mtk::brom::kCmdJumpDa, be32(0x00200000)));
    }

    void targetConfig()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArrayLiteral("\x01\x02\x03\x04\x05\x06\x07\x08"));
        Client client(transport);
        const QByteArray config = client.getTargetConfig();
        QCOMPARE(config.size(), 8);
        QCOMPARE(transport.writes().first(),
                 gf::mtk::brom::frame(gf::mtk::brom::kCmdGetTargetConfig));
    }
};

#include "test_brom.moc"
GF_TEST_CLASS(TestMtkBrom)
