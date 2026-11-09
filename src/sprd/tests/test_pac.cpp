#include <QtTest>

#include "TestRegistry.h"
#include "sprd/Pac.h"
#include "usb/ProtocolSupport.h"

namespace {

void putU32(QByteArray& data, int offset, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        data[offset + i] = char((value >> (8 * i)) & 0xFF);
}

QByteArray makePac()
{
    const QByteArray fdl1(64, '\x11');
    const QByteArray fdl2(128, '\x22');
    const QByteArray code(96, '\x33');

    const int headerSize = 0x100;
    const int tableOffset = 0x20;
    const int imageCount = 3;
    const int fdl1Offset = headerSize;
    const int fdl2Offset = fdl1Offset + fdl1.size();
    const int codeOffset = fdl2Offset + fdl2.size();
    const int total = codeOffset + code.size();

    QByteArray pac(total, '\0');
    pac[0] = 'P';
    pac[1] = 'A';
    pac[2] = 'C';
    pac[3] = '\0';
    putU32(pac, 0x04, 1);                     // version
    putU32(pac, 0x08, quint32(headerSize));   // header size
    putU32(pac, 0x0C, quint32(imageCount));   // image count
    putU32(pac, 0x10, quint32(tableOffset));  // table offset
    putU32(pac, 0x14, quint32(total));        // total size

    struct Entry {
        quint32 type;
        quint32 offset;
        QByteArray data;
        const char* name;
    };
    const Entry entries[] = {
        {1, quint32(fdl1Offset), fdl1, "FDL1"},
        {2, quint32(fdl2Offset), fdl2, "FDL2"},
        {0, quint32(codeOffset), code, "boot.img"},
    };
    for (int index = 0; index < imageCount; ++index) {
        const qsizetype base = tableOffset + index * int(gf::sprd::kPacImageEntrySize);
        putU32(pac, int(base) + 0x00, entries[index].type);
        putU32(pac, int(base) + 0x04, entries[index].offset);
        putU32(pac, int(base) + 0x08, quint32(entries[index].data.size()));
        putU32(pac, int(base) + 0x0C, 0xDEADBEEF);
        const QByteArray name = QByteArray(entries[index].name);
        for (int i = 0; i < name.size() && i < 32; ++i)
            pac[int(base) + 0x10 + i] = name[i];
    }
    pac.replace(fdl1Offset, fdl1.size(), fdl1);
    pac.replace(fdl2Offset, fdl2.size(), fdl2);
    pac.replace(codeOffset, code.size(), code);
    return pac;
}

} // namespace

class TestSprdPac : public QObject {
    Q_OBJECT

private slots:
    void parseSyntheticPac()
    {
        const QByteArray file = makePac();
        const gf::sprd::PacInfo info = gf::sprd::PacInfo::parse(file);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.version, quint32(1));
        QCOMPARE(info.headerSize, quint32(0x100));
        QCOMPARE(info.totalSize, quint32(file.size()));
        QCOMPARE(info.images.size(), 3);
        QCOMPARE(info.images[0].name, QStringLiteral("FDL1"));
        QCOMPARE(info.images[0].type, quint32(1));
        QCOMPARE(info.images[0].size, quint32(64));
        QCOMPARE(info.images[1].name, QStringLiteral("FDL2"));
        QCOMPARE(info.images[2].name, QStringLiteral("boot.img"));
        QCOMPARE(info.images[2].crc32, quint32(0xDEADBEEF));
        QVERIFY(gf::sprd::describePac(info).contains(QStringLiteral("PAC")));
    }

    void imageDataIsExtracted()
    {
        const QByteArray file = makePac();
        const gf::sprd::PacInfo info = gf::sprd::PacInfo::parse(file);
        QVERIFY(info.valid);
        QString error;
        const QByteArray fdl1 = info.imageData(file, 0, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(fdl1.size(), 64);
        QCOMPARE(fdl1, QByteArray(64, '\x11'));
        const QByteArray boot = info.imageData(file, 2);
        QCOMPARE(boot, QByteArray(96, '\x33'));

        const QByteArray missing = info.imageData(file, 99, &error);
        QVERIFY(missing.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void typeNames()
    {
        QCOMPARE(gf::sprd::pacImageTypeName(0), QStringLiteral("CODE"));
        QCOMPARE(gf::sprd::pacImageTypeName(1), QStringLiteral("FDL1"));
        QCOMPARE(gf::sprd::pacImageTypeName(2), QStringLiteral("FDL2"));
        QVERIFY(gf::sprd::pacImageTypeName(0x77).contains(QStringLiteral("0x77")));
    }

    void rejectWrongMagic()
    {
        QByteArray file(0x100, '\0');
        file[0] = 'X';
        const gf::sprd::PacInfo info = gf::sprd::PacInfo::parse(file);
        QVERIFY(!info.valid);
        QVERIFY(info.error.contains(QStringLiteral("PAC")));
    }

    void rejectTruncatedHeader()
    {
        const gf::sprd::PacInfo info =
            gf::sprd::PacInfo::parse(QByteArrayLiteral("PAC\0\x01\x00"));
        QVERIFY(!info.valid);
    }

    void rejectEmptyTable()
    {
        QByteArray file = makePac();
        putU32(file, 0x0C, 0);
        const gf::sprd::PacInfo info = gf::sprd::PacInfo::parse(file);
        QVERIFY(!info.valid);
        QVERIFY(info.error.contains(QStringLiteral("镜像表")));
    }

    void skipOutOfRangeImage()
    {
        QByteArray file = makePac();
        // Point the first image far outside the file; parser should warn and
        // keep the two valid images.
        putU32(file, 0x20 + 0x04, 0x00FFFFFF);
        const gf::sprd::PacInfo info = gf::sprd::PacInfo::parse(file);
        QVERIFY(info.valid);
        QCOMPARE(info.images.size(), 2);
        QVERIFY(!info.warnings.isEmpty());
    }
};

#include "test_pac.moc"
GF_TEST_CLASS(TestSprdPac)
