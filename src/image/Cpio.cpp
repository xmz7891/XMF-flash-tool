#include "Cpio.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace gf::img::cpio {

namespace {

constexpr qsizetype kHeaderSize = 110;
constexpr qsizetype kNameOffset = 110; // magic 6 + 13 个 8 字符字段
constexpr qsizetype kMagicSize = 6;

quint32 parseHexField(const QByteArray& data, qsizetype offset)
{
    bool ok = false;
    const quint32 value = data.mid(offset, 8).toUInt(&ok, 16);
    if (!ok)
        fail(QStringLiteral("cpio 头部字段不是合法十六进制（偏移 %1）").arg(offset));
    return value;
}

void appendHexField(QByteArray& out, quint32 value)
{
    const QByteArray digits = QByteArray::number(value, 16).rightJustified(8, '0').right(8);
    out.append(digits);
}

QString safeName(QString name)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);
    while (name.endsWith(QLatin1Char('/')))
        name.chop(1);
    if (name == QStringLiteral("."))
        name.clear();
    return name;
}

} // namespace

QString Entry::typeText() const
{
    if (isDir())
        return QStringLiteral("目录");
    if (isRegular())
        return QStringLiteral("文件");
    if (isSymlink())
        return QStringLiteral("符号链接");
    if (isCharDevice())
        return QStringLiteral("字符设备");
    if (isBlockDevice())
        return QStringLiteral("块设备");
    if (isFifo())
        return QStringLiteral("FIFO");
    return QStringLiteral("未知");
}

QString Entry::permissionText() const
{
    static const char* rwx = "rwx";
    QString out;
    for (int shift = 6; shift >= 0; shift -= 3) {
        for (int bit = 2; bit >= 0; --bit)
            out += (mode & (1u << (shift + bit))) ? QLatin1Char(rwx[2 - bit]) : QLatin1Char('-');
    }
    return out;
}

QString formatMode(quint32 mode)
{
    return QStringLiteral("0%1").arg(QString::number(mode & 0xFFFFu, 8).rightJustified(6, QLatin1Char('0')));
}

quint32 parseModeText(const QString& text, bool* ok)
{
    QString t = text.trimmed();
    if (t.startsWith(QStringLiteral("0o"), Qt::CaseInsensitive) ||
        t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        t = t.mid(2);
    // 统一按八进制理解：0755 / 755 / 0o755 均得到 0755。
    bool good = false;
    const quint32 value = t.toUInt(&good, 8);
    if (ok)
        *ok = good && !t.isEmpty();
    return good ? value : 0;
}

Archive parse(const QByteArray& data)
{
    Archive archive;
    qsizetype pos = 0;
    const qsizetype total = data.size();
    while (true) {
        if (pos + kMagicSize > total)
            fail(QStringLiteral("cpio 数据被截断（缺少结束标记）"));
        const QByteArray magic = data.mid(pos, kMagicSize);
        if (magic == "070701")
            archive.crcMode = false;
        else if (magic == "070702")
            archive.crcMode = true;
        else {
            if (pos == 0)
                fail(QStringLiteral("不是有效的 cpio newc 归档（magic=%1）")
                         .arg(QString::fromLatin1(magic.toHex())));
            fail(QStringLiteral("cpio 归档在中途出现非法 magic（偏移 %1）").arg(pos));
        }
        if (pos + kHeaderSize > total)
            fail(QStringLiteral("cpio 头部被截断（偏移 %1）").arg(pos));

        Entry entry;
        entry.ino = parseHexField(data, pos + 6);
        entry.mode = parseHexField(data, pos + 14);
        entry.uid = parseHexField(data, pos + 22);
        entry.gid = parseHexField(data, pos + 30);
        entry.nlink = parseHexField(data, pos + 38);
        entry.mtime = parseHexField(data, pos + 46);
        const quint32 fileSize = parseHexField(data, pos + 54);
        entry.devMajor = parseHexField(data, pos + 62);
        entry.devMinor = parseHexField(data, pos + 70);
        entry.rdevMajor = parseHexField(data, pos + 78);
        entry.rdevMinor = parseHexField(data, pos + 86);
        const quint32 nameSize = parseHexField(data, pos + 94);

        if (nameSize == 0)
            fail(QStringLiteral("cpio 记录名为空（偏移 %1）").arg(pos));
        const qsizetype nameOffset = pos + kNameOffset;
        if (nameOffset + qsizetype(nameSize) > total)
            fail(QStringLiteral("cpio 记录名被截断（偏移 %1）").arg(pos));
        QString name = QString::fromUtf8(data.mid(nameOffset, qsizetype(nameSize) - 1));
        const qsizetype nameEnd = pos + kHeaderSize + qsizetype(nameSize);
        pos = (nameEnd + 3) & ~qsizetype(3);

        if (name == QStringLiteral("TRAILER!!!"))
            break;

        if (fileSize > quint32(total) || pos + qsizetype(fileSize) > total)
            fail(QStringLiteral("cpio 文件数据被截断：%1（声明 %2 字节）").arg(name).arg(fileSize));
        entry.name = name;
        entry.data = data.mid(pos, qsizetype(fileSize));
        pos = (pos + qsizetype(fileSize) + 3) & ~qsizetype(3);
        archive.entries.append(entry);
    }
    return archive;
}

QByteArray serialize(const Archive& archive)
{
    QByteArray out;
    // 粗略估算容量，避免多次扩容。
    qsizetype estimate = 0;
    for (const auto& e : archive.entries)
        estimate += kHeaderSize + e.name.toUtf8().size() + 1 + e.data.size() + 8;
    out.reserve(estimate + kHeaderSize + 16);

    quint32 nextIno = 0;
    for (const auto& src : archive.entries) {
        Entry e = src;
        if (e.ino == 0)
            e.ino = ++nextIno;
        else if (e.ino > nextIno)
            nextIno = e.ino;
        const QByteArray nameBytes = e.name.toUtf8() + QByteArray(1, '\0');
        out.append(archive.crcMode ? "070702" : "070701");
        appendHexField(out, e.ino);
        appendHexField(out, e.mode);
        appendHexField(out, e.uid);
        appendHexField(out, e.gid);
        appendHexField(out, e.nlink);
        appendHexField(out, e.mtime);
        appendHexField(out, quint32(e.data.size()));
        appendHexField(out, e.devMajor);
        appendHexField(out, e.devMinor);
        appendHexField(out, e.rdevMajor);
        appendHexField(out, e.rdevMinor);
        appendHexField(out, quint32(nameBytes.size()));
        appendHexField(out, archive.crcMode ? gf::img::crc32(e.data) : 0);
        out.append(nameBytes);
        alignTo(out, 4);
        out.append(e.data);
        alignTo(out, 4);
    }

    // 结束记录 TRAILER!!!。
    const QByteArray trailerName = QByteArray("TRAILER!!!") + QByteArray(1, '\0');
    out.append(archive.crcMode ? "070702" : "070701");
    appendHexField(out, quint32(archive.entries.size() + 1));
    appendHexField(out, 0);
    appendHexField(out, 0);
    appendHexField(out, 0);
    appendHexField(out, 1);
    appendHexField(out, 0);
    appendHexField(out, 0);
    appendHexField(out, 0);
    appendHexField(out, 0);
    appendHexField(out, 0);
    appendHexField(out, 0);
    appendHexField(out, quint32(trailerName.size()));
    appendHexField(out, 0);
    out.append(trailerName);
    alignTo(out, 4);
    return out;
}

QByteArray firstArchiveRegion(const QByteArray& data)
{
    // 解析一遍以确认长度，随后裁剪到结束记录之后（含补齐）。
    Archive a = parse(data);
    QByteArray out = serialize(a);
    return out;
}

void extractTo(const Archive& archive, const QString& dir, const ProgressFn& progress,
               const CancelFn& cancel)
{
    ensureParentDirectory(gf::img::joinPath(dir, QStringLiteral("dummy")));
    const int count = int(archive.entries.size());
    int index = 0;
    for (const auto& e : archive.entries) {
        checkCancel(cancel);
        ++index;
        reportProgress(progress, count > 0 ? int(100.0 * index / count) : 100,
                       QStringLiteral("解包 %1").arg(e.name));
        const QString rel = safeName(e.name);
        if (rel.isEmpty())
            continue;
        const QString target = gf::img::joinPath(dir, rel);
        if (e.isDir()) {
            QDir().mkpath(target);
            continue;
        }
        // 不信任归档中的绝对路径 / 上跳。
        if (rel.contains(QStringLiteral("..")))
            fail(QStringLiteral("cpio 记录路径非法：%1").arg(e.name));
        ensureParentDirectory(target);
        if (e.isSymlink()) {
            // Windows 上无符号链接权限：写成 <名字>.symlink.txt 说明文件。
            writeFileOrThrow(target + QStringLiteral(".symlink.txt"), e.data);
            continue;
        }
        writeFileOrThrow(target, e.data);
    }
}

Archive fromDirectory(const QString& dir, const QString& rootNameInArchive)
{
    Archive archive;
    QDir root(dir);
    if (!root.exists())
        fail(QStringLiteral("目录不存在：%1").arg(dir));

    quint32 ino = 1;
    const QDateTime now = QDateTime::currentDateTime();
    const quint32 mtime = quint32(now.toSecsSinceEpoch());

    auto addDir = [&](const QString& rel) {
        Entry e;
        e.name = rel;
        e.ino = ino++;
        e.mode = 0040755u;
        e.mtime = mtime;
        archive.entries.append(e);
    };

    const QString prefix = rootNameInArchive.isEmpty() ? QString() : safeName(rootNameInArchive);
    if (!prefix.isEmpty())
        addDir(prefix);

    QStringList relativeDirs;
    QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QString rel = root.relativeFilePath(path);
        rel = safeName(rel);
        if (!prefix.isEmpty())
            rel = prefix + QLatin1Char('/') + rel;
        relativeDirs.append(rel);
    }
    relativeDirs.sort();
    for (const QString& rel : relativeDirs)
        addDir(rel);

    QDirIterator fit(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    QStringList files;
    while (fit.hasNext())
        files.append(fit.next());
    files.sort();
    for (const QString& path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            fail(QStringLiteral("无法读取文件：%1").arg(path));
        Entry e;
        QString rel = safeName(root.relativeFilePath(path));
        if (!prefix.isEmpty())
            rel = prefix + QLatin1Char('/') + rel;
        e.name = rel;
        e.ino = ino++;
        e.mode = 0100644u;
        e.mtime = mtime;
        e.data = f.readAll();
        archive.entries.append(e);
    }
    return archive;
}

void setPermissions(Archive& archive, const QString& name, quint32 mode, quint32 uid, quint32 gid,
                    bool recursive)
{
    const QString want = safeName(name);
    bool matched = false;
    for (auto& e : archive.entries) {
        const QString entryName = safeName(e.name);
        const bool hit = entryName == want ||
                         (recursive && (entryName.startsWith(want + QLatin1Char('/'))));
        if (!hit)
            continue;
        matched = true;
        if (mode != 0)
            e.mode = (e.mode & 0170000u) | (mode & 07777u);
        if (uid != 0)
            e.uid = uid;
        if (gid != 0)
            e.gid = gid;
    }
    if (!matched)
        fail(QStringLiteral("归档中未找到条目：%1").arg(name));
}

void putFile(Archive& archive, const QString& name, const QByteArray& data, quint32 mode)
{
    const QString want = safeName(name);
    const bool isDir = (mode & 0170000u) == 0040000u;
    for (auto& e : archive.entries) {
        if (safeName(e.name) == want) {
            e.data = data;
            if (mode != 0)
                e.mode = (e.mode & 0170000u) | (mode & 07777u);
            if (isDir)
                e.mode = 0040755u | (mode & 07777u);
            return;
        }
    }
    quint32 nextIno = 1;
    for (const auto& e : archive.entries)
        nextIno = qMax(nextIno, e.ino + 1);
    Entry e;
    e.name = want;
    e.ino = nextIno;
    e.mode = isDir ? (0040755u | (mode & 07777u)) : ((mode & 0170000u) == 0 ? (0100644u | (mode & 07777u)) : mode);
    e.mtime = quint32(QDateTime::currentDateTime().toSecsSinceEpoch());
    e.data = data;
    archive.entries.append(e);
}

bool removeEntry(Archive& archive, const QString& name, bool recursive)
{
    const QString want = safeName(name);
    bool removed = false;
    for (qsizetype i = archive.entries.size() - 1; i >= 0; --i) {
        const QString entryName = safeName(archive.entries.at(i).name);
        if (entryName == want || (recursive && entryName.startsWith(want + QLatin1Char('/')))) {
            archive.entries.removeAt(i);
            removed = true;
        }
    }
    return removed;
}

} // namespace gf::img::cpio
