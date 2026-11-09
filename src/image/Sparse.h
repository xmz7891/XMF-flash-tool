#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::sparse {

/// Android sparse image（libsparse）chunk 类型。
enum class ChunkType : quint16 {
    Raw = 0xCAC1,
    Fill = 0xCAC2,
    DontCare = 0xCAC3,
    Crc32 = 0xCAC4,
};

struct Chunk {
    ChunkType type = ChunkType::Raw;
    quint32 blockCount = 0;
    QByteArray data; // raw: 原始数据；fill/crc32: 4 字节；dontcare: 空

    QString typeText() const;
};

struct Image {
    quint32 blockSize = 4096;
    quint32 totalBlocks = 0;
    quint32 imageChecksum = 0;
    quint32 majorVersion = 1;
    quint32 minorVersion = 0;
    QVector<Chunk> chunks;

    quint64 rawSize() const { return quint64(totalBlocks) * blockSize; }
};

struct FileInfo {
    quint32 blockSize = 0;
    quint32 totalBlocks = 0;
    quint32 totalChunks = 0;
    quint32 imageChecksum = 0;
    quint64 rawSize = 0;
    qint64 fileSize = 0;
};

bool isSparse(const QByteArray& data);
Image parse(const QByteArray& data);
QByteArray serialize(const Image& image);

QByteArray toRaw(const Image& image);
Image fromRaw(const QByteArray& raw, quint32 blockSize = 4096, bool detectFill = true,
              bool detectDontCare = true);

/// 只读文件头 + chunk 头，用于大镜像信息展示。
FileInfo inspectFile(const QString& path);
/// 大镜像流式解包（sparse -> raw）。
void unpackFile(const QString& sparsePath, const QString& rawPath, const ProgressFn& progress = {},
                const CancelFn& cancel = {});
/// 大镜像流式打包（raw -> sparse）。
void packFile(const QString& rawPath, const QString& sparsePath, quint32 blockSize = 4096,
              bool detectFill = true, bool detectDontCare = true, const ProgressFn& progress = {},
              const CancelFn& cancel = {});

QString chunkTypeText(ChunkType type);

} // namespace gf::img::sparse
