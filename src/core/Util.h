#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace gf::util {

QString formatBytes(qint64 bytes);
QString formatDuration(qint64 ms);
QString formatSpeed(qint64 bytes, qint64 ms);
QString hex32(quint32 value);
QString hex8(quint8 value);
bool parseHex(const QString& text, quint64* out);
bool parseSize(const QString& text, quint64* out);

bool ensureDir(const QString& path);
bool writeFile(const QString& path, const QByteArray& data);
bool writeFileAtomic(const QString& path, const QByteArray& data);
QByteArray readFile(const QString& path);
QString fileNameOf(const QString& path);
QString baseNameOf(const QString& path);
QString extensionOf(const QString& path);
QString uniquePath(const QString& path);

QString sha256Hex(const QByteArray& data);
QString sha256File(const QString& path);
QString md5File(const QString& path);
QString crc32Hex(const QByteArray& data);

quint16 readLE16(const QByteArray& data, qsizetype offset, quint16 fallback = 0);
quint32 readLE32(const QByteArray& data, qsizetype offset, quint32 fallback = 0);
quint64 readLE64(const QByteArray& data, qsizetype offset, quint64 fallback = 0);
quint32 readBE32(const QByteArray& data, qsizetype offset, quint32 fallback = 0);
void appendLE16(QByteArray& out, quint16 v);
void appendLE32(QByteArray& out, quint32 v);
void appendLE64(QByteArray& out, quint64 v);

QString appDir();
QString appDataDir();
QString cacheDir();
QString logsDir();
QString dataDir();
QString tempDir(const QString& prefix = QStringLiteral("xmf"));

bool isAdmin();
QString osVersion();

// Process helpers (synchronous, used by adb/fastboot/driver tooling).
struct ProcessResult {
    int exitCode = -1;
    bool started = false;
    bool timedOut = false;
    QByteArray stdoutData;
    QByteArray stderrData;
};
ProcessResult runProcess(const QString& program, const QStringList& arguments, int timeoutMs = 120000,
                         const QString& workingDir = {}, const QByteArray& stdinData = {});
QString findExecutable(const QString& name, const QStringList& extraDirs = {});

void openUrl(const QString& url);
void openPath(const QString& path);
void revealInExplorer(const QString& path);
bool copyDir(const QString& from, const QString& to);

} // namespace gf::util
