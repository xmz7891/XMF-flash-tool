#include "BootImage.h"

#include "Compression.h"

#include <QCryptographicHash>

namespace gf::img::boot {

namespace {

constexpr qint64 kBootPageSizeV3 = 4096;
constexpr qint64 kVendorBootPageSize = 4096;

quint32 rd32(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        fail(QStringLiteral("boot 头部数据被截断（偏移 %1）").arg(at));
    return quint32(quint8(d.at(at))) | (quint32(quint8(d.at(at + 1))) << 8) |
           (quint32(quint8(d.at(at + 2))) << 16) | (quint32(quint8(d.at(at + 3))) << 24);
}

quint64 rd64(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 8 > d.size())
        fail(QStringLiteral("boot 头部数据被截断（偏移 %1）").arg(at));
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v |= quint64(quint8(d.at(at + i))) << (8 * i);
    return v;
}

void wr32(QByteArray& out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

void wr64(QByteArray& out, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        out.append(char((v >> (8 * i)) & 0xFF));
}

QString cstr(const QByteArray& field)
{
    const qsizetype nul = field.indexOf('\0');
    const QByteArray text = nul < 0 ? field : field.left(nul);
    return QString::fromUtf8(text);
}

void appendFixed(QByteArray& out, const QString& text, int size)
{
    QByteArray bytes = text.toUtf8();
    if (bytes.size() > size)
        bytes.truncate(size);
    out.append(bytes.leftJustified(size, '\0'));
}

qint64 alignUp(qint64 value, qint64 alignment)
{
    if (alignment <= 1)
        return value;
    const qint64 rem = value % alignment;
    return rem == 0 ? value : value + (alignment - rem);
}

QByteArray paddedSection(const QByteArray& data, qint64 pageSize)
{
    return data.leftJustified(qsizetype(alignUp(data.size(), pageSize)), '\0');
}

QByteArray sectionAt(const QByteArray& data, qint64 offset, quint32 size, qint64 pageSize,
                     const QString& what)
{
    if (size == 0)
        return {};
    if (offset < 0 || offset + qint64(size) > data.size())
        fail(QStringLiteral("boot 镜像被截断：%1 声明 %2 字节，实际仅剩 %3 字节")
                 .arg(what)
                 .arg(size)
                 .arg(data.size() - offset));
    return data.mid(offset, size);
}

} // namespace

QString Image::kindText() const
{
    switch (kind) {
    case ImageKind::Boot:
        return QStringLiteral("Android boot");
    case ImageKind::InitBoot:
        return QStringLiteral("init_boot");
    case ImageKind::VendorBoot:
        return QStringLiteral("vendor_boot");
    }
    return QStringLiteral("未知");
}

QString Image::versionText() const
{
    return QStringLiteral("v%1").arg(headerVersion);
}

quint64 Image::totalSize() const
{
    quint64 total = headerSize ? headerSize : pageSize;
    total += quint64(kernel.size()) + quint64(ramdisk.size()) + quint64(second.size()) +
             quint64(dtb.size()) + quint64(recoveryDtbo.size()) + quint64(signature.size()) +
             quint64(bootconfig.size());
    for (const auto& f : vendorRamdisks)
        total += quint64(f.data.size());
    return total;
}

bool looksLikeBootImage(const QByteArray& data)
{
    if (data.size() < 8)
        return false;
    return data.left(8) == QByteArray("ANDROID!") || data.left(8) == QByteArray("VNDRBOOT");
}

bool isCpio(const QByteArray& data)
{
    return data.size() >= 6 &&
           (data.left(6) == QByteArray("070701") || data.left(6) == QByteArray("070702"));
}

QByteArray decompressRamdisk(const QByteArray& ramdisk, QString* formatName)
{
    const compress::Format format = compress::detect(ramdisk);
    if (formatName != nullptr)
        *formatName = compress::formatName(format);
    switch (format) {
    case compress::Format::Gzip:
        return compress::gzipDecompress(ramdisk);
    case compress::Format::Lzop:
        return compress::lzopDecompress(ramdisk);
    case compress::Format::Lz4:
        fail(QStringLiteral("ramdisk 使用 lz4 压缩，暂不支持（可先用 lz4 工具解压后再处理）"));
    case compress::Format::Lzma:
        fail(QStringLiteral("ramdisk 使用 xz/lzma 压缩，暂不支持"));
    case compress::Format::Bzip2:
        fail(QStringLiteral("ramdisk 使用 bzip2 压缩，暂不支持"));
    default:
        break;
    }
    if (isCpio(ramdisk))
        return ramdisk;
    fail(QStringLiteral("ramdisk 不是可识别的 cpio 或 gzip/lzop 压缩数据"));
}

QByteArray compressRamdisk(const QByteArray& ramdisk)
{
    if (isCpio(ramdisk))
        return compress::gzipCompress(ramdisk);
    return ramdisk;
}

Image parse(const QByteArray& data)
{
    if (data.size() < 44)
        fail(QStringLiteral("boot 镜像太小（%1 字节）").arg(data.size()));
    const QByteArray magic = data.left(8);
    Image img;

    if (magic == QByteArray("VNDRBOOT")) {
        img.kind = ImageKind::VendorBoot;
        img.headerVersion = rd32(data, 8);
        if (img.headerVersion < 3 || img.headerVersion > 4)
            fail(QStringLiteral("不支持的 vendor_boot 头版本：%1").arg(img.headerVersion));
        img.pageSize = quint32(kVendorBootPageSize);
        img.kernelAddr = rd32(data, 16);
        img.ramdiskAddr = rd32(data, 20);
        const quint32 vendorRamdiskSize = rd32(data, 24);
        img.cmdline = cstr(data.mid(28, 2048));
        img.tagsAddr = rd32(data, 2076);
        img.boardName = cstr(data.mid(2080, 16));
        img.headerSize = rd32(data, 2096);
        const quint32 dtbSize = rd32(data, 2100);
        img.dtbAddr = rd64(data, 2104);
        quint32 tableSize = 0;
        quint32 tableEntryNum = 0;
        quint32 tableEntrySize = 108;
        quint32 bootconfigSize = 0;
        if (img.headerVersion >= 4) {
            if (data.size() < 2128)
                fail(QStringLiteral("vendor_boot v4 头部被截断"));
            tableSize = rd32(data, 2112);
            tableEntryNum = rd32(data, 2116);
            tableEntrySize = rd32(data, 2120);
            bootconfigSize = rd32(data, 2124);
        }
        if (img.headerSize < 2112 || img.headerSize > 4096)
            img.headerSize = img.headerVersion >= 4 ? 2128 : 2112;
        img.vendorRamdiskTableEntrySize = tableEntrySize;

        qint64 pos = alignUp(img.headerSize, img.pageSize);
        const QByteArray vendorRamdisk =
            sectionAt(data, pos, vendorRamdiskSize, img.pageSize, QStringLiteral("vendor_ramdisk"));
        pos = alignUp(pos + vendorRamdiskSize, img.pageSize);
        img.dtb = sectionAt(data, pos, dtbSize, img.pageSize, QStringLiteral("dtb"));
        pos = alignUp(pos + dtbSize, img.pageSize);
        const QByteArray table =
            sectionAt(data, pos, tableSize, img.pageSize, QStringLiteral("ramdisk 表"));
        pos = alignUp(pos + tableSize, img.pageSize);
        img.bootconfig = sectionAt(data, pos, bootconfigSize, img.pageSize, QStringLiteral("bootconfig"));

        if (tableEntryNum == 0) {
            VendorFragment fragment;
            fragment.type = 1;
            fragment.data = vendorRamdisk;
            img.vendorRamdisks.append(fragment);
        } else {
            if (tableEntrySize < 44)
                fail(QStringLiteral("vendor_ramdisk 表项大小非法：%1").arg(tableEntrySize));
            for (quint32 i = 0; i < tableEntryNum; ++i) {
                const qint64 at = qint64(i) * tableEntrySize;
                if (at + 44 > table.size())
                    fail(QStringLiteral("vendor_ramdisk 表被截断（第 %1 项）").arg(i));
                VendorFragment fragment;
                const quint32 size = rd32(table, at);
                const quint32 offset = rd32(table, at + 4);
                fragment.type = rd32(table, at + 8);
                const qsizetype nameSize = qMin<qsizetype>(32, tableEntrySize - 12);
                fragment.name = cstr(table.mid(at + 12, nameSize));
                if (offset + size > vendorRamdisk.size())
                    fail(QStringLiteral("vendor_ramdisk 分片越界（第 %1 项）").arg(i));
                fragment.data = vendorRamdisk.mid(offset, size);
                img.vendorRamdisks.append(fragment);
            }
        }
        return img;
    }

    if (magic != QByteArray("ANDROID!"))
        fail(QStringLiteral("不是 Android boot 镜像（magic=%1）")
                 .arg(QString::fromLatin1(magic.toHex())));

    img.headerVersion = rd32(data, 40);
    if (img.headerVersion > 4)
        fail(QStringLiteral("不支持的 boot 头版本：%1").arg(img.headerVersion));

    if (img.headerVersion <= 2) {
        img.kind = ImageKind::Boot;
        const quint32 kernelSize = rd32(data, 8);
        img.kernelAddr = rd32(data, 12);
        const quint32 ramdiskSize = rd32(data, 16);
        img.ramdiskAddr = rd32(data, 20);
        const quint32 secondSize = rd32(data, 24);
        img.secondAddr = rd32(data, 28);
        img.tagsAddr = rd32(data, 32);
        img.pageSize = rd32(data, 36);
        if (img.pageSize < 512 || img.pageSize > 65536 || (img.pageSize & (img.pageSize - 1)) != 0)
            fail(QStringLiteral("boot 头页大小非法：%1").arg(img.pageSize));
        img.osVersion = rd32(data, 44);
        img.boardName = cstr(data.mid(48, 16));
        img.cmdline = cstr(data.mid(64, 512));
        img.extraCmdline = cstr(data.mid(584, 1024));
        quint32 recoveryDtboSize = 0;
        quint32 dtbSize = 0;
        if (img.headerVersion >= 1) {
            if (data.size() >= 1624) {
                recoveryDtboSize = rd32(data, 1608);
                img.recoveryDtboOffset = quint32(rd64(data, 1612));
                img.headerSize = rd32(data, 1620);
            }
        }
        if (img.headerVersion >= 2) {
            if (data.size() >= 1636)
                dtbSize = rd32(data, 1624);
            if (data.size() >= 1644)
                img.dtbAddr = rd64(data, 1628);
        }
        if (img.headerSize == 0)
            img.headerSize = img.headerVersion >= 2 ? 1636 : (img.headerVersion >= 1 ? 1624 : 1608);

        qint64 pos = alignUp(img.pageSize, img.pageSize);
        img.kernel = sectionAt(data, pos, kernelSize, img.pageSize, QStringLiteral("kernel"));
        pos = alignUp(pos + kernelSize, img.pageSize);
        img.ramdisk = sectionAt(data, pos, ramdiskSize, img.pageSize, QStringLiteral("ramdisk"));
        pos = alignUp(pos + ramdiskSize, img.pageSize);
        img.second = sectionAt(data, pos, secondSize, img.pageSize, QStringLiteral("second"));
        pos = alignUp(pos + secondSize, img.pageSize);
        if (recoveryDtboSize > 0) {
            img.recoveryDtbo =
                sectionAt(data, pos, recoveryDtboSize, img.pageSize, QStringLiteral("recovery_dtbo"));
            pos = alignUp(pos + recoveryDtboSize, img.pageSize);
        }
        if (dtbSize > 0)
            img.dtb = sectionAt(data, pos, dtbSize, img.pageSize, QStringLiteral("dtb"));
        return img;
    }

    // v3 / v4（boot 与 init_boot 共用格式）
    img.pageSize = quint32(kBootPageSizeV3);
    const quint32 kernelSize = rd32(data, 8);
    const quint32 ramdiskSize = rd32(data, 12);
    img.osVersion = rd32(data, 16);
    img.headerSize = rd32(data, 20);
    if (img.headerSize < 1580 || img.headerSize > 4096)
        img.headerSize = img.headerVersion >= 4 ? 1584 : 1580;
    img.cmdline = cstr(data.mid(44, 1536));
    img.kind = kernelSize == 0 && img.headerVersion >= 4 ? ImageKind::InitBoot : ImageKind::Boot;

    quint32 signatureSize = 0;
    if (img.headerVersion >= 4) {
        if (data.size() >= 1584)
            signatureSize = rd32(data, 1580);
        img.signatureSize = signatureSize;
    }
    qint64 pos = alignUp(img.headerSize, img.pageSize);
    img.kernel = sectionAt(data, pos, kernelSize, img.pageSize, QStringLiteral("kernel"));
    pos = alignUp(pos + kernelSize, img.pageSize);
    img.ramdisk = sectionAt(data, pos, ramdiskSize, img.pageSize, QStringLiteral("ramdisk"));
    pos = alignUp(pos + ramdiskSize, img.pageSize);
    if (signatureSize > 0)
        img.signature = sectionAt(data, pos, signatureSize, img.pageSize, QStringLiteral("boot 签名"));
    return img;
}

QByteArray computeId(const Image& image)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    auto addSection = [&](const QByteArray& data) {
        hash.addData(data);
        QByteArray size;
        wr32(size, quint32(data.size()));
        hash.addData(size);
    };
    addSection(image.kernel);
    addSection(image.ramdisk);
    addSection(image.second);
    return hash.result().left(8);
}

QByteArray serialize(const Image& source)
{
    Image img = source;
    if (img.pageSize == 0)
        img.pageSize = img.headerVersion >= 3 ? quint32(kBootPageSizeV3) : 2048;

    QByteArray header;
    if (img.kind == ImageKind::VendorBoot) {
        if (img.headerVersion < 3)
            fail(QStringLiteral("vendor_boot 需要 v3 或 v4 头版本"));
        if (img.headerVersion >= 4)
            img.headerSize = 2128;
        else
            img.headerSize = 2112;
        const QByteArray vendorRamdisk = [&] {
            QByteArray all;
            for (const auto& f : img.vendorRamdisks)
                all.append(f.data);
            return all;
        }();
        QByteArray table;
        if (img.headerVersion >= 4) {
            quint32 offset = 0;
            for (const auto& f : img.vendorRamdisks) {
                wr32(table, quint32(f.data.size()));
                wr32(table, offset);
                wr32(table, f.type);
                appendFixed(table, f.name, 32);
                table.append(QByteArray(64, '\0')); // board_id[16]
                offset += quint32(f.data.size());
            }
        }
        if (img.vendorRamdisks.isEmpty()) {
            // 空表也写 0 条目，解析端会得到一个空分片。
            img.vendorRamdisks.clear();
        }
        const quint32 tableEntrySize = 108;
        img.vendorRamdiskTableEntrySize = tableEntrySize;

        header.append("VNDRBOOT", 8);
        wr32(header, img.headerVersion);
        wr32(header, img.pageSize);
        wr32(header, quint32(img.kernelAddr));
        wr32(header, quint32(img.ramdiskAddr));
        wr32(header, quint32(vendorRamdisk.size()));
        header.append(img.cmdline.toUtf8().leftJustified(2048, '\0').left(2048));
        wr32(header, quint32(img.tagsAddr));
        appendFixed(header, img.boardName, 16);
        wr32(header, img.headerSize);
        wr32(header, quint32(img.dtb.size()));
        wr64(header, img.dtbAddr);
        if (img.headerVersion >= 4) {
            wr32(header, quint32(table.size()));
            wr32(header, quint32(img.vendorRamdisks.size()));
            wr32(header, tableEntrySize);
            wr32(header, quint32(img.bootconfig.size()));
        }
        if (header.size() != qsizetype(img.headerSize))
            fail(QStringLiteral("vendor_boot 头部长度错误：%1 != %2")
                     .arg(header.size())
                     .arg(img.headerSize));

        QByteArray out = paddedSection(header, img.pageSize);
        out.append(paddedSection(vendorRamdisk, img.pageSize));
        out.append(paddedSection(img.dtb, img.pageSize));
        out.append(paddedSection(table, img.pageSize));
        out.append(paddedSection(img.bootconfig, img.pageSize));
        return out;
    }

    if (img.headerVersion <= 2) {
        const QByteArray id = computeId(img);
        if (img.headerVersion >= 2)
            img.headerSize = 1636;
        else if (img.headerVersion >= 1)
            img.headerSize = 1624;
        else
            img.headerSize = 1608;

        header.append("ANDROID!", 8);
        wr32(header, quint32(img.kernel.size()));
        wr32(header, quint32(img.kernelAddr));
        wr32(header, quint32(img.ramdisk.size()));
        wr32(header, quint32(img.ramdiskAddr));
        wr32(header, quint32(img.second.size()));
        wr32(header, quint32(img.secondAddr));
        wr32(header, quint32(img.tagsAddr));
        wr32(header, img.pageSize);
        wr32(header, img.headerVersion);
        wr32(header, img.osVersion);
        appendFixed(header, img.boardName, 16);
        QByteArray cmdline = img.cmdline.toUtf8();
        if (img.extraCmdline.isEmpty() && cmdline.size() > 512) {
            img.extraCmdline = QString::fromUtf8(cmdline.mid(512));
            cmdline.truncate(512);
        }
        header.append(cmdline.leftJustified(512, '\0').left(512));
        header.append(id.leftJustified(8, '\0').left(8));
        const QByteArray extra = img.extraCmdline.toUtf8();
        header.append(extra.leftJustified(1024, '\0').left(1024));
        if (img.headerVersion >= 1) {
            wr32(header, quint32(img.recoveryDtbo.size()));
            wr64(header, img.recoveryDtboOffset);
            wr32(header, img.headerSize);
        }
        if (img.headerVersion >= 2) {
            wr32(header, quint32(img.dtb.size()));
            wr64(header, img.dtbAddr);
        }

        QByteArray out = paddedSection(header, img.pageSize);
        out.append(paddedSection(img.kernel, img.pageSize));
        out.append(paddedSection(img.ramdisk, img.pageSize));
        out.append(paddedSection(img.second, img.pageSize));
        if (img.headerVersion >= 1)
            out.append(paddedSection(img.recoveryDtbo, img.pageSize));
        if (img.headerVersion >= 2)
            out.append(paddedSection(img.dtb, img.pageSize));
        return out;
    }

    // boot / init_boot v3 / v4
    if (img.headerVersion < 3 || img.headerVersion > 4)
        fail(QStringLiteral("不支持的 boot 头版本：%1").arg(img.headerVersion));
    img.headerSize = img.headerVersion >= 4 ? 1584 : 1580;
    header.append("ANDROID!", 8);
    wr32(header, quint32(img.kernel.size()));
    wr32(header, quint32(img.ramdisk.size()));
    wr32(header, img.osVersion);
    wr32(header, img.headerSize);
    for (int i = 0; i < 4; ++i)
        wr32(header, 0);
    wr32(header, img.headerVersion);
    header.append(img.cmdline.toUtf8().leftJustified(1536, '\0').left(1536));
    if (img.headerVersion >= 4)
        wr32(header, quint32(img.signature.size()));
    if (header.size() != qsizetype(img.headerSize))
        fail(QStringLiteral("boot v%1 头部长度错误：%2").arg(img.headerVersion).arg(header.size()));

    QByteArray out = paddedSection(header, img.pageSize);
    out.append(paddedSection(img.kernel, img.pageSize));
    out.append(paddedSection(img.ramdisk, img.pageSize));
    if (img.headerVersion >= 4 && !img.signature.isEmpty())
        out.append(paddedSection(img.signature, img.pageSize));
    return out;
}

} // namespace gf::img::boot
