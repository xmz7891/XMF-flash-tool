// 固件/镜像格式工具：payload、boot、cpio、super、sparse、vbmeta、GPT 等。
#include "ToolRegistry.h"
#include "TaskManager.h"

#include "BootImage.h"
#include "BootLogo.h"
#include "Compression.h"
#include "Cpio.h"
#include "Gpt.h"
#include "Payload.h"
#include "Sparse.h"
#include "SuperImage.h"
#include "Vbmeta.h"

#include "core/Util.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
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

QStringList splitNames(const QString& text)
{
    QStringList out;
    static const QRegularExpression separator(QStringLiteral("[,\\s;]+"));
    for (const QString& part : text.split(separator, Qt::SkipEmptyParts))
        out << part;
    return out;
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

void writeTextFile(const QString& path, const QStringList& lines)
{
    gf::img::writeFileOrThrow(path, lines.join(QLatin1Char('\n')).toUtf8() + QByteArray("\n"));
}

QVariantMap sectorSizeField()
{
    return fields::choice(QStringLiteral("sectorSize"), QStringLiteral("扇区大小"),
                          {QStringLiteral("512"), QStringLiteral("4096")}, 0,
                          QStringLiteral("GPT 默认 512，部分 UFS 硬盘为 4096"));
}

quint32 sectorSizeOf(const QVariantMap& options)
{
    return optInt(options, QStringLiteral("sectorSize"), 0) == 1 ? 4096 : 512;
}

// ---------------------------------------------------------------------------
// payload
// ---------------------------------------------------------------------------

void payloadInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("payload.bin 路径"));
    ctx.info(QStringLiteral("解析 payload：%1").arg(input));
    img::payload::Payload payload = img::payload::parseFile(input);
    ctx.info(QStringLiteral("格式版本 %1，manifest %2 字节，数据起点 %3")
                 .arg(payload.header.version)
                 .arg(payload.header.manifestSize)
                 .arg(payload.header.dataOffset));
    ctx.info(QStringLiteral("块大小 %1，分区 %2 个，minor_version %3")
                 .arg(payload.manifest.blockSize)
                 .arg(payload.manifest.partitions.size())
                 .arg(payload.manifest.minorVersion));
    int percent = 5;
    for (const auto& partition : payload.manifest.partitions) {
        ctx.checkCanceled();
        QHash<QString, int> counts;
        quint64 dataBytes = 0;
        for (const auto& op : partition.operations) {
            counts[img::payload::operationTypeText(op.type)] += 1;
            dataBytes += op.dataLength;
        }
        QStringList summary;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            summary << QStringLiteral("%1 x%2").arg(it.key()).arg(it.value());
        ctx.info(QStringLiteral("%1：%2，%3 个操作（%4），数据 %5")
                     .arg(partition.name,
                          partition.newInfo.size ? img::formatBytes(partition.newInfo.size)
                                                 : QStringLiteral("大小未知"))
                     .arg(partition.operations.size())
                     .arg(summary.join(QStringLiteral(", ")))
                     .arg(img::formatBytes(dataBytes)));
        if (!partition.filesystemType.isEmpty())
            ctx.debug(QStringLiteral("  文件系统类型：%1").arg(partition.filesystemType));
        percent = qMin(95, percent + 5);
        ctx.setProgress(percent);
    }
    ctx.ok(QStringLiteral("payload 解析完成，共 %1 个分区。%2")
               .arg(payload.manifest.partitions.size())
               .arg(img::payload::supportSummary()));
}

void payloadUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("payload.bin 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出目录"));
    const QStringList only = splitNames(optText(options, QStringLiteral("partitions")));
    const bool verify = optBool(options, QStringLiteral("verify"), true);
    ctx.info(QStringLiteral("读取 payload 清单：%1").arg(input));
    img::payload::Payload payload = img::payload::parseFile(input);
    ctx.info(QStringLiteral("共 %1 个分区，开始导出到 %2")
                 .arg(payload.manifest.partitions.size())
                 .arg(output));
    img::payload::extractAll(input, payload, output, only, verify, progressOf(ctx), cancelOf(ctx));
    ctx.ok(QStringLiteral("payload 导出完成：%1").arg(output));
}

void payloadPack(TaskContext& ctx, const QVariantMap& options)
{
    const QString source = requireText(options, QStringLiteral("source"), QStringLiteral("分区镜像目录"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出 payload.bin 路径"));
    QStringList names = splitNames(optText(options, QStringLiteral("partitions")));
    if (names.isEmpty())
        throw std::runtime_error("请填写要打包的分区名（逗号分隔）");
    const int blockSize = optInt(options, QStringLiteral("blockSize"), 4096);
    if (blockSize < 512 || blockSize > 1024 * 1024)
        throw std::runtime_error("块大小必须在 512 ~ 1048576 之间");
    ctx.info(QStringLiteral("打包 %1 个分区为 %2（块大小 %3）")
                 .arg(names.size())
                 .arg(output)
                 .arg(blockSize));
    img::payload::packDirectory(source, names, output, quint32(blockSize), progressOf(ctx),
                                cancelOf(ctx));
    ctx.ok(QStringLiteral("payload 打包完成：%1").arg(output));
}

// ---------------------------------------------------------------------------
// boot / init_boot / vendor_boot
// ---------------------------------------------------------------------------

QString ramdiskFormatName(const QByteArray& ramdisk)
{
    const img::compress::Format format = img::compress::detect(ramdisk);
    return img::compress::formatName(format);
}

void bootInfoImpl(TaskContext& ctx, const QString& input)
{
    const QByteArray data = img::readFileOrThrow(input);
    if (!img::boot::looksLikeBootImage(data))
        throw std::runtime_error("不是 Android boot / init_boot / vendor_boot 镜像（magic 不匹配）");
    const img::boot::Image image = img::boot::parse(data);
    ctx.info(QStringLiteral("类型：%1，头版本：%2，页大小：%3，头部大小：%4")
                 .arg(image.kindText(), image.versionText())
                 .arg(image.pageSize)
                 .arg(image.headerSize));
    if (image.kind == img::boot::ImageKind::VendorBoot) {
        ctx.info(QStringLiteral("内核地址 0x%1，ramdisk 地址 0x%2，tags 地址 0x%3")
                     .arg(image.kernelAddr, 0, 16)
                     .arg(image.ramdiskAddr, 0, 16)
                     .arg(image.tagsAddr, 0, 16));
        ctx.info(QStringLiteral("dtb：%1，bootconfig：%2")
                     .arg(image.dtb.isEmpty() ? QStringLiteral("无") : img::formatBytes(image.dtb.size()),
                          image.bootconfig.isEmpty() ? QStringLiteral("无")
                                                     : img::formatBytes(image.bootconfig.size())));
        ctx.info(QStringLiteral("cmdline：%1").arg(image.cmdline));
        int index = 0;
        for (const auto& fragment : image.vendorRamdisks) {
            ctx.info(QStringLiteral("ramdisk 分片 #%1：类型 %2，名称 %3，大小 %4，格式 %5")
                         .arg(index++)
                         .arg(fragment.type)
                         .arg(fragment.name.isEmpty() ? QStringLiteral("(无)") : fragment.name)
                         .arg(img::formatBytes(fragment.data.size()))
                         .arg(ramdiskFormatName(fragment.data)));
        }
        return;
    }
    ctx.info(QStringLiteral("kernel：%1，ramdisk：%2，second：%3，dtb：%4")
                 .arg(image.kernel.isEmpty() ? QStringLiteral("无") : img::formatBytes(image.kernel.size()),
                      image.ramdisk.isEmpty() ? QStringLiteral("无")
                                              : img::formatBytes(image.ramdisk.size()),
                      image.second.isEmpty() ? QStringLiteral("无") : img::formatBytes(image.second.size()),
                      image.dtb.isEmpty() ? QStringLiteral("无") : img::formatBytes(image.dtb.size())));
    ctx.info(QStringLiteral("kernel 地址 0x%1，ramdisk 地址 0x%2，tags 地址 0x%3，dtb 地址 0x%4")
                 .arg(image.kernelAddr, 0, 16)
                 .arg(image.ramdiskAddr, 0, 16)
                 .arg(image.tagsAddr, 0, 16)
                 .arg(image.dtbAddr, 0, 16));
    if (!image.boardName.isEmpty())
        ctx.info(QStringLiteral("board name：%1").arg(image.boardName));
    if (!image.cmdline.isEmpty())
        ctx.info(QStringLiteral("cmdline：%1").arg(image.cmdline));
    if (!image.extraCmdline.isEmpty())
        ctx.info(QStringLiteral("extra cmdline：%1").arg(image.extraCmdline));
    if (!image.ramdisk.isEmpty())
        ctx.info(QStringLiteral("ramdisk 压缩格式：%1").arg(ramdiskFormatName(image.ramdisk)));
    if (image.headerVersion <= 2)
        ctx.info(QStringLiteral("header id：%1")
                     .arg(QString::fromLatin1(img::boot::computeId(image).toHex())));
    if (!image.signature.isEmpty())
        ctx.info(QStringLiteral("boot 签名：%1").arg(img::formatBytes(image.signature.size())));
}

void bootInfo(TaskContext& ctx, const QVariantMap& options)
{
    bootInfoImpl(ctx, requireText(options, QStringLiteral("input"), QStringLiteral("boot 镜像路径")));
    ctx.ok(QStringLiteral("镜像解析完成"));
}

void bootUnpackImpl(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("boot 镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出目录"));
    const bool decompress = optBool(options, QStringLiteral("decompress"), true);
    const bool extractCpio = optBool(options, QStringLiteral("extractCpio"), true);
    const QByteArray data = img::readFileOrThrow(input);
    if (!img::boot::looksLikeBootImage(data))
        throw std::runtime_error("不是 Android boot / init_boot / vendor_boot 镜像（magic 不匹配）");
    const img::boot::Image image = img::boot::parse(data);
    img::ensureParentDirectory(img::joinPath(output, QStringLiteral("x")));
    ctx.info(QStringLiteral("类型 %1 %2，开始解包到 %3")
                 .arg(image.kindText(), image.versionText(), output));

    if (!image.kernel.isEmpty())
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("kernel")), image.kernel);
    if (!image.second.isEmpty())
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("second")), image.second);
    if (!image.dtb.isEmpty())
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("dtb")), image.dtb);
    if (!image.recoveryDtbo.isEmpty())
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("recovery_dtbo")),
                              image.recoveryDtbo);
    if (!image.signature.isEmpty())
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("boot_signature")), image.signature);
    if (!image.bootconfig.isEmpty())
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("bootconfig")), image.bootconfig);

    ctx.setProgress(30);
    ctx.setStatus(QStringLiteral("写入 ramdisk"));
    if (!image.ramdisk.isEmpty()) {
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("ramdisk.img")), image.ramdisk);
        if (decompress) {
            QString formatName;
            try {
                const QByteArray cpio = img::boot::decompressRamdisk(image.ramdisk, &formatName);
                if (cpio != image.ramdisk) {
                    img::writeFileOrThrow(img::joinPath(output, QStringLiteral("ramdisk.cpio")), cpio);
                    ctx.info(QStringLiteral("ramdisk 压缩格式 %1，已解压为 ramdisk.cpio").arg(formatName));
                    if (extractCpio) {
                        const img::cpio::Archive archive = img::cpio::parse(cpio);
                        ctx.info(QStringLiteral("ramdisk 含 %1 个条目").arg(archive.entries.size()));
                        img::cpio::extractTo(archive, img::joinPath(output, QStringLiteral("ramdisk")),
                                             progressOf(ctx), cancelOf(ctx));
                    }
                }
            } catch (const std::exception& e) {
                ctx.warn(QStringLiteral("解压 ramdisk 失败，仅保留原始 ramdisk.img：%1")
                             .arg(QString::fromUtf8(e.what())));
            }
        }
    }
    int fragmentIndex = 0;
    for (const auto& fragment : image.vendorRamdisks) {
        const QString name = fragment.name.isEmpty()
                                 ? QStringLiteral("vendor_ramdisk_%1.img").arg(fragmentIndex)
                                 : QStringLiteral("vendor_ramdisk_%1_%2.img")
                                       .arg(fragmentIndex)
                                       .arg(fragment.name);
        img::writeFileOrThrow(img::joinPath(output, name), fragment.data);
        ++fragmentIndex;
    }

    QStringList info;
    info << QStringLiteral("类型：%1 %2").arg(image.kindText(), image.versionText());
    info << QStringLiteral("页大小：%1，头部大小：%2").arg(image.pageSize).arg(image.headerSize);
    if (!image.cmdline.isEmpty())
        info << QStringLiteral("cmdline：%1").arg(image.cmdline);
    if (!image.extraCmdline.isEmpty())
        info << QStringLiteral("extra cmdline：%1").arg(image.extraCmdline);
    writeTextFile(img::joinPath(output, QStringLiteral("info.txt")), info);
    writeTextFile(img::joinPath(output, QStringLiteral("cmdline.txt")),
                  {image.cmdline + (image.extraCmdline.isEmpty() ? QString()
                                                                 : QLatin1Char(' ') + image.extraCmdline)});
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("解包完成：%1").arg(output));
}

void bootUnpack(TaskContext& ctx, const QVariantMap& options)
{
    bootUnpackImpl(ctx, options);
}

QByteArray ramdiskFromPath(const QString& path, TaskContext& ctx)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        ctx.info(QStringLiteral("从目录打包 ramdisk：%1").arg(path));
        const img::cpio::Archive archive = img::cpio::fromDirectory(path);
        const QByteArray cpio = img::cpio::serialize(archive);
        return img::compress::gzipCompress(cpio);
    }
    const QByteArray data = img::readFileOrThrow(path);
    if (img::boot::isCpio(data)) {
        ctx.info(QStringLiteral("ramdisk 为未压缩 cpio，自动 gzip 压缩"));
        return img::compress::gzipCompress(data);
    }
    if (img::compress::isGzip(data) || img::compress::isLzop(data)) {
        ctx.info(QStringLiteral("ramdisk 已是压缩格式（%1），保持原样")
                     .arg(ramdiskFormatName(data)));
        return data;
    }
    if (img::compress::detect(data) == img::compress::Format::Lz4)
        throw std::runtime_error("ramdisk 为 lz4 压缩，暂不支持重打包（请先解压为 cpio）");
    ctx.warn(QStringLiteral("ramdisk 文件不是 cpio/gzip/lzop，按原始数据处理"));
    return data;
}

void bootRepackImpl(TaskContext& ctx, const QVariantMap& options, bool initBoot)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("原始 boot 镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出镜像路径"));
    const QByteArray templateData = img::readFileOrThrow(input);
    if (!img::boot::looksLikeBootImage(templateData))
        throw std::runtime_error("原始镜像不是 boot 镜像（magic 不匹配）");
    img::boot::Image image = img::boot::parse(templateData);
    if (initBoot) {
        image.kind = img::boot::ImageKind::InitBoot;
        if (image.headerVersion < 4)
            image.headerVersion = 4;
    }

    const int versionChoice = optInt(options, QStringLiteral("version"), 0);
    if (versionChoice > 0)
        image.headerVersion = quint32(versionChoice - 1);
    if (image.headerVersion > 4)
        throw std::runtime_error("头版本必须是 v0 ~ v4");
    if (image.kind == img::boot::ImageKind::VendorBoot && image.headerVersion < 3)
        throw std::runtime_error("vendor_boot 只支持 v3/v4");

    const int pageSize = optInt(options, QStringLiteral("pageSize"), 0);
    if (pageSize > 0)
        image.pageSize = quint32(pageSize);

    const QString kernelPath = optText(options, QStringLiteral("kernel"));
    if (!kernelPath.isEmpty()) {
        image.kernel = img::readFileOrThrow(kernelPath);
        ctx.info(QStringLiteral("替换 kernel：%1").arg(img::formatBytes(image.kernel.size())));
    }
    const QString secondPath = optText(options, QStringLiteral("second"));
    if (!secondPath.isEmpty())
        image.second = img::readFileOrThrow(secondPath);
    const QString dtbPath = optText(options, QStringLiteral("dtb"));
    if (!dtbPath.isEmpty())
        image.dtb = img::readFileOrThrow(dtbPath);

    const QString ramdiskPath = optText(options, QStringLiteral("ramdisk"));
    if (!ramdiskPath.isEmpty()) {
        image.ramdisk = ramdiskFromPath(ramdiskPath, ctx);
        ctx.info(QStringLiteral("替换 ramdisk：%1").arg(img::formatBytes(image.ramdisk.size())));
    } else if (initBoot) {
        throw std::runtime_error("init_boot 重打包必须提供 ramdisk 文件或目录");
    }

    const QString cmdline = optText(options, QStringLiteral("cmdline"));
    if (!cmdline.isEmpty()) {
        image.cmdline = cmdline;
        image.extraCmdline.clear();
        ctx.info(QStringLiteral("设置 cmdline：%1").arg(cmdline));
    }

    ctx.setProgress(70);
    const QByteArray out = img::boot::serialize(image);
    img::writeFileOrThrow(output, out);
    ctx.info(QStringLiteral("生成 %1 %2，共 %3")
                 .arg(image.kindText(), image.versionText(), img::formatBytes(out.size())));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("重打包完成：%1").arg(output));
}

void bootRepack(TaskContext& ctx, const QVariantMap& options)
{
    bootRepackImpl(ctx, options, false);
}

void initBootUnpack(TaskContext& ctx, const QVariantMap& options)
{
    bootUnpackImpl(ctx, options);
}

void initBootRepack(TaskContext& ctx, const QVariantMap& options)
{
    bootRepackImpl(ctx, options, true);
}

void vendorBootInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("vendor_boot 镜像路径"));
    const QByteArray data = img::readFileOrThrow(input);
    const img::boot::Image image = img::boot::parse(data);
    if (image.kind != img::boot::ImageKind::VendorBoot)
        throw std::runtime_error("这不是 vendor_boot 镜像（magic 不是 VNDRBOOT）");
    bootInfoImpl(ctx, input);
    ctx.ok(QStringLiteral("vendor_boot 解析完成"));
}

void vendorBootUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("vendor_boot 镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出目录"));
    const QByteArray data = img::readFileOrThrow(input);
    const img::boot::Image image = img::boot::parse(data);
    if (image.kind != img::boot::ImageKind::VendorBoot)
        throw std::runtime_error("这不是 vendor_boot 镜像（magic 不是 VNDRBOOT）");
    img::ensureParentDirectory(img::joinPath(output, QStringLiteral("x")));
    if (!image.dtb.isEmpty())
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("dtb")), image.dtb);
    if (!image.bootconfig.isEmpty())
        img::writeFileOrThrow(img::joinPath(output, QStringLiteral("bootconfig")), image.bootconfig);
    int index = 0;
    QStringList info;
    info << QStringLiteral("vendor_boot %1，页大小 %2").arg(image.versionText()).arg(image.pageSize);
    info << QStringLiteral("cmdline：%1").arg(image.cmdline);
    for (const auto& fragment : image.vendorRamdisks) {
        const QString name = fragment.name.isEmpty()
                                 ? QStringLiteral("vendor_ramdisk_%1.img").arg(index)
                                 : QStringLiteral("vendor_ramdisk_%1_%2.img")
                                       .arg(index)
                                       .arg(fragment.name);
        img::writeFileOrThrow(img::joinPath(output, name), fragment.data);
        info << QStringLiteral("分片 %1：类型 %2，大小 %3，格式 %4")
                    .arg(name)
                    .arg(fragment.type)
                    .arg(img::formatBytes(fragment.data.size()))
                    .arg(ramdiskFormatName(fragment.data));
        ++index;
    }
    writeTextFile(img::joinPath(output, QStringLiteral("info.txt")), info);
    ctx.ok(QStringLiteral("vendor_boot 解包完成：%1").arg(output));
}

void vendorBootRepack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("原始 vendor_boot 镜像"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出镜像路径"));
    const QByteArray data = img::readFileOrThrow(input);
    img::boot::Image image = img::boot::parse(data);
    if (image.kind != img::boot::ImageKind::VendorBoot)
        throw std::runtime_error("原始镜像不是 vendor_boot（magic 不是 VNDRBOOT）");
    const QString dtbPath = optText(options, QStringLiteral("dtb"));
    if (!dtbPath.isEmpty())
        image.dtb = img::readFileOrThrow(dtbPath);
    const QString bootconfigPath = optText(options, QStringLiteral("bootconfig"));
    if (!bootconfigPath.isEmpty())
        image.bootconfig = img::readFileOrThrow(bootconfigPath);
    const QString ramdiskPath = optText(options, QStringLiteral("ramdisk"));
    if (!ramdiskPath.isEmpty()) {
        img::boot::VendorFragment fragment;
        fragment.type = 1;
        fragment.name = QStringLiteral("platform");
        fragment.data = ramdiskFromPath(ramdiskPath, ctx);
        image.vendorRamdisks = {fragment};
    }
    const QString cmdline = optText(options, QStringLiteral("cmdline"));
    if (!cmdline.isEmpty())
        image.cmdline = cmdline;
    const QByteArray out = img::boot::serialize(image);
    img::writeFileOrThrow(output, out);
    ctx.ok(QStringLiteral("vendor_boot 重打包完成：%1（%2）")
               .arg(output, img::formatBytes(out.size())));
}

// ---------------------------------------------------------------------------
// cpio ramdisk
// ---------------------------------------------------------------------------

QByteArray readMaybeCompressed(const QString& path, TaskContext& ctx)
{
    const QByteArray data = img::readFileOrThrow(path);
    if (img::boot::isCpio(data))
        return data;
    try {
        const QByteArray decompressed = img::boot::decompressRamdisk(data);
        ctx.info(QStringLiteral("已解压输入（%1）").arg(ramdiskFormatName(data)));
        return decompressed;
    } catch (const std::exception& e) {
        throw std::runtime_error(QStringLiteral("输入不是 cpio 或可识别的压缩格式：%1")
                                     .arg(QString::fromUtf8(e.what()))
                                     .toStdString());
    }
}

void cpioUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("cpio / ramdisk 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出目录"));
    const QByteArray data = readMaybeCompressed(input, ctx);
    const img::cpio::Archive archive = img::cpio::parse(data);
    ctx.info(QStringLiteral("cpio 含 %1 个条目，解包到 %2")
                 .arg(archive.entries.size())
                 .arg(output));
    img::cpio::extractTo(archive, output, progressOf(ctx), cancelOf(ctx));
    int dirs = 0;
    int files = 0;
    for (const auto& entry : archive.entries) {
        if (entry.isDir())
            ++dirs;
        else
            ++files;
    }
    ctx.ok(QStringLiteral("解包完成：%1 个目录，%2 个文件").arg(dirs).arg(files));
}

void cpioPack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("目录"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出路径"));
    const bool gzipOut = optBool(options, QStringLiteral("gzip"), true);
    const img::cpio::Archive archive = img::cpio::fromDirectory(input);
    const QByteArray cpio = img::cpio::serialize(archive);
    const QByteArray out = gzipOut ? img::compress::gzipCompress(cpio) : cpio;
    img::writeFileOrThrow(output, out);
    ctx.info(QStringLiteral("打包 %1 个条目，输出 %2")
                 .arg(archive.entries.size())
                 .arg(img::formatBytes(out.size())));
    ctx.ok(QStringLiteral("cpio 打包完成：%1").arg(output));
}

void ramdiskModify(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("ramdisk 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出路径"));
    QByteArray data = readMaybeCompressed(input, ctx);
    img::cpio::Archive archive = img::cpio::parse(data);
    ctx.info(QStringLiteral("ramdisk 含 %1 个条目").arg(archive.entries.size()));

    const QString addPath = optText(options, QStringLiteral("addPath"));
    const QString addSource = optText(options, QStringLiteral("addSource"));
    if (!addPath.isEmpty() || !addSource.isEmpty()) {
        if (addPath.isEmpty() || addSource.isEmpty())
            throw std::runtime_error("添加文件需要同时填写「归档内路径」和「本地文件」");
        const QByteArray fileData = img::readFileOrThrow(addSource);
        img::cpio::putFile(archive, addPath, fileData, 0100644u);
        ctx.info(QStringLiteral("已添加/替换：%1（%2 字节）").arg(addPath).arg(fileData.size()));
    }

    const QString removePath = optText(options, QStringLiteral("removePath"));
    if (!removePath.isEmpty()) {
        if (!img::cpio::removeEntry(archive, removePath, true))
            throw std::runtime_error(QStringLiteral("归档中未找到要删除的路径：%1")
                                         .arg(removePath)
                                         .toStdString());
        ctx.info(QStringLiteral("已删除：%1").arg(removePath));
    }

    const QString chmodPath = optText(options, QStringLiteral("chmodPath"));
    const QString chmodMode = optText(options, QStringLiteral("chmodMode"));
    if (!chmodPath.isEmpty() && !chmodMode.isEmpty()) {
        bool ok = false;
        const quint32 mode = img::cpio::parseModeText(chmodMode, &ok);
        if (!ok)
            throw std::runtime_error("权限格式错误，请填写八进制权限，如 0755");
        img::cpio::setPermissions(archive, chmodPath, mode, 0, 0, true);
        ctx.info(QStringLiteral("已设置 %1 权限为 %2").arg(chmodPath, img::cpio::formatMode(mode)));
    }

    const QString selinux = optText(options, QStringLiteral("selinux"));
    if (!selinux.isEmpty()) {
        const QString target = chmodPath.isEmpty() ? QStringLiteral(".") : chmodPath;
        QByteArray contexts;
        for (const auto& entry : archive.entries) {
            if (entry.name == QStringLiteral("file_contexts"))
                contexts = entry.data;
        }
        contexts.append(QStringLiteral("%1 %2\n").arg(target, selinux).toUtf8());
        img::cpio::putFile(archive, QStringLiteral("file_contexts"), contexts, 0100644u);
        ctx.warn(QStringLiteral("已写入 file_contexts 条目（cpio 本身无法保存 SELinux 扩展属性）"));
    }

    const bool gzipOut = optBool(options, QStringLiteral("gzip"), true);
    const QByteArray cpio = img::cpio::serialize(archive);
    const QByteArray out = gzipOut ? img::compress::gzipCompress(cpio) : cpio;
    img::writeFileOrThrow(output, out);
    ctx.ok(QStringLiteral("ramdisk 修改完成：%1（%2）").arg(output, img::formatBytes(out.size())));
}

// ---------------------------------------------------------------------------
// super (动态分区)
// ---------------------------------------------------------------------------

void superInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("super.img 路径"));
    const img::superimg::SuperInfo info = img::superimg::inspectFile(input);
    ctx.info(QStringLiteral("geometry：metadata_max_size=%1，slot_count=%2，logical_block_size=%3")
                 .arg(info.geometry.metadataMaxSize)
                 .arg(info.geometry.metadataSlotCount)
                 .arg(info.geometry.logicalBlockSize));
    ctx.info(QStringLiteral("文件大小 %1，geometry 校验 %2，sparse 包装：%3")
                 .arg(img::formatBytes(quint64(info.fileSize)))
                 .arg(info.geometry.checksumValid ? QStringLiteral("通过") : QStringLiteral("不匹配"))
                 .arg(info.sparseWrapped ? QStringLiteral("是") : QStringLiteral("否")));
    for (const auto& warning : info.warnings)
        ctx.warn(warning);

    const img::superimg::Metadata* metadata = info.active();
    if (metadata == nullptr) {
        ctx.error(QStringLiteral("没有可用的元数据槽"));
        throw std::runtime_error("super 元数据解析失败：没有可用槽");
    }
    ctx.info(QStringLiteral("活动槽：%1，元数据版本 %2.%3，头 %4 字节，表 %5 字节")
                 .arg(info.activeSlot)
                 .arg(metadata->majorVersion)
                 .arg(metadata->minorVersion)
                 .arg(metadata->headerSize)
                 .arg(metadata->tablesSize));
    ctx.info(QStringLiteral("分区 %1 个，组 %2 个，块设备 %3 个")
                 .arg(metadata->partitions.size())
                 .arg(metadata->groups.size())
                 .arg(metadata->blockDevices.size()));
    for (const auto& device : metadata->blockDevices) {
        ctx.info(QStringLiteral("块设备 %1：起始扇区 %2，对齐 %3，大小 %4，flags 0x%5")
                     .arg(device.partitionName)
                     .arg(device.firstLogicalSector)
                     .arg(device.alignment)
                     .arg(img::formatBytes(device.size * 512))
                     .arg(device.flags, 0, 16));
    }
    for (const auto& group : metadata->groups) {
        ctx.info(QStringLiteral("组 %1：flags 0x%2，maximum_size %3")
                     .arg(group.name)
                     .arg(group.flags, 0, 16)
                     .arg(group.maximumSize == 0 ? QStringLiteral("不限")
                                                 : img::formatBytes(group.maximumSize * 512)));
    }
    for (const auto& partition : metadata->partitions) {
        quint64 bytes = 0;
        for (quint32 i = 0; i < partition.numExtents; ++i) {
            const quint32 at = partition.firstExtentIndex + i;
            if (at < quint32(metadata->extents.size()))
                bytes += metadata->extents.at(int(at)).numSectors * 512;
        }
        ctx.info(QStringLiteral("分区 %1：%2，%3 个 extent，组索引 %4")
                     .arg(partition.name, img::formatBytes(bytes))
                     .arg(partition.numExtents)
                     .arg(partition.groupIndex));
        for (quint32 i = 0; i < partition.numExtents; ++i) {
            const quint32 at = partition.firstExtentIndex + i;
            if (at >= quint32(metadata->extents.size()))
                continue;
            const auto& extent = metadata->extents.at(int(at));
            ctx.debug(QStringLiteral("  extent %1：%2，起始扇区 %3，扇区数 %4")
                          .arg(i)
                          .arg(extent.typeText())
                          .arg(extent.targetData)
                          .arg(extent.numSectors));
        }
    }
    const QStringList issues = img::superimg::validate(info);
    for (const auto& issue : issues)
        ctx.warn(issue);
    ctx.ok(QStringLiteral("super 解析完成：%1 个分区").arg(metadata->partitions.size()));
}

void superSplit(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("super.img 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出目录"));
    const QStringList only = splitNames(optText(options, QStringLiteral("partitions")));
    const bool sparseOut = optBool(options, QStringLiteral("sparse"), false);
    const img::superimg::SuperInfo info = img::superimg::inspectFile(input);
    for (const auto& warning : info.warnings)
        ctx.warn(warning);
    ctx.info(QStringLiteral("开始拆分 super（%1 个元数据槽，sparse 输出：%2）")
                 .arg(info.metadataSlots.size())
                 .arg(sparseOut ? QStringLiteral("是") : QStringLiteral("否")));
    img::superimg::split(input, info, output, only, sparseOut, progressOf(ctx), cancelOf(ctx));
    ctx.ok(QStringLiteral("super 拆分完成：%1").arg(output));
}

void superMerge(TaskContext& ctx, const QVariantMap& options)
{
    const QString source = requireText(options, QStringLiteral("source"), QStringLiteral("分区镜像目录"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出 super.img 路径"));
    QStringList names = splitNames(optText(options, QStringLiteral("partitions")));
    if (names.isEmpty()) {
        const QDir dir(source);
        const QStringList files = dir.entryList({QStringLiteral("*.img")}, QDir::Files);
        for (const QString& file : files)
            names << QFileInfo(file).completeBaseName();
        if (names.isEmpty())
            throw std::runtime_error("目录中未找到 *.img 分区镜像，请手动填写分区列表");
        ctx.info(QStringLiteral("自动识别分区：%1").arg(names.join(QStringLiteral(", "))));
    }
    const QString sizeText = optText(options, QStringLiteral("size"));
    quint64 superSize = 0;
    if (!sizeText.isEmpty()) {
        bool ok = false;
        superSize = img::parseNumber(sizeText, &ok);
        if (!ok || superSize == 0)
            throw std::runtime_error("super 大小格式错误（可用 4G / 0x100000000 写法）");
    }
    const int slotCount = optInt(options, QStringLiteral("slots"), 2);
    const bool sparseOut = optBool(options, QStringLiteral("sparse"), false);
    ctx.info(QStringLiteral("合并 %1 个分区，槽数 %2，sparse 输出 %3")
                 .arg(names.size())
                 .arg(slotCount)
                 .arg(sparseOut ? QStringLiteral("是") : QStringLiteral("否")));
    img::superimg::merge(source, names, output, superSize, quint32(slotCount), sparseOut,
                         progressOf(ctx), cancelOf(ctx));
    ctx.ok(QStringLiteral("super 合并完成：%1").arg(output));
}

void superResize(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("super.img 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出路径"));
    const QString sizeText = requireText(options, QStringLiteral("size"), QStringLiteral("新的大小"));
    bool ok = false;
    const quint64 newSize = img::parseNumber(sizeText, &ok);
    if (!ok || newSize == 0)
        throw std::runtime_error("大小格式错误（可用 6G / 0x180000000 写法）");
    ctx.info(QStringLiteral("调整 super 大小：%1 -> %2")
                 .arg(img::formatBytes(quint64(QFileInfo(input).size())), img::formatBytes(newSize)));
    img::superimg::resize(input, output, newSize, progressOf(ctx), cancelOf(ctx));
    ctx.ok(QStringLiteral("super 大小已调整：%1").arg(output));
}

// ---------------------------------------------------------------------------
// sparse
// ---------------------------------------------------------------------------

void sparseInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("sparse 镜像路径"));
    const img::sparse::FileInfo info = img::sparse::inspectFile(input);
    ctx.info(QStringLiteral("块大小 %1，总块数 %2，chunk %3 个，image_checksum 0x%4")
                 .arg(info.blockSize)
                 .arg(info.totalBlocks)
                 .arg(info.totalChunks)
                 .arg(info.imageChecksum, 8, 16, QLatin1Char('0')));
    ctx.info(QStringLiteral("原始大小 %1，sparse 文件 %2（压缩比 %3%）")
                 .arg(img::formatBytes(info.rawSize))
                 .arg(img::formatBytes(quint64(info.fileSize)))
                 .arg(quint64(info.fileSize) * 100 / qMax<quint64>(1, info.rawSize)));
    if (info.fileSize <= 256 * 1024 * 1024) {
        try {
            const img::sparse::Image image = img::sparse::parse(img::readFileOrThrow(input));
            QHash<QString, int> counts;
            for (const auto& chunk : image.chunks)
                counts[chunk.typeText()] += 1;
            for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
                ctx.info(QStringLiteral("chunk 类型 %1：%2 个").arg(it.key()).arg(it.value()));
        } catch (const std::exception& e) {
            ctx.warn(QStringLiteral("完整 chunk 统计失败：%1").arg(QString::fromUtf8(e.what())));
        }
    } else {
        ctx.info(QStringLiteral("文件较大，仅显示头部统计（完整统计请用 sparse 解包）"));
    }
    ctx.ok(QStringLiteral("sparse 解析完成"));
}

void sparseUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("sparse 镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出 raw 路径"));
    img::sparse::unpackFile(input, output, progressOf(ctx), cancelOf(ctx));
    ctx.ok(QStringLiteral("sparse 解包完成：%1（%2）")
               .arg(output, img::formatBytes(quint64(QFileInfo(output).size()))));
}

void sparsePack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("raw 镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出 sparse 路径"));
    const int blockSize = optInt(options, QStringLiteral("blockSize"), 4096);
    if (blockSize < 512 || blockSize > 1024 * 1024)
        throw std::runtime_error("块大小必须在 512 ~ 1048576 之间");
    const bool fill = optBool(options, QStringLiteral("fill"), true);
    const bool dontCare = optBool(options, QStringLiteral("dontCare"), true);
    img::sparse::packFile(input, output, quint32(blockSize), fill, dontCare, progressOf(ctx),
                          cancelOf(ctx));
    ctx.ok(QStringLiteral("sparse 打包完成：%1 -> %2（%3）")
               .arg(img::formatBytes(quint64(QFileInfo(input).size())),
                    img::formatBytes(quint64(QFileInfo(output).size())),
                    output));
}

// ---------------------------------------------------------------------------
// vbmeta / AVB
// ---------------------------------------------------------------------------

void vbmetaInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("vbmeta 路径"));
    const QByteArray data = img::readFileOrThrow(input);
    const img::vbmeta::Vbmeta vbmeta = img::vbmeta::parse(data);
    for (const auto& line : img::vbmeta::describe(vbmeta))
        ctx.info(line);
    if (vbmeta.hashtreeDisabled() || vbmeta.verificationDisabled())
        ctx.warn(QStringLiteral("当前 flags 已关闭部分校验（%1）").arg(vbmeta.flagsText()));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("vbmeta 解析完成：%1 个描述符").arg(vbmeta.descriptors.size()));
}

void vbmetaDisable(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("vbmeta 路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出路径"));
    const int mode = optInt(options, QStringLiteral("mode"), 0);
    const bool clearAuth = optBool(options, QStringLiteral("clearAuth"), false);
    const QByteArray data = img::readFileOrThrow(input);
    const img::vbmeta::Vbmeta vbmeta = img::vbmeta::parse(data);
    quint32 newFlags = vbmeta.flags & ~(img::vbmeta::kFlagHashtreeDisabled |
                                        img::vbmeta::kFlagVerificationDisabled);
    switch (mode) {
    case 0:
        newFlags |= img::vbmeta::kFlagHashtreeDisabled | img::vbmeta::kFlagVerificationDisabled;
        break;
    case 1:
        newFlags |= img::vbmeta::kFlagHashtreeDisabled;
        break;
    case 2:
        newFlags |= img::vbmeta::kFlagVerificationDisabled;
        break;
    default:
        break;
    }
    const QByteArray out = img::vbmeta::patchFlags(data, newFlags, clearAuth);
    img::writeFileOrThrow(output, out);
    ctx.info(QStringLiteral("flags：0x%1 -> 0x%2")
                 .arg(vbmeta.flags, 8, 16, QLatin1Char('0'))
                 .arg(newFlags, 8, 16, QLatin1Char('0')));
    if (clearAuth) {
        ctx.warn(QStringLiteral("已清零签名块：该 vbmeta 不再包含有效签名，需要重新签名后才能被设备接受"));
    } else {
        ctx.warn(QStringLiteral("仅修改 flags 会破坏原有签名；设备若强制校验签名，需要使用 avbtool 重新签名"));
    }
    ctx.ok(QStringLiteral("vbmeta flags 已修改：%1").arg(output));
}

void vbmetaVerify(TaskContext& ctx, const QVariantMap& options)
{
    const QString vbmetaPath = requireText(options, QStringLiteral("vbmeta"), QStringLiteral("vbmeta 路径"));
    const QString imagePath = requireText(options, QStringLiteral("image"), QStringLiteral("分区镜像路径"));
    const int wanted = optInt(options, QStringLiteral("index"), 0);
    const QByteArray data = img::readFileOrThrow(vbmetaPath);
    const img::vbmeta::Vbmeta vbmeta = img::vbmeta::parse(data);
    QVector<img::vbmeta::Descriptor> hashtrees;
    for (const auto& descriptor : vbmeta.descriptors) {
        if (descriptor.tag == quint32(img::vbmeta::DescriptorTag::Hashtree))
            hashtrees.append(descriptor);
    }
    if (hashtrees.isEmpty())
        throw std::runtime_error("vbmeta 中没有 hashtree 描述符");
    if (wanted < 0 || wanted >= hashtrees.size())
        throw std::runtime_error(QStringLiteral("hashtree 描述符索引越界（共 %1 个）")
                                     .arg(hashtrees.size())
                                     .toStdString());
    const auto& descriptor = hashtrees.at(wanted);
    ctx.info(img::vbmeta::descriptorText(descriptor));
    ctx.setStatus(QStringLiteral("计算 verity 根哈希"));
    const img::vbmeta::VerifyResult result =
        img::vbmeta::verifyHashtree(descriptor, imagePath, cancelOf(ctx));
    ctx.info(result.message);
    if (!result.ok)
        throw std::runtime_error(result.message.toStdString());
    ctx.ok(QStringLiteral("hashtree 校验通过"));
}

// ---------------------------------------------------------------------------
// GPT
// ---------------------------------------------------------------------------

void gptInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("磁盘/整盘镜像路径"));
    const quint32 sectorSize = sectorSizeOf(options);
    const qint64 size = QFileInfo(input).size();
    if (size > 2LL * 1024 * 1024 * 1024)
        throw std::runtime_error("镜像超过 2GB，请使用「按 GPT 提取分区」工具直接取分区");
    const QByteArray disk = img::readFileOrThrow(input);
    const img::gpt::Table table = img::gpt::parse(disk, sectorSize);
    ctx.info(QStringLiteral("磁盘 %1，扇区大小 %2，总扇区 %3")
                 .arg(img::formatBytes(quint64(disk.size())))
                 .arg(sectorSize)
                 .arg(table.diskSectors));
    ctx.info(QStringLiteral("MBR：%1，保护性 MBR：%2")
                 .arg(table.hasMbr ? QStringLiteral("存在") : QStringLiteral("缺失"))
                 .arg(table.protectiveMbr ? QStringLiteral("是") : QStringLiteral("否")));
    ctx.info(QStringLiteral("主 GPT：%1（头 CRC %2，分区表 CRC %3）")
                 .arg(table.primary.present ? QStringLiteral("存在") : QStringLiteral("缺失"))
                 .arg(table.primary.crcValid ? QStringLiteral("有效") : QStringLiteral("无效"))
                 .arg(table.primary.entriesCrcValid ? QStringLiteral("有效") : QStringLiteral("无效")));
    ctx.info(QStringLiteral("备份 GPT：%1（头 CRC %2，分区表 CRC %3）")
                 .arg(table.backup.present ? QStringLiteral("存在") : QStringLiteral("缺失"))
                 .arg(table.backup.crcValid ? QStringLiteral("有效") : QStringLiteral("无效"))
                 .arg(table.backup.entriesCrcValid ? QStringLiteral("有效") : QStringLiteral("无效")));
    if (table.primary.present) {
        ctx.info(QStringLiteral("磁盘 GUID：%1，可用扇区 %2 ~ %3，分区表项 %4 x %5")
                     .arg(table.primary.diskGuid())
                     .arg(table.primary.firstUsableLba)
                     .arg(table.primary.lastUsableLba)
                     .arg(table.primary.numEntries)
                     .arg(table.primary.entrySize));
    }
    for (const auto& warning : table.warnings)
        ctx.warn(warning);
    int used = 0;
    for (int i = 0; i < table.entries.size(); ++i) {
        const auto& entry = table.entries.at(i);
        if (entry.isEmpty())
            continue;
        ++used;
        ctx.info(QStringLiteral("#%1 %2：偏移 %3，大小 %4，类型 %5")
                     .arg(i)
                     .arg(entry.name.isEmpty() ? QStringLiteral("(未命名)") : entry.name)
                     .arg(img::formatBytes(img::gpt::partitionOffsetBytes(table, i)),
                          img::formatBytes(entry.sectorCount() * sectorSize))
                     .arg(entry.typeGuid()));
    }
    ctx.ok(QStringLiteral("GPT 解析完成：%1 个分区").arg(used));
}

void gptBackup(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("磁盘镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("备份文件路径"));
    const quint32 sectorSize = sectorSizeOf(options);
    const QByteArray disk = img::readFileOrThrow(input);
    const QByteArray backup = img::gpt::exportBackup(disk, sectorSize);
    img::writeFileOrThrow(output, backup);
    ctx.ok(QStringLiteral("GPT 备份已导出：%1（%2）").arg(output, img::formatBytes(quint64(backup.size()))));
}

void gptRestore(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("磁盘镜像路径"));
    const QString backupPath = requireText(options, QStringLiteral("backup"), QStringLiteral("GPT 备份文件路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出镜像路径"));
    const quint32 sectorSize = sectorSizeOf(options);
    QByteArray disk = img::readFileOrThrow(input);
    const QByteArray backup = img::readFileOrThrow(backupPath);
    const QStringList log = img::gpt::importBackup(disk, backup, sectorSize);
    img::writeFileOrThrow(output, disk);
    for (const auto& line : log)
        ctx.info(line);
    ctx.ok(QStringLiteral("GPT 已从备份恢复：%1").arg(output));
}

void gptRepair(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("磁盘镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出镜像路径"));
    const quint32 sectorSize = sectorSizeOf(options);
    QByteArray disk = img::readFileOrThrow(input);
    const QStringList log = img::gpt::repair(disk, sectorSize);
    img::writeFileOrThrow(output, disk);
    for (const auto& line : log)
        ctx.info(line);
    ctx.ok(QStringLiteral("GPT 修复完成：%1").arg(output));
}

void gptExtract(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("磁盘镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出分区镜像路径"));
    const QString target = requireText(options, QStringLiteral("partition"), QStringLiteral("分区名或索引"));
    const quint32 sectorSize = sectorSizeOf(options);

    // 只读取 GPT 头部区域，避免整盘载入内存。
    const qint64 fileSize = QFileInfo(input).size();
    img::FileReader reader(input);
    const QByteArray head = reader.readAt(0, qMin<qint64>(qint64(sectorSize) * 2, fileSize));
    if (head.size() < qsizetype(sectorSize) * 2)
        throw std::runtime_error("镜像太小，无法读取 GPT 头");
    const quint32 numEntries = quint32(quint8(head.at(sectorSize + 80))) |
                               (quint32(quint8(head.at(sectorSize + 81))) << 8) |
                               (quint32(quint8(head.at(sectorSize + 82))) << 16) |
                               (quint32(quint8(head.at(sectorSize + 83))) << 24);
    const quint32 entrySize = quint32(quint8(head.at(sectorSize + 84))) |
                              (quint32(quint8(head.at(sectorSize + 85))) << 8) |
                              (quint32(quint8(head.at(sectorSize + 86))) << 16) |
                              (quint32(quint8(head.at(sectorSize + 87))) << 24);
    if (numEntries == 0 || numEntries > 4096 || entrySize < 128 || entrySize > 4096)
        throw std::runtime_error("GPT 分区表头参数异常");
    const qint64 tableBytes = qint64(numEntries) * entrySize;
    const QByteArray prefix = reader.readAt(0, qMin<qint64>(fileSize,
                                                           qint64(sectorSize) * 2 + tableBytes));
    const img::gpt::Table table = img::gpt::parse(prefix, sectorSize);

    int index = -1;
    bool ok = false;
    const quint32 numeric = target.toUInt(&ok);
    if (ok)
        index = int(numeric);
    else
        index = img::gpt::findByName(table, target);
    if (index < 0 || index >= table.entries.size() || table.entries.at(index).isEmpty())
        throw std::runtime_error(QStringLiteral("未找到分区：%1").arg(target).toStdString());

    const auto& entry = table.entries.at(index);
    const quint64 offset = img::gpt::partitionOffsetBytes(table, index);
    const quint64 bytes = entry.sectorCount() * sectorSize;
    if (qint64(offset + bytes) > fileSize)
        throw std::runtime_error("分区超出镜像文件范围（GPT 可能损坏）");
    ctx.info(QStringLiteral("提取分区 %1：偏移 %2，大小 %3")
                 .arg(entry.name.isEmpty() ? target : entry.name)
                 .arg(img::formatBytes(offset), img::formatBytes(bytes)));
    img::FileWriter writer(output, true);
    quint64 done = 0;
    while (done < bytes) {
        ctx.checkCanceled();
        const qint64 chunk = qint64(qMin<quint64>(4 * 1024 * 1024, bytes - done));
        writer.write(reader.readAt(qint64(offset + done), chunk));
        done += quint64(chunk);
        ctx.setProgress(int(100.0 * double(done) / double(bytes)));
    }
    writer.close();
    ctx.ok(QStringLiteral("分区已提取：%1").arg(output));
}

// ---------------------------------------------------------------------------
// 哈希与按偏移提取
// ---------------------------------------------------------------------------

void hashFile(TaskContext& ctx, const QVariantMap& options)
{
    QStringList paths;
    const QVariant raw = options.value(QStringLiteral("input"));
    if (raw.typeId() == QMetaType::QVariantList) {
        for (const QVariant& item : raw.toList())
            paths << item.toString();
    } else {
        paths = splitNames(raw.toString());
    }
    if (paths.isEmpty())
        throw std::runtime_error("请选择至少一个文件");
    const int algorithm = optInt(options, QStringLiteral("algorithm"), 0);
    const QString expected = optText(options, QStringLiteral("expected")).toLower();
    int matched = 0;
    int index = 0;
    for (const QString& path : paths) {
        ctx.checkCanceled();
        ++index;
        const QFileInfo info(path);
        if (!info.isFile())
            throw std::runtime_error(QStringLiteral("文件不存在：%1").arg(path).toStdString());
        QString hex;
        switch (algorithm) {
        case 0:
            hex = gf::util::sha256File(path);
            break;
        case 1: {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error(QStringLiteral("无法读取：%1").arg(path).toStdString());
            QCryptographicHash hash(QCryptographicHash::Sha1);
            hash.addData(&file);
            hex = QString::fromLatin1(hash.result().toHex());
            break;
        }
        case 2: {
            hex = gf::util::md5File(path);
            break;
        }
        default: {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error(QStringLiteral("无法读取：%1").arg(path).toStdString());
            quint32 state = img::crc32Init();
            while (!file.atEnd()) {
                const QByteArray chunk = file.read(4 * 1024 * 1024);
                state = img::crc32Update(state, chunk.constData(), chunk.size());
            }
            hex = QStringLiteral("%1").arg(img::crc32Final(state), 8, 16, QLatin1Char('0'));
            break;
        }
        }
        const QString name = info.fileName();
        ctx.info(QStringLiteral("%1  %2  %3").arg(hex, name, img::formatBytes(quint64(info.size()))));
        if (!expected.isEmpty()) {
            if (hex.compare(expected, Qt::CaseInsensitive) == 0) {
                ++matched;
                ctx.ok(QStringLiteral("%1 哈希匹配").arg(name));
            } else {
                ctx.error(QStringLiteral("%1 哈希不匹配（期望 %2）").arg(name, expected));
            }
        }
        ctx.setProgress(int(100.0 * index / paths.size()));
    }
    if (!expected.isEmpty() && matched != paths.size())
        throw std::runtime_error("存在哈希不匹配的文件");
    ctx.ok(QStringLiteral("哈希计算完成：%1 个文件").arg(paths.size()));
}

void extractByOffset(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("镜像路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出文件路径"));
    const QString offsetText = requireText(options, QStringLiteral("offset"), QStringLiteral("偏移"));
    const QString sizeText = requireText(options, QStringLiteral("size"), QStringLiteral("大小"));
    bool okOffset = false;
    bool okSize = false;
    const quint64 offset = img::parseNumber(offsetText, &okOffset);
    const quint64 size = img::parseNumber(sizeText, &okSize);
    if (!okOffset || !okSize || size == 0)
        throw std::runtime_error("偏移/大小格式错误（支持 0x1000 / 64K / 1048576）");
    img::FileReader reader(input);
    if (qint64(offset + size) > reader.size())
        throw std::runtime_error("提取范围超出文件大小");
    ctx.info(QStringLiteral("按偏移提取：%1 + %2 -> %3")
                 .arg(img::formatBytes(offset), img::formatBytes(size), output));
    img::FileWriter writer(output, true);
    quint64 done = 0;
    while (done < size) {
        ctx.checkCanceled();
        const qint64 chunk = qint64(qMin<quint64>(4 * 1024 * 1024, size - done));
        writer.write(reader.readAt(qint64(offset + done), chunk));
        done += quint64(chunk);
        ctx.setProgress(int(100.0 * double(done) / double(size)));
    }
    writer.close();
    ctx.ok(QStringLiteral("按偏移提取完成：%1").arg(output));
}

QVariantMap pathField(const QString& key, const QString& label, const QString& filter = QStringLiteral("所有文件 (*)"))
{
    return fields::path(key, label, filter);
}


void bootLogoExtract(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = optText(options, QStringLiteral("input"));
    QString outputDir = optText(options, QStringLiteral("output"));
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择 bootlogo / logo.bin 镜像文件");
    if (outputDir.isEmpty())
        outputDir = QFileInfo(input).absolutePath() + QStringLiteral("/bootlogo_extract");
    if (!util::ensureDir(outputDir))
        throw std::runtime_error(QStringLiteral("无法创建输出目录：%1").arg(outputDir).toStdString());

    QFile file(input);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(QStringLiteral("无法打开 %1").arg(input).toStdString());
    const qint64 size = file.size();
    if (size <= 0)
        throw std::runtime_error("镜像文件为空");
    if (size > 512LL * 1024 * 1024)
        throw std::runtime_error("镜像大于 512MB，bootlogo 分区一般不会这么大，请确认文件");
    const QByteArray blob = file.readAll();
    file.close();

    ctx.info(QStringLiteral("分析 %1（%2）").arg(QFileInfo(input).fileName(), img::formatBytes(quint64(size))));

    const QList<img::EmbeddedImage> images = img::findEmbeddedImages(blob);
    int written = 0;
    for (const img::EmbeddedImage& embedded : images) {
        ctx.checkCanceled();
        QImage image;
        if (!image.loadFromData(embedded.data))
            continue;
        const QString path = QDir(outputDir).filePath(
            QStringLiteral("logo_%1_%2.%3").arg(++written, 2, 10, QLatin1Char('0'))
                .arg(embedded.offset)
                .arg(embedded.format == QLatin1String("jpeg") ? QStringLiteral("jpg")
                                                              : embedded.format));
        if (!image.save(path)) {
            ctx.warn(QStringLiteral("写入失败：%1").arg(path));
            continue;
        }
        ctx.ok(QStringLiteral("已导出 %1 · %2×%3").arg(QFileInfo(path).fileName())
                   .arg(image.width()).arg(image.height()));
        if (written >= 32)
            break;
    }

    // No embedded bitmap: optionally decode a raw RGB565/RGB888/ARGB8888 frame.
    const int width = optInt(options, QStringLiteral("width"), 0);
    const int height = optInt(options, QStringLiteral("height"), 0);
    if (written == 0 && width > 0 && height > 0) {
        const QString offsetText = optText(options, QStringLiteral("offset")).trimmed();
        quint64 offset = 0;
        if (!offsetText.isEmpty()) {
            bool offsetOk = false;
            offset = img::parseNumber(offsetText, &offsetOk);
            if (!offsetOk)
                throw std::runtime_error("裸位图起始偏移格式错误（支持 0x1000 / 65536）");
        }
        const int bpp = optInt(options, QStringLiteral("bpp"), 16);
        const bool bigEndian = optText(options, QStringLiteral("endian")) == QLatin1String("big");
        const QImage raw = img::decodeRawBitmap(blob, width, height, qint64(offset), bpp, bigEndian);
        if (raw.isNull())
            throw std::runtime_error(QStringLiteral("按 %1×%2 / %3bpp 解码失败：文件数据不足").arg(width).arg(height).arg(bpp).toStdString());
        const QString path = QDir(outputDir).filePath(
            QStringLiteral("logo_raw_%1x%2_%3bpp.png").arg(width).arg(height).arg(bpp));
        if (!raw.save(path))
            throw std::runtime_error(QStringLiteral("无法写入 %1").arg(path).toStdString());
        ++written;
        ctx.ok(QStringLiteral("已按原始位图导出 %1").arg(path));
    }

    if (written == 0) {
        const qint64 frame16 = size / (qint64(1080) * 2400 * 2);
        const qint64 frame24 = size / (qint64(1080) * 2400 * 3);
        ctx.warn(QStringLiteral("没有找到 PNG/JPEG 图片。该文件可能是厂商私有格式："));
        ctx.log(0, QStringLiteral("  · 若为裸 RGB565 1080×2400，约可存 %1 帧；裸 RGB888 约 %2 帧").arg(frame16).arg(frame24));
        ctx.log(0, QStringLiteral("  · 可在「宽 / 高 / 位深」里填写实际值后重新提取（支持 16/24/32bpp）"));
        throw std::runtime_error("未识别到可导出的开机 LOGO 图片");
    }

    ctx.setProgress(100);
    ctx.ok(QStringLiteral("完成：导出 %1 张图片到 %2").arg(written).arg(outputDir));
}

void payloadDiffAnalyze(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = optText(options, QStringLiteral("input"));
    const QString sourceDir = optText(options, QStringLiteral("source"));
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择 payload.bin");
    const auto payload = img::payload::parseFile(input);
    ctx.info(QStringLiteral("Payload v%1 · 块大小 %2 · %3")
                 .arg(payload.header.version)
                 .arg(payload.manifest.blockSize)
                 .arg(payload.manifest.partialUpdate ? QStringLiteral("增量包 (partial update)")
                                                     : QStringLiteral("完整包")));
    ctx.setProgress(20);

    QSet<QString> needsSource;
    int totalOps = 0;
    for (const auto& partition : payload.manifest.partitions) {
        QMap<QString, int> histogram;
        bool partitionNeedsSource = false;
        for (const auto& operation : partition.operations) {
            const QString name = img::payload::operationTypeText(operation.type);
            histogram[name] = histogram.value(name) + 1;
            ++totalOps;
            switch (operation.type) {
            case img::payload::OperationType::Move:
            case img::payload::OperationType::Bsdiff:
            case img::payload::OperationType::SourceCopy:
            case img::payload::OperationType::SourceBsdiff:
                partitionNeedsSource = true;
                break;
            default:
                break;
            }
        }
        if (partitionNeedsSource)
            needsSource.insert(partition.name);
        QStringList detail;
        for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it)
            detail << QStringLiteral("%1×%2").arg(it.key()).arg(it.value());
        ctx.log(1, QStringLiteral("%1 · %2 · %3 个操作 [%4]")
                       .arg(partition.name, img::formatBytes(partition.newInfo.size))
                       .arg(partition.operations.size())
                       .arg(detail.isEmpty() ? QStringLiteral("无") : detail.join(QStringLiteral(", "))));
    }
    ctx.setProgress(60);

    if (!payload.manifest.partialUpdate) {
        ctx.ok(QStringLiteral("这是完整 payload（%1 个分区 / %2 个操作），可直接使用「Payload 解包」")
                   .arg(payload.manifest.partitions.size()).arg(totalOps));
        return;
    }

    if (needsSource.isEmpty()) {
        ctx.ok(QStringLiteral("增量包不依赖源镜像（仅有 REPLACE/ZERO 类操作），可由完整包工具链处理"));
        return;
    }

    ctx.info(QStringLiteral("以下分区需要旧镜像作为增量输入：%1")
                 .arg(QStringList(needsSource.values()).join(QStringLiteral(", "))));
    if (sourceDir.isEmpty()) {
        throw std::runtime_error("增量包需要源镜像目录，请在「源镜像目录」中选择旧系统镜像所在文件夹");
    }
    const QDir dir(sourceDir);
    QStringList missing;
    for (const QString& name : needsSource) {
        const QFileInfo info(dir.filePath(name + QStringLiteral(".img")));
        if (!info.exists() || info.size() <= 0)
            missing << name;
    }
    if (!missing.isEmpty())
        throw std::runtime_error(
            QStringLiteral("源镜像目录缺少：%1").arg(missing.join(QStringLiteral(", "))).toStdString());
    ctx.setProgress(80);
    ctx.warn(QStringLiteral("已确认源镜像齐全。BSDIFF/PUFFDIFF 类操作需要 bspatch 算法，本版本仅做分析与校验，"
                            "不会执行二进制差分合并。"));
    ctx.ok(QStringLiteral("增量分析完成：%1 个分区需要源镜像，均已找到").arg(needsSource.size()));
}

void appendPlannedFirmwareTools()

{
    ToolSpec spec;
    spec.id = QStringLiteral("image.bootlogo.extract");
    spec.category = QStringLiteral("image");
    spec.group = QStringLiteral("boot");
    spec.name = QStringLiteral("BootLogo 提取");
    spec.desc = QStringLiteral("从 logo.bin / bootlogo 分区提取 PNG/JPEG，或按裸位图（RGB565 等）解码");
    spec.icon = QStringLiteral("sparkles");
    spec.keywords = {QStringLiteral("bootlogo"), QStringLiteral("开机"), QStringLiteral("logo")};
    spec.fields = {
        pathField(QStringLiteral("input"), QStringLiteral("镜像路径"), QStringLiteral("*.bin *.img *.*")),
        fields::dir(QStringLiteral("output"), QStringLiteral("输出目录"), QString(), false),
        fields::integer(QStringLiteral("width"), QStringLiteral("裸位图宽度（可选）"), 0, 0, 16384,
                        QStringLiteral("像素")),
        fields::integer(QStringLiteral("height"), QStringLiteral("裸位图高度（可选）"), 0, 0, 16384,
                        QStringLiteral("像素")),
        fields::hex(QStringLiteral("offset"), QStringLiteral("裸位图起始偏移"), QStringLiteral("0")),
        fields::choice(QStringLiteral("bpp"), QStringLiteral("位深"),
                       {QStringLiteral("16"), QStringLiteral("24"), QStringLiteral("32")}, 0),
        fields::choice(QStringLiteral("endian"), QStringLiteral("字节序"),
                       {QStringLiteral("little"), QStringLiteral("big")}, 0),
    };
    ToolRegistry::instance().add(spec, bootLogoExtract);

    ToolSpec diff;
    diff.id = QStringLiteral("image.payload.applyDiff");
    diff.category = QStringLiteral("image");
    diff.group = QStringLiteral("payload");
    diff.name = QStringLiteral("Payload 增量分析");
    diff.desc = QStringLiteral("解析 partial update 的结构与操作类型，校验源镜像是否齐全（不做 bspatch 合并）");
    diff.icon = QStringLiteral("git-branch");
    diff.keywords = {QStringLiteral("payload"), QStringLiteral("增量"), QStringLiteral("diff")};
    diff.fields = {
        pathField(QStringLiteral("input"), QStringLiteral("payload.bin"), QStringLiteral("*.bin")),
        fields::dir(QStringLiteral("source"), QStringLiteral("源镜像目录（增量包需要）"), QString(), false),
    };
    addTool(diff, payloadDiffAnalyze);
}

} // namespace


void bootLogoPack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = optText(options, QStringLiteral("input"));
    QString output = optText(options, QStringLiteral("output")).trimmed();
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择 PNG / JPEG 图片");
    if (output.isEmpty())
        output = QFileInfo(input).absolutePath() + QLatin1Char('/') + QFileInfo(input).completeBaseName()
                 + QStringLiteral(".bin");
    if (!util::ensureDir(QFileInfo(output).absolutePath()))
        throw std::runtime_error("无法创建输出目录");

    QImage image(input);
    if (image.isNull())
        throw std::runtime_error("无法解码图片，请使用 PNG / JPEG / BMP");
    const int bppChoice = optInt(options, QStringLiteral("bpp"), 0);
    const int bpp = bppChoice == 1 ? 24 : (bppChoice == 2 ? 32 : 16);
    const bool bigEndian = optText(options, QStringLiteral("endian")) == QLatin1String("big");

    const QByteArray raw = img::encodeRawBitmap(image, bpp, bigEndian);
    if (raw.isEmpty())
        throw std::runtime_error("编码失败：图片为空或位深不受支持");

    const QString paddingText = optText(options, QStringLiteral("padding")).trimmed();
    qint64 padding = 0;
    if (!paddingText.isEmpty()) {
        bool ok = false;
        padding = qint64(img::parseNumber(paddingText, &ok));
        if (!ok)
            throw std::runtime_error("前置填充长度格式错误（支持 0x1000 / 4096）");
    }

    QFile file(output);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(QStringLiteral("无法写入 %1").arg(output).toStdString());
    if (padding > 0) {
        QByteArray zeros(qsizetype(padding), '\0');
        file.write(zeros);
    }
    file.write(raw);
    file.close();

    ctx.info(QStringLiteral("图片 %1×%2 → %3bpp %4 字节序")
                 .arg(image.width()).arg(image.height()).arg(bpp)
                 .arg(bigEndian ? QStringLiteral("big") : QStringLiteral("little")));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("已生成裸位图：%1（%2，填充 %3 字节）")
               .arg(output, img::formatBytes(quint64(raw.size())), QString::number(padding)));
}



void registerImageFirmwareTools()
{
    using namespace gf::fields;

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.payload.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("payload");
        spec.name = QStringLiteral("Payload 信息");
        spec.desc = QStringLiteral("解析 payload.bin：分区、操作类型、数据大小与哈希");
        spec.icon = QStringLiteral("file-text");
        spec.keywords = {QStringLiteral("payload"), QStringLiteral("ota"), QStringLiteral("信息")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("payload.bin"), QStringLiteral("payload (*.bin);;所有文件 (*)"))};
        spec.order = 10;
        addTool(spec, payloadInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.payload.unpack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("payload");
        spec.name = QStringLiteral("Payload 解包");
        spec.desc = QStringLiteral("解析 payload.bin，导出全部分区镜像（支持校验）");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("payload"), QStringLiteral("解包"), QStringLiteral("ota")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("payload.bin"), QStringLiteral("payload (*.bin);;所有文件 (*)")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录")),
                       text(QStringLiteral("partitions"), QStringLiteral("只导出分区（逗号分隔，留空=全部）"),
                            QString(), QStringLiteral("boot,system"), QString(), false),
                       boolean(QStringLiteral("verify"), QStringLiteral("校验哈希"), true)};
        spec.order = 11;
        addTool(spec, payloadUnpack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.payload.pack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("payload");
        spec.name = QStringLiteral("Payload 打包");
        spec.desc = QStringLiteral("把分区镜像目录打包成 payload.bin（REPLACE、不签名）");
        spec.icon = QStringLiteral("file-up");
        spec.keywords = {QStringLiteral("payload"), QStringLiteral("打包")};
        spec.fields = {dir(QStringLiteral("source"), QStringLiteral("分区镜像目录")),
                       text(QStringLiteral("partitions"), QStringLiteral("分区名（逗号分隔）"),
                            QString(), QStringLiteral("boot,system,vendor"), QStringLiteral("按文件名（不含 .img）指定"), true),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 payload.bin"), QStringLiteral("payload (*.bin);;所有文件 (*)")),
                       integer(QStringLiteral("blockSize"), QStringLiteral("块大小"), 4096, 512, 1048576)};
        spec.order = 12;
        addTool(spec, payloadPack);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.boot.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("Boot 镜像信息");
        spec.desc = QStringLiteral("解析 boot / init_boot / vendor_boot 头（v0-v4）");
        spec.icon = QStringLiteral("file-text");
        spec.keywords = {QStringLiteral("boot"), QStringLiteral("kernel"), QStringLiteral("ramdisk")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("boot 镜像"), QStringLiteral("boot (*.img);;所有文件 (*)"))};
        spec.order = 20;
        addTool(spec, bootInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.boot.unpack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("Boot 解包");
        spec.desc = QStringLiteral("解包 kernel / ramdisk / dtb，并解开 ramdisk 的 cpio");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("boot"), QStringLiteral("解包"), QStringLiteral("ramdisk")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("boot 镜像"), QStringLiteral("boot (*.img);;所有文件 (*)")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录")),
                       boolean(QStringLiteral("decompress"), QStringLiteral("解压 ramdisk"), true),
                       boolean(QStringLiteral("extractCpio"), QStringLiteral("解包 ramdisk 里的 cpio"), true)};
        spec.order = 21;
        addTool(spec, bootUnpack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.boot.repack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("Boot 重打包");
        spec.desc = QStringLiteral("以原 boot 为模板重打包 kernel / ramdisk / dtb（v0-v4）");
        spec.icon = QStringLiteral("file-up");
        spec.keywords = {QStringLiteral("boot"), QStringLiteral("重打包"), QStringLiteral("repack")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("原始 boot 镜像"), QStringLiteral("boot (*.img);;所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出镜像路径")),
                       path(QStringLiteral("kernel"), QStringLiteral("新 kernel（留空=保持）"), QStringLiteral("所有文件 (*)"), QString(), false),
                       path(QStringLiteral("ramdisk"), QStringLiteral("新 ramdisk（cpio / gz / 目录）"),
                            QStringLiteral("所有文件 (*)"), QStringLiteral("选择目录可自动打包 cpio"), false),
                       path(QStringLiteral("dtb"), QStringLiteral("新 dtb（留空=保持）"), QStringLiteral("所有文件 (*)"), QString(), false),
                       text(QStringLiteral("cmdline"), QStringLiteral("cmdline（留空=保持）"), QString(), QStringLiteral("console=ttyMSM0"), QString(), false),
                       choice(QStringLiteral("version"), QStringLiteral("头版本"),
                              {QStringLiteral("跟随原镜像"), QStringLiteral("v0"), QStringLiteral("v1"),
                               QStringLiteral("v2"), QStringLiteral("v3"), QStringLiteral("v4")}, 0),
                       integer(QStringLiteral("pageSize"), QStringLiteral("页大小（0=跟随原镜像）"), 0, 0, 65536)};
        spec.order = 22;
        addTool(spec, bootRepack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.init_boot.unpack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("init_boot 解包");
        spec.desc = QStringLiteral("解包 init_boot.img 的 ramdisk（v4）");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("init_boot"), QStringLiteral("解包"), QStringLiteral("ramdisk")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("init_boot 镜像"), QStringLiteral("init_boot (*.img);;所有文件 (*)")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录")),
                       boolean(QStringLiteral("decompress"), QStringLiteral("解压 ramdisk"), true),
                       boolean(QStringLiteral("extractCpio"), QStringLiteral("解包 ramdisk 里的 cpio"), true)};
        spec.order = 23;
        addTool(spec, initBootUnpack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.init_boot.repack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("init_boot 重打包");
        spec.desc = QStringLiteral("用新的 ramdisk 重打包 init_boot.img（v4）");
        spec.icon = QStringLiteral("file-up");
        spec.keywords = {QStringLiteral("init_boot"), QStringLiteral("重打包")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("原始 init_boot 镜像"), QStringLiteral("init_boot (*.img);;所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出镜像路径")),
                       path(QStringLiteral("ramdisk"), QStringLiteral("新 ramdisk（cpio / gz / 目录）"),
                            QStringLiteral("所有文件 (*)"), QStringLiteral("必填"), true),
                       text(QStringLiteral("cmdline"), QStringLiteral("cmdline（留空=保持）"), QString(), QString(), QString(), false)};
        spec.order = 24;
        addTool(spec, initBootRepack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.vendor_boot.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("vendor_boot 信息");
        spec.desc = QStringLiteral("解析 vendor_boot v3/v4 头部与 ramdisk 分片");
        spec.icon = QStringLiteral("file-text");
        spec.keywords = {QStringLiteral("vendor_boot"), QStringLiteral("dtb")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("vendor_boot 镜像"), QStringLiteral("vendor_boot (*.img);;所有文件 (*)"))};
        spec.order = 25;
        addTool(spec, vendorBootInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.vendor_boot.unpack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("vendor_boot 解包");
        spec.desc = QStringLiteral("解包 vendor_boot 的 ramdisk 分片、dtb 与 bootconfig");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("vendor_boot"), QStringLiteral("解包")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("vendor_boot 镜像"), QStringLiteral("vendor_boot (*.img);;所有文件 (*)")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录"))};
        spec.order = 26;
        addTool(spec, vendorBootUnpack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.vendor_boot.repack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("vendor_boot 重打包");
        spec.desc = QStringLiteral("替换 dtb / bootconfig / ramdisk 后重打包 vendor_boot");
        spec.icon = QStringLiteral("file-up");
        spec.keywords = {QStringLiteral("vendor_boot"), QStringLiteral("重打包")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("原始 vendor_boot 镜像"), QStringLiteral("vendor_boot (*.img);;所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出镜像路径")),
                       path(QStringLiteral("dtb"), QStringLiteral("新 dtb（留空=保持）"), QStringLiteral("所有文件 (*)"), QString(), false),
                       path(QStringLiteral("bootconfig"), QStringLiteral("新 bootconfig（留空=保持）"), QStringLiteral("所有文件 (*)"), QString(), false),
                       path(QStringLiteral("ramdisk"), QStringLiteral("新 vendor_ramdisk（留空=保持）"),
                            QStringLiteral("所有文件 (*)"), QStringLiteral("会替换为单个 platform 分片"), false),
                       text(QStringLiteral("cmdline"), QStringLiteral("cmdline（留空=保持）"), QString(), QString(), QString(), false)};
        spec.order = 27;
        addTool(spec, vendorBootRepack);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.cpio.unpack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("ramdisk");
        spec.name = QStringLiteral("cpio 解包");
        spec.desc = QStringLiteral("解包 cpio newc 归档（自动识别 gzip/lzop 压缩的 ramdisk）");
        spec.icon = QStringLiteral("package");
        spec.keywords = {QStringLiteral("cpio"), QStringLiteral("ramdisk"), QStringLiteral("解包")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("cpio / ramdisk 文件"), QStringLiteral("所有文件 (*)")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录"))};
        spec.order = 30;
        addTool(spec, cpioUnpack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.cpio.pack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("ramdisk");
        spec.name = QStringLiteral("cpio 打包");
        spec.desc = QStringLiteral("把目录打包成 cpio newc 归档（可 gzip 压缩）");
        spec.icon = QStringLiteral("archive");
        spec.keywords = {QStringLiteral("cpio"), QStringLiteral("打包"), QStringLiteral("ramdisk")};
        spec.fields = {dir(QStringLiteral("input"), QStringLiteral("输入目录")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出文件路径")),
                       boolean(QStringLiteral("gzip"), QStringLiteral("gzip 压缩输出"), true)};
        spec.order = 31;
        addTool(spec, cpioPack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.ramdisk.modify");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("ramdisk");
        spec.name = QStringLiteral("Ramdisk 修改");
        spec.desc = QStringLiteral("在 ramdisk 中增删文件、修改权限（八进制）与 SELinux file_contexts");
        spec.icon = QStringLiteral("wand-2");
        spec.keywords = {QStringLiteral("ramdisk"), QStringLiteral("cpio"), QStringLiteral("权限"), QStringLiteral("selinux")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("输入 ramdisk"), QStringLiteral("所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 ramdisk 路径")),
                       text(QStringLiteral("addPath"), QStringLiteral("添加文件：归档内路径"), QString(),
                            QStringLiteral("init.rc"), QString(), false),
                       path(QStringLiteral("addSource"), QStringLiteral("添加文件：本地文件"), QStringLiteral("所有文件 (*)"), QString(), false),
                       text(QStringLiteral("removePath"), QStringLiteral("删除路径（递归）"), QString(),
                            QStringLiteral("sbin/old_binary"), QString(), false),
                       text(QStringLiteral("chmodPath"), QStringLiteral("修改权限：路径"), QString(),
                            QStringLiteral("init"), QString(), false),
                       text(QStringLiteral("chmodMode"), QStringLiteral("修改权限：模式（八进制）"), QString(),
                            QStringLiteral("0755"), QString(), false),
                       text(QStringLiteral("selinux"), QStringLiteral("SELinux 上下文（可选）"), QString(),
                            QStringLiteral("u:object_r:vendor_file:s0"), QString(), false),
                       boolean(QStringLiteral("gzip"), QStringLiteral("gzip 压缩输出"), true)};
        spec.order = 32;
        addTool(spec, ramdiskModify);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.super.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("super");
        spec.name = QStringLiteral("Super 信息");
        spec.desc = QStringLiteral("解析 super.img 的 lp 元数据：geometry、组、分区、extent");
        spec.icon = QStringLiteral("layers");
        spec.keywords = {QStringLiteral("super"), QStringLiteral("动态分区"), QStringLiteral("lp")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("super.img"), QStringLiteral("super (*.img);;所有文件 (*)"))};
        spec.order = 40;
        addTool(spec, superInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.super.split");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("super");
        spec.name = QStringLiteral("Super 拆分");
        spec.desc = QStringLiteral("按 lp 元数据把 super.img 拆成各分区镜像");
        spec.icon = QStringLiteral("split");
        spec.keywords = {QStringLiteral("super"), QStringLiteral("拆分"), QStringLiteral("lpunpack")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("super.img"), QStringLiteral("super (*.img);;所有文件 (*)")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录")),
                       text(QStringLiteral("partitions"), QStringLiteral("只拆分分区（逗号分隔，留空=全部）"),
                            QString(), QStringLiteral("system,vendor"), QString(), false),
                       boolean(QStringLiteral("sparse"), QStringLiteral("输出 sparse 镜像"), false)};
        spec.order = 41;
        addTool(spec, superSplit);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.super.merge");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("super");
        spec.name = QStringLiteral("Super 合并");
        spec.desc = QStringLiteral("把分区镜像目录合并成 super.img（可指定大小与槽数）");
        spec.icon = QStringLiteral("merge");
        spec.keywords = {QStringLiteral("super"), QStringLiteral("合并"), QStringLiteral("lpmake")};
        spec.fields = {dir(QStringLiteral("source"), QStringLiteral("分区镜像目录")),
                       text(QStringLiteral("partitions"), QStringLiteral("分区名（逗号分隔，留空=自动识别 *.img）"),
                            QString(), QStringLiteral("system,vendor,product"), QString(), false),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 super.img"), QStringLiteral("super (*.img);;所有文件 (*)")),
                       text(QStringLiteral("size"), QStringLiteral("super 大小（留空=按内容）"), QString(),
                            QStringLiteral("4G"), QString(), false),
                       integer(QStringLiteral("slots"), QStringLiteral("元数据槽数量"), 2, 1, 4),
                       boolean(QStringLiteral("sparse"), QStringLiteral("输出 sparse 镜像"), false)};
        spec.order = 42;
        addTool(spec, superMerge);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.super.resize");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("super");
        spec.name = QStringLiteral("Super 扩容/缩容");
        spec.desc = QStringLiteral("调整 super.img 尾部大小并更新 lp 元数据");
        spec.icon = QStringLiteral("maximize-2");
        spec.keywords = {QStringLiteral("super"), QStringLiteral("扩容"), QStringLiteral("缩容")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("super.img"), QStringLiteral("super (*.img);;所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 super.img")),
                       text(QStringLiteral("size"), QStringLiteral("新大小"), QString(),
                            QStringLiteral("6G"), QString(), true)};
        spec.order = 43;
        addTool(spec, superResize);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.sparse.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("sparse");
        spec.name = QStringLiteral("Sparse 信息");
        spec.desc = QStringLiteral("查看 Android sparse 镜像的块大小、chunk 类型与比率");
        spec.icon = QStringLiteral("database");
        spec.keywords = {QStringLiteral("sparse"), QStringLiteral("simg"), QStringLiteral("信息")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("sparse 镜像"), QStringLiteral("sparse (*.img);;所有文件 (*)"))};
        spec.order = 50;
        addTool(spec, sparseInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.sparse.unpack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("sparse");
        spec.name = QStringLiteral("Sparse 解包");
        spec.desc = QStringLiteral("sparse image 转 raw（img↔bin 的 sparse 形态）");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("sparse"), QStringLiteral("解包"), QStringLiteral("raw")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("sparse 镜像"), QStringLiteral("sparse (*.img);;所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 raw 镜像"))};
        spec.order = 51;
        addTool(spec, sparseUnpack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.sparse.pack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("sparse");
        spec.name = QStringLiteral("Sparse 打包");
        spec.desc = QStringLiteral("raw image 转 sparse，自动识别 fill / dontcare 块");
        spec.icon = QStringLiteral("file-up");
        spec.keywords = {QStringLiteral("sparse"), QStringLiteral("打包")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("raw 镜像")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 sparse 镜像")),
                       integer(QStringLiteral("blockSize"), QStringLiteral("块大小"), 4096, 512, 1048576),
                       boolean(QStringLiteral("fill"), QStringLiteral("识别 fill 块"), true),
                       boolean(QStringLiteral("dontCare"), QStringLiteral("识别 dontcare 全零块"), true)};
        spec.order = 52;
        addTool(spec, sparsePack);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.vbmeta.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("avb");
        spec.name = QStringLiteral("VBMeta 信息");
        spec.desc = QStringLiteral("解析 AVB vbmeta：flags、描述符、哈希树与签名信息");
        spec.icon = QStringLiteral("shield-check");
        spec.keywords = {QStringLiteral("vbmeta"), QStringLiteral("avb"), QStringLiteral("verity")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("vbmeta 镜像"), QStringLiteral("vbmeta (*.img);;所有文件 (*)"))};
        spec.order = 60;
        addTool(spec, vbmetaInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.vbmeta.disable");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("avb");
        spec.name = QStringLiteral("VBMeta 关闭校验");
        spec.desc = QStringLiteral("修补 vbmeta flags（关闭 verity / verification），可清空签名块");
        spec.icon = QStringLiteral("shield-alert");
        spec.keywords = {QStringLiteral("vbmeta"), QStringLiteral("verity"), QStringLiteral("disable")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("vbmeta 镜像"), QStringLiteral("vbmeta (*.img);;所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出 vbmeta 路径")),
                       choice(QStringLiteral("mode"), QStringLiteral("修补方式"),
                              {QStringLiteral("关闭 verity + verification"),
                               QStringLiteral("仅关闭 verity"),
                               QStringLiteral("仅关闭 verification"),
                               QStringLiteral("清除 flags（恢复完整校验）")}, 0),
                       boolean(QStringLiteral("clearAuth"), QStringLiteral("同时清零签名块"), false,
                               QStringLiteral("清零后必须重新签名，谨慎使用"))};
        spec.order = 61;
        addTool(spec, vbmetaDisable);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.vbmeta.verify");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("avb");
        spec.name = QStringLiteral("DM-verity 校验");
        spec.desc = QStringLiteral("按 vbmeta 的 hashtree 描述符重新计算分区根哈希并比对");
        spec.icon = QStringLiteral("check-check");
        spec.keywords = {QStringLiteral("verity"), QStringLiteral("hashtree"), QStringLiteral("校验")};
        spec.fields = {pathField(QStringLiteral("vbmeta"), QStringLiteral("vbmeta 镜像"), QStringLiteral("vbmeta (*.img);;所有文件 (*)")),
                       pathField(QStringLiteral("image"), QStringLiteral("分区镜像")),
                       integer(QStringLiteral("index"), QStringLiteral("hashtree 描述符序号（0 起）"), 0, 0, 64)};
        spec.order = 62;
        addTool(spec, vbmetaVerify);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.gpt.info");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("gpt");
        spec.name = QStringLiteral("GPT 解析");
        spec.desc = QStringLiteral("解析主/备 GPT、保护性 MBR、分区表与 CRC 状态");
        spec.icon = QStringLiteral("hard-drive");
        spec.keywords = {QStringLiteral("gpt"), QStringLiteral("分区表"), QStringLiteral("mbr")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("磁盘/整盘镜像"), QStringLiteral("所有文件 (*)")),
                       sectorSizeField()};
        spec.order = 70;
        addTool(spec, gptInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.gpt.backup");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("gpt");
        spec.name = QStringLiteral("GPT 备份");
        spec.desc = QStringLiteral("导出 MBR + GPT 头 + 分区表到备份文件");
        spec.icon = QStringLiteral("save");
        spec.keywords = {QStringLiteral("gpt"), QStringLiteral("备份")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("磁盘/整盘镜像"), QStringLiteral("所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("备份文件路径")),
                       sectorSizeField()};
        spec.order = 71;
        addTool(spec, gptBackup);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.gpt.restore");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("gpt");
        spec.name = QStringLiteral("GPT 恢复");
        spec.desc = QStringLiteral("从备份文件恢复主 GPT，并按目标镜像大小重建备份 GPT");
        spec.icon = QStringLiteral("download");
        spec.keywords = {QStringLiteral("gpt"), QStringLiteral("恢复")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("磁盘/整盘镜像"), QStringLiteral("所有文件 (*)")),
                       pathField(QStringLiteral("backup"), QStringLiteral("GPT 备份文件")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出镜像路径")),
                       sectorSizeField()};
        spec.order = 72;
        addTool(spec, gptRestore);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.gpt.repair");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("gpt");
        spec.name = QStringLiteral("GPT 修复");
        spec.desc = QStringLiteral("重算 CRC、重建主/备 GPT 与保护性 MBR");
        spec.icon = QStringLiteral("wrench");
        spec.keywords = {QStringLiteral("gpt"), QStringLiteral("修复"), QStringLiteral("crc")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("磁盘/整盘镜像"), QStringLiteral("所有文件 (*)")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出镜像路径")),
                       sectorSizeField()};
        spec.order = 73;
        addTool(spec, gptRepair);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.gpt.extract");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("gpt");
        spec.name = QStringLiteral("按 GPT 提取分区");
        spec.desc = QStringLiteral("按分区名或索引从整盘镜像提取分区，无需载入整个镜像");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("gpt"), QStringLiteral("提取"), QStringLiteral("分区")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("磁盘/整盘镜像"), QStringLiteral("所有文件 (*)")),
                       text(QStringLiteral("partition"), QStringLiteral("分区名或索引"), QString(),
                            QStringLiteral("boot / 0"), QStringLiteral("找不到时按索引序号"), true),
                       pathField(QStringLiteral("output"), QStringLiteral("输出分区镜像路径")),
                       sectorSizeField()};
        spec.order = 74;
        addTool(spec, gptExtract);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.hash.file");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("misc");
        spec.name = QStringLiteral("文件哈希校验");
        spec.desc = QStringLiteral("批量计算 / 校验 SHA256、SHA1、MD5 或 CRC32");
        spec.icon = QStringLiteral("hash");
        spec.keywords = {QStringLiteral("哈希"), QStringLiteral("sha256"), QStringLiteral("校验")};
        spec.fields = {paths(QStringLiteral("input"), QStringLiteral("文件列表"), QStringLiteral("所有文件 (*)")),
                       choice(QStringLiteral("algorithm"), QStringLiteral("算法"),
                              {QStringLiteral("SHA256"), QStringLiteral("SHA1"), QStringLiteral("MD5"),
                               QStringLiteral("CRC32")}, 0),
                       text(QStringLiteral("expected"), QStringLiteral("期望哈希（可选）"), QString(),
                            QStringLiteral("粘贴哈希进行比对"), QString(), false)};
        spec.order = 80;
        addTool(spec, hashFile);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.partition.extract");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("misc");
        spec.name = QStringLiteral("按偏移提取");
        spec.desc = QStringLiteral("按字节偏移 + 大小从镜像中提取任意区域");
        spec.icon = QStringLiteral("scan-line");
        spec.keywords = {QStringLiteral("偏移"), QStringLiteral("提取"), QStringLiteral("dd")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("镜像路径"), QStringLiteral("所有文件 (*)")),
                       hex(QStringLiteral("offset"), QStringLiteral("偏移"), QStringLiteral("0x0"),
                           QStringLiteral("支持 0x / 十进制 / 带 K/M/G 后缀")),
                       text(QStringLiteral("size"), QStringLiteral("大小"), QString(), QStringLiteral("64M"),
                            QStringLiteral("支持 0x / 十进制 / 带 K/M/G 后缀"), true),
                       pathField(QStringLiteral("output"), QStringLiteral("输出文件路径"))};
        spec.order = 81;
        addTool(spec, extractByOffset);
    }


    {
        ToolSpec spec;
        spec.id = QStringLiteral("image.bootlogo.pack");
        spec.category = QStringLiteral("image");
        spec.group = QStringLiteral("boot");
        spec.name = QStringLiteral("BootLogo 打包");
        spec.desc = QStringLiteral("把 PNG/JPEG 转换成 logo 分区可用的裸位图（RGB565 / BGR888 / BGRA8888）");
        spec.icon = QStringLiteral("image-plus");
        spec.keywords = {QStringLiteral("bootlogo"), QStringLiteral("开机"), QStringLiteral("打包")};
        spec.fields = {
            pathField(QStringLiteral("input"), QStringLiteral("PNG / JPEG 图片"), QStringLiteral("*.png *.jpg *.jpeg *.bmp")),
            fields::path(QStringLiteral("output"), QStringLiteral("输出 .bin（可选）"), QStringLiteral("*.bin"),
                         QString(), false),
            fields::choice(QStringLiteral("bpp"), QStringLiteral("位深"),
                           {QStringLiteral("16 (RGB565)"), QStringLiteral("24 (BGR888)"),
                            QStringLiteral("32 (BGRA8888)")}, 0),
            fields::choice(QStringLiteral("endian"), QStringLiteral("字节序"),
                           {QStringLiteral("little"), QStringLiteral("big")}, 0),
            fields::hex(QStringLiteral("padding"), QStringLiteral("前置填充字节（可选）"), QStringLiteral("0")),
        };
        addTool(spec, bootLogoPack);
    }

    appendPlannedFirmwareTools();
}

} // namespace gf
