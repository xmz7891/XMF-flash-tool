#include "Scatter.h"

#include "Util.h"
#include "usb/ProtocolSupport.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace gf::mtk {

namespace {

struct Block {
    int indent = 0;
    QMap<QString, QString> values;
};

QString cleanValue(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2
        && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

bool parseBool(const QString& text, bool fallback = false)
{
    const QString value = text.trimmed().toLower();
    if (value == QLatin1String("true") || value == QLatin1String("yes")
        || value == QLatin1String("1"))
        return true;
    if (value == QLatin1String("false") || value == QLatin1String("no")
        || value == QLatin1String("0"))
        return false;
    return fallback;
}

quint64 parseNumber(const QString& text, quint64 fallback = 0)
{
    QString value = text.trimmed();
    if (value.isEmpty())
        return fallback;
    bool ok = false;
    const quint64 parsed = value.toULongLong(&ok, 0); // handles 0x prefix
    return ok ? parsed : fallback;
}

int indentOf(const QString& line)
{
    int count = 0;
    while (count < line.size() && (line[count] == QLatin1Char(' ') || line[count] == QLatin1Char('\t')))
        ++count;
    return count;
}

QList<Block> parseBlocks(const QString& text, QStringList* warnings)
{
    QList<Block> blocks;
    Block current;
    bool haveCurrent = false;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        QString line = rawLine;
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;

        const int indent = indentOf(line);
        if (trimmed.startsWith(QLatin1String("- "))) {
            if (haveCurrent)
                blocks.append(current);
            current = Block();
            current.indent = indent;
            haveCurrent = true;
            const QString rest = trimmed.mid(2);
            const int colon = rest.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                current.values.insert(rest.left(colon).trimmed(), cleanValue(rest.mid(colon + 1)));
            } else if (!rest.isEmpty()) {
                current.values.insert(QStringLiteral("_value"), cleanValue(rest));
            }
            continue;
        }

        const int colon = trimmed.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        if (!haveCurrent && warnings)
            warnings->append(QStringLiteral("忽略无法归属的行：%1").arg(trimmed));
        if (haveCurrent)
            current.values.insert(trimmed.left(colon).trimmed(), cleanValue(trimmed.mid(colon + 1)));
    }
    if (haveCurrent)
        blocks.append(current);
    return blocks;
}

QString valueOf(const Block& block, const char* key)
{
    return block.values.value(QString::fromLatin1(key));
}

void applyPartition(ScatterPartition* partition, const Block& block)
{
    partition->index = valueOf(block, "partition_index");
    partition->name = valueOf(block, "partition_name");
    partition->fileName = valueOf(block, "file_name");
    partition->type = valueOf(block, "type");
    partition->region = valueOf(block, "region");
    partition->storage = valueOf(block, "storage");
    partition->operationType = valueOf(block, "operation_type");
    partition->isDownload = parseBool(valueOf(block, "is_download"));
    partition->isReserved = parseBool(valueOf(block, "is_reserved"));
    partition->boundaryCheck = parseBool(valueOf(block, "boundary_check"));
    partition->linearStart = parseNumber(valueOf(block, "linear_start_addr"));
    partition->physicalStart = parseNumber(valueOf(block, "physical_start_addr"));
    partition->size = parseNumber(valueOf(block, "partition_size"));
    partition->reserve = parseNumber(valueOf(block, "reserve"));
}

} // namespace

ScatterInfo ScatterInfo::parse(const QString& text)
{
    ScatterInfo info;
    if (text.trimmed().isEmpty()) {
        info.error = QStringLiteral("scatter 文件为空");
        return info;
    }
    if (text.contains(QChar(u'\0'))) {
        info.error = QStringLiteral("scatter 文件包含二进制数据，可能已加密，无法解析");
        return info;
    }

    const QList<Block> blocks = parseBlocks(text, &info.warnings);
    if (blocks.isEmpty()) {
        info.error = QStringLiteral("未在 scatter 文件中找到任何配置块");
        return info;
    }

    for (const auto& block : blocks) {
        // The general block carries platform/project/storage metadata.
        if (block.values.contains(QStringLiteral("platform"))
            || block.values.contains(QStringLiteral("config_version"))) {
            info.configVersion = valueOf(block, "config_version");
            info.platform = valueOf(block, "platform");
            info.project = valueOf(block, "project");
            info.storage = valueOf(block, "storage");
            info.bootChannel = valueOf(block, "boot_channel");
            info.blockSize = parseNumber(valueOf(block, "block_size"));
        }

        if (!block.values.contains(QStringLiteral("partition_name")))
            continue;
        ScatterPartition partition;
        applyPartition(&partition, block);
        if (partition.name.isEmpty()) {
            info.warnings.append(QStringLiteral("发现没有 partition_name 的分区块，已忽略"));
            continue;
        }
        info.partitions.append(partition);
    }

    if (info.partitions.isEmpty()) {
        info.error = QStringLiteral("scatter 文件没有分区表（未找到 partition_name 字段）");
        return info;
    }

    // Partition sizes are expected to be sector aligned to the scatter block
    // size; only warn so unusual vendor files still parse.
    for (const auto& partition : info.partitions) {
        if (info.blockSize > 0 && partition.size > 0 && partition.size % info.blockSize != 0) {
            info.warnings.append(QStringLiteral("分区 %1 大小 0x%2 未按块大小 0x%3 对齐")
                                     .arg(partition.name)
                                     .arg(partition.size, 0, 16)
                                     .arg(info.blockSize, 0, 16));
        }
    }

    info.valid = true;
    return info;
}

ScatterInfo ScatterInfo::parseFile(const QString& path, QString* ioError)
{
    ScatterInfo info;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("无法打开 scatter 文件：%1").arg(path);
        if (ioError)
            *ioError = info.error;
        return info;
    }
    const QByteArray data = file.readAll();
    if (data.isEmpty()) {
        info.error = QStringLiteral("scatter 文件为空：%1").arg(path);
        if (ioError)
            *ioError = info.error;
        return info;
    }
    if (data.contains('\0')) {
        info.error = QStringLiteral("scatter 文件包含二进制数据，可能已加密，无法解析");
        if (ioError)
            *ioError = info.error;
        return info;
    }
    QString text = QString::fromUtf8(data);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    if (text.contains(QChar(0xFFFD))) {
        // Not valid UTF-8; old scatter files are usually ASCII/GBK. Try the
        // local 8-bit codec as a fallback so Chinese comments do not fail.
        text = QString::fromLocal8Bit(data);
        info.warnings.append(QStringLiteral("文件不是 UTF-8，已按本地编码读取"));
    }
    info = parse(text);
    if (!info.valid && ioError)
        *ioError = info.error;
    return info;
}

bool ScatterInfo::hasPartition(const QString& name) const
{
    for (const auto& partition : partitions) {
        if (partition.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString describeScatter(const ScatterInfo& info)
{
    if (!info.valid)
        return QStringLiteral("Scatter 无效：%1").arg(info.error);
    return QStringLiteral("Scatter：平台 %1，项目 %2，存储 %3，块大小 0x%4，分区 %5 个")
        .arg(info.platform.isEmpty() ? QStringLiteral("?") : info.platform)
        .arg(info.project.isEmpty() ? QStringLiteral("?") : info.project)
        .arg(info.storage.isEmpty() ? QStringLiteral("?") : info.storage)
        .arg(info.blockSize, 0, 16)
        .arg(info.partitions.size());
}

} // namespace gf::mtk
