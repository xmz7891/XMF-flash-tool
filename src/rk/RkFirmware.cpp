#include "RkFirmware.h"

#include <QString>

namespace gf::rockchip {

namespace {

quint32 readLe32(const QByteArray& data, int offset)
{
    if (offset < 0 || offset + 4 > data.size())
        return 0;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData()) + offset;
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

QString readCString(const QByteArray& data, int offset, int length)
{
    if (offset < 0 || offset >= data.size())
        return {};
    QByteArray raw = data.mid(offset, length);
    const int zero = raw.indexOf('\0');
    if (zero >= 0)
        raw.truncate(zero);
    return QString::fromUtf8(raw).trimmed();
}

} // namespace

QString chipName(quint8 chipCode)
{
    switch (chipCode) {
    case 0x50: return QStringLiteral("RK29xx");
    case 0x60: return QStringLiteral("RK30xx");
    case 0x70: return QStringLiteral("RK31xx");
    case 0x80: return QStringLiteral("RK32xx");
    case 0x41: return QStringLiteral("RK3368");
    case 0x36: return QStringLiteral("RK3326");
    case 0x32: return QStringLiteral("RK3562");
    case 0x38: return QStringLiteral("RK3566");
    case 0x30: return QStringLiteral("PX30");
    default: break;
    }
    return QStringLiteral("未知（0x%1）").arg(chipCode, 2, 16, QLatin1Char('0'));
}

RkfwInfo parseRkfwHeader(const QByteArray& data)
{
    RkfwInfo info;
    if (data.size() < int(kRkfwHeaderPrefix)) {
        info.error = QStringLiteral("文件太小，不是 RKFW 固件");
        return info;
    }
    if (!data.startsWith(QByteArrayLiteral("RKFW"))) {
        info.error = QStringLiteral("缺少 RKFW 签名");
        return info;
    }
    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    info.version = QStringLiteral("%1.%2.%3")
                       .arg(p[9])
                       .arg(p[8])
                       .arg((quint32(p[7]) << 8) | quint32(p[6]));
    info.code = readLe32(data, 0x0A);
    info.year = int(p[0x0E]) | (int(p[0x0F]) << 8);
    info.month = p[0x10];
    info.day = p[0x11];
    info.hour = p[0x12];
    info.minute = p[0x13];
    info.second = p[0x14];
    info.chipCode = p[0x15];
    info.chipName = chipName(info.chipCode);
    info.bootOffset = readLe32(data, 0x19);
    info.bootSize = readLe32(data, 0x1D);
    info.updateOffset = readLe32(data, 0x21);
    info.updateSize = readLe32(data, 0x25);
    info.ok = true;
    return info;
}

RkafInfo parseRkafHeader(const QByteArray& data)
{
    RkafInfo info;
    if (data.size() < int(kRkafHeaderSize)) {
        info.error = QStringLiteral("数据不足 %1 字节，无法解析 RKAF 头").arg(kRkafHeaderSize);
        return info;
    }
    if (!data.startsWith(QByteArrayLiteral("RKAF"))) {
        info.error = QStringLiteral("缺少 RKAF 签名");
        return info;
    }
    info.model = readCString(data, 8, 34);
    info.id = readCString(data, 42, 30);
    info.manufacturer = readCString(data, 72, 56);
    info.version = readLe32(data, 132);
    info.numParts = readLe32(data, 136);
    if (info.numParts > 16)
        info.numParts = 16;
    const int partsBase = 140;
    for (quint32 i = 0; i < info.numParts; ++i) {
        const int base = partsBase + int(i) * 112;
        RkafPartition part;
        part.name = readCString(data, base, 32);
        part.path = readCString(data, base + 32, 60);
        part.flashSize = readLe32(data, base + 92);
        part.partOffset = readLe32(data, base + 96);
        part.flashOffset = readLe32(data, base + 100);
        part.paddedSize = readLe32(data, base + 104);
        part.byteCount = readLe32(data, base + 108);
        if (part.name.isEmpty())
            continue;
        if (part.path == QLatin1String("SELF") || part.path == QLatin1String("RESERVED"))
            continue;
        info.partitions.append(part);
    }
    info.ok = true;
    return info;
}

RkafInfo parseRkafAt(const QByteArray& wholeImage, quint32 baseOffset)
{
    RkafInfo info;
    if (qint64(baseOffset) + qint64(kRkafHeaderSize) > wholeImage.size()) {
        info.error = QStringLiteral("RKAF 偏移超出文件大小");
        return info;
    }
    return parseRkafHeader(wholeImage.mid(int(baseOffset), int(kRkafHeaderSize)));
}

} // namespace gf::rockchip
