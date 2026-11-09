#include "ExtrasUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <stdexcept>

#include "core/TaskManager.h"
#include "core/Util.h"
#include "device/AdbClient.h"
#include "device/FastbootClient.h"
#include "device/PlatformTools.h"

namespace gf::extras {

using gf::device::AdbClient;
using gf::device::AdbResult;

void fail(const QString& message)
{
    throw std::runtime_error(message.toStdString());
}

QString textOption(const QVariantMap& options, const char* key)
{
    return options.value(QString::fromLatin1(key)).toString().trimmed();
}

bool boolOption(const QVariantMap& options, const char* key, bool fallback)
{
    const QVariant value = options.value(QString::fromLatin1(key));
    return value.isValid() ? value.toBool() : fallback;
}

int intOption(const QVariantMap& options, const char* key, int fallback)
{
    const QVariant value = options.value(QString::fromLatin1(key));
    bool ok = false;
    const int number = value.toInt(&ok);
    return ok ? number : fallback;
}

QString choiceOption(const QVariantMap& options, const char* key, const QString& fallback)
{
    QString value = textOption(options, key);
    if (value.isEmpty())
        return fallback;
    const int space = value.indexOf(QLatin1Char(' '));
    if (space > 0)
        value = value.left(space);
    return value;
}

QString requireAdbSerial(const QVariantMap& options)
{
    if (!AdbClient::available())
        fail(gf::device::platformToolsMissingMessage(QStringLiteral("adb")));
    QString error;
    const QString serial = AdbClient::resolveSerial(textOption(options, "serial"), &error);
    if (serial.isEmpty())
        fail(error.isEmpty() ? gf::device::noAdbDeviceMessage() : error);
    return serial;
}

QString requireFastbootSerial(const QVariantMap& options)
{
    if (!gf::device::FastbootClient::available())
        fail(gf::device::platformToolsMissingMessage(QStringLiteral("fastboot")));
    QString error;
    const QString serial =
        gf::device::FastbootClient::resolveSerial(textOption(options, "serial"), &error);
    if (serial.isEmpty())
        fail(error.isEmpty() ? gf::device::noFastbootDeviceMessage() : error);
    return serial;
}

QString shellQuote(const QString& text)
{
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString adbSuShell(const QString& serial, const QString& command, QString* error, int timeoutMs)
{
    const QString su = findSu(serial);
    if (su.isEmpty()) {
        if (error)
            *error = QStringLiteral("设备上没有可用的 su，无法执行需要 root 权限的命令。");
        return {};
    }
    const AdbResult result =
        AdbClient::runFor(serial, {QStringLiteral("shell"), QStringLiteral("%1 -c %2").arg(su, shellQuote(command))},
                          timeoutMs);
    if (error)
        *error = result.ok ? QString() : (result.error.isEmpty() ? result.output : result.error);
    return result.output;
}

QString findSu(const QString& serial)
{
    static const QStringList candidates = {
        QStringLiteral("/system/bin/su"), QStringLiteral("/system/xbin/su"),
        QStringLiteral("/sbin/su"),        QStringLiteral("/debug_ramdisk/su"),
        QStringLiteral("/data/adb/ksu/bin/su"),
    };
    QStringList quoted;
    for (const QString& candidate : candidates)
        quoted.append(shellQuote(candidate));
    const QString command = QStringLiteral("for p in %1; do [ -e \"$p\" ] && echo \"$p\"; done")
                                .arg(quoted.join(QLatin1Char(' ')));
    const AdbResult result = AdbClient::shell(serial, command, 20000);
    for (const QString& line : result.output.split(QLatin1Char('\n'))) {
        const QString path = line.trimmed();
        if (!path.isEmpty() && !path.contains(QLatin1Char(' ')))
            return path;
    }
    return {};
}

bool devicePathExists(const QString& serial, const QString& path)
{
    if (path.isEmpty())
        return false;
    const AdbResult result =
        AdbClient::shell(serial, QStringLiteral("[ -e %1 ] && echo yes || echo no").arg(shellQuote(path)),
                         15000);
    return result.output.contains(QStringLiteral("yes"));
}

QString adbShellRaw(const QString& serial, const QString& command, int timeoutMs)
{
    const AdbResult result = AdbClient::shell(serial, command, timeoutMs);
    return result.output;
}

QMap<QString, QString> parseKeyValueText(const QString& text, QStringList* order)
{
    QMap<QString, QString> values;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key.isEmpty())
            continue;
        if (!values.contains(key) && order)
            order->append(key);
        values.insert(key, value);
    }
    return values;
}

QString ensureOutputDir(const QString& requested, const QString& fallbackSubdir)
{
    QString dir = requested.trimmed();
    if (dir.isEmpty())
        dir = gf::util::appDataDir() + QLatin1Char('/') + fallbackSubdir;
    if (!gf::util::ensureDir(dir))
        fail(QStringLiteral("无法创建输出目录：%1").arg(dir));
    return dir;
}

QString uniqueOutputFile(const QString& dir, const QString& baseName, const QString& suffix,
                         const QString& extension)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString name = QStringLiteral("%1_%2_%3").arg(baseName, suffix, stamp);
    return gf::util::uniquePath(QStringLiteral("%1/%2.%3").arg(dir, name, extension));
}

void saveTextFile(const QString& path, const QString& content)
{
    if (!gf::util::writeFile(path, content.toUtf8()))
        fail(QStringLiteral("写入文件失败：%1").arg(path));
}

void logOutputLines(TaskContext& ctx, const QString& output, int limit)
{
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    int count = 0;
    for (const QString& line : lines) {
        ctx.info(line);
        if (++count >= limit) {
            ctx.info(QStringLiteral("……（输出过长，已截断）"));
            break;
        }
    }
}

} // namespace gf::extras
