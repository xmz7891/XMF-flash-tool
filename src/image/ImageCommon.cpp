#include "ImageCommon.h"

#include "core/Util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <array>
#include <cstdio>
#include <cstring>

namespace gf::img {

void fail(const QString& message)
{
    throw std::runtime_error(message.toStdString());
}

QString formatBytes(quint64 bytes)
{
    return gf::util::formatBytes(qint64(bytes));
}

quint64 parseNumber(const QString& text, bool* ok)
{
    QString t = text.trimmed();
    if (ok)
        *ok = false;
    if (t.isEmpty())
        return 0;

    // 0x / 0X 前缀按十六进制解析。
    if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        bool good = false;
        const quint64 value = t.mid(2).remove(QLatin1Char('_')).remove(QLatin1Char(' ')).toULongLong(&good, 16);
        if (ok)
            *ok = good;
        return good ? value : 0;
    }

    // 先尝试带单位后缀的十进制/浮点写法。
    quint64 size = 0;
    if (gf::util::parseSize(t, &size)) {
        // parseSize 也会接受纯十进制，这里优先按十进制整数处理以保持精确。
        bool plain = false;
        const quint64 direct = t.toULongLong(&plain, 10);
        if (ok)
            *ok = true;
        return plain ? direct : size;
    }
    // 再尝试纯十六进制（无 0x 前缀）只有在含 a-f 时才有意义，交给调用方显式指定。
    return 0;
}

QString numberToHex(quint64 value, int width, bool withPrefix)
{
    QString digits = QString::number(value, 16).toUpper();
    if (width > 0 && digits.size() < width)
        digits = digits.rightJustified(width, QLatin1Char('0'));
    return withPrefix ? QStringLiteral("0x") + digits : digits;
}

bool parseHexValue(const QString& text, quint64* out)
{
    return gf::util::parseHex(text, out);
}

QByteArray readFileOrThrow(const QString& path)
{
    QFile f(path);
    if (!f.exists())
        fail(QStringLiteral("文件不存在：%1").arg(path));
    if (!f.open(QIODevice::ReadOnly))
        fail(QStringLiteral("无法打开文件：%1（%2）").arg(path, f.errorString()));
    const QByteArray data = f.readAll();
    if (f.error() != QFile::NoError)
        fail(QStringLiteral("读取文件失败：%1（%2）").arg(path, f.errorString()));
    return data;
}

void writeFileOrThrow(const QString& path, const QByteArray& data)
{
    ensureParentDirectory(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        fail(QStringLiteral("无法写入文件：%1（%2）").arg(path, f.errorString()));
    if (f.write(data) != data.size())
        fail(QStringLiteral("写入文件失败：%1（%2）").arg(path, f.errorString()));
}

void ensureParentDirectory(const QString& path)
{
    const QString dir = QFileInfo(path).absolutePath();
    if (dir.isEmpty())
        return;
    if (!QDir().mkpath(dir))
        fail(QStringLiteral("无法创建目录：%1").arg(dir));
}

QString joinPath(const QString& dir, const QString& name)
{
    if (dir.isEmpty())
        return name;
    QString base = dir;
    while (base.endsWith(QLatin1Char('/')) || base.endsWith(QLatin1Char('\\')))
        base.chop(1);
    return base + QLatin1Char('/') + name;
}

QString uniqueOutputPath(const QString& path)
{
    return gf::util::uniquePath(path);
}

void alignTo(QByteArray& out, int alignment, char pad)
{
    if (alignment <= 1)
        return;
    const qsizetype rem = out.size() % alignment;
    if (rem == 0)
        return;
    const qsizetype need = alignment - rem;
    out.append(QByteArray(need, pad));
}

void padTo4(QByteArray& out)
{
    alignTo(out, 4);
}

bool isZero(const QByteArray& data)
{
    return isZeroRange(data, 0, data.size());
}

bool isZeroRange(const QByteArray& data, qsizetype offset, qsizetype length)
{
    if (offset < 0 || length <= 0 || offset > data.size())
        return true;
    const qsizetype end = qMin<qsizetype>(data.size(), offset + length);
    const char* p = data.constData() + offset;
    for (qsizetype i = 0; i < end - offset; ++i) {
        if (p[i] != '\0')
            return false;
    }
    return true;
}

namespace {

const std::array<quint32, 256>& crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

} // namespace

quint32 crc32Init()
{
    return 0xFFFFFFFFu;
}

quint32 crc32Update(quint32 state, const void* data, qsizetype length)
{
    const auto& table = crcTable();
    const auto* p = static_cast<const quint8*>(data);
    quint32 c = state;
    for (qsizetype i = 0; i < length; ++i)
        c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c;
}

quint32 crc32Final(quint32 state)
{
    return state ^ 0xFFFFFFFFu;
}

quint32 crc32(const void* data, qsizetype length)
{
    return crc32Final(crc32Update(crc32Init(), data, length));
}

quint32 crc32(const QByteArray& data)
{
    return crc32(data.constData(), data.size());
}

quint32 adler32Update(quint32 state, const void* data, qsizetype length)
{
    const auto* p = static_cast<const quint8*>(data);
    quint32 a = state & 0xFFFFu;
    quint32 b = (state >> 16) & 0xFFFFu;
    constexpr quint32 kMod = 65521u;
    qsizetype i = 0;
    while (i < length) {
        const qsizetype chunk = qMin<qsizetype>(length - i, 5552);
        for (qsizetype k = 0; k < chunk; ++k) {
            a += p[i + k];
            b += a;
        }
        a %= kMod;
        b %= kMod;
        i += chunk;
    }
    return (b << 16) | a;
}

quint32 adler32(const QByteArray& data)
{
    return adler32Update(1, data.constData(), data.size());
}

QString hexDump(const QByteArray& data, qsizetype offset, int maxBytes)
{
    if (maxBytes < 0)
        maxBytes = 0;
    const qsizetype begin = qBound<qsizetype>(0, offset, data.size());
    const qsizetype end = qMin<qsizetype>(data.size(), begin + maxBytes);
    QString out;
    for (qsizetype row = begin; row < end; row += 16) {
        out += QStringLiteral("%1  ").arg(row, 8, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("0X"), QStringLiteral("0x"));
        QString ascii;
        for (int i = 0; i < 16; ++i) {
            const qsizetype pos = row + i;
            if (pos < end) {
                const quint8 b = quint8(data.at(pos));
                out += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                ascii += (b >= 0x20 && b < 0x7F) ? QChar(QLatin1Char(char(b))) : QLatin1Char('.');
            } else {
                out += QStringLiteral("   ");
                ascii += QLatin1Char(' ');
            }
        }
        out += QStringLiteral(" | ") + ascii + QLatin1Char('\n');
    }
    return out;
}

QString guidString(const QByteArray& raw)
{
    if (raw.size() < 16)
        return {};
    auto le16 = [](const QByteArray& d, int at) -> quint16 {
        return quint16(quint8(d.at(at)) | (quint8(d.at(at + 1)) << 8));
    };
    auto le32 = [](const QByteArray& d, int at) -> quint32 {
        return quint32(quint8(d.at(at))) | (quint32(quint8(d.at(at + 1))) << 8) |
               (quint32(quint8(d.at(at + 2))) << 16) | (quint32(quint8(d.at(at + 3))) << 24);
    };
    const quint32 g1 = le32(raw, 0);
    const quint16 g2 = le16(raw, 4);
    const quint16 g3 = le16(raw, 6);
    QString out = QStringLiteral("%1-%2-%3-%4-")
                      .arg(g1, 8, 16, QLatin1Char('0'))
                      .arg(g2, 4, 16, QLatin1Char('0'))
                      .arg(g3, 4, 16, QLatin1Char('0'))
                      .arg(QString::fromLatin1(raw.mid(8, 2).toHex()));
    out += QString::fromLatin1(raw.mid(10, 6).toHex());
    return out.toUpper();
}

QString utf16LeToString(const QByteArray& raw)
{
    QString out;
    out.reserve(raw.size() / 2);
    for (qsizetype i = 0; i + 1 < raw.size(); i += 2) {
        const char16_t ch = char16_t(quint8(raw.at(i)) | (quint8(raw.at(i + 1)) << 8));
        if (ch == 0)
            break;
        out += QChar(ch);
    }
    return out;
}

QByteArray stringToUtf16Le(const QString& text, qsizetype maxChars)
{
    QByteArray out;
    const qsizetype count = maxChars >= 0 ? qMin<qsizetype>(text.size(), maxChars) : text.size();
    out.reserve(count * 2);
    for (qsizetype i = 0; i < count; ++i) {
        const char16_t ch = text.at(i).unicode();
        out.append(char(ch & 0xFF));
        out.append(char((ch >> 8) & 0xFF));
    }
    return out;
}

void reportProgress(const ProgressFn& fn, int percent, const QString& status)
{
    if (fn)
        fn(percent, status);
}

void checkCancel(const CancelFn& fn)
{
    if (fn)
        fn();
}

FileReader::FileReader(const QString& path)
    : m_path(path)
{
    auto* file = new QFile(path);
    if (!file->exists()) {
        delete file;
        fail(QStringLiteral("文件不存在：%1").arg(path));
    }
    if (!file->open(QIODevice::ReadOnly)) {
        const QString reason = file->errorString();
        delete file;
        fail(QStringLiteral("无法打开文件：%1（%2）").arg(path, reason));
    }
    m_file = file;
    m_size = file->size();
}

FileReader::~FileReader()
{
    delete static_cast<QFile*>(m_file);
}

QByteArray FileReader::readAt(qint64 offset, qint64 length)
{
    auto* file = static_cast<QFile*>(m_file);
    if (length <= 0)
        return {};
    if (offset < 0 || offset >= m_size)
        fail(QStringLiteral("读取越界：偏移 %1 超出文件大小 %2（%3）").arg(offset).arg(m_size).arg(m_path));
    if (offset + length > m_size)
        fail(QStringLiteral("读取越界：偏移 %1 + 长度 %2 超出文件大小 %3（%4）")
                 .arg(offset)
                 .arg(length)
                 .arg(m_size)
                 .arg(m_path));
    if (!file->seek(offset))
        fail(QStringLiteral("定位文件失败：%1").arg(m_path));
    QByteArray data = file->read(length);
    if (data.size() != length)
        fail(QStringLiteral("读取文件失败：%1（偏移 %2，期望 %3，实际 %4）")
                 .arg(m_path)
                 .arg(offset)
                 .arg(length)
                 .arg(data.size()));
    return data;
}

void FileReader::readInto(char* buffer, qint64 offset, qint64 length)
{
    const QByteArray data = readAt(offset, length);
    std::memcpy(buffer, data.constData(), size_t(data.size()));
}

QByteArray FileReader::readAll()
{
    return readAt(0, m_size);
}

FileWriter::FileWriter(const QString& path, bool truncate)
    : m_path(path)
{
    ensureParentDirectory(path);
    auto* file = new QFile(path);
    QIODevice::OpenMode mode = QIODevice::ReadWrite;
    if (truncate)
        mode |= QIODevice::Truncate;
    if (!file->open(mode)) {
        const QString reason = file->errorString();
        delete file;
        fail(QStringLiteral("无法写入文件：%1（%2）").arg(path, reason));
    }
    m_file = file;
}

FileWriter::~FileWriter()
{
    delete static_cast<QFile*>(m_file);
}

void FileWriter::write(const QByteArray& data)
{
    auto* file = static_cast<QFile*>(m_file);
    if (file->write(data) != data.size())
        fail(QStringLiteral("写入文件失败：%1（%2）").arg(m_path, file->errorString()));
}

void FileWriter::writeAt(qint64 offset, const QByteArray& data)
{
    auto* file = static_cast<QFile*>(m_file);
    if (!file->seek(offset))
        fail(QStringLiteral("定位文件失败：%1").arg(m_path));
    write(data);
}

void FileWriter::resize(qint64 size)
{
    auto* file = static_cast<QFile*>(m_file);
    if (!file->resize(size))
        fail(QStringLiteral("调整文件大小失败：%1（%2）").arg(m_path, file->errorString()));
}

qint64 FileWriter::size() const
{
    return static_cast<QFile*>(m_file)->size();
}

void FileWriter::close()
{
    static_cast<QFile*>(m_file)->close();
}

} // namespace gf::img
