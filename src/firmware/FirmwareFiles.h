#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "FirmwareCommon.h"

namespace gf::firmware {

/// 固件容器/镜像类型。
enum class FirmwareKind {
    Unknown,
    Zip,
    TarGz,
    Tar,
    Pac,
    RawImage,   // .img / .bin（内部可能是 sparse/super/payload/boot 等）
    Ozip,       // OPPO 加密 ozip（暂不解包）
    Ofp,        // OPPO OFP（暂不解包）
    Kdz,        // LG KDZ
    SevenZip,
    Rar,
    Gzip,
    Lz4,
    Lzop,
    Elf,
    Md5File,
    Sha256File,
};

QString kindText(FirmwareKind kind);
/// 该类型是否支持内置自动解包。
bool kindIsUnpackable(FirmwareKind kind);

struct FirmwareFileInfo {
    QString path;
    QString fileName;
    qint64 sizeBytes = 0;
    FirmwareKind kind = FirmwareKind::Unknown;
    QString kindText;
    QStringList hints;      // 由 magic 得到的内部格式线索
    QString hashSidecar;    // 同目录下的 .md5/.sha256 文件（若存在）
    bool recognized = false;
};

/// 识别固件类型（读取头部若干字节；head 可传入已读数据用于测试）。
FirmwareFileInfo identifyFirmware(const QString& path, const QByteArray& head = {});

struct UnpackResult {
    bool ok = false;
    QString error;
    QString outputDir;
    QString kindText;
    int filesExtracted = 0;
    int dirsCreated = 0;
    qint64 bytesWritten = 0;
    QStringList extracted; // 已提取的相对路径（最多 200 条）
    QStringList notes;
};

/// 自动解包：zip / tar.gz / tar / pac。其它类型返回明确的错误提示。
UnpackResult unpackFirmware(const QString& path, const QString& outputDir,
                            const ProgressFn& progress = {}, const CancelFn& cancel = {});

struct MergeResult {
    bool ok = false;
    QString error;
    QString outputPath;
    qint64 bytes = 0;
    QVector<QString> parts;
};

/// 查找分卷文件（foo.z01…foo.zNN + foo.zip / foo.001…），按正确顺序返回。
QVector<QString> findSplitParts(const QString& anyPartPath);
/// 合并分卷到 outputPath。
MergeResult mergeSplitFirmware(const QString& anyPartPath, const QString& outputPath,
                               const ProgressFn& progress = {}, const CancelFn& cancel = {});

} // namespace gf::firmware
