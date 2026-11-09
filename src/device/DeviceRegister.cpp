#include "app/ToolModules.h"

#include "AdbClient.h"
#include "BatchJobManager.h"
#include "DriverManager.h"
#include "FastbootClient.h"
#include "PlatformTools.h"
#include "Ports.h"

#include "core/TaskManager.h"
#include "core/ToolRegistry.h"
#include "core/Util.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStringConverter>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <atomic>
#include <future>
#include <QFileInfo>

#ifdef Q_OS_WIN
#    include <windows.h>
#endif
#include <vector>
#include <stdexcept>

namespace gf {

namespace {

using namespace gf::device;

/// std::runtime_error only takes std::string; every tool handler reports
/// failures through this helper so messages stay UTF-8 Chinese.
[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toStdString());
}

// ---------------------------------------------------------------------------
// Small helpers shared by the tool handlers
// ---------------------------------------------------------------------------

QString textOption(const QVariantMap& options, const char* key)
{
    return options.value(QString::fromLatin1(key)).toString().trimmed();
}

bool boolOption(const QVariantMap& options, const char* key, bool fallback = false)
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

/// Choice fields are authored as "id — 中文说明"; take the id part.
QString choiceOption(const QVariantMap& options, const char* key, const QString& fallback = {})
{
    QString value = textOption(options, key);
    if (value.isEmpty())
        return fallback;
    const int space = value.indexOf(QLatin1Char(' '));
    if (space > 0)
        value = value.left(space);
    return value;
}

QString ensureOutputDir(const QString& requested, const QString& fallbackSubdir)
{
    QString dir = requested.trimmed();
    if (dir.isEmpty())
        dir = util::appDataDir() + QLatin1Char('/') + fallbackSubdir;
    if (!util::ensureDir(dir))
        fail(QStringLiteral("无法创建输出目录：%1").arg(dir));
    return dir;
}

QString safeToken(const QString& text)
{
    QString token = text.trimmed();
    for (QChar& ch : token) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('.') && ch != QLatin1Char('-')
            && ch != QLatin1Char('_'))
            ch = QLatin1Char('_');
    }
    if (token.isEmpty())
        token = QStringLiteral("device");
    return token;
}

QString uniqueOutputFile(const QString& dir, const QString& baseName, const QString& suffix)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return util::uniquePath(QStringLiteral("%1/%2_%3.%4").arg(dir, baseName, stamp, suffix));
}

void saveTextFile(const QString& path, const QString& content)
{
    if (!util::writeFile(path, content.toUtf8()))
        fail(QStringLiteral("写入文件失败：%1").arg(path));
}

void ensureAdbAvailable()
{
    if (!AdbClient::available())
        fail(platformToolsMissingMessage(QStringLiteral("adb")));
}

void ensureFastbootAvailable()
{
    if (!FastbootClient::available())
        fail(platformToolsMissingMessage(QStringLiteral("fastboot")));
}

QString requireAdbSerial(const QVariantMap& options)
{
    ensureAdbAvailable();
    QString error;
    const QString serial = AdbClient::resolveSerial(textOption(options, "serial"), &error);
    if (serial.isEmpty())
        fail(error.isEmpty() ? noAdbDeviceMessage() : error);
    return serial;
}

QString requireFastbootSerial(const QVariantMap& options)
{
    ensureFastbootAvailable();
    QString error;
    const QString serial = FastbootClient::resolveSerial(textOption(options, "serial"), &error);
    if (serial.isEmpty())
        fail(error.isEmpty() ? noFastbootDeviceMessage() : error);
    return serial;
}

void throwIfFailed(const AdbResult& result, const QString& action)
{
    if (result.ok)
        return;
    QString message = result.error;
    if (message.isEmpty())
        message = result.output;
    if (message.isEmpty())
        message = QStringLiteral("未知错误");
    fail(QStringLiteral("%1失败：%2").arg(action, message));
}

void throwIfFailed(const FastbootResult& result, const QString& action)
{
    if (result.ok)
        return;
    QString message = result.error;
    if (message.isEmpty())
        message = result.output;
    if (message.isEmpty())
        message = QStringLiteral("未知错误");
    fail(QStringLiteral("%1失败：%2").arg(action, message));
}

void logOutputLines(TaskContext& ctx, const QString& output, int limit = 200)
{
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    int count = 0;
    for (const QString& line : lines) {
        ctx.info(line);
        if (++count >= limit) {
            ctx.info(QStringLiteral("……（输出过长，已截断显示）"));
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// ADB tools
// ---------------------------------------------------------------------------

void adbDevices(TaskContext& ctx, const QVariantMap&)
{
    ensureAdbAvailable();
    QString error;
    const auto devices = AdbClient::devices(&error);
    if (devices.isEmpty()) {
        if (!error.isEmpty())
            fail(error);
        fail(noAdbDeviceMessage());
    }
    for (const auto& device : devices) {
        const QString state = device.isOnline() ? QStringLiteral("在线")
                                               : QStringLiteral("状态：%1").arg(device.state);
        ctx.info(QStringLiteral("%1  [%2]").arg(device.displayName(), state));
    }
    const auto online = AdbClient::onlineDevices();
    if (online.isEmpty())
        ctx.warn(QStringLiteral("检测到 ADB 设备，但当前没有一台处于已授权（device）状态。"));
    ctx.ok(QStringLiteral("检测到 %1 台 ADB 设备，其中 %2 台在线。")
               .arg(devices.size())
               .arg(online.size()));
}

void adbServer(TaskContext& ctx, const QVariantMap& options)
{
    ensureAdbAvailable();
    const QString action = choiceOption(options, "action", QStringLiteral("start"));
    AdbResult result;
    if (action == QLatin1String("kill")) {
        result = AdbClient::killServer();
        throwIfFailed(result, QStringLiteral("停止 ADB 服务"));
        ctx.ok(QStringLiteral("ADB 服务已停止。"));
    } else if (action == QLatin1String("restart")) {
        const AdbResult killed = AdbClient::killServer();
        Q_UNUSED(killed);
        result = AdbClient::startServer();
        throwIfFailed(result, QStringLiteral("重启 ADB 服务"));
        ctx.ok(QStringLiteral("ADB 服务已重启。"));
    } else {
        result = AdbClient::startServer();
        throwIfFailed(result, QStringLiteral("启动 ADB 服务"));
        ctx.ok(QStringLiteral("ADB 服务已启动。"));
    }
    if (!result.output.isEmpty())
        ctx.debug(result.output);
}

void adbShell(TaskContext& ctx, const QVariantMap& options)
{
    const QString command = textOption(options, "command");
    if (command.isEmpty())
        fail(QStringLiteral("请填写要执行的 ADB 命令。"));
    const QString serial = requireAdbSerial(options);
    ctx.setStatus(QStringLiteral("在 %1 上执行命令").arg(serial));
    ctx.info(QStringLiteral("$ adb -s %1 shell %2").arg(serial, command));
    const bool execOut = boolOption(options, "execOut", false);
    const AdbResult result = execOut ? AdbClient::execOut(serial, command)
                                     : AdbClient::shell(serial, command);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output);
    throwIfFailed(result, QStringLiteral("执行 ADB 命令"));
    ctx.ok(QStringLiteral("命令执行完成。"));
}

void adbReboot(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString mode = choiceOption(options, "mode", QStringLiteral("system"));
    ctx.setStatus(QStringLiteral("重启设备到 %1").arg(mode));
    throwIfFailed(AdbClient::reboot(serial, mode), QStringLiteral("重启设备"));
    ctx.ok(QStringLiteral("已向 %1 发送重启指令（%2）。").arg(serial, mode));
}

void adbInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    QString error;
    const AdbClient::DeviceInfo info = AdbClient::deviceInfo(serial, &error);
    if (!error.isEmpty())
        fail(error);
    for (const QString& line : info.toLines())
        ctx.info(line);
    if (boolOption(options, "export", true)) {
        const QString dir = ensureOutputDir(textOption(options, "out"), QStringLiteral("device_info"));
        QString saved;
        if (!AdbClient::exportDeviceInfo(serial, dir, &saved, &error))
            fail(error.isEmpty() ? QStringLiteral("导出设备信息失败。") : error);
        ctx.info(QStringLiteral("设备信息已保存：%1").arg(saved));
    }
    ctx.ok(QStringLiteral("已读取 %1 的设备信息。").arg(info.model.isEmpty() ? serial : info.model));
}

void adbPartitions(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    ctx.setStatus(QStringLiteral("读取分区表"));
    QString error;
    const auto partitions = AdbClient::partitions(serial, &error);
    if (partitions.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("未能读取到分区表。") : error);
    for (const auto& partition : partitions) {
        if (partition.target.isEmpty())
            ctx.info(partition.name);
        else
            ctx.info(QStringLiteral("%1 → %2").arg(partition.name, partition.target));
    }
    ctx.ok(QStringLiteral("共读取到 %1 个分区。").arg(partitions.size()));
}

void adbLogs(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString type = choiceOption(options, "type", QStringLiteral("logcat"));
    const QString dir = ensureOutputDir(textOption(options, "out"), QStringLiteral("logs"));

    ctx.setStatus(QStringLiteral("导出 %1").arg(type));
    if (type == QLatin1String("dmesg")) {
        const AdbResult result = AdbClient::dmesg(serial);
        throwIfFailed(result, QStringLiteral("导出 dmesg"));
        const QString path = uniqueOutputFile(dir, QStringLiteral("dmesg_") + safeToken(serial),
                                              QStringLiteral("txt"));
        saveTextFile(path, result.output + QLatin1Char('\n'));
        ctx.ok(QStringLiteral("内核日志已保存：%1").arg(path));
        return;
    }
    if (type == QLatin1String("bugreport")) {
        const QString path = uniqueOutputFile(dir, QStringLiteral("bugreport_") + safeToken(serial),
                                              QStringLiteral("zip"));
        ctx.info(QStringLiteral("正在生成 bugreport（可能需要数分钟）…"));
        const AdbResult result = AdbClient::bugreport(serial, path);
        throwIfFailed(result, QStringLiteral("导出 bugreport"));
        ctx.ok(QStringLiteral("诊断包已保存：%1").arg(path));
        return;
    }
    const int lines = qBound(10, intOption(options, "lines", 500), 100000);
    const AdbResult result = AdbClient::logcat(serial, lines);
    throwIfFailed(result, QStringLiteral("导出 logcat"));
    const QString path = uniqueOutputFile(dir, QStringLiteral("logcat_") + safeToken(serial),
                                          QStringLiteral("txt"));
    saveTextFile(path, result.output + QLatin1Char('\n'));
    ctx.ok(QStringLiteral("日志已保存：%1").arg(path));
}

void adbWireless(TaskContext& ctx, const QVariantMap& options)
{
    const QString action = choiceOption(options, "action", QStringLiteral("tcpip"));
    const int port = qBound(1, intOption(options, "port", 5555), 65535);
    if (action == QLatin1String("connect") || action == QLatin1String("disconnect")) {
        const QString address = textOption(options, "address");
        if (address.isEmpty())
            fail(QStringLiteral("请填写设备 IP 地址（如 192.168.1.20）。"));
        const AdbResult result = action == QLatin1String("connect")
                                     ? AdbClient::connectNetwork(address, port)
                                     : AdbClient::disconnectNetwork(address, port);
        if (!result.output.isEmpty())
            ctx.info(result.output);
        throwIfFailed(result, action == QLatin1String("connect") ? QStringLiteral("连接无线设备")
                                                                 : QStringLiteral("断开无线设备"));
        ctx.ok(QStringLiteral("%1 %2:%3 完成。")
                   .arg(action == QLatin1String("connect") ? QStringLiteral("已连接")
                                                           : QStringLiteral("已断开"),
                        address)
                   .arg(port));
        return;
    }
    const QString serial = requireAdbSerial(options);
    throwIfFailed(AdbClient::enableTcpip(serial, port), QStringLiteral("开启无线调试"));
    const AdbResult ip = AdbClient::shell(serial, QStringLiteral("ip route"), 30000);
    QString address;
    for (const QString& line : ip.output.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        const int src = trimmed.indexOf(QStringLiteral("src "));
        if (src >= 0) {
            address = trimmed.mid(src + 4).trimmed().section(QLatin1Char(' '), 0, 0);
            break;
        }
    }
    ctx.ok(address.isEmpty()
               ? QStringLiteral("无线调试已开启（端口 %1），请断开 USB 后执行：adb connect <设备IP>:%1")
                     .arg(port)
               : QStringLiteral("无线调试已开启，请执行：adb connect %1:%2").arg(address).arg(port));
}

void adbForward(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString action = choiceOption(options, "action", QStringLiteral("add"));
    const QString local = textOption(options, "local");
    if (action == QLatin1String("list")) {
        const AdbResult result = AdbClient::forwardList(serial);
        throwIfFailed(result, QStringLiteral("查看端口转发"));
        logOutputLines(ctx, result.output.isEmpty() ? QStringLiteral("（当前没有端口转发）")
                                                    : result.output);
        ctx.ok(QStringLiteral("端口转发列表已读取。"));
        return;
    }
    if (local.isEmpty())
        fail(QStringLiteral("请填写本地端口（例如 8080 或 tcp:8080）。"));
    if (action == QLatin1String("remove")) {
        throwIfFailed(AdbClient::forwardRemove(serial, local), QStringLiteral("移除端口转发"));
        ctx.ok(QStringLiteral("已移除端口转发 %1。").arg(local));
        return;
    }
    const int remotePort = qBound(1, intOption(options, "remote", 8080), 65535);
    throwIfFailed(AdbClient::forward(serial, local, remotePort), QStringLiteral("添加端口转发"));
    ctx.ok(QStringLiteral("已转发：本机 %1 → 设备 tcp:%2").arg(local).arg(remotePort));
}

void adbPush(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString local = textOption(options, "local");
    if (local.isEmpty())
        fail(QStringLiteral("请选择要推送的本地文件。"));
    if (!QFileInfo::exists(local))
        fail(QStringLiteral("本地文件不存在：%1").arg(local));
    QString remote = textOption(options, "remote");
    if (remote.isEmpty())
        remote = QStringLiteral("/sdcard/");
    ctx.setStatus(QStringLiteral("推送文件"));
    const AdbResult result = AdbClient::push(serial, local, remote);
    if (!result.output.isEmpty())
        ctx.debug(result.output);
    throwIfFailed(result, QStringLiteral("推送文件"));
    ctx.ok(QStringLiteral("已推送 %1 → %2（%3）").arg(QFileInfo(local).fileName(), remote, serial));
}

void adbPull(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString remote = textOption(options, "remote");
    if (remote.isEmpty())
        fail(QStringLiteral("请填写设备上的文件或目录路径。"));
    const QString dir = ensureOutputDir(textOption(options, "out"), QStringLiteral("pull"));
    ctx.setStatus(QStringLiteral("拉取文件"));
    const AdbResult result = AdbClient::pull(serial, remote, dir);
    if (!result.output.isEmpty())
        ctx.debug(result.output);
    throwIfFailed(result, QStringLiteral("拉取文件"));
    ctx.ok(QStringLiteral("已拉取 %1 到 %2。").arg(remote, dir));
}

void adbInstall(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString apk = textOption(options, "apk");
    if (apk.isEmpty() || !QFileInfo::exists(apk))
        fail(QStringLiteral("请选择存在的 APK 文件。"));
    ctx.setStatus(QStringLiteral("安装 APK"));
    const AdbResult result = AdbClient::runFor(
        serial, {QStringLiteral("install"), QStringLiteral("-r"), QDir::toNativeSeparators(apk)},
        300000);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 50);
    throwIfFailed(result, QStringLiteral("安装 APK"));
    ctx.ok(QStringLiteral("APK 安装完成：%1").arg(QFileInfo(apk).fileName()));
}

void registerAdbToolSet()
{
    ToolSpec spec;
    spec.category = QStringLiteral("adb");
    spec.group = QStringLiteral("device");

    spec.id = QStringLiteral("adb.devices");
    spec.name = QStringLiteral("ADB 设备列表");
    spec.desc = QStringLiteral("列出当前连接的 ADB 设备及其授权状态");
    spec.icon = QStringLiteral("smartphone");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("devices"), QStringLiteral("设备")};
    addTool(spec, adbDevices);

    spec.id = QStringLiteral("adb.server");
    spec.name = QStringLiteral("ADB 服务管理");
    spec.desc = QStringLiteral("启动 / 停止 / 重启本机 adb server");
    spec.icon = QStringLiteral("settings");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("server"), QStringLiteral("服务")};
    spec.fields = {
        fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                       {QStringLiteral("start — 启动服务"), QStringLiteral("kill — 停止服务"),
                        QStringLiteral("restart — 重启服务")},
                       0),
    };
    addTool(spec, adbServer);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.shell");
    spec.name = QStringLiteral("ADB 命令执行");
    spec.desc = QStringLiteral("在设备上执行 adb shell / exec-out 命令并输出结果");
    spec.icon = QStringLiteral("terminal");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("shell"), QStringLiteral("命令")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, QStringLiteral("留空则自动选择"),
                     QStringLiteral("多台设备时必填"), false),
        fields::text(QStringLiteral("command"), QStringLiteral("命令"), QStringLiteral("getprop ro.product.model"),
                     QStringLiteral("例如 pm list packages")),
        fields::boolean(QStringLiteral("execOut"), QStringLiteral("使用 exec-out"), false,
                        QStringLiteral("需要原始输出时勾选")),
    };
    addTool(spec, adbShell);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.reboot");
    spec.name = QStringLiteral("ADB 重启设备");
    spec.desc = QStringLiteral("通过 ADB 重启到系统 / Fastboot / Recovery / EDL");
    spec.icon = QStringLiteral("rotate-ccw");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("reboot"), QStringLiteral("重启")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("mode"), QStringLiteral("目标模式"),
                       {QStringLiteral("system — 重启到系统"),
                        QStringLiteral("bootloader — 重启到 Fastboot"),
                        QStringLiteral("recovery — 重启到 Recovery"),
                        QStringLiteral("fastbootd — 重启到 Fastbootd"),
                        QStringLiteral("edl — 重启到 EDL 9008"),
                        QStringLiteral("brom — 重启到 BROM")},
                       0),
    };
    addTool(spec, adbReboot);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.info");
    spec.name = QStringLiteral("读取设备信息");
    spec.desc = QStringLiteral("读取型号、系统版本、指纹、基带等信息并导出");
    spec.icon = QStringLiteral("file-text");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("info"), QStringLiteral("设备信息")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::boolean(QStringLiteral("export"), QStringLiteral("导出到文本文件"), true),
        fields::dir(QStringLiteral("out"), QStringLiteral("输出目录"), QStringLiteral("留空使用用户数据目录"), false),
    };
    addTool(spec, adbInfo);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.partitions");
    spec.name = QStringLiteral("读取分区表");
    spec.desc = QStringLiteral("通过 ADB 读取 /dev/block/by-name 分区列表");
    spec.icon = QStringLiteral("layers");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("partition"), QStringLiteral("分区")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
    };
    addTool(spec, adbPartitions);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.logs");
    spec.name = QStringLiteral("导出设备日志");
    spec.desc = QStringLiteral("导出 logcat / dmesg / bugreport");
    spec.icon = QStringLiteral("file-down");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("logcat"), QStringLiteral("dmesg"),
                     QStringLiteral("bugreport")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("type"), QStringLiteral("日志类型"),
                       {QStringLiteral("logcat — 系统日志"), QStringLiteral("dmesg — 内核日志"),
                        QStringLiteral("bugreport — 完整诊断包")},
                       0),
        fields::integer(QStringLiteral("lines"), QStringLiteral("logcat 行数"), 500, 10, 100000),
        fields::dir(QStringLiteral("out"), QStringLiteral("输出目录"), QStringLiteral("留空使用用户数据目录"), false),
    };
    addTool(spec, adbLogs);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.wireless");
    spec.name = QStringLiteral("无线调试");
    spec.desc = QStringLiteral("开启 ADB over TCP/IP，或连接 / 断开无线设备");
    spec.icon = QStringLiteral("wifi");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("tcpip"), QStringLiteral("wifi"),
                     QStringLiteral("无线")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                       {QStringLiteral("tcpip — 开启无线调试"),
                        QStringLiteral("connect — 连接无线设备"),
                        QStringLiteral("disconnect — 断开无线连接")},
                       0),
        fields::text(QStringLiteral("address"), QStringLiteral("设备 IP"), {}, QStringLiteral("连接时填写"),
                     QStringLiteral("例如 192.168.1.20"), false),
        fields::integer(QStringLiteral("port"), QStringLiteral("端口"), 5555, 1, 65535),
    };
    addTool(spec, adbWireless);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.forward");
    spec.name = QStringLiteral("端口转发");
    spec.desc = QStringLiteral("管理 adb forward 转发规则");
    spec.icon = QStringLiteral("plug-zap");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("forward"), QStringLiteral("转发")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                       {QStringLiteral("add — 添加转发"), QStringLiteral("list — 查看转发"),
                        QStringLiteral("remove — 移除转发")},
                       0),
        fields::text(QStringLiteral("local"), QStringLiteral("本地端口"), QStringLiteral("tcp:8080"), {}, {}, false),
        fields::integer(QStringLiteral("remote"), QStringLiteral("设备端口"), 8080, 1, 65535),
    };
    addTool(spec, adbForward);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.push");
    spec.name = QStringLiteral("推送文件");
    spec.desc = QStringLiteral("把本地文件推送到设备");
    spec.icon = QStringLiteral("file-up");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("push"), QStringLiteral("推送")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::path(QStringLiteral("local"), QStringLiteral("本地文件")),
        fields::text(QStringLiteral("remote"), QStringLiteral("设备路径"), QStringLiteral("/sdcard/"), {}, {}, false),
    };
    addTool(spec, adbPush);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.pull");
    spec.name = QStringLiteral("拉取文件");
    spec.desc = QStringLiteral("从设备拉取文件或目录到本机");
    spec.icon = QStringLiteral("file-down");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("pull"), QStringLiteral("拉取")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::text(QStringLiteral("remote"), QStringLiteral("设备路径"), QStringLiteral("/sdcard/")),
        fields::dir(QStringLiteral("out"), QStringLiteral("保存目录"), QStringLiteral("留空使用用户数据目录"), false),
    };
    addTool(spec, adbPull);
    spec.fields.clear();

    spec.id = QStringLiteral("adb.install");
    spec.name = QStringLiteral("安装 APK");
    spec.desc = QStringLiteral("通过 ADB 安装或覆盖安装 APK");
    spec.icon = QStringLiteral("download");
    spec.keywords = {QStringLiteral("adb"), QStringLiteral("install"), QStringLiteral("apk")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::path(QStringLiteral("apk"), QStringLiteral("APK 文件"), QStringLiteral("*.apk")),
    };
    addTool(spec, adbInstall);
    spec.fields.clear();
}


QString defaultExportPath(const QString& prefix, const QString& suffix)
{
    const QString dir = util::dataDir() + QStringLiteral("/exports");
    util::ensureDir(dir);
    return dir + QLatin1Char('/') + prefix + QLatin1Char('-')
           + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + suffix;
}


void adbMultiShell(TaskContext& ctx, const QVariantMap& options)
{
    const QString command = textOption(options, "command").trimmed();
    if (command.isEmpty())
        fail(QStringLiteral("请填写要在所有设备上执行的命令"));
    ensureAdbAvailable();
    QString error;
    QList<AdbDevice> devices = AdbClient::onlineDevices(&error);
    if (devices.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("没有在线 ADB 设备") : error);
    if (devices.size() > 8) {
        ctx.warn(QStringLiteral("在线设备超过 8 台，仅处理前 8 台"));
        devices = devices.mid(0, 8);
    }
    ctx.info(QStringLiteral("在 %1 台设备上并行执行：%2").arg(devices.size()).arg(command));

    struct Result {
        QString serial;
        AdbResult adb;
    };
    std::vector<std::future<Result>> futures;
    futures.reserve(size_t(devices.size()));
    for (const AdbDevice& device : devices) {
        const QString serial = device.serial;
        futures.push_back(std::async(std::launch::async, [serial, command]() -> Result {
            Result result;
            result.serial = serial;
            result.adb = AdbClient::shell(serial, command);
            return result;
        }));
    }

    int done = 0;
    int failed = 0;
    for (auto& future : futures) {
        ctx.checkCanceled();
        const Result result = future.get();
        ++done;
        ctx.setProgress(int(100.0 * done / devices.size()));
        ctx.info(QStringLiteral("=== %1 ===").arg(result.serial));
        if (!result.adb.output.isEmpty())
            logOutputLines(ctx, result.adb.output, 100);
        if (result.adb.ok) {
            ctx.ok(QStringLiteral("%1 执行完成").arg(result.serial));
        } else {
            ++failed;
            ctx.warn(QStringLiteral("%1 执行失败：%2")
                         .arg(result.serial,
                              (result.adb.error.isEmpty() ? result.adb.output : result.adb.error).trimmed()));
        }
    }
    ctx.setProgress(100);
    if (failed == devices.size())
        fail(QStringLiteral("所有设备执行失败"));
    ctx.ok(QStringLiteral("多设备执行完成：%1 台成功 / %2 台失败").arg(devices.size() - failed).arg(failed));
}


void adbSideload(TaskContext& ctx, const QVariantMap& options)
{
    ensureAdbAvailable();
    const QString package = textOption(options, "package");
    if (package.isEmpty() || !QFileInfo::exists(package))
        fail(QStringLiteral("请选择要卡刷的 zip 包"));
    QString serial = textOption(options, "serial");
    QString error;
    if (serial.isEmpty())
        serial = AdbClient::resolveSerial(QString(), &error);
    if (serial.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("没有检测到 ADB 设备") : error);

    const QList<AdbDevice> devices = AdbClient::devices();
    QString state;
    for (const AdbDevice& device : devices) {
        if (device.serial == serial) {
            state = device.state;
            break;
        }
    }
    if (state != QLatin1String("sideload")) {
        ctx.warn(QStringLiteral("当前设备状态为「%1」，不是 sideload。请在 Recovery 中开启 ADB Sideload，"
                                "或在第三方 Recovery 中选择 Apply update from ADB。")
                     .arg(state.isEmpty() ? QStringLiteral("未知") : state));
    }
    ctx.info(QStringLiteral("正在通过 sideload 推送：%1（%2）")
                 .arg(QFileInfo(package).fileName(), util::formatBytes(QFileInfo(package).size())));
    ctx.setStatus(QStringLiteral("sideload 刷入中，请勿断开数据线"));
    const AdbResult result = AdbClient::sideload(serial, package);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 100);
    throwIfFailed(result, QStringLiteral("sideload 刷入"));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("sideload 刷入完成（%1）").arg(QFileInfo(package).fileName()));
}

void adbWaitDevice(TaskContext& ctx, const QVariantMap& options)
{
    ensureAdbAvailable();
    const QString serial = textOption(options, "serial");
    const int seconds = qBound(5, intOption(options, "timeout", 120), 1800);
    ctx.info(QStringLiteral("等待设备重新上线（最多 %1 秒）…").arg(seconds));
    ctx.setStatus(QStringLiteral("等待设备"));
    const AdbResult result = AdbClient::waitForDevice(serial, seconds * 1000);
    if (!result.ok)
        throwIfFailed(result, QStringLiteral("等待设备"));
    ctx.ok(QStringLiteral("设备已上线，可以继续下一步"));
    ctx.setProgress(100);
}

void adbBugreport(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    QString output = textOption(options, "output");
    if (output.isEmpty())
        output = defaultExportPath(QStringLiteral("bugreport-") + serial, QStringLiteral(".zip"));
    if (!util::ensureDir(QFileInfo(output).absolutePath()))
        fail(QStringLiteral("无法创建输出目录：%1").arg(QFileInfo(output).absolutePath()));
    ctx.setStatus(QStringLiteral("抓取 bugreport（可能需 1-5 分钟）…"));
    const AdbResult result = AdbClient::runFor(
        serial, {QStringLiteral("bugreport"), QDir::toNativeSeparators(output)}, 900000);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 25);
    throwIfFailed(result, QStringLiteral("抓取 bugreport"));
    const QFileInfo info(output);
    if (!info.exists() || info.size() == 0)
        fail(QStringLiteral("bugreport 未生成（%1）").arg(output));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("bugreport 已保存：%1（%2）").arg(output, util::formatBytes(info.size())));
}

void adbDmesg(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    QString output = textOption(options, "output");
    if (output.isEmpty())
        output = defaultExportPath(QStringLiteral("dmesg-") + serial, QStringLiteral(".txt"));
    ctx.setStatus(QStringLiteral("读取内核日志…"));
    const AdbResult result = AdbClient::shell(serial, QStringLiteral("dmesg"));
    if (!result.ok && result.output.trimmed().isEmpty())
        fail(result.error.isEmpty()
                 ? QStringLiteral("dmesg 读取失败（部分机型需要 root 或权限）")
                 : result.error);
    if (result.output.trimmed().isEmpty())
        fail(QStringLiteral("dmesg 输出为空（可能被内核限制）"));
    if (!util::writeFile(output, result.output.toUtf8()))
        fail(QStringLiteral("无法写入 %1").arg(output));
    const int lines = result.output.count(QLatin1Char('\n'));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("内核日志已保存：%1（%2 行）").arg(output).arg(lines));
}

void adbAppControl(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString package = textOption(options, "package").trimmed();
    if (package.isEmpty())
        fail(QStringLiteral("请填写应用包名，例如 com.example.app"));
    const QString action = choiceOption(options, "action", QStringLiteral("冻结（禁用）"));
    QString command;
    if (action.contains(QStringLiteral("冻结")))
        command = QStringLiteral("pm disable-user --user 0 %1").arg(package);
    else if (action.contains(QStringLiteral("解冻")))
        command = QStringLiteral("pm enable %1").arg(package);
    else
        command = QStringLiteral("pm uninstall -k --user 0 %1").arg(package);
    ctx.setStatus(QStringLiteral("执行：%1").arg(action));
    ctx.info(QStringLiteral("$ adb -s %1 shell %2").arg(serial, command));
    const AdbResult result = AdbClient::shell(serial, command);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output);
    throwIfFailed(result, action);
    if (result.output.contains(QStringLiteral("Error"), Qt::CaseInsensitive)
        || result.output.contains(QStringLiteral("Failure"), Qt::CaseInsensitive))
        fail(QStringLiteral("系统返回失败：%1").arg(result.output.trimmed()));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("%1 完成：%2").arg(action, package));
}

void adbApkExtract(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString package = textOption(options, "package").trimmed();
    if (package.isEmpty())
        fail(QStringLiteral("请填写应用包名"));
    QString outputDir = textOption(options, "output");
    if (outputDir.isEmpty())
        outputDir = util::dataDir() + QStringLiteral("/exports/apk-") + package;
    if (!util::ensureDir(outputDir))
        fail(QStringLiteral("无法创建目录：%1").arg(outputDir));
    ctx.setStatus(QStringLiteral("查询 APK 路径…"));
    const AdbResult paths = AdbClient::shell(serial, QStringLiteral("pm path %1").arg(package));
    throwIfFailed(paths, QStringLiteral("查询 APK 路径"));
    QStringList remotes;
    for (const QString& line : paths.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("package:")))
            remotes << trimmed.mid(8);
    }
    if (remotes.isEmpty())
        fail(QStringLiteral("未找到应用 %1 的 APK（包名是否正确？）").arg(package));
    int pulled = 0;
    for (const QString& remote : remotes) {
        ctx.setStatus(QStringLiteral("拉取 %1").arg(QFileInfo(remote).fileName()));
        const AdbResult result = AdbClient::runFor(
            serial, {QStringLiteral("pull"), remote, QDir::toNativeSeparators(outputDir)}, 300000);
        if (result.ok)
            ++pulled;
        else
            ctx.warn(QStringLiteral("拉取失败：%1").arg(remote));
    }
    if (pulled == 0)
        fail(QStringLiteral("所有 APK 拉取失败"));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("已导出 %1 个 APK 到 %2").arg(pulled).arg(outputDir));
}

void adbDeviceReport(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    QString output = textOption(options, "output");
    if (output.isEmpty())
        output = defaultExportPath(QStringLiteral("deviceinfo-") + serial, QStringLiteral(".txt"));
    QStringList lines;
    lines << QStringLiteral("# XMF ADB 设备信息报告")
          << QStringLiteral("# serial: %1").arg(serial)
          << QStringLiteral("# time: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));

    const struct {
        const char* title;
        QString command;
    } sections[] = {
        {"电池", QStringLiteral("dumpsys battery")},
        {"传感器服务", QStringLiteral("dumpsys sensorservice")},
        {"显示", QStringLiteral("dumpsys display | head -n 40")},
        {"系统属性", QStringLiteral("getprop ro.product.brand; getprop ro.product.model; getprop ro.build.version.release; getprop ro.build.version.sdk; getprop ro.build.display.id")},
    };
    for (const auto& section : sections) {
        lines << QString() << QStringLiteral("== %1 ==").arg(QString::fromUtf8(section.title));
        const AdbResult result = AdbClient::shell(serial, section.command);
        const QStringList rows = result.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const int limit = 120;
        for (int i = 0; i < rows.size() && i < limit; ++i)
            lines << rows.at(i);
        if (rows.size() > limit)
            lines << QStringLiteral("…（截断，共 %1 行）").arg(rows.size());
    }
    if (!util::writeFile(output, lines.join(QLatin1Char('\n')).toUtf8()))
        fail(QStringLiteral("无法写入 %1").arg(output));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("设备信息报告已保存：%1").arg(output));
}


void registerAdbExtrasToolSet()
{
    ToolSpec spec;
    spec.category = QStringLiteral("adb");
    {
        spec.id = QStringLiteral("adb.bugreport");
        spec.name = QStringLiteral("抓取 bugreport");
        spec.desc = QStringLiteral("通过 adb bugreport 生成完整诊断包（zip），用于反馈问题");
        spec.icon = QStringLiteral("file-archive");
        spec.keywords = {QStringLiteral("bugreport"), QStringLiteral("日志"), QStringLiteral("诊断")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
            fields::path(QStringLiteral("output"), QStringLiteral("保存为 zip（可选）"),
                         QStringLiteral("*.zip"), QString(), false),
        };
        addTool(spec, adbBugreport);
        spec.fields.clear();
    }
    {
        spec.id = QStringLiteral("adb.dmesg");
        spec.name = QStringLiteral("导出内核日志");
        spec.desc = QStringLiteral("读取 dmesg 内核日志并保存为文本（部分机型需要权限）");
        spec.icon = QStringLiteral("terminal");
        spec.keywords = {QStringLiteral("dmesg"), QStringLiteral("内核"), QStringLiteral("日志")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
            fields::path(QStringLiteral("output"), QStringLiteral("保存为文本（可选）"),
                         QStringLiteral("*.txt"), QString(), false),
        };
        addTool(spec, adbDmesg);
        spec.fields.clear();
    }
    {
        spec.id = QStringLiteral("adb.app.control");
        spec.name = QStringLiteral("应用冻结 / 卸载");
        spec.desc = QStringLiteral("按包名冻结、解冻或卸载当前用户的应用（pm disable-user / uninstall）");
        spec.icon = QStringLiteral("package");
        spec.keywords = {QStringLiteral("应用"), QStringLiteral("冻结"), QStringLiteral("卸载")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
            fields::text(QStringLiteral("package"), QStringLiteral("应用包名"),
                         {}, QStringLiteral("com.example.app")),
            fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                           {QStringLiteral("冻结（禁用）"), QStringLiteral("解冻（恢复）"),
                            QStringLiteral("卸载当前用户")}, 0),
        };
        addTool(spec, adbAppControl);
        spec.fields.clear();
    }
    {
        spec.id = QStringLiteral("adb.apk.extract");
        spec.name = QStringLiteral("导出应用 APK");
        spec.desc = QStringLiteral("按包名定位并拉取已安装的 APK（含系统预置应用）");
        spec.icon = QStringLiteral("download");
        spec.keywords = {QStringLiteral("apk"), QStringLiteral("提取"), QStringLiteral("系统应用")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
            fields::text(QStringLiteral("package"), QStringLiteral("应用包名"), {}, QStringLiteral("com.android.settings")),
            fields::dir(QStringLiteral("output"), QStringLiteral("输出目录（可选）"), QString(), false),
        };
        addTool(spec, adbApkExtract);
        spec.fields.clear();
    }
    {
        spec.id = QStringLiteral("adb.device.report");
        spec.name = QStringLiteral("导出设备硬件信息");
        spec.desc = QStringLiteral("汇总电池、传感器、显示与系统属性并保存为文本报告");
        spec.icon = QStringLiteral("file-text");
        spec.keywords = {QStringLiteral("电池"), QStringLiteral("传感器"), QStringLiteral("硬件信息")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
            fields::path(QStringLiteral("output"), QStringLiteral("保存为文本（可选）"),
                         QStringLiteral("*.txt"), QString(), false),
        };
        addTool(spec, adbDeviceReport);
        spec.fields.clear();

    {
        spec.id = QStringLiteral("adb.sideload");
        spec.name = QStringLiteral("Recovery 卡刷（sideload）");
        spec.desc = QStringLiteral("在 Recovery 的 ADB Sideload 状态下刷入 OTA/卡刷 zip 包");
        spec.icon = QStringLiteral("package-open");
        spec.keywords = {QStringLiteral("adb"), QStringLiteral("recovery"), QStringLiteral("sideload"),
                         QStringLiteral("卡刷")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号（留空自动检测）"), {}, {}, {}, false),
            fields::path(QStringLiteral("package"), QStringLiteral("卡刷 / OTA zip 包"),
                         QStringLiteral("*.zip")),
        };
        addTool(spec, adbSideload);
        spec.fields.clear();
    }
    {
        spec.id = QStringLiteral("adb.waitDevice");
        spec.name = QStringLiteral("等待设备上线");
        spec.desc = QStringLiteral("等待 ADB 设备重新出现（例如重启进入 Recovery 之后）");
        spec.icon = QStringLiteral("clock");
        spec.keywords = {QStringLiteral("adb"), QStringLiteral("wait-for-device"), QStringLiteral("等待")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号（留空=任意设备）"), {}, {}, {}, false),
            fields::integer(QStringLiteral("timeout"), QStringLiteral("超时（秒）"), 120, 5, 1800),
        };
        addTool(spec, adbWaitDevice);
        spec.fields.clear();
    }

    {
        spec.id = QStringLiteral("adb.multiShell");
        spec.name = QStringLiteral("多设备并行命令");
        spec.desc = QStringLiteral("在所有在线 ADB 设备上并行执行同一条 shell 命令并汇总输出");
        spec.icon = QStringLiteral("terminal");
        spec.keywords = {QStringLiteral("adb"), QStringLiteral("批量"), QStringLiteral("多设备")};
        spec.fields = {
            fields::text(QStringLiteral("command"), QStringLiteral("shell 命令"),
                         QStringLiteral("getprop ro.product.model"), {}, {}, true),
        };
        addTool(spec, adbMultiShell);
        spec.fields.clear();
    }
    }
}



void fastbootBattery(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    ctx.setStatus(QStringLiteral("读取电池与充电信息"));
    const QMap<QString, QString> vars = FastbootClient::getvarAll(serial, nullptr);
    if (vars.isEmpty())
        fail(QStringLiteral("设备没有返回 getvar 变量"));
    const QString voltage = vars.value(QStringLiteral("battery-voltage"));
    const QString socOk = vars.value(QStringLiteral("battery-soc-ok"));
    const QString charger = vars.value(QStringLiteral("charger-screen-enabled"));
    const QString offMode = vars.value(QStringLiteral("off-mode-charge"));
    if (!voltage.isEmpty())
        ctx.info(QStringLiteral("电池电压：%1 mV").arg(voltage));
    if (!socOk.isEmpty())
        ctx.info(QStringLiteral("电量状态：%1").arg(socOk == QLatin1String("yes") ? QStringLiteral("正常")
                                                                                   : QStringLiteral("异常（低电量/未接电池）")));
    if (!charger.isEmpty())
        ctx.info(QStringLiteral("充电屏：%1").arg(charger));
    if (!offMode.isEmpty())
        ctx.info(QStringLiteral("关机充电：%1").arg(offMode));
    const QString issue = FastbootClient::batteryIssue(serial);
    if (!issue.isEmpty())
        ctx.warn(issue);
    else
        ctx.ok(QStringLiteral("电池状态正常，可安全刷写/重启"));
    ctx.setProgress(100);
}

void fastbootPartitionsExport(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    QString output = textOption(options, "output");
    if (output.isEmpty()) {
        util::ensureDir(util::dataDir() + QStringLiteral("/exports"));
        output = util::dataDir() + QStringLiteral("/exports/fastboot_partitions_") + serial
                 + QStringLiteral(".csv");
    }
    if (!util::ensureDir(QFileInfo(output).absolutePath()))
        fail(QStringLiteral("无法创建目录：%1").arg(QFileInfo(output).absolutePath()));
    QString error;
    const QList<FastbootPartition> parts = FastbootClient::partitions(serial, &error);
    if (parts.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("设备没有返回分区") : error);
    QByteArray csv = "\xEF\xBB\xBF";
    csv += "name,size,type,slot,logical\n";
    for (const FastbootPartition& part : parts) {
        csv += part.name.toUtf8() + ',';
        csv += part.size.toUtf8() + ',';
        csv += part.type.toUtf8() + ',';
        csv += part.slot.toUtf8() + ',';
        csv += part.logical ? "true\n" : "false\n";
    }
    if (!util::writeFile(output, csv))
        fail(QStringLiteral("无法写入 %1").arg(output));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("已导出 %1 个分区到 %2").arg(parts.size()).arg(output));
}


void fastbootFlashCheck(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    const QString partition = textOption(options, "partition");
    const QString image = textOption(options, "image");
    if (partition.isEmpty())
        fail(QStringLiteral("请填写分区名"));
    if (image.isEmpty() || !QFileInfo::exists(image))
        fail(QStringLiteral("请选择存在的镜像文件"));
    ctx.info(QStringLiteral("只读检查，不会向设备写入任何数据。"));

    const QFileInfo info(image);
    quint64 partitionBytes = 0;
    QString partitionError;
    const bool exists = FastbootClient::partitionSize(serial, partition, &partitionBytes, &partitionError);
    if (!exists)
        fail(partitionError);
    ctx.info(QStringLiteral("目标分区 %1 容量：%2").arg(partition, util::formatBytes(partitionBytes)));
    ctx.info(QStringLiteral("镜像大小：%1（%2）").arg(util::formatBytes(info.size()), info.fileName()));

    QFile file(image);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("无法读取镜像：%1").arg(image));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (hash.addData(&file))
        ctx.info(QStringLiteral("SHA256：%1").arg(QString::fromLatin1(hash.result().toHex())));
    file.close();

    if (quint64(info.size()) > partitionBytes)
        ctx.error(QStringLiteral("镜像大于分区容量，刷写会被拒绝"));
    else
        ctx.ok(QStringLiteral("容量检查通过（镜像占分区 %1%）")
                   .arg(partitionBytes > 0 ? int(100.0 * double(info.size()) / double(partitionBytes)) : 0));

    const QString batteryIssue = FastbootClient::batteryIssue(serial);
    if (!batteryIssue.isEmpty())
        ctx.warn(batteryIssue + QStringLiteral("；请先充电再刷写"));
    else
        ctx.ok(QStringLiteral("电池状态正常"));

    ctx.setProgress(100);
    ctx.ok(QStringLiteral("刷写前检查完成：分区存在、容量与电量均已检查"));
}

// ---------------------------------------------------------------------------
// Fastboot tools




// ---------------------------------------------------------------------------

void fastbootDevices(TaskContext& ctx, const QVariantMap&)
{
    ensureFastbootAvailable();
    QString error;
    const auto devices = FastbootClient::devices(&error);
    if (devices.isEmpty()) {
        if (!error.isEmpty())
            fail(error);
        fail(noFastbootDeviceMessage());
    }
    for (const auto& device : devices)
        ctx.info(device.displayName());
    ctx.ok(QStringLiteral("检测到 %1 台 Fastboot 设备。").arg(devices.size()));
}

void fastbootGetvar(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    QString variable = textOption(options, "variable");
    if (variable.isEmpty())
        variable = QStringLiteral("all");
    ctx.setStatus(QStringLiteral("读取变量 %1").arg(variable));
    const FastbootResult result = FastbootClient::getvar(serial, variable);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 400);
    throwIfFailed(result, QStringLiteral("读取 Fastboot 变量"));
    ctx.ok(QStringLiteral("变量 %1 读取完成。").arg(variable));
}

void fastbootPartitions(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    ctx.setStatus(QStringLiteral("读取分区表"));
    QString error;
    const auto partitions = FastbootClient::partitions(serial, &error);
    if (partitions.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("未读取到分区表。") : error);
    for (const auto& partition : partitions) {
        ctx.info(QStringLiteral("%1  大小=%2  类型=%3%4")
                     .arg(partition.name, partition.size.isEmpty() ? QStringLiteral("?") : partition.size,
                          partition.type.isEmpty() ? QStringLiteral("?") : partition.type,
                          partition.logical ? QStringLiteral("  [逻辑分区]") : QString()));
    }
    ctx.ok(QStringLiteral("共读取到 %1 个分区。").arg(partitions.size()));
}

void fastbootFlash(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    const QString partition = textOption(options, "partition");
    const QString image = textOption(options, "image");
    if (partition.isEmpty())
        fail(QStringLiteral("请填写要刷写的分区名（如 boot / recovery）。"));
    if (image.isEmpty() || !QFileInfo::exists(image))
        fail(QStringLiteral("请选择存在的镜像文件。"));
    const QString batteryIssue = FastbootClient::batteryIssue(serial);
    if (!batteryIssue.isEmpty())
        fail(batteryIssue + QStringLiteral("；请先充电再刷写，避免中途断电变砖"));
    quint64 partitionBytes = 0;
    QString partitionError;
    if (!FastbootClient::partitionSize(serial, partition, &partitionBytes, &partitionError))
        fail(partitionError);
    const qint64 imageBytes = QFileInfo(image).size();
    if (qint64(partitionBytes) < imageBytes) {
        fail(QStringLiteral("镜像 %1 字节大于分区 %2 的容量 %3 字节，已拒绝刷写")
                 .arg(imageBytes).arg(partition).arg(partitionBytes));
    }
    ctx.info(QStringLiteral("分区 %1 容量 %2 · 镜像 %3")
                 .arg(partition, util::formatBytes(partitionBytes), util::formatBytes(imageBytes)));
    ctx.setStatus(QStringLiteral("刷写 %1").arg(partition));
    ctx.info(QStringLiteral("$ fastboot -s %1 flash %2 %3").arg(serial, partition, image));
    const FastbootResult result = FastbootClient::flash(serial, partition, image);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 100);
    throwIfFailed(result, QStringLiteral("刷写分区 %1").arg(partition));
    ctx.ok(QStringLiteral("分区 %1 刷写完成（%2）。").arg(partition, QFileInfo(image).fileName()));
}

void fastbootErase(TaskContext& ctx, const QVariantMap& options)
{
    if (!boolOption(options, "confirm", false))
        fail(QStringLiteral("擦除分区会丢失数据，请先勾选「确认擦除分区」。"));
    const QString serial = requireFastbootSerial(options);
    const QString partition = textOption(options, "partition");
    if (partition.isEmpty())
        fail(QStringLiteral("请填写要擦除的分区名。"));
    ctx.setStatus(QStringLiteral("擦除 %1").arg(partition));
    const FastbootResult result = FastbootClient::erase(serial, partition);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 50);
    throwIfFailed(result, QStringLiteral("擦除分区 %1").arg(partition));
    ctx.ok(QStringLiteral("分区 %1 已擦除。").arg(partition));
}

void fastbootReboot(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    const QString mode = choiceOption(options, "mode", QStringLiteral("system"));
    const QString batteryIssue = FastbootClient::batteryIssue(serial);
    if (!batteryIssue.isEmpty())
        ctx.warn(batteryIssue);
    ctx.setStatus(QStringLiteral("重启设备"));
    throwIfFailed(FastbootClient::reboot(serial, mode), QStringLiteral("重启设备"));
    ctx.ok(QStringLiteral("已发送重启指令（%1）。").arg(mode));
}

void fastbootLock(TaskContext& ctx, const QVariantMap& options)
{
    if (!boolOption(options, "confirm", false))
        fail(QStringLiteral("解锁/上锁 Bootloader 有风险，请先勾选「确认执行」。"));
    const QString serial = requireFastbootSerial(options);
    const QString action = choiceOption(options, "action", QStringLiteral("unlock"));
    const bool lock = action == QLatin1String("lock");
    ctx.setStatus(lock ? QStringLiteral("上锁 Bootloader") : QStringLiteral("解锁 Bootloader"));
    const FastbootResult result = FastbootClient::flashingLock(serial, lock);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 100);
    throwIfFailed(result, lock ? QStringLiteral("上锁 Bootloader") : QStringLiteral("解锁 Bootloader"));
    if (lock)
        ctx.ok(QStringLiteral("已发送上锁指令，设备可能会清除全部数据。"));
    else
        ctx.ok(QStringLiteral("已发送解锁指令，请在设备屏幕上确认。"));
}

void fastbootInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    const QString dir = ensureOutputDir(textOption(options, "out"), QStringLiteral("fastboot_info"));
    QString saved;
    QString error;
    ctx.setStatus(QStringLiteral("导出 Fastboot 信息"));
    if (!FastbootClient::exportInfo(serial, dir, &saved, &error))
        fail(error.isEmpty() ? QStringLiteral("导出失败。") : error);
    ctx.info(QStringLiteral("已保存：%1").arg(saved));
    ctx.ok(QStringLiteral("Fastboot 设备信息导出完成。"));
}

void fastbootOem(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireFastbootSerial(options);
    const QString command = textOption(options, "command");
    if (command.isEmpty())
        fail(QStringLiteral("请填写 OEM 命令（例如 device-info）。"));
    const FastbootResult result = FastbootClient::oemCommand(serial, command);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 200);
    throwIfFailed(result, QStringLiteral("执行 OEM 命令"));
    ctx.ok(QStringLiteral("OEM 命令执行完成。"));
}

void registerFastbootToolSet()
{
    ToolSpec spec;
    spec.category = QStringLiteral("adb");
    spec.group = QStringLiteral("fastboot");

    spec.id = QStringLiteral("fastboot.devices");
    spec.name = QStringLiteral("Fastboot 设备列表");
    spec.desc = QStringLiteral("列出处于 Bootloader / Fastboot 模式的设备");
    spec.icon = QStringLiteral("plug");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("devices")};
    addTool(spec, fastbootDevices);

    spec.id = QStringLiteral("fastboot.getvar");
    spec.name = QStringLiteral("读取 Fastboot 变量");
    spec.desc = QStringLiteral("读取单个 getvar 变量或 getvar all");
    spec.icon = QStringLiteral("file-text");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("getvar")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::text(QStringLiteral("variable"), QStringLiteral("变量名"), QStringLiteral("all"), {}, {}, false),
    };
    addTool(spec, fastbootGetvar);
    spec.fields.clear();

    spec.id = QStringLiteral("fastboot.partitions");
    spec.name = QStringLiteral("读取分区表");
    spec.desc = QStringLiteral("解析 getvar all 得到分区大小与类型");
    spec.icon = QStringLiteral("layers");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("partition"), QStringLiteral("分区")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
    };
    addTool(spec, fastbootPartitions);
    spec.fields.clear();

    spec.id = QStringLiteral("fastboot.flash");
    spec.name = QStringLiteral("刷写分区");
    spec.desc = QStringLiteral("fastboot flash <分区> <镜像>");
    spec.icon = QStringLiteral("file-up");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("flash"), QStringLiteral("刷写")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::text(QStringLiteral("partition"), QStringLiteral("分区名"), QStringLiteral("boot"),
                     QStringLiteral("例如 recovery / super")),
        fields::path(QStringLiteral("image"), QStringLiteral("镜像文件"), QStringLiteral("*.img")),
    };
    addTool(spec, fastbootFlash);
    spec.fields.clear();

    spec.id = QStringLiteral("fastboot.erase");
    spec.name = QStringLiteral("擦除分区");
    spec.desc = QStringLiteral("fastboot erase <分区>（危险操作）");
    spec.icon = QStringLiteral("shield-alert");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("erase"), QStringLiteral("擦除")};
    spec.badge = QStringLiteral("危险");
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::text(QStringLiteral("partition"), QStringLiteral("分区名"), {}, QStringLiteral("例如 userdata")),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("确认擦除分区"), false),
    };
    addTool(spec, fastbootErase);
    spec.fields.clear();

    spec.id = QStringLiteral("fastboot.reboot");
    spec.name = QStringLiteral("Fastboot 重启");
    spec.desc = QStringLiteral("重启到系统 / Bootloader / Recovery / EDL");
    spec.icon = QStringLiteral("rotate-ccw");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("reboot"), QStringLiteral("重启")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("mode"), QStringLiteral("目标模式"),
                       {QStringLiteral("system — 重启到系统"),
                        QStringLiteral("bootloader — 重启到 Bootloader"),
                        QStringLiteral("recovery — 重启到 Recovery"),
                        QStringLiteral("fastbootd — 重启到 Fastbootd"),
                        QStringLiteral("edl — 重启到 EDL 9008"),
                        QStringLiteral("poweroff — 关机")},
                       0),
    };
    addTool(spec, fastbootReboot);
    spec.fields.clear();

    spec.id = QStringLiteral("fastboot.lock");
    spec.name = QStringLiteral("Bootloader 解锁 / 上锁");
    spec.desc = QStringLiteral("fastboot flashing unlock / lock（可能清除数据）");
    spec.icon = QStringLiteral("key-round");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("unlock"), QStringLiteral("解锁")};
    spec.badge = QStringLiteral("危险");
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                       {QStringLiteral("unlock — 解锁 Bootloader"),
                        QStringLiteral("lock — 重新上锁")},
                       0),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("确认执行（会清除数据）"), false),
    };
    addTool(spec, fastbootLock);
    spec.fields.clear();

    spec.id = QStringLiteral("fastboot.info");
    spec.name = QStringLiteral("导出 Fastboot 信息");
    spec.desc = QStringLiteral("导出 getvar all 与分区表到文本文件");
    spec.icon = QStringLiteral("file-down");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("info")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::dir(QStringLiteral("out"), QStringLiteral("输出目录"), QStringLiteral("留空使用用户数据目录"), false),
    };
    addTool(spec, fastbootInfo);
    spec.fields.clear();

    {
        spec.id = QStringLiteral("fastboot.flash.check");
        spec.name = QStringLiteral("刷写前检查");
        spec.desc = QStringLiteral("只读检查目标分区、镜像容量、SHA256 与电量，再决定是否刷写");
        spec.icon = QStringLiteral("shield-check");
        spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("check"), QStringLiteral("安全检查")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
            fields::text(QStringLiteral("partition"), QStringLiteral("分区名"), {},
                         QStringLiteral("例如 boot / recovery")),
            fields::path(QStringLiteral("image"), QStringLiteral("镜像文件"), QStringLiteral("*.img *.bin")),
        };
        addTool(spec, fastbootFlashCheck);
        spec.fields.clear();
    }

    {
        spec.id = QStringLiteral("fastboot.battery");
        spec.name = QStringLiteral("电池与充电状态");
        spec.desc = QStringLiteral("读取 fastboot 暴露的电池电压、电量状态与充电屏设置（只读）");
        spec.icon = QStringLiteral("battery-charging");
        spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("battery"), QStringLiteral("电量")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        };
        addTool(spec, fastbootBattery);
        spec.fields.clear();
    }
    {
        spec.id = QStringLiteral("fastboot.partitions.export");
        spec.name = QStringLiteral("导出分区表 CSV");
        spec.desc = QStringLiteral("把 fastboot 分区表导出为 CSV（名称/大小/类型/槽位）");
        spec.icon = QStringLiteral("file-down");
        spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("partition"), QStringLiteral("csv")};
        spec.fields = {
            fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
            fields::path(QStringLiteral("output"), QStringLiteral("保存为 CSV（可选）"),
                         QStringLiteral("*.csv"), QString(), false),
        };
        addTool(spec, fastbootPartitionsExport);
        spec.fields.clear();
    }

    spec.id = QStringLiteral("fastboot.oem");
    spec.name = QStringLiteral("OEM 命令");
    spec.desc = QStringLiteral("执行 fastboot oem <命令>");
    spec.icon = QStringLiteral("wrench");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("oem")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::text(QStringLiteral("command"), QStringLiteral("OEM 命令"), QStringLiteral("device-info")),
    };
    addTool(spec, fastbootOem);
    spec.fields.clear();
}

// ---------------------------------------------------------------------------
// Port / driver tools
// ---------------------------------------------------------------------------

void scanPorts(TaskContext& ctx, const QVariantMap& options)
{
    ctx.setStatus(QStringLiteral("扫描 USB / 串口设备"));
    QList<PortRecord> ports = enumeratePorts();
    if (boolOption(options, "onlyFlashing", false))
        ports = flashingPorts(ports);
    if (ports.isEmpty()) {
        if (boolOption(options, "onlyFlashing", false))
            fail(QStringLiteral("未检测到处于刷机模式的设备。请让设备进入 EDL / BROM / FEL / "
                                                    "Loader 模式后重试。"));
        fail(QStringLiteral("未检测到可用的 USB 或串口设备，请检查数据线、驱动与设备连接。"));
    }
    for (const auto& port : ports) {
        const QString marker = port.identity.isFlashing ? QStringLiteral("[刷机]")
                               : port.identity.isAdb    ? QStringLiteral("[ADB]")
                               : port.identity.isFastboot ? QStringLiteral("[Fastboot]")
                                                          : QStringLiteral("[端口]");
        ctx.info(QStringLiteral("%1 %2").arg(marker, portSummaryLine(port)));
    }
    ctx.ok(QStringLiteral("扫描完成：共 %1 个端口，其中刷机端口 %2 个。")
               .arg(ports.size())
               .arg(flashingPorts(ports).size()));
}

void checkDrivers(TaskContext& ctx, const QVariantMap&)
{
    ctx.setStatus(QStringLiteral("检查驱动绑定状态"));
    const auto statuses = detectDrivers();
    int bound = 0;
    for (const auto& status : statuses) {
        ctx.info(driverStatusLine(status));
        if (status.bound())
            ++bound;
    }
    const QString summary = driverSummary(statuses);
    ctx.debug(summary);
    if (bound == 0)
        ctx.warn(QStringLiteral("当前没有检测到任何已绑定的刷机/ADB 驱动。连接设备后再检查，或安装 data/drivers 下的驱动。"));
    ctx.ok(QStringLiteral("驱动检测完成：%1/%2 类驱动已绑定。").arg(bound).arg(statuses.size()));
}

void installDrivers(TaskContext& ctx, const QVariantMap& options)
{
    if (!util::isAdmin())
        fail(QStringLiteral("安装驱动需要管理员权限。请右键以管理员身份运行本程序后重试。"));
    const QString requested = textOption(options, "dir");
    const QString dir = requested.isEmpty() ? driversDir() : requested;
    if (!QFileInfo::exists(dir))
        fail(QStringLiteral("驱动目录不存在：%1\n请把驱动包解压到该目录后重试。").arg(dir));
    const QStringList infs = findDriverInfs(dir);
    if (infs.isEmpty())
        fail(QStringLiteral("驱动目录中没有 .inf 文件：%1").arg(dir));

    ctx.info(QStringLiteral("在 %1 下找到 %2 个驱动包。").arg(dir).arg(infs.size()));
    int installed = 0;
    QStringList log;
    for (int i = 0; i < infs.size(); ++i) {
        ctx.checkCanceled();
        ctx.setProgress(int((i + 1) * 100.0 / infs.size()));
        const QString inf = infs.at(i);
        ctx.setStatus(QStringLiteral("安装 %1").arg(QFileInfo(inf).fileName()));
        QString output;
        QString error;
        if (installDriverInf(inf, &output, &error)) {
            ++installed;
            ctx.info(QStringLiteral("✓ %1").arg(QFileInfo(inf).fileName()));
        } else {
            log.append(error);
            ctx.warn(QStringLiteral("✗ %1：%2").arg(QFileInfo(inf).fileName(), error));
        }
    }
    if (installed == 0)
        fail(QStringLiteral("所有驱动安装均失败：\n%1").arg(log.join(QLatin1Char('\n'))));
    ctx.ok(QStringLiteral("驱动安装完成：成功 %1 / 共 %2 个。").arg(installed).arg(infs.size()));
}

void registerPortToolSet()
{
    ToolSpec spec;
    spec.category = QStringLiteral("bootloader");
    spec.group = QStringLiteral("device");
    spec.icon = QStringLiteral("usb");

    spec.id = QStringLiteral("device.ports");
    spec.name = QStringLiteral("端口扫描");
    spec.desc = QStringLiteral("枚举 USB / 串口设备并识别芯片模式（EDL / BROM / FEL / ADB…）");
    spec.keywords = {QStringLiteral("usb"), QStringLiteral("端口"), QStringLiteral("扫描"),
                     QStringLiteral("com")};
    spec.fields = {
        fields::boolean(QStringLiteral("onlyFlashing"), QStringLiteral("仅显示刷机端口"), false),
    };
    addTool(spec, scanPorts);
    spec.fields.clear();

    spec.id = QStringLiteral("device.drivers");
    spec.name = QStringLiteral("驱动状态检测");
    spec.desc = QStringLiteral("检查 QDLoader / MTK / SPRD / RK / AW / ADB 六类驱动绑定状态");
    spec.keywords = {QStringLiteral("驱动"), QStringLiteral("driver"), QStringLiteral("pnputil")};
    addTool(spec, checkDrivers);

    spec.id = QStringLiteral("device.driver.install");
    spec.name = QStringLiteral("安装驱动");
    spec.desc = QStringLiteral("用 pnputil 安装 data/drivers 下的驱动包（需要管理员）");
    spec.icon = QStringLiteral("wrench");
    spec.keywords = {QStringLiteral("驱动"), QStringLiteral("安装"), QStringLiteral("inf")};
    spec.fields = {
        fields::dir(QStringLiteral("dir"), QStringLiteral("驱动目录"),
                    QStringLiteral("留空使用 data/drivers"), false),
    };
    addTool(spec, installDrivers);
    spec.fields.clear();
}

// ---------------------------------------------------------------------------
// Batch tools
// ---------------------------------------------------------------------------

void batchQueue(TaskContext& ctx, const QVariantMap& options)
{
    ensureAdbAvailable();
    const QString command = textOption(options, "command");
    if (command.isEmpty())
        fail(QStringLiteral("请填写要在每台设备上执行的 ADB 命令。"));

    QStringList serials;
    for (const QString& line : textOption(options, "devices").split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString serial = line.trimmed();
        if (!serial.isEmpty() && !serials.contains(serial))
            serials.append(serial);
    }
    if (serials.isEmpty()) {
        serials = AdbClient::deviceSerials();
        if (serials.isEmpty())
            fail(noAdbDeviceMessage());
    }

    const int parallel = qBound(1, intOption(options, "parallel", 2), 16);
    const int retries = qBound(0, intOption(options, "retries", 0), 5);
    ctx.info(QStringLiteral("批量执行：%1 台设备，并发 %2，重试 %3 次").arg(serials.size()).arg(parallel).arg(retries));

    BatchJobManager manager(parallel);
    for (const QString& serial : serials) {
        BatchJob job;
        job.id = QStringLiteral("adb:%1").arg(serial);
        job.device = serial;
        job.label = serial;
        job.maxAttempts = retries + 1;
        job.run = [command, serial](BatchJobContext& context) {
            context.log(QStringLiteral("[%1] $ %2").arg(serial, command));
            context.setProgress(10);
            context.checkCanceled();
            const AdbResult result = AdbClient::shell(serial, command);
            if (!result.output.isEmpty()) {
                const QStringList lines = result.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (int i = 0; i < lines.size() && i < 50; ++i)
                    context.log(QStringLiteral("[%1] %2").arg(serial, lines.at(i)));
            }
            context.setProgress(100);
            if (!result.ok) {
                context.log(QStringLiteral("[%1] 失败：%2").arg(serial, result.error));
                return false;
            }
            return true;
        };
        manager.addJob(std::move(job));
    }

    manager.setProgressCallback([&ctx, &manager](int percent, const QString& status) {
        // TaskContext::setStatus throws TaskCanceled once the user pressed
        // cancel; swallow it here and let the manager unwind gracefully.
        try {
            ctx.setProgress(percent);
            ctx.setStatus(status);
        } catch (const TaskContext::TaskCanceled&) {
            manager.requestCancel();
            return;
        }
        if (ctx.isCanceled())
            manager.requestCancel();
    });
    manager.setLogCallback([&ctx](const QString& message, bool isError) {
        if (isError)
            ctx.error(message);
        else
            ctx.info(message);
    });

    const BatchSummary summary = manager.run();
    ctx.info(summary.toString());
    if (summary.failed > 0) {
        ctx.warn(QStringLiteral("失败设备：%1").arg(summary.failedJobs.join(QStringLiteral("、"))));
        if (summary.succeeded == 0)
            fail(summary.toString());
    }
    ctx.ok(summary.toString());
}

void batchScript(TaskContext& ctx, const QVariantMap& options)
{
    ensureAdbAvailable();
    QStringList commands;
    for (const QString& line : textOption(options, "script").split(QLatin1Char('\n'))) {
        const QString command = line.trimmed();
        if (!command.isEmpty() && !command.startsWith(QLatin1Char('#')))
            commands.append(command);
    }
    if (commands.isEmpty())
        fail(QStringLiteral("请填写至少一条 ADB 命令（每行一条）。"));

    QStringList serials;
    for (const QString& line : textOption(options, "devices").split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString serial = line.trimmed();
        if (!serial.isEmpty())
            serials.append(serial);
    }
    if (serials.isEmpty())
        serials = AdbClient::deviceSerials();
    if (serials.isEmpty())
        fail(noAdbDeviceMessage());

    const int parallel = qBound(1, intOption(options, "parallel", 2), 16);
    ctx.info(QStringLiteral("批量脚本：%1 台设备 × %2 条命令，并发 %3")
                 .arg(serials.size())
                 .arg(commands.size())
                 .arg(parallel));

    BatchJobManager manager(parallel);
    for (const QString& serial : serials) {
        BatchJob job;
        job.id = QStringLiteral("script:%1").arg(serial);
        job.device = serial;
        job.label = serial;
        job.maxAttempts = qBound(0, intOption(options, "retries", 0), 5) + 1;
        job.run = [commands, serial](BatchJobContext& context) {
            for (int i = 0; i < commands.size(); ++i) {
                context.checkCanceled();
                context.setProgress(int((i + 1) * 100.0 / commands.size()));
                const AdbResult result = AdbClient::shell(serial, commands.at(i));
                context.log(QStringLiteral("[%1] $ %2").arg(serial, commands.at(i)));
                if (!result.output.isEmpty())
                    context.log(QStringLiteral("[%1] %2").arg(serial, result.output.left(2000)));
                if (!result.ok) {
                    context.log(QStringLiteral("[%1] 命令失败：%2").arg(serial, result.error));
                    return false;
                }
            }
            return true;
        };
        manager.addJob(std::move(job));
    }
    manager.setProgressCallback([&ctx, &manager](int percent, const QString& status) {
        // TaskContext::setStatus throws TaskCanceled once the user pressed
        // cancel; swallow it here and let the manager unwind gracefully.
        try {
            ctx.setProgress(percent);
            ctx.setStatus(status);
        } catch (const TaskContext::TaskCanceled&) {
            manager.requestCancel();
            return;
        }
        if (ctx.isCanceled())
            manager.requestCancel();
    });
    manager.setLogCallback([&ctx](const QString& message, bool) { ctx.info(message); });
    const BatchSummary summary = manager.run();
    ctx.info(summary.toString());
    if (summary.failed > 0 && summary.succeeded == 0)
        fail(summary.toString());
    ctx.ok(summary.toString());
}

void batchVerify(TaskContext& ctx, const QVariantMap& options)
{
    QString dir = textOption(options, "dir");
    if (dir.isEmpty())
        fail(QStringLiteral("请选择要校验的目录。"));
    if (!QFileInfo::exists(dir))
        fail(QStringLiteral("目录不存在：%1").arg(dir));
    const QString algorithm = choiceOption(options, "algorithm", QStringLiteral("sha256"));

    QString manifestPath = textOption(options, "manifest");
    if (manifestPath.isEmpty()) {
        const QDir directory(dir);
        const QStringList candidates = directory.entryList(
            {QStringLiteral("*.sha256"), QStringLiteral("*.sha256sum"), QStringLiteral("*.md5"),
             QStringLiteral("*.txt")},
            QDir::Files, QDir::Name);
        if (!candidates.isEmpty())
            manifestPath = directory.filePath(candidates.first());
    }
    if (manifestPath.isEmpty() || !QFileInfo::exists(manifestPath))
        fail(QStringLiteral("未找到校验清单文件。请在该目录放置 .sha256/.md5 清单，"
                                                "或手动选择清单文件。"));

    const QList<HashManifestEntry> entries =
        parseHashManifest(QString::fromUtf8(util::readFile(manifestPath)));
    if (entries.isEmpty())
        fail(QStringLiteral("校验清单为空或格式无法识别：%1").arg(manifestPath));

    ctx.info(QStringLiteral("校验清单：%1（%2 个文件）").arg(manifestPath).arg(entries.size()));
    QList<HashManifestEntry> failed;
    for (int i = 0; i < entries.size(); ++i) {
        ctx.checkCanceled();
        const HashManifestEntry& entry = entries.at(i);
        ctx.setProgress(int((i + 1) * 100.0 / entries.size()));
        ctx.setStatus(QStringLiteral("校验 %1").arg(entry.path));
        QString filePath = entry.path;
        if (QFileInfo(filePath).isRelative())
            filePath = QDir(dir).filePath(filePath);
        QString actual;
        QString error;
        if (verifyFileHash(filePath, entry.hash, algorithm, &actual, &error)) {
            ctx.info(QStringLiteral("✓ %1").arg(entry.path));
        } else {
            failed.append(entry);
            ctx.warn(QStringLiteral("✗ %1：%2").arg(entry.path, error));
        }
    }
    if (!failed.isEmpty())
        fail(QStringLiteral("校验失败：%1 / %2 个文件不匹配。")
                                     .arg(failed.size())
                                     .arg(entries.size()));
    ctx.ok(QStringLiteral("校验通过：%1 个文件全部匹配。").arg(entries.size()));
}

void batchRename(TaskContext& ctx, const QVariantMap& options)
{
    const QString dir = textOption(options, "dir");
    if (dir.isEmpty() || !QFileInfo::exists(dir))
        fail(QStringLiteral("请选择存在的目录。"));
    const QString pattern = textOption(options, "pattern");
    if (pattern.isEmpty())
        fail(QStringLiteral("请填写重命名模板，例如 {name}_{index3}。"));
    const int start = intOption(options, "start", 1);
    const bool dryRun = boolOption(options, "dryRun", true);
    QStringList extensions;
    for (const QString& ext : textOption(options, "extensions").split(QLatin1Char(','), Qt::SkipEmptyParts))
        extensions.append(ext.trimmed().toLower());

    QDir directory(dir);
    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    QList<QFileInfo> targets;
    for (const QFileInfo& file : files) {
        if (extensions.isEmpty() || extensions.contains(file.suffix().toLower()))
            targets.append(file);
    }
    if (targets.isEmpty())
        fail(QStringLiteral("目录中没有需要重命名的文件。"));
    ctx.info(QStringLiteral("%1 %2 个文件。").arg(dryRun ? QStringLiteral("预览") : QStringLiteral("重命名"))
                 .arg(targets.size()));

    int renamed = 0;
    for (int i = 0; i < targets.size(); ++i) {
        ctx.checkCanceled();
        const QFileInfo& file = targets.at(i);
        ctx.setProgress(int((i + 1) * 100.0 / targets.size()));
        QString newName = expandRenamePattern(pattern, start + i, file.fileName());
        if (newName == file.fileName())
            continue;
        QString target = directory.filePath(newName);
        if (QFileInfo::exists(target))
            target = util::uniquePath(target);
        if (dryRun) {
            ctx.info(QStringLiteral("预览：%1 → %2").arg(file.fileName(), QFileInfo(target).fileName()));
            continue;
        }
        if (!QFile::rename(file.absoluteFilePath(), target)) {
            ctx.warn(QStringLiteral("重命名失败：%1").arg(file.fileName()));
            continue;
        }
        ++renamed;
        ctx.info(QStringLiteral("%1 → %2").arg(file.fileName(), QFileInfo(target).fileName()));
    }
    if (dryRun)
        ctx.ok(QStringLiteral("预览完成（未修改文件）。取消勾选「仅预览」后再次执行即可真正重命名。"));
    else
        ctx.ok(QStringLiteral("批量重命名完成：%1 个文件。").arg(renamed));
}


QStringList fileListOption(const QVariantMap& options, const char* key)
{
    QStringList files;
    const QVariant value = options.value(QString::fromLatin1(key));
    if (value.typeId() == QMetaType::QVariantList) {
        for (const QVariant& entry : value.toList()) {
            const QString path = entry.toString().trimmed();
            if (!path.isEmpty())
                files.append(path);
        }
    } else {
        for (const QString& part : value.toString().split(QRegularExpression(QStringLiteral("[;\n]")),
                                                         Qt::SkipEmptyParts)) {
            const QString path = part.trimmed();
            if (!path.isEmpty())
                files.append(path);
        }
    }
    return files;
}

QByteArray encodeGb18030(const QString& text)
{
#ifdef Q_OS_WIN
    const int size = WideCharToMultiByte(54936, 0, reinterpret_cast<const wchar_t*>(text.utf16()),
                                         int(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    QByteArray out(size, '\0');
    WideCharToMultiByte(54936, 0, reinterpret_cast<const wchar_t*>(text.utf16()), int(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
#else
    return text.toUtf8();
#endif
}

QString decodeGb18030(const QByteArray& data)
{
#ifdef Q_OS_WIN
    const int size = MultiByteToWideChar(54936, 0, data.constData(), int(data.size()), nullptr, 0);
    if (size <= 0)
        return {};
    QString out(size, QChar(0));
    MultiByteToWideChar(54936, 0, data.constData(), int(data.size()),
                        reinterpret_cast<wchar_t*>(out.data()), size);
    return out;
#else
    return QString::fromUtf8(data);
#endif
}

QString decodeAuto(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.mid(3));
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8.decode(data);
    if (!utf8.hasError())
        return text;
    return decodeGb18030(data);
}

void batchEncoding(TaskContext& ctx, const QVariantMap& options)
{
    const QStringList files = fileListOption(options, "files");
    if (files.isEmpty())
        fail(QStringLiteral("请选择要转换编码的文件"));
    const QString source = choiceOption(options, "source", QStringLiteral("auto"));
    const QString target = choiceOption(options, "target", QStringLiteral("utf-8"));
    QString outputDir = textOption(options, "output");
    if (!outputDir.isEmpty() && !util::ensureDir(outputDir))
        fail(QStringLiteral("无法创建输出目录：%1").arg(outputDir));

    int done = 0;
    for (const QString& path : files) {
        ctx.checkCanceled();
        ++done;
        ctx.setProgress(int(100.0 * done / files.size()));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            ctx.warn(QStringLiteral("无法读取：%1").arg(path));
            continue;
        }
        const QByteArray data = file.readAll();
        file.close();
        QString text;
        if (source == QLatin1String("utf-8") || source.contains(QStringLiteral("UTF-8")))
            text = QString::fromUtf8(data);
        else if (source == QLatin1String("gbk") || source.contains(QStringLiteral("GBK")))
            text = decodeGb18030(data);
        else
            text = decodeAuto(data);

        QByteArray encoded;
        if (target == QLatin1String("gbk") || target.contains(QStringLiteral("GBK"))) {
            encoded = encodeGb18030(text);
        } else {
            encoded = text.toUtf8();
            if (target.contains(QStringLiteral("bom"), Qt::CaseInsensitive))
                encoded.prepend("\xEF\xBB\xBF");
        }
        const QFileInfo info(path);
        const QString outPath = outputDir.isEmpty()
                                    ? info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
                                          + QStringLiteral(".converted.") + info.suffix()
                                    : outputDir + QLatin1Char('/') + info.completeBaseName()
                                          + QStringLiteral(".converted.") + info.suffix();
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            ctx.warn(QStringLiteral("无法写入：%1").arg(outPath));
            continue;
        }
        out.write(encoded);
        out.close();
        ctx.ok(QStringLiteral("%1 → %2（%3 字节）")
                   .arg(info.fileName(), QFileInfo(outPath).fileName())
                   .arg(encoded.size()));
    }
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("编码转换完成：%1 个文件").arg(files.size()));
}

void batchTimestamp(TaskContext& ctx, const QVariantMap& options)
{
    const QStringList files = fileListOption(options, "files");
    if (files.isEmpty())
        fail(QStringLiteral("请选择要修改时间戳的文件"));
    const QString timeText = textOption(options, "time").trimmed();
    QDateTime time = QDateTime::currentDateTime();
    if (!timeText.isEmpty()) {
        time = QDateTime::fromString(timeText, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!time.isValid()) {
            time = QDateTime::fromString(timeText, Qt::ISODate);
            if (!time.isValid())
                fail(QStringLiteral("时间格式错误，请使用 yyyy-MM-dd HH:mm:ss"));
        }
    }
    int done = 0;
    int changed = 0;
    for (const QString& path : files) {
        ctx.checkCanceled();
        ++done;
        ctx.setProgress(int(100.0 * done / files.size()));
        QFile file(path);
        if (!file.exists()) {
            ctx.warn(QStringLiteral("文件不存在：%1").arg(path));
            continue;
        }
        if (!file.open(QIODevice::ReadWrite)) {
            ctx.warn(QStringLiteral("无法打开：%1").arg(path));
            continue;
        }
        if (!file.setFileTime(time, QFileDevice::FileModificationTime)) {
            ctx.warn(QStringLiteral("修改失败：%1").arg(path));
            file.close();
            continue;
        }
        file.close();
        ++changed;
        ctx.ok(QStringLiteral("%1 → %2").arg(QFileInfo(path).fileName(), time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("时间戳修改完成：%1 / %2 个文件").arg(changed).arg(files.size()));
}

void registerBatchToolSet()
{
    ToolSpec spec;
    spec.category = QStringLiteral("batch");

    spec.id = QStringLiteral("batch.queue");
    spec.name = QStringLiteral("批量队列任务");
    spec.desc = QStringLiteral("对多台设备执行同一 ADB 命令：每设备串行、多设备并行、失败重试");
    spec.icon = QStringLiteral("list-checks");
    spec.keywords = {QStringLiteral("批量"), QStringLiteral("batch"), QStringLiteral("adb")};
    spec.fields = {
        fields::text(QStringLiteral("devices"), QStringLiteral("设备序列号（每行一个）"), {},
                     QStringLiteral("留空则使用全部在线 ADB 设备"), QStringLiteral("可选"), false),
        fields::text(QStringLiteral("command"), QStringLiteral("ADB 命令"),
                     QStringLiteral("getprop ro.product.model")),
        fields::integer(QStringLiteral("parallel"), QStringLiteral("并发设备数"), 2, 1, 16),
        fields::integer(QStringLiteral("retries"), QStringLiteral("失败重试次数"), 0, 0, 5),
    };
    addTool(spec, batchQueue);
    spec.fields.clear();

    spec.id = QStringLiteral("batch.script");
    spec.name = QStringLiteral("批量脚本执行");
    spec.desc = QStringLiteral("按顺序在多台设备上执行多行 ADB 命令脚本");
    spec.icon = QStringLiteral("terminal");
    spec.keywords = {QStringLiteral("批量"), QStringLiteral("脚本"), QStringLiteral("script")};
    spec.fields = {
        fields::text(QStringLiteral("devices"), QStringLiteral("设备序列号（每行一个）"), {}, {},
                     QStringLiteral("留空使用全部在线设备"), false),
        fields::text(QStringLiteral("script"), QStringLiteral("命令脚本（每行一条）"), {},
                     QStringLiteral("# 开头的行会被忽略")),
        fields::integer(QStringLiteral("parallel"), QStringLiteral("并发设备数"), 2, 1, 16),
        fields::integer(QStringLiteral("retries"), QStringLiteral("失败重试次数"), 0, 0, 5),
    };
    addTool(spec, batchScript);
    spec.fields.clear();

    spec.id = QStringLiteral("batch.verify");
    spec.name = QStringLiteral("批量校验文件");
    spec.desc = QStringLiteral("按 .sha256 / .md5 清单批量校验固件文件");
    spec.icon = QStringLiteral("shield-check");
    spec.keywords = {QStringLiteral("校验"), QStringLiteral("hash"), QStringLiteral("sha256")};
    spec.fields = {
        fields::dir(QStringLiteral("dir"), QStringLiteral("待校验目录")),
        fields::path(QStringLiteral("manifest"), QStringLiteral("校验清单（可选）"),
                     QStringLiteral("*.sha256 *.txt"), QStringLiteral("留空自动查找目录中的清单"), false),
        fields::choice(QStringLiteral("algorithm"), QStringLiteral("哈希算法"),
                       {QStringLiteral("sha256 — SHA-256"), QStringLiteral("md5 — MD5")}, 0),
    };
    addTool(spec, batchVerify);
    spec.fields.clear();

    spec.id = QStringLiteral("batch.rename");
    spec.name = QStringLiteral("批量重命名");
    spec.desc = QStringLiteral("按模板批量重命名文件（默认仅预览）");
    spec.icon = QStringLiteral("file-cog");
    spec.keywords = {QStringLiteral("批量"), QStringLiteral("重命名"), QStringLiteral("rename")};
    spec.fields = {
        fields::dir(QStringLiteral("dir"), QStringLiteral("目录")),
        fields::text(QStringLiteral("pattern"), QStringLiteral("命名模板"), QStringLiteral("{name}_{index3}"),
                     QStringLiteral("支持 {name} {index} {index2} {index3} {ext} {filename}")),
        fields::integer(QStringLiteral("start"), QStringLiteral("起始编号"), 1, 1, 1000000),
        fields::text(QStringLiteral("extensions"), QStringLiteral("仅处理扩展名"),
                     QStringLiteral("img,bin"), QStringLiteral("逗号分隔，留空处理全部"), {}, false),
        fields::boolean(QStringLiteral("dryRun"), QStringLiteral("仅预览（不修改文件）"), true),
    };
    addTool(spec, batchRename);
    spec.fields.clear();
    {
        spec.id = QStringLiteral("batch.encoding");
        spec.name = QStringLiteral("批量编码转换");
        spec.desc = QStringLiteral("把文本文件在 UTF-8 / UTF-8 BOM / GBK(GB18030) 之间转换");
        spec.icon = QStringLiteral("file-type");
        spec.keywords = {QStringLiteral("编码"), QStringLiteral("utf-8"), QStringLiteral("gbk")};
        spec.fields = {
            fields::paths(QStringLiteral("files"), QStringLiteral("文件列表"), QStringLiteral("*.txt *.csv *.xml *.json *.prop")),
            fields::choice(QStringLiteral("source"), QStringLiteral("源编码"),
                           {QStringLiteral("auto"), QStringLiteral("utf-8"), QStringLiteral("gbk")}, 0),
            fields::choice(QStringLiteral("target"), QStringLiteral("目标编码"),
                           {QStringLiteral("utf-8"), QStringLiteral("utf-8 bom"), QStringLiteral("gbk")}, 0),
            fields::dir(QStringLiteral("output"), QStringLiteral("输出目录（可选）"), QString(), false),
        };
        addTool(spec, batchEncoding);
        spec.fields.clear();
    }
    {
        spec.id = QStringLiteral("batch.timestamp");
        spec.name = QStringLiteral("批量修改时间戳");
        spec.desc = QStringLiteral("把文件的修改时间统一设置为指定时间（留空=当前时间）");
        spec.icon = QStringLiteral("calendar-clock");
        spec.keywords = {QStringLiteral("时间戳"), QStringLiteral("mtime"), QStringLiteral("批量")};
        spec.fields = {
            fields::paths(QStringLiteral("files"), QStringLiteral("文件列表"), QStringLiteral("所有文件 (*.*)")),
            fields::text(QStringLiteral("time"), QStringLiteral("时间（yyyy-MM-dd HH:mm:ss，留空=当前）"),
                         {}, QStringLiteral("2024-01-01 12:00:00"), {}, false),
        };
        addTool(spec, batchTimestamp);
        spec.fields.clear();
    }

}

} // namespace

void registerDeviceTools()
{
    registerAdbToolSet();
    registerAdbExtrasToolSet();
    registerFastbootToolSet();
    registerPortToolSet();
    registerBatchToolSet();
}

} // namespace gf
