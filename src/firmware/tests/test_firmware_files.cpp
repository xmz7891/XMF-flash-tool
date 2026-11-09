#include <QtTest>

#include "TestRegistry.h"

#include "FirmwareCommon.h"
#include "FirmwareFiles.h"

#include "image/Compression.h"
#include "image/ImageCommon.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {

void put16(QByteArray& out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void put32(QByteArray& out, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        out.append(char((value >> (8 * i)) & 0xFF));
}

void putLe32At(QByteArray& data, qsizetype at, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        data[at + i] = char((value >> (8 * i)) & 0xFF);
}

/// 生成一个只含 stored 条目的最小 zip（足够测试解包路径）。
QByteArray buildZip(const QVector<QPair<QString, QByteArray>>& entries)
{
    QByteArray body;
    QByteArray central;
    for (const auto& entry : entries) {
        const QString name = entry.first;
        const QByteArray data = entry.second;
        const QByteArray nameBytes = name.toUtf8();
        const quint32 crc = gf::img::crc32(data);
        const quint32 offset = quint32(body.size());
        put32(body, 0x04034b50);
        put16(body, 20);
        put16(body, 0);
        put16(body, 0); // stored
        put16(body, 0);
        put16(body, 0x21);
        put32(body, crc);
        put32(body, quint32(data.size()));
        put32(body, quint32(data.size()));
        put16(body, quint16(nameBytes.size()));
        put16(body, 0);
        body.append(nameBytes);
        body.append(data);

        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0x21);
        put32(central, crc);
        put32(central, quint32(data.size()));
        put32(central, quint32(data.size()));
        put16(central, quint16(nameBytes.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central.append(nameBytes);
    }
    QByteArray out = body;
    const quint32 cdOffset = quint32(out.size());
    out.append(central);
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(entries.size()));
    put16(out, quint16(entries.size()));
    put32(out, quint32(central.size()));
    put32(out, cdOffset);
    put16(out, 0);
    return out;
}

void putOctalField(QByteArray& header, int offset, int length, quint64 value)
{
    QByteArray digits = QByteArray::number(value, 8).rightJustified(length - 1, '0');
    header.replace(offset, digits.size(), digits);
    header[offset + length - 1] = '\0';
}

QByteArray tarEntry(const QString& name, const QByteArray& data, char type = '0',
                    quint32 mode = 0644)
{
    QByteArray header(512, '\0');
    const QByteArray nameBytes = name.toUtf8().left(100);
    header.replace(0, nameBytes.size(), nameBytes);
    putOctalField(header, 100, 8, mode);
    putOctalField(header, 108, 8, 0);
    putOctalField(header, 116, 8, 0);
    putOctalField(header, 124, 12, quint64(data.size()));
    putOctalField(header, 136, 12, 1700000000);
    header[156] = type;
    header.replace(257, 6, QByteArray("ustar\0", 6));
    header.replace(263, 2, QByteArray("00", 2));
    QByteArray out = header;
    out.append(data);
    const qsizetype pad = (512 - (data.size() % 512)) % 512;
    out.append(QByteArray(pad, '\0'));
    return out;
}

QByteArray buildTar()
{
    QByteArray tar;
    tar.append(tarEntry(QStringLiteral("images/"), QByteArray(), '5', 0755));
    tar.append(tarEntry(QStringLiteral("images/boot.img"), QByteArray("BOOT-IMAGE-DATA")));
    tar.append(tarEntry(QStringLiteral("flash.bat"), QByteArray("echo flash")));
    tar.append(QByteArray(1024, '\0'));
    return tar;
}

QByteArray buildPac(const QVector<QPair<QString, QByteArray>>& images)
{
    constexpr int kHeaderSize = 0x100;
    constexpr int kTableOffset = 0x20;
    constexpr int kEntrySize = 0x40;
    qsizetype dataStart = kHeaderSize;
    for (const auto& image : images)
        dataStart += image.second.size();
    QByteArray pac(qsizetype(kHeaderSize), '\0');
    // 预留数据区
    pac.resize(qsizetype(dataStart));
    pac.replace(0, 4, QByteArray("PAC\0", 4));
    putLe32At(pac, 4, 1);
    putLe32At(pac, 8, kHeaderSize);
    putLe32At(pac, 0x0C, quint32(images.size()));
    putLe32At(pac, 0x10, kTableOffset);
    putLe32At(pac, 0x14, quint32(pac.size()));

    qint64 offset = kHeaderSize;
    for (int i = 0; i < images.size(); ++i) {
        const qsizetype base = kTableOffset + qsizetype(i) * kEntrySize;
        putLe32At(pac, base + 0x00, quint32(i == 0 ? 1 : 0)); // type
        putLe32At(pac, base + 0x04, quint32(offset));
        putLe32At(pac, base + 0x08, quint32(images.at(i).second.size()));
        putLe32At(pac, base + 0x0C, 0);
        const QByteArray name = images.at(i).first.toUtf8().left(32);
        pac.replace(base + 0x10, name.size(), name);
        pac.replace(qsizetype(offset), images.at(i).second.size(), images.at(i).second);
        offset += images.at(i).second.size();
    }
    return pac;
}

QByteArray readFileBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class TestFirmwareFiles : public QObject
{
    Q_OBJECT

private slots:
    void identifyCommonTypes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto write = [&](const QString& name, const QByteArray& data) {
            const QString path = gf::firmware::ensureDirectory(dir.path())
                                     ? QDir(dir.path()).filePath(name)
                                     : QString();
            QFile file(path);
            file.open(QIODevice::WriteOnly | QIODevice::Truncate);
            file.write(data);
            return path;
        };

        const QString zipPath = write(QStringLiteral("fw.zip"), buildZip({{QStringLiteral("a.txt"),
                                                                            QByteArray("hello")}}));
        QCOMPARE(gf::firmware::identifyFirmware(zipPath).kind,
                 gf::firmware::FirmwareKind::Zip);
        QVERIFY(gf::firmware::kindIsUnpackable(gf::firmware::FirmwareKind::Zip));

        const QString tgzPath = write(QStringLiteral("fw.tgz"),
                                      gf::img::compress::gzipCompress(buildTar()));
        QCOMPARE(gf::firmware::identifyFirmware(tgzPath).kind,
                 gf::firmware::FirmwareKind::TarGz);

        const QString pacPath = write(QStringLiteral("fw.pac"),
                                      buildPac({{QStringLiteral("fdl1.bin"), QByteArray("FDL1")}}));
        QCOMPARE(gf::firmware::identifyFirmware(pacPath).kind,
                 gf::firmware::FirmwareKind::Pac);

        // raw 镜像 + magic 线索
        QByteArray sparse(4096, '\0');
        sparse[0] = char(0x3A);
        sparse[1] = char(0xFF);
        sparse[2] = char(0x26);
        sparse[3] = char(0xED);
        const QString sparsePath = write(QStringLiteral("system.img"), sparse);
        const auto sparseInfo = gf::firmware::identifyFirmware(sparsePath);
        QCOMPARE(sparseInfo.kind, gf::firmware::FirmwareKind::RawImage);
        QVERIFY(sparseInfo.hints.join(QLatin1Char(' ')).contains(QStringLiteral("sparse")));

        const QString bootPath = write(QStringLiteral("boot.img"),
                                       QByteArray("ANDROID!").leftJustified(2048, '\0'));
        const auto bootInfo = gf::firmware::identifyFirmware(bootPath);
        QCOMPARE(bootInfo.kind, gf::firmware::FirmwareKind::RawImage);
        QVERIFY(bootInfo.hints.join(QLatin1Char(' ')).contains(QStringLiteral("boot")));

        const QString ozipPath = write(QStringLiteral("fw.ozip"), QByteArray("OZIP-DATA"));
        QCOMPARE(gf::firmware::identifyFirmware(ozipPath).kind,
                 gf::firmware::FirmwareKind::Ozip);
        QCOMPARE(gf::firmware::identifyFirmware(write(QStringLiteral("fw.ofp"), QByteArray("OFP"))).kind,
                 gf::firmware::FirmwareKind::Ofp);
        QCOMPARE(gf::firmware::identifyFirmware(
                     write(QStringLiteral("fw.gz"), gf::img::compress::gzipCompress("data")))
                     .kind,
                 gf::firmware::FirmwareKind::Gzip);
        QCOMPARE(gf::firmware::identifyFirmware(
                     write(QStringLiteral("fw.elf"),
                           QByteArray("\x7F" "ELF", 4).leftJustified(512, '\0')))
                     .kind,
                 gf::firmware::FirmwareKind::Elf);
        QCOMPARE(gf::firmware::identifyFirmware(
                     write(QStringLiteral("fw.md5"),
                           QByteArray("a4bd134f029bd7d93d1cf595a4edb60a  fw.zip\n")))
                     .kind,
                 gf::firmware::FirmwareKind::Md5File);
    }

    void identifyFindsHashSidecar()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString zipPath = QDir(dir.path()).filePath(QStringLiteral("rom.zip"));
        QFile zip(zipPath);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        zip.write(buildZip({{QStringLiteral("a.txt"), QByteArray("x")}}));
        zip.close();
        const QString sidecar = zipPath + QStringLiteral(".md5");
        QFile md5(sidecar);
        QVERIFY(md5.open(QIODevice::WriteOnly));
        md5.write(QByteArray("a4bd134f029bd7d93d1cf595a4edb60a  rom.zip\n"));
        md5.close();
        const auto info = gf::firmware::identifyFirmware(zipPath);
        QCOMPARE(info.hashSidecar, sidecar);
    }

    void unpackZip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString zipPath = QDir(dir.path()).filePath(QStringLiteral("ota.zip"));
        const QByteArray zipBytes = buildZip({{QStringLiteral("payload.bin"),
                                               QByteArray("payload-data")},
                                              {QStringLiteral("META-INF/"), QByteArray()},
                                              {QStringLiteral("META-INF/updater-script"),
                                               QByteArray("ui_print(\"hi\");\n")}});
        QFile zip(zipPath);
        QVERIFY(zip.open(QIODevice::WriteOnly));
        zip.write(zipBytes);
        zip.close();

        const QString outDir = QDir(dir.path()).filePath(QStringLiteral("out"));
        const auto result = gf::firmware::unpackFirmware(zipPath, outDir);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.kindText, QStringLiteral("zip 压缩包"));
        QVERIFY(result.filesExtracted >= 2);
        QCOMPARE(readFileBytes(QDir(outDir).filePath(QStringLiteral("payload.bin"))),
                 QByteArray("payload-data"));
        QVERIFY(QFileInfo::exists(QDir(outDir).filePath(QStringLiteral("META-INF"))));
        QCOMPARE(readFileBytes(QDir(outDir).filePath(QStringLiteral("META-INF/updater-script"))),
                 QByteArray("ui_print(\"hi\");\n"));
    }

    void unpackTarGz()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("fastboot.tgz"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(gf::img::compress::gzipCompress(buildTar()));
        file.close();

        const QString outDir = QDir(dir.path()).filePath(QStringLiteral("out"));
        const auto result = gf::firmware::unpackFirmware(path, outDir);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.kindText, QStringLiteral("tar.gz / tgz 线刷包"));
        QCOMPARE(readFileBytes(QDir(outDir).filePath(QStringLiteral("images/boot.img"))),
                 QByteArray("BOOT-IMAGE-DATA"));
        QCOMPARE(readFileBytes(QDir(outDir).filePath(QStringLiteral("flash.bat"))),
                 QByteArray("echo flash"));
        QVERIFY(result.dirsCreated >= 1);
    }

    void unpackPac()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray pac = buildPac({{QStringLiteral("fdl1.bin"), QByteArray("FDL1DATA")},
                                         {QStringLiteral("boot.img"), QByteArray("BOOTDATA")}});
        const QString path = QDir(dir.path()).filePath(QStringLiteral("fw.pac"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pac);
        file.close();

        const QString outDir = QDir(dir.path()).filePath(QStringLiteral("out"));
        const auto result = gf::firmware::unpackFirmware(path, outDir);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(readFileBytes(QDir(outDir).filePath(QStringLiteral("fdl1.bin"))),
                 QByteArray("FDL1DATA"));
        QCOMPARE(readFileBytes(QDir(outDir).filePath(QStringLiteral("boot.img"))),
                 QByteArray("BOOTDATA"));
        QVERIFY(result.filesExtracted >= 2);
    }

    void unpackUnsupportedType()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("fw.ozip"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("OZIP"));
        file.close();
        const auto result = gf::firmware::unpackFirmware(
            path, QDir(dir.path()).filePath(QStringLiteral("out")));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("OPPO")));
    }

    void splitPartsDetectionAndMerge()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto write = [&](const QString& name, const QByteArray& data) {
            const QString path = QDir(dir.path()).filePath(name);
            QFile file(path);
            file.open(QIODevice::WriteOnly | QIODevice::Truncate);
            file.write(data);
            return path;
        };

        // 先写 .zip 主文件，再写分卷，验证合并顺序为 z01 -> z02 -> zip。
        write(QStringLiteral("rom.zip"), QByteArray("ZZZ"));
        write(QStringLiteral("rom.z01"), QByteArray("AAAA"));
        write(QStringLiteral("rom.z02"), QByteArray("BB"));
        const auto parts = gf::firmware::findSplitParts(QDir(dir.path()).filePath(QStringLiteral("rom.zip")));
        QCOMPARE(parts.size(), 3);
        QCOMPARE(QFileInfo(parts.at(0)).fileName(), QStringLiteral("rom.z01"));
        QCOMPARE(QFileInfo(parts.at(1)).fileName(), QStringLiteral("rom.z02"));
        QCOMPARE(QFileInfo(parts.at(2)).fileName(), QStringLiteral("rom.zip"));

        const QString merged = QDir(dir.path()).filePath(QStringLiteral("rom_merged.zip"));
        const auto result = gf::firmware::mergeSplitFirmware(
            QDir(dir.path()).filePath(QStringLiteral("rom.z01")), merged);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(readFileBytes(merged), QByteArray("AAAABBZZZ"));
        QCOMPARE(result.parts.size(), 3);

        // 数字分卷
        write(QStringLiteral("fw.001"), QByteArray("111"));
        write(QStringLiteral("fw.002"), QByteArray("222"));
        const auto numericParts =
            gf::firmware::findSplitParts(QDir(dir.path()).filePath(QStringLiteral("fw.001")));
        QCOMPARE(numericParts.size(), 2);
        const QString numericMerged = QDir(dir.path()).filePath(QStringLiteral("fw_merged.bin"));
        QVERIFY(gf::firmware::mergeSplitFirmware(QDir(dir.path()).filePath(QStringLiteral("fw.002")),
                                                 numericMerged)
                    .ok);
        QCOMPARE(readFileBytes(numericMerged), QByteArray("111222"));
    }

    void hashVerification()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray data;
        for (int i = 0; i < 100000; ++i)
            data.append(char((i * 17) & 0xFF));
        const QString path = QDir(dir.path()).filePath(QStringLiteral("fw.bin"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();

        const auto hashes = gf::firmware::hashFile(path);
        QCOMPARE(hashes.size, qint64(data.size()));
        QCOMPARE(hashes.md5.size(), 32);
        QCOMPARE(hashes.sha256.size(), 64);

        QString actual;
        QString error;
        QVERIFY(gf::firmware::verifyFileHash(path, hashes.md5, &actual, &error));
        QCOMPARE(actual, hashes.md5);
        QVERIFY(gf::firmware::verifyFileHash(path, hashes.sha256));
        QVERIFY(!gf::firmware::verifyFileHash(path, QByteArray(32, '0'), &actual, &error));
        QVERIFY(!error.isEmpty());

        QCOMPARE(gf::firmware::sizeTextToBytes(QStringLiteral("1.5 GB")), qint64(1.5 * 1024 * 1024 * 1024));
    }
};

#include "test_firmware_files.moc"
GF_TEST_CLASS(TestFirmwareFiles)
