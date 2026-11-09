#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "Lz4Codec.h"

namespace gf::extras::root {

/// 注入 ramdisk 的文件描述。
struct InjectFile {
    QString path;                 // 归档内路径，例如 "overlay.d/sbin/ksud"
    QByteArray data;
    quint32 mode = 0100644u;      // 默认普通文件 rw-r--r--
};

/// boot/init_boot ramdisk 修补参数。
struct PatchOptions {
    QByteArray payload;                       // 作为 /init 注入的内容（如 magiskinit）
    QString payloadLabel = QStringLiteral("magiskinit");
    bool backupInit = true;                   // 原 init 备份到 .backup/init
    QByteArray magiskConfig;                  // 非空时写入 .backup/.magisk
    QList<InjectFile> extraFiles;             // 额外注入文件
};

struct PatchOutcome {
    QByteArray patchedRamdisk;                // 修补后的 cpio（未压缩）
    QByteArray patchedBoot;                   // 修补后的完整 boot 镜像
    QString ramdiskFormat;                    // "gzip" / "lzo" / "lz4" / "cpio"
    lz4::Frame lz4Frame = lz4::Frame::Unknown;
    QStringList changes;
    QString initSha1;                         // 原 init 的 SHA1
};

/// 纯 ramdisk 修补：解析 cpio、备份并替换 init、注入额外文件，返回新 cpio。
/// 损坏/缺少必要文件时抛 std::runtime_error（中文）。
QByteArray patchRamdisk(const QByteArray& plainCpio, const PatchOptions& options,
                        PatchOutcome* outcome = nullptr);

/// 完整 boot/init_boot 镜像修补：解压 ramdisk（gzip/lzop/lz4/裸 cpio）、注入、按原格式重打包。
QByteArray patchBootImage(const QByteArray& bootData, const PatchOptions& options,
                          PatchOutcome* outcome = nullptr);

/// 修改 ramdisk 中属性文件（default.prop / prop.default 等）的键值，返回新 cpio。
QByteArray applyPropertyOverrides(const QByteArray& plainCpio, const QStringList& targetFiles,
                                  const QMap<QString, QString>& overrides,
                                  QStringList* changes = nullptr, QString* error = nullptr);

QMap<QString, QString> parsePropLines(const QString& text);
QString serializePropLines(const QMap<QString, QString>& values,
                           const QStringList& order = QStringList());

/// 自动识别并解压 ramdisk；formatName 返回 "gzip"/"lzo"/"lz4"/"cpio"。
QByteArray decompressRamdiskAuto(const QByteArray& ramdisk, QString* format = nullptr,
                                 lz4::Frame* frame = nullptr);
/// 用与 format 对应的方式压缩 ramdisk（lz4 会保留原帧类型）。
QByteArray compressRamdiskLike(const QByteArray& plain, const QString& format,
                               lz4::Frame frame = lz4::Frame::Unknown);

} // namespace gf::extras::root
