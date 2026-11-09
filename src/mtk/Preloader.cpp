#include "Preloader.h"

#include "usb/ProtocolSupport.h"

#include <QStringList>

namespace gf::mtk {

bool PreloaderInfo::looksLikePreloader(const QByteArray& data)
{
    if (data.size() < int(kPreloaderHeaderSize))
        return false;
    const quint32 magic = proto::readU32le(data, 0);
    return (magic & kPreloaderMagicMask) == kPreloaderMagicLow;
}

PreloaderInfo PreloaderInfo::parse(const QByteArray& data)
{
    PreloaderInfo info;
    if (data.size() < int(kPreloaderHeaderSize)) {
        info.error = QStringLiteral("文件太小，不足 %1 字节的 preloader 头").arg(kPreloaderHeaderSize);
        return info;
    }

    info.magic = proto::readU32le(data, 0);
    if ((info.magic & kPreloaderMagicMask) != kPreloaderMagicLow) {
        info.error = QStringLiteral("不是 MTK preloader：magic=0x%1（期望低 24 位为 0x4D4D4D）")
                         .arg(info.magic, 8, 16, QLatin1Char('0'));
        return info;
    }

    info.headerSize = proto::readU32le(data, 4);
    if (info.headerSize < 0x20 || info.headerSize > 0x1000
        || info.headerSize > quint32(data.size())) {
        info.error = QStringLiteral("preloader 头长度异常：0x%1").arg(info.headerSize, 0, 16);
        return info;
    }

    info.fileInfoName =
        QString::fromLatin1(data.mid(8, 12).constData(), 12).remove(QChar('\0')).trimmed();
    info.imageSize = proto::readU32le(data, 0x20);
    info.fieldAt24 = proto::readU32le(data, 0x24);
    info.fieldAt28 = proto::readU32le(data, 0x28);
    info.fieldAt2C = proto::readU32le(data, 0x2C);
    info.fieldAt30 = proto::readU32le(data, 0x30);
    info.fieldAt34 = proto::readU32le(data, 0x34);

    if (info.fileInfoName != QLatin1String("FILE_INFO"))
        info.warnings.append(
            QStringLiteral("FILE_INFO 名称不同（实际“%1”），可能是厂商定制头").arg(info.fileInfoName));

    if (info.imageSize > quint32(data.size())) {
        info.error = QStringLiteral("preloader 头声明的大小 %1 超过文件大小 %2")
                         .arg(info.imageSize)
                         .arg(data.size());
        return info;
    }
    if (info.imageSize < quint32(data.size())) {
        info.hasAppendedSignature = true;
        info.warnings.append(QStringLiteral("文件比头部声明多 %1 字节，通常为追加签名")
                                 .arg(qint64(data.size()) - qint64(info.imageSize)));
    }

    // Walk the tagged section chain. It is normal for the chain to stop before
    // the end of the file: the boot code follows without a tag.
    quint32 offset = info.headerSize;
    while (offset + 8 <= quint32(data.size())) {
        const quint32 magic = proto::readU32le(data, offset);
        if ((magic & kPreloaderMagicMask) != kPreloaderMagicLow)
            break;
        const quint16 length = proto::readU16le(data, offset + 4);
        const quint16 type = proto::readU16le(data, offset + 6);
        if (length < 8 || quint64(offset) + length > quint64(data.size())) {
            info.warnings.append(QStringLiteral("第 %1 个段长度异常（0x%2），停止解析")
                                     .arg(info.sections.size())
                                     .arg(length, 0, 16));
            break;
        }
        PreloaderSection section;
        section.offset = offset;
        section.length = length;
        section.type = type;
        info.sections.append(section);
        offset += length;
    }
    info.sectionsEnd = offset;

    if (info.sections.isEmpty())
        info.warnings.append(QStringLiteral("未找到带 MMM 标记的段表，可能是精简/定制 preloader"));

    info.valid = true;
    return info;
}

QString describePreloader(const PreloaderInfo& info)
{
    if (!info.valid)
        return QStringLiteral("Preloader 无效：%1").arg(info.error);
    return QStringLiteral("MTK Preloader：%1，头 %2 字节，镜像 %3 字节，%4 个段，代码偏移 0x%5")
        .arg(info.fileInfoName.isEmpty() ? QStringLiteral("?") : info.fileInfoName)
        .arg(info.headerSize)
        .arg(info.imageSize)
        .arg(info.sections.size())
        .arg(info.sectionsEnd, 0, 16);
}

} // namespace gf::mtk
