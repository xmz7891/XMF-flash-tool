#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::payload {

/// ChromeOS update_engine 负载（payload.bin）的操作类型。
enum class OperationType {
    Replace = 0,
    ReplaceBz = 1,
    Move = 2,
    Bsdiff = 3,
    SourceCopy = 4,
    SourceBsdiff = 5,
    Zero = 6,
    Discard = 7,
    ReplaceXz = 8,
    Puffdiff = 9,
    BrotliBsdiff = 10,
    Lz4diffBsdiff = 11,
    Lz4diff = 12,
    Zucchini = 13,
};

QString operationTypeText(OperationType type);

struct Extent {
    quint64 startBlock = 0;
    quint64 numBlocks = 0;
    quint64 byteOffset(quint32 blockSize) const { return startBlock * blockSize; }
    quint64 byteLength(quint32 blockSize) const { return numBlocks * blockSize; }
};

struct Operation {
    OperationType type = OperationType::Replace;
    quint64 dataOffset = 0;
    quint64 dataLength = 0;
    QVector<Extent> srcExtents;
    QVector<Extent> dstExtents;
    quint64 srcLength = 0;
    quint64 dstLength = 0;
    QByteArray dataSha256;
    QByteArray srcSha256;
};

struct PartitionInfo {
    quint64 size = 0;
    QByteArray hash;
};

struct PartitionUpdate {
    QString name;
    QVector<Operation> operations;
    PartitionInfo newInfo;
    QString filesystemType;
    bool runPostinstall = false;
};

struct Manifest {
    quint32 blockSize = 4096;
    quint64 signaturesOffset = 0;
    quint64 signaturesSize = 0;
    quint32 minorVersion = 0;
    quint64 maxTimestamp = 0;
    bool partialUpdate = false;
    QVector<PartitionUpdate> partitions;
};

struct Header {
    QString magic;
    quint64 version = 0;
    quint64 manifestSize = 0;
    quint32 metadataSignatureSize = 0;
    qint64 manifestOffset = 0;
    qint64 dataOffset = 0;
};

struct Payload {
    Header header;
    Manifest manifest;
};

/// 读取 payload.bin 头与 manifest（不读取数据块）。
Payload parseFile(const QString& path);
/// 解析内存中的 payload 头 + manifest。
Payload parse(const QByteArray& data);
/// manifest 序列化（供测试与打包用）。
QByteArray encodeManifest(const Manifest& manifest);
Manifest decodeManifest(const QByteArray& data);

/// 导出所有（或指定）分区镜像到 outDir。
void extractAll(const QString& payloadPath, const Payload& payload, const QString& outDir,
                const QStringList& onlyPartitions, bool verifyHashes,
                const ProgressFn& progress = {}, const CancelFn& cancel = {});

/// 把目录中的分区镜像打包成 payload.bin（REPLACE 操作，不签名）。
void packDirectory(const QString& sourceDir, const QStringList& partitionNames,
                   const QString& outputPath, quint32 blockSize, const ProgressFn& progress = {},
                   const CancelFn& cancel = {});

/// 支持的提取/打包操作类型描述（用于 UI 提示）。
QString supportSummary();

} // namespace gf::img::payload
