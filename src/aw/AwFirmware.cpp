#include "AwFirmware.h"

#include <QStringList>

namespace gf::alwwin {

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

bool safeDumpName(const QString& name)
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    for (const QChar ch : name) {
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\')
            || ch.unicode() < 0x20 || ch == QLatin1Char('"'))
            return false;
    }
    return true;
}

} // namespace

AwImageInfo parseAwHeader(const QByteArray& header)
{
    AwImageInfo info;
    if (header.size() < 68) {
        info.error = QStringLiteral("文件太小，不是 IMAGEWTY 固件");
        return info;
    }
    if (!header.startsWith(QByteArrayLiteral("IMAGEWTY"))) {
        info.error = QStringLiteral("缺少 IMAGEWTY 签名");
        return info;
    }
    info.headerVersion = readLe32(header, 8);
    info.headerSize = readLe32(header, 12);
    info.formatVersion = readLe32(header, 20);
    info.totalImageSize = readLe32(header, 24);
    info.fileHeaderLength = readLe32(header, 32);
    info.usbProductId = readLe32(header, 36);
    info.usbVendorId = readLe32(header, 40);
    info.hardwareId = readLe32(header, 44);
    info.firmwareId = readLe32(header, 48);
    info.numFiles = readLe32(header, 60);
    if (info.headerSize < 68 || info.headerSize > kAwHeaderSize) {
        info.error = QStringLiteral("头部尺寸异常（%1）").arg(info.headerSize);
        return info;
    }
    if (info.numFiles == 0 || info.numFiles > kAwMaxFiles) {
        info.error = QStringLiteral("文件数量异常（%1）").arg(info.numFiles);
        return info;
    }
    if (info.fileHeaderLength < kAwFileEntryMin || info.fileHeaderLength > 0x100000) {
        info.error = QStringLiteral("文件头尺寸异常（%1）").arg(info.fileHeaderLength);
        return info;
    }
    info.ok = true;
    return info;
}

AwImageInfo parseAwImage(const QByteArray& header, const QByteArray& table, quint32 physicalSize)
{
    AwImageInfo info = parseAwHeader(header);
    if (!info.ok)
        return info;

    const quint64 tableEnd = quint64(kAwFileHeadersStart)
                             + quint64(info.numFiles) * quint64(info.fileHeaderLength);
    if (tableEnd > physicalSize) {
        info.ok = false;
        info.error = QStringLiteral("文件头表超出文件大小（需要 %1 字节）").arg(tableEnd);
        return info;
    }
    if (quint64(table.size()) < quint64(info.numFiles) * quint64(info.fileHeaderLength)) {
        info.ok = false;
        info.error = QStringLiteral("文件头表数据不完整");
        return info;
    }

    for (quint32 i = 0; i < info.numFiles; ++i) {
        const int base = int(i * info.fileHeaderLength);
        AwFileEntry entry;
        const quint32 nameLength = readLe32(table, base);
        entry.headerSize = readLe32(table, base + 4);
        entry.mainType = readCString(table, base + 8, 8);
        entry.subType = readCString(table, base + 16, 16);
        // Layout: nameLen(4) headerSize(4) mainType(8) subType(16) unknown0(4)
        //         fileName(256) stored(4) pad1(4) original(4) pad2(4) offset(4)
        entry.name = readCString(table, base + 36, 256);
        entry.storedLength = readLe32(table, base + 292);
        entry.originalLength = readLe32(table, base + 300);
        entry.offset = readLe32(table, base + 308);
        entry.lengthMismatch = entry.originalLength != entry.storedLength;
        if (nameLength == 0 || nameLength > 256 || entry.headerSize < kAwFileEntryMin
            || !safeDumpName(entry.name) || quint64(entry.offset) < tableEnd
            || quint64(entry.offset) + entry.storedLength > physicalSize) {
            info.ok = false;
            info.error = QStringLiteral("第 %1 个文件条目无效（%2）").arg(i + 1).arg(entry.name);
            return info;
        }
        info.files.append(entry);
    }
    return info;
}

} // namespace gf::alwwin
