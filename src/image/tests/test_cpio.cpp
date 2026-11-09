#include <QtTest>

#include "TestRegistry.h"

#include "Cpio.h"

#include <QDir>
#include <QTemporaryDir>

class TestCpio : public QObject
{
    Q_OBJECT

private slots:
    void serializeParseRoundTrip()
    {
        gf::img::cpio::Archive archive;
        gf::img::cpio::Entry dir;
        dir.name = QStringLiteral("init");
        dir.mode = 0040755u;
        dir.ino = 1;
        dir.mtime = 1700000000;
        archive.entries.append(dir);

        gf::img::cpio::Entry file;
        file.name = QStringLiteral("init/init.rc");
        file.mode = 0100644u;
        file.data = QByteArray("on boot\n    start zygote\n");
        file.ino = 2;
        file.uid = 1000;
        file.gid = 1000;
        file.mtime = 1700000001;
        archive.entries.append(file);

        gf::img::cpio::Entry link;
        link.name = QStringLiteral("init/link");
        link.mode = 0120777u;
        link.data = QByteArray("init.rc");
        link.ino = 3;
        archive.entries.append(link);

        const QByteArray encoded = gf::img::cpio::serialize(archive);
        QVERIFY(encoded.startsWith("070701"));
        QCOMPARE(encoded.size() % 4, 0);

        const gf::img::cpio::Archive parsed = gf::img::cpio::parse(encoded);
        QCOMPARE(parsed.entries.size(), archive.entries.size());
        for (int i = 0; i < archive.entries.size(); ++i) {
            QCOMPARE(parsed.entries.at(i).name, archive.entries.at(i).name);
            QCOMPARE(parsed.entries.at(i).mode, archive.entries.at(i).mode);
            QCOMPARE(parsed.entries.at(i).data, archive.entries.at(i).data);
            QCOMPARE(parsed.entries.at(i).ino, archive.entries.at(i).ino);
            QCOMPARE(parsed.entries.at(i).mtime, archive.entries.at(i).mtime);
        }
        QVERIFY(parsed.entries.at(0).isDir());
        QVERIFY(parsed.entries.at(1).isRegular());
        QVERIFY(parsed.entries.at(2).isSymlink());
        QCOMPARE(parsed.entries.at(1).permissionText(), QStringLiteral("rw-r--r--"));

        // 再次序列化必须字节一致（保证 pack→parse 幂等）。
        QCOMPARE(gf::img::cpio::serialize(parsed), encoded);
    }

    void crcArchiveRoundTrip()
    {
        gf::img::cpio::Archive archive;
        gf::img::cpio::Entry file;
        file.name = QStringLiteral("a.txt");
        file.mode = 0100644u;
        file.data = QByteArray("hello cpio");
        archive.entries.append(file);
        archive.crcMode = true;
        const QByteArray encoded = gf::img::cpio::serialize(archive);
        QVERIFY(encoded.startsWith("070702"));
        const gf::img::cpio::Archive parsed = gf::img::cpio::parse(encoded);
        QCOMPARE(parsed.entries.size(), 1);
        QCOMPARE(parsed.entries.first().data, QByteArray("hello cpio"));
        QVERIFY(parsed.crcMode);
    }

    void directoryArchiveAndExtract()
    {
        QTemporaryDir source;
        QVERIFY(source.isValid());
        const QString nested = gf::img::joinPath(source.path(), QStringLiteral("sbin"));
        gf::img::ensureParentDirectory(gf::img::joinPath(nested, QStringLiteral("x")));
        gf::img::writeFileOrThrow(gf::img::joinPath(source.path(), QStringLiteral("init.rc")),
                                  QByteArray("service adbd /sbin/adbd\n"));
        gf::img::writeFileOrThrow(gf::img::joinPath(nested, QStringLiteral("adbd")),
                                  QByteArray("ELF fake binary"));

        const gf::img::cpio::Archive archive = gf::img::cpio::fromDirectory(source.path());
        const QByteArray encoded = gf::img::cpio::serialize(archive);
        const gf::img::cpio::Archive parsed = gf::img::cpio::parse(encoded);

        bool sawDir = false;
        bool sawNested = false;
        for (const auto& entry : parsed.entries) {
            if (entry.name == QStringLiteral("sbin") && entry.isDir())
                sawDir = true;
            if (entry.name == QStringLiteral("sbin/adbd"))
                sawNested = true;
        }
        QVERIFY(sawDir);
        QVERIFY(sawNested);

        QTemporaryDir target;
        QVERIFY(target.isValid());
        gf::img::cpio::extractTo(parsed, target.path());
        QCOMPARE(gf::img::readFileOrThrow(gf::img::joinPath(target.path(), QStringLiteral("init.rc"))),
                 QByteArray("service adbd /sbin/adbd\n"));
        QCOMPARE(gf::img::readFileOrThrow(
                     gf::img::joinPath(target.path(), QStringLiteral("sbin/adbd"))),
                 QByteArray("ELF fake binary"));
    }

    void modifyEntries()
    {
        gf::img::cpio::Archive archive;
        gf::img::cpio::Entry file;
        file.name = QStringLiteral("init.rc");
        file.mode = 0100644u;
        file.data = QByteArray("old");
        archive.entries.append(file);

        gf::img::cpio::putFile(archive, QStringLiteral("vendor/bin/tool"),
                               QByteArray("tool-binary"), 0100755u);
        QCOMPARE(archive.entries.size(), 2);

        // 同名 putFile 应替换而不是追加。
        gf::img::cpio::putFile(archive, QStringLiteral("init.rc"), QByteArray("new"));
        QCOMPARE(archive.entries.size(), 2);
        QCOMPARE(archive.entries.first().data, QByteArray("new"));

        bool ok = false;
        const quint32 mode = gf::img::cpio::parseModeText(QStringLiteral("0755"), &ok);
        QVERIFY(ok);
        QCOMPARE(mode, 0755u);
        gf::img::cpio::setPermissions(archive, QStringLiteral("init.rc"), mode, 0, 0, false);
        const gf::img::cpio::Archive parsed =
            gf::img::cpio::parse(gf::img::cpio::serialize(archive));
        quint32 parsedMode = 0;
        for (const auto& entry : parsed.entries) {
            if (entry.name == QStringLiteral("init.rc"))
                parsedMode = entry.mode;
        }
        QCOMPARE(parsedMode & 07777u, 0755u);

        QVERIFY(gf::img::cpio::removeEntry(archive, QStringLiteral("vendor"), true));
        QCOMPARE(archive.entries.size(), 1);
        QVERIFY(!gf::img::cpio::removeEntry(archive, QStringLiteral("not-there"), true));
        QCOMPARE(gf::img::cpio::formatMode(0100755u), QStringLiteral("0100755"));
    }

    void rejectsBrokenData()
    {
        bool threw = false;
        try {
            gf::img::cpio::parse(QByteArray("not-a-cpio-archive"));
        } catch (const std::exception& e) {
            threw = QString::fromUtf8(e.what()).contains(QStringLiteral("cpio"));
        }
        QVERIFY(threw);

        gf::img::cpio::Archive archive;
        gf::img::cpio::Entry file;
        file.name = QStringLiteral("x");
        file.mode = 0100644u;
        file.data = QByteArray(100, 'x');
        archive.entries.append(file);
        QByteArray encoded = gf::img::cpio::serialize(archive);
        encoded.truncate(encoded.size() - 40);
        threw = false;
        try {
            gf::img::cpio::parse(encoded);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }
};

#include "test_cpio.moc"
GF_TEST_CLASS(TestCpio)
