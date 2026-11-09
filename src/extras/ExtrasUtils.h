#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace gf {
class TaskContext;
}

namespace gf::extras {

/// 抛出一个带中文说明的 std::runtime_error（Tool handler 的标准失败方式）。
[[noreturn]] void fail(const QString& message);

// --- Tool option helpers ---------------------------------------------------
QString textOption(const QVariantMap& options, const char* key);
bool boolOption(const QVariantMap& options, const char* key, bool fallback = false);
int intOption(const QVariantMap& options, const char* key, int fallback);
/// Choice 字段格式为 "id — 中文说明"，取第一段 id。
QString choiceOption(const QVariantMap& options, const char* key, const QString& fallback = {});

// --- adb / fastboot helpers ------------------------------------------------
/// 解析 ADB 序列号（空则自动选择唯一在线设备），失败抛中文错误。
QString requireAdbSerial(const QVariantMap& options);
QString requireFastbootSerial(const QVariantMap& options);

/// 在设备上以 root（su -c）执行 shell 命令；命令字符串会被单引号安全包裹。
QString adbSuShell(const QString& serial, const QString& command, QString* error = nullptr,
                   int timeoutMs = 60000);
/// 检测设备是否有 su（返回 su 路径，找不到为空）。
QString findSu(const QString& serial);
/// 以 root 权限判断某个路径是否存在。
bool devicePathExists(const QString& serial, const QString& path);
/// 执行 adb shell 并返回输出（忽略失败细节，用于探测）。
QString adbShellRaw(const QString& serial, const QString& command, int timeoutMs = 30000);

/// 把任意命令字符串安全放进 sh -c 的单引号中。
QString shellQuote(const QString& text);

// --- JSON-ish / text helpers ----------------------------------------------
/// 读取 key=value 形式的文本（用于配置/属性文件），忽略注释与空行。
QMap<QString, QString> parseKeyValueText(const QString& text, QStringList* order = nullptr);

// --- Output helpers --------------------------------------------------------
QString ensureOutputDir(const QString& requested, const QString& fallbackSubdir);
QString uniqueOutputFile(const QString& dir, const QString& baseName, const QString& suffix,
                         const QString& extension = QStringLiteral("txt"));
void saveTextFile(const QString& path, const QString& content);
/// 逐行写日志（限制行数，避免刷屏）。
void logOutputLines(TaskContext& ctx, const QString& output, int limit = 200);

// --- Icons / categories are declared in ToolRegistry.h; no helpers needed.

} // namespace gf::extras
