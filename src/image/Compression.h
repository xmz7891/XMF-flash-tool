#pragma once

#include <QByteArray>
#include <QString>

#include "ImageCommon.h"

namespace gf::img::compress {

enum class Format { Unknown, Gzip, Lzop, Lz4, Lzma, Bzip2, Zip };

Format detect(const QByteArray& data);
QString formatName(Format format);

bool isGzip(const QByteArray& data);
bool isLzop(const QByteArray& data);

// --- gzip / deflate -------------------------------------------------------
/// 生成标准 gzip 文件（deflate 压缩，mtime=0）。
QByteArray gzipCompress(const QByteArray& data, int level = 6);
/// 解压标准 gzip 文件，校验 CRC32 与长度。
QByteArray gzipDecompress(const QByteArray& data);
/// 纯 deflate 流（raw，无 zlib/gzip 包装）解压。
QByteArray inflateRaw(const QByteArray& data);
/// 纯 deflate 流压缩（借助 Qt 的 zlib 封装并剥离包装，返回值可直接塞进 gzip）。
QByteArray deflateRaw(const QByteArray& data, int level = 6);

// --- LZO1X / lzop ---------------------------------------------------------
/// LZO1X 解压（纯压缩块，不含 lzop 容器）。expectedSize<0 时不限长。
QByteArray lzo1xDecompress(const QByteArray& data, qsizetype expectedSize = -1);
/// LZO1X 压缩：仅输出字面量编码（体积不减小，但格式合法）。
QByteArray lzo1xCompress(const QByteArray& data);
/// 生成 lzop 容器（44 字节头 + 数据块）。
QByteArray lzopCompress(const QByteArray& data, const QString& fileName = {});
/// 解压 lzop 容器。
QByteArray lzopDecompress(const QByteArray& data);

} // namespace gf::img::compress
