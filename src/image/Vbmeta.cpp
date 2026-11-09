#include "Vbmeta.h"

#include <QCryptographicHash>
#include <QFile>

namespace gf::img::vbmeta {

namespace {

quint32 rd32be(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        fail(QStringLiteral("vbmeta 数据被截断"));
    return (quint32(quint8(d.at(at))) << 24) | (quint32(quint8(d.at(at + 1))) << 16) |
           (quint32(quint8(d.at(at + 2))) << 8) | quint32(quint8(d.at(at + 3)));
}

quint64 rd64be(const QByteArray& d, qsizetype at)
{
    if (at < 0 || at + 8 > d.size())
        fail(QStringLiteral("vbmeta 数据被截断"));
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | quint64(quint8(d.at(at + i)));
    return v;
}

void wr32be(QByteArray& d, qsizetype at, quint32 v)
{
    d[at] = char((v >> 24) & 0xFF);
    d[at + 1] = char((v >> 16) & 0xFF);
    d[at + 2] = char((v >> 8) & 0xFF);
    d[at + 3] = char(v & 0xFF);
}

QString fixedString(const QByteArray& data, qsizetype at, qsizetype maxSize)
{
    const QByteArray raw = data.mid(at, maxSize);
    const qsizetype nul = raw.indexOf('\0');
    return QString::fromLatin1(nul < 0 ? raw : raw.left(nul));
}

QString tagNameOf(quint32 tag)
{
    switch (DescriptorTag(tag)) {
    case DescriptorTag::Property:
        return QStringLiteral("属性 (property)");
    case DescriptorTag::Hashtree:
        return QStringLiteral("哈希树 (hashtree)");
    case DescriptorTag::Hash:
        return QStringLiteral("哈希 (hash)");
    case DescriptorTag::KernelCmdline:
        return QStringLiteral("内核命令行 (kernel_cmdline)");
    case DescriptorTag::ChainPartition:
        return QStringLiteral("链式分区 (chain_partition)");
    }
    return QStringLiteral("未知描述符 (%1)").arg(tag);
}

void parseHashtreeBody(const QByteArray& body, Descriptor& descriptor)
{
    // AVB 使用紧凑（packed）大端编码：标准 hashtree 描述符 body 为 184 字节。
    if (body.size() < 88)
        return;
    const bool packed = body.size() >= 184;
    if (packed || body.size() >= 192) {
        const qsizetype base = packed ? 0 : 0;
        quint32 version = 0;
        quint64 imageSize = 0;
        quint64 treeOffset = 0;
        quint64 treeSize = 0;
        quint32 dataBlock = 0;
        quint32 hashBlock = 0;
        quint32 fecRoots = 0;
        quint64 fecOffset = 0;
        quint64 fecSize = 0;
        qsizetype algoAt = 0;
        if (packed) {
            version = rd32be(body, base + 0);
            imageSize = rd64be(body, base + 4);
            treeOffset = rd64be(body, base + 12);
            treeSize = rd64be(body, base + 20);
            dataBlock = rd32be(body, base + 28);
            hashBlock = rd32be(body, base + 32);
            fecRoots = rd32be(body, base + 36);
            fecOffset = rd64be(body, base + 40);
            fecSize = rd64be(body, base + 48);
            algoAt = base + 56;
        } else {
            version = rd32be(body, 0);
            imageSize = rd64be(body, 8);
            treeOffset = rd64be(body, 16);
            treeSize = rd64be(body, 24);
            dataBlock = rd32be(body, 32);
            hashBlock = rd32be(body, 36);
            fecRoots = rd32be(body, 40);
            fecOffset = rd64be(body, 48);
            fecSize = rd64be(body, 56);
            algoAt = 64;
        }
        descriptor.dmVerityVersion = version;
        descriptor.imageSize = imageSize;
        descriptor.treeOffset = treeOffset;
        descriptor.treeSize = treeSize;
        descriptor.dataBlockSize = dataBlock;
        descriptor.hashBlockSize = hashBlock;
        descriptor.fecNumRoots = fecRoots;
        descriptor.fecOffset = fecOffset;
        descriptor.fecSize = fecSize;
        descriptor.hashAlgorithm = fixedString(body, algoAt, 32);
        if (packed && body.size() >= 184) {
            descriptor.rootDigest = body.mid(88, 64);
            descriptor.salt = body.mid(152, 32);
        } else if (body.size() >= 192) {
            descriptor.rootDigest = body.mid(96, 64);
            descriptor.salt = body.mid(160, 32);
        }
    }
}

void parseHashBody(const QByteArray& body, Descriptor& descriptor)
{
    if (body.size() < 108)
        return;
    descriptor.imageSize = rd64be(body, 0);
    descriptor.hashAlgorithm = fixedString(body, 8, 32);
    descriptor.digest = body.mid(40, 64);
    const quint32 saltLen = rd32be(body, 104);
    if (body.size() >= 140) {
        const qsizetype len = qMin<qsizetype>(qMax<quint32>(saltLen, 0), 32);
        descriptor.salt = body.mid(108, len);
    }
}

void parsePropertyBody(const QByteArray& body, Descriptor& descriptor)
{
    if (body.size() < 16)
        return;
    const quint64 keyLen = rd64be(body, 0);
    const quint64 valueLen = rd64be(body, 8);
    if (quint64(body.size()) < 16 + keyLen + valueLen)
        return;
    descriptor.propertyKey = QString::fromUtf8(body.mid(16, qsizetype(keyLen)));
    descriptor.propertyValue =
        QString::fromUtf8(body.mid(16 + qsizetype(keyLen), qsizetype(valueLen)));
}

void parseKernelCmdlineBody(const QByteArray& body, Descriptor& descriptor)
{
    if (body.size() < 8)
        return;
    descriptor.cmdlineFlags = rd32be(body, 0);
    const quint32 len = rd32be(body, 4);
    if (quint64(body.size()) < 8 + quint64(len))
        return;
    descriptor.kernelCmdline = QString::fromUtf8(body.mid(8, qsizetype(len)));
}

void parseChainBody(const QByteArray& body, Descriptor& descriptor)
{
    if (body.size() < 40)
        return;
    descriptor.rollbackIndexLocation = rd32be(body, 0);
    descriptor.chainPartitionName = fixedString(body, 4, 32);
    const quint32 keyLen = rd32be(body, 36);
    if (quint64(body.size()) >= 40 + quint64(keyLen))
        descriptor.chainPublicKey = body.mid(40, qsizetype(keyLen));
}

QString bytesToHex(const QByteArray& data)
{
    return QString::fromLatin1(data.toHex());
}

QCryptographicHash::Algorithm algorithmFromName(const QString& name, int* digestSize)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QStringLiteral("sha1")) {
        if (digestSize)
            *digestSize = 20;
        return QCryptographicHash::Sha1;
    }
    if (lower == QStringLiteral("sha512")) {
        if (digestSize)
            *digestSize = 64;
        return QCryptographicHash::Sha512;
    }
    if (digestSize)
        *digestSize = 32;
    return QCryptographicHash::Sha256;
}

QByteArray applySaltHash(QCryptographicHash::Algorithm algorithm, const QByteArray& salt,
                         const QByteArray& block)
{
    QCryptographicHash hash(algorithm);
    if (!salt.isEmpty())
        hash.addData(salt);
    hash.addData(block);
    return hash.result();
}

} // namespace

bool looksLikeVbmeta(const QByteArray& data)
{
    return data.size() >= 256 && data.left(4) == QByteArray("AVB0");
}

QString Vbmeta::algorithmText() const
{
    switch (algorithmType) {
    case 0:
        return QStringLiteral("NONE（无签名）");
    case 1:
        return QStringLiteral("SHA256_RSA2048");
    case 2:
        return QStringLiteral("SHA256_RSA4096");
    case 3:
        return QStringLiteral("SHA256_RSA8192");
    case 4:
        return QStringLiteral("SHA512_RSA2048");
    case 5:
        return QStringLiteral("SHA512_RSA4096");
    case 6:
        return QStringLiteral("SHA512_RSA8192");
    default:
        break;
    }
    return QStringLiteral("未知算法 (%1)").arg(algorithmType);
}

QString Vbmeta::flagsText() const
{
    QStringList parts;
    if (hashtreeDisabled())
        parts << QStringLiteral("HASHTREE_DISABLED");
    if (verificationDisabled())
        parts << QStringLiteral("VERIFICATION_DISABLED");
    if (parts.isEmpty())
        parts << QStringLiteral("无（正常校验）");
    return parts.join(QStringLiteral(" | "));
}

Vbmeta parse(const QByteArray& data)
{
    if (!looksLikeVbmeta(data))
        fail(QStringLiteral("不是 vbmeta 镜像（magic 不是 AVB0）"));
    Vbmeta v;
    v.raw = data;
    v.requiredMajor = rd32be(data, 4);
    v.requiredMinor = rd32be(data, 8);
    v.authenticationDataBlockSize = rd64be(data, 12);
    v.auxiliaryDataBlockSize = rd64be(data, 20);
    v.algorithmType = rd32be(data, 28);
    v.hashOffset = rd64be(data, 32);
    v.hashSize = rd64be(data, 40);
    v.signatureOffset = rd64be(data, 48);
    v.signatureSize = rd64be(data, 56);
    v.publicKeyOffset = rd64be(data, 64);
    v.publicKeySize = rd64be(data, 72);
    v.publicKeyMetadataOffset = rd64be(data, 80);
    v.publicKeyMetadataSize = rd64be(data, 88);
    v.descriptorsOffset = rd64be(data, 96);
    v.descriptorsSize = rd64be(data, 104);
    v.rollbackIndex = rd64be(data, 112);
    v.flags = rd32be(data, 120);
    v.rollbackIndexLocation = rd32be(data, 124);
    v.releaseString = fixedString(data, 128, 48);

    auto checkRange = [&](quint64 offset, quint64 size, const QString& what) {
        if (quint64(data.size()) < offset + size)
            fail(QStringLiteral("vbmeta %1 超出镜像范围（偏移 %2，大小 %3）")
                     .arg(what)
                     .arg(offset)
                     .arg(size));
    };
    checkRange(v.hashOffset, v.hashSize, QStringLiteral("hash"));
    checkRange(v.signatureOffset, v.signatureSize, QStringLiteral("签名"));
    checkRange(v.publicKeyOffset, v.publicKeySize, QStringLiteral("公钥"));
    checkRange(v.publicKeyMetadataOffset, v.publicKeyMetadataSize, QStringLiteral("公钥元数据"));
    checkRange(v.descriptorsOffset, v.descriptorsSize, QStringLiteral("描述符"));
    v.hash = data.mid(qsizetype(v.hashOffset), qsizetype(v.hashSize));
    v.signature = data.mid(qsizetype(v.signatureOffset), qsizetype(v.signatureSize));
    v.publicKey = data.mid(qsizetype(v.publicKeyOffset), qsizetype(v.publicKeySize));

    const qint64 descStart = qint64(v.descriptorsOffset);
    const qint64 descEnd = descStart + qint64(v.descriptorsSize);
    qint64 pos = descStart;
    while (pos + 16 <= descEnd) {
        const quint64 rawTag = rd64be(data, pos);
        const quint64 numFollowing = rd64be(data, pos + 8);
        quint32 tag = quint32(rawTag >> 32);
        if (tag > 4 && rawTag <= 4)
            tag = quint32(rawTag);
        if (quint64(pos) + 16 + numFollowing > quint64(descEnd))
            fail(QStringLiteral("vbmeta 描述符长度越界（偏移 %1）").arg(pos));
        Descriptor descriptor;
        descriptor.tag = tag;
        descriptor.rawTagWord = rawTag;
        descriptor.offset = pos;
        descriptor.numBytesFollowing = numFollowing;
        descriptor.tagName = tagNameOf(tag);
        const QByteArray body = data.mid(pos + 16, qsizetype(numFollowing));
        switch (DescriptorTag(tag)) {
        case DescriptorTag::Property:
            parsePropertyBody(body, descriptor);
            break;
        case DescriptorTag::Hashtree:
            parseHashtreeBody(body, descriptor);
            break;
        case DescriptorTag::Hash:
            parseHashBody(body, descriptor);
            break;
        case DescriptorTag::KernelCmdline:
            parseKernelCmdlineBody(body, descriptor);
            break;
        case DescriptorTag::ChainPartition:
            parseChainBody(body, descriptor);
            break;
        default:
            break;
        }
        v.descriptors.append(descriptor);
        pos += 16 + qint64(numFollowing);
    }
    return v;
}

QString descriptorText(const Descriptor& d)
{
    QStringList parts;
    parts << d.tagName;
    switch (DescriptorTag(d.tag)) {
    case DescriptorTag::Property:
        parts << QStringLiteral("%1 = %2").arg(d.propertyKey, d.propertyValue);
        break;
    case DescriptorTag::Hashtree:
        parts << QStringLiteral("镜像 %1，数据块 %2，哈希块 %3，算法 %4")
                     .arg(formatBytes(d.imageSize))
                     .arg(d.dataBlockSize)
                     .arg(d.hashBlockSize)
                     .arg(d.hashAlgorithm);
        parts << QStringLiteral("树偏移 %1，树大小 %2")
                     .arg(d.treeOffset)
                     .arg(formatBytes(d.treeSize));
        if (d.fecNumRoots > 0)
            parts << QStringLiteral("FEC roots %1，FEC 偏移 %2").arg(d.fecNumRoots).arg(d.fecOffset);
        parts << QStringLiteral("root=%1").arg(bytesToHex(d.rootDigest.left(32)));
        if (!d.salt.isEmpty())
            parts << QStringLiteral("salt=%1").arg(bytesToHex(d.salt));
        break;
    case DescriptorTag::Hash:
        parts << QStringLiteral("镜像 %1，算法 %2").arg(formatBytes(d.imageSize), d.hashAlgorithm);
        parts << QStringLiteral("digest=%1").arg(bytesToHex(d.digest.left(32)));
        if (!d.salt.isEmpty())
            parts << QStringLiteral("salt=%1").arg(bytesToHex(d.salt));
        break;
    case DescriptorTag::KernelCmdline:
        parts << QStringLiteral("flags=%1，cmdline=%2").arg(d.cmdlineFlags).arg(d.kernelCmdline);
        break;
    case DescriptorTag::ChainPartition:
        parts << QStringLiteral("分区 %1，rollback 槽 %2，公钥 %3 字节")
                     .arg(d.chainPartitionName)
                     .arg(d.rollbackIndexLocation)
                     .arg(d.chainPublicKey.size());
        break;
    default:
        break;
    }
    return parts.join(QStringLiteral("；"));
}

QStringList describe(const Vbmeta& v)
{
    QStringList lines;
    lines << QStringLiteral("vbmeta 版本要求：%1.%2，算法：%3").arg(v.requiredMajor).arg(v.requiredMinor).arg(v.algorithmText());
    lines << QStringLiteral("flags = 0x%1（%2）").arg(v.flags, 8, 16, QLatin1Char('0')).arg(v.flagsText());
    lines << QStringLiteral("rollback index = %1，槽 = %2").arg(v.rollbackIndex).arg(v.rollbackIndexLocation);
    lines << QStringLiteral("release string = %1").arg(v.releaseString);
    lines << QStringLiteral("认证数据块 %1，辅助数据块 %2（签名 %3 字节，公钥 %4 字节）")
                 .arg(v.authenticationDataBlockSize)
                 .arg(v.auxiliaryDataBlockSize)
                 .arg(v.signatureSize)
                 .arg(v.publicKeySize);
    lines << QStringLiteral("hash = %1").arg(bytesToHex(v.hash));
    lines << QStringLiteral("描述符 %1 个：").arg(v.descriptors.size());
    for (const auto& d : v.descriptors)
        lines << QStringLiteral("  - %1").arg(descriptorText(d));
    return lines;
}

QByteArray patchFlags(const QByteArray& data, quint32 newFlags, bool clearAuthentication)
{
    Vbmeta v = parse(data);
    QByteArray out = data;
    wr32be(out, 120, newFlags);
    if (clearAuthentication) {
        const qint64 blockSize = qint64(v.authenticationDataBlockSize);
        if (blockSize > 0 && 256 + blockSize <= out.size()) {
            out.replace(256, blockSize, QByteArray(qsizetype(blockSize), '\0'));
            wr32be(out, 120, newFlags);
        }
    }
    return out;
}

VerifyResult verifyHashtree(const Descriptor& descriptor, const QString& imagePath, const CancelFn& cancel)
{
    VerifyResult result;
    if (DescriptorTag(descriptor.tag) != DescriptorTag::Hashtree) {
        result.message = QStringLiteral("该描述符不是 hashtree 类型，无法校验");
        return result;
    }
    if (descriptor.dataBlockSize == 0 || descriptor.hashBlockSize == 0) {
        result.message = QStringLiteral("hashtree 描述符缺少块大小信息");
        return result;
    }
    int digestSize = 32;
    const QCryptographicHash::Algorithm algorithm =
        algorithmFromName(descriptor.hashAlgorithm, &digestSize);
    const QByteArray salt = descriptor.salt;
    result.expectedRoot = descriptor.rootDigest.left(digestSize);

    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.message = QStringLiteral("无法打开镜像：%1").arg(imagePath);
        return result;
    }
    quint64 imageSize = descriptor.imageSize;
    if (imageSize == 0 || imageSize > quint64(file.size()))
        imageSize = quint64(file.size());

    QByteArray level;
    quint64 treeSize = 0;
    quint64 read = 0;
    while (read < imageSize) {
        checkCancel(cancel);
        QByteArray block = file.read(qint64(descriptor.dataBlockSize));
        if (block.isEmpty())
            break;
        read += quint64(block.size());
        if (quint64(block.size()) < descriptor.dataBlockSize)
            block.append(QByteArray(qsizetype(descriptor.dataBlockSize) - block.size(), '\0'));
        level.append(applySaltHash(algorithm, salt, block));
    }
    if (read < imageSize) {
        result.message = QStringLiteral("镜像数据不足：期望 %1 字节，实际 %2 字节")
                             .arg(formatBytes(imageSize), formatBytes(read));
        return result;
    }
    // 第一层先补齐到哈希块，再逐层向上。
    if (level.size() % qsizetype(descriptor.hashBlockSize) != 0)
        level.append(QByteArray(qsizetype(descriptor.hashBlockSize) -
                                    (level.size() % qsizetype(descriptor.hashBlockSize)),
                                '\0'));
    treeSize += quint64(level.size());
    while (level.size() > qsizetype(descriptor.hashBlockSize)) {
        checkCancel(cancel);
        QByteArray next;
        for (qsizetype at = 0; at < level.size(); at += qsizetype(descriptor.hashBlockSize)) {
            next.append(applySaltHash(algorithm, salt,
                                      level.mid(at, qsizetype(descriptor.hashBlockSize))));
        }
        if (next.size() % qsizetype(descriptor.hashBlockSize) != 0)
            next.append(QByteArray(qsizetype(descriptor.hashBlockSize) -
                                       (next.size() % qsizetype(descriptor.hashBlockSize)),
                                   '\0'));
        level = next;
        treeSize += quint64(level.size());
    }
    QByteArray top = level;
    top = top.leftJustified(qsizetype(descriptor.hashBlockSize), '\0');
    result.computedRoot = applySaltHash(algorithm, salt, top);
    result.treeSize = treeSize;
    result.ok = result.computedRoot.left(digestSize) == result.expectedRoot;
    if (result.ok) {
        result.message = QStringLiteral("hashtree 校验通过：root=%1，树大小 %2")
                             .arg(bytesToHex(result.computedRoot.left(digestSize)),
                                  formatBytes(treeSize));
    } else {
        result.message = QStringLiteral("hashtree 校验失败：计算 root=%1，期望 root=%2")
                             .arg(bytesToHex(result.computedRoot.left(digestSize)),
                                  bytesToHex(result.expectedRoot));
    }
    return result;
}

} // namespace gf::img::vbmeta
