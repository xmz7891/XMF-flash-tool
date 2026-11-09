#include "PacketStore.h"

#include "ZipArchive.h"

#include "Log.h"
#include "Util.h"

#include <QDir>
#include <QDomDocument>
#include <QFileInfo>
#include <QSet>
#include <QStringConverter>

namespace gf::packets {

struct PacketStore::Impl {
    QVector<PacketInfo> packets;
    QHash<QString, int> indexById;
    QHash<QString, std::shared_ptr<ZipArchive>> archives;
    QStringList scannedDirs;
};

PacketStore::PacketStore(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
}

PacketStore::~PacketStore() = default;

PacketStore& PacketStore::instance()
{
    static PacketStore store;
    return store;
}

QString PacketStore::packetDir() const
{
    return util::dataDir() + QStringLiteral("/packets");
}

void PacketStore::initialize()
{
    QStringList dirs;
    dirs << packetDir();
    dirs << util::appDataDir() + QStringLiteral("/packets");
    const QString envDir = qEnvironmentVariable("XMF_PACKET_DIR");
    if (!envDir.isEmpty())
        dirs << envDir;
    scan(dirs);
}

void PacketStore::refresh()
{
    QStringList dirs = d->scannedDirs;
    if (dirs.isEmpty())
        dirs << packetDir();
    scan(dirs);
}

void PacketStore::scan(const QStringList& directories)
{
    d->scannedDirs = directories;
    d->packets.clear();
    d->indexById.clear();
    d->archives.clear();

    QSet<QString> seenFiles;
    for (const QString& dir : directories) {
        QDir directory(dir);
        if (!directory.exists())
            continue;
        const auto files = directory.entryInfoList({QStringLiteral("*.zip")}, QDir::Files, QDir::Name);
        for (const auto& file : files) {
            // skip duplicates such as "pack.zip" and "pack(1).zip"
            const QString base = file.completeBaseName();
            const QString canonical = base.endsWith(QStringLiteral("(1)")) ? base.chopped(3) : base;
            if (seenFiles.contains(canonical))
                continue;
            seenFiles.insert(canonical);
            loadContainer(file.absoluteFilePath(), true);
        }
        const auto subdirs = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& subdir : subdirs) {
            if (QFileInfo::exists(subdir.absoluteFilePath() + QStringLiteral("/Config.xml")))
                loadContainer(subdir.absoluteFilePath(), false);
        }
    }

    std::sort(d->packets.begin(), d->packets.end(), [](const PacketInfo& a, const PacketInfo& b) {
        return a.displayName.toLower() < b.displayName.toLower();
    });
    for (int i = 0; i < d->packets.size(); ++i)
        d->indexById.insert(d->packets.at(i).id, i);

    GF_INFO(QStringLiteral("packets"), QStringLiteral("扫描到 %1 个引导包，共 %2 个机型")
                                             .arg(d->packets.size())
                                             .arg(modelCount()));
    emit packetsChanged();
}

void PacketStore::scanDirectory(const QString& directory)
{
    scan({directory});
}

namespace {

QString findConfigEntry(const QStringList& entries)
{
    for (const QString& name : entries) {
        if (name.compare(QStringLiteral("Config.xml"), Qt::CaseInsensitive) == 0)
            return name;
    }
    for (const QString& name : entries) {
        if (name.endsWith(QStringLiteral("/Config.xml"), Qt::CaseInsensitive))
            return name;
    }
    return {};
}

QString prefixOf(const QString& entry)
{
    const int slash = entry.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : entry.left(slash + 1);
}

QString normaliseRelative(const QString& path)
{
    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (p.startsWith(QLatin1Char('/')))
        p.remove(0, 1);
    return p;
}

} // namespace

void PacketStore::loadContainer(const QString& path, bool isZip)
{
    PacketInfo packet;
    packet.filePath = path;
    packet.sourceType = isZip ? QStringLiteral("zip") : QStringLiteral("folder");
    packet.id = QFileInfo(path).completeBaseName();
    // normalise duplicate names such as "package_qualcomm_edl-master(1)"
    if (packet.id.endsWith(QStringLiteral("(1)")))
        packet.id.chop(3);
    packet.id = packet.id.toLower();

    QByteArray configData;
    QStringList entries;
    if (isZip) {
        auto archive = std::make_shared<ZipArchive>();
        if (!archive->open(path)) {
            setError(archive->errorString());
            GF_WARN(QStringLiteral("packets"), QStringLiteral("跳过引导包 %1: %2").arg(path, archive->errorString()));
            return;
        }
        entries = archive->entryNames();
        packet.configEntry = findConfigEntry(entries);
        if (packet.configEntry.isEmpty()) {
            GF_WARN(QStringLiteral("packets"), QStringLiteral("引导包 %1 中没有 Config.xml").arg(path));
            return;
        }
        packet.prefix = prefixOf(packet.configEntry);
        configData = archive->read(packet.configEntry);
        d->archives.insert(packet.id, archive);
        packet.sizeBytes = QFileInfo(path).size();
    } else {
        packet.configEntry = QStringLiteral("Config.xml");
        packet.prefix.clear();
        configData = util::readFile(path + QStringLiteral("/Config.xml"));
        packet.sizeBytes = 0;
        const auto files = QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto& f : files)
            entries.append(f.fileName());
    }

    if (configData.isEmpty()) {
        GF_WARN(QStringLiteral("packets"), QStringLiteral("引导包 %1 的 Config.xml 为空").arg(path));
        return;
    }

    QDomDocument doc;
    QString parseError;
    int line = 0;
    int column = 0;
    if (!doc.setContent(configData, &parseError, &line, &column)) {
        setError(QStringLiteral("Config.xml 解析失败 (行 %1): %2").arg(line).arg(parseError));
        GF_WARN(QStringLiteral("packets"), m_lastError);
        return;
    }

    const QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("GeekFlashPacket")) {
        GF_WARN(QStringLiteral("packets"), QStringLiteral("引导包 %1 根节点不是 GeekFlashPacket").arg(path));
        return;
    }

    const QDomElement info = root.firstChildElement(QStringLiteral("PacketInfo"));
    packet.publisher = info.attribute(QStringLiteral("Publisher"));
    packet.version = info.attribute(QStringLiteral("PublishVersion"));
    packet.tip = info.attribute(QStringLiteral("Tip"));
    packet.type = info.attribute(QStringLiteral("Type")).toUpper();
    const QString dateAttr = info.attribute(QStringLiteral("PublishDate"));
    if (!dateAttr.isEmpty()) {
        bool ok = false;
        const qint64 ms = dateAttr.toLongLong(&ok);
        if (ok)
            packet.published = QDateTime::fromMSecsSinceEpoch(ms);
    }
    packet.displayName = packet.type.isEmpty() ? packet.id : (packet.type + QStringLiteral(" · ") + packet.id);
    if (packet.displayName.contains(QStringLiteral("package"), Qt::CaseInsensitive)) {
        QString name = packet.displayName;
        name.replace(QStringLiteral("package_"), QString(), Qt::CaseInsensitive);
        name.replace(QStringLiteral("-master"), QString(), Qt::CaseInsensitive);
        packet.displayName = name;
    }

    const auto brands = root.elementsByTagName(QStringLiteral("Brand"));
    QSet<QString> brandSet;
    QSet<QString> seriesSet;
    for (int b = 0; b < brands.count(); ++b) {
        const QDomElement brand = brands.at(b).toElement();
        const QString brandName = brand.attribute(QStringLiteral("Name"));
        brandSet.insert(brandName);
        const auto seriesList = brand.elementsByTagName(QStringLiteral("Series"));
        for (int s = 0; s < seriesList.count(); ++s) {
            const QDomElement series = seriesList.at(s).toElement();
            const QString seriesName = series.attribute(QStringLiteral("Name"));
            seriesSet.insert(brandName + QLatin1Char('/') + seriesName);
            const auto modelList = series.elementsByTagName(QStringLiteral("Model"));
            for (int m = 0; m < modelList.count(); ++m) {
                const QDomElement model = modelList.at(m).toElement();
                ModelEntry entry;
                entry.name = model.attribute(QStringLiteral("Name"));
                entry.brand = brandName;
                entry.series = seriesName;
                entry.description = model.attribute(QStringLiteral("Description"));
                const auto attrs = model.attributes();
                for (int a = 0; a < attrs.count(); ++a) {
                    const auto attr = attrs.item(a).toAttr();
                    entry.attributes.insert(attr.name(), attr.value());
                }
                if (entry.name.isEmpty())
                    entry.name = QStringLiteral("未命名机型");
                packet.models.append(entry);
            }
        }
    }
    packet.brands = brandSet.values();
    packet.brands.sort();
    packet.brandCount = brandSet.size();
    packet.seriesCount = seriesSet.size();
    packet.modelCount = packet.models.size();

    d->packets.append(packet);
    const QString key = packet.id;
    if (isZip)
        d->archives.insert(key, d->archives.value(key));
}

void PacketStore::setError(const QString& error)
{
    m_lastError = error;
}

QVariantList PacketStore::packets() const
{
    QVariantList list;
    list.reserve(d->packets.size());
    for (const PacketInfo& p : d->packets) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), p.id);
        map.insert(QStringLiteral("name"), p.displayName);
        map.insert(QStringLiteral("type"), p.type);
        map.insert(QStringLiteral("publisher"), p.publisher);
        map.insert(QStringLiteral("version"), p.version);
        map.insert(QStringLiteral("tip"), p.tip);
        map.insert(QStringLiteral("published"), p.published.toString(QStringLiteral("yyyy-MM-dd")));
        map.insert(QStringLiteral("sourceType"), p.sourceType);
        map.insert(QStringLiteral("filePath"), p.filePath);
        map.insert(QStringLiteral("brandCount"), p.brandCount);
        map.insert(QStringLiteral("seriesCount"), p.seriesCount);
        map.insert(QStringLiteral("modelCount"), p.modelCount);
        map.insert(QStringLiteral("brands"), p.brands);
        map.insert(QStringLiteral("sizeBytes"), double(p.sizeBytes));
        map.insert(QStringLiteral("loaded"), true);
        list.append(map);
    }
    return list;
}

int PacketStore::modelCount() const
{
    int total = 0;
    for (const PacketInfo& p : d->packets)
        total += p.modelCount;
    return total;
}

const PacketInfo* PacketStore::packet(const QString& id) const
{
    const auto it = d->indexById.constFind(id);
    if (it == d->indexById.constEnd())
        return nullptr;
    return &d->packets.at(it.value());
}

QStringList PacketStore::packetIds() const
{
    QStringList ids;
    for (const PacketInfo& p : d->packets)
        ids << p.id;
    return ids;
}

QVariantList PacketStore::models(const QString& packetId) const
{
    QVariantList list;
    const PacketInfo* info = packet(packetId);
    if (!info)
        return list;
    for (const ModelEntry& model : info->models) {
        QVariantMap map = model.attributes;
        map.insert(QStringLiteral("name"), model.name);
        map.insert(QStringLiteral("brand"), model.brand);
        map.insert(QStringLiteral("series"), model.series);
        map.insert(QStringLiteral("description"), model.description);
        list.append(map);
    }
    return list;
}

QVariantMap PacketStore::findModel(const QString& packetId, const QString& modelName) const
{
    const PacketInfo* info = packet(packetId);
    if (!info)
        return {};
    for (const ModelEntry& model : info->models) {
        if (model.name.compare(modelName, Qt::CaseInsensitive) == 0) {
            QVariantMap map = model.attributes;
            map.insert(QStringLiteral("name"), model.name);
            map.insert(QStringLiteral("brand"), model.brand);
            map.insert(QStringLiteral("series"), model.series);
            return map;
        }
    }
    return {};
}

QString PacketStore::resourcePath(const QString& packetId, const QString& relativePath)
{
    const PacketInfo* info = packet(packetId);
    if (!info)
        return {};
    const QString rel = normaliseRelative(relativePath);
    if (rel.isEmpty())
        return {};

    if (info->sourceType == QLatin1String("folder")) {
        const QString full = info->filePath + QLatin1Char('/') + rel;
        return QFileInfo::exists(full) ? QDir::toNativeSeparators(full) : QString();
    }

    const auto archiveIt = d->archives.constFind(packetId);
    if (archiveIt == d->archives.constEnd() || !archiveIt.value())
        return {};

    QString entry = info->prefix + rel;
    if (!archiveIt.value()->contains(entry)) {
        // try without the archive prefix, then case-insensitive match
        if (archiveIt.value()->contains(rel)) {
            entry = rel;
        } else {
            const QStringList entries = archiveIt.value()->entryNames();
            QString match;
            for (const QString& candidate : entries) {
                if (candidate.endsWith(rel, Qt::CaseSensitive)) {
                    match = candidate;
                    break;
                }
            }
            if (match.isEmpty())
                return {};
            entry = match;
        }
    }
    return extractZipEntry(*info, entry);
}

QString PacketStore::modelResource(const QString& packetId, const QString& modelName, const QString& attribute)
{
    const QVariantMap model = findModel(packetId, modelName);
    if (model.isEmpty())
        return {};
    const QString value = model.value(attribute).toString();
    if (value.isEmpty())
        return {};
    return resourcePath(packetId, value);
}

QString PacketStore::cachedPath(const PacketInfo& packet, const QString& entry)
{
    QString relative = entry;
    if (!packet.prefix.isEmpty() && relative.startsWith(packet.prefix))
        relative = relative.mid(packet.prefix.size());
    const QString target = util::cacheDir() + QStringLiteral("/packets/") + packet.id + QLatin1Char('/')
                           + normaliseRelative(relative);
    return target;
}

QString PacketStore::extractZipEntry(const PacketInfo& packet, const QString& entry)
{
    const auto archiveIt = d->archives.constFind(packet.id);
    if (archiveIt == d->archives.constEnd() || !archiveIt.value())
        return {};
    const QString target = cachedPath(packet, entry);
    const qint64 expected = archiveIt.value()->entrySize(entry);
    const QFileInfo targetInfo(target);
    if (targetInfo.exists() && (expected < 0 || targetInfo.size() == expected))
        return QDir::toNativeSeparators(target);
    if (!archiveIt.value()->extractTo(entry, target)) {
        setError(QStringLiteral("无法从引导包提取 %1").arg(entry));
        return {};
    }
    return QDir::toNativeSeparators(target);
}

bool PacketStore::importPackage(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        setError(QStringLiteral("路径不存在: %1").arg(path));
        return false;
    }
    const QString targetDir = packetDir();
    util::ensureDir(targetDir);
    if (info.isFile() && info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0) {
        const QString target = targetDir + QLatin1Char('/') + info.fileName();
        if (QFile::exists(target))
            QFile::remove(target);
        if (!QFile::copy(path, target)) {
            setError(QStringLiteral("复制引导包失败: %1").arg(path));
            return false;
        }
    } else if (info.isDir()) {
        if (!QFileInfo::exists(path + QStringLiteral("/Config.xml"))) {
            setError(QStringLiteral("目录中没有 Config.xml: %1").arg(path));
            return false;
        }
        const QString target = targetDir + QLatin1Char('/') + info.fileName();
        if (QFileInfo::exists(target))
            setError(QStringLiteral("引导包已存在: %1").arg(target));
        else if (!util::copyDir(path, target)) {
            setError(QStringLiteral("复制引导包目录失败: %1").arg(path));
            return false;
        }
    } else {
        setError(QStringLiteral("不支持的引导包类型: %1").arg(path));
        return false;
    }
    refresh();
    return true;
}

} // namespace gf::packets
