#include "PlatformTools.h"

#include <QDir>
#include <QFileInfo>

namespace gf::device {

namespace {

QString envOverrideDir()
{
    return qEnvironmentVariable("XMF_PLATFORM_TOOLS");
}

} // namespace

QStringList toolSearchDirs()
{
    QStringList dirs;
    const QString override = envOverrideDir();
    if (!override.isEmpty())
        dirs.append(QDir::toNativeSeparators(override));
    const QString appDir = util::appDir();
    dirs.append(QDir::toNativeSeparators(appDir + QStringLiteral("/data/platform-tools")));
    dirs.append(QDir::toNativeSeparators(appDir + QStringLiteral("/resources/bin/platform-tools")));
    return dirs;
}

QString platformToolsDir()
{
    return QDir::toNativeSeparators(util::appDir() + QStringLiteral("/data/platform-tools"));
}

ToolPath locateTool(const QString& toolName)
{
    const QString exe = toolName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
                            ? toolName
                            : toolName + QStringLiteral(".exe");
    const QString override = envOverrideDir();
    if (!override.isEmpty()) {
        const QString candidate = QDir(override).filePath(exe);
        if (QFileInfo::exists(candidate))
            return {QDir::toNativeSeparators(candidate), QStringLiteral("XMF_PLATFORM_TOOLS")};
    }
    const QString appDir = util::appDir();
    struct Candidate {
        QString dir;
        const char* source;
    };
    const Candidate candidates[] = {
        {appDir + QStringLiteral("/data/platform-tools"), "data/platform-tools"},
        {appDir + QStringLiteral("/resources/bin/platform-tools"), "resources/bin/platform-tools"},
    };
    for (const auto& candidate : candidates) {
        const QString path = QDir(candidate.dir).filePath(exe);
        if (QFileInfo::exists(path))
            return {QDir::toNativeSeparators(path), QString::fromLatin1(candidate.source)};
    }
    const QString inPath = util::findExecutable(toolName);
    if (!inPath.isEmpty())
        return {inPath, QStringLiteral("PATH")};
    return {};
}

bool platformToolsReady()
{
    return locateTool(QStringLiteral("adb")).isValid() && locateTool(QStringLiteral("fastboot")).isValid();
}

QString platformToolsHint()
{
    return QStringLiteral("请运行 scripts/fetch_platform_tools.ps1 或手动把 Android platform-tools "
                          "放到程序目录的 data/platform-tools。查找顺序：data/platform-tools → "
                          "resources/bin/platform-tools → 系统 PATH。");
}

bool ToolRunResult::ok() const
{
    return error.isEmpty() && proc.started && !proc.timedOut && proc.exitCode == 0;
}

QByteArray ToolRunResult::combined() const
{
    QByteArray data = proc.stdoutData;
    if (!proc.stderrData.isEmpty()) {
        if (!data.isEmpty() && !data.endsWith('\n'))
            data.append('\n');
        data.append(proc.stderrData);
    }
    return data;
}

QString ToolRunResult::outputText() const
{
    return QString::fromLocal8Bit(combined()).trimmed();
}

QString ToolRunResult::failureText() const
{
    if (!error.isEmpty())
        return error;
    if (proc.timedOut)
        return QStringLiteral("%1 执行超时（%2 秒）").arg(tool, QString::number(120));
    const QString output = outputText();
    QString text = QStringLiteral("%1 执行失败（退出码 %2）").arg(tool).arg(proc.exitCode);
    if (!output.isEmpty()) {
        const QString tail = output.right(600);
        text += QStringLiteral("：\n") + tail;
    }
    return text;
}

ToolRunResult runTool(const QString& toolName, const QStringList& arguments, int timeoutMs)
{
    ToolRunResult result;
    result.tool = toolName;
    result.arguments = arguments;
    const ToolPath tool = locateTool(toolName);
    if (!tool.isValid()) {
        result.error = platformToolsMissingMessage(toolName);
        return result;
    }
    result.toolPath = tool.path;
    result.proc = util::runProcess(tool.path, arguments, timeoutMs);
    if (!result.proc.started) {
        result.error = QStringLiteral("无法启动 %1（%2）。%3")
                           .arg(tool.path, QString::fromLocal8Bit(result.proc.stderrData).trimmed(),
                                platformToolsHint());
    }
    return result;
}

ToolRunResult runAdb(const QStringList& arguments, int timeoutMs)
{
    return runTool(QStringLiteral("adb"), arguments, timeoutMs);
}

ToolRunResult runFastboot(const QStringList& arguments, int timeoutMs)
{
    return runTool(QStringLiteral("fastboot"), arguments, timeoutMs);
}

QString noAdbDeviceMessage()
{
    return QStringLiteral("未检测到 ADB 设备，请开启 USB 调试并授权本机（设备管理器中应出现 ADB 接口）。");
}

QString noFastbootDeviceMessage()
{
    return QStringLiteral("未检测到 Fastboot 设备，请将设备进入 Bootloader / Fastboot 模式后重试。");
}

QString platformToolsMissingMessage(const QString& toolName)
{
    return QStringLiteral("未找到 %1.exe，无法执行该操作。%2").arg(toolName, platformToolsHint());
}

} // namespace gf::device
