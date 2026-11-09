#include "PartitionTable.h"

#include "mtk/Scatter.h"
#include "sprd/Pac.h"
#include "usb/ProtocolSupport.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace gf::flows {

namespace {

quint64 parseNumber(const QString& text, bool* ok = nullptr)
{
    QString value = text.trimmed();
    bool valid = false;
    quint64 parsed = 0;
    if (value.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        parsed = value.mid(2).toULongLong(&valid, 16);
    } else {
        parsed = value.toULongLong(&valid, 10);
        if (!valid)
            parsed = value.toULongLong(&valid, 16); // parameter files may omit 0x
    }
    if (ok)
        *ok = valid;
    return parsed;
}

quint32 readU32le(const QByteArray& data, qsizetype offset)
{
    return proto::readU32le(data, offset);
}

quint64 readU64le(const QByteArray& data, qsizetype offset)
{
    return proto::readU64le(data, offset);
}

QString guidString(const QByteArray& raw)
{
    if (raw.size() < 16)
        return {};
    const quint32 data1 = readU32le(raw, 0);
    const quint16 data2 = proto::readU16le(raw, 4);
    const quint16 data3 = proto::readU16le(raw, 6);
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(data1, 8, 16, QLatin1Char('0'))
        .arg(data2, 4, 16, QLatin1Char('0'))
        .arg(data3, 4, 16, QLatin1Char('0'))
        .arg(QString::fromLatin1(raw.mid(8, 2).toHex()), QString::fromLatin1(raw.mid(10, 6).toHex()));
}

QString decodeUtf16Name(const QByteArray& raw)
{
    QString name;
    for (qsizetype i = 0; i + 1 < raw.size(); i += 2) {
        const quint16 code = proto::readU16le(raw, i);
        if (code == 0)
            break;
        name.append(QChar(code));
    }
    return name.trimmed();
}

QString strippedSlotName(QString name)
{
    if (name.size() > 2) {
        const QString suffix = name.right(2);
        if (suffix == QLatin1String("_a") || suffix == QLatin1String("_b"))
            name.chop(2);
    }
    return name;
}

} // namespace

QVariantMap PartitionInfo::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("index"), index);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("fileName"), fileName);
    map.insert(QStringLiteral("type"), type);
    map.insert(QStringLiteral("region"), region);
    map.insert(QStringLiteral("offset"), QVariant::fromValue(offset));
    map.insert(QStringLiteral("size"), QVariant::fromValue(size));
    map.insert(QStringLiteral("offsetHex"), QStringLiteral("0x%1").arg(offset, 0, 16));
    map.insert(QStringLiteral("sizeHex"), QStringLiteral("0x%1").arg(size, 0, 16));
    map.insert(QStringLiteral("isDownload"), isDownload);
    map.insert(QStringLiteral("hasAddress"), hasAddress);
    QString slot;
    if (name.endsWith(QLatin1String("_a")))
        slot = QStringLiteral("a");
    else if (name.endsWith(QLatin1String("_b")))
        slot = QStringLiteral("b");
    map.insert(QStringLiteral("slot"), slot);
    return map;
}

QStringList PartitionTable::names() const
{
    QStringList out;
    out.reserve(partitions.size());
    for (const auto& partition : partitions)
        out.append(partition.name);
    return out;
}

int PartitionTable::indexOf(const QString& name) const
{
    for (int i = 0; i < partitions.size(); ++i) {
        if (partitions[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    }
    const QString base = strippedSlotName(name);
    for (int i = 0; i < partitions.size(); ++i) {
        if (partitions[i].name.compare(base, Qt::CaseInsensitive) == 0)
            return i;
        if (strippedSlotName(partitions[i].name).compare(base, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

const PartitionInfo* PartitionTable::find(const QString& name) const
{
    const int index = indexOf(name);
    return index >= 0 ? &partitions[index] : nullptr;
}

QVariantList PartitionTable::toVariantList() const
{
    QVariantList out;
    out.reserve(partitions.size());
    for (const auto& partition : partitions)
        out.append(partition.toVariant());
    return out;
}

PartitionTable parseGptPrefix(const QByteArray& prefix, const QString& platform,
                              const QString& storageType)
{
    PartitionTable table;
    table.platform = platform;
    table.source = QStringLiteral("gpt");
    table.storageType = storageType;

    constexpr qint32 candidates[] = {512, 4096};
    for (const qint32 sectorSize : candidates) {
        const qsizetype headerOffset = sectorSize;
        if (prefix.size() < headerOffset + 92)
            continue;
        if (!prefix.mid(headerOffset, 8).startsWith(QByteArrayLiteral("EFI PART")))
            continue;

        const quint32 headerSize = readU32le(prefix, headerOffset + 12);
        if (headerSize < 92 || headerSize > 512)
            continue;
        const quint64 entriesLba = readU64le(prefix, headerOffset + 72);
        const quint32 numEntries = readU32le(prefix, headerOffset + 80);
        const quint32 entrySize = readU32le(prefix, headerOffset + 84);
        if (entrySize < 128 || numEntries == 0 || numEntries > 1024)
            continue;

        const quint64 entriesOffset = entriesLba * quint64(sectorSize);
        if (entriesOffset + quint64(numEntries) * entrySize > quint64(prefix.size())) {
            table.error = QStringLiteral("GPT 分区表超出已读取范围");
            return table;
        }

        table.sectorSize = quint32(sectorSize);
        for (quint32 i = 0; i < numEntries; ++i) {
            const qsizetype base = qsizetype(entriesOffset) + qsizetype(i) * entrySize;
            const QByteArray typeGuid = prefix.mid(base, 16);
            if (typeGuid == QByteArray(16, '\0') || typeGuid.isEmpty())
                continue;
            PartitionInfo partition;
            partition.index = int(table.partitions.size());
            partition.name = decodeUtf16Name(prefix.mid(base + 56, 72));
            if (partition.name.isEmpty())
                partition.name = QStringLiteral("partition_%1").arg(partition.index);
            partition.type = guidString(typeGuid);
            const quint64 firstLba = readU64le(prefix, base + 32);
            const quint64 lastLba = readU64le(prefix, base + 40);
            partition.offset = firstLba * quint64(sectorSize);
            partition.size = lastLba >= firstLba ? (lastLba - firstLba + 1) * quint64(sectorSize) : 0;
            partition.hasAddress = true;
            partition.isDownload = true;
            table.partitions.append(partition);
        }

        if (table.partitions.isEmpty()) {
            table.error = QStringLiteral("GPT 中没有有效分区项");
            return table;
        }
        table.valid = true;
        return table;
    }

    table.error = QStringLiteral("未找到 GPT 头（512/4096 字节扇区均不匹配）");
    return table;
}

PartitionTable PartitionTable::fromGptData(const QByteArray& prefix, const QString& platform,
                                           const QString& storageType)
{
    return parseGptPrefix(prefix, platform, storageType);
}

PartitionTable PartitionTable::fromScatter(const mtk::ScatterInfo& scatter, const QString& platform)
{
    PartitionTable table;
    table.platform = platform;
    table.source = QStringLiteral("scatter");
    table.storageType = scatter.storage;
    table.sectorSize = 512;
    if (!scatter.valid) {
        table.error = scatter.error;
        return table;
    }
    for (const auto& source : scatter.partitions) {
        PartitionInfo partition;
        partition.index = int(table.partitions.size());
        partition.name = source.name;
        partition.fileName = source.fileName;
        partition.type = source.type;
        partition.region = source.region;
        partition.offset = source.physicalStart;
        partition.size = source.size;
        partition.isDownload = source.isDownload;
        partition.hasAddress = true;
        table.partitions.append(partition);
    }
    table.valid = !table.partitions.isEmpty();
    if (!table.valid)
        table.error = QStringLiteral("scatter 中没有分区");
    return table;
}

PartitionTable PartitionTable::fromPac(const sprd::PacInfo& pac, const QString& platform)
{
    PartitionTable table;
    table.platform = platform;
    table.source = QStringLiteral("pac");
    table.sectorSize = 512;
    if (!pac.valid) {
        table.error = pac.error;
        return table;
    }
    for (int i = 0; i < pac.images.size(); ++i) {
        const auto& image = pac.images[i];
        PartitionInfo partition;
        partition.index = i;
        partition.name = image.name.isEmpty()
                             ? QStringLiteral("%1_%2").arg(sprd::pacImageTypeName(image.type)).arg(i)
                             : image.name;
        partition.fileName = partition.name;
        partition.type = sprd::pacImageTypeName(image.type);
        partition.size = image.size;
        partition.isDownload = true;
        // PAC image table has no storage address; a partition map must be
        // supplied before write/read operations (see SprdFlow).
        partition.hasAddress = false;
        table.partitions.append(partition);
    }
    table.valid = !table.partitions.isEmpty();
    if (!table.valid)
        table.error = QStringLiteral("PAC 中没有镜像");
    return table;
}

PartitionTable PartitionTable::fromParameterText(const QString& text, const QString& platform)
{
    PartitionTable table;
    table.platform = platform;
    table.source = QStringLiteral("parameter");
    table.sectorSize = 512;
    table.storageType = QStringLiteral("emmc");

    // Rockchip parameter.txt: CMDLINE: mtdparts=rk29xxnand:0x2000@0x4000(uboot),...
    static const QRegularExpression tokenPattern(
        QStringLiteral("([0-9a-fA-Fx]+)@([0-9a-fA-Fx]+)\\(([^)]+)\\)"));
    auto iterator = tokenPattern.globalMatch(text);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        PartitionInfo partition;
        partition.index = int(table.partitions.size());
        partition.size = parseNumber(match.captured(1)) * 512ull;
        partition.offset = parseNumber(match.captured(2)) * 512ull;
        partition.name = match.captured(3).trimmed();
        partition.type = QStringLiteral("rockchip");
        partition.isDownload = true;
        partition.hasAddress = true;
        if (!partition.name.isEmpty())
            table.partitions.append(partition);
    }

    table.valid = !table.partitions.isEmpty();
    if (!table.valid)
        table.error = QStringLiteral("parameter.txt 中没有 mtdparts 分区信息");
    return table;
}

PartitionTable PartitionTable::fromPartitionMapText(const QString& text, const QString& platform,
                                                    quint32 sectorSize)
{
    PartitionTable table;
    table.platform = platform;
    table.source = QStringLiteral("map");
    table.sectorSize = sectorSize;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        QString line = rawLine;
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const int comment = line.indexOf(QLatin1Char('#'));
        if (comment >= 0)
            line = line.left(comment);
        line = line.trimmed();
        if (line.isEmpty())
            continue;

        QStringList parts;
        if (line.contains(QLatin1Char('='))) {
            const int equal = line.indexOf(QLatin1Char('='));
            const QString name = line.left(equal).trimmed();
            const QString rest = line.mid(equal + 1).trimmed();
            const QStringList range = rest.split(QLatin1Char(':'));
            parts << name << (range.size() > 0 ? range[0] : QString())
                  << (range.size() > 1 ? range[1] : QString());
        } else if (line.contains(QLatin1Char(','))) {
            parts = line.split(QLatin1Char(','));
        } else {
            parts = line.split(QRegularExpression(QStringLiteral("\\s+")));
        }
        if (parts.size() < 3)
            continue;

        PartitionInfo partition;
        partition.index = int(table.partitions.size());
        partition.name = parts[0].trimmed();
        partition.offset = parseNumber(parts[1]);
        partition.size = parseNumber(parts[2]);
        partition.type = parts.size() > 3 ? parts[3].trimmed() : QStringLiteral("partition");
        partition.isDownload = true;
        partition.hasAddress = true;
        if (partition.name.isEmpty())
            continue;
        table.partitions.append(partition);
    }

    table.valid = !table.partitions.isEmpty();
    if (!table.valid)
        table.error = QStringLiteral("分区地址表为空（格式：name,offset,size，支持 0x 十六进制）");
    return table;
}

QString timestampForFileName(const QDateTime& now)
{
    return now.toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString backupFileName(const QString& platform, const QString& partition, const QDateTime& now)
{
    QString safePlatform = platform;
    QString safePartition = partition;
    for (QString* text : {&safePlatform, &safePartition}) {
        text->replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
        if (text->isEmpty())
            *text = QStringLiteral("unknown");
    }
    return QStringLiteral("%1_%2_%3.img")
        .arg(safePlatform, safePartition, timestampForFileName(now));
}

} // namespace gf::flows
