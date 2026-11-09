#include "RkFirmware.h"

#include "TaskManager.h"
#include "ToolRegistry.h"
#include "Util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <stdexcept>

namespace gf {

namespace {

using namespace gf::rockchip;

QString textOption(const QVariantMap& options, const QString& key)
{
    return options.value(key).toString().trimmed();
}

QString safeName(const QString& name)
{
    QString out = name;
    for (const QChar ch : QStringLiteral("\\/:*?\"<>|"))
        out.replace(ch, QLatin1Char('_'));
    return out;
}

bool readRange(const QString& path, quint32 offset, quint32 length, QByteArray* out, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开 %1").arg(path);
        return false;
    }
    if (!file.seek(offset)) {
        if (error)
            *error = QStringLiteral("无法定位到偏移 0x%1").arg(offset, 0, 16);
        return false;
    }
    *out = file.read(length);
    return out->size() == int(length);
}

bool copyRange(const QString& path, quint32 offset, quint32 length, const QString& output,
               TaskContext& ctx, int progressBase, int progressSpan)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    if (!in.seek(offset))
        return false;
    QFile out(output);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    quint32 remaining = length;
    constexpr quint32 chunk = 4 * 1024 * 1024;
    while (remaining > 0) {
        ctx.checkCanceled();
        const quint32 take = qMin(chunk, remaining);
        const QByteArray data = in.read(take);
        if (data.size() != int(take))
            return false;
        out.write(data);
        remaining -= take;
        if (length > 0)
            ctx.setProgress(progressBase + int(progressSpan * (1.0 - double(remaining) / double(length))));
    }
    return true;
}

void rkFirmwareInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = textOption(options, QStringLiteral("input"));
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择 Rockchip update.img / RKFW 固件");

    QFile file(input);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(QStringLiteral("无法打开 %1").arg(input).toStdString());
    const QByteArray head = file.read(0x40);
    file.close();
    if (head.startsWith(QByteArrayLiteral("RKFW"))) {
        QByteArray header;
        QString error;
        if (!readRange(input, 0, kRkfwHeaderPrefix, &header, &error))
            throw std::runtime_error(error.toStdString());
        const RkfwInfo fw = parseRkfwHeader(header);
        if (!fw.ok)
            throw std::runtime_error(fw.error.toStdString());
        ctx.info(QStringLiteral("RKFW 固件 · 版本 %1 · 芯片 %2").arg(fw.version, fw.chipName));
        ctx.info(QStringLiteral("构建时间 %1-%2-%3 %4:%5:%6")
                     .arg(fw.year).arg(fw.month, 2, 10, QLatin1Char('0'))
                     .arg(fw.day, 2, 10, QLatin1Char('0')).arg(fw.hour, 2, 10, QLatin1Char('0'))
                     .arg(fw.minute, 2, 10, QLatin1Char('0')).arg(fw.second, 2, 10, QLatin1Char('0')));
        ctx.info(QStringLiteral("BOOT 区：0x%1 · %2").arg(fw.bootOffset, 0, 16)
                     .arg(util::formatBytes(fw.bootSize)));
        QByteArray rkafHeader;
        if (readRange(input, fw.updateOffset, kRkafHeaderSize, &rkafHeader, &error)) {
            const RkafInfo info = parseRkafHeader(rkafHeader);
            if (info.ok) {
                ctx.info(QStringLiteral("update.img：%1 · 厂商 %2 · 版本 %3 · %4 个分区")
                             .arg(info.model, info.manufacturer).arg(info.version).arg(info.partitions.size()));
                for (const RkafPartition& part : info.partitions) {
                    ctx.log(1, QStringLiteral("%1 · %2 · 偏移 0x%3 · 大小 %4")
                                   .arg(part.name, part.path)
                                   .arg(part.partOffset, 0, 16)
                                   .arg(util::formatBytes(part.byteCount)));
                }
            } else {
                ctx.warn(QStringLiteral("嵌入的 RKAF 解析失败：%1").arg(info.error));
            }
        } else {
            ctx.warn(QStringLiteral("无法读取嵌入的 RKAF：%1").arg(error));
        }
        ctx.setProgress(100);
        ctx.ok(QStringLiteral("RKFW 固件解析完成"));
        return;
    }

    if (head.startsWith(QByteArrayLiteral("RKAF"))) {
        QByteArray header;
        QString error;
        if (!readRange(input, 0, kRkafHeaderSize, &header, &error))
            throw std::runtime_error(error.toStdString());
        const RkafInfo info = parseRkafHeader(header);
        if (!info.ok)
            throw std::runtime_error(info.error.toStdString());
        ctx.info(QStringLiteral("型号 %1 · 厂商 %2 · 版本 %3 · %4 个分区")
                     .arg(info.model, info.manufacturer).arg(info.version).arg(info.partitions.size()));
        for (const RkafPartition& part : info.partitions) {
            ctx.log(1, QStringLiteral("%1 · %2 · 偏移 0x%3 · 大小 %4")
                           .arg(part.name, part.path)
                           .arg(part.partOffset, 0, 16)
                           .arg(util::formatBytes(part.byteCount)));
        }
        ctx.setProgress(100);
        ctx.ok(QStringLiteral("RKAF 固件解析完成"));
        return;
    }

    throw std::runtime_error("不是 Rockchip 固件：文件头既不是 RKFW 也不是 RKAF");
}

void rkFirmwareUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = textOption(options, QStringLiteral("input"));
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择 Rockchip update.img / RKFW 固件");
    QString outputDir = textOption(options, QStringLiteral("output"));
    if (outputDir.isEmpty())
        outputDir = QFileInfo(input).absolutePath() + QStringLiteral("/rk_firmware");
    if (!util::ensureDir(outputDir))
        throw std::runtime_error(QStringLiteral("无法创建输出目录 %1").arg(outputDir).toStdString());

    QFile file(input);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(QStringLiteral("无法打开 %1").arg(input).toStdString());
    const QByteArray head = file.read(0x40);
    file.close();

    quint32 rkafBase = 0;
    QString model;
    if (head.startsWith(QByteArrayLiteral("RKFW"))) {
        QByteArray header;
        QString error;
        if (!readRange(input, 0, kRkfwHeaderPrefix, &header, &error))
            throw std::runtime_error(error.toStdString());
        const RkfwInfo fw = parseRkfwHeader(header);
        if (!fw.ok)
            throw std::runtime_error(fw.error.toStdString());
        rkafBase = fw.updateOffset;
        if (fw.bootSize > 0) {
            const QString bootPath = QDir(outputDir).filePath(QStringLiteral("BOOT.bin"));
            ctx.setStatus(QStringLiteral("提取 BOOT…"));
            if (copyRange(input, fw.bootOffset, fw.bootSize, bootPath, ctx, 0, 10))
                ctx.ok(QStringLiteral("已提取 BOOT → %1").arg(bootPath));
            else
                ctx.warn(QStringLiteral("BOOT 提取失败"));
        }
    } else if (!head.startsWith(QByteArrayLiteral("RKAF"))) {
        throw std::runtime_error("不是 Rockchip 固件：文件头既不是 RKFW 也不是 RKAF");
    }

    QByteArray rkafHeader;
    QString error;
    if (!readRange(input, rkafBase, kRkafHeaderSize, &rkafHeader, &error))
        throw std::runtime_error(error.toStdString());
    const RkafInfo info = parseRkafHeader(rkafHeader);
    if (!info.ok)
        throw std::runtime_error(info.error.toStdString());
    if (info.partitions.isEmpty())
        throw std::runtime_error("固件中没有可提取的分区");

    quint64 totalBytes = 0;
    for (const RkafPartition& part : info.partitions)
        totalBytes += part.byteCount;

    int index = 0;
    for (const RkafPartition& part : info.partitions) {
        ctx.checkCanceled();
        ++index;
        ctx.setStatus(QStringLiteral("提取 %1（%2/%3）").arg(part.name).arg(index).arg(info.partitions.size()));
        QString name = safeName(part.path.isEmpty() ? part.name : part.path);
        if (!name.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
            name += QStringLiteral(".img");
        const QString target = QDir(outputDir).filePath(name);
        const int base = 10 + int(88.0 * (index - 1) / info.partitions.size());
        const int span = qMax(1, int(88.0 / info.partitions.size()));
        if (!copyRange(input, rkafBase + part.partOffset, part.byteCount, target, ctx, base, span)) {
            ctx.warn(QStringLiteral("提取失败：%1（偏移 0x%2）")
                         .arg(part.name)
                         .arg(rkafBase + part.partOffset, 0, 16));
            continue;
        }
        ctx.ok(QStringLiteral("%1 · %2").arg(name, util::formatBytes(part.byteCount)));
    }
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("RK 固件解包完成：%1 → %2").arg(input, outputDir));
}

} // namespace

void registerRkFirmwareTools()
{
    auto& registry = ToolRegistry::instance();
    {
        ToolSpec spec;
        spec.category = QStringLiteral("rk");
        spec.group = QStringLiteral("firmware");
        spec.id = QStringLiteral("rk.firmware.info");
        spec.name = QStringLiteral("RK 固件解析");
        spec.desc = QStringLiteral("解析 Rockchip update.img / RKFW 固件：型号、芯片、分区与偏移");
        spec.icon = QStringLiteral("file-search");
        spec.keywords = {QStringLiteral("rk"), QStringLiteral("update.img"), QStringLiteral("rkfw"),
                         QStringLiteral("rkaf")};
        spec.fields = {
            fields::path(QStringLiteral("input"), QStringLiteral("Rockchip 固件"),
                         QStringLiteral("*.img *.bin *.*")),
        };
        registry.add(spec, rkFirmwareInfo);
    }
    {
        ToolSpec spec;
        spec.category = QStringLiteral("rk");
        spec.group = QStringLiteral("firmware");
        spec.id = QStringLiteral("rk.firmware.unpack");
        spec.name = QStringLiteral("RK 固件解包");
        spec.desc = QStringLiteral("解包 RKFW / RKAF 固件为镜像文件（BOOT.bin 与各分区 img）");
        spec.icon = QStringLiteral("package-open");
        spec.keywords = {QStringLiteral("rk"), QStringLiteral("解包"), QStringLiteral("update.img")};
        spec.fields = {
            fields::path(QStringLiteral("input"), QStringLiteral("Rockchip 固件"),
                         QStringLiteral("*.img *.bin *.*")),
            fields::dir(QStringLiteral("output"), QStringLiteral("输出目录（可选）"), QString(), false),
        };
        registry.add(spec, rkFirmwareUnpack);
    }
}

} // namespace gf
