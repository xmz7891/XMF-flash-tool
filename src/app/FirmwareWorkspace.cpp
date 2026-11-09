#include "FirmwareWorkspace.h"

#include "Util.h"
#include "device/AdbClient.h"
#include "device/FastbootClient.h"
#include "image/Compression.h"
#include "image/Gpt.h"
#include "mtk/Scatter.h"
#include "aw/AwFirmware.h"
#include "rk/RkFirmware.h"
#include "sprd/Pac.h"

#include <QFile>
#include <QFileInfo>

namespace gf {

namespace {

QVariantMap emptyResult(const QString& error)
{
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("error"), error);
    result.insert(QStringLiteral("partitions"), QVariantList());
    return result;
}

QVariantMap resultWith(const QString& kind, const QString& title, const QString& subtitle,
                       const QVariantList& partitions)
{
    QVariantMap result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("kind"), kind);
    result.insert(QStringLiteral("title"), title);
    result.insert(QStringLiteral("subtitle"), subtitle);
    result.insert(QStringLiteral("partitions"), partitions);
    return result;
}

QVariantMap partitionRow(const QString& name, quint64 sizeBytes, const QString& address,
                         const QString& file, const QString& region, bool checked,
                         const QString& detail = {})
{
    QVariantMap row;
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("sizeBytes"), double(sizeBytes));
    row.insert(QStringLiteral("sizeText"), sizeBytes > 0 ? util::formatBytes(qint64(sizeBytes))
                                                         : QStringLiteral("—"));
    row.insert(QStringLiteral("address"), address);
    row.insert(QStringLiteral("file"), file);
    row.insert(QStringLiteral("region"), region);
    row.insert(QStringLiteral("checked"), checked);
    row.insert(QStringLiteral("detail"), detail);
    return row;
}

struct TarEntry {
    QString name;
    quint64 size = 0;
    char type = '0';
};

quint64 parseOctal(const QByteArray& data)
{
    quint64 value = 0;
    for (char c : data) {
        if (c < '0' || c > '7')
            break;
        value = value * 8 + quint64(c - '0');
    }
    return value;
}

QString tarString(const QByteArray& header, int offset, int length)
{
    QByteArray raw = header.mid(offset, length);
    const int zero = raw.indexOf('\0');
    if (zero >= 0)
        raw.truncate(zero);
    return QString::fromUtf8(raw);
}

QList<TarEntry> parseTar(const QByteArray& data)
{
    QList<TarEntry> entries;
    qsizetype pos = 0;
    while (pos + 512 <= data.size()) {
        const QByteArray header = data.mid(pos, 512);
        QString name = tarString(header, 0, 100);
        if (name.isEmpty())
            break;
        const QString magic = tarString(header, 257, 8);
        const QString prefix = tarString(header, 345, 155);
        if (!prefix.isEmpty())
            name = prefix + QLatin1Char('/') + name;
        TarEntry entry;
        entry.name = name;
        entry.size = parseOctal(header.mid(124, 12));
        entry.type = header.size() > 156 ? header.at(156) : '0';
        entries.append(entry);
        pos += 512 + qsizetype((entry.size + 511) / 512) * 512;
        if (magic.isEmpty() && entry.size == 0 && entries.size() > 1)
            break;
    }
    return entries;
}

QVariantMap fastbootResult(const QString& serial)
{
    QString error;
    QString resolved = serial;
    if (resolved.isEmpty()) {
        resolved = device::FastbootClient::resolveSerial(QString(), &error);
        if (resolved.isEmpty())
            return emptyResult(error);
    }
    QMap<QString, QString> vars = device::FastbootClient::getvarAll(resolved, &error);
    const QList<device::FastbootPartition> parts = device::FastbootClient::partitions(resolved, &error);
    if (parts.isEmpty())
        return emptyResult(error.isEmpty() ? QStringLiteral("设备没有返回分区信息") : error);

    QVariantList rows;
    rows.reserve(parts.size());
    for (const device::FastbootPartition& part : parts) {
        bool ok = false;
        const quint64 bytes = part.size.toULongLong(&ok, 0);
        QVariantMap row = partitionRow(part.name, ok ? bytes : 0, part.size, QString(), part.type,
                                       true, part.slot.isEmpty() ? QString() : (QStringLiteral("slot ") + part.slot));
        rows.append(row);
    }
    const QString product = vars.value(QStringLiteral("product"));
    const QString slot = vars.value(QStringLiteral("current-slot"));
    const QString unlocked = vars.value(QStringLiteral("unlocked"));
    const QString subtitle = QStringLiteral("Fastboot · %1%2%3 · %4 个分区")
                                 .arg(product.isEmpty() ? resolved : product)
                                 .arg(slot.isEmpty() ? QString() : QStringLiteral(" · 槽位 ") + slot)
                                 .arg(unlocked.isEmpty() ? QString()
                                                         : QStringLiteral(" · ") + (unlocked == QLatin1String("yes")
                                                                                       ? QStringLiteral("已解锁")
                                                                                       : QStringLiteral("未解锁")))
                                 .arg(rows.size());
    return resultWith(QStringLiteral("fastboot"), QStringLiteral("Fastboot 分区表"), subtitle, rows);
}

} // namespace

FirmwareWorkspace::FirmwareWorkspace(QObject* parent)
    : QObject(parent)
{
}

QVariantMap FirmwareWorkspace::loadScatter(const QString& path)
{
    if (!QFileInfo::exists(path))
        return emptyResult(QStringLiteral("文件不存在: ") + path);
    QString ioError;
    const mtk::ScatterInfo info = mtk::ScatterInfo::parseFile(path, &ioError);
    if (!info.valid)
        return emptyResult(info.error.isEmpty() ? ioError : info.error);

    QVariantList partitions;
    partitions.reserve(info.partitions.size());
    for (const mtk::ScatterPartition& part : info.partitions) {
        if (part.name.isEmpty())
            continue;
        QVariantMap row = partitionRow(part.name, part.size,
                                       QStringLiteral("0x%1").arg(part.physicalStart, 0, 16),
                                       part.fileName, part.region, part.isDownload,
                                       part.type);
        partitions.append(row);
    }
    const QString subtitle = QStringLiteral("平台 %1 · 工程 %2 · %3 · %4 个分区")
                                 .arg(info.platform.isEmpty() ? QStringLiteral("未知") : info.platform,
                                      info.project.isEmpty() ? QStringLiteral("未知") : info.project,
                                      info.storage.isEmpty() ? QStringLiteral("未知存储") : info.storage)
                                 .arg(partitions.size());
    return resultWith(QStringLiteral("mtk-scatter"), QStringLiteral("Scatter 分区表"), subtitle,
                      partitions);
}

QVariantMap FirmwareWorkspace::loadPac(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists())
        return emptyResult(QStringLiteral("文件不存在: ") + path);
    if (info.size() > 3LL * 1024 * 1024 * 1024)
        return emptyResult(QStringLiteral("PAC 超过 3GB，暂不支持在界面内解析，请使用 SPD 模式直接刷写"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return emptyResult(QStringLiteral("无法打开 PAC: ") + path);
    const QByteArray data = file.readAll();
    file.close();

    const sprd::PacInfo pac = sprd::PacInfo::parse(data);
    if (!pac.valid)
        return emptyResult(pac.error.isEmpty() ? QStringLiteral("PAC 结构无法识别") : pac.error);

    QVariantList partitions;
    partitions.reserve(pac.images.size());
    for (const sprd::PacImage& image : pac.images) {
        QVariantMap row = partitionRow(image.name, image.size,
                                       QStringLiteral("0x%1").arg(image.offset, 0, 16), QString(),
                                       sprd::pacImageTypeName(image.type), true);
        partitions.append(row);
    }
    const QString subtitle = QStringLiteral("PAC v%1 · %2 个镜像").arg(pac.version).arg(partitions.size());
    return resultWith(QStringLiteral("sprd-pac"), QStringLiteral("PAC 镜像表"), subtitle, partitions);
}

QVariantMap FirmwareWorkspace::loadMiFlashPackage(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists())
        return emptyResult(QStringLiteral("文件不存在: ") + path);
    if (!info.fileName().endsWith(QStringLiteral(".tgz"), Qt::CaseInsensitive)
        && !info.fileName().endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive))
        return emptyResult(QStringLiteral("请选择 .tgz 格式的小米线刷包"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return emptyResult(QStringLiteral("无法打开固件包: ") + path);
    // Guard against pathological files: tar listing requires inflating the stream.
    if (file.size() > 2LL * 1024 * 1024 * 1024)
        return emptyResult(QStringLiteral("固件包超过 2GB，请直接使用 MiFlash 模式刷写"));
    const QByteArray compressed = file.readAll();
    file.close();

    QByteArray tar;
    if (img::compress::isGzip(compressed))
        tar = img::compress::gzipDecompress(compressed);
    else if (compressed.size() > 512 && compressed.mid(257, 5) == QByteArrayLiteral("ustar"))
        tar = compressed;
    if (tar.isEmpty())
        return emptyResult(QStringLiteral("无法解压固件包，文件可能已损坏"));

    const QList<TarEntry> entries = parseTar(tar);
    QVariantList partitions;
    for (const TarEntry& entry : entries) {
        if (entry.type != '0' && entry.type != '7')
            continue;
        if (!entry.name.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
            continue;
        const QString base = QFileInfo(entry.name).completeBaseName();
        QVariantMap row = partitionRow(base, entry.size, QString(), entry.name, QString(), true);
        partitions.append(row);
    }
    if (partitions.isEmpty())
        return emptyResult(QStringLiteral("固件包中没有找到 .img 镜像"));

    const QString subtitle = QStringLiteral("线刷包 · %1 个镜像 · %2")
                                 .arg(partitions.size())
                                 .arg(util::formatBytes(info.size()));
    return resultWith(QStringLiteral("miflash-tgz"), QStringLiteral("线刷包镜像"), subtitle, partitions);
}

QVariantMap FirmwareWorkspace::loadRkFirmware(const QString& path)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists())
        return emptyResult(QStringLiteral("文件不存在：") + path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return emptyResult(QStringLiteral("无法打开固件：") + path);
    const QByteArray head = file.read(0x40);
    quint32 rkafBase = 0;
    QString extra;
    if (head.startsWith(QByteArrayLiteral("RKFW"))) {
        if (!file.seek(0)) {
            return emptyResult(QStringLiteral("读取 RKFW 头失败"));
        }
        const QByteArray header = file.read(int(rockchip::kRkfwHeaderPrefix));
        const rockchip::RkfwInfo info = rockchip::parseRkfwHeader(header);
        if (!info.ok)
            return emptyResult(info.error);
        rkafBase = info.updateOffset;
        extra = QStringLiteral("RKFW %1 · %2").arg(info.version, info.chipName);
    } else if (!head.startsWith(QByteArrayLiteral("RKAF"))) {
        return emptyResult(QStringLiteral("不是 Rockchip 固件（缺少 RKFW / RKAF 签名）"));
    }

    if (!file.seek(rkafBase)) {
        file.close();
        return emptyResult(QStringLiteral("无法定位嵌入式 RKAF（偏移 0x%1）").arg(rkafBase, 0, 16));
    }
    const QByteArray rkafHeader = file.read(int(rockchip::kRkafHeaderSize));
    file.close();
    const rockchip::RkafInfo info = rockchip::parseRkafHeader(rkafHeader);
    if (!info.ok)
        return emptyResult(info.error);
    if (info.partitions.isEmpty())
        return emptyResult(QStringLiteral("固件中没有可用的分区镜像"));

    QVariantList rows;
    rows.reserve(info.partitions.size());
    for (const rockchip::RkafPartition& part : info.partitions) {
        const QVariantMap row =
            partitionRow(part.name, part.byteCount,
                         QStringLiteral("0x%1").arg(part.flashOffset, 0, 16),
                         part.path.isEmpty() ? part.name : part.path, QString(),
                         true, QStringLiteral("固件内偏移 +0x%1").arg(part.partOffset, 0, 16));
        rows.append(row);
    }
    QString subtitle = QStringLiteral("%1 · %2 个分区 · %3")
                           .arg(info.model.isEmpty() ? QStringLiteral("RK 固件") : info.model)
                           .arg(rows.size())
                           .arg(extra.isEmpty() ? QStringLiteral("RKAF") : extra);
    if (!info.manufacturer.isEmpty())
        subtitle += QStringLiteral(" · ") + info.manufacturer;
    return resultWith(QStringLiteral("rk-firmware"), QStringLiteral("RK 固件镜像"), subtitle, rows);
}

QVariantMap FirmwareWorkspace::loadAwFirmware(const QString& path)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists())
        return emptyResult(QStringLiteral("文件不存在：") + path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return emptyResult(QStringLiteral("无法打开固件：") + path);
    const quint32 physicalSize = quint32(file.size());
    const QByteArray header = file.read(int(alwwin::kAwHeaderSize));
    const alwwin::AwImageInfo partial = alwwin::parseAwHeader(header);
    if (!partial.ok) {
        file.close();
        return emptyResult(partial.error);
    }
    QByteArray table;
    file.seek(alwwin::kAwFileHeadersStart);
    table = file.read(qint64(partial.numFiles) * qint64(partial.fileHeaderLength));
    file.close();
    const alwwin::AwImageInfo info = alwwin::parseAwImage(header, table, physicalSize);
    if (!info.ok)
        return emptyResult(info.error);
    if (info.files.isEmpty())
        return emptyResult(QStringLiteral("固件中没有文件条目"));

    QVariantList rows;
    rows.reserve(info.files.size());
    for (const alwwin::AwFileEntry& entry : info.files) {
        const QVariantMap row =
            partitionRow(entry.name, entry.storedLength,
                         QStringLiteral("0x%1").arg(entry.offset, 0, 16), entry.name,
                         entry.mainType, true,
                         entry.lengthMismatch ? QStringLiteral("长度字段不一致") : entry.subType);
        rows.append(row);
    }
    const QString subtitle = QStringLiteral("IMAGEWTY v%1 · %2 个文件 · HW 0x%3 · 总大小 %4")
                                 .arg(info.headerVersion)
                                 .arg(rows.size())
                                 .arg(info.hardwareId, 0, 16)
                                 .arg(util::formatBytes(physicalSize));
    return resultWith(QStringLiteral("aw-firmware"), QStringLiteral("全志固件镜像"), subtitle, rows);
}

QVariantMap FirmwareWorkspace::loadGptImage(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists())
        return emptyResult(QStringLiteral("文件不存在: ") + path);
    if (info.size() > 256LL * 1024 * 1024)
        return emptyResult(QStringLiteral("仅支持读取 256MB 以内的镜像分区表"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return emptyResult(QStringLiteral("无法打开镜像: ") + path);
    const QByteArray data = file.readAll();
    file.close();

    const img::gpt::Table table = img::gpt::parse(data);
    if (!table.primary.present && !table.backup.present)
        return emptyResult(QStringLiteral("镜像中没有 GPT 分区表"));

    QVariantList partitions;
    for (int i = 0; i < table.entries.size(); ++i) {
        const img::gpt::Entry& entry = table.entries.at(i);
        if (entry.isEmpty())
            continue;
        const quint64 bytes = entry.sectorCount() * table.sectorSize;
        const QString address = QStringLiteral("0x%1").arg(img::gpt::partitionOffsetBytes(table, i), 0, 16);
        QVariantMap row = partitionRow(entry.name, bytes, address, QString(), QString(), true,
                                       entry.typeGuid());
        partitions.append(row);
    }
    const QString subtitle = QStringLiteral("%1 个分区 · LBA 扇区 %2 字节")
                                 .arg(partitions.size())
                                 .arg(table.sectorSize);
    return resultWith(QStringLiteral("gpt"), QStringLiteral("GPT 分区表"), subtitle, partitions);
}

QVariantMap FirmwareWorkspace::loadAdbPartitions(const QString& serial)
{
    QString error;
    QString resolved = serial;
    if (resolved.isEmpty()) {
        resolved = device::AdbClient::resolveSerial(QString(), &error);
        if (resolved.isEmpty())
            return emptyResult(error);
    }
    const QList<device::PartitionLink> links = device::AdbClient::partitions(resolved, &error);
    if (links.isEmpty())
        return emptyResult(error.isEmpty() ? QStringLiteral("设备没有返回分区信息") : error);

    QVariantList partitions;
    partitions.reserve(links.size());
    for (const device::PartitionLink& link : links) {
        QVariantMap row = partitionRow(link.name, 0, QString(), link.target, QString(), false);
        partitions.append(row);
    }
    const QString subtitle = QStringLiteral("设备 %1 · %2 个分区").arg(resolved).arg(partitions.size());
    return resultWith(QStringLiteral("adb"), QStringLiteral("设备分区表"), subtitle, partitions);
}

QVariantMap FirmwareWorkspace::loadFastbootPartitions(const QString& serial)
{
    return fastbootResult(serial);
}

} // namespace gf
