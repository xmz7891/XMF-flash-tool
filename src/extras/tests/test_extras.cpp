#include <QtTest>

#include <stdexcept>

#include "TestRegistry.h"

#include "app/ToolModules.h"
#include "core/ToolRegistry.h"
#include "core/Util.h"

#include "BootImage.h"
#include "Compression.h"
#include "Cpio.h"
#include "ImageCommon.h"

#include "BypassSupport.h"
#include "BootloaderSupport.h"
#include "ExtrasUtils.h"
#include "Lz4Codec.h"
#include "RootImage.h"
#include "RootSupport.h"

#include "image/BootImage.h"

using namespace gf::extras;

namespace {

QByteArray makeCpioRamdisk(const QByteArray& initData = QByteArrayLiteral("ORIGINAL-INIT"))
{
    gf::img::cpio::Archive archive;
    gf::img::cpio::Entry init;
    init.name = QStringLiteral("init");
    init.mode = 0100750u;
    init.ino = 1;
    init.data = initData;
    archive.entries.append(init);

    gf::img::cpio::Entry prop;
    prop.name = QStringLiteral("default.prop");
    prop.mode = 0100644u;
    prop.ino = 2;
    prop.data = QByteArrayLiteral("ro.a=1\nro.b=2\n");
    archive.entries.append(prop);

    gf::img::cpio::Entry dir;
    dir.name = QStringLiteral("system");
    dir.mode = 0040755u;
    dir.ino = 3;
    archive.entries.append(dir);
    return gf::img::cpio::serialize(archive);
}

QByteArray makeGzipRamdisk()
{
    return gf::img::compress::gzipCompress(makeCpioRamdisk());
}

QByteArray makeBootImage(const QByteArray& ramdisk)
{
    gf::img::boot::Image image;
    image.kind = gf::img::boot::ImageKind::Boot;
    image.headerVersion = 0;
    image.pageSize = 2048;
    image.kernel = QByteArrayLiteral("KERNEL-IMAGE");
    image.ramdisk = ramdisk;
    image.second = QByteArrayLiteral("SECOND-STAGE");
    image.cmdline = QStringLiteral("console=ttyMSM0 androidboot.hardware=test");
    image.kernelAddr = 0x8000;
    image.ramdiskAddr = 0x1000000;
    image.tagsAddr = 0x100;
    return gf::img::boot::serialize(image);
}

const gf::img::cpio::Entry* findEntry(const gf::img::cpio::Archive& archive, const QString& name)
{
    for (const auto& entry : archive.entries) {
        QString normalized = entry.name;
        while (normalized.startsWith(QStringLiteral("./")))
            normalized.remove(0, 2);
        if (normalized.compare(name, Qt::CaseInsensitive) == 0)
            return &entry;
    }
    return nullptr;
}

int countEntries(const gf::img::cpio::Archive& archive, const QString& name)
{
    int count = 0;
    for (const auto& entry : archive.entries) {
        QString normalized = entry.name;
        while (normalized.startsWith(QStringLiteral("./")))
            normalized.remove(0, 2);
        if (normalized.compare(name, Qt::CaseInsensitive) == 0)
            ++count;
    }
    return count;
}

QByteArray lcgData(int size)
{
    QByteArray data(size, '\0');
    quint32 state = 0x12345678u;
    for (int i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        data[i] = char((state >> 16) & 0xFF);
    }
    return data;
}

} // namespace

class TestExtras : public QObject {
    Q_OBJECT

private slots:
    // ---- LZ4 -------------------------------------------------------------
    void lz4RoundTrip_data();
    void lz4RoundTrip();
    void lz4HandcraftedBlock();
    void lz4DetectFrames();

    // ---- ramdisk / boot 修补 ---------------------------------------------
    void patchRamdiskInjectsInit();
    void patchRamdiskWithoutInit();
    void patchRamdiskRejectsEmptyPayload();
    void patchRamdiskKeepsDotPrefix();
    void applyPropertyOverrides();
    void patchGzipBootImage();
    void patchLz4BootImage();
    void patchInitBootImage();

    // ---- root 状态 / 模块 -------------------------------------------------
    void rootEvaluate_data();
    void rootEvaluate();
    void moduleParsing();

    // ---- bootloader -------------------------------------------------------
    void bootloaderParsing();
    void bootloaderSlotValidation();

    // ---- bypass -----------------------------------------------------------
    void bypassDialHelpers();
    void bypassComponentValidation();
    void bypassFactoryInfo();

    // ---- 工具注册 ---------------------------------------------------------
    void apatchKernelInspection();

    void registersExtrasTools();
};

// ---------------------------------------------------------------------------
// LZ4
// ---------------------------------------------------------------------------

void TestExtras::lz4RoundTrip_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("short") << QByteArrayLiteral("hello");
    QTest::newRow("repeated") << QByteArray(4096, 'a');
    QTest::newRow("pattern") << QByteArray("ABCDEFGH").repeated(512);
    QTest::newRow("random-5k") << lcgData(5000);
}

void TestExtras::lz4RoundTrip()
{
    QFETCH(QByteArray, payload);

    const QByteArray block = lz4::compressBlock(payload);
    QCOMPARE(lz4::decompressBlock(block), payload);

    const QByteArray legacy = lz4::compress(payload, lz4::Frame::Legacy);
    if (!payload.isEmpty()) {
        QCOMPARE(lz4::detect(legacy), lz4::Frame::Legacy);
        QCOMPARE(lz4::decompress(legacy), payload);
    }

    const QByteArray modern = lz4::compress(payload, lz4::Frame::Modern);
    if (!payload.isEmpty()) {
        QCOMPARE(lz4::detect(modern), lz4::Frame::Modern);
        QCOMPARE(lz4::decompress(modern), payload);
    }
}

void TestExtras::lz4HandcraftedBlock()
{
    // token 0x14: 1 literal + 8 bytes match at offset 1 -> "a" * 9
    QByteArray block;
    block.append(char(0x14));
    block.append('a');
    block.append(char(0x01));
    block.append(char(0x00));
    QCOMPARE(lz4::decompressBlock(block), QByteArray(9, 'a'));
    QCOMPARE(lz4::decompress(block), QByteArray(9, 'a'));
}

void TestExtras::lz4DetectFrames()
{
    QCOMPARE(lz4::detect(QByteArray()), lz4::Frame::Unknown);
    QCOMPARE(lz4::detect(QByteArrayLiteral("\x04\x22\x4D\x18rest")), lz4::Frame::Modern);
    QCOMPARE(lz4::detect(QByteArrayLiteral("\x02\x21\x4C\x18rest")), lz4::Frame::Legacy);
    QCOMPARE(lz4::detect(QByteArrayLiteral("plaindata")), lz4::Frame::RawBlock);
}

// ---------------------------------------------------------------------------
// ramdisk / boot 修补
// ---------------------------------------------------------------------------

void TestExtras::patchRamdiskInjectsInit()
{
    root::PatchOptions options;
    options.payload = QByteArrayLiteral("MAGISKINIT-BINARY");
    options.payloadLabel = QStringLiteral("magiskinit");
    options.magiskConfig = QByteArrayLiteral("KEEPVERITY=true\nKEEPFORCEENCRYPT=false\nSHA1=deadbeef\n");
    root::InjectFile extra;
    extra.path = QStringLiteral("overlay.d/sbin/helper");
    extra.mode = 0100750u;
    extra.data = QByteArrayLiteral("HELPER");
    options.extraFiles.append(extra);

    root::PatchOutcome outcome;
    const QByteArray patched = root::patchRamdisk(makeCpioRamdisk(), options, &outcome);
    QVERIFY(!patched.isEmpty());

    const gf::img::cpio::Archive archive = gf::img::cpio::parse(patched);
    const gf::img::cpio::Entry* init = findEntry(archive, QStringLiteral("init"));
    QVERIFY(init);
    QCOMPARE(init->data, QByteArrayLiteral("MAGISKINIT-BINARY"));
    QCOMPARE(countEntries(archive, QStringLiteral("init")), 1);

    const gf::img::cpio::Entry* backup = findEntry(archive, QStringLiteral(".backup/init"));
    QVERIFY(backup);
    QCOMPARE(backup->data, QByteArrayLiteral("ORIGINAL-INIT"));

    QVERIFY(findEntry(archive, QStringLiteral(".backup/.magisk")));
    QVERIFY(findEntry(archive, QStringLiteral("overlay.d/sbin/helper")));
    QVERIFY(findEntry(archive, QStringLiteral("overlay.d/sbin")));
    QCOMPARE(outcome.initSha1.size(), 40);
    QVERIFY(!outcome.changes.isEmpty());
}

void TestExtras::patchRamdiskWithoutInit()
{
    gf::img::cpio::Archive archive;
    gf::img::cpio::Entry prop;
    prop.name = QStringLiteral("default.prop");
    prop.mode = 0100644u;
    prop.ino = 1;
    prop.data = QByteArrayLiteral("ro.a=1\n");
    archive.entries.append(prop);

    root::PatchOptions options;
    options.payload = QByteArrayLiteral("NEW-INIT");
    const QByteArray patched = root::patchRamdisk(gf::img::cpio::serialize(archive), options);
    const gf::img::cpio::Archive result = gf::img::cpio::parse(patched);
    const gf::img::cpio::Entry* init = findEntry(result, QStringLiteral("init"));
    QVERIFY(init);
    QCOMPARE(init->data, QByteArrayLiteral("NEW-INIT"));
}

void TestExtras::patchRamdiskRejectsEmptyPayload()
{
    root::PatchOptions options;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, root::patchRamdisk(makeCpioRamdisk(), options));
}

void TestExtras::patchRamdiskKeepsDotPrefix()
{
    gf::img::cpio::Archive archive;
    gf::img::cpio::Entry init;
    init.name = QStringLiteral("./init");
    init.mode = 0100750u;
    init.ino = 1;
    init.data = QByteArrayLiteral("DOT-INIT");
    archive.entries.append(init);

    root::PatchOptions options;
    options.payload = QByteArrayLiteral("REPLACED");
    const QByteArray patched = root::patchRamdisk(gf::img::cpio::serialize(archive), options);
    const gf::img::cpio::Archive result = gf::img::cpio::parse(patched);
    QCOMPARE(countEntries(result, QStringLiteral("init")), 1);
    const gf::img::cpio::Entry* entry = findEntry(result, QStringLiteral("init"));
    QVERIFY(entry);
    QCOMPARE(entry->data, QByteArrayLiteral("REPLACED"));
    QVERIFY(findEntry(result, QStringLiteral(".backup/init")));
}

void TestExtras::applyPropertyOverrides()
{
    QMap<QString, QString> overrides;
    overrides.insert(QStringLiteral("ro.b"), QStringLiteral("9"));
    overrides.insert(QStringLiteral("ro.c"), QStringLiteral("3"));

    QStringList changes;
    QString error;
    const QByteArray patched = root::applyPropertyOverrides(
        makeCpioRamdisk(), {QStringLiteral("default.prop")}, overrides, &changes, &error);
    QVERIFY2(!patched.isEmpty(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(changes.size(), 1);

    const gf::img::cpio::Archive archive = gf::img::cpio::parse(patched);
    const gf::img::cpio::Entry* prop = findEntry(archive, QStringLiteral("default.prop"));
    QVERIFY(prop);
    const QMap<QString, QString> props = root::parsePropLines(QString::fromUtf8(prop->data));
    QCOMPARE(props.value(QStringLiteral("ro.a")), QStringLiteral("1"));
    QCOMPARE(props.value(QStringLiteral("ro.b")), QStringLiteral("9"));
    QCOMPARE(props.value(QStringLiteral("ro.c")), QStringLiteral("3"));
}

void TestExtras::patchGzipBootImage()
{
    const QByteArray boot = makeBootImage(makeGzipRamdisk());
    // 先确认原始镜像可解析且 ramdisk 可识别。
    const gf::img::boot::Image parsed = gf::img::boot::parse(boot);
    QCOMPARE(parsed.kernel, QByteArrayLiteral("KERNEL-IMAGE"));

    root::PatchOptions options;
    options.payload = QByteArrayLiteral("MAGISKINIT");
    root::PatchOutcome outcome;
    const QByteArray patched = root::patchBootImage(boot, options, &outcome);
    QVERIFY(!patched.isEmpty());
    QCOMPARE(outcome.ramdiskFormat, QStringLiteral("gzip"));

    const gf::img::boot::Image patchedImage = gf::img::boot::parse(patched);
    QCOMPARE(patchedImage.kernel, QByteArrayLiteral("KERNEL-IMAGE"));
    QCOMPARE(outcome.initSha1.size(), 40);

    QString format;
    const QByteArray plain = root::decompressRamdiskAuto(patchedImage.ramdisk, &format);
    QCOMPARE(format, QStringLiteral("gzip"));
    const gf::img::cpio::Archive archive = gf::img::cpio::parse(plain);
    const gf::img::cpio::Entry* init = findEntry(archive, QStringLiteral("init"));
    QVERIFY(init);
    QCOMPARE(init->data, QByteArrayLiteral("MAGISKINIT"));
}

void TestExtras::patchLz4BootImage()
{
    const QByteArray ramdisk = lz4::compress(makeCpioRamdisk(), lz4::Frame::Legacy);
    const QByteArray boot = makeBootImage(ramdisk);

    root::PatchOptions options;
    options.payload = QByteArrayLiteral("KSU-PAYLOAD");
    root::PatchOutcome outcome;
    const QByteArray patched = root::patchBootImage(boot, options, &outcome);
    QCOMPARE(outcome.ramdiskFormat, QStringLiteral("lz4"));
    QCOMPARE(outcome.lz4Frame, lz4::Frame::Legacy);

    const gf::img::boot::Image patchedImage = gf::img::boot::parse(patched);
    QCOMPARE(lz4::detect(patchedImage.ramdisk), lz4::Frame::Legacy);
    const QByteArray plain = lz4::decompress(patchedImage.ramdisk);
    const gf::img::cpio::Archive archive = gf::img::cpio::parse(plain);
    const gf::img::cpio::Entry* init = findEntry(archive, QStringLiteral("init"));
    QVERIFY(init);
    QCOMPARE(init->data, QByteArrayLiteral("KSU-PAYLOAD"));
}

void TestExtras::patchInitBootImage()
{
    gf::img::boot::Image image;
    image.kind = gf::img::boot::ImageKind::InitBoot;
    image.headerVersion = 4;
    image.pageSize = 4096;
    image.ramdisk = makeGzipRamdisk();
    image.osVersion = 0x0C000000u;
    const QByteArray boot = gf::img::boot::serialize(image);
    QVERIFY(!boot.isEmpty());

    root::PatchOptions options;
    options.payload = QByteArrayLiteral("INITBOOT-PAYLOAD");
    const QByteArray patched = root::patchBootImage(boot, options);
    const gf::img::boot::Image parsed = gf::img::boot::parse(patched);
    QCOMPARE(parsed.kind, gf::img::boot::ImageKind::InitBoot);
    const QByteArray plain = root::decompressRamdiskAuto(parsed.ramdisk);
    const gf::img::cpio::Archive archive = gf::img::cpio::parse(plain);
    const gf::img::cpio::Entry* init = findEntry(archive, QStringLiteral("init"));
    QVERIFY(init);
    QCOMPARE(init->data, QByteArrayLiteral("INITBOOT-PAYLOAD"));
}

// ---------------------------------------------------------------------------
// root 状态 / 模块
// ---------------------------------------------------------------------------

void TestExtras::rootEvaluate_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("debuggable");
    QTest::addColumn<QStringList>("paths");
    QTest::addColumn<QString>("suOutput");
    QTest::addColumn<QString>("versionOutput");
    QTest::addColumn<bool>("rooted");
    QTest::addColumn<int>("engine");

    QTest::newRow("none")
        << QStringLiteral("Pixel") << QStringLiteral("0") << QStringList{} << QString()
        << QString() << false << int(root::RootEngine::None);
    QTest::newRow("magisk")
        << QStringLiteral("Pixel") << QStringLiteral("0")
        << QStringList{QStringLiteral("/system/bin/magisk")}
        << QStringLiteral("uid=0(root) gid=0(root)") << QStringLiteral("25.2:MAGISK")
        << true << int(root::RootEngine::Magisk);
    QTest::newRow("kernelsu")
        << QStringLiteral("Redmi") << QStringLiteral("0")
        << QStringList{QStringLiteral("/data/adb/ksu")} << QString()
        << QStringLiteral("KernelSU 0.9.5") << true << int(root::RootEngine::KernelSU);
    QTest::newRow("apatch")
        << QStringLiteral("OnePlus") << QStringLiteral("0")
        << QStringList{QStringLiteral("/data/adb/apatch")} << QString()
        << QStringLiteral("APatch 1.0") << true << int(root::RootEngine::APatch);
    QTest::newRow("su-only")
        << QStringLiteral("Generic") << QStringLiteral("1") << QStringList{}
        << QStringLiteral("uid=0(root)") << QString() << true << int(root::RootEngine::Other);
}

void TestExtras::rootEvaluate()
{
    QFETCH(QString, model);
    QFETCH(QString, debuggable);
    QFETCH(QStringList, paths);
    QFETCH(QString, suOutput);
    QFETCH(QString, versionOutput);
    QFETCH(bool, rooted);
    QFETCH(int, engine);

    QMap<QString, QString> props;
    props.insert(QStringLiteral("ro.product.model"), model);
    props.insert(QStringLiteral("ro.debuggable"), debuggable);
    props.insert(QStringLiteral("ro.build.type"), debuggable == QLatin1String("1")
                                                      ? QStringLiteral("userdebug")
                                                      : QStringLiteral("user"));
    props.insert(QStringLiteral("ro.boot.flash.locked"), QStringLiteral("1"));
    props.insert(QStringLiteral("ro.boot.verifiedbootstate"), QStringLiteral("green"));

    const root::RootStatus status =
        root::evaluate(props, paths, QString(), suOutput, versionOutput);
    QCOMPARE(status.rooted, rooted);
    QCOMPARE(int(status.engine), engine);
    QCOMPARE(status.verifiedBootLocked, true);
    QCOMPARE(status.verifiedBootState, QStringLiteral("green"));
    if (rooted)
        QVERIFY(status.summary().contains(QStringLiteral("Root 状态：已 Root")));
    else
        QVERIFY(status.summary().contains(QStringLiteral("未 Root")));
}

void TestExtras::moduleParsing()
{
    const QString listing = QStringLiteral(
        "acc\n"
        "busybox-ndk\n"
        ".backup\n"
        "modules_update\n"
        "\n"
        "some/thing\n");
    const QStringList ids = root::parseModuleIds(listing);
    QVERIFY(ids.contains(QStringLiteral("acc")));
    QVERIFY(ids.contains(QStringLiteral("busybox-ndk")));
    QVERIFY(ids.contains(QStringLiteral("modules_update")));
    QVERIFY(!ids.contains(QStringLiteral(".backup")));
    QVERIFY(!ids.contains(QStringLiteral("some/thing")));

    root::MagiskModule module = root::parseModuleFlags(
        QStringLiteral("acc"), QStringLiteral("disable\nremove\nmodule.prop\n"));
    QCOMPARE(module.id, QStringLiteral("acc"));
    QVERIFY(module.disabled);
    QVERIFY(module.removePending);
    QVERIFY(!module.updatePending);

    root::applyModuleProp(module,
                          QStringLiteral("id=acc\nname=ACC 加速\nversion=1.2.3\ndescription=test\n"));
    QCOMPARE(module.name, QStringLiteral("ACC 加速"));
    QCOMPARE(module.version, QStringLiteral("1.2.3"));
    QCOMPARE(module.description, QStringLiteral("test"));
}

// ---------------------------------------------------------------------------
// bootloader
// ---------------------------------------------------------------------------

void TestExtras::bootloaderParsing()
{
    QMap<QString, QString> vars;
    vars.insert(QStringLiteral("version-bootloader"), QStringLiteral("0.5"));
    vars.insert(QStringLiteral("product"), QStringLiteral("raven"));
    vars.insert(QStringLiteral("serialno"), QStringLiteral("ABC123"));
    vars.insert(QStringLiteral("current-slot"), QStringLiteral("a"));
    vars.insert(QStringLiteral("slot-count"), QStringLiteral("2"));
    vars.insert(QStringLiteral("unlocked"), QStringLiteral("yes"));
    vars.insert(QStringLiteral("secure"), QStringLiteral("no"));
    vars.insert(QStringLiteral("verity"), QStringLiteral("enforcing"));
    vars.insert(QStringLiteral("veritymode"), QStringLiteral("enforcing"));
    vars.insert(QStringLiteral("slot-successful:_a"), QStringLiteral("yes"));
    vars.insert(QStringLiteral("slot-successful:_b"), QStringLiteral("no"));
    vars.insert(QStringLiteral("slot-unbootable:_b"), QStringLiteral("yes"));
    vars.insert(QStringLiteral("slot-retry-count:_a"), QStringLiteral("3"));

    const auto info = bootloader::parseBootloaderInfo(vars);
    QCOMPARE(info.versionBootloader, QStringLiteral("0.5"));
    QCOMPARE(info.product, QStringLiteral("raven"));
    QCOMPARE(info.currentSlot, QStringLiteral("a"));
    QCOMPARE(info.slotCount, 2);
    QVERIFY(info.unlocked);
    QVERIFY(!info.secure);
    QVERIFY(info.verityEnabled);
    QCOMPARE(info.slotStates.size(), 2);
    QCOMPARE(info.slotStates.at(0).slot, QStringLiteral("a"));
    QVERIFY(info.slotStates.at(0).successful);
    QCOMPARE(info.slotStates.at(0).retryCount, 3);
    QCOMPARE(info.slotStates.at(1).slot, QStringLiteral("b"));
    QVERIFY(!info.slotStates.at(1).successful);
    QVERIFY(info.slotStates.at(1).unbootable);

    const QString summary = info.summary();
    QVERIFY(summary.contains(QStringLiteral("已解锁")));
    QVERIFY(summary.contains(QStringLiteral("A/B")));

    QCOMPARE(bootloader::activeSlotArgument(QStringLiteral("b")),
             QStringLiteral("--set-active=b"));
    QVERIFY(bootloader::knownReadVariables().contains(QStringLiteral("current-slot")));
}

void TestExtras::bootloaderSlotValidation()
{
    QVERIFY(bootloader::isValidSlot(QStringLiteral("a")));
    QVERIFY(bootloader::isValidSlot(QStringLiteral("B")));
    QVERIFY(!bootloader::isValidSlot(QStringLiteral("c")));
    QVERIFY(!bootloader::isValidSlot(QString()));

    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             bypass::buildActivityStartCommand(QStringLiteral("broken")));
}

// ---------------------------------------------------------------------------
// bypass
// ---------------------------------------------------------------------------

void TestExtras::bypassDialHelpers()
{
    const auto codes = bypass::dialCodes();
    QVERIFY(codes.size() >= 5);
    QStringList ids;
    for (const auto& code : codes) {
        QVERIFY2(!code.id.isEmpty(), "dial code id empty");
        QVERIFY(bypass::isValidDialCode(code.code));
        QVERIFY(!ids.contains(code.id));
        ids.append(code.id);
    }
    QVERIFY(bypass::isValidDialCode(QStringLiteral("*#*#4636#*#*")));
    QVERIFY(!bypass::isValidDialCode(QStringLiteral("*#;reboot#*")));

    const QString command = bypass::buildDialIntentCommand(QStringLiteral("*#*#4636#*#*"));
    QVERIFY(command.contains(QStringLiteral("android.intent.action.DIAL")));
    QVERIFY(command.contains(QStringLiteral("tel:*#*#4636#*#*")));
    QVERIFY(command.contains(QLatin1Char('\'')));

    const auto activities = bypass::engineeringActivities();
    QVERIFY(activities.size() >= 5);
    QStringList components;
    for (const auto& activity : activities) {
        QVERIFY(bypass::isValidComponent(activity.component));
        QVERIFY(!components.contains(activity.component));
        components.append(activity.component);
    }
    QVERIFY(bypass::findDialCode(QStringLiteral("4636")) != nullptr);
    QVERIFY(bypass::findEngineeringActivity(QStringLiteral("testing-settings")) != nullptr);
    QVERIFY(bypass::findDialCode(QStringLiteral("does-not-exist")) == nullptr);
}

void TestExtras::bypassComponentValidation()
{
    QVERIFY(bypass::isValidComponent(QStringLiteral("com.android.settings/.TestingSettings")));
    QVERIFY(!bypass::isValidComponent(QStringLiteral("com.android.settings")));
    QVERIFY(!bypass::isValidComponent(QStringLiteral("/.TestingSettings")));
    QVERIFY(!bypass::isValidComponent(QStringLiteral("pkg/act; reboot")));
}

void TestExtras::bypassFactoryInfo()
{
    QMap<QString, QString> props;
    props.insert(QStringLiteral("ro.product.model"), QStringLiteral("Pixel 7"));
    props.insert(QStringLiteral("ro.product.manufacturer"), QStringLiteral("Google"));
    props.insert(QStringLiteral("ro.build.version.release"), QStringLiteral("14"));
    props.insert(QStringLiteral("ro.build.id"), QStringLiteral("UQ1A.240205.002"));
    props.insert(QStringLiteral("ro.product.cpu.abi"), QStringLiteral("arm64-v8a"));
    props.insert(QStringLiteral("ro.board.platform"), QStringLiteral("tensor"));
    props.insert(QStringLiteral("ro.boot.serialno"), QStringLiteral("SN123"));

    const auto info = bypass::factoryInfoFromProps(props);
    QCOMPARE(info.model, QStringLiteral("Pixel 7"));
    QCOMPARE(info.manufacturer, QStringLiteral("Google"));
    QCOMPARE(info.androidVersion, QStringLiteral("14"));
    QCOMPARE(info.board, QStringLiteral("tensor"));
    QCOMPARE(info.serialno, QStringLiteral("SN123"));

    const QString text = bypass::factoryInfoText(info);
    QVERIFY(text.contains(QStringLiteral("Pixel 7")));
    QVERIFY(text.contains(QStringLiteral("arm64-v8a")));
}

// ---------------------------------------------------------------------------
// 工具注册
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// APatch kernel inspection
// ---------------------------------------------------------------------------

void TestExtras::apatchKernelInspection()
{
    gf::img::boot::Image image;
    image.kind = gf::img::boot::ImageKind::Boot;
    image.headerVersion = 0;
    image.pageSize = 2048;
    image.kernel = QByteArrayLiteral("Linux version 5.15.78-test (builder@xmf) #1 SMP PREEMPT")
                   + QByteArray(256, '\0');
    image.ramdisk = QByteArray(1024, 0x42);
    const QByteArray data = gf::img::boot::serialize(image);
    QVERIFY(!data.isEmpty());

    const root::KernelImageInfo info = root::inspectKernelImage(data);
    QVERIFY2(info.ok, qPrintable(info.error));
    QVERIFY2(info.imageKind.contains(QStringLiteral("boot")), qPrintable(info.imageKind));
    QCOMPARE(info.headerVersion, 0);
    QCOMPARE(info.kernelSize, qint64(image.kernel.size()));
    QCOMPARE(info.ramdiskSize, qint64(image.ramdisk.size()));
    QCOMPARE(info.kernelCompression, QStringLiteral("raw"));
    QVERIFY2(info.kernelVersion.contains(QStringLiteral("5.15.78-test")), qPrintable(info.kernelVersion));

    const root::KernelImageInfo garbage = root::inspectKernelImage(QByteArrayLiteral("not a boot image"));
    QVERIFY(!garbage.ok);
    QVERIFY(!garbage.error.isEmpty());

    QVERIFY(!root::inspectKernelImage(QByteArray()).ok);
}

void TestExtras::registersExtrasTools()
{
    static bool registered = false;
    if (!registered) {
        gf::registerRootTools();
        gf::registerBootloaderTools();
        gf::registerUnlockTools();
        gf::registerBypassTools();
        registered = true;
    }

    struct Expectation {
        const char* id;
        const char* category;
        bool planned;
    };
    const Expectation expectations[] = {
        {"root.status", "root", false},
        {"root.boot.patch", "root", false},
        {"root.prop.patch", "root", false},
        {"root.modules", "root", false},
        {"root.module", "root", false},
        {"root.hide", "root", false},
        {"root.apatch.patch", "root", false},
        {"bootloader.info", "bootloader", false},
        {"bootloader.getvar", "bootloader", false},
        {"bootloader.slots", "bootloader", false},
        {"bootloader.setvar", "bootloader", false},
        {"bootloader.unlock", "bootloader", false},
        {"bootloader.verify", "bootloader", false},
        {"bootloader.redtext", "bootloader", false},
        {"bootloader.backup.env", "bootloader", false},
        {"unlock.userdata", "unlock", false},
        {"unlock.metadata", "unlock", false},
        {"unlock.cache", "unlock", false},
        {"unlock.frp", "unlock", false},
        {"unlock.dalvik", "unlock", false},
        {"unlock.simlock", "unlock", false},
        {"unlock.account.official", "unlock", false},
        {"unlock.sim.official", "unlock", false},
        {"bypass.codes", "bypass", false},
        {"bypass.dial", "bypass", false},
        {"bypass.activities", "bypass", false},
        {"bypass.factory", "bypass", false},
        {"bypass.deviceinfo", "bypass", false},
        {"bypass.frp.guide", "bypass", false},
        {"bypass.carrier.guide", "bypass", false},
    };

    auto& registry = gf::ToolRegistry::instance();
    const QStringList allowedCategories = {
        QStringLiteral("image"), QStringLiteral("qcom"), QStringLiteral("mtk"),
        QStringLiteral("bootloader"), QStringLiteral("sprd"), QStringLiteral("rk"),
        QStringLiteral("aw"), QStringLiteral("root"), QStringLiteral("unlock"),
        QStringLiteral("adb"), QStringLiteral("firmware"), QStringLiteral("batch"),
        QStringLiteral("bypass"),
    };
    for (const auto& expectation : expectations) {
        const QString id = QString::fromLatin1(expectation.id);
        QVERIFY2(registry.has(id), qPrintable(QStringLiteral("missing tool: %1").arg(id)));
        const QVariantMap spec = registry.tool(id);
        QCOMPARE(spec.value(QStringLiteral("category")).toString(),
                 QString::fromLatin1(expectation.category));
        QVERIFY(allowedCategories.contains(spec.value(QStringLiteral("category")).toString()));
        QVERIFY(!spec.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(!spec.value(QStringLiteral("desc")).toString().isEmpty());
        const auto handler = registry.handler(id);
        if (expectation.planned) {
            QVERIFY2(!handler, qPrintable(QStringLiteral("planned tool has handler: %1").arg(id)));
            QCOMPARE(spec.value(QStringLiteral("maturity")).toInt(), 2);
        } else {
            QVERIFY2(static_cast<bool>(handler),
                     qPrintable(QStringLiteral("missing handler: %1").arg(id)));
        }
    }

    // registerBatchTools 只是 AppController 的兼容入口（批量工具由设备层注册），
    // 重复调用不能改变工具数量。
    const int before = registry.count();
    gf::registerBatchTools();
    QCOMPARE(registry.count(), before);
}

#include "test_extras.moc"
GF_TEST_CLASS(TestExtras)
