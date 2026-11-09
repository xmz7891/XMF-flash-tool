#include <QtTest>

#include <QByteArray>

#include "TestRegistry.h"
#include "aw/AwFirmware.h"

using namespace gf::alwwin;

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
    for (int i = 0; i < raw.size() && i < length; ++i)
        data[offset + i] = raw.at(i);
}

struct AwTestImage {
    QByteArray header;
    QByteArray table;
    quint32 physicalSize = 0;
};

AwTestImage buildImage()
{
    const quint32 fileHeaderLength = 0x200;
    const quint32 numFiles = 2;
    const quint32 tableEnd = kAwFileHeadersStart + numFiles * fileHeaderLength;
    const quint32 payload0 = 0x100;
    const quint32 payload1 = 0x180;

    AwTestImage image;
    image.header = QByteArray(int(kAwHeaderSize), '\0');
    image.header.replace(0, 8, "IMAGEWTY");
    putLe32(image.header, 8, 0x0100);   // header_version
    putLe32(image.header, 12, 1024);    // header_size
    putLe32(image.header, 20, 1);       // format_version
    putLe32(image.header, 24, tableEnd + payload0 + payload1);
    putLe32(image.header, 32, fileHeaderLength);
    putLe32(image.header, 36, 0x1234);  // usb pid
    putLe32(image.header, 40, 0x5678);  // usb vid
    putLe32(image.header, 44, 0x0A0B0C0D);
    putLe32(image.header, 48, 0x11223344);
    putLe32(image.header, 60, numFiles);

    image.table = QByteArray(int(numFiles * fileHeaderLength), '\0');
    // entry 0: boot.fex (payload0 bytes at tableEnd)
    putLe32(image.table, 0, 11);        // filename_length ("boot.fex\0")
    putLe32(image.table, 4, fileHeaderLength);
    putName(image.table, 8, 8, "BOOT");
    putName(image.table, 16, 16, "RFSFAT16");
    putName(image.table, 36, 256, "boot.fex");
    putLe32(image.table, 292, payload0);   // stored_length
    putLe32(image.table, 300, payload0);   // original_length
    putLe32(image.table, 308, tableEnd);   // offset
    // entry 1: rootfs.fex (payload1 bytes after boot)
    const int second = int(fileHeaderLength);
    putLe32(image.table, second, 12);
    putLe32(image.table, second + 4, fileHeaderLength);
    putName(image.table, second + 8, 8, "RFSFAT16");
    putName(image.table, second + 16, 16, "ROOTFS");
    putName(image.table, second + 36, 256, "rootfs.fex");
    putLe32(image.table, second + 292, payload1);
    putLe32(image.table, second + 300, payload1);
    putLe32(image.table, second + 308, tableEnd + payload0);

    image.physicalSize = tableEnd + payload0 + payload1;
    return image;
}

} // namespace

class TestAwFirmware : public QObject {
    Q_OBJECT

private slots:
    void parsesImageEwty()
    {
        const AwTestImage image = buildImage();
        const AwImageInfo info = parseAwImage(image.header, image.table, image.physicalSize);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.headerVersion, 0x0100u);
        QCOMPARE(info.usbVendorId, 0x5678u);
        QCOMPARE(info.numFiles, 2u);
        QCOMPARE(info.files.size(), 2);
        QCOMPARE(info.files.at(0).name, QStringLiteral("boot.fex"));
        QCOMPARE(info.files.at(0).mainType, QStringLiteral("BOOT"));
        QCOMPARE(info.files.at(0).offset, kAwFileHeadersStart + 2 * 0x200u);
        QCOMPARE(info.files.at(1).name, QStringLiteral("rootfs.fex"));
        QCOMPARE(info.files.at(1).offset, kAwFileHeadersStart + 2 * 0x200u + 0x100u);
    }

    void rejectsForeignData()
    {
        QByteArray data(1024, '\0');
        data.replace(0, 8, "NOTWTY12");
        QVERIFY(!parseAwHeader(data).ok);
        QVERIFY(!parseAwImage(data, QByteArray(), 1024).ok);
    }

    void rejectsOutOfRangeEntry()
    {
        AwTestImage image = buildImage();
        // Force the first file offset past the physical size.
        putLe32(image.table, 308, image.physicalSize + 0x100);
        const AwImageInfo info = parseAwImage(image.header, image.table, image.physicalSize);
        QVERIFY(!info.ok);
        QVERIFY(!info.error.isEmpty());
    }

    void rejectsUnsafeName()
    {
        AwTestImage image = buildImage();
        putName(image.table, 36, 256, "../evil.fex");
        const AwImageInfo info = parseAwImage(image.header, image.table, image.physicalSize);
        QVERIFY(!info.ok);
    }
};

GF_TEST_CLASS(TestAwFirmware)

#include "test_awfirmware.moc"
