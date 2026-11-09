#include "BootLogo.h"

#include <QtEndian>

namespace gf::img {

namespace {

struct Signature {
    const char* name;
    QByteArray magic;
};

const QList<Signature>& signatures()
{
    static const QList<Signature> list = {
        {"png", QByteArrayLiteral("\x89PNG\r\n\x1a\n")},
        {"jpeg", QByteArrayLiteral("\xFF\xD8\xFF")},
        {"bmp", QByteArrayLiteral("BM")},
        {"webp", QByteArrayLiteral("RIFF")},
    };
    return list;
}

quint32 imageLengthFor(const QByteArray& blob, qint64 start, const QByteArray& magic)
{
    // Try to compute a sane end for the common containers; fall back to EOF.
    if (magic.startsWith(QByteArrayLiteral("BM")) && start + 6 <= blob.size()) {
        const quint32 size = qFromLittleEndian<quint32>(blob.constData() + start + 2);
        if (size >= 14 && start + qint64(size) <= blob.size())
            return size;
    }
    return quint32(blob.size() - start);
}

} // namespace

QList<EmbeddedImage> findEmbeddedImages(const QByteArray& blob, int maxImages)
{
    QList<EmbeddedImage> found;
    for (const Signature& signature : signatures()) {
        qint64 from = 0;
        while (found.size() < maxImages) {
            const qint64 at = blob.indexOf(signature.magic, from);
            if (at < 0)
                break;
            from = at + signature.magic.size();
            EmbeddedImage image;
            image.format = QString::fromLatin1(signature.name);
            if (signature.name == QLatin1String("png")) {
                // PNG chunk: find IEND and cut there, otherwise take the rest.
                const qint64 end = blob.indexOf(QByteArrayLiteral("IEND"), at);
                const qint64 stop = end >= 0 ? end + 8 : blob.size();
                image.data = blob.mid(at, qint64(stop) - at);
            } else {
                image.data = blob.mid(at, imageLengthFor(blob, at, signature.magic));
            }
            image.offset = at;
            found.append(image);
        }
        if (found.size() >= maxImages)
            break;
    }
    return found;
}

QImage decodeRawBitmap(const QByteArray& data, int width, int height, qint64 offset, int bitsPerPixel,
                       bool bigEndian)
{
    if (width <= 0 || height <= 0 || offset < 0)
        return {};
    const int bytesPerPixel = bitsPerPixel / 8;
    if (bytesPerPixel <= 0)
        return {};
    const qint64 required = offset + qint64(width) * height * bytesPerPixel;
    if (required > data.size())
        return {};

    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::black);
    const uchar* base = reinterpret_cast<const uchar*>(data.constData()) + offset;
    const qsizetype rowBytes = qsizetype(width) * bytesPerPixel;

    for (int y = 0; y < height; ++y) {
        const uchar* row = base + y * rowBytes;
        auto* out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const uchar* pixel = row + x * bytesPerPixel;
            quint8 r = 0, g = 0, b = 0;
            if (bitsPerPixel == 16) {
                const quint16 value = bigEndian ? qFromBigEndian<quint16>(pixel)
                                                : qFromLittleEndian<quint16>(pixel);
                r = quint8(((value >> 11) & 0x1F) * 255 / 31);
                g = quint8(((value >> 5) & 0x3F) * 255 / 63);
                b = quint8((value & 0x1F) * 255 / 31);
            } else if (bitsPerPixel == 24) {
                // raw logos are stored BGR on Android devices
                b = pixel[0];
                g = pixel[1];
                r = pixel[2];
            } else {
                // 32-bit: assume BGRA / RGBA in little endian memory order.
                b = pixel[0];
                g = pixel[1];
                r = pixel[2];
            }
            out[x] = qRgb(r, g, b);
        }
    }
    return image;
}

QByteArray encodeRawBitmap(const QImage& image, int bitsPerPixel, bool bigEndian)
{
    if (image.isNull())
        return {};
    if (bitsPerPixel != 16 && bitsPerPixel != 24 && bitsPerPixel != 32)
        return {};
    const QImage source = image.convertToFormat(QImage::Format_RGB32);
    const int bytesPerPixel = bitsPerPixel / 8;
    QByteArray out;
    out.resize(qsizetype(source.width()) * source.height() * bytesPerPixel);
    char* base = out.data();
    for (int y = 0; y < source.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(source.scanLine(y));
        char* target = base + qsizetype(y) * source.width() * bytesPerPixel;
        for (int x = 0; x < source.width(); ++x) {
            const QRgb pixel = row[x];
            const quint8 r = quint8(qRed(pixel));
            const quint8 g = quint8(qGreen(pixel));
            const quint8 b = quint8(qBlue(pixel));
            char* targetPixel = target + x * bytesPerPixel;
            if (bitsPerPixel == 16) {
                const quint16 value = quint16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                if (bigEndian) {
                    targetPixel[0] = char(value >> 8);
                    targetPixel[1] = char(value & 0xFF);
                } else {
                    targetPixel[0] = char(value & 0xFF);
                    targetPixel[1] = char(value >> 8);
                }
            } else if (bitsPerPixel == 24) {
                targetPixel[0] = char(b);
                targetPixel[1] = char(g);
                targetPixel[2] = char(r);
            } else {
                targetPixel[0] = char(b);
                targetPixel[1] = char(g);
                targetPixel[2] = char(r);
                targetPixel[3] = char(qAlpha(pixel));
            }
        }
    }
    return out;
}

} // namespace gf::img
