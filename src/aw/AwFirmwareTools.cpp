#include "AwFirmware.h"

#include "TaskManager.h"
#include "ToolRegistry.h"
#include "Util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <stdexcept>

namespace gf {

namespace {

using namespace gf::alwwin;

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
    if (!in.open(QIODevice::ReadOnly) || !in.seek(offset))
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

bool loadImage(const QString& path, QByteArray* header, QByteArray* table, quint32* physicalSize,
               AwImageInfo* info, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开 %1").arg(path);
        return false;
    }
    *physicalSize = quint32(file.size());
    *header = file.read(int(kAwHeaderSize));
    AwImageInfo partial = parseAwHeader(*header);
    if (!partial.ok) {
        if (error)
            *error = partial.error;
        return false;
    }
    const qint64 tableBytes = qint64(partial.numFiles) * qint64(partial.fileHeaderLength);
    file.seek(kAwFileHeadersStart);
    *table = file.read(tableBytes);
    *info = parseAwImage(*header, *table, *physicalSize);
    if (!info->ok) {
        if (error)
            *error = info->error;
        return false;
    }
    return true;
}

void awFirmwareInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = textOption(options, QStringLiteral("input"));
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择全志 IMAGEWTY 固件");
    QByteArray header;
    QByteArray table;
    quint32 physicalSize = 0;
    AwImageInfo info;
    QString error;
    if (!loadImage(input, &header, &table, &physicalSize, &info, &error))
        throw std::runtime_error(error.toStdString());

    ctx.info(QStringLiteral("IMAGEWTY 固件 · 头部版本 0x%1 · 格式版本 %2 · USB %3:%4")
                 .arg(info.headerVersion, 0, 16)
                 .arg(info.formatVersion)
                 .arg(info.usbVendorId, 4, 16, QLatin1Char('0'))
                 .arg(info.usbProductId, 4, 16, QLatin1Char('0')));
    ctx.info(QStringLiteral("硬件 ID 0x%1 · 固件 ID 0x%2 · 总大小 %3")
                 .arg(info.hardwareId, 0, 16)
                 .arg(info.firmwareId, 0, 16)
                 .arg(util::formatBytes(physicalSize)));
    for (const AwFileEntry& entry : info.files) {
        ctx.log(1, QStringLiteral("%1 · %2/%3 · 偏移 0x%4 · %5%6")
                       .arg(entry.name, entry.mainType, entry.subType)
                       .arg(entry.offset, 0, 16)
                       .arg(util::formatBytes(entry.storedLength))
                       .arg(entry.lengthMismatch ? QStringLiteral(" · 长度字段不一致") : QString()));
    }
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("IMAGEWTY 解析完成：%1 个文件").arg(info.files.size()));
}

void awFirmwareUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = textOption(options, QStringLiteral("input"));
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择全志 IMAGEWTY 固件");
    QString outputDir = textOption(options, QStringLiteral("output"));
    if (outputDir.isEmpty())
        outputDir = QFileInfo(input).absolutePath() + QStringLiteral("/aw_firmware");
    if (!util::ensureDir(outputDir))
        throw std::runtime_error(QStringLiteral("无法创建输出目录 %1").arg(outputDir).toStdString());

    QByteArray header;
    QByteArray table;
    quint32 physicalSize = 0;
    AwImageInfo info;
    QString error;
    if (!loadImage(input, &header, &table, &physicalSize, &info, &error))
        throw std::runtime_error(error.toStdString());

    int index = 0;
    for (const AwFileEntry& entry : info.files) {
        ctx.checkCanceled();
        ++index;
        ctx.setStatus(QStringLiteral("提取 %1（%2/%3）").arg(entry.name).arg(index).arg(info.files.size()));
        const QString target = QDir(outputDir).filePath(safeName(entry.name));
        const int base = int(95.0 * (index - 1) / info.files.size());
        const int span = qMax(1, int(95.0 / info.files.size()));
        if (!copyRange(input, entry.offset, entry.storedLength, target, ctx, base, span)) {
            ctx.warn(QStringLiteral("提取失败：%1").arg(entry.name));
            continue;
        }
        ctx.ok(QStringLiteral("%1 · %2").arg(target, util::formatBytes(entry.storedLength)));
        if (entry.lengthMismatch)
            ctx.log(0, QStringLiteral("提示：%1 的存储长度与原始长度不同（%2 / %3），可能是压缩镜像")
                           .arg(entry.name).arg(entry.storedLength).arg(entry.originalLength));
    }
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("全志固件解包完成：%1 → %2").arg(input, outputDir));
}

} // namespace

void registerAwFirmwareTools()
{
    auto& registry = ToolRegistry::instance();
    {
        ToolSpec spec;
        spec.category = QStringLiteral("aw");
        spec.group = QStringLiteral("firmware");
        spec.id = QStringLiteral("aw.firmware.info");
        spec.name = QStringLiteral("全志固件解析");
        spec.desc = QStringLiteral("解析 IMAGEWTY（PhoenixSuit）固件：文件表、类型、偏移与大小");
        spec.icon = QStringLiteral("file-search");
        spec.keywords = {QStringLiteral("aw"), QStringLiteral("imag ewty"), QStringLiteral("phoenixsuit")};
        spec.fields = {
            fields::path(QStringLiteral("input"), QStringLiteral("全志 IMAGEWTY 固件"),
                         QStringLiteral("*.img *.bin *.*")),
        };
        registry.add(spec, awFirmwareInfo);
    }
    {
        ToolSpec spec;
        spec.category = QStringLiteral("aw");
        spec.group = QStringLiteral("firmware");
        spec.id = QStringLiteral("aw.firmware.unpack");
        spec.name = QStringLiteral("全志固件解包");
        spec.desc = QStringLiteral("把 IMAGEWTY 固件中的文件全部提取到目录（保留原始文件类型）");
        spec.icon = QStringLiteral("package-open");
        spec.keywords = {QStringLiteral("aw"), QStringLiteral("解包"), QStringLiteral("imag ewty")};
        spec.fields = {
            fields::path(QStringLiteral("input"), QStringLiteral("全志 IMAGEWTY 固件"),
                         QStringLiteral("*.img *.bin *.*")),
            fields::dir(QStringLiteral("output"), QStringLiteral("输出目录（可选）"), QString(), false),
        };
        registry.add(spec, awFirmwareUnpack);
    }
}

} // namespace gf
