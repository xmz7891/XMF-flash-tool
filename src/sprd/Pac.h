#pragma once

// Spreadtrum PAC firmware container parser.
//
// PAC files wrap a set of download images (FDL1, FDL2, NV, code partitions...)
// in one container. Layout used here (little endian), matching the layout used
// by the community PAC unpackers:
//
//   Header (0x100 bytes typical):
//     0x00  char magic[4]   = "PAC\0"  (bytes 50 41 43 00)
//     0x04  u32 version
//     0x08  u32 headerSize  (>= 0x20, typically 0x100)
//     0x0C  u32 imageCount
//     0x10  u32 tableOffset (offset of the image table, typically 0x20)
//     0x14  u32 totalSize   (container size)
//     0x18  ...             reserved / vendor fields
//   Image table entry (0x40 bytes each at tableOffset):
//     0x00  u32 type        (1 = FDL1, 2 = FDL2, 0 = code, ...)
//     0x04  u32 offset      (absolute file offset of the image data)
//     0x08  u32 size
//     0x0C  u32 crc32       (optional, 0 when unused)
//     0x10  char name[32]   (NUL padded ASCII)
//     0x30  ...             reserved
//
// TODO(protocols): real PAC files produced by newer SPD tools may carry an
// extra header/encryption layer; verify with a retail PAC and document the
// vendor variants in docs/PROTOCOLS.md. Parsing is structural only - no
// signature verification is performed.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace gf::sprd {

inline constexpr quint32 kPacMagic = 0x00434150; // "PAC\0" little endian
inline constexpr quint32 kPacDefaultHeaderSize = 0x100;
inline constexpr quint32 kPacImageEntrySize = 0x40;

enum class PacImageType : quint32 {
    Code = 0,
    Fdl1 = 1,
    Fdl2 = 2,
    Nv = 3,
    Erase = 4,
    Firmware = 5,
};

struct PacImage {
    quint32 type = 0;
    quint32 offset = 0;
    quint32 size = 0;
    quint32 crc32 = 0;
    QString name;
};

struct PacInfo {
    bool valid = false;
    quint32 version = 0;
    quint32 headerSize = 0;
    quint32 totalSize = 0;
    QList<PacImage> images;
    QStringList warnings;
    QString error;

    static PacInfo parse(const QByteArray& file);
    QByteArray imageData(const QByteArray& file, int index, QString* error = nullptr) const;
};

QString pacImageTypeName(quint32 type);
QString describePac(const PacInfo& info);

} // namespace gf::sprd
