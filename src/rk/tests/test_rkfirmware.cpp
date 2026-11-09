#include <QtTest>

#include <QByteArray>

#include "TestRegistry.h"
#include "rk/RkFirmware.h"

using namespace gf::rockchip;

namespace {

void putLe32(QByteArray& data, int offset, quint32 value)
{
    data[offset] = char(value & 0xFF);
    data[offset + 1] = char((value >> 8) & 0xFF);
    data[offset + 2] = char((value >> 16) & 0xFF);
    data[offset + 3] = char((value >> 24) & 0xFF);
}

void putName(QByteArray& data, int offset, int length, const char* text)
{
    const QByteArray raw(text);
    for (int i = 0; i < raw.size() && i < length - 1; ++i)
        data[offset + i] = raw.at(i);
}

QByteArray buildRkaf()
{
    QByteArray data(int(kRkafHeaderSize) + 0x3000, '\0');
    data.replace(0, 4, "RKAF");
    putLe32(data, 4, quint32(data.size() - 4));
    putName(data, 8, 34, "XMFRK-TEST");
    putName(data, 72, 56, "XMF");
    putLe32(data, 132, 0x01020304);
    const int partsBase = 140;
    putLe32(data, 136, 3);
    // boot
    putName(data, partsBase, 32, "boot");
    putName(data, partsBase + 32, 60, "boot.img");
    putLe32(data, partsBase + 92, 0x40000);
    putLe32(data, partsBase + 96, 2048);
    putLe32(data, partsBase + 104, 0x40000);
    putLe32(data, partsBase + 108, 0x1000);
    // system
    putName(data, partsBase + 112, 32, "system");
    putName(data, partsBase + 112 + 32, 60, "system.img");
    putLe32(data, partsBase + 112 + 92, 0x800000);
    putLe32(data, partsBase + 112 + 96, 2048 + 0x1000);
    putLe32(data, partsBase + 112 + 104, 0x800000);
    putLe32(data, partsBase + 112 + 108, 0x2000);
    // SELF placeholder must be skipped
    putName(data, partsBase + 224, 32, "self");
    putName(data, partsBase + 224 + 32, 60, "SELF");
    putLe32(data, partsBase + 224 + 108, 0x100);
    // fill payload
    for (int i = 0; i < 0x1000; ++i)
        data[2048 + i] = char(i % 251);
    for (int i = 0; i < 0x2000; ++i)
        data[2048 + 0x1000 + i] = char((i * 7) % 253);
    return data;
}

} // namespace

class TestRkFirmware : public QObject {
    Q_OBJECT

private slots:
    void parsesRkafHeader()
    {
        const QByteArray data = buildRkaf();
        const RkafInfo info = parseRkafHeader(data);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.model, QStringLiteral("XMFRK-TEST"));
        QCOMPARE(info.manufacturer, QStringLiteral("XMF"));
        QCOMPARE(info.numParts, 3u);
        QCOMPARE(info.partitions.size(), 2); // SELF skipped
        QCOMPARE(info.partitions.at(0).name, QStringLiteral("boot"));
        QCOMPARE(info.partitions.at(0).partOffset, 2048u);
        QCOMPARE(info.partitions.at(0).byteCount, 0x1000u);
        QCOMPARE(info.partitions.at(1).name, QStringLiteral("system"));
        QCOMPARE(info.partitions.at(1).partOffset, 2048u + 0x1000u);
    }

    void parsesRkfwWrapper()
    {
        QByteArray data(0x100, '\0');
        data.replace(0, 4, "RKFW");
        data[6] = char(1);
        data[7] = char(0);
        data[8] = char(7);
        data[9] = char(2); // version 2.7.1
        data[0x0E] = char(0xE8);
        data[0x0F] = char(0x07); // year 2024
        data[0x10] = 3;
        data[0x11] = 14;
        data[0x12] = 9;
        data[0x13] = 30;
        data[0x14] = 15;
        data[0x15] = char(0x38); // RK3566
        putLe32(data, 0x19, 0x29);
        putLe32(data, 0x1D, 0x40);
        putLe32(data, 0x21, 0x100);
        putLe32(data, 0x25, 0x800);

        const RkfwInfo info = parseRkfwHeader(data);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.version, QStringLiteral("2.7.1"));
        QCOMPARE(info.chipCode, quint8(0x38));
        QCOMPARE(info.chipName, QStringLiteral("RK3566"));
        QCOMPARE(info.year, 2024);
        QCOMPARE(info.month, 3);
        QCOMPARE(info.updateOffset, 0x100u);
        QCOMPARE(info.updateSize, 0x800u);
    }

    void rejectsForeignData()
    {
        QByteArray data(64, '\0');
        data.replace(0, 4, "PK\x03\x04");
        QVERIFY(!parseRkfwHeader(data).ok);
        QVERIFY(!parseRkafHeader(data).ok);
    }

    void parsesEmbeddedRkaf()
    {
        QByteArray image(0x100, '\0');
        const QByteArray rkaf = buildRkaf();
        image += rkaf;
        const RkafInfo info = parseRkafAt(image, 0x100);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.partitions.size(), 2);
        QCOMPARE(info.partitions.at(1).name, QStringLiteral("system"));
    }
};

GF_TEST_CLASS(TestRkFirmware)

#include "test_rkfirmware.moc"
