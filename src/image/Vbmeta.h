#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ImageCommon.h"

namespace gf::img::vbmeta {

constexpr quint32 kFlagHashtreeDisabled = 1;
constexpr quint32 kFlagVerificationDisabled = 2;

enum class DescriptorTag {
    Property = 0,
    Hashtree = 1,
    Hash = 2,
    KernelCmdline = 3,
    ChainPartition = 4,
};

struct Descriptor {
    quint32 tag = 0;
    quint64 rawTagWord = 0;
    qint64 offset = 0;       // 相对 vbmeta 结构起点
    quint64 numBytesFollowing = 0;
    QString tagName;

    // property
    QString propertyKey;
    QString propertyValue;
    // hashtree
    quint32 dmVerityVersion = 0;
    quint64 imageSize = 0;
    quint64 treeOffset = 0;
    quint64 treeSize = 0;
    quint32 dataBlockSize = 0;
    quint32 hashBlockSize = 0;
    quint32 fecNumRoots = 0;
    quint64 fecOffset = 0;
    quint64 fecSize = 0;
    QString hashAlgorithm;
    QByteArray rootDigest;
    QByteArray salt;
    // hash
    QByteArray digest;
    // kernel cmdline
    quint32 cmdlineFlags = 0;
    QString kernelCmdline;
    // chain partition
    quint32 rollbackIndexLocation = 0;
    QString chainPartitionName;
    QByteArray chainPublicKey;
};

struct Vbmeta {
    quint32 requiredMajor = 0;
    quint32 requiredMinor = 0;
    quint64 authenticationDataBlockSize = 0;
    quint64 auxiliaryDataBlockSize = 0;
    quint32 algorithmType = 0;
    quint64 hashOffset = 0;
    quint64 hashSize = 0;
    quint64 signatureOffset = 0;
    quint64 signatureSize = 0;
    quint64 publicKeyOffset = 0;
    quint64 publicKeySize = 0;
    quint64 publicKeyMetadataOffset = 0;
    quint64 publicKeyMetadataSize = 0;
    quint64 descriptorsOffset = 0;
    quint64 descriptorsSize = 0;
    quint64 rollbackIndex = 0;
    quint32 flags = 0;
    quint32 rollbackIndexLocation = 0;
    QString releaseString;
    QVector<Descriptor> descriptors;
    QByteArray hash;
    QByteArray signature;
    QByteArray publicKey;
    QByteArray raw;

    QString algorithmText() const;
    QString flagsText() const;
    bool hashtreeDisabled() const { return (flags & kFlagHashtreeDisabled) != 0; }
    bool verificationDisabled() const { return (flags & kFlagVerificationDisabled) != 0; }
};

bool looksLikeVbmeta(const QByteArray& data);
Vbmeta parse(const QByteArray& data);
QStringList describe(const Vbmeta& vbmeta);
QString descriptorText(const Descriptor& descriptor);

/// 修改 flags；clearAuthentication 为 true 时把签名块清零（需要外部重新签名）。
/// 返回值：新的 vbmeta 数据。
QByteArray patchFlags(const QByteArray& data, quint32 newFlags, bool clearAuthentication);

struct VerifyResult {
    bool ok = false;
    QString message;
    QByteArray computedRoot;
    QByteArray expectedRoot;
    quint64 treeSize = 0;
};

/// 校验 dm-verity hashtree：从镜像数据重新计算 root digest 并与描述符比较。
VerifyResult verifyHashtree(const Descriptor& descriptor, const QString& imagePath, const CancelFn& cancel = {});

} // namespace gf::img::vbmeta
