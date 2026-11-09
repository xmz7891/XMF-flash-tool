#include "Pac.h"

#include "usb/ProtocolSupport.h"

#include <QStringList>

namespace gf::sprd {

QString pacImageTypeName(quint32 type)
{
    switch (PacImageType(type)) {
    case PacImageType::Code:
        return QStringLiteral("CODE");
    case PacImageType::Fdl1:
        return QStringLiteral("FDL1");
    case PacImageType::Fdl2:
        return QStringLiteral("FDL2");
    case PacImageType::Nv:
        return QStringLiteral("NV");
    case PacImageType::Erase:
        return QStringLiteral("ERASE");
    case PacImageType::Firmware:
        return QStringLiteral("FIRMWARE");
    }
    return QStringLiteral("TYPE_0x%1").arg(type, 0, 16);
}

PacInfo PacInfo::parse(const QByteArray& file)
{
    PacInfo info;
    if (file.size() < 0x20) {
        info.error = QStringLiteral("PAC 文件太小（%1 字节）").arg(file.size());
        return info;
    }
    if (quint8(file[0]) != 'P' || quint8(file[1]) != 'A' || quint8(file[2]) != 'C') {
        info.error = QStringLiteral("不是 PAC 文件（缺少“PAC”魔数）");
        return info;
    }

    info.version = proto::readU32le(file, 4);
    info.headerSize = proto::readU32le(file, 8);
    const quint32 imageCount = proto::readU32le(file, 0x0C);
    quint32 tableOffset = proto::readU32le(file, 0x10);
    info.totalSize = proto::readU32le(file, 0x14);

    if (info.headerSize < 0x20 || info.headerSize > quint32(file.size())) {
        info.error = QStringLiteral("PAC 头长度异常：0x%1").arg(info.headerSize, 0, 16);
        return info;
    }
    if (tableOffset == 0)
        tableOffset = 0x20;
    if (tableOffset < 0x20 || tableOffset > info.headerSize) {
        info.error = QStringLiteral("PAC 镜像表偏移异常：0x%1").arg(tableOffset, 0, 16);
        return info;
    }
    if (imageCount == 0) {
        info.error = QStringLiteral("PAC 镜像表为空");
        return info;
    }
    if (imageCount > 512) {
        info.error = QStringLiteral("PAC 镜像数量异常：%1").arg(imageCount);
        return info;
    }
    const quint64 tableEnd = quint64(tableOffset) + quint64(imageCount) * kPacImageEntrySize;
    if (tableEnd > quint64(file.size())) {
        info.error = QStringLiteral("PAC 镜像表越过文件末尾（需要 %1 字节）").arg(tableEnd);
        return info;
    }

    for (quint32 index = 0; index < imageCount; ++index) {
        const qsizetype base = qsizetype(tableOffset) + qsizetype(index) * kPacImageEntrySize;
        PacImage image;
        image.type = proto::readU32le(file, base + 0x00);
        image.offset = proto::readU32le(file, base + 0x04);
        image.size = proto::readU32le(file, base + 0x08);
        image.crc32 = proto::readU32le(file, base + 0x0C);
        image.name =
            QString::fromLatin1(file.mid(base + 0x10, 32).constData(), 32)
                .remove(QChar('\0'))
                .trimmed();

        if (image.offset > quint32(file.size())
            || image.size > quint32(file.size()) - image.offset) {
            info.warnings.append(QStringLiteral("镜像 %1（%2）数据超出文件范围，已跳过")
                                     .arg(index)
                                     .arg(image.name.isEmpty() ? pacImageTypeName(image.type)
                                                               : image.name));
            continue;
        }
        if (image.size == 0)
            info.warnings.append(QStringLiteral("镜像 %1 大小为 0").arg(index));
        info.images.append(image);
    }

    if (info.images.isEmpty()) {
        info.error = QStringLiteral("PAC 中没有可用的镜像数据");
        return info;
    }
    if (info.totalSize == 0) {
        info.warnings.append(QStringLiteral("PAC 头部未记录总大小，按文件大小处理"));
    } else if (info.totalSize != quint32(file.size())) {
        info.warnings.append(QStringLiteral("PAC 头总大小 %1 与文件大小 %2 不一致")
                                 .arg(info.totalSize)
                                 .arg(file.size()));
    }

    info.valid = true;
    return info;
}

QByteArray PacInfo::imageData(const QByteArray& file, int index, QString* error) const
{
    if (index < 0 || index >= images.size()) {
        if (error)
            *error = QStringLiteral("镜像索引越界：%1").arg(index);
        return {};
    }
    const PacImage& image = images[index];
    if (image.offset > quint32(file.size())
        || image.size > quint32(file.size()) - image.offset) {
        if (error)
            *error = QStringLiteral("镜像 %1 数据超出文件范围").arg(image.name);
        return {};
    }
    return file.mid(qsizetype(image.offset), qsizetype(image.size));
}

QString describePac(const PacInfo& info)
{
    if (!info.valid)
        return QStringLiteral("PAC 无效：%1").arg(info.error);
    return QStringLiteral("PAC 容器：版本 %1，头 0x%2，镜像 %3 个，总大小 %4 字节")
        .arg(info.version)
        .arg(info.headerSize, 0, 16)
        .arg(info.images.size())
        .arg(info.totalSize);
}

} // namespace gf::sprd
