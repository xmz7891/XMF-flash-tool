#include "FirmwareBrowser.h"

#include <QFileInfo>

namespace gf {

using namespace firmware;

FirmwareBrowser::FirmwareBrowser(QObject* parent)
    : QObject(parent)
{
    QString error;
    m_brands = loadOfflineBrands(&error);
    m_models = loadOfflineModels(&error);
}

QString brandName(const BrandEntry& brand)
{
    return brand.name.isEmpty() ? brand.id : brand.name;
}

QVariantList FirmwareBrowser::brands() const
{
    QVariantList list;
    list.reserve(m_brands.size());
    for (const BrandEntry& brand : m_brands) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), brand.id);
        map.insert(QStringLiteral("name"), brandName(brand));
        map.insert(QStringLiteral("subtitle"), brand.subtitle);
        map.insert(QStringLiteral("searchHint"), brand.searchHint);
        map.insert(QStringLiteral("officialCount"), brand.official.size());
        int modelCount = 0;
        for (const ModelEntry& model : m_models)
            if (model.brand.compare(brand.id, Qt::CaseInsensitive) == 0)
                ++modelCount;
        map.insert(QStringLiteral("modelCount"), modelCount);
        list.append(map);
    }
    return list;
}

QVariantMap FirmwareBrowser::brand(const QString& id) const
{
    for (const BrandEntry& brand : m_brands) {
        if (brand.id.compare(id, Qt::CaseInsensitive) != 0)
            continue;
        QVariantMap map;
        map.insert(QStringLiteral("id"), brand.id);
        map.insert(QStringLiteral("name"), brandName(brand));
        map.insert(QStringLiteral("subtitle"), brand.subtitle);
        map.insert(QStringLiteral("searchHint"), brand.searchHint);
        QVariantList notes;
        for (const QString& note : brand.notes)
            notes.append(note);
        map.insert(QStringLiteral("notes"), notes);
        QVariantList links;
        for (const QVariantMap& link : brand.official) {
            links.append(link);
        }
        map.insert(QStringLiteral("official"), links);
        return map;
    }
    return {};
}

QVariantList FirmwareBrowser::officialLinks(const QString& brandId) const
{
    return brand(brandId).value(QStringLiteral("official")).toList();
}

QVariantList FirmwareBrowser::models(const QString& brandId, const QString& query, int limit) const
{
    const QString needle = normalizeQuery(query);
    QVariantList list;
    for (const ModelEntry& model : m_models) {
        if (!brandId.isEmpty() && model.brand.compare(brandId, Qt::CaseInsensitive) != 0)
            continue;
        if (!needle.isEmpty()) {
            bool match = normalizeQuery(model.codename).contains(needle)
                         || normalizeQuery(model.series).contains(needle);
            if (!match) {
                for (const QString& name : model.names) {
                    if (normalizeQuery(name).contains(needle)) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match)
                continue;
        }
        QVariantList names;
        for (const QString& name : model.names)
            names.append(name);
        QVariantMap map;
        map.insert(QStringLiteral("codename"), model.codename);
        map.insert(QStringLiteral("brand"), model.brand);
        map.insert(QStringLiteral("series"), model.series);
        map.insert(QStringLiteral("names"), names);
        map.insert(QStringLiteral("name"), model.names.isEmpty() ? model.codename : model.names.first());
        list.append(map);
        if (list.size() >= limit)
            break;
    }
    return list;
}

QString FirmwareBrowser::identifyFile(const QString& path) const
{
    const QString fileName = QFileInfo(path).fileName();
    if (fileName.isEmpty())
        return {};
    const QHash<QString, QStringList> devices;
    return codenameFromFileName(fileName, m_models, devices);
}

} // namespace gf
