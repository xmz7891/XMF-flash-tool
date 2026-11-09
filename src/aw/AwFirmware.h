#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace gf::alwwin {

/// One file entry in an Allwinner IMAGEWTY firmware image.
struct AwFileEntry {
    QString name;
    QString mainType;
    QString subType;
    quint32 storedLength = 0;
    quint32 originalLength = 0;
    quint32 offset = 0;
    quint32 headerSize = 0;
    bool lengthMismatch = false;
};

/// Parsed IMAGEWTY image (the Allwinner "PhoenixSuit" firmware container).
struct AwImageInfo {
    bool ok = false;
    QString error;
    quint32 headerVersion = 0;
    quint32 headerSize = 0;
    quint32 formatVersion = 0;
    quint32 totalImageSize = 0;
    quint32 fileHeaderLength = 0;
    quint32 usbProductId = 0;
    quint32 usbVendorId = 0;
    quint32 hardwareId = 0;
    quint32 firmwareId = 0;
    quint32 numFiles = 0;
    QVector<AwFileEntry> files;
};

constexpr quint32 kAwMagicSize = 8;
constexpr quint32 kAwHeaderSize = 1024;
constexpr quint32 kAwFileHeadersStart = 0x400;
constexpr quint32 kAwFileEntryMin = 0x138;
constexpr quint32 kAwMaxFiles = 10000;

/// Parses the main header only (needs at least 68 bytes).
AwImageInfo parseAwHeader(const QByteArray& header);
/// Parses the header plus the file-header table. `header` must contain the
/// first 1024 bytes and `table` the file headers; `physicalSize` is the file
/// size used for range validation.
AwImageInfo parseAwImage(const QByteArray& header, const QByteArray& table, quint32 physicalSize);

} // namespace gf::alwwin
