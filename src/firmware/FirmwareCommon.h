#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

namespace gf::firmware {

using ProgressFn = std::function<void(int percent, const QString& status)>;
using CancelFn = std::function<void()>;

/// <data>/firmware 目录：安装后位于 exe 同级 data/，开发时从工作目录向上查找。
QString firmwareDataDir();
QString brandsJsonPath();
QString modelsJsonPath();
/// 用户下载目录（读取 App.settings.downloadDir 使用的同一 QSettings 键）。
QString downloadDir();
/// 数据集/查询缓存目录。
QString cacheDir();

QString formatBytes(qint64 bytes);
/// "315.9 MB" / "1.2 GB" -> 字节数；无法解析返回 -1。
qint64 sizeTextToBytes(const QString& text);

struct FileHashes {
    QString md5;
    QString sha256;
    qint64 size = 0;
};

/// 单次读取文件同时计算 MD5 / SHA256（大固件只读一遍）。
FileHashes hashFile(const QString& path, const CancelFn& cancel = {});
/// 校验文件哈希；actual 返回实际值，error 返回错误说明。
bool verifyFileHash(const QString& path, const QString& expected, QString* actual = nullptr,
                    QString* error = nullptr, const CancelFn& cancel = {});

/// 去掉 Windows 不允许的文件名字符。
QString sanitizeFileName(const QString& name);
/// 从 URL 提取文件名（已 URL 解码），失败返回空。
QString fileNameFromUrl(const QString& url);
/// 保证目录存在，失败返回 false。
bool ensureDirectory(const QString& path);
bool writeFile(const QString& path, const QByteArray& data);
QByteArray readFile(const QString& path);

void reportProgress(const ProgressFn& fn, int percent, const QString& status);
void checkCancel(const CancelFn& fn);

} // namespace gf::firmware
