#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <stdexcept>

namespace gf::img {

/// 进度回调：percent 0..100（-1 表示不确定），status 为当前步骤的中文描述。
using ProgressFn = std::function<void(int percent, const QString& status)>;
/// 取消检查回调：用户取消时应抛出 gf::TaskContext::TaskCanceled。
using CancelFn = std::function<void()>;

constexpr quint64 kKiB = 1024ull;
constexpr quint64 kMiB = 1024ull * kKiB;
constexpr quint64 kGiB = 1024ull * kMiB;

/// 抛出带中文信息的 std::runtime_error。
[[noreturn]] void fail(const QString& message);

/// 182272 -> "178.0 KiB"
QString formatBytes(quint64 bytes);
/// 解析 "0x1000" / "4096" / "4K" / "8 MiB" / "1G" 等写法；失败返回 0 并置 *ok=false。
quint64 parseNumber(const QString& text, bool* ok = nullptr);
/// 十进制/十六进制/带后缀数值 -> 规范化十六进制字符串（默认 "0x" 前缀）。
QString numberToHex(quint64 value, int width = 0, bool withPrefix = true);
/// 解析纯数字或 0x 十六进制；成功返回 true。
bool parseHexValue(const QString& text, quint64* out);

QByteArray readFileOrThrow(const QString& path);
void writeFileOrThrow(const QString& path, const QByteArray& data);
void ensureParentDirectory(const QString& path);
QString joinPath(const QString& dir, const QString& name);
/// 已存在则加 " (2)" 之类后缀，避免覆盖。
QString uniqueOutputPath(const QString& path);

/// 在 out 末尾补 0 直到 size 为 alignment 的整数倍。
void alignTo(QByteArray& out, int alignment, char pad = '\0');
/// 在 QByteArray 末尾追加 4 字节对齐补零。
void padTo4(QByteArray& out);
bool isZero(const QByteArray& data);
bool isZeroRange(const QByteArray& data, qsizetype offset, qsizetype length);

/// 标准 CRC-32（IEEE 802.3，zlib 兼容，初值/终值由内部处理）。
quint32 crc32(const QByteArray& data);
quint32 crc32(const void* data, qsizetype length);
quint32 crc32Init();
quint32 crc32Update(quint32 state, const void* data, qsizetype length);
quint32 crc32Final(quint32 state);
/// zlib adler32（gzip / lzop 使用，初值 1）。
quint32 adler32(const QByteArray& data);
quint32 adler32Update(quint32 state, const void* data, qsizetype length);

/// 经典 16 字节十六进制 + ASCII 视图。
QString hexDump(const QByteArray& data, qsizetype offset = 0, int maxBytes = 256);
/// GUID（GPT/AVB）：前 4/2/2 字节小端，其余按字节顺序。
QString guidString(const QByteArray& raw);
/// 小端 UTF-16LE 字符串（GPT 分区名）。
QString utf16LeToString(const QByteArray& raw);
QByteArray stringToUtf16Le(const QString& text, qsizetype maxChars = -1);

/// 安全回调：未设置时为空操作。
void reportProgress(const ProgressFn& fn, int percent, const QString& status);
void checkCancel(const CancelFn& fn);

/// 读大文件的小工具：按需读取指定区间。
class FileReader {
public:
    explicit FileReader(const QString& path);
    ~FileReader();
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    qint64 size() const { return m_size; }
    QString path() const { return m_path; }
    QByteArray readAt(qint64 offset, qint64 length);
    void readInto(char* buffer, qint64 offset, qint64 length);
    /// 取整个文件（调用方确认尺寸合理）。
    QByteArray readAll();

private:
    QString m_path;
    qint64 m_size = 0;
    void* m_file = nullptr; // QFile*
};

/// 顺序写大文件的工具。
class FileWriter {
public:
    explicit FileWriter(const QString& path, bool truncate = true);
    ~FileWriter();
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    void write(const QByteArray& data);
    void writeAt(qint64 offset, const QByteArray& data);
    void resize(qint64 size);
    qint64 size() const;
    void close();
    QString path() const { return m_path; }

private:
    QString m_path;
    void* m_file = nullptr; // QFile*
};

} // namespace gf::img
