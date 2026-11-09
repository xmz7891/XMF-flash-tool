#include <QtTest>

#include "TestRegistry.h"

#include "SuperImage.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {

QByteArray makePartition(int blocks, int seed)
{
    QByteArray data(4096 * blocks, '\0');
    for (int i = 0; i < data.size(); ++i)
        data[i] = char((i * 61 + seed * 17) & 0xFF);
    return data;
}

void writePartitions(const QString& dir, const QByteArray& a, const QByteArray& b)
{
    gf::img::ensureParentDirectory(gf::img::joinPath(dir, QStringLiteral("x")));
    gf::img::writeFileOrThrow(gf::img::joinPath(dir, QStringLiteral("boot.img")), a);
    gf::img::writeFileOrThrow(gf::img::joinPath(dir, QStringLiteral("system.img")), b);
}

} // namespace

class TestSuper : public QObject
{
    Q_OBJECT

private slots:
    void mergeParseSplitRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString partsDir = gf::img::joinPath(dir.path(), QStringLiteral("parts"));
        const QByteArray bootImage = makePartition(3, 1);
        const QByteArray systemImage = makePartition(5, 2);
        writePartitions(partsDir, bootImage, systemImage);

        const QString superPath = gf::img::joinPath(dir.path(), QStringLiteral("super.img"));
        gf::img::superimg::merge(partsDir, {QStringLiteral("boot"), QStringLiteral("system")},
                                 superPath, 0, 2, false);
        QVERIFY(QFileInfo(superPath).size() > qint64(bootImage.size() + systemImage.size()));

        const gf::img::superimg::SuperInfo info = gf::img::superimg::inspectFile(superPath);
        QCOMPARE(info.geometry.metadataSlotCount, 2u);
        QCOMPARE(info.geometry.logicalBlockSize, 4096u);
        QVERIFY(info.geometry.checksumValid);
        QCOMPARE(info.metadataSlots.size(), 2);
        QVERIFY(info.activeSlot >= 0);
        const auto* metadata = info.active();
        QVERIFY(metadata != nullptr);
        QVERIFY(metadata->headerChecksumValid);
        QVERIFY(metadata->tablesChecksumValid);
        QCOMPARE(metadata->partitions.size(), 2);
        QCOMPARE(metadata->groups.size(), 1);
        QCOMPARE(metadata->blockDevices.size(), 1);
        QCOMPARE(metadata->groups.first().name, QStringLiteral("default"));
        QCOMPARE(metadata->blockDevices.first().partitionName, QStringLiteral("super"));
        QCOMPARE(metadata->partitions.at(0).name, QStringLiteral("boot"));
        QCOMPARE(metadata->partitions.at(1).name, QStringLiteral("system"));
        QVERIFY(gf::img::superimg::validate(info).isEmpty());
        QVERIFY(!info.sparseWrapped);

        const QString outDir = gf::img::joinPath(dir.path(), QStringLiteral("split"));
        gf::img::superimg::split(superPath, info, outDir, {}, false);
        QCOMPARE(gf::img::readFileOrThrow(gf::img::joinPath(outDir, QStringLiteral("boot.img"))),
                 bootImage);
        QCOMPARE(gf::img::readFileOrThrow(gf::img::joinPath(outDir, QStringLiteral("system.img"))),
                 systemImage);

        // 只拆分一个分区
        const QString onlyDir = gf::img::joinPath(dir.path(), QStringLiteral("only"));
        gf::img::superimg::split(superPath, info, onlyDir, {QStringLiteral("system")}, false);
        QVERIFY(!QFileInfo::exists(gf::img::joinPath(onlyDir, QStringLiteral("boot.img"))));
        QVERIFY(QFileInfo::exists(gf::img::joinPath(onlyDir, QStringLiteral("system.img"))));
    }

    void sparseWrappedSuper()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString partsDir = gf::img::joinPath(dir.path(), QStringLiteral("parts"));
        const QByteArray bootImage = makePartition(2, 3);
        const QByteArray systemImage = makePartition(4, 4);
        writePartitions(partsDir, bootImage, systemImage);

        const QString superPath = gf::img::joinPath(dir.path(), QStringLiteral("super_sparse.img"));
        gf::img::superimg::merge(partsDir, {QStringLiteral("boot"), QStringLiteral("system")},
                                 superPath, 8ull * 1024 * 1024, 3, true);
        const gf::img::superimg::SuperInfo info = gf::img::superimg::inspectFile(superPath);
        QVERIFY(info.sparseWrapped);
        QCOMPARE(info.geometry.metadataSlotCount, 3u);
        QCOMPARE(info.metadataSlots.size(), 3);
        QCOMPARE(info.active()->partitions.size(), 2);

        const QString outDir = gf::img::joinPath(dir.path(), QStringLiteral("split_sparse"));
        gf::img::superimg::split(superPath, info, outDir, {}, false);
        QCOMPARE(gf::img::readFileOrThrow(gf::img::joinPath(outDir, QStringLiteral("boot.img"))),
                 bootImage);
        QCOMPARE(gf::img::readFileOrThrow(gf::img::joinPath(outDir, QStringLiteral("system.img"))),
                 systemImage);
    }

    void resizeSuper()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString partsDir = gf::img::joinPath(dir.path(), QStringLiteral("parts"));
        writePartitions(partsDir, makePartition(2, 5), makePartition(3, 6));
        const QString superPath = gf::img::joinPath(dir.path(), QStringLiteral("super.img"));
        gf::img::superimg::merge(partsDir, {QStringLiteral("boot"), QStringLiteral("system")},
                                 superPath, 4ull * 1024 * 1024, 2, false);
        const qint64 oldSize = QFileInfo(superPath).size();

        const QString biggerPath = gf::img::joinPath(dir.path(), QStringLiteral("super_big.img"));
        const quint64 biggerSize = quint64(oldSize) + 4ull * 1024 * 1024;
        gf::img::superimg::resize(superPath, biggerPath, biggerSize);
        QCOMPARE(QFileInfo(biggerPath).size(), qint64(biggerSize));
        const auto biggerInfo = gf::img::superimg::inspectFile(biggerPath);
        QCOMPARE(biggerInfo.metadataSlots.size(), 2);
        QCOMPARE(biggerInfo.active()->partitions.size(), 2);
        QCOMPARE(biggerInfo.active()->blockDevices.first().size, biggerSize / 512);

        // 缩小到小于数据结束位置必须失败
        bool threw = false;
        try {
            gf::img::superimg::resize(superPath,
                                      gf::img::joinPath(dir.path(), QStringLiteral("bad.img")),
                                      2ull * 1024 * 1024);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void mergeTooSmallFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString partsDir = gf::img::joinPath(dir.path(), QStringLiteral("parts"));
        writePartitions(partsDir, makePartition(4, 7), makePartition(4, 8));
        bool threw = false;
        try {
            gf::img::superimg::merge(partsDir, {QStringLiteral("boot"), QStringLiteral("system")},
                                     gf::img::joinPath(dir.path(), QStringLiteral("super.img")),
                                     1024 * 1024, 2, false);
        } catch (const std::exception& e) {
            threw = QString::fromUtf8(e.what()).contains(QStringLiteral("super"));
        }
        QVERIFY(threw);
    }

    void rejectsNonSuper()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = gf::img::joinPath(dir.path(), QStringLiteral("fake.img"));
        gf::img::writeFileOrThrow(path, QByteArray(8192, '\x42'));
        bool threw = false;
        try {
            gf::img::superimg::inspectFile(path);
        } catch (const std::exception& e) {
            threw = QString::fromUtf8(e.what()).contains(QStringLiteral("super"));
        }
        QVERIFY(threw);
    }
};

#include "test_super.moc"
GF_TEST_CLASS(TestSuper)
