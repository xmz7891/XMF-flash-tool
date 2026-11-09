#include "Util.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>

#include <windows.h>

#include <cmath>

namespace gf::util {

QString formatBytes(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("—");
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(value, 0, 'f', value < 10 ? 2 : 1).arg(QString::fromLatin1(units[unit]));
}

QString formatDuration(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("—");
    const qint64 totalSeconds = ms / 1000;
    const qint64 h = totalSeconds / 3600;
    const qint64 m = (totalSeconds % 3600) / 60;
    const qint64 s = totalSeconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatSpeed(qint64 bytes, qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("—");
    const double bps = double(bytes) * 1000.0 / double(ms);
    return formatBytes(qint64(bps)) + QStringLiteral("/s");
}

QString hex32(quint32 value)
{
    return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("0X"), QStringLiteral("0x"));
}

QString hex8(quint8 value)
{
    return QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("0X"), QStringLiteral("0x"));
}

bool parseHex(const QString& text, quint64* out)
{
    QString t = text.trimmed();
    if (t.startsWith(QStringLiteral("0x")) || t.startsWith(QStringLiteral("0X")))
        t = t.mid(2);
    t.remove(QLatin1Char('_')).remove(QLatin1Char(' '));
    if (t.isEmpty())
        return false;
    bool ok = false;
    const quint64 v = t.toULongLong(&ok, 16);
    if (ok && out)
        *out = v;
    return ok;
}

bool parseSize(const QString& text, quint64* out)
{
    QString t = text.trimmed().toUpper();
    if (t.isEmpty())
        return false;
    qint64 multiplier = 1;
    if (t.endsWith(QStringLiteral("KB"))) { multiplier = 1024LL; t.chop(2); }
    else if (t.endsWith(QStringLiteral("MB"))) { multiplier = 1024LL * 1024; t.chop(2); }
    else if (t.endsWith(QStringLiteral("GB"))) { multiplier = 1024LL * 1024 * 1024; t.chop(2); }
    else if (t.endsWith(QStringLiteral("TB"))) { multiplier = 1024LL * 1024 * 1024 * 1024; t.chop(2); }
    else if (t.endsWith(QStringLiteral("K"))) { multiplier = 1024LL; t.chop(1); }
    else if (t.endsWith(QStringLiteral("M"))) { multiplier = 1024LL * 1024; t.chop(1); }
    else if (t.endsWith(QStringLiteral("G"))) { multiplier = 1024LL * 1024 * 1024; t.chop(1); }
    bool ok = false;
    const double value = t.trimmed().toDouble(&ok);
    if (!ok)
        return false;
    if (out)
        *out = quint64(value * double(multiplier));
    return true;
}

bool ensureDir(const QString& path)
{
    if (path.isEmpty())
        return false;
    return QDir().mkpath(path);
}

bool writeFile(const QString& path, const QByteArray& data)
{
    ensureDir(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

bool writeFileAtomic(const QString& path, const QByteArray& data)
{
    const QString tmp = path + QStringLiteral(".tmp");
    if (!writeFile(tmp, data))
        return false;
    QFile::remove(path);
    return QFile::rename(tmp, path);
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QString fileNameOf(const QString& path) { return QFileInfo(path).fileName(); }
QString baseNameOf(const QString& path) { return QFileInfo(path).completeBaseName(); }
QString extensionOf(const QString& path) { return QFileInfo(path).suffix().toLower(); }

QString uniquePath(const QString& path)
{
    if (!QFileInfo::exists(path))
        return path;
    const QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    const QString base = fi.completeBaseName();
    const QString ext = fi.suffix().isEmpty() ? QString() : QStringLiteral(".") + fi.suffix();
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = QStringLiteral("%1/%2 (%3)%4").arg(dir, base).arg(i).arg(ext);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return path;
}

QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString sha256File(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString md5File(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

QString crc32Hex(const QByteArray& data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (char ch : data)
        crc = table[(crc ^ quint8(ch)) & 0xFF] ^ (crc >> 8);
    crc ^= 0xFFFFFFFFu;
    return QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0')).toUpper();
}

quint16 readLE16(const QByteArray& data, qsizetype offset, quint16 fallback)
{
    if (offset < 0 || offset + 2 > data.size())
        return fallback;
    const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

quint32 readLE32(const QByteArray& data, qsizetype offset, quint32 fallback)
{
    if (offset < 0 || offset + 4 > data.size())
        return fallback;
    const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint64 readLE64(const QByteArray& data, qsizetype offset, quint64 fallback)
{
    if (offset < 0 || offset + 8 > data.size())
        return fallback;
    quint64 v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | quint8(data.at(offset + i));
    return v;
}

quint32 readBE32(const QByteArray& data, qsizetype offset, quint32 fallback)
{
    if (offset < 0 || offset + 4 > data.size())
        return fallback;
    const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

void appendLE16(QByteArray& out, quint16 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

void appendLE32(QByteArray& out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

void appendLE64(QByteArray& out, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        out.append(char((v >> (8 * i)) & 0xFF));
}

QString appDir()
{
    static const QString dir = QCoreApplication::applicationDirPath();
    return dir;
}

QString appDataDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    ensureDir(dir);
    return dir;
}

QString cacheDir()
{
    const QString dir = appDataDir() + QStringLiteral("/cache");
    ensureDir(dir);
    return dir;
}

QString logsDir()
{
    const QString dir = appDataDir() + QStringLiteral("/logs");
    ensureDir(dir);
    return dir;
}

QString dataDir()
{
    const QString dir = appDir() + QStringLiteral("/data");
    ensureDir(dir);
    return dir;
}

QString tempDir(const QString& prefix)
{
    static int counter = 0;
    const QString dir = QDir::tempPath() + QStringLiteral("/xmf_") + prefix + QStringLiteral("_")
                        + QString::number(QCoreApplication::applicationPid()) + QStringLiteral("_")
                        + QString::number(++counter);
    ensureDir(dir);
    return dir;
}

bool isAdmin()
{
    BOOL isMember = FALSE;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0,
                                 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isMember);
        FreeSid(adminGroup);
    }
    return isMember == TRUE;
}

QString osVersion()
{
    return QString::fromWCharArray(L"Windows 10/11");
}

ProcessResult runProcess(const QString& program, const QStringList& arguments, int timeoutMs,
                         const QString& workingDir, const QByteArray& stdinData)
{
    ProcessResult result;
    QProcess process;
    if (!workingDir.isEmpty())
        process.setWorkingDirectory(workingDir);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(10000)) {
        result.stderrData = process.errorString().toUtf8();
        return result;
    }
    result.started = true;
    if (!stdinData.isEmpty()) {
        process.write(stdinData);
        process.closeWriteChannel();
    }
    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished(2000);
    }
    result.exitCode = process.exitCode();
    result.stdoutData = process.readAllStandardOutput();
    result.stderrData = process.readAllStandardError();
    return result;
}

QString findExecutable(const QString& name, const QStringList& extraDirs)
{
    const QString exe = name.endsWith(QStringLiteral(".exe")) ? name : name + QStringLiteral(".exe");
    for (const QString& dir : extraDirs) {
        const QString candidate = QDir(dir).filePath(exe);
        if (QFileInfo::exists(candidate))
            return QDir::toNativeSeparators(candidate);
    }
    const QStringList pathDirs = qEnvironmentVariable("PATH").split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& dir : pathDirs) {
        const QString candidate = QDir(dir.trimmed()).filePath(exe);
        if (QFileInfo::exists(candidate))
            return QDir::toNativeSeparators(candidate);
    }
    return {};
}

void openUrl(const QString& url)
{
    QDesktopServices::openUrl(QUrl(url));
}

void openPath(const QString& path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void revealInExplorer(const QString& path)
{
    if (path.isEmpty())
        return;
    QString arg = QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path));
    QProcess::startDetached(QStringLiteral("explorer.exe"), {arg});
}

bool copyDir(const QString& from, const QString& to)
{
    QDir source(from);
    if (!source.exists())
        return false;
    QDir().mkpath(to);
    const auto entries = source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        const QString dest = QDir(to).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDir(entry.absoluteFilePath(), dest))
                return false;
        } else {
            QFile::remove(dest);
            if (!QFile::copy(entry.absoluteFilePath(), dest))
                return false;
        }
    }
    return true;
}

} // namespace gf::util
