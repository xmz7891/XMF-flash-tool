#pragma once

// Partition model shared by all flashing pipelines.
//
// The table is always derived from data that was actually loaded or read:
//   * Qualcomm / Rockchip : GPT read from the device (Firehose / RockUSB)
//   * MediaTek            : scatter file shipped with the firmware
//   * Spreadtrum          : PAC image table (+ optional address map)
//   * Allwinner           : user supplied partition map (FEL has no table API)
//
// Nothing in this file touches the UI: tables are plain data and can be
// converted to QVariantList for the QML workbench (see toVariantList()).

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace gf::mtk {
struct ScatterInfo;
}
namespace gf::sprd {
struct PacInfo;
}

namespace gf::flows {

struct PartitionInfo {
    int index = 0;
    QString name;
    QString fileName;  // firmware file name (scatter) / PAC image name
    QString type;      // scatter type / GPT type / PAC image type
    QString region;    // scatter region / storage region
    quint64 offset = 0; // byte offset on the storage device
    quint64 size = 0;   // bytes
    bool isDownload = false;
    bool hasAddress = true; // false for PAC entries before an address map is applied

    bool contains(quint64 byteOffset) const { return byteOffset >= offset && byteOffset < offset + size; }
    QVariantMap toVariant() const;
};

struct PartitionTable {
    bool valid = false;
    QString platform;   // qcom / mtk / sprd / rk / aw
    QString source;     // gpt / scatter / pac / parameter / map
    QString storageType; // ufs / emmc / spinor ...
    quint32 sectorSize = 512;
    QString error;
    QList<PartitionInfo> partitions;

    int count() const { return int(partitions.size()); }
    QStringList names() const;
    /// Looks a partition up by exact name first, then base name (strips _a/_b).
    int indexOf(const QString& name) const;
    const PartitionInfo* find(const QString& name) const;
    QVariantList toVariantList() const;

    static PartitionTable fromGptData(const QByteArray& prefix, const QString& platform,
                                      const QString& storageType = {});
    static PartitionTable fromScatter(const mtk::ScatterInfo& scatter,
                                      const QString& platform = QStringLiteral("mtk"));
    static PartitionTable fromPac(const sprd::PacInfo& pac,
                                  const QString& platform = QStringLiteral("sprd"));
    static PartitionTable fromParameterText(const QString& text,
                                            const QString& platform = QStringLiteral("rk"));
    static PartitionTable fromPartitionMapText(const QString& text, const QString& platform,
                                               quint32 sectorSize = 512);
};

/// <platform>_<partition>_<yyyyMMdd_HHmmss>.img
QString backupFileName(const QString& platform, const QString& partition,
                       const QDateTime& now = QDateTime::currentDateTime());
QString timestampForFileName(const QDateTime& now = QDateTime::currentDateTime());

/// Parses MBR+GPT from a device prefix. Scans 512- and 4096-byte logical sector
/// sizes, so it works for both eMMC (512) and UFS (4096) Firehose reads.
PartitionTable parseGptPrefix(const QByteArray& prefix, const QString& platform,
                              const QString& storageType = {});

} // namespace gf::flows
