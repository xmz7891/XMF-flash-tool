#include <QtTest>

#include "TestRegistry.h"
#include "qcom/Firehose.h"
#include "usb/MockTransport.h"

using gf::qcom::firehose::Client;
using gf::qcom::firehose::Response;
using gf::qcom::firehose::StorageConfig;

namespace {

QByteArray ackResponse()
{
    return QByteArrayLiteral("<response value=\"ACK\" rawmode=\"false\" />");
}

QByteArray nakResponse()
{
    return QByteArrayLiteral("<response value=\"NAK\" rawmode=\"false\" />");
}

} // namespace

class TestFirehose : public QObject {
    Q_OBJECT

private slots:
    void parseAck()
    {
        const Response response =
            gf::qcom::firehose::parseResponseXml(QString::fromUtf8(ackResponse()));
        QVERIFY(response.received);
        QVERIFY(response.acknowledged);
        QVERIFY(!response.nak);
        QVERIFY(response.ok());
        QCOMPARE(response.value, QStringLiteral("ACK"));
    }

    void parseNakWithLog()
    {
        const QString raw = QStringLiteral(
            "<log value=\"ERROR: 0x1234\" /><response value=\"NAK\" rawmode=\"false\" />");
        const Response response = gf::qcom::firehose::parseResponseXml(raw);
        QVERIFY(response.received);
        QVERIFY(response.nak);
        QVERIFY(!response.ok());
        QCOMPARE(response.logs.size(), 1);
        QCOMPARE(response.logs.first(), QStringLiteral("ERROR: 0x1234"));
    }

    void responseCompleteDetection()
    {
        QVERIFY(gf::qcom::firehose::responseComplete(ackResponse()));
        QVERIFY(!gf::qcom::firehose::responseComplete(QByteArrayLiteral("<log value=\"x\" />")));
        QVERIFY(!gf::qcom::firehose::responseComplete(QByteArrayLiteral("<response value=\"ACK\"")));
        QVERIFY(gf::qcom::firehose::responseComplete(
            QByteArrayLiteral("<response value=\"ACK\"><storage_info /></response>")));
    }

    void wrapCommandAddsDataRoot()
    {
        const QString wrapped =
            gf::qcom::firehose::wrapCommand(QStringLiteral("<nop />"));
        QVERIFY(wrapped.startsWith(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>")));
        QVERIFY(wrapped.contains(QStringLiteral("<data><nop /></data>")));
    }

    void configureSendsExpectedXml()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(ackResponse());
        Client client(transport);

        StorageConfig config;
        config.memoryName = QStringLiteral("ufs");
        config.maxPayloadBytes = 1048576;
        const Response response = client.configure(config);
        QVERIFY2(response.ok(), qPrintable(response.error));

        const QByteArray written = transport.written();
        const QString xml = QString::fromUtf8(written);
        QVERIFY(xml.contains(QStringLiteral("<configure MemoryName=\"ufs\"")));
        QVERIFY(xml.contains(QStringLiteral("MaxPayloadSizeToTargetInBytes=\"1048576\"")));
        QVERIFY(xml.contains(QStringLiteral("<data>")));
        QVERIFY(xml.contains(QStringLiteral("</data>")));
    }

    void fragmentedResponseIsAssembled()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArrayLiteral("<respo"));
        transport.queueRead(QByteArrayLiteral("nse value=\"ACK\" rawmode=\"false\" />"));
        Client client(transport);
        const Response response = client.nop();
        QVERIFY2(response.ok(), qPrintable(response.error));
    }

    void nackIsReported()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(nakResponse());
        Client client(transport);
        const Response response = client.nop();
        QVERIFY(response.received);
        QVERIFY(!response.ok());
        QVERIFY(response.nak);
    }

    void missingResponseIsReported()
    {
        gf::usb::MockTransport transport;
        Client client(transport);
        const Response response = client.nop(30);
        QVERIFY(!response.received);
        QVERIFY(!response.error.isEmpty());
    }

    void readSectorsGetsPayload()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(ackResponse());
        QByteArray payload(4096 * 2, '\x5A');
        transport.queueRead(payload);
        Client client(transport);

        QByteArray data;
        const Response response = client.readSectors(QStringLiteral("ufs"), 0, 10, 2, 4096, &data);
        QVERIFY2(response.ok(), qPrintable(response.error));
        QCOMPARE(data.size(), 8192);
        QCOMPARE(data, payload);

        const QString xml = QString::fromUtf8(transport.writes().first());
        QVERIFY(xml.contains(QStringLiteral("<read")));
        QVERIFY(xml.contains(QStringLiteral("start_sector=\"10\"")));
        QVERIFY(xml.contains(QStringLiteral("num_sectors=\"2\"")));
        QVERIFY(xml.contains(QStringLiteral("SECTOR_SIZE_IN_BYTES=\"4096\"")));
    }

    void programSectorsWritesPaddedChunks()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(ackResponse());
        transport.queueRead(ackResponse());
        Client client(transport);

        QByteArray file(5000, '\x11');
        const Response response = client.programSectors(QStringLiteral("ufs"), 0, 100, file, 4096,
                                                        QStringLiteral("test.bin"));
        QVERIFY2(response.ok(), qPrintable(response.error));

        const auto writes = transport.writes();
        QCOMPARE(writes.size(), 2);
        const QString xml = QString::fromUtf8(writes[0]);
        QVERIFY(xml.contains(QStringLiteral("<program")));
        QVERIFY(xml.contains(QStringLiteral("start_sector=\"100\"")));
        // 5000 bytes -> two 4096-byte sectors
        QVERIFY(xml.contains(QStringLiteral("num_partial_sectors=\"2\"")));
        QCOMPARE(writes[1].size(), 8192);
        QVERIFY(writes[1].startsWith(file));
    }

    void programSectorsWithoutDataAck()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(ackResponse());
        Client client(transport);
        const Response response = client.programSectors(QStringLiteral("emmc"), 0, 0,
                                                        QByteArray(4096, '\x22'), 4096,
                                                        QStringLiteral("x.bin"), 1024 * 1024, false);
        QVERIFY2(response.ok(), qPrintable(response.error));
        QCOMPARE(transport.writes().size(), 2);
    }

    void eraseAndPowerXml()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(ackResponse());
        transport.queueRead(ackResponse());
        Client client(transport);

        QVERIFY(client.eraseSectors(QStringLiteral("ufs"), 0, 64, 16, 4096).ok());
        const QString eraseXml = QString::fromUtf8(transport.writes()[0]);
        QVERIFY(eraseXml.contains(QStringLiteral("<erase")));
        QVERIFY(eraseXml.contains(QStringLiteral("start_sector=\"64\"")));
        QVERIFY(eraseXml.contains(QStringLiteral("num_sectors=\"16\"")));

        QVERIFY(client.power(QStringLiteral("reset")).ok());
        const QString powerXml = QString::fromUtf8(transport.writes()[1]);
        QVERIFY(powerXml.contains(QStringLiteral("<power value=\"reset\" />")));
    }

    void patchSendsInlineValue()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(ackResponse());
        Client client(transport);

        gf::qcom::firehose::Patch patch;
        patch.startSector = 1;
        patch.byteOffset = 2;
        patch.value = QByteArrayLiteral("\x01\x02");
        const Response response = client.patch(patch);
        QVERIFY2(response.ok(), qPrintable(response.error));

        const QString xml = QString::fromUtf8(transport.writes().first());
        QVERIFY(xml.contains(QStringLiteral("<patch")));
        QVERIFY(xml.contains(QStringLiteral("value=\"0x0102\"")));
        QVERIFY(xml.contains(QStringLiteral("size_in_bytes=\"2\"")));
        QVERIFY(xml.contains(QStringLiteral("byte_offset=\"2\"")));
        QVERIFY(xml.contains(QStringLiteral("start_sector=\"1\"")));
    }

    void patchRejectsEmptyValue()
    {
        gf::usb::MockTransport transport;
        Client client(transport);
        bool threw = false;
        try {
            client.patch(gf::qcom::firehose::Patch{});
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("patch")));
        }
        QVERIFY(threw);
    }

    void buildersProduceWellFormedCommands()
    {
        QVERIFY(gf::qcom::firehose::buildConfigureXml(StorageConfig())
                    .contains(QStringLiteral("MemoryName=\"ufs\"")));
        QVERIFY(gf::qcom::firehose::buildReadXml(QStringLiteral("ufs"), 0, 1, 2, 4096)
                    .contains(QStringLiteral("num_sectors=\"2\"")));
        QVERIFY(gf::qcom::firehose::buildEraseXml(QStringLiteral("ufs"), 0, 1, 2, 4096)
                    .contains(QStringLiteral("<erase")));
        QVERIFY(gf::qcom::firehose::buildPowerXml(QStringLiteral("off"))
                    .contains(QStringLiteral("value=\"off\"")));
        gf::qcom::firehose::Patch patch;
        patch.value = QByteArrayLiteral("\xAA");
        QVERIFY(gf::qcom::firehose::buildPatchXml(patch)
                    .contains(QStringLiteral("value=\"0xaa\"")));
    }
};

#include "test_firehose.moc"
GF_TEST_CLASS(TestFirehose)
