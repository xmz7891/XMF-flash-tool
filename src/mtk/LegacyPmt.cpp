#include "LegacyPmt.h"

#include <QtEndian>

namespace gf::mtk {
namespace {
constexpr int kBlockSize = 0x1000;
constexpr int kReserved = 0x100000;
constexpr int kMirrorDelta = 0x1000;
constexpr int kEntrySize = 0x58;
constexpr int kEntryCount = 40;
constexpr quint32 kPtMagic = 0x50547631u;
constexpr quint32 kMptMagic = 0x4D505431u;

quint64 rd64(const QByteArray& d, int at)
{
    if (at + 8 > d.size()) return 0;
    return qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(d.constData() + at));
}

bool validBlock(const QByteArray& block, quint32 magic)
{
    if (block.size() < kBlockSize) return false;
    const uchar* p = reinterpret_cast<const uchar*>(block.constData());
    if (qFromLittleEndian<quint32>(p) != magic) return false;
    if (block.mid(4, 4) != QByteArray("1.0\0", 4)) return false;
    return qFromLittleEndian<quint32>(p + 0xFFC) == magic;
}

LegacyPmtInfo parseBlock(const QByteArray& block, quint32 magic, bool mirror)
{
    LegacyPmtInfo info;
    info.mirror = mirror;
    if (!validBlock(block, magic)) { info.error = QStringLiteral("invalid PT/MPT block"); return info; }
    for (int i = 0; i < kEntryCount; ++i) {
        const int base = 8 + i * kEntrySize;
        QByteArray raw = block.mid(base, 64);
        const int nul = raw.indexOf('\0');
        if (nul >= 0) raw.truncate(nul);
        const QString name = QString::fromLatin1(raw).trimmed();
        const quint64 size = rd64(block, base + 0x40);
        const quint64 offset = rd64(block, base + 0x48);
        const quint64 flags = rd64(block, base + 0x50);
        if (name.isEmpty() && size == 0 && offset == 0) continue;
        info.entries.append({name, size, offset, flags});
    }
    info.valid = true;
    return info;
}
} // namespace

LegacyPmtInfo parseLegacyPmt(const QByteArray& userImage)
{
    LegacyPmtInfo info;
    if (userImage.size() < kReserved + kBlockSize) {
        info.error = QStringLiteral("image too small for legacy PMT (need >= 1 MiB reserve)");
        return info;
    }
    const int mainAt = userImage.size() - kReserved;
    const int mirrorAt = mainAt + kMirrorDelta;
    if (mainAt >= 0) {
        const auto main = parseBlock(userImage.mid(mainAt, kBlockSize), kPtMagic, false);
        if (main.valid) return main;
    }
    if (mirrorAt + kBlockSize <= userImage.size()) {
        const auto mirror = parseBlock(userImage.mid(mirrorAt, kBlockSize), kMptMagic, true);
        if (mirror.valid) return mirror;
    }
    info.error = QStringLiteral("no valid legacy PT/MPT magic at USER end");
    return info;
}

QString describeLegacyPmt(const LegacyPmtInfo& info)
{
    if (!info.valid) return info.error;
    return QStringLiteral("legacy %1 · %2 partitions")
        .arg(info.mirror ? QStringLiteral("MPT") : QStringLiteral("PT"))
        .arg(info.entries.size());
}
} // namespace gf::mtk
