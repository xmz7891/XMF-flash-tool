#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace gf::rockchip {

/// One image entry inside a Rockchip RKAF (update.img) container.
struct RkafPartition {
    QString name;
    QString path;
    quint32 flashSize = 0;
    quint32 partOffset = 0;
    quint32 flashOffset = 0;
    quint32 paddedSize = 0;
    quint32 byteCount = 0;
};

/// Parsed RKAF header. `data` must start at the "RKAF" signature.
struct RkafInfo {
    bool ok = false;
    QString error;
    QString model;
    QString id;
    QString manufacturer;
    quint32 version = 0;
    quint32 numParts = 0;
    QVector<RkafPartition> partitions;
};

/// Parsed RKFW wrapper header (factory update image). `data` must start at
/// "RKFW"; only the first 0x29 bytes are required.
struct RkfwInfo {
    bool ok = false;
    QString error;
    QString version;
    quint32 code = 0;
    quint8 chipCode = 0;
    QString chipName;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    quint32 bootOffset = 0;
    quint32 bootSize = 0;
    quint32 updateOffset = 0;
    quint32 updateSize = 0;
};

/// Size of the RKAF header in bytes.
constexpr quint32 kRkafHeaderSize = 2048;
/// Minimum size of the RKFW header prefix that parseRkfwHeader() reads.
constexpr quint32 kRkfwHeaderPrefix = 0x29;

RkfwInfo parseRkfwHeader(const QByteArray& data);
RkafInfo parseRkafHeader(const QByteArray& data);
/// Parse an embedded RKAF whose absolute offset is `baseOffset`; just a helper
/// for callers that already loaded the whole image.
RkafInfo parseRkafAt(const QByteArray& wholeImage, quint32 baseOffset);
QString chipName(quint8 chipCode);

} // namespace gf::rockchip
