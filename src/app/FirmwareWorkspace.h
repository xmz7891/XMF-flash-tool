#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

namespace gf {

/// Builds the partition / image tables shown by the flashing workbenches.
///
/// Nothing is listed until the user actually loads firmware or refreshes a
/// connected device: every method returns a result map and the UI shows its
/// empty state when the returned partition list is empty.
///
///   { ok: bool, error: QString, kind: QString, title: QString,
///     subtitle: QString, partitions: [ { name, sizeText, sizeBytes,
///     address, file, region, checked, detail } ] }
class FirmwareWorkspace : public QObject {
    Q_OBJECT

public:
    explicit FirmwareWorkspace(QObject* parent = nullptr);

    /// MTK scatter.txt -> partition table.
    Q_INVOKABLE QVariantMap loadScatter(const QString& path);
    /// Spreadtrum PAC container -> image table.
    Q_INVOKABLE QVariantMap loadPac(const QString& path);
    /// Xiaomi/Redmi tgz fastboot package -> image table (tar.gz listing).
    Q_INVOKABLE QVariantMap loadMiFlashPackage(const QString& path);
    /// GPT inside a disk image -> partition table.
    Q_INVOKABLE QVariantMap loadGptImage(const QString& path);
    /// Partitions of a connected device via ADB (by-name links).
    Q_INVOKABLE QVariantMap loadAdbPartitions(const QString& serial);
    /// Partitions of a bootloader/fastboot mode device via getvar all.
    Q_INVOKABLE QVariantMap loadFastbootPartitions(const QString& serial);
    /// Parses a Rockchip RKFW/RKAF update image into the partition table.
    Q_INVOKABLE QVariantMap loadRkFirmware(const QString& path);
    /// Parses an Allwinner IMAGEWTY (PhoenixSuit) firmware image.
    Q_INVOKABLE QVariantMap loadAwFirmware(const QString& path);
};

} // namespace gf
