#include <QtTest>

#include "TestRegistry.h"

#include "Compression.h"
#include "ImageCommon.h"
#include "Vbmeta.h"

#include <QCryptographicHash>
#include <QTemporaryDir>

namespace {

constexpr quint32 kBlock = 4096;

QByteArray hashWithSalt(QCryptographicHash::Algorithm algorithm, const QByteArray& salt,
                        const QByteArray& block)
{
    QCryptographicHash hash(algorithm);
    hash.addData(salt);
    hash.addData(block);
    return hash.result();
}

struct VerityTree {
    QByteArray root;
    quint64 treeSize = 0;
};

/// 与 avbtool / dm-verity 一致的方式计算根哈希。
VerityTree computeTree(const QByteArray& image, const QByteArray& salt, quint32 hashBlock)
{
    VerityTree result;
    QByteArray level;
    for (qsizetype at = 0; at < image.size(); at += kBlock) {
        QByteArray block = image.mid(at, kBlock);
        block = block.leftJustified(kBlock, '\0');
        level.append(hashWithSalt(QCryptographicHash::Sha256, salt, block));
    }
    if (level.size() % hashBlock != 0)
        level.append(QByteArray(hashBlock - (level.size() % hashBlock), '\0'));
    result.treeSize = quint64(level.size());
    while (level.size() > qsizetype(hashBlock)) {
        QByteArray next;
        for (qsizetype at = 0; at < level.size(); at += hashBlock)
            next.append(hashWithSalt(QCryptographicHash::Sha256, salt, level.mid(at, hashBlock)));
        if (next.size() % hashBlock != 0)
            next.append(QByteArray(hashBlock - (next.size() % hashBlock), '\0'));
        level = next;
        result.treeSize += quint64(level.size());
    }
    const QByteArray top = level.leftJustified(hashBlock, '\0');
    result.root = hashWithSalt(QCryptographicHash::Sha256, salt, top);
    return result;
}

void appendBe32(QByteArray& out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

void appendBe64(QByteArray& out, quint64 v)
{
    for (int i = 7; i >= 0; --i)
        out.append(char((v >> (8 * i)) & 0xFF));
}

QByteArray buildVbmeta(const QByteArray& image, const QByteArray& salt, const QByteArray& root,
                       quint64 treeSize)
{
    // hashtree 描述符 body：紧凑大端编码，共 184 字节。
    QByteArray body;
    appendBe32(body, 1);                            // dm_verity_version
    appendBe64(body, quint64(image.size()));        // image_size
    appendBe64(body, quint64(image.size()));        // tree_offset（紧跟数据）
    appendBe64(body, treeSize);                     // tree_size
    appendBe32(body, kBlock);                       // data_block_size
    appendBe32(body, kBlock);                       // hash_block_size
    appendBe32(body, 0);                            // fec_num_roots
    appendBe64(body, 0);                            // fec_offset
    appendBe64(body, 0);                            // fec_size
    body.append(QByteArray("sha256").leftJustified(32, '\0'));
    body.append(root.leftJustified(64, '\0'));
    body.append(salt.leftJustified(32, '\0'));

    QByteArray descriptor;
    appendBe64(descriptor, quint64(1) << 32); // tag << 32（AVB 约定）
    appendBe64(descriptor, quint64(body.size()));
    descriptor.append(body);

    QByteArray header;
    header.append("AVB0", 4);
    appendBe32(header, 1);  // required_libavb_version_major
    appendBe32(header, 0);  // required_libavb_version_minor
    appendBe64(header, 0);  // authentication_data_block_size
    appendBe64(header, 0);  // auxiliary_data_block_size
    appendBe32(header, 0);  // algorithm_type = NONE
    appendBe64(header, 0);  // hash_offset
    appendBe64(header, 0);  // hash_size
    appendBe64(header, 0);  // signature_offset
    appendBe64(header, 0);  // signature_size
    appendBe64(header, 0);  // public_key_offset
    appendBe64(header, 0);  // public_key_size
    appendBe64(header, 0);  // public_key_metadata_offset
    appendBe64(header, 0);  // public_key_metadata_size
    appendBe64(header, 256); // descriptors_offset
    appendBe64(header, quint64(descriptor.size())); // descriptors_size
    appendBe64(header, 7);  // rollback_index
    appendBe32(header, 0);  // flags
    appendBe32(header, 0);  // rollback_index_location
    header.append(QByteArray("avbtool 1.2.0").leftJustified(48, '\0'));
    header.append(QByteArray(80, '\0'));
    return header + descriptor;
}

} // namespace

class TestVbmeta : public QObject
{
    Q_OBJECT

private slots:
    void parseHashtreeDescriptor()
    {
        const QByteArray image = QByteArray(kBlock * 3, '\x33');
        const QByteArray salt = QByteArray(32, '\x10');
        const VerityTree tree = computeTree(image, salt, kBlock);
        const QByteArray vbmetaBytes = buildVbmeta(image, salt, tree.root, tree.treeSize);

        QVERIFY(gf::img::vbmeta::looksLikeVbmeta(vbmetaBytes));
        const gf::img::vbmeta::Vbmeta vbmeta = gf::img::vbmeta::parse(vbmetaBytes);
        QCOMPARE(vbmeta.requiredMajor, 1u);
        QCOMPARE(vbmeta.algorithmType, 0u);
        QCOMPARE(vbmeta.flags, 0u);
        QVERIFY(!vbmeta.hashtreeDisabled());
        QVERIFY(!vbmeta.verificationDisabled());
        QCOMPARE(vbmeta.releaseString, QStringLiteral("avbtool 1.2.0"));
        QCOMPARE(vbmeta.rollbackIndex, quint64(7));
        QCOMPARE(vbmeta.descriptors.size(), 1);

        const auto& descriptor = vbmeta.descriptors.first();
        QCOMPARE(descriptor.tag, 1u);
        QCOMPARE(descriptor.tagName.contains(QStringLiteral("hashtree")), true);
        QCOMPARE(descriptor.imageSize, quint64(image.size()));
        QCOMPARE(descriptor.dataBlockSize, kBlock);
        QCOMPARE(descriptor.hashBlockSize, kBlock);
        QCOMPARE(descriptor.hashAlgorithm, QStringLiteral("sha256"));
        QCOMPARE(descriptor.rootDigest.left(32), tree.root);
        QCOMPARE(descriptor.salt, salt);
        QCOMPARE(descriptor.treeSize, tree.treeSize);

        const QStringList lines = gf::img::vbmeta::describe(vbmeta);
        QVERIFY(lines.size() >= 5);
        QVERIFY(!gf::img::vbmeta::descriptorText(descriptor).isEmpty());
    }

    void verifyHashtreeGoodAndBad()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray image(kBlock * 5, '\0');
        for (int i = 0; i < image.size(); ++i)
            image[i] = char((i * 29) & 0xFF);
        const QByteArray salt = QByteArray(32, '\x2A');
        const VerityTree tree = computeTree(image, salt, kBlock);
        const QByteArray vbmetaBytes = buildVbmeta(image, salt, tree.root, tree.treeSize);
        const gf::img::vbmeta::Vbmeta vbmeta = gf::img::vbmeta::parse(vbmetaBytes);

        const QString imagePath = gf::img::joinPath(dir.path(), QStringLiteral("system.img"));
        gf::img::writeFileOrThrow(imagePath, image);
        const auto result = gf::img::vbmeta::verifyHashtree(vbmeta.descriptors.first(), imagePath);
        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(result.expectedRoot, tree.root);
        QVERIFY(result.treeSize > 0);

        // 篡改数据后校验必须失败
        QByteArray corrupted = image;
        corrupted[100] = char(quint8(corrupted.at(100)) ^ 0xFF);
        const QString corruptedPath = gf::img::joinPath(dir.path(), QStringLiteral("bad.img"));
        gf::img::writeFileOrThrow(corruptedPath, corrupted);
        const auto bad = gf::img::vbmeta::verifyHashtree(vbmeta.descriptors.first(), corruptedPath);
        QVERIFY(!bad.ok);
        QVERIFY(!bad.message.isEmpty());
    }

    void patchFlags()
    {
        const QByteArray image = QByteArray(kBlock, '\x44');
        const QByteArray salt = QByteArray(32, '\x55');
        const VerityTree tree = computeTree(image, salt, kBlock);
        const QByteArray original = buildVbmeta(image, salt, tree.root, tree.treeSize);

        const QByteArray patched = gf::img::vbmeta::patchFlags(original, 3, false);
        const gf::img::vbmeta::Vbmeta vbmeta = gf::img::vbmeta::parse(patched);
        QCOMPARE(vbmeta.flags, 3u);
        QVERIFY(vbmeta.hashtreeDisabled());
        QVERIFY(vbmeta.verificationDisabled());
        QVERIFY(vbmeta.flagsText().contains(QStringLiteral("HASHTREE_DISABLED")));
        QVERIFY(vbmeta.flagsText().contains(QStringLiteral("VERIFICATION_DISABLED")));
        // 其余字节不能被改动
        QCOMPARE(patched.left(120), original.left(120));
        QCOMPARE(patched.mid(124), original.mid(124));

        const QByteArray cleared = gf::img::vbmeta::patchFlags(original, 2, true);
        const gf::img::vbmeta::Vbmeta clearedVbmeta = gf::img::vbmeta::parse(cleared);
        QCOMPARE(clearedVbmeta.flags, 2u);
    }

    void rejectsInvalidData()
    {
        bool threw = false;
        try {
            gf::img::vbmeta::parse(QByteArray(256, '\0'));
        } catch (const std::exception& e) {
            threw = QString::fromUtf8(e.what()).contains(QStringLiteral("vbmeta"));
        }
        QVERIFY(threw);
        QVERIFY(!gf::img::vbmeta::looksLikeVbmeta(QByteArray("NOPE")));
    }
};

#include "test_vbmeta.moc"
GF_TEST_CLASS(TestVbmeta)
