#include "RootImage.h"

#include <QCryptographicHash>
#include <QSet>

#include <algorithm>

#include "BootImage.h"
#include "Cpio.h"
#include "Compression.h"
#include "ExtrasUtils.h"

namespace gf::extras::root {

namespace {

QString normalizedName(const QString& name)
{
    QString normalized = name;
    while (normalized.startsWith(QStringLiteral("./")))
        normalized.remove(0, 2);
    while (normalized.startsWith(QLatin1Char('/')))
        normalized.remove(0, 1);
    return normalized;
}

/// Android ramdisk 里的 cpio 条目有的写成 "init"、有的写成 "./init"。保持
/// 原始风格，避免修补后同时出现两个名称不同的 init 条目。
QString archivePrefix(const gf::img::cpio::Archive& archive, const QString& hint = {})
{
    if (hint.startsWith(QStringLiteral("./")))
        return QStringLiteral("./");
    if (!hint.isEmpty())
        return {};
    for (const auto& entry : archive.entries) {
        if (entry.name.startsWith(QStringLiteral("./")))
            return QStringLiteral("./");
        if (!entry.name.isEmpty())
            return {};
    }
    return {};
}

QString styleName(const QString& prefix, const QString& relative)
{
    return prefix + relative;
}

bool hasEntry(const gf::img::cpio::Archive& archive, const QString& name)
{
    for (const auto& entry : archive.entries) {
        if (normalizedName(entry.name).compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

quint32 nextInode(gf::img::cpio::Archive& archive)
{
    quint32 maximum = 0;
    for (const auto& entry : archive.entries)
        maximum = std::max(maximum, entry.ino);
    return maximum + 1;
}

void ensureDirectoryEntry(gf::img::cpio::Archive& archive, const QString& directory)
{
    const QString name = normalizedName(directory);
    if (name.isEmpty() || hasEntry(archive, name))
        return;
    gf::img::cpio::Entry entry;
    entry.name = directory;
    entry.ino = nextInode(archive);
    entry.mode = 0040755u;
    entry.nlink = 2;
    archive.entries.append(entry);
}

void ensureParentDirectories(gf::img::cpio::Archive& archive, const QString& prefix,
                             const QString& path)
{
    const QString normalized = normalizedName(path);
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString current;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        if (!current.isEmpty())
            current += QLatin1Char('/');
        current += parts.at(i);
        ensureDirectoryEntry(archive, styleName(prefix, current));
    }
}

const gf::img::cpio::Entry* findEntry(const gf::img::cpio::Archive& archive, const QString& name)
{
    const QString wanted = normalizedName(name);
    for (const auto& entry : archive.entries) {
        if (normalizedName(entry.name).compare(wanted, Qt::CaseInsensitive) == 0)
            return &entry;
    }
    return nullptr;
}

} // namespace

QString serializePropLines(const QMap<QString, QString>& values, const QStringList& order)
{
    QStringList lines;
    QSet<QString> emitted;
    for (const QString& key : order) {
        if (!values.contains(key) || emitted.contains(key))
            continue;
        emitted.insert(key);
        lines.append(QStringLiteral("%1=%2").arg(key, values.value(key)));
    }
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (emitted.contains(it.key()))
            continue;
        lines.append(QStringLiteral("%1=%2").arg(it.key(), it.value()));
    }
    return lines.isEmpty() ? QString() : lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QMap<QString, QString> parsePropLines(const QString& text)
{
    return parseKeyValueText(text);
}

QByteArray patchRamdisk(const QByteArray& plainCpio, const PatchOptions& options,
                        PatchOutcome* outcome)
{
    if (plainCpio.isEmpty())
        fail(QStringLiteral("ramdisk 为空，无法修补。"));
    if (options.payload.isEmpty())
        fail(QStringLiteral("没有提供要注入的 %1 内容。").arg(options.payloadLabel));

    gf::img::cpio::Archive archive;
    try {
        archive = gf::img::cpio::parse(plainCpio);
    } catch (const std::exception& exception) {
        fail(QStringLiteral("ramdisk cpio 解析失败：%1").arg(QString::fromUtf8(exception.what())));
    }

    PatchOutcome local;
    const gf::img::cpio::Entry* init = findEntry(archive, QStringLiteral("init"));
    const QString prefix = archivePrefix(archive, init ? init->name : QString());
    const QString initName = init ? init->name : styleName(prefix, QStringLiteral("init"));
    const QByteArray initData = init ? init->data : QByteArray();
    const quint32 initMode = init ? (init->mode == 0 ? 0100750u : init->mode) : 0100750u;
    if (init) {
        local.initSha1 = QString::fromLatin1(
            QCryptographicHash::hash(initData, QCryptographicHash::Sha1).toHex());
        if (options.backupInit) {
            ensureDirectoryEntry(archive, styleName(prefix, QStringLiteral(".backup")));
            gf::img::cpio::putFile(archive,
                                   styleName(prefix, QStringLiteral(".backup/init")),
                                   initData, initMode);
            local.changes.append(QStringLiteral("备份原始 init → .backup/init（%1 字节）")
                                     .arg(initData.size()));
        }
        gf::img::cpio::putFile(archive, initName, options.payload, 0100750u);
        local.changes.append(QStringLiteral("注入 %1 → init（%2 字节，模式 0750）")
                                 .arg(options.payloadLabel)
                                 .arg(options.payload.size()));
    } else {
        gf::img::cpio::putFile(archive, initName, options.payload, 0100750u);
        local.changes.append(QStringLiteral("ramdisk 中没有 init，直接新增 %1 → init（%2 字节）")
                                 .arg(options.payloadLabel)
                                 .arg(options.payload.size()));
    }

    if (!options.magiskConfig.isEmpty()) {
        ensureDirectoryEntry(archive, styleName(prefix, QStringLiteral(".backup")));
        gf::img::cpio::putFile(archive,
                               styleName(prefix, QStringLiteral(".backup/.magisk")),
                               options.magiskConfig, 0100000u);
        local.changes.append(QStringLiteral("写入 .backup/.magisk 配置"));
    }

    for (const InjectFile& file : options.extraFiles) {
        ensureParentDirectories(archive, prefix, file.path);
        gf::img::cpio::putFile(archive, styleName(prefix, normalizedName(file.path)), file.data,
                               file.mode);
        local.changes.append(QStringLiteral("注入 %1（%2 字节）").arg(file.path).arg(file.data.size()));
    }

    local.patchedRamdisk = gf::img::cpio::serialize(archive);
    if (outcome) {
        const QStringList changes = local.changes;
        const QString initSha1 = local.initSha1;
        *outcome = local;
        outcome->changes = changes;
        outcome->initSha1 = initSha1;
    }
    return local.patchedRamdisk;
}

QByteArray applyPropertyOverrides(const QByteArray& plainCpio, const QStringList& targetFiles,
                                  const QMap<QString, QString>& overrides, QStringList* changes,
                                  QString* error)
{
    if (targetFiles.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有指定要修改的属性文件。");
        return {};
    }
    gf::img::cpio::Archive archive;
    try {
        archive = gf::img::cpio::parse(plainCpio);
    } catch (const std::exception& exception) {
        if (error)
            *error = QStringLiteral("ramdisk cpio 解析失败：%1")
                         .arg(QString::fromUtf8(exception.what()));
        return {};
    }

    QStringList applied;
    const QString prefix = archivePrefix(archive);
    for (const QString& target : targetFiles) {
        const gf::img::cpio::Entry* entry = findEntry(archive, target);
        QString existing;
        QStringList order;
        quint32 mode = 0100644u;
        QString entryName = styleName(prefix, normalizedName(target));
        if (entry) {
            existing = QString::fromUtf8(entry->data);
            mode = entry->mode == 0 ? 0100644u : entry->mode;
            entryName = entry->name;
        }
        QMap<QString, QString> values = parseKeyValueText(existing, &order);
        for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
            if (!values.contains(it.key()))
                order.append(it.key());
            values.insert(it.key(), it.value());
        }
        const QByteArray data = serializePropLines(values, order).toUtf8();
        ensureParentDirectories(archive, prefix, target);
        gf::img::cpio::putFile(archive, entryName, data, mode);
        applied.append(QStringLiteral("%1（%2 项）").arg(target).arg(overrides.size()));
    }
    if (changes)
        *changes = applied;
    if (error)
        error->clear();
    return gf::img::cpio::serialize(archive);
}

QByteArray decompressRamdiskAuto(const QByteArray& ramdisk, QString* format, lz4::Frame* frame)
{
    if (ramdisk.isEmpty())
        fail(QStringLiteral("ramdisk 为空。"));
    const lz4::Frame lz4Frame = lz4::detect(ramdisk);
    if (lz4Frame == lz4::Frame::Modern || lz4Frame == lz4::Frame::Legacy) {
        if (format)
            *format = QStringLiteral("lz4");
        if (frame)
            *frame = lz4Frame;
        return lz4::decompress(ramdisk);
    }
    if (gf::img::boot::isCpio(ramdisk)) {
        if (format)
            *format = QStringLiteral("cpio");
        if (frame)
            *frame = lz4::Frame::Unknown;
        return ramdisk;
    }
    const gf::img::compress::Format detected = gf::img::compress::detect(ramdisk);
    if (detected == gf::img::compress::Format::Gzip) {
        if (format)
            *format = QStringLiteral("gzip");
        if (frame)
            *frame = lz4::Frame::Unknown;
        return gf::img::compress::gzipDecompress(ramdisk);
    }
    if (detected == gf::img::compress::Format::Lzop) {
        if (format)
            *format = QStringLiteral("lzo");
        if (frame)
            *frame = lz4::Frame::Unknown;
        return gf::img::compress::lzopDecompress(ramdisk);
    }
    fail(QStringLiteral("不支持的 ramdisk 压缩格式：%1（支持 gzip / lzop / lz4 / 裸 cpio）")
             .arg(gf::img::compress::formatName(detected)));
}

QByteArray compressRamdiskLike(const QByteArray& plain, const QString& format, lz4::Frame frame)
{
    const QString id = format.trimmed().toLower();
    if (id.isEmpty() || id == QLatin1String("cpio"))
        return plain;
    if (id == QLatin1String("gzip"))
        return gf::img::compress::gzipCompress(plain);
    if (id == QLatin1String("lzo") || id == QLatin1String("lzop"))
        return gf::img::compress::lzopCompress(plain);
    if (id == QLatin1String("lz4"))
        return lz4::compress(plain, frame == lz4::Frame::Unknown ? lz4::Frame::Modern : frame);
    fail(QStringLiteral("无法按格式 %1 重新打包 ramdisk。").arg(format));
}

QByteArray patchBootImage(const QByteArray& bootData, const PatchOptions& options,
                          PatchOutcome* outcome)
{
    gf::img::boot::Image image;
    try {
        image = gf::img::boot::parse(bootData);
    } catch (const std::exception& exception) {
        fail(QStringLiteral("boot 镜像解析失败：%1").arg(QString::fromUtf8(exception.what())));
    }
    if (image.kind == gf::img::boot::ImageKind::VendorBoot)
        fail(QStringLiteral("vendor_boot 镜像的分段 ramdisk 修补暂不支持，请改用 boot / init_boot。"));
    if (image.ramdisk.isEmpty())
        fail(QStringLiteral("该镜像没有 ramdisk（可能是仅含内核的 boot 镜像）。"
                            "Android 13+ 设备请使用 init_boot 镜像修补。"));

    QString format;
    lz4::Frame frame = lz4::Frame::Unknown;
    const QByteArray plain = decompressRamdiskAuto(image.ramdisk, &format, &frame);
    if (!gf::img::boot::isCpio(plain))
        fail(QStringLiteral("ramdisk 解压后不是 cpio 归档，无法修补。"));

    PatchOutcome local;
    local.patchedRamdisk = patchRamdisk(plain, options, &local);
    local.ramdiskFormat = format;
    local.lz4Frame = frame;
    const QByteArray newRamdisk = compressRamdiskLike(local.patchedRamdisk, format, frame);
    image.ramdisk = newRamdisk;
    local.patchedBoot = gf::img::boot::serialize(image);
    local.changes.append(QStringLiteral("ramdisk 用 %1 重新打包（%2 → %3 字节）")
                             .arg(format)
                             .arg(plain.size())
                             .arg(newRamdisk.size()));
    if (outcome) {
        const QStringList changes = local.changes;
        const QByteArray patchedBoot = local.patchedBoot;
        *outcome = local;
        outcome->changes = changes;
        outcome->patchedBoot = patchedBoot;
    }
    return local.patchedBoot;
}

} // namespace gf::extras::root
