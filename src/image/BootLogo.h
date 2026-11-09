#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

namespace gf::img {

/// One picture found inside a logo / boot logo blob by signature scanning.
struct EmbeddedImage {
    QString format;      // "png" / "jpeg" / "bmp" / "webp"
    QByteArray data;
    qint64 offset = 0;
};

/// Scans a blob for embedded PNG / JPEG / BMP / WebP headers. Many custom
/// bootlogo partitions contain PNG or JPEG bitmaps even when the outer format
/// is proprietary, so this works without knowing the vendor header.
QList<EmbeddedImage> findEmbeddedImages(const QByteArray& blob, int maxImages = 64);

/// Decodes a headerless or padded raw bitmap (RGB565 / RGB888 / ARGB8888) into
/// a QImage. Returns a null image when the buffer is too small.
QImage decodeRawBitmap(const QByteArray& data, int width, int height, qint64 offset, int bitsPerPixel,
                       bool bigEndian = false);

/// Encodes a QImage into a headerless raw bitmap (RGB565 / RGB888 / ARGB8888).
/// 24/32-bit output is BGR(A) ordered, matching Android logo partitions.
QByteArray encodeRawBitmap(const QImage& image, int bitsPerPixel, bool bigEndian = false);

} // namespace gf::img
