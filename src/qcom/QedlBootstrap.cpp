#include "QedlBootstrap.h"
#include <QFileInfo>
#include <QProcess>
#include <QCoreApplication>
#include <QtGlobal>
namespace gf::qcom {
QString qedlExecutable() { return QCoreApplication::applicationDirPath() + QStringLiteral("/data/thirdparty/qedl/qedl.exe"); }
bool bootstrapLoader(const QString& programmerPath, const QString& port, QString* error)
{
    const QString exe = QFileInfo(qedlExecutable()).exists()
        ? qedlExecutable()
        : QStringLiteral("C:/Users/shu/Desktop/sjgj/data/thirdparty/qedl/qedl.exe");
    if (!QFileInfo::exists(exe)) { if (error) *error = "qedl.exe missing"; return false; }
    QStringList args;
    args << QStringLiteral("--loader") << programmerPath;
    if (!port.trimmed().isEmpty()) args << QStringLiteral("--port") << port.trimmed();
    args << QStringLiteral("gpt");
    QProcess p;
    p.setWorkingDirectory(QFileInfo(exe).absolutePath());
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(exe, args);
    if (!p.waitForStarted(3000)) { if (error) *error = "qedl start failed"; return false; }
    while (!p.waitForFinished(4000)) {}
    if (p.state() != QProcess::NotRunning) { p.kill(); p.waitForFinished(1000); if (error) *error = "qedl timeout"; return false; }
    if (p.exitCode() != 0) { if (error) *error = QString::fromLocal8Bit(p.readAll()); return false; }
    return true;
}
}
