#include "OfficialTools.h"

#include "Util.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace gf {

namespace {
QString thirdPartyRoot()
{
    return QDir(util::dataDir()).filePath(QStringLiteral("thirdparty"));
}

QString driverRoot()
{
    return QDir(util::dataDir()).filePath(QStringLiteral("drivers"));
}
} // namespace

OfficialTools::OfficialTools(QObject* parent)
    : QObject(parent)
{
    refresh();
}

QString OfficialTools::root() const
{
    return QDir::toNativeSeparators(thirdPartyRoot());
}

QVector<OfficialTools::ToolDef> OfficialTools::definitions() const
{
    return {
        {QStringLiteral("miflash"), QStringLiteral("小米 MiFlash"),
         QStringLiteral("官方小米线刷工具，内置 QSaharaServer / fh_loader / MTK DA / 驱动"),
         QStringLiteral("smartphone"), QStringLiteral("thirdparty/miflash/XiaoMiFlash.exe"),
         QStringLiteral("thirdparty/miflash"), QStringLiteral("gui")},
        {QStringLiteral("spflashtool"), QStringLiteral("SP Flash Tool"),
         QStringLiteral("联发科官方刷机工具，支持 Scatter 固件下载 / 格式化 / 回读"),
         QStringLiteral("microchip"),
         QStringLiteral("thirdparty/mtk/SP_Flash_Tool_V5.2404_Win/flash_tool.exe"),
         QStringLiteral("thirdparty/mtk/SP_Flash_Tool_V5.2404_Win"), QStringLiteral("gui")},
        {QStringLiteral("sprd"), QStringLiteral("Unisoc Research Download"),
         QStringLiteral("紫光展锐官方 PAC 刷机工具，FDL 下载与分区读写"),
         QStringLiteral("layers"), QStringLiteral("thirdparty/sprd/win-x86/SPRDClientCore.exe"),
         QStringLiteral("thirdparty/sprd/win-x86"), QStringLiteral("gui")},
        {QStringLiteral("qsahara"), QStringLiteral("QSaharaServer"),
         QStringLiteral("高通官方 Sahara 引导程序上传工具（命令行，供内部流水线调用）"),
         QStringLiteral("terminal"), QStringLiteral("thirdparty/miflash/QSaharaServer.exe"),
         QStringLiteral("thirdparty/miflash"), QStringLiteral("cli")},
        {QStringLiteral("fh_loader"), QStringLiteral("fh_loader"),
         QStringLiteral("高通官方 Firehose 分区读写工具（命令行，供内部流水线调用）"),
         QStringLiteral("terminal"), QStringLiteral("thirdparty/miflash/fh_loader.exe"),
         QStringLiteral("thirdparty/miflash"), QStringLiteral("cli")},
    };
}

void OfficialTools::refresh()
{
    const QString dataRoot = util::dataDir();
    QVariantList tools;
    for (const ToolDef& def : definitions()) {
        const QString exe = QDir(dataRoot).filePath(def.relativeExe);
        const QString dir = QDir(dataRoot).filePath(def.relativeDir);
        const QFileInfo exeInfo(exe);
        QVariantMap map;
        map.insert(QStringLiteral("id"), def.id);
        map.insert(QStringLiteral("name"), def.name);
        map.insert(QStringLiteral("description"), def.description);
        map.insert(QStringLiteral("icon"), def.icon);
        map.insert(QStringLiteral("kind"), def.kind);
        map.insert(QStringLiteral("path"), QDir::toNativeSeparators(exe));
        map.insert(QStringLiteral("dir"), QDir::toNativeSeparators(dir));
        map.insert(QStringLiteral("available"), exeInfo.exists());
        tools.append(map);
    }
    // ---- open-source helper tools bundled under thirdparty/oss ------------
    const QDir ossDir(QDir(thirdPartyRoot()).filePath(QStringLiteral("oss")));
    if (ossDir.exists()) {
        const auto addOss = [&tools](const QString& id, const QString& name, const QString& desc,
                                     const QString& icon, const QString& kind, const QString& exePath) {
            QVariantMap map;
            map.insert(QStringLiteral("id"), id);
            map.insert(QStringLiteral("name"), name);
            map.insert(QStringLiteral("description"), desc);
            map.insert(QStringLiteral("icon"), icon);
            map.insert(QStringLiteral("kind"), kind);
            map.insert(QStringLiteral("path"), QDir::toNativeSeparators(exePath));
            map.insert(QStringLiteral("dir"), QDir::toNativeSeparators(QFileInfo(exePath).absolutePath()));
            map.insert(QStringLiteral("available"), QFileInfo::exists(exePath));
            map.insert(QStringLiteral("oss"), true);
            tools.append(map);
        };
        const auto findExe = [](const QDir& root, const QString& fileName) -> QString {
            const QFileInfoList found = root.entryInfoList({fileName}, QDir::Files, QDir::Name);
            if (!found.isEmpty())
                return found.first().absoluteFilePath();
            QDirIterator it(root.absolutePath(), {fileName}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QFileInfo info(it.next());
                if (info.fileName().compare(fileName, Qt::CaseInsensitive) == 0)
                    return info.absoluteFilePath();
            }
            return {};
        };
        const QString scrcpy = findExe(ossDir, QStringLiteral("scrcpy.exe"));
        if (!scrcpy.isEmpty())
            addOss(QStringLiteral("scrcpy"), QStringLiteral("scrcpy 屏幕镜像"),
                   QStringLiteral("开源屏幕镜像工具（Genymobile/scrcpy），已内嵌启动"),
                   QStringLiteral("monitor-smartphone"), QStringLiteral("gui"), scrcpy);
        const QString pdg = findExe(ossDir, QStringLiteral("payload-dumper-go.exe"));
        if (!pdg.isEmpty())
            addOss(QStringLiteral("payload-dumper-go"), QStringLiteral("payload-dumper-go"),
                   QStringLiteral("开源 payload 解包工具（ssut/payload-dumper-go）"),
                   QStringLiteral("file-archive"), QStringLiteral("cli"), pdg);
    }

    m_tools = tools;

    QVariantList drivers;
    const QDir driversDir(driverRoot());
    if (driversDir.exists()) {
        const auto vendors = driversDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& vendor : vendors) {
            const auto infs = QDir(vendor.absoluteFilePath())
                                  .entryInfoList({QStringLiteral("*.inf")}, QDir::Files, QDir::Name);
            if (infs.isEmpty())
                continue;
            QVariantList names;
            for (const QFileInfo& inf : infs)
                names.append(inf.fileName());
            QVariantMap map;
            map.insert(QStringLiteral("vendor"), vendor.fileName());
            map.insert(QStringLiteral("infCount"), infs.size());
            map.insert(QStringLiteral("infs"), names);
            map.insert(QStringLiteral("path"), QDir::toNativeSeparators(vendor.absoluteFilePath()));
            drivers.append(map);
        }
    }
    m_drivers = drivers;
    emit toolsChanged();
}

QVariantList OfficialTools::tools() const
{
    return m_tools;
}

QVariantList OfficialTools::drivers() const
{
    return m_drivers;
}

QString OfficialTools::toolPath(const QString& id) const
{
    for (const QVariant& entry : m_tools) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toString() == id)
            return map.value(QStringLiteral("path")).toString();
    }
    return {};
}

bool OfficialTools::launch(const QString& id)
{
    for (const QVariant& entry : m_tools) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toString() != id)
            continue;
        if (!map.value(QStringLiteral("available")).toBool())
            return false;
        const QString exe = map.value(QStringLiteral("path")).toString();
        const QString dir = map.value(QStringLiteral("dir")).toString();
        return QProcess::startDetached(exe, {}, dir);
    }
    return false;
}

void OfficialTools::openFolder(const QString& id)
{
    for (const QVariant& entry : m_tools) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) {
            util::openPath(map.value(QStringLiteral("dir")).toString());
            return;
        }
    }
}

void OfficialTools::openRoot()
{
    util::openPath(root());
}

} // namespace gf
