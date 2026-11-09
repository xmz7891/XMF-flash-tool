#pragma once

// MediaTek scatter file parser.
//
// A scatter file is the YAML-ish partition description shipped with every MTK
// firmware. Only the subset needed by the flashing workbench is parsed:
//
//   - general: MTK_PLATFORM_CFG
//     info:
//       - config_version: V1.1.2
//         platform: MT6768
//         project: k62v1_64_bsp
//         storage: EMMC
//         boot_channel: MSDC_0
//         block_size: 0x20000
//
//   - partition_index: SYS0
//     partition_name: preloader
//     file_name: preloader_k62v1_64_bsp.bin
//     is_download: true
//     type: SV5_BL_BIN
//     linear_start_addr: 0x0
//     physical_start_addr: 0x0
//     partition_size: 0x40000
//     region: EMMC_BOOT_1
//     storage: HW_STORAGE_EMMC
//     boundary_check: true
//     is_reserved: false
//     operation_type: BOOTLOADERS
//     reserve: 0x00
//
// Parsing is deliberately tolerant: blocks are key/value collections, comments
// (#) and blank lines are ignored, values may be quoted. Encrypted/binary
// scatter files (which newer SP Flash Tool versions produce) are rejected with
// a readable error instead of being misparsed.

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace gf::mtk {

struct ScatterPartition {
    QString index;
    QString name;
    QString fileName;
    QString type;
    QString region;
    QString storage;
    QString operationType;
    bool isDownload = false;
    bool isReserved = false;
    bool boundaryCheck = false;
    quint64 linearStart = 0;
    quint64 physicalStart = 0;
    quint64 size = 0;
    quint64 reserve = 0;
};

struct ScatterInfo {
    bool valid = false;
    QString configVersion;
    QString platform;
    QString project;
    QString storage;
    QString bootChannel;
    quint64 blockSize = 0;
    QList<ScatterPartition> partitions;
    QStringList warnings;
    QString error;

    static ScatterInfo parse(const QString& text);
    static ScatterInfo parseFile(const QString& path, QString* ioError = nullptr);

    bool hasPartition(const QString& name) const;
};

QString describeScatter(const ScatterInfo& info);

} // namespace gf::mtk
