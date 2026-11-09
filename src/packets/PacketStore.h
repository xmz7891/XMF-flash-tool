#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <memory>

namespace gf::packets {

class ZipArchive;

/// One device record from a GeekFlashPacket Config.xml (Model element).
struct ModelEntry {
    QString name;
    QString brand;
    QString series;
    QVariantMap attributes;
    QString description;
};

struct PacketInfo {
    QString id;
    QString displayName;
    QString filePath;       // zip path or folder path
    QString sourceType;     // "zip" | "folder"
    QString type;           // MTK / QC / SPRD
    QString publisher;
    QString version;
    QString tip;
    QDateTime published;
    QString configEntry;    // path of Config.xml inside the container
    QString prefix;         // archive folder prefix ("" for folders)
    int brandCount = 0;
    int seriesCount = 0;
    int modelCount = 0;
    QStringList brands;
    QVector<ModelEntry> models;
    qint64 sizeBytes = 0;
};

/// Discovers GeekFlashPacket resource packs (Config.xml + payload files) and
/// resolves the DA / FDL / Preloader files referenced by model entries.
class PacketStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList packets READ packets NOTIFY packetsChanged)
    Q_PROPERTY(QString packetDir READ packetDir CONSTANT)
    Q_PROPERTY(int modelCount READ modelCount NOTIFY packetsChanged)

public:
    explicit PacketStore(QObject* parent = nullptr);
    ~PacketStore() override;

    /// Application-wide instance used by AppController and packet tools.
    static PacketStore& instance();

    /// Scans the standard directories: <exe>/data/packets and the user data dir.
    void initialize();
    /// Scans explicit directories (used by tests).
    void scan(const QStringList& directories);

    QVariantList packets() const;
    int modelCount() const;
    QString packetDir() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool importPackage(const QString& path);
    Q_INVOKABLE QVariantList models(const QString& packetId) const;
    Q_INVOKABLE QVariantMap findModel(const QString& packetId, const QString& modelName) const;
    /// Extracts (zip) or resolves (folder) a referenced resource to a local path.
    Q_INVOKABLE QString resourcePath(const QString& packetId, const QString& relativePath);
    /// Extracts a model resource by attribute name (e.g. "DA1", "Preloader").
    Q_INVOKABLE QString modelResource(const QString& packetId, const QString& modelName,
                                      const QString& attribute);

    const PacketInfo* packet(const QString& id) const;
    QStringList packetIds() const;
    QString errorString() const { return m_lastError; }

signals:
    void packetsChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> d;

    void scanDirectory(const QString& directory);
    void loadContainer(const QString& path, bool isZip);
    QString extractZipEntry(const PacketInfo& packet, const QString& entry);
    QString cachedPath(const PacketInfo& packet, const QString& entry);
    void setError(const QString& error);

    QString m_lastError;
};

} // namespace gf::packets
