#include "AmlogicImage.h"
#include <QtEndian>
#include <QFile>

namespace gf::img::amlogic {
namespace {
quint32 u32(const QByteArray& d, int o) { if (o < 0 || o + 4 > d.size()) return 0; return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(d.constData()+o)); }
quint64 u64(const QByteArray& d, int o) { if (o < 0 || o + 8 > d.size()) return 0; return qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(d.constData()+o)); }
QString text(const QByteArray& d, int o, int len) {
    QByteArray b = d.mid(o, len);
    const int nul = b.indexOf('\0');
    if (nul >= 0) b.truncate(nul);
    return QString::fromLatin1(b).trimmed();
}
}
Image parse(const QByteArray& data) {
    Image image;
    constexpr quint32 kMagic = 0x27b51956;
    constexpr int kHead = 64;
    if (data.size() < kHead) { image.error = "Amlogic image smaller than 64-byte header"; return image; }
    const quint32 magic = u32(data, 8);
    if (magic != kMagic) { image.error = QStringLiteral("Amlogic image magic mismatch (0x%1)").arg(magic, 8, 16, QLatin1Char('0')); return image; }
    image.version = u32(data, 4);
    if (image.version != 1 && image.version != 2) { image.error = QStringLiteral("unsupported Amlogic image version %1").arg(image.version); return image; }
    image.align = u32(data, 20);
    image.imageSize = u64(data, 12);
    const quint32 count = u32(data, 24);
    const int typeLen = image.version == 1 ? 32 : 256;
    const int infoSize = 32 + 2 * typeLen + 32; // packed item header
    if (count > 4096 || qint64(kHead) + qint64(count) * infoSize > data.size()) {
        image.error = "Amlogic item table truncated";
        return image;
    }
    for (quint32 i = 0; i < count; ++i) {
        const int base = kHead + int(i) * infoSize;
        Item item;
        item.id = u32(data, base);
        item.fileType = u32(data, base + 4);
        item.currentOffset = u64(data, base + 8);
        item.offset = u64(data, base + 16);
        item.size = u64(data, base + 24);
        item.mainType = text(data, base + 32, typeLen);
        item.subType = text(data, base + 32 + typeLen, typeLen);
        item.verify = u32(data, base + 32 + 2 * typeLen);
        if (item.offset > quint64(data.size()) || item.size > quint64(data.size()) - item.offset) {
            image.error = QStringLiteral("item %1 (%2.%3) points outside image").arg(i).arg(item.subType, item.mainType);
            return image;
        }
        image.items.append(item);
    }
    image.valid = true;
    return image;
}

namespace {
quint32 crc32(const QByteArray& data) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xffffffffu;
    for (char ch : data) crc = table[(crc ^ uchar(ch)) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}
void put32(QByteArray& b, int o, quint32 v) { for (int i = 0; i < 4; ++i) b[o+i] = char((v >> (8*i)) & 0xff); }
void put64(QByteArray& b, int o, quint64 v) { for (int i = 0; i < 8; ++i) b[o+i] = char((v >> (8*i)) & 0xff); }
void putType(QByteArray& b, int o, int len, const QString& s) {
    const QByteArray x = s.toLatin1();
    for (int i = 0; i < len; ++i) b[o+i] = i < x.size() ? x[i] : '\0';
}
}
bool packDirectory(const QString& dir, const QString& output, QString* error) {
    QFile cmd(dir + QStringLiteral("/commands.txt"));
    if (!cmd.open(QIODevice::ReadOnly)) { if (error) *error = "cannot open commands.txt"; return false; }
    struct Entry { QString type, name, file; quint32 imgType = 0; QByteArray data; };
    QList<Entry> entries;
    while (!cmd.atEnd()) {
        QString line = QString::fromUtf8(cmd.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = line.split(QLatin1Char(':'));
        if (parts.size() < 4) { if (error) *error = "bad commands.txt line: " + line; return false; }
        Entry e;
        e.type = parts.at(0); e.name = parts.at(1);
        e.imgType = parts.at(2) == QLatin1String("sparse") ? 0xFE : 0x00;
        e.file = dir + QLatin1Char('/') + parts.at(3);
        QFile f(e.file);
        if (!f.open(QIODevice::ReadOnly)) { if (error) *error = "cannot read " + e.file; return false; }
        e.data = f.readAll();
        entries.append(e);
    }
    const int typeLen = 256;
    const int infoSize = 32 + 2 * typeLen + 32;
    const int headerSize = 64;
    qint64 dataOffset = headerSize + qint64(entries.size()) * infoSize;
    QByteArray out(headerSize + entries.size() * infoSize, '\0');
    for (int i = 0; i < entries.size(); ++i) {
        const Entry& e = entries.at(i);
        const int base = headerSize + i * infoSize;
        put32(out, base, quint32(i));
        put32(out, base + 4, e.imgType);
        put64(out, base + 8, 0);
        put64(out, base + 16, quint64(dataOffset));
        put64(out, base + 24, quint64(e.data.size()));
        putType(out, base + 32, typeLen, e.type);
        putType(out, base + 32 + typeLen, typeLen, e.name);
        if (e.type == QLatin1String("PARTITION")) out[base + 32 + 2 * typeLen] = 1;
        out.append(e.data);
        while (out.size() % 4 != 0) out.append('\0');
        dataOffset = out.size();
    }
    put32(out, 0, 0);
    put32(out, 4, 2);
    put32(out, 8, 0x27b51956u);
    put64(out, 12, quint64(out.size()));
    put32(out, 20, 4);
    put32(out, 24, quint32(entries.size()));
    put32(out, 0, crc32(out.mid(4)));
    QFile f(output);
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size()) { if (error) *error = "cannot write output"; return false; }
    return true;
}

QString describe(const Image& image) {
    if (!image.valid) return image.error;
    return QStringLiteral("Amlogic burning image v%1 · align %2 · %3 items · %4 MiB")
        .arg(image.version).arg(image.align).arg(image.items.size())
        .arg(image.imageSize / 1024.0 / 1024.0, 0, 'f', 1);
}
}
