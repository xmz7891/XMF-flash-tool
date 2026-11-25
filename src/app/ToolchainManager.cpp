#include "ToolchainManager.h"
#include "Util.h"
#include "packets/ZipArchive.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QDir>

namespace gf {
namespace {
QString dataRoot() { return util::dataDir(); }

QHash<QString, QString> readManifest()
{
    QHash<QString, QString> out;
    QFile f(dataRoot() + QStringLiteral("/toolchain.sha256"));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    for (const QByteArray& line : f.readAll().split('\n')) {
        const QList<QByteArray> p = line.trimmed().split(' ');
        if (p.size() >= 2)
            out.insert(QString::fromUtf8(p.last()).replace('\\', '/'), QString::fromLatin1(p.first()));
    }
    return out;
}

bool verifyFile(const QString& relative, const QString& expected, QString* error)
{
    QFile f(dataRoot() + QLatin1Char('/') + relative);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("缺少工具链文件：%1").arg(relative);
        return false;
    }
    const QString actual = QString::fromLatin1(
        QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
    if (actual.compare(expected, Qt::CaseInsensitive) != 0) {
        if (error) *error = QStringLiteral("工具链摘要不匹配：%1").arg(relative);
        return false;
    }
    return true;
}
}

bool ensureToolchain(QString* error)
{
    const QString zipPath = dataRoot() + QStringLiteral("/toolchain.zip");
    if (QFileInfo::exists(zipPath)) {
        packets::ZipArchive zip;
        if (!zip.open(zipPath)) {
            if (error) *error = QStringLiteral("无法打开工具链归档：%1").arg(zip.errorString());
            return false;
        }
        for (const QString& entry : zip.entryNames()) {
            if (entry.startsWith(QLatin1String("thirdparty/")) == false)
                continue;
            const QString target = dataRoot() + QLatin1Char('/') + entry;
            if (!QFileInfo::exists(target)) {
                if (!zip.extractTo(entry, target)) {
                    if (error) *error = QStringLiteral("解压失败：%1").arg(entry);
                    return false;
                }
            }
        }
    }
    const auto manifest = readManifest();
    for (auto it = manifest.constBegin(); it != manifest.constEnd(); ++it) {
        if (!it.key().endsWith(QLatin1String(".exe")) && !it.key().endsWith(QLatin1String(".dll")))
            continue;
        if (!verifyFile(it.key(), it.value(), error))
            return false;
    }
    return true;
}
}
