#include "FirmwareCommon.h"

#include "core/Util.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace gf::firmware {

namespace {

QString findDataSubdir()
{
    QStringList candidates;
    const QString appData = gf::util::dataDir();
    if (!appData.isEmpty())
        candidates << QDir(appData).filePath(QStringLiteral("firmware"));
    const QString appDir = gf::util::appDir();
    if (!appDir.isEmpty())
        candidates << QDir(appDir).filePath(QStringLiteral("data/firmware"));

    QString base = QDir::currentPath();
    for (int depth = 0; depth < 6; ++depth) {
        candidates << QDir(base).filePath(QStringLiteral("data/firmware"));
        QDir up(base);
        if (!up.cdUp())
            break;
        base = up.absolutePath();
    }
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("brands.json"))) ||
            QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("models.json"))))
            return QDir(candidate).absolutePath();
    }
    // 尚未生成数据时返回第一个候选（调用方会得到 "文件不存在" 的明确错误）。
    return candidates.isEmpty() ? QString() : QDir(candidates.first()).absolutePath();
}

} // namespace

QString firmwareDataDir()
{
    static const QString dir = findDataSubdir();
    return dir;
}

QString brandsJsonPath()
{
    return QDir(firmwareDataDir()).filePath(QStringLiteral("brands.json"));
}

QString modelsJsonPath()
{
    return QDir(firmwareDataDir()).filePath(QStringLiteral("models.json"));
}

QString downloadDir()
{
    QSettings settings(QStringLiteral("XMF"), QStringLiteral("XMFTool"));
    QString dir = settings.value(QStringLiteral("paths/downloads")).toString();
    if (dir.trimmed().isEmpty()) {
        const QString downloads =
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        dir = QDir(downloads.isEmpty() ? QDir::homePath() : downloads)
                  .filePath(QStringLiteral("XMF"));
    }
    return dir;
}

QString cacheDir()
{
    const QString dir = QDir(gf::util::cacheDir()).filePath(QStringLiteral("firmware"));
    QDir().mkpath(dir);
    return dir;
}

QString formatBytes(qint64 bytes)
{
    return gf::util::formatBytes(bytes);
}

qint64 sizeTextToBytes(const QString& text)
{
    quint64 value = 0;
    if (!gf::util::parseSize(text, &value))
        return -1;
    return qint64(value);
}

FileHashes hashFile(const QString& path, const CancelFn& cancel)
{
    FileHashes result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        checkCancel(cancel);
        const QByteArray chunk = file.read(4 * 1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError)
            return FileHashes{};
        md5.addData(chunk);
        sha256.addData(chunk);
        result.size += chunk.size();
    }
    result.md5 = QString::fromLatin1(md5.result().toHex());
    result.sha256 = QString::fromLatin1(sha256.result().toHex());
    return result;
}

bool verifyFileHash(const QString& path, const QString& expected, QString* actual, QString* error,
                    const CancelFn& cancel)
{
    const QString want = expected.trimmed().toLower();
    if (want.isEmpty()) {
        if (error)
            *error = QStringLiteral("未提供期望哈希值");
        return false;
    }
    const FileHashes hashes = hashFile(path, cancel);
    if (hashes.size == 0 && !QFileInfo::exists(path)) {
        if (error)
            *error = QStringLiteral("文件不存在：%1").arg(path);
        return false;
    }
    QString actualValue;
    if (want.size() == 32)
        actualValue = hashes.md5;
    else
        actualValue = hashes.sha256;
    if (actual)
        *actual = actualValue;
    if (actualValue.compare(want, Qt::CaseInsensitive) != 0) {
        if (error)
            *error = QStringLiteral("哈希不匹配：期望 %1，实际 %2").arg(want, actualValue);
        return false;
    }
    return true;
}

QString sanitizeFileName(const QString& name)
{
    QString out;
    out.reserve(name.size());
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (const QChar ch : name) {
        if (invalid.contains(ch) || ch.unicode() < 0x20)
            out += QLatin1Char('_');
        else
            out += ch;
    }
    out = out.trimmed();
    while (out.endsWith(QLatin1Char('.')))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("firmware") : out;
}

QString fileNameFromUrl(const QString& url)
{
    const QUrl parsed(url);
    const QString path = parsed.path();
    if (path.isEmpty())
        return {};
    const QString last = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    return sanitizeFileName(QUrl::fromPercentEncoding(last.toUtf8()));
}

bool ensureDirectory(const QString& path)
{
    if (path.isEmpty())
        return false;
    return QDir().mkpath(path);
}

bool writeFile(const QString& path, const QByteArray& data)
{
    return gf::util::writeFileAtomic(path, data);
}

QByteArray readFile(const QString& path)
{
    return gf::util::readFile(path);
}

void reportProgress(const ProgressFn& fn, int percent, const QString& status)
{
    if (fn)
        fn(percent, status);
}

void checkCancel(const CancelFn& fn)
{
    if (fn)
        fn();
}

} // namespace gf::firmware
