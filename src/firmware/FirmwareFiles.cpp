#include "FirmwareFiles.h"

#include "image/Compression.h"
#include "image/ZipPackage.h"
#include "sprd/Pac.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cstring>

#include <algorithm>

namespace gf::firmware {

namespace {

constexpr qint64 kMaxInMemoryUnpack = 1536LL * 1024 * 1024;

bool startsWithBytes(const QByteArray& data, const char* magic, int length)
{
    return data.size() >= length && std::memcmp(data.constData(), magic, size_t(length)) == 0;
}

QString extensionOf(const QString& path)
{
    return QFileInfo(path).suffix().toLower();
}

QString cstr(const QByteArray& data, int offset, int length)
{
    QByteArray raw = data.mid(offset, length);
    const int zero = raw.indexOf('\0');
    if (zero >= 0)
        raw.truncate(zero);
    return QString::fromUtf8(raw);
}

quint64 parseOctal(const QByteArray& data, int offset, int length)
{
    quint64 value = 0;
    const QByteArray raw = data.mid(offset, length);
    for (char c : raw) {
        if (c < '0' || c > '7')
            break;
        value = value * 8 + quint64(c - '0');
    }
    return value;
}

bool isZeroBlock(const QByteArray& data, qsizetype offset)
{
    for (int i = 0; i < 512; ++i) {
        if (data.at(offset + i) != '\0')
            return false;
    }
    return true;
}

QString safeTarName(QString name)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QStringLiteral("./")))
        name = name.mid(2);
    while (name.startsWith(QLatin1Char('/')))
        name = name.mid(1);
    if (name.contains(QStringLiteral("..")))
        return {};
    return name;
}

QByteArray readHead(const QString& path, qint64 length)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.read(length);
}

QString sidecarHashFile(const QString& path)
{
    const QFileInfo info(path);
    const QDir dir = info.absoluteDir();
    const QStringList candidates = {
        info.fileName() + QStringLiteral(".md5"),
        info.completeBaseName() + QStringLiteral(".md5"),
        info.fileName() + QStringLiteral(".sha256"),
        info.completeBaseName() + QStringLiteral(".sha256"),
    };
    for (const QString& candidate : candidates) {
        const QString full = dir.filePath(candidate);
        if (QFileInfo::exists(full))
            return full;
    }
    return {};
}

void addExtracted(UnpackResult& result, const QString& relative)
{
    if (result.extracted.size() < 200)
        result.extracted << relative;
}

} // namespace

QString kindText(FirmwareKind kind)
{
    switch (kind) {
    case FirmwareKind::Zip:
        return QStringLiteral("zip 压缩包");
    case FirmwareKind::TarGz:
        return QStringLiteral("tar.gz / tgz 线刷包");
    case FirmwareKind::Tar:
        return QStringLiteral("tar 归档");
    case FirmwareKind::Pac:
        return QStringLiteral("展锐 PAC 固件");
    case FirmwareKind::RawImage:
        return QStringLiteral("原始镜像（img/bin）");
    case FirmwareKind::Ozip:
        return QStringLiteral("OPPO OZIP 加密固件");
    case FirmwareKind::Ofp:
        return QStringLiteral("OPPO OFP 固件");
    case FirmwareKind::Kdz:
        return QStringLiteral("LG KDZ 固件");
    case FirmwareKind::SevenZip:
        return QStringLiteral("7z 压缩包");
    case FirmwareKind::Rar:
        return QStringLiteral("rar 压缩包");
    case FirmwareKind::Gzip:
        return QStringLiteral("gzip 压缩数据");
    case FirmwareKind::Lz4:
        return QStringLiteral("lz4 压缩数据");
    case FirmwareKind::Lzop:
        return QStringLiteral("lzop 压缩数据");
    case FirmwareKind::Elf:
        return QStringLiteral("ELF 可执行文件");
    case FirmwareKind::Md5File:
        return QStringLiteral("MD5 校验文件");
    case FirmwareKind::Sha256File:
        return QStringLiteral("SHA256 校验文件");
    case FirmwareKind::Unknown:
        break;
    }
    return QStringLiteral("未知");
}

bool kindIsUnpackable(FirmwareKind kind)
{
    return kind == FirmwareKind::Zip || kind == FirmwareKind::TarGz || kind == FirmwareKind::Tar ||
           kind == FirmwareKind::Pac;
}

FirmwareFileInfo identifyFirmware(const QString& path, const QByteArray& headIn)
{
    FirmwareFileInfo info;
    info.path = path;
    info.fileName = QFileInfo(path).fileName();
    info.sizeBytes = QFileInfo(path).size();
    const QByteArray head = headIn.isEmpty() ? readHead(path, 4096) : headIn;
    const QString ext = extensionOf(path);
    const QString lowerName = info.fileName.toLower();

    // 1) Some OPPO/Realme packages keep the .ozip/.ofp extension but are plain
    // zip archives (no encryption). Content wins over extension when it is zip.
    if ((ext == QStringLiteral("ozip") || ext == QStringLiteral("ofp"))
        && (startsWithBytes(head, "PK\x03\x04", 4) || startsWithBytes(head, "PK\x05\x06", 4))) {
        info.kind = FirmwareKind::Zip;
        info.hints << QStringLiteral("zip 兼容容器（扩展名 .%1）").arg(ext);
    } else if (ext == QStringLiteral("ozip")) {
        info.kind = FirmwareKind::Ozip;
    } else if (ext == QStringLiteral("ofp")) {
        info.kind = FirmwareKind::Ofp;
    } else if (ext == QStringLiteral("kdz")) {
        info.kind = FirmwareKind::Kdz;
    } else if (startsWithBytes(head, "PK\x03\x04", 4) || startsWithBytes(head, "PK\x05\x06", 4)) {
        info.kind = FirmwareKind::Zip;
    } else if (head.size() >= 4 && quint8(head.at(0)) == 0x1F && quint8(head.at(1)) == 0x8B) {
        info.kind = (lowerName.endsWith(QStringLiteral(".tar.gz")) || ext == QStringLiteral("tgz"))
                        ? FirmwareKind::TarGz
                        : FirmwareKind::Gzip;
    } else if (head.size() >= 265 && head.mid(257, 5) == QByteArray("ustar")) {
        info.kind = FirmwareKind::Tar;
    } else if (startsWithBytes(head, "PAC\0", 4) || ext == QStringLiteral("pac")) {
        info.kind = FirmwareKind::Pac;
    } else if (startsWithBytes(head, "7z\xBC\xAF\x27\x1C", 6)) {
        info.kind = FirmwareKind::SevenZip;
    } else if (startsWithBytes(head, "Rar!", 4)) {
        info.kind = FirmwareKind::Rar;
    } else if (head.size() >= 4 && quint8(head.at(0)) == 0x04 && quint8(head.at(1)) == 0x22 &&
               quint8(head.at(2)) == 0x4D && quint8(head.at(3)) == 0x18) {
        info.kind = FirmwareKind::Lz4;
    } else if (head.size() >= 9 && quint8(head.at(0)) == 0x89 && head.mid(1, 3) == QByteArray("LZO", 3)) {
        info.kind = FirmwareKind::Lzop;
    } else if (startsWithBytes(head, "\x7F" "ELF", 4)) {
        info.kind = FirmwareKind::Elf;
    } else if (ext == QStringLiteral("md5") && head.size() <= 512 &&
               QString::fromLatin1(head.left(32))
                   .contains(QRegularExpression(QStringLiteral("^[0-9a-fA-F]{32}")))) {
        info.kind = FirmwareKind::Md5File;
    } else if ((ext == QStringLiteral("sha256") || ext == QStringLiteral("sha256sum")) &&
               head.size() <= 1024) {
        info.kind = FirmwareKind::Sha256File;
    } else if (ext == QStringLiteral("zip")) {
        info.kind = FirmwareKind::Zip;
    } else if (ext == QStringLiteral("tgz") || lowerName.endsWith(QStringLiteral(".tar.gz"))) {
        info.kind = FirmwareKind::TarGz;
    } else if (ext == QStringLiteral("tar")) {
        info.kind = FirmwareKind::Tar;
    } else if (ext == QStringLiteral("img") || ext == QStringLiteral("bin") ||
               ext == QStringLiteral("raw") || ext == QStringLiteral("mbn")) {
        info.kind = FirmwareKind::RawImage;
    } else if (head.size() >= 4096) {
        info.kind = FirmwareKind::RawImage;
    } else {
        info.kind = FirmwareKind::Unknown;
    }

    // 2) magic 线索（帮助判断 raw 镜像内部格式）。
    if (head.size() >= 4) {
        const quint32 magic32 = quint32(quint8(head.at(0))) | (quint32(quint8(head.at(1))) << 8) |
                                (quint32(quint8(head.at(2))) << 16) |
                                (quint32(quint8(head.at(3))) << 24);
        if (magic32 == 0xED26FF3Au)
            info.hints << QStringLiteral("Android sparse image");
        else if (head.left(4) == QByteArray("CrAU"))
            info.hints << QStringLiteral("ChromeOS/Android payload");
        else if (head.left(4) == QByteArray("AVB0"))
            info.hints << QStringLiteral("AVB vbmeta");
        else if (head.left(8) == QByteArray("ANDROID!"))
            info.hints << QStringLiteral("Android boot image");
        else if (head.left(8) == QByteArray("VNDRBOOT"))
            info.hints << QStringLiteral("Android vendor_boot image");
        else if (magic32 == 0x616C4467u)
            info.hints << QStringLiteral("Android super image（lp metadata）");
        else if (head.left(6) == QByteArray("070701") || head.left(6) == QByteArray("070702"))
            info.hints << QStringLiteral("cpio 归档（ramdisk）");
        else if (head.size() >= 1024 && head.mid(512, 8) == QByteArray("EFI PART"))
            info.hints << QStringLiteral("GPT 分区表镜像");
        else if (head.size() >= 1082 &&
                 quint16(quint8(head.at(1080)) | (quint8(head.at(1081)) << 8)) == 0xEF53)
            info.hints << QStringLiteral("ext4 文件系统");
        else if (head.left(2) == QByteArray("BZ"))
            info.hints << QStringLiteral("bzip2 压缩");
    }
    if (lowerName.contains(QStringLiteral("fastboot")) ||
        lowerName.contains(QStringLiteral("miflash")))
        info.hints << QStringLiteral("小米线刷包（fastboot 目录结构）");
    if (lowerName.contains(QStringLiteral("recovery")))
        info.hints << QStringLiteral("疑似 Recovery 卡刷包");

    info.kindText = kindText(info.kind);
    info.hashSidecar = sidecarHashFile(path);
    info.recognized = info.kind != FirmwareKind::Unknown;
    return info;
}

namespace {

UnpackResult extractTarBuffer(const QByteArray& tar, const QString& outputDir, UnpackResult result,
                              const ProgressFn& progress, const CancelFn& cancel)
{
    qsizetype pos = 0;
    QString pendingName;
    while (pos + 512 <= tar.size()) {
        checkCancel(cancel);
        if (isZeroBlock(tar, pos))
            break;
        const QByteArray header = tar.mid(pos, 512);
        const quint64 size = parseOctal(header, 124, 12);
        const char type = header.at(156);
        QString name = cstr(header, 0, 100);
        const QString prefix = cstr(header, 345, 155);
        if (!prefix.isEmpty())
            name = prefix + QLatin1Char('/') + name;

        if (type == 'L') { // GNU 长文件名
            if (pos + 512 + qsizetype(size) <= tar.size())
                pendingName = cstr(tar.mid(pos + 512, int(size)), 0, int(size));
            pos += 512 + ((qsizetype(size) + 511) / 512) * 512;
            continue;
        }
        if (type == 'x' || type == 'g') { // pax 扩展头
            const QByteArray pax = tar.mid(pos + 512, qsizetype(size));
            for (const QByteArray& line : pax.split('\n')) {
                const int eq = line.indexOf('=');
                if (eq <= 0)
                    continue;
                const QByteArray key = line.left(eq);
                const int space = key.indexOf(' ');
                if (space >= 0 && key.mid(space + 1) == QByteArray("path")) {
                    pendingName = QString::fromUtf8(line.mid(eq + 1));
                }
            }
            pos += 512 + ((qsizetype(size) + 511) / 512) * 512;
            continue;
        }
        if (!pendingName.isEmpty()) {
            name = pendingName;
            pendingName.clear();
        }
        const QString relative = safeTarName(name);
        pos += 512;
        if (relative.isEmpty()) {
            pos += ((qsizetype(size) + 511) / 512) * 512;
            continue;
        }
        const QString target = QDir(outputDir).filePath(relative);
        if (type == '5' || relative.endsWith(QLatin1Char('/'))) {
            if (ensureDirectory(target))
                ++result.dirsCreated;
        } else if (type == '0' || type == '\0' || type == '7') {
            if (pos + qsizetype(size) > tar.size()) {
                result.error = QStringLiteral("tar 数据被截断：%1").arg(relative);
                result.ok = false;
                return result;
            }
            if (ensureDirectory(QFileInfo(target).absolutePath())) {
                QFile file(target);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    result.error = QStringLiteral("无法写入：%1").arg(target);
                    result.ok = false;
                    return result;
                }
                const QByteArray content = tar.mid(pos, qsizetype(size));
                if (file.write(content) != content.size()) {
                    result.error = QStringLiteral("写入失败：%1").arg(target);
                    result.ok = false;
                    return result;
                }
                ++result.filesExtracted;
                result.bytesWritten += qint64(size);
                addExtracted(result, relative);
            }
        } else if (type == '2') {
            result.notes << QStringLiteral("跳过符号链接：%1").arg(relative);
        } else {
            result.notes << QStringLiteral("跳过特殊条目 %1（type=%2）").arg(relative).arg(int(type));
        }
        pos += ((qsizetype(size) + 511) / 512) * 512;
        if (tar.size() > 0)
            reportProgress(progress, int(5.0 + 75.0 * double(pos) / double(tar.size())),
                           QStringLiteral("解包 %1").arg(relative));
    }
    result.ok = true;
    return result;
}

} // namespace

UnpackResult unpackFirmware(const QString& path, const QString& outputDir, const ProgressFn& progress,
                            const CancelFn& cancel)
{
    UnpackResult result;
    result.outputDir = outputDir;
    const FirmwareFileInfo info = identifyFirmware(path);
    result.kindText = info.kindText;
    if (!QFileInfo::exists(path)) {
        result.error = QStringLiteral("文件不存在：%1").arg(path);
        return result;
    }
    if (!ensureDirectory(outputDir)) {
        result.error = QStringLiteral("无法创建输出目录：%1").arg(outputDir);
        return result;
    }

    switch (info.kind) {
    case FirmwareKind::Zip: {
        reportProgress(progress, 3, QStringLiteral("读取 zip 卡刷包目录"));
        gf::img::zip::Archive archive;
        try {
            archive = gf::img::zip::open(path);
        } catch (const std::exception& e) {
            result.error = QString::fromUtf8(e.what());
            return result;
        }
        const qsizetype total = qMax<qsizetype>(1, archive.entries.size());
        for (qsizetype index = 0; index < archive.entries.size(); ++index) {
            checkCancel(cancel);
            const auto& entry = archive.entries.at(index);
            reportProgress(progress, int(3.0 + 90.0 * double(index) / double(total)),
                           QStringLiteral("解压 %1").arg(entry.name));
            QString relative = entry.name;
            relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
            while (relative.startsWith(QLatin1Char('/')))
                relative = relative.mid(1);
            if (relative.contains(QStringLiteral(".."))) {
                result.notes << QStringLiteral("跳过非法路径：%1").arg(entry.name);
                continue;
            }
            if (relative.isEmpty())
                continue;
            const QString target = QDir(outputDir).filePath(relative);
            if (entry.isDirectory()) {
                if (ensureDirectory(target))
                    ++result.dirsCreated;
                continue;
            }
            try {
                const QByteArray data = gf::img::zip::readEntry(archive, entry);
                if (!ensureDirectory(QFileInfo(target).absolutePath())) {
                    result.error = QStringLiteral("无法创建目录：%1").arg(QFileInfo(target).absolutePath());
                    return result;
                }
                QFile file(target);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    result.error = QStringLiteral("无法写入：%1").arg(target);
                    return result;
                }
                file.write(data);
                ++result.filesExtracted;
                result.bytesWritten += data.size();
                addExtracted(result, relative);
            } catch (const std::exception& e) {
                result.error = QString::fromUtf8(e.what());
                return result;
            }
        }
        result.ok = true;
        reportProgress(progress, 100, QStringLiteral("zip 解包完成"));
        return result;
    }
    case FirmwareKind::Tar:
    case FirmwareKind::TarGz: {
        const qint64 fileSize = QFileInfo(path).size();
        if (fileSize > kMaxInMemoryUnpack) {
            result.error = QStringLiteral(
                "固件过大（%1），内置 tar 解包需要整体载入内存；请先用系统 tar/7z 解压后再处理")
                               .arg(formatBytes(fileSize));
            return result;
        }
        reportProgress(progress, 5, QStringLiteral("读取归档"));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("无法读取文件：%1").arg(path);
            return result;
        }
        QByteArray data = file.readAll();
        file.close();
        if (info.kind == FirmwareKind::TarGz) {
            reportProgress(progress, 15, QStringLiteral("解压 gzip"));
            try {
                data = gf::img::compress::gzipDecompress(data);
                result.notes << QStringLiteral("gzip 解压后 %1").arg(formatBytes(data.size()));
            } catch (const std::exception& e) {
                result.error = QStringLiteral("gzip 解压失败：%1").arg(QString::fromUtf8(e.what()));
                return result;
            }
        }
        result = extractTarBuffer(data, outputDir, result, progress, cancel);
        if (result.ok)
            reportProgress(progress, 100, QStringLiteral("tar 解包完成"));
        return result;
    }
    case FirmwareKind::Pac: {
        const qint64 fileSize = QFileInfo(path).size();
        if (fileSize > kMaxInMemoryUnpack) {
            result.error = QStringLiteral(
                "PAC 过大（%1），请使用「展锐 PAC 解析」工具分镜像提取，或先解压到本地")
                               .arg(formatBytes(fileSize));
            return result;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("无法读取文件：%1").arg(path);
            return result;
        }
        const QByteArray data = file.readAll();
        file.close();
        const gf::sprd::PacInfo pac = gf::sprd::PacInfo::parse(data);
        if (!pac.valid) {
            result.error = pac.error.isEmpty() ? QStringLiteral("PAC 结构无法识别") : pac.error;
            return result;
        }
        result.notes << QStringLiteral("PAC 版本 %1，镜像 %2 个").arg(pac.version).arg(pac.images.size());
        int index = 0;
        for (const auto& image : pac.images) {
            checkCancel(cancel);
            ++index;
            reportProgress(progress, int(5.0 + 90.0 * index / qMax(1, int(pac.images.size()))),
                           QStringLiteral("提取 PAC 镜像 %1").arg(image.name));
            const QString rawName = image.name.trimmed();
            QString name = rawName.isEmpty() ? QStringLiteral("image_%1").arg(index)
                                             : sanitizeFileName(rawName);
            if (!name.contains(QLatin1Char('.')))
                name += QStringLiteral(".bin");
            QString error;
            const QByteArray payload = pac.imageData(data, index - 1, &error);
            if (!error.isEmpty()) {
                result.notes << QStringLiteral("镜像 %1 提取失败：%2").arg(image.name, error);
                continue;
            }
            const QString target = QDir(outputDir).filePath(name);
            if (!ensureDirectory(QFileInfo(target).absolutePath())) {
                result.error = QStringLiteral("无法创建目录：%1").arg(QFileInfo(target).absolutePath());
                return result;
            }
            QFile out(target);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                result.error = QStringLiteral("无法写入：%1").arg(target);
                return result;
            }
            out.write(payload);
            ++result.filesExtracted;
            result.bytesWritten += payload.size();
            addExtracted(result, name);
        }
        result.ok = true;
        reportProgress(progress, 100, QStringLiteral("PAC 解包完成"));
        return result;
    }
    case FirmwareKind::Ozip:
    case FirmwareKind::Ofp:
        result.error = QStringLiteral(
                           "%1：目前仅支持识别，解包功能尚未实现（OPPO 官方固件为加密容器，需要官方工具）")
                           .arg(info.kindText);
        return result;
    default:
        break;
    }
    result.error = QStringLiteral("%1 不支持自动解包（可用「固件类型识别」查看内部格式）")
                       .arg(info.kindText);
    return result;
}

QVector<QString> findSplitParts(const QString& anyPartPath)
{
    const QFileInfo info(anyPartPath);
    const QDir dir = info.absoluteDir();
    const QString name = info.fileName();
    const QString lower = name.toLower();

    static const QRegularExpression zipPart(QStringLiteral("\\.z(\\d{2,4})$"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression numPart(QStringLiteral("\\.(\\d{3,4})$"));

    auto collectZipParts = [&](const QString& baseName) {
        QVector<QString> parts;
        static const QRegularExpression partRe(QStringLiteral("^(.+)\\.z(\\d{2,4})$"),
                                               QRegularExpression::CaseInsensitiveOption);
        QVector<QPair<int, QString>> found;
        const QStringList entries = dir.entryList(QDir::Files, QDir::Name);
        for (const QString& entry : entries) {
            const auto match = partRe.match(entry);
            if (!match.hasMatch())
                continue;
            if (match.captured(1).compare(baseName, Qt::CaseInsensitive) != 0)
                continue;
            found.append({match.captured(2).toInt(), dir.filePath(entry)});
        }
        std::sort(found.begin(), found.end(),
                  [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                      return a.first < b.first;
                  });
        for (const auto& item : found)
            parts.append(item.second);
        const QString mainFile = dir.filePath(baseName + QStringLiteral(".zip"));
        if (QFileInfo::exists(mainFile))
            parts.append(mainFile);
        return parts;
    };

    auto collectNumericParts = [&](const QString& baseName) {
        QVector<QString> parts;
        static const QRegularExpression partRe(QStringLiteral("^(.+)\\.(\\d{3,4})$"));
        QVector<QPair<int, QString>> found;
        const QStringList entries = dir.entryList(QDir::Files, QDir::Name);
        for (const QString& entry : entries) {
            const auto match = partRe.match(entry);
            if (!match.hasMatch())
                continue;
            if (match.captured(1).compare(baseName, Qt::CaseInsensitive) != 0)
                continue;
            found.append({match.captured(2).toInt(), dir.filePath(entry)});
        }
        std::sort(found.begin(), found.end(),
                  [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                      return a.first < b.first;
                  });
        for (const auto& item : found)
            parts.append(item.second);
        return parts;
    };

    if (lower.endsWith(QStringLiteral(".zip")))
        return collectZipParts(name.left(name.size() - 4));
    const auto zipMatch = zipPart.match(name);
    if (zipMatch.hasMatch())
        return collectZipParts(name.left(zipMatch.capturedStart()));
    const auto numMatch = numPart.match(name);
    if (numMatch.hasMatch())
        return collectNumericParts(name.left(numMatch.capturedStart()));
    return {};
}

MergeResult mergeSplitFirmware(const QString& anyPartPath, const QString& outputPath,
                               const ProgressFn& progress, const CancelFn& cancel)
{
    MergeResult result;
    result.outputPath = outputPath;
    QVector<QString> parts = findSplitParts(anyPartPath);
    if (parts.isEmpty() && QFileInfo(anyPartPath).isFile())
        parts.append(anyPartPath);
    if (parts.size() < 2) {
        result.error = QStringLiteral("未找到分卷文件（需要 .z01/.z02… + .zip，或 .001/.002…）");
        return result;
    }
    qint64 totalBytes = 0;
    for (const QString& part : parts) {
        if (!QFileInfo::exists(part)) {
            result.error = QStringLiteral("分卷缺失：%1").arg(part);
            return result;
        }
        totalBytes += QFileInfo(part).size();
    }
    result.parts = parts;
    if (!ensureDirectory(QFileInfo(outputPath).absolutePath())) {
        result.error = QStringLiteral("无法创建输出目录");
        return result;
    }
    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("无法写入：%1").arg(outputPath);
        return result;
    }
    qint64 written = 0;
    for (int index = 0; index < parts.size(); ++index) {
        checkCancel(cancel);
        QFile part(parts.at(index));
        if (!part.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("无法读取分卷：%1").arg(parts.at(index));
            return result;
        }
        while (!part.atEnd()) {
            checkCancel(cancel);
            const QByteArray chunk = part.read(8 * 1024 * 1024);
            if (chunk.isEmpty() && part.error() != QFile::NoError) {
                result.error = QStringLiteral("读取分卷失败：%1").arg(parts.at(index));
                return result;
            }
            if (out.write(chunk) != chunk.size()) {
                result.error = QStringLiteral("写入失败：%1").arg(outputPath);
                return result;
            }
            written += chunk.size();
            reportProgress(progress, int(100.0 * double(written) / double(qMax<qint64>(1, totalBytes))),
                           QStringLiteral("合并分卷 %1/%2").arg(index + 1).arg(parts.size()));
        }
        part.close();
    }
    out.close();
    result.ok = true;
    result.bytes = written;
    return result;
}

} // namespace gf::firmware
