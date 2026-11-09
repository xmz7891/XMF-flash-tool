#include "ZipArchive.h"

#include "third_party/miniz.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace gf::packets {

struct ZipArchive::Impl {
    mz_zip_archive zip{};
    bool open = false;
    QString error;
};

ZipArchive::ZipArchive()
    : d(std::make_unique<Impl>())
{
}

ZipArchive::~ZipArchive()
{
    close();
}

bool ZipArchive::open(const QString& path)
{
    close();
    QFileInfo info(path);
    if (!info.exists()) {
        d->error = QStringLiteral("文件不存在: %1").arg(path);
        return false;
    }
    const QByteArray nativePath = QDir::toNativeSeparators(path).toUtf8();
    if (!mz_zip_reader_init_file(&d->zip, nativePath.constData(), 0)) {
        d->error = QStringLiteral("无法打开 zip 归档: %1").arg(path);
        return false;
    }
    d->open = true;
    return true;
}

void ZipArchive::close()
{
    if (d->open) {
        mz_zip_reader_end(&d->zip);
        d->open = false;
        d->zip = mz_zip_archive{};
    }
}

bool ZipArchive::isOpen() const
{
    return d->open;
}

QStringList ZipArchive::entryNames() const
{
    QStringList names;
    if (!d->open)
        return names;
    const mz_uint count = mz_zip_reader_get_num_files(&d->zip);
    names.reserve(int(count));
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&d->zip, i, &stat))
            continue;
        names.append(QString::fromUtf8(stat.m_filename));
    }
    return names;
}

bool ZipArchive::contains(const QString& entry) const
{
    if (!d->open)
        return false;
    return mz_zip_reader_locate_file(&d->zip, entry.toUtf8().constData(), nullptr, 0) >= 0;
}

qint64 ZipArchive::entrySize(const QString& entry) const
{
    if (!d->open)
        return -1;
    mz_zip_archive_file_stat stat;
    const int index = mz_zip_reader_locate_file(&d->zip, entry.toUtf8().constData(), nullptr, 0);
    if (index < 0 || !mz_zip_reader_file_stat(&d->zip, mz_uint(index), &stat))
        return -1;
    return qint64(stat.m_uncomp_size);
}

QByteArray ZipArchive::read(const QString& entry) const
{
    if (!d->open)
        return {};
    const int index = mz_zip_reader_locate_file(&d->zip, entry.toUtf8().constData(), nullptr, 0);
    if (index < 0)
        return {};
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&d->zip, mz_uint(index), &stat))
        return {};
    QByteArray data(int(stat.m_uncomp_size), Qt::Uninitialized);
    if (!mz_zip_reader_extract_to_mem(&d->zip, mz_uint(index), data.data(), size_t(data.size()), 0))
        return {};
    return data;
}

bool ZipArchive::extractTo(const QString& entry, const QString& destination) const
{
    if (!d->open)
        return false;
    const int index = mz_zip_reader_locate_file(&d->zip, entry.toUtf8().constData(), nullptr, 0);
    if (index < 0)
        return false;
    QDir().mkpath(QFileInfo(destination).absolutePath());
    const QByteArray nativePath = QDir::toNativeSeparators(destination).toUtf8();
    return mz_zip_reader_extract_to_file(&d->zip, mz_uint(index), nativePath.constData(), 0) != 0;
}

QString ZipArchive::errorString() const
{
    return d->error;
}

} // namespace gf::packets
