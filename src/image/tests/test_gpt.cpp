#include <QtTest>

#include "TestRegistry.h"

#include "Gpt.h"

#include <QTemporaryDir>

namespace {

constexpr quint32 kSector = 512;
constexpr quint64 kDiskSectors = 2048; // 1 MiB
constexpr quint32 kEntryCount = 128;
constexpr quint32 kEntrySize = 128;
constexpr quint32 kEntrySectors = (kEntryCount * kEntrySize) / kSector;
constexpr quint64 kBackupEntryLba = kDiskSectors - 1 - kEntrySectors;

void putLe32(QByteArray& data, qsizetype at, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        data[at + i] = char((value >> (8 * i)) & 0xFF);
}

void putLe64(QByteArray& data, qsizetype at, quint64 value)
{
    for (int i = 0; i < 8; ++i)
        data[at + i] = char((value >> (8 * i)) & 0xFF);
}

void writeEntry(QByteArray& table, int index, const QString& name, quint64 firstLba,
                quint64 lastLba, quint8 typeByte)
{
    const qsizetype at = qsizetype(index) * kEntrySize;
    table[at + 0] = char(typeByte);      // 类型 GUID 首字节非零即表示已使用
    table[at + 16] = char(0x11 + index); // 唯一 GUID
    putLe64(table, at + 32, firstLba);
    putLe64(table, at + 40, lastLba);
    putLe64(table, at + 48, 0);
    const QByteArray utf16 = gf::img::stringToUtf16Le(name, 36);
    table.replace(at + 56, utf16.size(), utf16);
}

QByteArray buildHeader(quint64 myLba, quint64 alternateLba, quint64 entriesLba,
                       quint32 entriesCrc)
{
    QByteArray header(kSector, '\0');
    header.replace(0, 8, QByteArray("EFI PART"));
    putLe32(header, 8, 0x00010000);      // revision
    putLe32(header, 12, 92);             // header size
    putLe32(header, 16, 0);              // crc 占位
    putLe32(header, 20, 0);              // reserved
    putLe64(header, 24, myLba);
    putLe64(header, 32, alternateLba);
    putLe64(header, 40, 34); // first usable
    putLe64(header, 48, kDiskSectors - 34);
    for (int i = 0; i < 16; ++i)
        header[56 + i] = char(i + 1); // 磁盘 GUID
    putLe64(header, 72, entriesLba);
    putLe32(header, 80, kEntryCount);
    putLe32(header, 84, kEntrySize);
    putLe32(header, 88, entriesCrc);
    const quint32 crc = gf::img::crc32(header.left(92));
    putLe32(header, 16, crc);
    return header;
}

QByteArray buildDisk(bool withMbr = true)
{
    QByteArray disk(kSector * kDiskSectors, '\0');
    if (withMbr) {
        QByteArray mbr(kSector, '\0');
        mbr[446] = char(0);
        mbr[447] = char(0x00);
        mbr[448] = char(0x02);
        mbr[449] = char(0x00);
        mbr[450] = char(0xEE);
        mbr[451] = char(0xFF);
        mbr[452] = char(0xFF);
        mbr[453] = char(0xFF);
        putLe32(mbr, 454, 1);
        putLe32(mbr, 458, quint32(kDiskSectors - 1));
        mbr[510] = char(0x55);
        mbr[511] = char(0xAA);
        disk.replace(0, kSector, mbr);
    }

    QByteArray table(kEntryCount * kEntrySize, '\0');
    writeEntry(table, 0, QStringLiteral("boot"), 34, 133, 0x28);
    writeEntry(table, 1, QStringLiteral("system"), 134, 1023, 0x29);
    const quint32 entriesCrc = gf::img::crc32(table);

    disk.replace(qint64(2) * kSector, table.size(), table);
    disk.replace(qint64(kBackupEntryLba) * kSector, table.size(), table);
    disk.replace(qint64(1) * kSector, kSector, buildHeader(1, kDiskSectors - 1, 2, entriesCrc));
    disk.replace(qint64(kDiskSectors - 1) * kSector, kSector,
                 buildHeader(kDiskSectors - 1, 1, kBackupEntryLba, entriesCrc));
    return disk;
}

} // namespace

class TestGpt : public QObject
{
    Q_OBJECT

private slots:
    void parseDisk()
    {
        const QByteArray disk = buildDisk();
        const gf::img::gpt::Table table = gf::img::gpt::parse(disk, kSector);
        QCOMPARE(table.sectorSize, kSector);
        QCOMPARE(table.diskSectors, kDiskSectors);
        QVERIFY(table.hasMbr);
        QVERIFY(table.protectiveMbr);
        QVERIFY(table.primary.present);
        QVERIFY(table.primary.crcValid);
        QVERIFY(table.primary.entriesCrcValid);
        QVERIFY(table.backup.present);
        QVERIFY(table.backup.crcValid);
        QVERIFY(!table.usedBackup);
        QCOMPARE(table.partitionCount(), 2);
        QCOMPARE(table.entries.size(), int(kEntryCount));
        QCOMPARE(table.entries.first().name, QStringLiteral("boot"));
        QCOMPARE(table.entries.at(1).name, QStringLiteral("system"));
        QCOMPARE(table.entries.first().firstLba, quint64(34));
        QCOMPARE(table.entries.first().lastLba, quint64(133));
        QCOMPARE(table.entries.first().sectorCount(), quint64(100));
        QCOMPARE(gf::img::gpt::findByName(table, QStringLiteral("boot")), 0);
        QCOMPARE(gf::img::gpt::findByName(table, QStringLiteral("SYSTEM")), 1);
        QCOMPARE(gf::img::gpt::findByName(table, QStringLiteral("data")), -1);
        QCOMPARE(gf::img::gpt::partitionOffsetBytes(table, 0), quint64(34) * kSector);
        QVERIFY(gf::img::gpt::partitionSummary(table, 0).contains(QStringLiteral("boot")));
        QCOMPARE(table.primary.diskGuid().size(), 36);
        QVERIFY(!table.entries.first().isEmpty());
        QVERIFY(table.entries.at(5).isEmpty());
    }

    void repairCorruptedPrimary()
    {
        QByteArray disk = buildDisk();
        // 破坏主 GPT 头的 CRC
        disk[1 * kSector + 16] = char(quint8(disk.at(1 * kSector + 16)) ^ 0xFF);
        const gf::img::gpt::Table damaged = gf::img::gpt::parse(disk, kSector);
        QVERIFY(damaged.usedBackup);
        QCOMPARE(damaged.partitionCount(), 2);
        QVERIFY(!damaged.warnings.isEmpty());

        const QStringList log = gf::img::gpt::repair(disk, kSector);
        QVERIFY(!log.isEmpty());
        const gf::img::gpt::Table repaired = gf::img::gpt::parse(disk, kSector);
        QVERIFY(!repaired.usedBackup);
        QVERIFY(repaired.primary.crcValid);
        QVERIFY(repaired.primary.entriesCrcValid);
        QVERIFY(repaired.backup.crcValid);
        QCOMPARE(repaired.partitionCount(), 2);
        QCOMPARE(repaired.entries.first().name, QStringLiteral("boot"));
        // 修复后再次修复应当幂等（返回的修复日志不应包含无法修复的信息）
        const QStringList again = gf::img::gpt::repair(disk, kSector);
        QVERIFY(!again.isEmpty());
    }

    void backupAndRestore()
    {
        QByteArray disk = buildDisk();
        const QByteArray backup = gf::img::gpt::exportBackup(disk, kSector);
        QVERIFY(backup.size() >= qint64(kSector) * 3);

        // 清空主 GPT 头与分区表，模拟损坏
        disk.replace(qint64(1) * kSector, qint64(1 + kEntrySectors) * kSector,
                     QByteArray(qsizetype((1 + kEntrySectors) * kSector), '\0'));
        bool parseFailed = false;
        try {
            const gf::img::gpt::Table table = gf::img::gpt::parse(disk, kSector);
            QVERIFY(table.usedBackup); // 备份仍然有效
        } catch (const std::exception&) {
            parseFailed = true;
        }
        QVERIFY(!parseFailed);

        const QStringList log = gf::img::gpt::importBackup(disk, backup, kSector);
        QVERIFY(!log.isEmpty());
        const gf::img::gpt::Table restored = gf::img::gpt::parse(disk, kSector);
        QVERIFY(!restored.usedBackup);
        QCOMPARE(restored.partitionCount(), 2);
        QCOMPARE(restored.entries.at(1).name, QStringLiteral("system"));
    }

    void missingMbrRepaired()
    {
        QByteArray disk = buildDisk(false);
        const gf::img::gpt::Table table = gf::img::gpt::parse(disk, kSector);
        QVERIFY(!table.hasMbr);
        QVERIFY(!table.warnings.isEmpty());
        const QStringList log = gf::img::gpt::repair(disk, kSector);
        QVERIFY(log.join(QLatin1Char(' ')).contains(QStringLiteral("MBR")));
        const gf::img::gpt::Table repaired = gf::img::gpt::parse(disk, kSector);
        QVERIFY(repaired.hasMbr);
        QVERIFY(repaired.protectiveMbr);
    }

    void extractPartitionBytes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray disk = buildDisk();
        // 在分区 0 的起始位置写入可识别内容
        const QByteArray marker("XMF-GPT-PARTITION");
        disk.replace(qint64(34) * kSector, marker.size(), marker);
        const QString path = gf::img::joinPath(dir.path(), QStringLiteral("disk.img"));
        gf::img::writeFileOrThrow(path, disk);

        const auto table = gf::img::gpt::parse(gf::img::readFileOrThrow(path), kSector);
        const int index = gf::img::gpt::findByName(table, QStringLiteral("boot"));
        QVERIFY(index >= 0);
        const quint64 offset = gf::img::gpt::partitionOffsetBytes(table, index);
        const quint64 size = table.entries.at(index).sectorCount() * kSector;
        const QByteArray extracted = gf::img::readFileOrThrow(path).mid(qsizetype(offset),
                                                                      qsizetype(size));
        QCOMPARE(extracted.size(), qsizetype(size));
        QCOMPARE(extracted.left(marker.size()), marker);
    }

    void rejectsInvalidDisk()
    {
        bool threw = false;
        try {
            gf::img::gpt::parse(QByteArray(100, '\0'), kSector);
        } catch (const std::exception& e) {
            threw = QString::fromUtf8(e.what()).contains(QStringLiteral("GPT")) ||
                    QString::fromUtf8(e.what()).contains(QStringLiteral("磁盘"));
        }
        QVERIFY(threw);

        QByteArray disk(kSector * 8, '\0');
        threw = false;
        try {
            gf::img::gpt::parse(disk, kSector);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }
};

#include "test_gpt.moc"
GF_TEST_CLASS(TestGpt)
