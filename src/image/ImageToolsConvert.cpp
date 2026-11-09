// 镜像转换与杂项工具：img↔bin、lzo、gzip、hex/dec、空白块扫描、zip 卡刷包、ELF。
#include "ToolRegistry.h"
#include "TaskManager.h"

#include "BootImage.h"
#include "Compression.h"
#include "ElfFormat.h"
#include "Sparse.h"
#include "ZipPackage.h"

#include "core/Util.h"

#include <QDir>
#include <QProcess>
#include <QFileInfo>
#include <QRegularExpression>

#include <exception>

namespace gf {

namespace {

using gf::img::ProgressFn;
using gf::img::CancelFn;

QString optText(const QVariantMap& options, const QString& key)
{
    return options.value(key).toString().trimmed();
}

bool optBool(const QVariantMap& options, const QString& key, bool def = false)
{
    if (!options.contains(key))
        return def;
    return options.value(key).toBool();
}

int optInt(const QVariantMap& options, const QString& key, int def)
{
    bool ok = false;
    const int value = options.value(key).toInt(&ok);
    return ok ? value : def;
}

QString requireText(const QVariantMap& options, const QString& key, const QString& label)
{
    const QString value = optText(options, key);
    if (value.isEmpty())
        throw std::runtime_error(QStringLiteral("请填写%1").arg(label).toStdString());
    return value;
}

ProgressFn progressOf(TaskContext& ctx)
{
    return [&ctx](int percent, const QString& status) {
        ctx.setProgress(percent);
        if (!status.isEmpty())
            ctx.setStatus(status);
    };
}

CancelFn cancelOf(TaskContext& ctx)
{
    return [&ctx] { ctx.checkCanceled(); };
}

QVariantMap pathField(const QString& key, const QString& label,
                      const QString& filter = QStringLiteral("所有文件 (*)"))
{
    return fields::path(key, label, filter);
}

void copyWithSparseUnwrap(const QString& input, const QString& output, TaskContext& ctx,
                          const QString& fromText, const QString& toText)
{
    const QByteArray head = [&] {
        img::FileReader reader(input);
        return reader.readAt(0, qMin<qint64>(28, reader.size()));
    }();
    if (img::sparse::isSparse(head)) {
        ctx.info(QStringLiteral("检测到 sparse 镜像，先解包为 raw 再输出"));
        img::sparse::unpackFile(input, output, progressOf(ctx), cancelOf(ctx));
        ctx.ok(QStringLiteral("%1 -> %2 完成（sparse 已解包）：%3").arg(fromText, toText, output));
        return;
    }
    img::FileReader reader(input);
    img::FileWriter writer(output, true);
    const qint64 size = reader.size();
    qint64 done = 0;
    while (done < size) {
        ctx.checkCanceled();
        const qint64 chunk = qMin<qint64>(4 * 1024 * 1024, size - done);
        writer.write(reader.readAt(done, chunk));
        done += chunk;
        ctx.setProgress(int(100.0 * double(done) / double(qMax<qint64>(1, size))));
    }
    writer.close();
    ctx.ok(QStringLiteral("%1 -> %2 完成：%3（%4）")
               .arg(fromText, toText, output, img::formatBytes(quint64(size))));
}

void imgToBin(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("输入 img 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出 bin 路径"));
    copyWithSparseUnwrap(input, output, ctx, QStringLiteral("img"), QStringLiteral("bin"));
}

void binToImg(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("输入 bin 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出 img 路径"));
    copyWithSparseUnwrap(input, output, ctx, QStringLiteral("bin"), QStringLiteral("img"));
}

void compressToLzo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("输入镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出 .lzo 路径"));
    const QByteArray data = img::readFileOrThrow(input);
    ctx.info(QStringLiteral("LZO1X 字面量编码 %1 字节（不减小体积，仅供兼容工具链）").arg(data.size()));
    const QByteArray out = img::compress::lzopCompress(data, QFileInfo(input).fileName());
    img::writeFileOrThrow(output, out);
    ctx.ok(QStringLiteral("img -> lzo 完成：%1（%2）")
               .arg(output, img::formatBytes(quint64(out.size()))));
}

void decompressLzo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("输入 .lzo 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出镜像路径"));
    const QByteArray data = img::readFileOrThrow(input);
    ctx.setProgress(20);
    const QByteArray out = img::compress::lzopDecompress(data);
    img::writeFileOrThrow(output, out);
    ctx.ok(QStringLiteral("lzo -> img 完成：%1（%2）")
               .arg(output, img::formatBytes(quint64(out.size()))));
}

void compressToGzip(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("输入镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出 .gz 路径"));
    const QByteArray data = img::readFileOrThrow(input);
    ctx.setProgress(20);
    const QByteArray out = img::compress::gzipCompress(data);
    img::writeFileOrThrow(output, out);
    const double ratio = data.isEmpty() ? 0.0 : 100.0 * double(out.size()) / double(data.size());
    ctx.info(QStringLiteral("压缩率 %1%").arg(ratio, 0, 'f', 1));
    ctx.ok(QStringLiteral("img -> gz 完成：%1（%2 -> %3）")
               .arg(output, img::formatBytes(quint64(data.size())),
                    img::formatBytes(quint64(out.size()))));
}

void decompressGzip(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("输入 .gz 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出镜像路径"));
    const QByteArray data = img::readFileOrThrow(input);
    ctx.setProgress(20);
    const QByteArray out = img::compress::gzipDecompress(data);
    img::writeFileOrThrow(output, out);
    ctx.ok(QStringLiteral("gz -> img 完成：%1（%2）")
               .arg(output, img::formatBytes(quint64(out.size()))));
}

void numberConvert(TaskContext& ctx, const QVariantMap& options)
{
    const QString text = requireText(options, QStringLiteral("value"), QStringLiteral("数值"));
    bool ok = false;
    const quint64 value = img::parseNumber(text, &ok);
    if (!ok)
        throw std::runtime_error("数值格式无法识别（支持十进制、0x 十六进制、K/M/G 后缀）");
    ctx.info(QStringLiteral("输入：%1").arg(text));
    ctx.info(QStringLiteral("十六进制：0x%1").arg(QString::number(value, 16).toUpper()));
    ctx.info(QStringLiteral("十进制：%1").arg(value));
    ctx.info(QStringLiteral("字节：%1").arg(img::formatBytes(value)));
    ctx.info(QStringLiteral("扇区（512B）：%1；块（4K）：%2").arg(value / 512).arg(value / 4096));
    ctx.info(QStringLiteral("小端十六进制字节：%1").arg([&] {
        QByteArray bytes;
        for (int i = 0; i < 8; ++i)
            bytes.append(char((value >> (8 * i)) & 0xFF));
        return QString::fromLatin1(bytes.toHex(' '));
    }()));
    ctx.ok(QStringLiteral("%1 = %2（0x%3）")
               .arg(text, QString::number(value), QString::number(value, 16).toUpper()));
}

void blankScan(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("镜像路径"));
    int blockSize = optInt(options, QStringLiteral("blockSize"), 4096);
    if (blockSize < 512 || blockSize > 1024 * 1024)
        throw std::runtime_error("块大小必须在 512 ~ 1048576 之间");
    const qint64 maxRanges = optInt(options, QStringLiteral("maxRanges"), 50);
    img::FileReader reader(input);
    const qint64 size = reader.size();
    const qint64 totalBlocks = (size + blockSize - 1) / blockSize;
    qint64 zeroBlocks = 0;
    qint64 rangeStart = -1;
    int rangeCount = 0;
    QVector<QPair<qint64, qint64>> ranges;
    for (qint64 block = 0; block < totalBlocks; ++block) {
        ctx.checkCanceled();
        const QByteArray data = reader.readAt(block * blockSize,
                                             qMin<qint64>(blockSize, size - block * blockSize));
        const bool zero = img::isZero(data);
        if (zero) {
            ++zeroBlocks;
            if (rangeStart < 0)
                rangeStart = block;
        } else if (rangeStart >= 0) {
            ++rangeCount;
            if (ranges.size() < qMax<qint64>(1, maxRanges))
                ranges.append({rangeStart, block - rangeStart});
            rangeStart = -1;
        }
        if ((block & 0xFFF) == 0)
            ctx.setProgress(int(100.0 * double(block) / double(qMax<qint64>(1, totalBlocks))));
    }
    if (rangeStart >= 0) {
        ++rangeCount;
        if (ranges.size() < qMax<qint64>(1, maxRanges))
            ranges.append({rangeStart, totalBlocks - rangeStart});
    }
    ctx.info(QStringLiteral("块大小 %1，总块数 %2，全零块 %3（%4），空白区间 %5 个")
                 .arg(blockSize)
                 .arg(totalBlocks)
                 .arg(zeroBlocks)
                 .arg(img::formatBytes(quint64(zeroBlocks) * quint64(blockSize)))
                 .arg(rangeCount));
    for (const auto& range : ranges) {
        ctx.info(QStringLiteral("  空白区间：块 %1 起 %2 块（偏移 %3，大小 %4）")
                     .arg(range.first)
                     .arg(range.second)
                     .arg(img::formatBytes(quint64(range.first) * quint64(blockSize)),
                          img::formatBytes(quint64(range.second) * quint64(blockSize))));
    }
    ctx.ok(QStringLiteral("空白块扫描完成：%1 / %2 块为空")
               .arg(zeroBlocks)
               .arg(totalBlocks));
}

void zipInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("zip 卡刷包路径"));
    const img::zip::Archive archive = img::zip::open(input);
    ctx.info(QStringLiteral("zip 卡刷包：%1，文件大小 %2，条目 %3 个")
                 .arg(QFileInfo(input).fileName(), img::formatBytes(quint64(archive.fileSize)))
                 .arg(archive.entries.size()));
    int images = 0;
    for (const auto& entry : archive.entries) {
        ctx.checkCanceled();
        if (entry.isDirectory())
            continue;
        ctx.info(QStringLiteral("  %1  %2  %3")
                     .arg(entry.name, entry.methodText(), entry.sizeText()));
        if (entry.isImage())
            ++images;
    }
    ctx.info(QStringLiteral("镜像类文件 %1 个").arg(images));
    const QByteArray script =
        img::zip::readEntryByName(archive, QStringLiteral("META-INF/com/google/android/updater-script"));
    if (script.isEmpty()) {
        ctx.warn(QStringLiteral("未找到 updater-script（可能是 fastboot 包或新式 OTA）"));
    } else {
        const img::zip::UpdaterInfo updater = img::zip::parseUpdaterScript(script);
        for (const auto& line : img::zip::updaterSummary(updater))
            ctx.info(line);
    }
    ctx.ok(QStringLiteral("卡刷包解析完成：%1").arg(input));
}

void zipExtract(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("zip 卡刷包路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出目录"));
    const QString filter = optText(options, QStringLiteral("filter"));
    const img::zip::Archive archive = img::zip::open(input);
    img::ensureParentDirectory(img::joinPath(output, QStringLiteral("x")));
    int extracted = 0;
    int index = 0;
    for (const auto& entry : archive.entries) {
        ctx.checkCanceled();
        ++index;
        ctx.setProgress(int(100.0 * index / qMax<qsizetype>(1, archive.entries.size())));
        if (entry.isDirectory())
            continue;
        if (!filter.isEmpty() && !entry.name.contains(filter, Qt::CaseInsensitive))
            continue;
        QString safeName = entry.name;
        safeName.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (safeName.contains(QStringLiteral("..")) || safeName.startsWith(QLatin1Char('/')))
            throw std::runtime_error(QStringLiteral("zip 条目路径非法：%1").arg(entry.name).toStdString());
        const QString target = img::joinPath(output, safeName);
        ctx.setStatus(QStringLiteral("解压 %1").arg(entry.name));
        img::writeFileOrThrow(target, img::zip::readEntry(archive, entry));
        ++extracted;
    }
    ctx.ok(QStringLiteral("解压完成：%1 个文件 -> %2").arg(extracted).arg(output));
}

void zipVerify(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("zip 卡刷包路径"));
    const img::zip::Archive archive = img::zip::open(input);
    ctx.info(QStringLiteral("开始校验 %1 个条目").arg(archive.entries.size()));
    const QStringList problems = img::zip::verify(archive, cancelOf(ctx));
    if (!problems.isEmpty()) {
        for (const auto& problem : problems)
            ctx.error(problem);
        throw std::runtime_error(QStringLiteral("卡刷包校验失败：%1 个条目有问题")
                                     .arg(problems.size())
                                     .toStdString());
    }
    ctx.ok(QStringLiteral("卡刷包 CRC 校验全部通过（%1 个条目）").arg(archive.entries.size()));
}

void elfInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("ELF 文件路径"));
    const QByteArray data = img::readFileOrThrow(input);
    if (!img::elf::isElf(data))
        throw std::runtime_error("不是 ELF 文件（magic 不匹配）");
    const img::elf::Info info = img::elf::parse(data);
    for (const auto& line : img::elf::describe(info))
        ctx.info(line);
    ctx.ok(QStringLiteral("ELF 解析完成：%1，%2 个段，%3 个节，%4 个符号")
               .arg(info.machineText())
               .arg(info.segments.size())
               .arg(info.sections.size())
               .arg(info.symbols.size()));
}

void kernelSymbols(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("ELF 内核 / 镜像路径"));
    const QString filter = optText(options, QStringLiteral("filter")).toLower();
    const QString output = optText(options, QStringLiteral("output"));
    const QVector<img::elf::Symbol> symbols = img::elf::readSymbols(input);
    if (symbols.isEmpty())
        throw std::runtime_error("该 ELF 没有符号表（可能已被 strip）");
    QStringList lines;
    for (const auto& symbol : symbols) {
        if (symbol.name.isEmpty())
            continue;
        if (!filter.isEmpty() && !symbol.name.toLower().contains(filter))
            continue;
        lines << QStringLiteral("0x%1 %2 %3 %4")
                     .arg(symbol.value, 16, 16, QLatin1Char('0'))
                     .arg(symbol.bindText(), symbol.typeText(), symbol.name);
    }
    ctx.info(QStringLiteral("共 %1 个符号，匹配 %2 个").arg(symbols.size()).arg(lines.size()));
    for (const auto& line : lines.mid(0, 300))
        ctx.info(line);
    if (lines.size() > 300)
        ctx.info(QStringLiteral("…（仅显示前 300 条，完整列表请保存到文件）"));
    if (!output.isEmpty()) {
        img::writeFileOrThrow(output, lines.join(QLatin1Char('\n')).toUtf8() + QByteArray("\n"));
        ctx.info(QStringLiteral("符号表已保存：%1").arg(output));
    }
    ctx.ok(QStringLiteral("内核符号表读取完成"));
}

QString typeNameFromHead(const QByteArray& head)
{
    if (img::sparse::isSparse(head))
        return QStringLiteral("Android sparse image");
    if (head.left(4) == QByteArray("CrAU"))
        return QStringLiteral("ChromeOS/Android payload（payload.bin）");
    if (head.left(8) == QByteArray("ANDROID!"))
        return QStringLiteral("Android boot image（v0-v4）");
    if (head.left(8) == QByteArray("VNDRBOOT"))
        return QStringLiteral("Android vendor_boot image");
    if (head.size() > 4 && quint32(quint8(head.at(0))) == 0x67 && quint8(head.at(1)) == 0x44 &&
        quint8(head.at(2)) == 0x6C && quint8(head.at(3)) == 0x61)
        return QStringLiteral("Android super image（lp metadata）");
    if (head.left(4) == QByteArray("AVB0"))
        return QStringLiteral("AVB vbmeta image");
    if (head.size() >= 1024 && head.mid(512, 8) == QByteArray("EFI PART"))
        return QStringLiteral("GPT 磁盘/整盘镜像");
    if (img::boot::isCpio(head))
        return QStringLiteral("cpio newc 归档（ramdisk）");
    if (head.left(2) == QByteArray("PK"))
        return QStringLiteral("zip 压缩包（卡刷包 / bootanimation）");
    if (img::elf::isElf(head))
        return QStringLiteral("ELF 可执行文件");
    if (head.left(3) == QByteArray("BZh"))
        return QStringLiteral("bzip2 压缩数据");
    if (img::compress::isGzip(head))
        return QStringLiteral("gzip 压缩数据");
    if (img::compress::isLzop(head))
        return QStringLiteral("lzop（LZO1X）压缩数据");
    if (head.size() >= 4 && quint8(head.at(0)) == 0x04 && quint8(head.at(1)) == 0x22 &&
        quint8(head.at(2)) == 0x4D && quint8(head.at(3)) == 0x18)
        return QStringLiteral("lz4 压缩数据");
    if (head.size() >= 1082 && quint16(quint8(head.at(1080)) | (quint8(head.at(1081)) << 8)) == 0xEF53)
        return QStringLiteral("ext4 文件系统镜像");
    return QStringLiteral("未知");
}

void detectImage(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("镜像路径"));
    img::FileReader reader(input);
    const QByteArray head = reader.readAt(0, qMin<qint64>(4096, reader.size()));
    const QString type = typeNameFromHead(head);
    ctx.info(QStringLiteral("文件：%1（%2）")
                 .arg(QFileInfo(input).fileName(), img::formatBytes(quint64(reader.size()))));
    ctx.info(QStringLiteral("识别结果：%1").arg(type));
    ctx.info(QStringLiteral("头部 64 字节：\n%1").arg(img::hexDump(head, 0, 64)));
    ctx.ok(QStringLiteral("镜像类型识别完成"));
}

void batchScan(TaskContext& ctx, const QVariantMap& options)
{
    const QString source = requireText(options, QStringLiteral("source"), QStringLiteral("镜像目录"));
    const QString output = optText(options, QStringLiteral("output"));
    const bool copyFiles = optBool(options, QStringLiteral("copy"), false);
    const bool withHash = optBool(options, QStringLiteral("hash"), true);
    const QStringList filters = {QStringLiteral("*.img"),  QStringLiteral("*.bin"),
                                 QStringLiteral("*.mbn"),  QStringLiteral("*.elf"),
                                 QStringLiteral("*.pac"),  QStringLiteral("*.gz"),
                                 QStringLiteral("*.lzo"),  QStringLiteral("*.zip"),
                                 QStringLiteral("*.tar.md5")};
    QDir dir(source);
    if (!dir.exists())
        throw std::runtime_error(QStringLiteral("目录不存在：%1").arg(source).toStdString());
    const QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty())
        throw std::runtime_error("目录中没有找到镜像文件");

    if (copyFiles) {
        if (output.isEmpty())
            throw std::runtime_error("勾选导出时必须填写输出目录");
        img::ensureParentDirectory(img::joinPath(output, QStringLiteral("x")));
    }
    QStringList manifest;
    manifest << QStringLiteral("# XMF 镜像清单（类型 / 大小 / SHA256 / 文件名）");
    int index = 0;
    for (const QFileInfo& info : files) {
        ctx.checkCanceled();
        ++index;
        ctx.setProgress(int(100.0 * index / files.size()));
        ctx.setStatus(QStringLiteral("扫描 %1").arg(info.fileName()));
        const QByteArray head = [&] {
            img::FileReader reader(info.absoluteFilePath());
            return reader.readAt(0, qMin<qint64>(4096, reader.size()));
        }();
        const QString type = typeNameFromHead(head);
        QString hash = QStringLiteral("-");
        if (withHash)
            hash = gf::util::sha256File(info.absoluteFilePath());
        ctx.info(QStringLiteral("%1：%2，%3，sha256=%4")
                     .arg(info.fileName(), type, img::formatBytes(quint64(info.size())), hash));
        manifest << QStringLiteral("%1\t%2\t%3\t%4")
                        .arg(type)
                        .arg(info.size())
                        .arg(hash)
                        .arg(info.fileName());
        if (copyFiles) {
            const QString target = img::joinPath(output, info.fileName());
            QFile::remove(target);
            if (!QFile::copy(info.absoluteFilePath(), target))
                throw std::runtime_error(QStringLiteral("复制文件失败：%1").arg(info.fileName()).toStdString());
        }
    }
    if (copyFiles)
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("xmf_image_manifest.txt")),
                              manifest.join(QLatin1Char('\n')).toUtf8() + QByteArray("\n"));
    ctx.ok(QStringLiteral("批量扫描完成：%1 个文件%2")
               .arg(files.size())
               .arg(copyFiles ? QStringLiteral("，已导出到 ") + output : QString()));
}

void bootAnimationExtract(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("bootanimation.zip 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出目录"));
    const img::zip::Archive archive = img::zip::open(input);
    img::ensureParentDirectory(img::joinPath(output, QStringLiteral("x")));
    const QByteArray desc = img::zip::readEntryByName(archive, QStringLiteral("desc.txt"));
    if (desc.isEmpty())
        throw std::runtime_error("不是有效的 bootanimation.zip（缺少 desc.txt）");
    ctx.info(QStringLiteral("desc.txt：\n%1").arg(QString::fromUtf8(desc)));
    const QStringList descLines = QString::fromUtf8(desc).split(QLatin1Char('\n'));
    QStringList frames;
    int index = 0;
    for (const auto& entry : archive.entries) {
        ctx.checkCanceled();
        ++index;
        ctx.setProgress(int(100.0 * index / qMax<qsizetype>(1, archive.entries.size())));
        if (entry.isDirectory() || entry.name == QStringLiteral("desc.txt"))
            continue;
        const QFileInfo info(entry.name);
        const QString suffix = info.suffix().toLower();
        if (suffix != QStringLiteral("png") && suffix != QStringLiteral("jpg") &&
            suffix != QStringLiteral("jpeg") && suffix != QStringLiteral("webp"))
            continue;
        const QString target = img::joinPath(output, entry.name);
        img::writeFileOrThrow(target, img::zip::readEntry(archive, entry));
        frames << entry.name;
    }
    QStringList summary;
    summary << QStringLiteral("提取动画帧 %1 张").arg(frames.size());
    for (const auto& line : descLines) {
        if (!line.trimmed().isEmpty())
            summary << QStringLiteral("desc: %1").arg(line.trimmed());
    }
    img::writeFileOrThrow(img::joinPath(output, QStringLiteral("desc.txt")),
                          summary.join(QLatin1Char('\n')).toUtf8() + QByteArray("\n"));
    ctx.ok(QStringLiteral("开机动画已提取：%1 帧 -> %2").arg(frames.size()).arg(output));
}


QString zstdExecutable()
{
    const QString bundled = util::dataDir() + QStringLiteral("/thirdparty/oss/zstd/zstd.exe");
    if (QFileInfo::exists(bundled))
        return bundled;
    return util::findExecutable(QStringLiteral("zstd"));
}

void zstdDecompressTool(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = optText(options, QStringLiteral("input"));
    QString output = optText(options, QStringLiteral("output"));
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择 .zst / .zstd 压缩镜像");
    if (!input.endsWith(QStringLiteral(".zst"), Qt::CaseInsensitive)
        && !input.endsWith(QStringLiteral(".zstd"), Qt::CaseInsensitive))
        throw std::runtime_error("输入文件不是 .zst / .zstd 格式");
    if (output.isEmpty()) {
        output = input;
        const int dot = output.lastIndexOf(QLatin1Char('.'));
        if (dot > 0)
            output = output.left(dot);
        else
            output += QStringLiteral(".img");
    }
    if (!util::ensureDir(QFileInfo(output).absolutePath()))
        throw std::runtime_error(QStringLiteral("无法创建输出目录：%1").arg(QFileInfo(output).absolutePath()).toStdString());

    const QString zstd = zstdExecutable();
    if (zstd.isEmpty())
        throw std::runtime_error("未找到 zstd 可执行文件（data/thirdparty/oss/zstd/zstd.exe）");

    ctx.setStatus(QStringLiteral("zstd 解压中…"));
    ctx.info(QStringLiteral("输入：%1").arg(QFileInfo(input).fileName()));
    ctx.info(QStringLiteral("输出：%1").arg(output));

    // Stream to the output file: boot/super images can be multi-gigabyte, so we
    // must not capture stdout in memory. stderr stays a pipe for diagnostics.
    QProcess process;
    process.setProgram(zstd);
    process.setArguments({QStringLiteral("-d"), QStringLiteral("-f"), QStringLiteral("-c"),
                          QStringLiteral("--"), QDir::toNativeSeparators(input)});
    process.setStandardOutputFile(output);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000))
        throw std::runtime_error("无法启动 zstd 进程");
    QByteArray stderrData;
    while (!process.waitForFinished(200)) {
        if (process.state() == QProcess::NotRunning)
            break;
        stderrData += process.readAllStandardError();
        ctx.setStatus(QStringLiteral("zstd 解压中… %1").arg(util::formatBytes(QFileInfo(output).size())));
    }
    stderrData += process.readAllStandardError();
    const QString stderrText = QString::fromLocal8Bit(stderrData).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QFile::remove(output);
        throw std::runtime_error(QStringLiteral("zstd 解压失败（退出码 %1）：%2")
                                     .arg(process.exitCode())
                                     .arg(stderrText.isEmpty() ? QStringLiteral("无输出") : stderrText)
                                     .toStdString());
    }
    const QFileInfo outInfo(output);
    if (!outInfo.exists() || outInfo.size() == 0)
        throw std::runtime_error("zstd 解压完成但输出文件不存在或为空");
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("解压完成：%1（%2）").arg(outInfo.fileName(), util::formatBytes(outInfo.size())));
}

void appendPlannedConvertTools()

{
    ToolSpec spec;
    spec.id = QStringLiteral("image.convert.zstd");
    spec.category = QStringLiteral("image");
    spec.group = QStringLiteral("convert");
    spec.name = QStringLiteral("镜像 zstd 解压");
    spec.desc = QStringLiteral("用内置 zstd 1.5.6 解压 .zst / .zstd 镜像");
    spec.icon = QStringLiteral("file-archive");
    spec.keywords = {QStringLiteral("zstd"), QStringLiteral("解压")};
    spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("输入镜像"), QStringLiteral("*.zst *.zstd")),
                   fields::path(QStringLiteral("output"), QStringLiteral("输出镜像（可选，默认去掉 .zst）"),
                                QStringLiteral("*.*"), QString(), false)};
    ToolRegistry::instance().add(spec, zstdDecompressTool);
}

} // namespace

void registerImageConvertTools()
{
    using namespace gf::fields;

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.detect");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("镜像类型识别");
        spec.desc = QStringLiteral("根据 magic 判断镜像格式（sparse/payload/boot/super/vbmeta/GPT/ELF…）");
        spec.icon = QStringLiteral("search");
        spec.keywords = {QStringLiteral("识别"), QStringLiteral("magic"), QStringLiteral("格式")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("镜像路径"))};
        spec.order = 90;
        addTool(spec, detectImage);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.batchScan");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("镜像批量扫描/导出");
        spec.desc = QStringLiteral("扫描目录中的镜像：类型、大小、SHA256，可批量导出并生成清单");
        spec.icon = QStringLiteral("list-checks");
        spec.keywords = {QStringLiteral("批量"), QStringLiteral("导入"), QStringLiteral("导出"),
                         QStringLiteral("哈希")};
        spec.fields = {dir(QStringLiteral("source"), QStringLiteral("镜像目录")),
                       dir(QStringLiteral("output"), QStringLiteral("导出目录（可选）"), QString(), false),
                       boolean(QStringLiteral("copy"), QStringLiteral("复制/导出文件"), false),
                       boolean(QStringLiteral("hash"), QStringLiteral("计算 SHA256"), true)};
        spec.order = 99;
        addTool(spec, batchScan);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.img2bin");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("IMG 转 BIN");
        spec.desc = QStringLiteral("输出原始 BIN（sparse 镜像会自动解包为 raw）");
        spec.icon = QStringLiteral("arrow-right");
        spec.keywords = {QStringLiteral("img"), QStringLiteral("bin"), QStringLiteral("转换")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("输入 img")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 bin"))};
        spec.order = 91;
        addTool(spec, imgToBin);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.bin2img");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("BIN 转 IMG");
        spec.desc = QStringLiteral("把原始 BIN 重新输出为 IMG（可再打包为 sparse）");
        spec.icon = QStringLiteral("arrow-left");
        spec.keywords = {QStringLiteral("bin"), QStringLiteral("img"), QStringLiteral("转换")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("输入 bin")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 img"))};
        spec.order = 92;
        addTool(spec, binToImg);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.img2lzo");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("IMG 转 LZO");
        spec.desc = QStringLiteral("生成 lzop 容器（LZO1X 字面量编码，仅供兼容工具链）");
        spec.icon = QStringLiteral("file-archive");
        spec.keywords = {QStringLiteral("lzo"), QStringLiteral("lzop"), QStringLiteral("压缩")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("输入镜像")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 .lzo"))};
        spec.order = 93;
        addTool(spec, compressToLzo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.lzo2img");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("LZO 转 IMG");
        spec.desc = QStringLiteral("解压 lzop 容器（支持标准 LZO1X 压缩块）");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("lzo"), QStringLiteral("解压")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("输入 .lzo")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出镜像"))};
        spec.order = 94;
        addTool(spec, decompressLzo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.img2gz");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("IMG 转 GZ");
        spec.desc = QStringLiteral("gzip 压缩镜像（deflate，含 CRC32 校验）");
        spec.icon = QStringLiteral("download");
        spec.keywords = {QStringLiteral("gzip"), QStringLiteral("gz"), QStringLiteral("压缩")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("输入镜像")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 .gz"))};
        spec.order = 95;
        addTool(spec, compressToGzip);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.gz2img");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("GZ 转 IMG");
        spec.desc = QStringLiteral("解压 gzip 镜像并校验 CRC32 / 长度");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("gzip"), QStringLiteral("解压")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("输入 .gz")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出镜像"))};
        spec.order = 96;
        addTool(spec, decompressGzip);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.convert.number");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("十六进制 / 十进制转换");
        spec.desc = QStringLiteral("数值互转，并换算字节、扇区、4K 块数量");
        spec.icon = QStringLiteral("hash");
        spec.keywords = {QStringLiteral("hex"), QStringLiteral("十进制"), QStringLiteral("转换")};
        spec.fields = {text(QStringLiteral("value"), QStringLiteral("数值"), QString(),
                            QStringLiteral("0x1F000 / 4096 / 2M"), QStringLiteral("支持 0x、K/M/G 后缀"), true)};
        spec.order = 97;
        addTool(spec, numberConvert);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.scan.blank");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("convert");
        spec.name = QStringLiteral("空白块扫描");
        spec.desc = QStringLiteral("扫描镜像中的全零块，列出空白区间并统计占比");
        spec.icon = QStringLiteral("scan-line");
        spec.keywords = {QStringLiteral("空白"), QStringLiteral("零块"), QStringLiteral("扫描")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("镜像路径")),
                       integer(QStringLiteral("blockSize"), QStringLiteral("块大小"), 4096, 512, 1048576),
                       integer(QStringLiteral("maxRanges"), QStringLiteral("最多显示区间数"), 50, 1, 1000)};
        spec.order = 98;
        addTool(spec, blankScan);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.zip.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("zip");
        spec.name = QStringLiteral("卡刷包解析");
        spec.desc = QStringLiteral("解析 zip 卡刷包：条目、镜像文件、updater-script");
        spec.icon = QStringLiteral("package");
        spec.keywords = {QStringLiteral("zip"), QStringLiteral("卡刷"), QStringLiteral("rom")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("zip 卡刷包"), QStringLiteral("zip (*.zip);;所有文件 (*)"))};
        spec.order = 100;
        addTool(spec, zipInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.zip.extract");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("zip");
        spec.name = QStringLiteral("卡刷包解压");
        spec.desc = QStringLiteral("解压 zip 卡刷包（可只解压指定文件名关键字）");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("zip"), QStringLiteral("解压"), QStringLiteral("卡刷")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("zip 卡刷包"), QStringLiteral("zip (*.zip);;所有文件 (*)")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录")),
                       text(QStringLiteral("filter"), QStringLiteral("文件名过滤（留空=全部）"), QString(),
                            QStringLiteral(".img"), QString(), false)};
        spec.order = 101;
        addTool(spec, zipExtract);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.zip.verify");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("zip");
        spec.name = QStringLiteral("卡刷包校验");
        spec.desc = QStringLiteral("逐条目解压并校验 CRC32，报告损坏文件");
        spec.icon = QStringLiteral("check-check");
        spec.keywords = {QStringLiteral("zip"), QStringLiteral("crc"), QStringLiteral("校验")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("zip 卡刷包"), QStringLiteral("zip (*.zip);;所有文件 (*)"))};
        spec.order = 102;
        addTool(spec, zipVerify);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.elf.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("elf");
        spec.name = QStringLiteral("ELF 解析");
        spec.desc = QStringLiteral("解析 ELF32/ELF64：体系结构、段、节与符号");
        spec.icon = QStringLiteral("cpu");
        spec.keywords = {QStringLiteral("elf"), QStringLiteral("内核"), QStringLiteral("da")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("ELF 文件"), QStringLiteral("所有文件 (*)"))};
        spec.order = 110;
        addTool(spec, elfInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.elf.symbols");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("elf");
        spec.name = QStringLiteral("内核符号表");
        spec.desc = QStringLiteral("读取 ELF 符号表，按关键字过滤并可导出文本");
        spec.icon = QStringLiteral("list-checks");
        spec.keywords = {QStringLiteral("符号表"), QStringLiteral("symbols"), QStringLiteral("elf")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("ELF 内核 / 镜像"), QStringLiteral("所有文件 (*)")),
                       text(QStringLiteral("filter"), QStringLiteral("符号名过滤（可选）"), QString(),
                            QStringLiteral("init_"), QString(), false),
                       pathField(QStringLiteral("output"), QStringLiteral("导出文本（可选）"))};
        spec.order = 111;
        addTool(spec, kernelSymbols);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.bootanimation.extract");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("开机动画提取");
        spec.desc = QStringLiteral("解析 bootanimation.zip，导出 desc.txt 与全部动画帧");
        spec.icon = QStringLiteral("sparkles");
        spec.keywords = {QStringLiteral("bootanimation"), QStringLiteral("开机动画"), QStringLiteral("zip")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("bootanimation.zip"), QStringLiteral("zip (*.zip);;所有文件 (*)")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录"))};
        spec.order = 112;
        addTool(spec, bootAnimationExtract);
    }

    appendPlannedConvertTools();
}

} // namespace gf
