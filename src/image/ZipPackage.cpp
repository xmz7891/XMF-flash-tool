#include "ZipPackage.h"

#include "Compression.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace gf::img::zip {

namespace {

constexpr quint32 kEndSignature = 0x06054b50u;
constexpr quint32 kCentralSignature = 0x02014b50u;
constexpr quint32 kLocalSignature = 0x04034b50u;

quint16 rd16(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 2 > d.size())
        fail(QStringLiteral("zip 数据被截断"));
    return quint16(quint8(d.at(at)) | (quint8(d.at(at + 1)) << 8));
}

quint32 rd32(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        fail(QStringLiteral("zip 数据被截断"));
    return quint32(quint8(d.at(at))) | (quint32(quint8(d.at(at + 1))) << 8) |
           (quint32(quint8(d.at(at + 2))) << 16) | (quint32(quint8(d.at(at + 3))) << 24);
}

quint64 rd64(const QByteArray& d, qsizetype at)
{
    const quint64 low = rd32(d, at);
    const quint64 high = rd32(d, at + 4);
    return low | (high << 32);
}

/// 读取归档中的一段数据。
QByteArray readRange(const Archive& archive, quint64 offset, quint64 length)
{
    if (length == 0)
        return {};
    if (!archive.buffer.isEmpty()) {
        if (offset + length > quint64(archive.buffer.size()))
            fail(QStringLiteral("zip 读取越界"));
        return archive.buffer.mid(qsizetype(offset), qsizetype(length));
    }
    FileReader reader(archive.path);
    return reader.readAt(qint64(offset), qint64(length));
}

Archive parseCentralDirectory(const QByteArray& tail, const QByteArray& endRecord,
                              qint64 fileSize, const QString& path, const QByteArray& buffer)
{
    Archive archive;
    archive.path = path;
    archive.fileSize = fileSize;
    archive.buffer = buffer;
    archive.hasEndRecord = true;
    const quint16 totalEntries = rd16(endRecord, 10);
    if (totalEntries == 0xFFFF)
        fail(QStringLiteral("暂不支持 ZIP64 卡刷包"));
    const quint32 cdSize = rd32(endRecord, 12);
    const quint32 cdOffset = rd32(endRecord, 16);
    const quint16 commentLen = rd16(endRecord, 20);

    Q_UNUSED(tail);
    QByteArray cd;
    if (!buffer.isEmpty()) {
        if (quint64(cdOffset) + cdSize > quint64(buffer.size()))
            fail(QStringLiteral("zip 中央目录超出文件范围"));
        cd = buffer.mid(qsizetype(cdOffset), qsizetype(cdSize));
    } else {
        FileReader reader(path);
        cd = reader.readAt(qint64(cdOffset), qint64(cdSize));
    }
    if (commentLen > 0 && buffer.isEmpty() && fileSize > 0) {
        FileReader reader(path);
        const qint64 commentOffset = fileSize - qint64(commentLen);
        if (commentOffset > 0)
            archive.comment = QString::fromUtf8(reader.readAt(commentOffset, commentLen));
    }

    qsizetype pos = 0;
    for (quint16 i = 0; i < totalEntries; ++i) {
        if (pos + 46 > cd.size())
            fail(QStringLiteral("zip 中央目录被截断（第 %1 项）").arg(i));
        if (rd32(cd, pos) != kCentralSignature)
            fail(QStringLiteral("zip 中央目录签名错误（第 %1 项）").arg(i));
        Entry entry;
        entry.flags = rd16(cd, pos + 8);
        entry.method = rd16(cd, pos + 10);
        entry.modTime = rd16(cd, pos + 12);
        entry.modDate = rd16(cd, pos + 14);
        entry.crc = rd32(cd, pos + 16);
        entry.compressedSize = rd32(cd, pos + 20);
        entry.uncompressedSize = rd32(cd, pos + 24);
        const quint16 nameLen = rd16(cd, pos + 28);
        const quint16 extraLen = rd16(cd, pos + 30);
        const quint16 itemCommentLen = rd16(cd, pos + 32);
        entry.localHeaderOffset = rd32(cd, pos + 42);
        if (entry.compressedSize == 0xFFFFFFFFu || entry.uncompressedSize == 0xFFFFFFFFu ||
            entry.localHeaderOffset == 0xFFFFFFFFu)
            fail(QStringLiteral("暂不支持 ZIP64 条目：%1").arg(entry.name));
        if (pos + 46 + nameLen + extraLen + itemCommentLen > cd.size())
            fail(QStringLiteral("zip 中央目录条目越界（第 %1 项）").arg(i));
        entry.name = QString::fromUtf8(cd.mid(pos + 46, nameLen));
        archive.entries.append(entry);
        pos += 46 + nameLen + extraLen + itemCommentLen;
    }
    return archive;
}

} // namespace

bool Entry::isImage() const
{
    const QString lower = name.toLower();
    static const QStringList suffixes = {
        QStringLiteral(".img"),  QStringLiteral(".bin"),  QStringLiteral(".mbn"),
        QStringLiteral(".elf"),  QStringLiteral(".fd"),   QStringLiteral(".pac"),
        QStringLiteral(".ops"),  QStringLiteral(".tar"),  QStringLiteral(".tar.md5"),
        QStringLiteral(".lz4"),  QStringLiteral(".gz"),
    };
    for (const auto& suffix : suffixes) {
        if (lower.endsWith(suffix))
            return true;
    }
    return false;
}

QString Entry::methodText() const
{
    switch (method) {
    case 0:
        return QStringLiteral("存储");
    case 8:
        return QStringLiteral("deflate");
    case 12:
        return QStringLiteral("bzip2");
    case 14:
        return QStringLiteral("lzma");
    default:
        break;
    }
    return QStringLiteral("方法 %1").arg(method);
}

QString Entry::sizeText() const
{
    return QStringLiteral("%1 -> %2").arg(formatBytes(uncompressedSize), formatBytes(compressedSize));
}

bool isZip(const QByteArray& data)
{
    return data.size() >= 4 && data.left(4) == QByteArray("PK\x03\x04", 4);
}

Archive open(const QString& path)
{
    FileReader reader(path);
    const qint64 size = reader.size();
    if (size < 22)
        fail(QStringLiteral("文件太小，不是 zip：%1").arg(path));
    const qint64 tailSize = qMin<qint64>(size, 65557);
    const QByteArray tail = reader.readAt(size - tailSize, tailSize);
    const qint64 found = tail.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (found < 0)
        fail(QStringLiteral("不是有效的 zip 卡刷包（未找到 EOCD）：%1").arg(path));
    const QByteArray endRecord = tail.mid(found, 22);
    return parseCentralDirectory(tail, endRecord, size, path, QByteArray());
}

Archive parse(const QByteArray& data)
{
    if (data.size() < 22)
        fail(QStringLiteral("数据太小，不是 zip"));
    const qint64 found = data.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (found < 0)
        fail(QStringLiteral("不是有效的 zip 数据（未找到 EOCD）"));
    const QByteArray endRecord = data.mid(found, 22);
    return parseCentralDirectory(data, endRecord, data.size(), QString(), data);
}

QByteArray readEntry(const Archive& archive, const Entry& entry)
{
    if (entry.isDirectory())
        return {};
    const QByteArray local = readRange(archive, entry.localHeaderOffset, 30);
    if (rd32(local, 0) != kLocalSignature)
        fail(QStringLiteral("zip 本地文件头签名错误：%1").arg(entry.name));
    const quint16 nameLen = rd16(local, 26);
    const quint16 extraLen = rd16(local, 28);
    const QByteArray header = readRange(archive, entry.localHeaderOffset, 30 + nameLen + extraLen);
    const QByteArray data = readRange(archive, entry.localHeaderOffset + 30 + nameLen + extraLen,
                                      entry.compressedSize);

    QByteArray out;
    switch (entry.method) {
    case 0:
        out = data;
        break;
    case 8:
        out = compress::inflateRaw(data);
        break;
    default:
        fail(QStringLiteral("不支持的 zip 压缩方式（%1）：%2").arg(entry.methodText(), entry.name));
    }
    if (out.size() != qsizetype(entry.uncompressedSize))
        fail(QStringLiteral("zip 条目解压后大小不符：%1（期望 %2，实际 %3）")
                 .arg(entry.name)
                 .arg(entry.uncompressedSize)
                 .arg(out.size()));
    if (crc32(out) != entry.crc)
        fail(QStringLiteral("zip 条目 CRC32 校验失败：%1").arg(entry.name));
    return out;
}

QByteArray readEntryByName(const Archive& archive, const QString& name)
{
    for (const auto& entry : archive.entries) {
        if (entry.name == name)
            return readEntry(archive, entry);
    }
    return {};
}

QStringList verify(const Archive& archive, const CancelFn& cancel)
{
    QStringList problems;
    int index = 0;
    for (const auto& entry : archive.entries) {
        checkCancel(cancel);
        ++index;
        Q_UNUSED(index);
        if (entry.isDirectory())
            continue;
        try {
            readEntry(archive, entry);
        } catch (const std::exception& e) {
            problems << QStringLiteral("%1：%2")
                            .arg(entry.name, QString::fromUtf8(e.what()));
        }
    }
    return problems;
}

UpdaterInfo parseUpdaterScript(const QByteArray& text)
{
    UpdaterInfo info;
    const QString script = QString::fromUtf8(text);
    const QRegularExpression quote(QStringLiteral("\"([^\"]*)\""));
    const QStringList lines = script.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        info.rawLines << line;
        if (line.contains(QStringLiteral("package_extract_file"))) {
            QStringList quoted;
            auto it = quote.globalMatch(line);
            while (it.hasNext())
                quoted << it.next().captured(1);
            for (const auto& item : quoted) {
                if (item.contains(QLatin1Char('/')) && item.startsWith(QLatin1Char('/')))
                    continue;
                if (item.endsWith(QStringLiteral(".img")) || item.endsWith(QStringLiteral(".bin")) ||
                    item.endsWith(QStringLiteral(".mbn")) || item.endsWith(QStringLiteral(".elf")))
                    info.packageExtractFile << item;
            }
        } else if (line.contains(QStringLiteral("package_extract_dir"))) {
            auto it = quote.globalMatch(line);
            if (it.hasNext())
                info.packageExtractDir << it.next().captured(1);
        } else if (line.contains(QStringLiteral("ui_print"))) {
            auto it = quote.globalMatch(line);
            if (it.hasNext())
                info.uiPrint << it.next().captured(1);
        } else if (line.contains(QStringLiteral("mount("))) {
            auto it = quote.globalMatch(line);
            if (it.hasNext())
                info.mountPoints << it.next().captured(1);
        } else if (line.contains(QStringLiteral("format("))) {
            auto it = quote.globalMatch(line);
            if (it.hasNext())
                info.formatTargets << it.next().captured(1);
        } else if (line.contains(QStringLiteral("set_perm"))) {
            info.setPerm << line;
        } else if (line.startsWith(QStringLiteral("assert"))) {
            info.asserts << line;
        }
    }
    return info;
}

QStringList updaterSummary(const UpdaterInfo& info)
{
    QStringList lines;
    lines << QStringLiteral("updater-script 语句 %1 行").arg(info.rawLines.size());
    if (!info.packageExtractFile.isEmpty())
        lines << QStringLiteral("写入镜像：%1").arg(info.packageExtractFile.join(QStringLiteral(", ")));
    if (!info.packageExtractDir.isEmpty())
        lines << QStringLiteral("解包目录：%1").arg(info.packageExtractDir.join(QStringLiteral(", ")));
    if (!info.mountPoints.isEmpty())
        lines << QStringLiteral("挂载点：%1").arg(info.mountPoints.join(QStringLiteral(", ")));
    if (!info.formatTargets.isEmpty())
        lines << QStringLiteral("格式化：%1").arg(info.formatTargets.join(QStringLiteral(", ")));
    if (!info.asserts.isEmpty())
        lines << QStringLiteral("机型断言：%1").arg(info.asserts.join(QStringLiteral(" | ")));
    if (!info.uiPrint.isEmpty())
        lines << QStringLiteral("提示信息 %1 条：%2").arg(info.uiPrint.size()).arg(info.uiPrint.join(QStringLiteral(" / ")));
    return lines;
}

QStringList imageNames(const Archive& archive)
{
    QStringList names;
    for (const auto& entry : archive.entries) {
        if (!entry.isDirectory() && entry.isImage())
            names << entry.name;
    }
    return names;
}

} // namespace gf::img::zip
