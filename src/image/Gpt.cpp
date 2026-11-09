#include "Gpt.h"

namespace gf::img::gpt {

namespace {

quint32 rd32(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        fail(QStringLiteral("GPT 数据被截断"));
    return quint32(quint8(d.at(at))) | (quint32(quint8(d.at(at + 1))) << 8) |
           (quint32(quint8(d.at(at + 2))) << 16) | (quint32(quint8(d.at(at + 3))) << 24);
}

quint64 rd64(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 8 > d.size())
        fail(QStringLiteral("GPT 数据被截断"));
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v |= quint64(quint8(d.at(at + i))) << (8 * i);
    return v;
}

void wr32(QByteArray& d, quint32 v)
{
    d.append(char(v & 0xFF));
    d.append(char((v >> 8) & 0xFF));
    d.append(char((v >> 16) & 0xFF));
    d.append(char((v >> 24) & 0xFF));
}

void wr64(QByteArray& d, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        d.append(char((v >> (8 * i)) & 0xFF));
}

int entrySectors(const Header& header, quint32 sectorSize)
{
    const qint64 bytes = qint64(header.numEntries) * qint64(header.entrySize);
    return int((bytes + sectorSize - 1) / sectorSize);
}

Header parseHeaderAt(const QByteArray& disk, quint64 lba, quint32 sectorSize,
                     QVector<Entry>* entriesOut, bool* entriesOk)
{
    Header h;
    h.offsetBytes = qint64(lba) * sectorSize;
    if (h.offsetBytes < 0 || h.offsetBytes + kHeaderSize > disk.size())
        return h;
    const QByteArray raw = disk.mid(h.offsetBytes, sectorSize);
    if (raw.left(8) != QByteArray("EFI PART", 8))
        return h;
    h.present = true;
    h.revision = rd32(raw, 8);
    h.headerSize = rd32(raw, 12);
    h.headerCrc = rd32(raw, 16);
    h.myLba = rd64(raw, 24);
    h.alternateLba = rd64(raw, 32);
    h.firstUsableLba = rd64(raw, 40);
    h.lastUsableLba = rd64(raw, 48);
    h.diskGuidRaw = raw.mid(56, 16);
    h.entriesLba = rd64(raw, 72);
    h.numEntries = rd32(raw, 80);
    h.entrySize = rd32(raw, 84);
    h.entriesCrc = rd32(raw, 88);

    if (h.headerSize < quint32(kHeaderSize) || h.headerSize > sectorSize)
        fail(QStringLiteral("GPT 头大小非法：%1").arg(h.headerSize));
    if (h.numEntries == 0 || h.numEntries > 4096)
        fail(QStringLiteral("GPT 分区表项数量非法：%1").arg(h.numEntries));
    if (h.entrySize < quint32(kEntrySize) || h.entrySize % 8 != 0 || h.entrySize > 4096)
        fail(QStringLiteral("GPT 分区表项大小非法：%1").arg(h.entrySize));

    QByteArray headerForCrc = raw.left(qsizetype(h.headerSize));
    headerForCrc.replace(16, 4, QByteArray(4, '\0'));
    h.crcValid = crc32(headerForCrc) == h.headerCrc;

    const qsizetype tableBytes = qsizetype(h.numEntries) * h.entrySize;
    if (entriesOut != nullptr) {
        const qint64 tableOffset = qint64(h.entriesLba) * sectorSize;
        if (tableOffset < 0 || tableOffset + tableBytes > disk.size())
            fail(QStringLiteral("GPT 分区表超出磁盘大小（条目区 LBA %1）").arg(h.entriesLba));
        const QByteArray tableRaw = disk.mid(tableOffset, tableBytes);
        h.entriesCrcValid = crc32(tableRaw) == h.entriesCrc;
        if (entriesOk)
            *entriesOk = h.entriesCrcValid;
        for (quint32 i = 0; i < h.numEntries; ++i) {
            const qsizetype at = qsizetype(i) * h.entrySize;
            Entry e;
            e.typeGuidRaw = tableRaw.mid(at + 0, 16);
            e.uniqueGuidRaw = tableRaw.mid(at + 16, 16);
            e.firstLba = rd64(tableRaw, at + 32);
            e.lastLba = rd64(tableRaw, at + 40);
            e.attributes = rd64(tableRaw, at + 48);
            e.name = utf16LeToString(tableRaw.mid(at + 56, 72));
            entriesOut->append(e);
        }
    }
    return h;
}

bool headerUsable(const Header& h)
{
    return h.present && h.crcValid && h.entriesCrcValid;
}

int countUsed(const QVector<Entry>& entries)
{
    int n = 0;
    for (const auto& e : entries) {
        if (!e.isEmpty())
            ++n;
    }
    return n;
}

QByteArray buildEntries(const QVector<Entry>& entries, const Header& ref)
{
    const qsizetype tableBytes = qsizetype(ref.numEntries) * ref.entrySize;
    QByteArray out(tableBytes, '\0');
    for (qsizetype i = 0; i < entries.size() && i < qsizetype(ref.numEntries); ++i) {
        const Entry& e = entries.at(i);
        const qsizetype at = i * ref.entrySize;
        const QByteArray type = e.typeGuidRaw.leftJustified(16, '\0').left(16);
        const QByteArray unique = e.uniqueGuidRaw.leftJustified(16, '\0').left(16);
        out.replace(at + 0, 16, type);
        out.replace(at + 16, 16, unique);
        QByteArray numbers;
        wr64(numbers, e.firstLba);
        wr64(numbers, e.lastLba);
        wr64(numbers, e.attributes);
        out.replace(at + 32, 24, numbers);
        out.replace(at + 56, 72, stringToUtf16Le(e.name, 36).leftJustified(72, '\0').left(72));
    }
    return out;
}

QByteArray buildHeader(const Header& ref, quint64 myLba, quint64 alternateLba, quint64 entriesLba,
                       quint32 entriesCrc, quint32 sectorSize)
{
    const quint32 headerSize = kHeaderSize;
    QByteArray raw;
    raw.reserve(qsizetype(headerSize));
    raw.append("EFI PART", 8);
    wr32(raw, ref.revision != 0 ? ref.revision : 0x00010000u);
    wr32(raw, headerSize);
    wr32(raw, 0); // CRC 占位
    wr32(raw, 0);
    wr64(raw, myLba);
    wr64(raw, alternateLba);
    wr64(raw, ref.firstUsableLba);
    wr64(raw, ref.lastUsableLba);
    raw.append(ref.diskGuidRaw.leftJustified(16, '\0').left(16));
    wr64(raw, entriesLba);
    wr32(raw, ref.numEntries);
    wr32(raw, ref.entrySize);
    wr32(raw, entriesCrc);
    if (raw.size() != qsizetype(headerSize))
        fail(QStringLiteral("GPT 头长度错误"));
    raw = raw.leftJustified(sectorSize, '\0');
    const quint32 crc = crc32(raw.left(qsizetype(headerSize)));
    QByteArray crcBytes;
    wr32(crcBytes, crc);
    raw.replace(16, 4, crcBytes);
    return raw;
}

struct Source {
    Header header;
    QVector<Entry> entries;
    bool fromBackup = false;
};

Source pickSource(const QByteArray& disk, quint32 sectorSize, Table* table)
{
    Source src;
    QVector<Entry> entries;
    bool entriesOk = false;
    Header primary = parseHeaderAt(disk, 1, sectorSize, &entries, &entriesOk);
    if (table != nullptr) {
        table->primary = primary;
        table->primary.crcValid = primary.crcValid && primary.entriesCrcValid;
    }
    if (headerUsable(primary)) {
        src.header = primary;
        src.entries = entries;
        src.fromBackup = false;
        return src;
    }

    entries.clear();
    entriesOk = false;
    const quint64 lastLba = quint64(disk.size()) / sectorSize - 1;
    Header backup = parseHeaderAt(disk, lastLba, sectorSize, &entries, &entriesOk);
    if (table != nullptr) {
        table->backup = backup;
        table->backup.crcValid = backup.crcValid && backup.entriesCrcValid;
    }
    if (headerUsable(backup)) {
        src.header = backup;
        src.entries = entries;
        src.fromBackup = true;
        return src;
    }
    fail(QStringLiteral("主 GPT 与备份 GPT 均无效：无法解析分区表（主头 %1，备份头 %2）")
             .arg(primary.present ? (primary.crcValid ? QStringLiteral("CRC 有效") : QStringLiteral("CRC 无效"))
                                  : QStringLiteral("缺失"))
             .arg(backup.present ? (backup.crcValid ? QStringLiteral("CRC 有效") : QStringLiteral("CRC 无效"))
                                 : QStringLiteral("缺失")));
}

void writeProtectiveMbr(QByteArray& disk, quint32 sectorSize, quint64 diskSectors)
{
    QByteArray mbr(sectorSize, '\0');
    const quint8 chsStart[3] = {0x00, 0x02, 0x00};
    const quint8 chsEnd[3] = {0xFF, 0xFF, 0xFF};
    mbr[446] = char(0);
    mbr[447] = char(chsStart[0]);
    mbr[448] = char(chsStart[1]);
    mbr[449] = char(chsStart[2]);
    mbr[450] = char(0xEE);
    mbr[451] = char(chsEnd[0]);
    mbr[452] = char(chsEnd[1]);
    mbr[453] = char(chsEnd[2]);
    QByteArray lba;
    wr32(lba, 1);
    mbr.replace(454, 4, lba);
    QByteArray count;
    wr32(count, quint32(qMin<quint64>(diskSectors > 0 ? diskSectors - 1 : 0, 0xFFFFFFFFull)));
    mbr.replace(458, 4, count);
    mbr[510] = char(0x55);
    mbr[511] = char(0xAA);
    disk.replace(0, sectorSize, mbr);
}

QStringList rebuild(const QByteArray& disk, quint32 sectorSize, const Source& src, QByteArray& out)
{
    QStringList log;
    out = disk;
    const quint64 diskSectors = quint64(out.size()) / sectorSize;
    if (diskSectors < 3)
        fail(QStringLiteral("磁盘映像过小，无法写入 GPT（%1 扇区）").arg(diskSectors));

    Header ref = src.header;
    if (ref.numEntries == 0 || ref.entrySize < quint32(kEntrySize))
        fail(QStringLiteral("GPT 参考头信息不完整，无法重建"));
    // 参考头可能来自备份（myLba 指向末尾），这里统一按标准布局重建。
    const quint64 entryLba = 2;
    const int entriesSec = entrySectors(ref, sectorSize);
    const quint64 backupEntryLba = diskSectors - 1 - quint64(entriesSec);
    if (backupEntryLba < entryLba + quint64(entriesSec))
        fail(QStringLiteral("磁盘映像过小，无法同时容纳主/备 GPT 分区表"));

    ref.lastUsableLba = qMin<quint64>(ref.lastUsableLba, backupEntryLba - 1);

    const QByteArray entries = buildEntries(src.entries, ref);
    const quint32 entriesCrc = crc32(entries);
    if (entriesCrc != ref.entriesCrc)
        log << QStringLiteral("分区表 CRC 已重新计算：0x%1 -> 0x%2")
                   .arg(ref.entriesCrc, 8, 16, QLatin1Char('0'))
                   .arg(entriesCrc, 8, 16, QLatin1Char('0'));

    const QByteArray primaryHeader =
        buildHeader(ref, 1, diskSectors - 1, entryLba, entriesCrc, sectorSize);
    const QByteArray backupHeader =
        buildHeader(ref, diskSectors - 1, 1, backupEntryLba, entriesCrc, sectorSize);

    out.replace(qint64(entryLba) * sectorSize, entries.size(), entries);
    out.replace(qint64(1) * sectorSize, primaryHeader.size(), primaryHeader);
    out.replace(qint64(backupEntryLba) * sectorSize, entries.size(), entries);
    out.replace(qint64(diskSectors - 1) * sectorSize, backupHeader.size(), backupHeader);

    if (out.size() >= 512) {
        const bool hasMbr = quint8(out.at(510)) == 0x55 && quint8(out.at(511)) == 0xAA;
        if (!hasMbr || quint8(out.at(450)) != 0xEE) {
            writeProtectiveMbr(out, sectorSize, diskSectors);
            log << QStringLiteral("已写入保护性 MBR");
        }
    }
    log << QStringLiteral("已重写主 GPT（头 LBA 1，分区表 LBA %1）").arg(entryLba);
    log << QStringLiteral("已重写备份 GPT（头 LBA %1，分区表 LBA %2）")
               .arg(diskSectors - 1)
               .arg(backupEntryLba);
    return log;
}

} // namespace

int Table::partitionCount() const
{
    return countUsed(entries);
}

QString partitionSummary(const Table& table, int index)
{
    if (index < 0 || index >= table.entries.size())
        return {};
    const Entry& e = table.entries.at(index);
    return QStringLiteral("%1  [%2 ~ %3, %4 扇区, %5]")
        .arg(e.name.isEmpty() ? QStringLiteral("(未命名)") : e.name)
        .arg(e.firstLba)
        .arg(e.lastLba)
        .arg(e.sectorCount())
        .arg(formatBytes(quint64(e.sectorCount()) * table.sectorSize));
}

Table parse(const QByteArray& disk, quint32 sectorSize)
{
    if (sectorSize < 512 || sectorSize > 65536 || (sectorSize & (sectorSize - 1)) != 0)
        fail(QStringLiteral("扇区大小非法：%1（应为 2 的幂且 >= 512）").arg(sectorSize));
    if (disk.size() < qint64(sectorSize) * 3)
        fail(QStringLiteral("磁盘映像太小（%1 字节），无法包含 GPT").arg(disk.size()));

    Table table;
    table.sectorSize = sectorSize;
    table.diskSectors = quint64(disk.size()) / sectorSize;

    const QByteArray mbr = disk.left(qsizetype(sectorSize));
    table.hasMbr = mbr.size() >= 512 && quint8(mbr.at(510)) == 0x55 && quint8(mbr.at(511)) == 0xAA;
    if (table.hasMbr) {
        for (int i = 0; i < 4; ++i) {
            const qsizetype at = 446 + i * 16;
            if (quint8(mbr.at(at + 4)) == 0xEE) {
                table.protectiveMbr = true;
                break;
            }
        }
    } else {
        table.warnings << QStringLiteral("未找到 MBR 签名（0x55AA），可能是纯 GPT 映像");
    }

    Source src = pickSource(disk, sectorSize, &table);
    table.entries = src.entries;
    table.usedBackup = src.fromBackup;
    if (src.fromBackup)
        table.warnings << QStringLiteral("主 GPT 损坏，已使用备份 GPT 解析");

    // 备份副本明细（用于对比显示）。
    if (qint64(table.diskSectors - 1) * sectorSize + kHeaderSize <= disk.size()) {
        QVector<Entry> backupEntries;
        bool backupEntriesOk = false;
        const Header backup = parseHeaderAt(disk, table.diskSectors - 1, sectorSize, &backupEntries,
                                            &backupEntriesOk);
        if (backup.present && backup.crcValid && backup.entriesCrcValid) {
            table.backup = backup;
            table.backupEntries = backupEntries;
            if (!src.fromBackup && countUsed(backupEntries) != countUsed(table.entries))
                table.warnings << QStringLiteral("备份 GPT 的分区表与主 GPT 不一致（主 %1 项，备 %2 项）")
                                      .arg(countUsed(table.entries))
                                      .arg(countUsed(backupEntries));
        }
    }
    return table;
}

int findByName(const Table& table, const QString& name, bool exact)
{
    const QString want = name.trimmed();
    if (want.isEmpty())
        return -1;
    for (int i = 0; i < table.entries.size(); ++i) {
        const Entry& e = table.entries.at(i);
        if (e.isEmpty())
            continue;
        if (exact ? (e.name == want) : e.name.compare(want, Qt::CaseInsensitive) == 0)
            return i;
    }
    if (!exact) {
        for (int i = 0; i < table.entries.size(); ++i) {
            const Entry& e = table.entries.at(i);
            if (!e.isEmpty() && e.name.contains(want, Qt::CaseInsensitive))
                return i;
        }
    }
    return -1;
}

quint64 partitionOffsetBytes(const Table& table, int index)
{
    if (index < 0 || index >= table.entries.size())
        fail(QStringLiteral("分区索引越界：%1").arg(index));
    return quint64(table.entries.at(index).firstLba) * table.sectorSize;
}

QStringList repair(QByteArray& disk, quint32 sectorSize)
{
    if (sectorSize < 512 || (sectorSize & (sectorSize - 1)) != 0)
        fail(QStringLiteral("扇区大小非法：%1").arg(sectorSize));
    Table table;
    Source src = pickSource(disk, sectorSize, &table);
    QByteArray out;
    QStringList log = rebuild(disk, sectorSize, src, out);
    disk = out;
    log.prepend(src.fromBackup ? QStringLiteral("主 GPT 损坏，使用备份重建")
                               : QStringLiteral("以主 GPT 为基准修复"));
    return log;
}

QByteArray exportBackup(const QByteArray& disk, quint32 sectorSize)
{
    Table table = parse(disk, sectorSize);
    const Header& h = table.usedBackup ? table.backup : table.primary;
    if (!h.present)
        fail(QStringLiteral("没有可导出的 GPT 头"));
    const int entriesSec = entrySectors(h, sectorSize);
    const qsizetype bytes = (2 + entriesSec) * qsizetype(sectorSize);
    if (bytes > disk.size())
        fail(QStringLiteral("GPT 备份数据超出磁盘大小"));
    return disk.left(bytes);
}

QStringList importBackup(QByteArray& disk, const QByteArray& backup, quint32 sectorSize)
{
    if (sectorSize < 512 || (sectorSize & (sectorSize - 1)) != 0)
        fail(QStringLiteral("扇区大小非法：%1").arg(sectorSize));
    if (backup.size() < qint64(sectorSize) * 2)
        fail(QStringLiteral("GPT 备份文件太小：%1 字节").arg(backup.size()));

    QVector<Entry> entries;
    bool entriesOk = false;
    const Header header = parseHeaderAt(backup, 1, sectorSize, &entries, &entriesOk);
    if (!header.present)
        fail(QStringLiteral("GPT 备份文件缺少有效头（EFI PART magic）"));
    if (!header.crcValid)
        fail(QStringLiteral("GPT 备份文件头 CRC 校验失败"));
    if (!entriesOk)
        fail(QStringLiteral("GPT 备份文件分区表 CRC 校验失败"));

    Source src;
    src.header = header;
    src.entries = entries;
    src.fromBackup = true;
    QByteArray out;
    QStringList log = rebuild(disk, sectorSize, src, out);
    disk = out;
    log.prepend(QStringLiteral("已从备份文件恢复主 GPT"));
    return log;
}

} // namespace gf::img::gpt
