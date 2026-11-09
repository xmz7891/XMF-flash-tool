#include <QtTest>

#include "TestRegistry.h"

#include "Payload.h"
#include "Proto.h"

#include <QDir>
#include <QTemporaryDir>

class TestPayload : public QObject
{
    Q_OBJECT

private slots:
    void manifestRoundTrip()
    {
        using namespace gf::img::payload;
        Manifest manifest;
        manifest.blockSize = 4096;
        manifest.minorVersion = 2;
        manifest.maxTimestamp = 1234567;
        manifest.signaturesOffset = 0;
        manifest.signaturesSize = 0;

        PartitionUpdate update;
        update.name = QStringLiteral("system");
        update.filesystemType = QStringLiteral("ext4");
        update.newInfo.size = 8192;
        update.newInfo.hash = QByteArray(32, char(0x5A));

        Operation replace;
        replace.type = OperationType::Replace;
        replace.dataOffset = 1024;
        replace.dataLength = 8192;
        replace.dstLength = 8192;
        replace.dstExtents = {{0, 2}};
        replace.dataSha256 = QByteArray(32, char(0x33));
        update.operations.append(replace);

        Operation zero;
        zero.type = OperationType::Zero;
        zero.dstExtents = {{4, 2}};
        update.operations.append(zero);
        manifest.partitions.append(update);

        const QByteArray encoded = encodeManifest(manifest);
        const Manifest decoded = decodeManifest(encoded);
        QCOMPARE(decoded.blockSize, 4096u);
        QCOMPARE(decoded.minorVersion, 2u);
        QCOMPARE(decoded.maxTimestamp, quint64(1234567));
        QCOMPARE(decoded.partitions.size(), 1);
        const auto& part = decoded.partitions.first();
        QCOMPARE(part.name, QStringLiteral("system"));
        QCOMPARE(part.filesystemType, QStringLiteral("ext4"));
        QCOMPARE(part.newInfo.size, quint64(8192));
        QCOMPARE(part.newInfo.hash, QByteArray(32, char(0x5A)));
        QCOMPARE(part.operations.size(), 2);
        QCOMPARE(int(part.operations.at(0).type), int(OperationType::Replace));
        QCOMPARE(part.operations.at(0).dataOffset, quint64(1024));
        QCOMPARE(part.operations.at(0).dstExtents.size(), 1);
        QCOMPARE(part.operations.at(0).dstExtents.first().startBlock, quint64(0));
        QCOMPARE(part.operations.at(0).dstExtents.first().numBlocks, quint64(2));
        QCOMPARE(int(part.operations.at(1).type), int(OperationType::Zero));
    }

    void partialManifestRoundTrip()
    {
        using namespace gf::img::payload;
        Manifest manifest;
        manifest.blockSize = 4096;
        manifest.minorVersion = 2;
        manifest.partialUpdate = true;

        PartitionUpdate update;
        update.name = QStringLiteral("system");
        update.newInfo.size = 16384;

        Operation copy;
        copy.type = OperationType::SourceCopy;
        copy.srcExtents = {{0, 2}};
        copy.dstExtents = {{0, 2}};
        update.operations.append(copy);
        Operation move;
        move.type = OperationType::Move;
        move.srcExtents = {{2, 1}};
        move.dstExtents = {{2, 1}};
        update.operations.append(move);
        manifest.partitions.append(update);

        const QByteArray encoded = encodeManifest(manifest);
        const Manifest decoded = decodeManifest(encoded);
        QVERIFY(decoded.partialUpdate);
        QCOMPARE(decoded.partitions.size(), 1);
        QCOMPARE(decoded.partitions.first().operations.size(), 2);
        QCOMPARE(int(decoded.partitions.first().operations.at(0).type),
                 int(OperationType::SourceCopy));
        QCOMPARE(int(decoded.partitions.first().operations.at(1).type),
                 int(OperationType::Move));

        // Wrap the manifest in a real CrAU container so parse() accepts it.
        const QByteArray manifestBytes = encodeManifest(manifest);
        QByteArray blob = QByteArrayLiteral("CrAU");
        auto appendBe64 = [&blob](quint64 value) {
            for (int i = 7; i >= 0; --i)
                blob.append(char((value >> (i * 8)) & 0xFF));
        };
        auto appendBe32 = [&blob](quint32 value) {
            for (int i = 3; i >= 0; --i)
                blob.append(char((value >> (i * 8)) & 0xFF));
        };
        appendBe64(2); // version
        appendBe64(quint64(manifestBytes.size()));
        appendBe32(0); // metadata signature size
        blob += manifestBytes;
        const auto parsed = gf::img::payload::parse(blob);
        QVERIFY(parsed.manifest.partialUpdate);
        QCOMPARE(parsed.manifest.partitions.size(), 1);
        QCOMPARE(parsed.manifest.partitions.first().name, QStringLiteral("system"));
    }

    void protobufWireFormat()
    {
        QByteArray out;
        gf::img::proto::writeVarintField(out, 3, 4096);
        gf::img::proto::writeBytesField(out, 13, QByteArray("hello"));
        gf::img::proto::Reader reader(out);
        gf::img::proto::Field field;
        QVERIFY(reader.next(field));
        QCOMPARE(field.number, 3u);
        QCOMPARE(field.wire, 0);
        QCOMPARE(field.varint, quint64(4096));
        QVERIFY(reader.next(field));
        QCOMPARE(field.number, 13u);
        QCOMPARE(field.wire, 2);
        QCOMPARE(field.bytes, QByteArray("hello"));
        QVERIFY(!reader.next(field));
    }

    void packParseExtractRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = gf::img::joinPath(dir.path(), QStringLiteral("parts"));
        gf::img::ensureParentDirectory(gf::img::joinPath(source, QStringLiteral("x")));

        QByteArray bootData(5000, '\0');
        for (int i = 0; i < bootData.size(); ++i)
            bootData[i] = char((i * 7) & 0xFF);
        QByteArray systemData(4096 * 3 + 123, char(0x11));
        for (int i = 0; i < systemData.size(); i += 997)
            systemData[i] = char(i & 0xFF);
        gf::img::writeFileOrThrow(gf::img::joinPath(source, QStringLiteral("boot.img")), bootData);
        gf::img::writeFileOrThrow(gf::img::joinPath(source, QStringLiteral("system.img")), systemData);

        const QString payloadPath = gf::img::joinPath(dir.path(), QStringLiteral("payload.bin"));
        gf::img::payload::packDirectory(source, {QStringLiteral("boot"), QStringLiteral("system")},
                                        payloadPath, 4096);
        QVERIFY(QFileInfo(payloadPath).size() > qint64(bootData.size() + systemData.size()));

        const gf::img::payload::Payload payload = gf::img::payload::parseFile(payloadPath);
        QCOMPARE(payload.header.version, quint64(2));
        QCOMPARE(payload.header.magic, QStringLiteral("CrAU"));
        QVERIFY(payload.header.manifestSize > 0);
        QCOMPARE(payload.manifest.partitions.size(), 2);
        QCOMPARE(payload.manifest.partitions.at(0).name, QStringLiteral("boot"));
        QCOMPARE(payload.manifest.partitions.at(1).name, QStringLiteral("system"));
        QCOMPARE(payload.manifest.partitions.at(0).newInfo.size, quint64(bootData.size()));
        QCOMPARE(payload.manifest.partitions.at(1).newInfo.size, quint64(systemData.size()));
        QCOMPARE(payload.manifest.partitions.at(0).operations.first().type,
                 gf::img::payload::OperationType::Replace);
        QVERIFY(!payload.manifest.partitions.at(0).operations.first().dataSha256.isEmpty());

        const QString outDir = gf::img::joinPath(dir.path(), QStringLiteral("out"));
        gf::img::payload::extractAll(payloadPath, payload, outDir, {}, true);
        QCOMPARE(gf::img::readFileOrThrow(gf::img::joinPath(outDir, QStringLiteral("boot.img"))),
                 bootData);
        QCOMPARE(gf::img::readFileOrThrow(gf::img::joinPath(outDir, QStringLiteral("system.img"))),
                 systemData);

        // 只导出指定分区
        const QString onlyDir = gf::img::joinPath(dir.path(), QStringLiteral("only"));
        gf::img::payload::extractAll(payloadPath, payload, onlyDir, {QStringLiteral("boot")}, false);
        QVERIFY(QFileInfo::exists(gf::img::joinPath(onlyDir, QStringLiteral("boot.img"))));
        QVERIFY(!QFileInfo::exists(gf::img::joinPath(onlyDir, QStringLiteral("system.img"))));
    }

    void rejectsInvalidPayload()
    {
        bool threw = false;
        try {
            gf::img::payload::parse(QByteArray("CrAU not really a payload"));
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
        threw = false;
        try {
            gf::img::payload::parse(QByteArray("NOPE0123456789012345678"));
        } catch (const std::exception& e) {
            threw = QString::fromUtf8(e.what()).contains(QStringLiteral("payload"));
        }
        QVERIFY(threw);
    }

    void missingPartitionFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = gf::img::joinPath(dir.path(), QStringLiteral("parts"));
        gf::img::ensureParentDirectory(gf::img::joinPath(source, QStringLiteral("x")));
        gf::img::writeFileOrThrow(gf::img::joinPath(source, QStringLiteral("boot.img")),
                                  QByteArray(4096, 'B'));
        const QString payloadPath = gf::img::joinPath(dir.path(), QStringLiteral("payload.bin"));
        gf::img::payload::packDirectory(source, {QStringLiteral("boot")}, payloadPath, 4096);
        const auto payload = gf::img::payload::parseFile(payloadPath);
        bool threw = false;
        try {
            gf::img::payload::extractAll(payloadPath, payload,
                                         gf::img::joinPath(dir.path(), QStringLiteral("out")),
                                         {QStringLiteral("vendor")}, false);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }
};

#include "test_payload.moc"
GF_TEST_CLASS(TestPayload)
