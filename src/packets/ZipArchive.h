#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

namespace gf::packets {

/// Minimal read-only ZIP archive backed by miniz. Used for GeekFlashPacket
/// containers (resource packs are distributed as .zip or as extracted folders).
class ZipArchive {
public:
    ZipArchive();
    ~ZipArchive();
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    bool open(const QString& path);
    void close();
    bool isOpen() const;

    QStringList entryNames() const;
    bool contains(const QString& entry) const;
    /// Reads one entry into memory.
    QByteArray read(const QString& entry) const;
    /// Extracts one entry to disk (creates parent directories).
    bool extractTo(const QString& entry, const QString& destination) const;
    qint64 entrySize(const QString& entry) const;

    QString errorString() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace gf::packets
