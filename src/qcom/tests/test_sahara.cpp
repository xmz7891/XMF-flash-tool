#include <QtTest>

#include "TestRegistry.h"
#include "qcom/Sahara.h"
#include "usb/MockTransport.h"
#include "usb/tests/TestData.h"

using gf::qcom::sahara::Client;
using gf::qcom::sahara::DeviceHello;

namespace {

DeviceHello expectedResponse(quint32 version, quint32 minVersion, quint32 maxCommandLength)
{
    DeviceHello hello;
    hello.valid = true;
    hello.version = version;
    hello.minVersion = minVersion;
    hello.maxCommandLength = maxCommandLength;
    hello.mode = gf::qcom::sahara::kModeImageTxPending;
    return hello;
}

} // namespace

class TestSahara : public QObject {
    Q_OBJECT

private slots:
    void helloPacketLayout()
    {
        const QByteArray packet = gf::qcom::sahara::buildHelloPacket(2, 1, 0x1000, 0);
        QCOMPARE(packet.size(), 48);
        const auto header = gf::qcom::sahara::parseHeader(packet);
        QCOMPARE(header.command, gf::qcom::sahara::kCmdHello);
        QCOMPARE(header.length, quint32(48));
        QCOMPARE(gf::proto::readU32le(packet, 8), quint32(2));  // version
        QCOMPARE(gf::proto::readU32le(packet, 12), quint32(1)); // min version
        QCOMPARE(gf::proto::readU32le(packet, 16), quint32(0x1000));
    }

    void handshakeScriptedDevice()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildHelloPacket(2, 1, 0x1000, 0));

        Client client(transport);
        const DeviceHello hello = client.handshake();
        QVERIFY(hello.valid);
        QCOMPARE(hello.version, quint32(2));
        QVERIFY(client.handshakeDone());

        const auto writes = transport.writes();
        QCOMPARE(writes.size(), 1);
        const QByteArray expected =
            gf::qcom::sahara::buildHelloResponse(expectedResponse(2, 1, 0x1000));
        QCOMPARE(writes.first(), expected);
        QCOMPARE(gf::qcom::sahara::parseHeader(writes.first()).length, quint32(48));
    }

    void handshakeWrongCommandThrows()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildPacket(0x99, QByteArray(8, '\0')));
        Client client(transport);
        bool threw = false;
        try {
            client.handshake();
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("握手失败")));
        }
        QVERIFY(threw);
    }

    void uploadScriptedDevice()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildHelloPacket(2, 1, 0x1000, 0));
        transport.queueRead(gf::qcom::sahara::buildReadDataRequest(4));
        transport.queueRead(gf::qcom::sahara::buildDonePacket(1, 0));

        Client client(transport);
        const auto result = client.upload(QByteArrayLiteral("ABCD"));
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(result.bytesTransferred, qint64(4));
        QCOMPARE(result.imageId, quint32(1));

        const auto writes = transport.writes();
        QCOMPARE(writes.size(), 4);
        QCOMPARE(writes[1], QByteArrayLiteral("ABCD"));
        QCOMPARE(writes[2], gf::qcom::sahara::buildEndImageTransfer(0, false));
        QCOMPARE(writes[3], gf::qcom::sahara::buildDoneResponse(1, 0, false));
    }

    void upload64ScriptedDevice()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildHelloPacket(3, 2, 0x1000, 0));
        transport.queueRead(gf::qcom::sahara::buildReadDataRequest(4, true));
        transport.queueRead(gf::qcom::sahara::buildDonePacket(1, 0, true));

        Client client(transport);
        const auto result = client.upload(QByteArrayLiteral("EFGH"));
        QVERIFY2(result.success, qPrintable(result.error));

        const auto writes = transport.writes();
        QCOMPARE(writes.size(), 4);
        QCOMPARE(writes[2], gf::qcom::sahara::buildEndImageTransfer(0, true));
        QCOMPARE(writes[3], gf::qcom::sahara::buildDoneResponse(1, 0, true));
    }

    void uploadUnexpectedCommandFails()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildHelloPacket());
        transport.queueRead(gf::qcom::sahara::buildPacket(0x55, QByteArray(4, '\0')));

        Client client(transport);
        const auto result = client.upload(QByteArrayLiteral("AAAA"));
        QVERIFY(!result.success);
        QVERIFY(result.error.contains(QStringLiteral("未预期")));
    }

    void uploadFailureStatusFails()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildHelloPacket());
        transport.queueRead(gf::qcom::sahara::buildReadDataRequest(4));
        transport.queueRead(gf::qcom::sahara::buildDonePacket(1, 0x42));

        Client client(transport);
        const auto result = client.upload(QByteArrayLiteral("ABCD"));
        QVERIFY(!result.success);
        QCOMPARE(result.status, quint32(0x42));
        QVERIFY(result.error.contains(QStringLiteral("42")));
    }

    void uploadRealDevprgElf()
    {
        const QByteArray image =
            xmf_test::repoBytes(QStringLiteral("_research/extract/package_qualcomm_edl-master/"
                                               "Common/prog_ufs_firehose_sdm845.elf"));
        if (image.isEmpty())
            QSKIP("缺少 devprg ELF 样本，跳过");

        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildHelloPacket(3, 1, 0x1000, 0));
        constexpr qint64 kChunk = 64 * 1024;
        for (qint64 offset = 0; offset < image.size(); offset += kChunk) {
            const quint32 size = quint32(qMin<qint64>(kChunk, image.size() - offset));
            transport.queueRead(gf::qcom::sahara::buildReadDataRequest(size, true));
        }
        transport.queueRead(gf::qcom::sahara::buildDonePacket(1, 0, true));

        Client client(transport);
        const auto result = client.upload(image);
        QVERIFY2(result.success, qPrintable(result.error));
        QCOMPARE(result.bytesTransferred, qint64(image.size()));

        // hello response + one write per READ_DATA + end transfers + done response
        QVERIFY(transport.writeCount() > int(image.size() / kChunk));
    }

    void executeScriptedDevice()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildHelloPacket(2, 1, 0x1000, 0));
        transport.queueRead(
            gf::qcom::sahara::buildExecuteResponsePacket(0, 0, false));

        Client client(transport);
        client.handshake();
        client.execute(0x12345678);
        const auto writes = transport.writes();
        QCOMPARE(writes.size(), 2);
        QCOMPARE(writes[1], gf::qcom::sahara::buildExecutePacket(0x12345678, 0, false));
    }

    void execute64ScriptedDevice()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(gf::qcom::sahara::buildHelloPacket(3, 2, 0x1000, 0));
        transport.queueRead(gf::qcom::sahara::buildExecuteResponsePacket(0, 0, true));

        Client client(transport);
        client.handshake();
        client.execute(0x1'0000'2000ull);
        const auto writes = transport.writes();
        QCOMPARE(writes.size(), 2);
        QCOMPARE(writes[1], gf::qcom::sahara::buildExecutePacket(0x1'0000'2000ull, 0, true));
    }
};

#include "test_sahara.moc"
GF_TEST_CLASS(TestSahara)
