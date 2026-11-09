#pragma once

#include <QString>
#include <QStringList>

#include "core/Util.h"

namespace gf::device {

/// Platform-tools lookup result. `source` describes where the binary came from
/// so errors can tell the user exactly which copy is used.
struct ToolPath {
    QString path;
    QString source;      // "data/platform-tools", "resources/bin/platform-tools", "PATH", ...
    bool isValid() const { return !path.isEmpty(); }
};

/// exe/data/platform-tools, exe/resources/bin/platform-tools plus the
/// XMF_PLATFORM_TOOLS environment override (useful for tests / portable mode).
QStringList toolSearchDirs();

/// Locates adb.exe / fastboot.exe. Search order:
///   1. %XMF_PLATFORM_TOOLS%
///   2. <exe>/data/platform-tools
///   3. <exe>/resources/bin/platform-tools
///   4. PATH
ToolPath locateTool(const QString& toolName);

/// <exe>/data/platform-tools (may not exist).
QString platformToolsDir();
bool platformToolsReady();
/// Chinese description of the search order, used in error messages.
QString platformToolsHint();

/// Result of one adb/fastboot invocation.
struct ToolRunResult {
    QString tool;                 // "adb" / "fastboot"
    QString toolPath;
    QStringList arguments;
    util::ProcessResult proc;
    QString error;                // non-empty when the command could not run at all

    bool ok() const;
    bool timedOut() const { return proc.timedOut; }
    int exitCode() const { return proc.exitCode; }
    /// stdout + stderr (adb/fastboot write most diagnostics to stderr).
    QByteArray combined() const;
    QString outputText() const;
    /// Chinese error describing why the command failed, including output tail.
    QString failureText() const;
};

ToolRunResult runTool(const QString& toolName, const QStringList& arguments, int timeoutMs = 120000);
ToolRunResult runAdb(const QStringList& arguments, int timeoutMs = 120000);
ToolRunResult runFastboot(const QStringList& arguments, int timeoutMs = 120000);

/// Standard user facing messages required by the device layer contract.
QString noAdbDeviceMessage();
QString noFastbootDeviceMessage();
QString platformToolsMissingMessage(const QString& toolName);

} // namespace gf::device
