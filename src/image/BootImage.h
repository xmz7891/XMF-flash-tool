#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::boot {

enum class ImageKind {
    Boot,      // Android boot.img（v0-v4）
    InitBoot,  // init_boot.img（v4，无 kernel）
    VendorBoot // vendor_boot.img（v3/v4）
};

struct VendorFragment {
    quint32 type = 1; // 1 = platform, 2 = recovery, 3 = dlkm
    QString name;
    QByteArray data;
};

struct Image {
    ImageKind kind = ImageKind::Boot;
    quint32 headerVersion = 0;
    quint32 pageSize = 2048;
    quint32 headerSize = 0;
    quint32 osVersion = 0;
    quint64 kernelAddr = 0;
    quint64 ramdiskAddr = 0;
    quint64 secondAddr = 0;
    quint64 tagsAddr = 0;
    quint64 dtbAddr = 0;
    QString boardName;
    QString cmdline;      // v0-v2: cmdline + extra_cmdline；v3/v4: 完整 cmdline
    QString extraCmdline; // 仅 v0-v2 使用
    QByteArray kernel;
    QByteArray ramdisk;
    QByteArray second;
    QByteArray dtb;
    QByteArray recoveryDtbo;
    quint32 recoveryDtboOffset = 0;
    quint32 signatureSize = 0;
    QByteArray signature; // v4 boot signature
    // vendor_boot
    QVector<VendorFragment> vendorRamdisks;
    quint32 vendorRamdiskTableEntrySize = 108;
    QByteArray bootconfig;

    QString kindText() const;
    QString versionText() const;
    quint64 totalSize() const;
};

bool looksLikeBootImage(const QByteArray& data);
Image parse(const QByteArray& data);
QByteArray serialize(const Image& image);
/// v0-v2 的 header id（SHA1 前 8 字节）。
QByteArray computeId(const Image& image);

bool isCpio(const QByteArray& data);
/// 解压 ramdisk（gzip/lzop 等）；formatName 返回识别到的压缩格式。
QByteArray decompressRamdisk(const QByteArray& ramdisk, QString* formatName = nullptr);
/// 生成 gzip 压缩的 ramdisk。
QByteArray compressRamdisk(const QByteArray& ramdisk);

} // namespace gf::img::boot
