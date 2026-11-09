#include <QtTest>

#include "TestRegistry.h"

#include "Compression.h"
#include "ImageCommon.h"
#include "ZipPackage.h"

#include <QTemporaryDir>

namespace {

struct ZipItem {
    QString name;
    QByteArray data;        // 原始数据
    QByteArray compressed;  // 实际写入 zip 的数据
    quint16 method = 0;
};

void put16(QByteArray& out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void put32(QByteArray& out, quint32 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}

QByteArray buildZip(const QVector<ZipItem>& items)
{
    QByteArray body;
    QByteArray central;
    for (const auto& item : items) {
        const quint32 offset = quint32(body.size());
        const QByteArray name = item.name.toUtf8();
        const quint32 crc = gf::img::crc32(item.data);
        const quint32 compSize = quint32(item.compressed.size());
        const quint32 uncompSize = quint32(item.data.size());
        put32(body, 0x04034b50);
        put16(body, 20); // version needed
        put16(body, 0);  // flags
        put16(body, item.method);
        put16(body, 0);      // time
        put16(body, 0x21);   // date
        put32(body, crc);
        put32(body, compSize);
        put32(body, uncompSize);
        put16(body, quint16(name.size()));
        put16(body, 0); // extra
        body.append(name);
        body.append(item.compressed);

        put32(central, 0x02014b50);
        put16(central, 20); // version made by
        put16(central, 20); // version needed
        put16(central, 0);  // flags
        put16(central, item.method);
        put16(central, 0);
        put16(central, 0x21);
        put32(central, crc);
        put32(central, compSize);
        put32(central, uncompSize);
        put16(central, quint16(name.size()));
        put16(central, 0); // extra
        put16(central, 0); // comment
        put16(central, 0); // disk
        put16(central, 0); // internal attr
        put32(central, 0); // external attr
        put32(central, offset);
        central.append(name);
    }
    QByteArray out = body;
    const quint32 cdOffset = quint32(out.size());
    out.append(central);
    put32(out, 0x06054b50);
    put16(out, 0);                    // disk
    put16(out, 0);                    // cd disk
    put16(out, quint16(items.size())); // entries on disk
    put16(out, quint16(items.size())); // total entries
    put32(out, quint32(central.size()));
    put32(out, cdOffset);
    put16(out, 0); // comment length
    return out;
}

QByteArray makeUpdaterScript()
{
    return QByteArray(
        "show_progress(0.1, 0);\n"
        "ui_print(\"XMF test rom\");\n"
        "mount(\"ext4\", \"EMMC\", \"/dev/block/bootdevice/by-name/system\", \"/system\");\n"
        "package_extract_file(\"boot.img\", \"/dev/block/bootdevice/by-name/boot\");\n"
        "package_extract_file(\"system.img\", \"/dev/block/bootdevice/by-name/system\");\n"
        "package_extract_dir(\"META-INF/com/google/android\", \"/tmp\");\n"
        "assert(getprop(\"ro.product.device\") == \"xmf\");\n"
        "format(\"ext4\", \"EMMC\", \"/dev/block/bootdevice/by-name/userdata\");\n"
        "set_perm(0, 0, 0644, \"/system/build.prop\");\n");
}

QVector<ZipItem> makePackage(const QByteArray& script, const QByteArray& boot, const QByteArray& system)
{
    QVector<ZipItem> items;
    ZipItem dir;
    dir.name = QStringLiteral("META-INF/com/google/android/");
    items.append(dir);
    ZipItem updater;
    updater.name = QStringLiteral("META-INF/com/google/android/updater-script");
    updater.data = script;
    updater.compressed = script;
    items.append(updater);
    ZipItem bootItem;
    bootItem.name = QStringLiteral("boot.img");
    bootItem.data = boot;
    bootItem.method = 8;
    bootItem.compressed = gf::img::compress::deflateRaw(boot, 6);
    items.append(bootItem);
    ZipItem systemItem;
    systemItem.name = QStringLiteral("system.img");
    systemItem.data = system;
    systemItem.compressed = system;
    items.append(systemItem);
    return items;
}

} // namespace

class TestZip : public QObject
{
    Q_OBJECT

private slots:
    void parseAndReadEntries()
    {
        QByteArray boot(6000, '\0');
        for (int i = 0; i < boot.size(); ++i)
            boot[i] = char((i * 19) & 0xFF);
        const QByteArray system(5000, char(0x77));
        const QByteArray script = makeUpdaterScript();
        const QByteArray zipBytes = buildZip(makePackage(script, boot, system));
        QVERIFY(gf::img::zip::isZip(zipBytes));

        const gf::img::zip::Archive archive = gf::img::zip::parse(zipBytes);
        QCOMPARE(archive.entries.size(), 4);
        QVERIFY(archive.hasEndRecord);
        QVERIFY(archive.entries.first().isDirectory());
        QCOMPARE(archive.entries.at(2).name, QStringLiteral("boot.img"));
        QCOMPARE(archive.entries.at(2).methodText(), QStringLiteral("deflate"));
        QVERIFY(archive.entries.at(2).isImage());

        QCOMPARE(gf::img::zip::readEntryByName(
                     archive, QStringLiteral("META-INF/com/google/android/updater-script")),
                 script);
        QCOMPARE(gf::img::zip::readEntry(archive, archive.entries.at(2)), boot);
        QCOMPARE(gf::img::zip::readEntry(archive, archive.entries.at(3)), system);

        const QStringList images = gf::img::zip::imageNames(archive);
        QCOMPARE(images.size(), 2);
        QVERIFY(images.contains(QStringLiteral("boot.img")));
        QVERIFY(images.contains(QStringLiteral("system.img")));
        QVERIFY(gf::img::zip::verify(archive).isEmpty());
    }

    void updaterScriptParsing()
    {
        const gf::img::zip::UpdaterInfo info =
            gf::img::zip::parseUpdaterScript(makeUpdaterScript());
        QVERIFY(info.packageExtractFile.contains(QStringLiteral("boot.img")));
        QVERIFY(info.packageExtractFile.contains(QStringLiteral("system.img")));
        QCOMPARE(info.packageExtractDir.size(), 1);
        QVERIFY(info.uiPrint.join(QLatin1Char(' ')).contains(QStringLiteral("XMF test rom")));
        QCOMPARE(info.mountPoints.size(), 1);
        QCOMPARE(info.formatTargets.size(), 1);
        QCOMPARE(info.asserts.size(), 1);
        QCOMPARE(info.setPerm.size(), 1);
        const QStringList summary = gf::img::zip::updaterSummary(info);
        QVERIFY(summary.size() >= 3);
    }

    void corruptEntryDetected()
    {
        const QByteArray script = makeUpdaterScript();
        const QByteArray boot(1024, char(0x22));
        const QByteArray system(2048, char(0x33));
        QByteArray zipBytes = buildZip(makePackage(script, boot, system));
        const gf::img::zip::Archive good = gf::img::zip::parse(zipBytes);

        // 破坏 system.img 的本地数据（位于本地头之后，直接按偏移改动）
        const auto& systemEntry = good.entries.at(3);
        const qsizetype localAt = qsizetype(systemEntry.localHeaderOffset);
        const quint16 nameLen = quint16(quint8(zipBytes.at(localAt + 26)) |
                                        (quint8(zipBytes.at(localAt + 27)) << 8));
        const quint16 extraLen = quint16(quint8(zipBytes.at(localAt + 28)) |
                                         (quint8(zipBytes.at(localAt + 29)) << 8));
        const qsizetype dataAt = localAt + 30 + nameLen + extraLen;
        zipBytes[dataAt + 10] = char(quint8(zipBytes.at(dataAt + 10)) ^ 0x5A);

        const gf::img::zip::Archive corrupted = gf::img::zip::parse(zipBytes);
        const QStringList problems = gf::img::zip::verify(corrupted);
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().contains(QStringLiteral("system.img")));
    }

    void openFromFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray script = makeUpdaterScript();
        const QByteArray boot(4096, char(0x44));
        const QByteArray zipBytes = buildZip(makePackage(script, boot, QByteArray(100, 's')));
        const QString path = gf::img::joinPath(dir.path(), QStringLiteral("rom.zip"));
        gf::img::writeFileOrThrow(path, zipBytes);

        const gf::img::zip::Archive archive = gf::img::zip::open(path);
        QCOMPARE(archive.fileSize, qint64(zipBytes.size()));
        QCOMPARE(archive.entries.size(), 4);
        QCOMPARE(gf::img::zip::readEntryByName(archive, QStringLiteral("boot.img")), boot);
        QVERIFY(gf::img::zip::verify(archive).isEmpty());
    }

    void rejectsInvalidZip()
    {
        bool threw = false;
        try {
            gf::img::zip::parse(QByteArray("this is not a zip file"));
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
        QVERIFY(!gf::img::zip::isZip(QByteArray("nope")));
    }
};

#include "test_zip.moc"
GF_TEST_CLASS(TestZip)
