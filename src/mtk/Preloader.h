#pragma once

// MediaTek preloader image parser.
//
// Real preloaders start with a 0x38-byte "FILE_INFO" header followed by a chain
// of small tagged sections, then the ARM boot code:
//
//   0x00  u32 magic         = 0x014D4D4D ("MMM" + 0x01)
//   0x04  u32 headerSize    = 0x38 (56) in every sample we have
//   0x08  char name[12]     = "FILE_INFO"
//   0x14  ...               (file count / version / flags, chip dependent)
//   0x20  u32 imageSize     total preloader image size; equals the file size
//                           for most dumps, a few files append a signature so
//                           imageSize = fileSize - 256
//   0x24  u32 fieldAt24     observed 0x20000/0x40000/0x80000/0x100000/0xFFFFFFFF;
//                           purpose unconfirmed, do NOT treat as load address
//   0x38  sections: repeated { u32 magic=0x014D4D4D, u16 length, u16 type,
//                              payload[length-8] } until the boot code starts.
//
// Sample sections (MT6768): type 1 (12 bytes), type 7 (100 bytes),
// type 2 (20 bytes), type 8 (48 bytes). The first non-"MMM" offset is the
// entry area of the actual ARM code.
//
// Used only for offline validation / user feedback; flashing uses the raw file.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace gf::mtk {

inline constexpr quint32 kPreloaderMagic = 0x014D4D4D; // little endian "MMM\x01"
inline constexpr quint32 kPreloaderMagicMask = 0x00FFFFFF;
inline constexpr quint32 kPreloaderMagicLow = 0x004D4D4D; // "MMM"
inline constexpr quint16 kPreloaderHeaderSize = 0x38;

struct PreloaderSection {
    quint32 offset = 0;
    quint16 length = 0;
    quint16 type = 0;
};

struct PreloaderInfo {
    bool valid = false;
    quint32 magic = 0;
    quint32 headerSize = 0;
    QString fileInfoName;
    quint32 imageSize = 0;
    quint32 fieldAt24 = 0;
    quint32 fieldAt28 = 0;
    quint32 fieldAt2C = 0;
    quint32 fieldAt30 = 0;
    quint32 fieldAt34 = 0;
    QList<PreloaderSection> sections;
    quint32 sectionsEnd = 0; // first byte after the tagged sections (boot code)
    bool hasAppendedSignature = false;
    QStringList warnings;
    QString error;

    static bool looksLikePreloader(const QByteArray& data);
    static PreloaderInfo parse(const QByteArray& data);
};

QString describePreloader(const PreloaderInfo& info);

} // namespace gf::mtk
