#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "firmware/FirmwareData.h"

namespace gf {

/// Read-only browser over the bundled offline brand / model database
/// (data/firmware/brands.json + models.json). Used by the firmware page; all
/// lookups are in-memory and cheap enough to call from QML bindings.
class FirmwareBrowser : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList brands READ brands CONSTANT)
    Q_PROPERTY(int modelCount READ modelCount CONSTANT)

public:
    explicit FirmwareBrowser(QObject* parent = nullptr);

    QVariantList brands() const;
    int modelCount() const { return int(m_models.size()); }

    Q_INVOKABLE QVariantMap brand(const QString& id) const;
    Q_INVOKABLE QVariantList models(const QString& brandId, const QString& query, int limit = 200) const;
    Q_INVOKABLE QVariantList officialLinks(const QString& brandId) const;
    /// Best-effort codename extraction from a firmware file name.
    Q_INVOKABLE QString identifyFile(const QString& path) const;

private:
    QVector<firmware::BrandEntry> m_brands;
    QVector<firmware::ModelEntry> m_models;
};

} // namespace gf
