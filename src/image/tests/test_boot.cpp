#include <QtTest>

#include "TestRegistry.h"

#include "BootImage.h"
#include "Compression.h"
#include "Cpio.h"

namespace {

QByteArray makeRamdiskCpio()
{
    gf::img::cpio::Archive archive;
    gf::img::cpio::Entry dir;
    dir.name = QStringLiteral("init");
    dir.mode = 0040755u;
    dir.ino = 1;
    archive.entries.append(dir);
    gf::img::cpio::Entry file;
    file.name = QStringLiteral("init/init.rc");
    file.mode = 0100750u;
    file.data = QByteArray("on early-init\n");
    file.ino = 2;
    archive.entries.append(file);
    return gf::img::cpio::serialize(archive);
}

QByteArray makeRamdiskGzip()
{
    return gf::img::compress::gzipCompress(makeRamdiskCpio());
}

QByteArray patternBytes(int size, int seed)
{
    QByteArray data(size, '\0');
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 31 + seed) & 0xFF);
    return data;
}

} // namespace

class TestBoot : public QObject
{
    Q_OBJECT

private slots:
    void isBootImageDetection()
    {
        QVERIFY(gf::img::boot::looksLikeBootImage(QByteArray("ANDROID!").leftJustified(64, '\0')));
        QVERIFY(gf::img::boot::looksLikeBootImage(QByteArray("VNDRBOOT").leftJustified(64, '\0')));
        QVERIFY(!gf::img::boot::looksLikeBootImage(QByteArray("SOMETHING ELSE")));
        QVERIFY(gf::img::boot::isCpio(makeRamdiskCpio()));
        QVERIFY(!gf::img::boot::isCpio(makeRamdiskGzip()));
    }

    void v0RoundTrip()
    {
        gf::img::boot::Image image;
        image.kind = gf::img::boot::ImageKind::Boot;
        image.headerVersion = 0;
        image.pageSize = 2048;
        image.kernel = patternBytes(3000, 1);
        image.ramdisk = makeRamdiskGzip();
        image.second = patternBytes(700, 2);
        image.pageSize = 2048;
        image.kernelAddr = 0x80008000u;
        image.ramdiskAddr = 0x81000000u;
        image.tagsAddr = 0x80000100u;
        image.boardName = QStringLiteral("xmf-test");
        image.cmdline = QStringLiteral("console=ttyMSM0,115200 androidboot.hardware=test");

        const QByteArray encoded = gf::img::boot::serialize(image);
        QVERIFY(encoded.startsWith("ANDROID!"));
        const gf::img::boot::Image parsed = gf::img::boot::parse(encoded);
        QCOMPARE(parsed.headerVersion, 0u);
        QCOMPARE(parsed.pageSize, 2048u);
        QCOMPARE(parsed.kernel, image.kernel);
        QCOMPARE(parsed.ramdisk, image.ramdisk);
        QCOMPARE(parsed.second, image.second);
        QCOMPARE(parsed.cmdline, image.cmdline);
        QCOMPARE(parsed.boardName, image.boardName);
        QVERIFY(parsed.kind == gf::img::boot::ImageKind::Boot);
        // v0 header id 写在 576 偏移处
        QCOMPARE(encoded.mid(576, 8), gf::img::boot::computeId(image));
        QCOMPARE(gf::img::boot::computeId(image).size(), 8);
        // 未压缩 ramdisk 必须能被识别并解压回 cpio
        QCOMPARE(gf::img::boot::decompressRamdisk(parsed.ramdisk), makeRamdiskCpio());
        // 再次打包应字节稳定
        QCOMPARE(gf::img::boot::serialize(parsed), encoded);
    }

    void v2WithDtbAndDtbo()
    {
        gf::img::boot::Image image;
        image.headerVersion = 2;
        image.pageSize = 2048;
        image.kernel = patternBytes(4096 * 2 + 17, 3);
        image.ramdisk = makeRamdiskGzip();
        image.dtb = patternBytes(4096 + 33, 4);
        image.recoveryDtbo = patternBytes(1024, 5);
        image.dtbAddr = 0x81F00000u;
        image.cmdline = QStringLiteral("console=ttyMSM0");

        const QByteArray encoded = gf::img::boot::serialize(image);
        const gf::img::boot::Image parsed = gf::img::boot::parse(encoded);
        QCOMPARE(parsed.headerVersion, 2u);
        QCOMPARE(parsed.kernel, image.kernel);
        QCOMPARE(parsed.ramdisk, image.ramdisk);
        QCOMPARE(parsed.dtb, image.dtb);
        QCOMPARE(parsed.recoveryDtbo, image.recoveryDtbo);
        QCOMPARE(parsed.dtbAddr, image.dtbAddr);
        QCOMPARE(gf::img::boot::serialize(parsed), encoded);
    }

    void v3AndV4RoundTrip()
    {
        for (quint32 version : {3u, 4u}) {
            gf::img::boot::Image image;
            image.headerVersion = version;
            image.pageSize = 4096;
            image.kernel = patternBytes(5000, int(version));
            image.ramdisk = makeRamdiskGzip();
            image.osVersion = 0x0C000000u;
            image.cmdline = QStringLiteral("androidboot.selinux=permissive");
            if (version == 4)
                image.signature = patternBytes(256, 6);

            const QByteArray encoded = gf::img::boot::serialize(image);
            const gf::img::boot::Image parsed = gf::img::boot::parse(encoded);
            QCOMPARE(parsed.headerVersion, version);
            QCOMPARE(parsed.pageSize, 4096u);
            QCOMPARE(parsed.kernel, image.kernel);
            QCOMPARE(parsed.ramdisk, image.ramdisk);
            QCOMPARE(parsed.cmdline, image.cmdline);
            QCOMPARE(parsed.osVersion, image.osVersion);
            if (version == 4)
                QCOMPARE(parsed.signature, image.signature);
            QCOMPARE(gf::img::boot::serialize(parsed), encoded);
            // 头长度固定
            QCOMPARE(parsed.headerSize, version == 4 ? 1584u : 1580u);
        }
    }

    void initBootDetection()
    {
        gf::img::boot::Image image;
        image.kind = gf::img::boot::ImageKind::InitBoot;
        image.headerVersion = 4;
        image.pageSize = 4096;
        image.ramdisk = makeRamdiskGzip();
        const QByteArray encoded = gf::img::boot::serialize(image);
        const gf::img::boot::Image parsed = gf::img::boot::parse(encoded);
        QVERIFY(parsed.kind == gf::img::boot::ImageKind::InitBoot);
        QVERIFY(parsed.kernel.isEmpty());
        QCOMPARE(parsed.ramdisk, image.ramdisk);
    }

    void vendorBootRoundTrip()
    {
        for (quint32 version : {3u, 4u}) {
            gf::img::boot::Image image;
            image.kind = gf::img::boot::ImageKind::VendorBoot;
            image.headerVersion = version;
            image.pageSize = 4096;
            image.kernelAddr = 0x80008000u;
            image.ramdiskAddr = 0x81000000u;
            image.tagsAddr = 0x80000100u;
            image.dtbAddr = 0x81F00000u;
            image.cmdline = QStringLiteral("androidboot.hardware=qcom");
            image.boardName = QStringLiteral("vendor");
            image.dtb = patternBytes(2048, 8);
            if (version >= 4) {
                gf::img::boot::VendorFragment platform;
                platform.type = 1;
                platform.name = QStringLiteral("platform");
                platform.data = makeRamdiskGzip();
                gf::img::boot::VendorFragment dlkm;
                dlkm.type = 3;
                dlkm.name = QStringLiteral("dlkm");
                dlkm.data = patternBytes(1000, 9);
                image.vendorRamdisks = {platform, dlkm};
                image.bootconfig = QByteArray("androidboot.foo=1\n");
            } else {
                gf::img::boot::VendorFragment single;
                single.type = 1;
                single.data = makeRamdiskGzip();
                image.vendorRamdisks = {single};
            }

            const QByteArray encoded = gf::img::boot::serialize(image);
            QVERIFY(encoded.startsWith("VNDRBOOT"));
            const gf::img::boot::Image parsed = gf::img::boot::parse(encoded);
            QVERIFY(parsed.kind == gf::img::boot::ImageKind::VendorBoot);
            QCOMPARE(parsed.headerVersion, version);
            QCOMPARE(parsed.cmdline, image.cmdline);
            QCOMPARE(parsed.boardName, image.boardName);
            QCOMPARE(parsed.dtb, image.dtb);
            QCOMPARE(parsed.vendorRamdisks.size(), image.vendorRamdisks.size());
            for (int i = 0; i < image.vendorRamdisks.size(); ++i) {
                QCOMPARE(parsed.vendorRamdisks.at(i).type, image.vendorRamdisks.at(i).type);
                if (version >= 4)
                    QCOMPARE(parsed.vendorRamdisks.at(i).name, image.vendorRamdisks.at(i).name);
                QCOMPARE(parsed.vendorRamdisks.at(i).data, image.vendorRamdisks.at(i).data);
            }
            if (version >= 4)
                QCOMPARE(parsed.bootconfig, image.bootconfig);
            QCOMPARE(gf::img::boot::serialize(parsed), encoded);
        }
    }

    void ramdiskDecompressHelpers()
    {
        const QByteArray cpio = makeRamdiskCpio();
        QString format;
        QCOMPARE(gf::img::boot::decompressRamdisk(cpio, &format), cpio);
        QVERIFY(format.contains(QStringLiteral("未知")));
        const QByteArray compressed = gf::img::boot::compressRamdisk(cpio);
        QVERIFY(gf::img::compress::isGzip(compressed));
        QCOMPARE(gf::img::boot::decompressRamdisk(compressed), cpio);

        bool threw = false;
        try {
            gf::img::boot::decompressRamdisk(QByteArray(64, 'Q'));
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void rejectsBrokenImages()
    {
        bool threw = false;
        try {
            gf::img::boot::parse(QByteArray(100, '\0'));
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);

        // 头部声明超出文件大小
        gf::img::boot::Image image;
        image.headerVersion = 3;
        image.pageSize = 4096;
        image.kernel = patternBytes(4096, 1);
        image.ramdisk = QByteArray(100, 'R');
        QByteArray encoded = gf::img::boot::serialize(image);
        encoded.truncate(4096 + 100);
        threw = false;
        try {
            gf::img::boot::parse(encoded);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }
};

#include "test_boot.moc"
GF_TEST_CLASS(TestBoot)
