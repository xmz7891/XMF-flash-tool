#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "TestRegistry.h"
#include "packets/PacketStore.h"
#include "core/Util.h"

using namespace gf;
using namespace gf::packets;

namespace {

QString repoRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("data/packets"))))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return {};
}

QString writeSyntheticPackage(const QString& dir)
{
    util::ensureDir(dir);
    const QByteArray config = R"(<?xml version="1.0"?>
<GeekFlashPacket>
  <PacketInfo Publisher="unit-test" PublishDate="1700000000000" PublishVersion="1.2.3" Tip="test" Type="SPRD" />
  <Brand Name="TestBrand">
    <Series Name="TestSeries">
      <Model Name="TestModel" PayloadAddr="0x65015f08" FDL1="/FDL1/test_fdl1.bin"
             FDL1Addr="0x65000800" FDL2="/FDL2/test_fdl2.bin" FDL2Addr="0x9efffe00"
             Description="synthetic device" />
    </Series>
  </Brand>
</GeekFlashPacket>)";
    QFile::remove(dir + QStringLiteral("/Config.xml"));
    QFile file(dir + QStringLiteral("/Config.xml"));
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(config);
    file.close();
    util::ensureDir(dir + QStringLiteral("/FDL1"));
    util::writeFile(dir + QStringLiteral("/FDL1/test_fdl1.bin"), QByteArray(1024, 'A'));
    util::ensureDir(dir + QStringLiteral("/FDL2"));
    util::writeFile(dir + QStringLiteral("/FDL2/test_fdl2.bin"), QByteArray(4096, 'B'));
    return dir;
}

} // namespace

class TestPacketStore : public QObject {
    Q_OBJECT
private slots:
    void resolvesRealResourcePacks()
    {
        const QString root = repoRoot();
        if (root.isEmpty())
            QSKIP("仓库 data/packets 不存在，跳过真实引导包测试");
        PacketStore store;
        store.scan({root + QStringLiteral("/data/packets")});
        const QStringList ids = store.packetIds();
        QVERIFY2(ids.size() >= 3, qPrintable(QStringLiteral("引导包数量不足: %1").arg(ids.size())));

        QString mtkId;
        QString qcId;
        QString sprdId;
        for (const QString& id : ids) {
            if (id.contains(QStringLiteral("mtk"), Qt::CaseInsensitive))
                mtkId = id;
            else if (id.contains(QStringLiteral("qualcomm"), Qt::CaseInsensitive))
                qcId = id;
            else if (id.contains(QStringLiteral("sprd"), Qt::CaseInsensitive))
                sprdId = id;
        }
        QVERIFY(!mtkId.isEmpty());
        QVERIFY(!qcId.isEmpty());
        QVERIFY(!sprdId.isEmpty());

        // MTK pack ships DA files inside the zip; they must be extractable.
        QVERIFY(!store.findModel(mtkId, QStringLiteral("MT6895")).isEmpty());
        const QString da1 = store.modelResource(mtkId, QStringLiteral("MT6895"), QStringLiteral("DA1"));
        QVERIFY2(!da1.isEmpty(), "MTK DA1 未解析");
        QVERIFY2(QFileInfo(da1).size() > 1000, "MTK DA1 文件异常");

        // QC pack ships per-device firehose loaders as "Loader".
        QVERIFY(!store.findModel(qcId, QStringLiteral("439全系")).isEmpty());
        const QString loader = store.modelResource(qcId, QStringLiteral("439全系"), QStringLiteral("Loader"));
        QVERIFY2(!loader.isEmpty(), "QC Loader 未解析");
        QVERIFY2(QFileInfo(loader).size() > 1000, "QC Loader 文件异常");

        // The SPRD pack only contains Config.xml (no FDL payload): the resolver
        // must report "missing" instead of returning a bogus path.
        QVERIFY(!store.findModel(sprdId, QStringLiteral("iPlay 50mini EN")).isEmpty());
        QVERIFY(store.modelResource(sprdId, QStringLiteral("iPlay 50mini EN"), QStringLiteral("FDL1")).isEmpty());
    }

    void parsesSyntheticFolderPackage()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString pkg = writeSyntheticPackage(temp.path() + QStringLiteral("/pkg"));
        QVERIFY(!pkg.isEmpty());

        PacketStore store;
        store.scan({temp.path()});
        const QVariantList packets = store.packets();
        QCOMPARE(packets.size(), 1);
        const QVariantMap packet = packets.first().toMap();
        QCOMPARE(packet.value(QStringLiteral("type")).toString(), QStringLiteral("SPRD"));
        QCOMPARE(packet.value(QStringLiteral("publisher")).toString(), QStringLiteral("unit-test"));
        QCOMPARE(packet.value(QStringLiteral("modelCount")).toInt(), 1);

        const QVariantList models = store.models(packet.value(QStringLiteral("id")).toString());
        QCOMPARE(models.size(), 1);
        const QVariantMap model = models.first().toMap();
        QCOMPARE(model.value(QStringLiteral("name")).toString(), QStringLiteral("TestModel"));
        QCOMPARE(model.value(QStringLiteral("brand")).toString(), QStringLiteral("TestBrand"));
        QCOMPARE(model.value(QStringLiteral("FDL1Addr")).toString(), QStringLiteral("0x65000800"));

        const QString fdl1 = store.modelResource(packet.value(QStringLiteral("id")).toString(),
                                                 QStringLiteral("TestModel"), QStringLiteral("FDL1"));
        QVERIFY(!fdl1.isEmpty());
        QCOMPARE(QFileInfo(fdl1).size(), qint64(1024));
    }

    void scansRealPackages_data()
    {
        QTest::addColumn<QString>("packetId");
        QTest::newRow("mtk") << QStringLiteral("package_mtk_edl-master");
        QTest::newRow("sprd") << QStringLiteral("package_sprd_u2s-master");
    }

    void scansRealPackages()
    {
        QFETCH(QString, packetId);
        const QString root = repoRoot();
        if (root.isEmpty())
            QSKIP("repository data directory not found");
        PacketStore store;
        store.scan({root + QStringLiteral("/data/packets")});
        QVERIFY(!store.packetIds().isEmpty());
        bool found = false;
        for (const QString& id : store.packetIds()) {
            if (id.contains(packetId.section(QLatin1Char('_'), 0, 0)) || id.contains(QStringLiteral("mtk"))
                || id.contains(QStringLiteral("sprd")))
                found = true;
        }
        QVERIFY(found);
        const QVariantList packets = store.packets();
        int totalModels = 0;
        for (const QVariant& entry : packets)
            totalModels += entry.toMap().value(QStringLiteral("modelCount")).toInt();
        QVERIFY(totalModels > 0);
    }

    void extractsMtkDaFromZip()
    {
        const QString root = repoRoot();
        if (root.isEmpty())
            QSKIP("repository data directory not found");
        PacketStore store;
        store.scan({root + QStringLiteral("/data/packets")});
        QString mtkId;
        const QVariantList packets = store.packets();
        for (const QVariant& entry : packets) {
            const QVariantMap packet = entry.toMap();
            if (packet.value(QStringLiteral("type")).toString() == QLatin1String("MTK")
                && packet.value(QStringLiteral("sourceType")).toString() == QLatin1String("zip")) {
                mtkId = packet.value(QStringLiteral("id")).toString();
                break;
            }
        }
        if (mtkId.isEmpty())
            QSKIP("MTK zip package not available");
        const QVariantList models = store.models(mtkId);
        QVERIFY(!models.isEmpty());
        for (const QVariant& entry : models) {
            const QVariantMap model = entry.toMap();
            const QString da1 = model.value(QStringLiteral("DA1")).toString();
            if (da1.isEmpty())
                continue;
            const QString path = store.modelResource(mtkId, model.value(QStringLiteral("name")).toString(),
                                                     QStringLiteral("DA1"));
            QVERIFY2(!path.isEmpty(), qPrintable(da1));
            QVERIFY(QFileInfo::exists(path));
            QVERIFY(QFileInfo(path).size() > 0);
            break;
        }
    }
};

#include "test_packetstore.moc"
GF_TEST_CLASS(TestPacketStore)
