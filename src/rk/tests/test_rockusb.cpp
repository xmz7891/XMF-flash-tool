#include <QtTest>

#include "TestRegistry.h"
#include "rk/RockUsb.h"
#include "usb/MockTransport.h"

namespace {

QByteArray makeCsw(quint32 tag, quint8 status = 0, quint32 residue = 0)
{
    QByteArray csw;
    csw += gf::proto::u32le(gf::rk::kCswSignature);
    csw += gf::proto::u32le(tag);
    csw += gf::proto::u32le(residue);
    csw.append(char(status));
    return csw;
}

} // namespace

class TestRockUsb : public QObject {
    Q_OBJECT

private slots:
    void cbwEncodeLayout()
    {
        const gf::rk::Cbw cbw = gf::rk::makeCbw(0x04, 1024, true, QByteArrayLiteral("\x01\x02"));
        const QByteArray raw = cbw.encode();
        QCOMPARE(raw.size(), gf::rk::kCbwSize);
        QCOMPARE(gf::rk::kCbwSize, 31);
        QCOMPARE(gf::proto::readU32le(raw, 0), gf::rk::kCbwSignature);
        QCOMPARE(gf::proto::readU32le(raw, 8), quint32(1024));
        QCOMPARE(quint8(raw[12]), gf::rk::kDirectionIn);
        QCOMPARE(quint8(raw[13]), quint8(0));
        QCOMPARE(quint8(raw[14]), quint8(3));
        QCOMPARE(quint8(raw[15]), quint8(0x04));
        QCOMPARE(quint8(raw[16]), quint8(0x01));
        QCOMPARE(quint8(raw[17]), quint8(0x02));
        QCOMPARE(quint8(raw[31 - 1]), quint8(0)); // padded command block
    }

    void cswDecode()
    {
        const auto csw = gf::rk::Csw::decode(makeCsw(0x11223344));
        QVERIFY(csw.valid);
        QVERIFY(csw.ok());
        QCOMPARE(csw.tag, quint32(0x11223344));

        const auto failed = gf::rk::Csw::decode(makeCsw(1, 1, 512));
        QVERIFY(failed.valid);
        QVERIFY(!failed.ok());
        QCOMPARE(failed.residue, quint32(512));

        QByteArray bad = makeCsw(1);
        bad[0] = 'X';
        const auto invalid = gf::rk::Csw::decode(bad);
        QVERIFY(!invalid.valid);
        QVERIFY(!invalid.error.isEmpty());
    }

    void readSectorCbwParameters()
    {
        const gf::rk::Cbw cbw = gf::rk::makeReadSector(0x12345678, 2);
        QCOMPARE(cbw.transferLength, quint32(1024));
        QCOMPARE(cbw.flags, gf::rk::kDirectionIn);
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdReadSector);
        QCOMPARE(quint8(cbw.cb[1]), quint8(0));
        QCOMPARE(gf::proto::readU32le(cbw.cb, 2), quint32(0x12345678));
        QCOMPARE(gf::proto::readU16le(cbw.cb, 6), quint16(2));
    }

    void writeSectorCbwParameters()
    {
        const gf::rk::Cbw cbw = gf::rk::makeWriteSector(0x00000010, 4);
        QCOMPARE(cbw.transferLength, quint32(2048));
        QCOMPARE(cbw.flags, gf::rk::kDirectionOut);
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdWriteSector);
        QCOMPARE(gf::proto::readU32le(cbw.cb, 2), quint32(0x10));
        QCOMPARE(gf::proto::readU16le(cbw.cb, 6), quint16(4));
    }

    void deviceInfoFlow()
    {
        gf::usb::MockTransport transport;
        QByteArray raw(128, '\0');
        raw[0] = 1; // version = 1
        const QByteArray chip = QByteArrayLiteral("RK3399");
        for (int i = 0; i < chip.size(); ++i)
            raw[4 + i] = chip[i];
        transport.queueRead(raw);
        transport.queueRead(makeCsw(1));

        gf::rk::Client client(transport);
        const gf::rk::DeviceInfo info = client.deviceInfo();
        QCOMPARE(info.version, quint32(1));
        QCOMPARE(info.chip, QStringLiteral("RK3399"));
        QCOMPARE(transport.writes().size(), 1);
        const gf::rk::Cbw cbw = gf::rk::Cbw::decode(transport.writes().first());
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdDeviceInfo);
        QCOMPARE(cbw.transferLength, quint32(128));
    }

    void readSectorsFlow()
    {
        gf::usb::MockTransport transport;
        const QByteArray payload(1024, '\x5A');
        transport.queueRead(payload);
        transport.queueRead(makeCsw(1));

        gf::rk::Client client(transport);
        const QByteArray data = client.readSectors(5, 2);
        QCOMPARE(data, payload);
        const gf::rk::Cbw cbw = gf::rk::Cbw::decode(transport.writes().first());
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdReadSector);
        QCOMPARE(gf::proto::readU32le(cbw.cb, 2), quint32(5));
        QCOMPARE(gf::proto::readU16le(cbw.cb, 6), quint16(2));
    }

    void writeSectorsFlow()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(makeCsw(1));

        const QByteArray data(1024, '\x33');
        gf::rk::Client client(transport);
        client.writeSectors(7, data);
        const auto writes = transport.writes();
        QCOMPARE(writes.size(), 2);
        const gf::rk::Cbw cbw = gf::rk::Cbw::decode(writes[0]);
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdWriteSector);
        QCOMPARE(cbw.transferLength, quint32(1024));
        QCOMPARE(writes[1], data);
    }

    void writeSectorsRejectsUnaligned()
    {
        gf::usb::MockTransport transport;
        gf::rk::Client client(transport);
        bool threw = false;
        try {
            client.writeSectors(0, QByteArray(100, '\0'));
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("整数倍")));
        }
        QVERIFY(threw);
    }

    void eraseSectorsFlow()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(makeCsw(1));
        gf::rk::Client client(transport);
        client.eraseSectors(0x100, 0x10);
        const gf::rk::Cbw cbw = gf::rk::Cbw::decode(transport.writes().first());
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdEraseSector);
        QCOMPARE(cbw.transferLength, quint32(0));
        QCOMPARE(gf::proto::readU32le(cbw.cb, 2), quint32(0x100));
        QCOMPARE(gf::proto::readU16le(cbw.cb, 6), quint16(0x10));
    }

    void resetToLoaderFlag()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(makeCsw(1));
        gf::rk::Client client(transport);
        client.rebootToLoader();
        const gf::rk::Cbw cbw = gf::rk::Cbw::decode(transport.writes().first());
        QCOMPARE(quint8(cbw.cb[0]), gf::rk::kCmdSetResetFlag);
        QCOMPARE(quint8(cbw.cb[4]), quint8(1));
    }

    void failedStatusThrows()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(makeCsw(1, 1));
        gf::rk::Client client(transport);
        bool threw = false;
        try {
            client.eraseSectors(0, 1);
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("失败")));
        }
        QVERIFY(threw);
    }

    void tagMismatchThrows()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(makeCsw(99));
        gf::rk::Client client(transport);
        bool threw = false;
        try {
            client.eraseSectors(0, 1);
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("标签")));
        }
        QVERIFY(threw);
    }

    void deviceModeName()
    {
        QVERIFY(gf::rk::deviceModeName(0x110A).contains(QStringLiteral("Rockchip")));
    }
};

#include "test_rockusb.moc"
GF_TEST_CLASS(TestRockUsb)
