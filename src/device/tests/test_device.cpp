#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "TestRegistry.h"
#include "app/ToolModules.h"
#include "core/ToolRegistry.h"
#include "core/Util.h"
#include "device/AdbClient.h"
#include "device/BatchJobManager.h"
#include "device/DeviceEnumerator.h"
#include "device/DriverManager.h"
#include "device/FastbootClient.h"
#include "device/Ports.h"

using namespace gf::device;

class TestDeviceLayer : public QObject {
    Q_OBJECT

private slots:
    // ---- adb output parsing -------------------------------------------------
    void parseAdbDevices_data();
    void parseAdbDevices();
    void parseGetpropOutput();
    void parseByNameLinks();

    // ---- fastboot output parsing -------------------------------------------
    void parseFastbootDevices();
    void parseGetvarOutput();
    void parsePartitionTable();

    // ---- port identification -----------------------------------------------
    void parseVidPid();
    void parseComPort();
    void classifyPorts_data();
    void classifyPorts();

    // ---- driver detection ---------------------------------------------------
    void matchDriverBindings_data();
    void matchDriverBindings();

    // ---- batch helpers ------------------------------------------------------
    void parseHashManifest();
    void expandRenamePattern_data();
    void expandRenamePattern();
    void fileHashVerification();

    // ---- batch queue --------------------------------------------------------
    void batchRunsAllJobs();
    void batchRespectsParallelLimit();
    void batchSerializesSameDevice();
    void batchRetriesFailedJobs();
    void batchCancelBeforeRun();
    void batchCancelDuringRun();
    void batchAggregatesProgress();
    void batchReportsFailures();

    // ---- tool registration / error behaviour --------------------------------
    void driverDetectionShape();
    void handlerErrorsAreChinese();
    void registersDeviceTools();
};

// ---------------------------------------------------------------------------
// adb parsing
// ---------------------------------------------------------------------------

void TestDeviceLayer::parseAdbDevices_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QStringList>("serials");
    QTest::addColumn<QStringList>("states");

    const QString normal = QStringLiteral(
        "List of devices attached\n"
        "R58M12345\tdevice product:beyond1lte model:SM_G973F device:beyond1 transport_id:1\n"
        "0123456789ABCDEF\toffline\n"
        "abcdef\tunauthorized usb:1-1\n"
        "\n");
    QTest::newRow("mixed") << normal
                           << QStringList{QStringLiteral("R58M12345"), QStringLiteral("0123456789ABCDEF"),
                                          QStringLiteral("abcdef")}
                           << QStringList{QStringLiteral("device"), QStringLiteral("offline"),
                                          QStringLiteral("unauthorized")};

    const QString daemon = QStringLiteral(
        "* daemon not running; starting now at tcp:5037\n"
        "* daemon started successfully\n"
        "List of devices attached\n"
        "emulator-5554\tdevice\n");
    QTest::newRow("daemon-banner") << daemon << QStringList{QStringLiteral("emulator-5554")}
                                   << QStringList{QStringLiteral("device")};

    QTest::newRow("empty") << QStringLiteral("List of devices attached\n\n") << QStringList{}
                           << QStringList{};

    const QString noPermission = QStringLiteral(
        "List of devices attached\n"
        "ABC123\tno permissions (user in plugdev group; are your udev rules wrong?); see [http://x]\n");
    QTest::newRow("no-permissions") << noPermission << QStringList{QStringLiteral("ABC123")}
                                    << QStringList{QStringLiteral("no")};

    QTest::newRow("tcp-device") << QStringLiteral("192.168.1.20:5555\tdevice\n")
                                << QStringList{QStringLiteral("192.168.1.20:5555")}
                                << QStringList{QStringLiteral("device")};
}

void TestDeviceLayer::parseAdbDevices()
{
    QFETCH(QString, input);
    QFETCH(QStringList, serials);
    QFETCH(QStringList, states);

    const auto devices = gf::device::parseAdbDevices(input);
    QStringList actualSerials;
    QStringList actualStates;
    for (const auto& device : devices) {
        actualSerials.append(device.serial);
        actualStates.append(device.state);
    }
    QCOMPARE(actualSerials, serials);
    QCOMPARE(actualStates, states);
}

void TestDeviceLayer::parseGetpropOutput()
{
    const QString output = QStringLiteral(
        "[ro.product.model]: [Pixel 7]\n"
        "[ro.product.brand]: [google]\n"
        "[ro.build.version.sdk]: [34]\n"
        "[empty.prop]: []\n"
        "not a property line\n");
    const auto props = gf::device::parseGetpropOutput(output);
    QCOMPARE(props.value(QStringLiteral("ro.product.model")), QStringLiteral("Pixel 7"));
    QCOMPARE(props.value(QStringLiteral("ro.product.brand")), QStringLiteral("google"));
    QCOMPARE(props.value(QStringLiteral("ro.build.version.sdk")), QStringLiteral("34"));
    QCOMPARE(props.value(QStringLiteral("empty.prop")), QString());
    QVERIFY(!props.contains(QStringLiteral("not a property line")));
}

void TestDeviceLayer::parseByNameLinks()
{
    const QString output = QStringLiteral(
        "lrwxrwxrwx 1 root root 16 1970-01-01 00:00 boot -> /dev/block/sde11\n"
        "lrwxrwxrwx 1 root root 16 1970-01-01 00:00 system -> /dev/block/sde29\n"
        "boot\n"
        "vendor\n"
        "ls: /dev/block/by-name: No such file or directory\n");
    const auto links = gf::device::parseByNameLinks(output);
    QCOMPARE(links.size(), 4);
    QCOMPARE(links.at(0).name, QStringLiteral("boot"));
    QCOMPARE(links.at(0).target, QStringLiteral("/dev/block/sde11"));
    QCOMPARE(links.at(1).name, QStringLiteral("system"));
    QCOMPARE(links.at(1).target, QStringLiteral("/dev/block/sde29"));
    QCOMPARE(links.at(2).name, QStringLiteral("boot"));
    QCOMPARE(links.at(3).name, QStringLiteral("vendor"));
}

// ---------------------------------------------------------------------------
// fastboot parsing
// ---------------------------------------------------------------------------

void TestDeviceLayer::parseFastbootDevices()
{
    QCOMPARE(gf::device::parseFastbootDevices(QString()).size(), 0);

    const QString output = QStringLiteral(
        "3A991FDH200456\tfastboot\r\n"
        "\r\n"
        "3B881FDH200999\tfastbootd\r\n"
        "fastboot: error: no devices found\r\n");
    const auto devices = gf::device::parseFastbootDevices(output);
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices.at(0).serial, QStringLiteral("3A991FDH200456"));
    QCOMPARE(devices.at(0).mode, QStringLiteral("fastboot"));
    QVERIFY(!devices.at(0).isFastbootD());
    QCOMPARE(devices.at(1).serial, QStringLiteral("3B881FDH200999"));
    QVERIFY(devices.at(1).isFastbootD());
}

void TestDeviceLayer::parseGetvarOutput()
{
    const QString output = QStringLiteral(
        "(bootloader) version:0.4\n"
        "(bootloader) version-bootloader:0.4\n"
        "(bootloader) partition-size:boot:0x0000000004000000\n"
        "(bootloader) partition-type:boot:raw\n"
        "(bootloader) has-slot:boot:yes\n"
        "(bootloader) is-logical:super:yes\n"
        "(bootloader) current-slot:a\n"
        "(bootloader) slot-successful:_a: yes\n"
        "Finished. Total time: 0.042s\n"
        "OKAY [  0.001s]\n");
    const auto vars = gf::device::parseGetvarOutput(output);
    QCOMPARE(vars.value(QStringLiteral("version")), QStringLiteral("0.4"));
    QCOMPARE(vars.value(QStringLiteral("partition-size:boot")),
             QStringLiteral("0x0000000004000000"));
    QCOMPARE(vars.value(QStringLiteral("partition-type:boot")), QStringLiteral("raw"));
    QCOMPARE(vars.value(QStringLiteral("has-slot:boot")), QStringLiteral("yes"));
    QCOMPARE(vars.value(QStringLiteral("is-logical:super")), QStringLiteral("yes"));
    QCOMPARE(vars.value(QStringLiteral("current-slot")), QStringLiteral("a"));
    QCOMPARE(vars.value(QStringLiteral("slot-successful:_a")), QStringLiteral("yes"));
    QVERIFY(!vars.contains(QStringLiteral("Finished. Total time")));
}

void TestDeviceLayer::parsePartitionTable()
{
    const QString output = QStringLiteral(
        "(bootloader) partition-size:boot:0x04000000\n"
        "(bootloader) partition-type:boot:raw\n"
        "(bootloader) has-slot:boot:yes\n"
        "(bootloader) partition-size:super:0x400000000\n"
        "(bootloader) partition-type:super:raw\n"
        "(bootloader) is-logical:super:yes\n"
        "(bootloader) partition-size:userdata:0x1000000000\n"
        "(bootloader) partition-type:userdata:ext4\n");
    const auto vars = gf::device::parseGetvarOutput(output);
    const auto partitions = gf::device::parsePartitionTable(vars);
    QCOMPARE(partitions.size(), 3);
    // Sorted alphabetically: boot, super, userdata.
    QCOMPARE(partitions.at(0).name, QStringLiteral("boot"));
    QCOMPARE(partitions.at(0).size, QStringLiteral("0x04000000"));
    QCOMPARE(partitions.at(0).slot, QStringLiteral("yes"));
    QVERIFY(!partitions.at(0).logical);
    QCOMPARE(partitions.at(1).name, QStringLiteral("super"));
    QVERIFY(partitions.at(1).logical);
    QCOMPARE(partitions.at(2).name, QStringLiteral("userdata"));
    QCOMPARE(partitions.at(2).type, QStringLiteral("ext4"));
}

// ---------------------------------------------------------------------------
// port identification
// ---------------------------------------------------------------------------

void TestDeviceLayer::parseVidPid()
{
    quint16 vid = 0;
    quint16 pid = 0;
    QVERIFY(gf::device::parseVidPid(QStringLiteral("USB\\VID_05C6&PID_9008&REV_0000"), &vid, &pid));
    QCOMPARE(vid, quint16(0x05C6));
    QCOMPARE(pid, quint16(0x9008));

    QVERIFY(gf::device::parseVidPid(QStringLiteral("USB\\VID_18D1&PID_4EE7&MI_01\\7&1A2B3C4D&0&0001"), &vid, &pid));
    QCOMPARE(vid, quint16(0x18D1));
    QCOMPARE(pid, quint16(0x4EE7));

    vid = 123;
    pid = 456;
    QVERIFY(!gf::device::parseVidPid(QStringLiteral("USB\\Class_FF&SubClass_42&Prot_01"), &vid, &pid));
    QCOMPARE(vid, quint16(123)); // out parameters untouched on failure
    QCOMPARE(pid, quint16(456));

    QVERIFY(!gf::device::parseVidPid(QStringLiteral("PCI\\VEN_8086&DEV_1234"), &vid, &pid));
    QCOMPARE(formatVidPid(0x05C6, 0x9008), QStringLiteral("VID_05C6&PID_9008"));
}

void TestDeviceLayer::parseComPort()
{
    QCOMPARE(gf::device::parseComPort(QStringLiteral("COM5")), 5);
    QCOMPARE(gf::device::parseComPort(QStringLiteral("Qualcomm HS-USB QDLoader 9008 (COM12)")), 12);
    QCOMPARE(gf::device::parseComPort(QStringLiteral("USB Serial Device (COM3)")), 3);
    QCOMPARE(gf::device::parseComPort(QStringLiteral("no port here")), 0);
    QCOMPARE(gf::device::parseComPort(QStringLiteral("COM")), 0);
}

void TestDeviceLayer::classifyPorts_data()
{
    QTest::addColumn<QString>("description");
    QTest::addColumn<QString>("service");
    QTest::addColumn<QString>("hardwareId");
    QTest::addColumn<quint16>("vid");
    QTest::addColumn<quint16>("pid");
    QTest::addColumn<int>("comPort");
    QTest::addColumn<int>("kind");       // PortKind as int
    QTest::addColumn<QString>("chipHint");

    QTest::newRow("qcom-edl")
        << QStringLiteral("Qualcomm HS-USB QDLoader 9008 (COM5)") << QStringLiteral("qcusbser")
        << QStringLiteral("USB\\VID_05C6&PID_9008&REV_0000") << quint16(0x05C6) << quint16(0x9008) << 5
        << int(PortKind::QdLoader) << QStringLiteral("qcom-edl");
    QTest::newRow("mtk-brom")
        << QStringLiteral("MediaTek USB Port") << QStringLiteral("mtk_usb")
        << QStringLiteral("USB\\VID_0E8D&PID_0003") << quint16(0x0E8D) << quint16(0x0003) << 0
        << int(PortKind::MtkBrom) << QStringLiteral("mtk-brom");
    QTest::newRow("mtk-preloader")
        << QStringLiteral("MediaTek PreLoader USB VCOM (COM9)") << QStringLiteral("mtk_usb")
        << QStringLiteral("USB\\VID_0E8D&PID_2000") << quint16(0x0E8D) << quint16(0x2000) << 9
        << int(PortKind::MtkPreloader) << QStringLiteral("mtk-preloader");
    QTest::newRow("sprd-fdl")
        << QStringLiteral("Spreadtrum USB Device") << QStringLiteral("sprd_usb")
        << QStringLiteral("USB\\VID_1782&PID_4D00") << quint16(0x1782) << quint16(0x4D00) << 0
        << int(PortKind::SprdFdl) << QStringLiteral("sprd-fdl");
    QTest::newRow("rockchip")
        << QStringLiteral("Rockchip USB Device") << QStringLiteral("rockusb")
        << QStringLiteral("USB\\VID_2207&PID_110A") << quint16(0x2207) << quint16(0x110A) << 0
        << int(PortKind::Rockchip) << QStringLiteral("rk");
    QTest::newRow("allwinner")
        << QStringLiteral("Allwinner FEL USB Device") << QStringLiteral("aw_usb")
        << QStringLiteral("USB\\VID_1F3A&PID_EFE8") << quint16(0x1F3A) << quint16(0xEFE8) << 0
        << int(PortKind::Allwinner) << QStringLiteral("aw-fel");
    QTest::newRow("adb")
        << QStringLiteral("Android Composite ADB Interface") << QStringLiteral("WinUSB")
        << QStringLiteral("USB\\VID_18D1&PID_4EE7&MI_01") << quint16(0x18D1) << quint16(0x4EE7) << 0
        << int(PortKind::Adb) << QStringLiteral("adb");
    QTest::newRow("fastboot")
        << QStringLiteral("Android Bootloader Interface") << QStringLiteral("WinUSB")
        << QStringLiteral("USB\\VID_18D1&PID_4EE0&MI_00") << quint16(0x18D1) << quint16(0x4EE0) << 0
        << int(PortKind::Fastboot) << QStringLiteral("fastboot");
    QTest::newRow("plain-serial")
        << QStringLiteral("USB Serial Device") << QStringLiteral("usbser")
        << QStringLiteral("USB\\VID_1A86&PID_7523") << quint16(0x1A86) << quint16(0x7523) << 6
        << int(PortKind::Serial) << QString();
}

void TestDeviceLayer::classifyPorts()
{
    QFETCH(QString, description);
    QFETCH(QString, service);
    QFETCH(QString, hardwareId);
    QFETCH(quint16, vid);
    QFETCH(quint16, pid);
    QFETCH(int, comPort);
    QFETCH(int, kind);
    QFETCH(QString, chipHint);

    RawDevice device;
    device.description = description;
    device.service = service;
    device.hardwareIds = {hardwareId};
    device.vid = vid;
    device.pid = pid;
    device.comPort = comPort;
    device.instanceId = hardwareId;
    const PortIdentity identity = classifyPort(device);
    QCOMPARE(int(identity.kind), kind);
    QCOMPARE(identity.chipHint, chipHint);
    if (kind == int(PortKind::Adb))
        QVERIFY(identity.isAdb);
    if (kind == int(PortKind::Fastboot))
        QVERIFY(identity.isFastboot);
    if (kind == int(PortKind::QdLoader) || kind == int(PortKind::MtkBrom)
        || kind == int(PortKind::MtkPreloader) || kind == int(PortKind::SprdFdl)
        || kind == int(PortKind::Rockchip) || kind == int(PortKind::Allwinner))
        QVERIFY(identity.isFlashing);
}

// ---------------------------------------------------------------------------
// driver detection classifier
// ---------------------------------------------------------------------------

void TestDeviceLayer::matchDriverBindings_data()
{
    QTest::addColumn<QString>("description");
    QTest::addColumn<QString>("service");
    QTest::addColumn<QString>("hardwareId");
    QTest::addColumn<quint16>("vid");
    QTest::addColumn<quint16>("pid");
    QTest::addColumn<QStringList>("expectedKinds"); // driverKindId list

    QTest::newRow("qdloader")
        << QStringLiteral("Qualcomm HS-USB QDLoader 9008 (COM5)") << QStringLiteral("qcusbser")
        << QStringLiteral("USB\\VID_05C6&PID_9008") << quint16(0x05C6) << quint16(0x9008)
        << QStringList{QStringLiteral("qdloader")};
    QTest::newRow("mtk")
        << QStringLiteral("MediaTek PreLoader USB VCOM") << QStringLiteral("mtk_usb")
        << QStringLiteral("USB\\VID_0E8D&PID_2000") << quint16(0x0E8D) << quint16(0x2000)
        << QStringList{QStringLiteral("mtk")};
    QTest::newRow("sprd")
        << QStringLiteral("Spreadtrum USB Device") << QStringLiteral("sprd_usb")
        << QStringLiteral("USB\\VID_1782&PID_4D00") << quint16(0x1782) << quint16(0x4D00)
        << QStringList{QStringLiteral("sprd")};
    QTest::newRow("rk")
        << QStringLiteral("Rockchip USB Device") << QStringLiteral("rockusb")
        << QStringLiteral("USB\\VID_2207&PID_110A") << quint16(0x2207) << quint16(0x110A)
        << QStringList{QStringLiteral("rk")};
    QTest::newRow("aw")
        << QStringLiteral("Allwinner FEL") << QStringLiteral("aw_usb")
        << QStringLiteral("USB\\VID_1F3A&PID_EFE8") << quint16(0x1F3A) << quint16(0xEFE8)
        << QStringList{QStringLiteral("aw")};
    QTest::newRow("adb")
        << QStringLiteral("Android Composite ADB Interface") << QStringLiteral("WinUSB")
        << QStringLiteral("USB\\VID_18D1&PID_4EE7&MI_01") << quint16(0x18D1) << quint16(0x4EE7)
        << QStringList{QStringLiteral("adb")};
    QTest::newRow("unknown")
        << QStringLiteral("USB Input Device") << QStringLiteral("HidUsb")
        << QStringLiteral("USB\\VID_046D&PID_C52B") << quint16(0x046D) << quint16(0xC52B)
        << QStringList{};
}

void TestDeviceLayer::matchDriverBindings()
{
    QFETCH(QString, description);
    QFETCH(QString, service);
    QFETCH(QString, hardwareId);
    QFETCH(quint16, vid);
    QFETCH(quint16, pid);
    QFETCH(QStringList, expectedKinds);

    RawDevice device;
    device.description = description;
    device.service = service;
    device.hardwareIds = {hardwareId};
    device.vid = vid;
    device.pid = pid;

    const auto matches = gf::device::matchDriverBindings(device);
    QStringList kinds;
    for (const auto& match : matches)
        kinds.append(driverKindId(match.kind));
    QCOMPARE(kinds, expectedKinds);
}

// ---------------------------------------------------------------------------
// batch helpers
// ---------------------------------------------------------------------------

void TestDeviceLayer::parseHashManifest()
{
    const QString text = QStringLiteral(
        "# firmware checksums\n"
        "aabbccdd00112233445566778899aabbccddeeff00112233445566778899aabb  boot.img\n"
        "1122334455667788990011223344556677889900112233445566778899001122 *vendor_boot.img\n"
        "SHA256 (super.img) = "
        "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\n"
        "\n"
        "not-a-hash-line\n");
    const auto entries = gf::device::parseHashManifest(text);
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).hash, QStringLiteral(
        "aabbccdd00112233445566778899aabbccddeeff00112233445566778899aabb"));
    QCOMPARE(entries.at(0).path, QStringLiteral("boot.img"));
    QCOMPARE(entries.at(1).path, QStringLiteral("vendor_boot.img"));
    QCOMPARE(entries.at(2).path, QStringLiteral("super.img"));
    QCOMPARE(entries.at(2).hash, QStringLiteral(
        "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"));
}

void TestDeviceLayer::expandRenamePattern_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<int>("index");
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("expected");

    QTest::newRow("index-padded") << QStringLiteral("{name}_{index3}") << 7
                                  << QStringLiteral("boot.img") << QStringLiteral("boot_007.img");
    QTest::newRow("index-plain") << QStringLiteral("{name}_{index}") << 12
                                 << QStringLiteral("recovery.img") << QStringLiteral("recovery_12.img");
    QTest::newRow("explicit-ext") << QStringLiteral("rom-{index2}.{ext}") << 3
                                  << QStringLiteral("a.bin") << QStringLiteral("rom-03.bin");
    QTest::newRow("filename") << QStringLiteral("{filename}") << 1
                              << QStringLiteral("super.img") << QStringLiteral("super.img");
    QTest::newRow("illegal-chars") << QStringLiteral("{name}:{index}") << 1
                                   << QStringLiteral("boot.img") << QStringLiteral("boot_1.img");
    QTest::newRow("empty-pattern") << QString() << 1 << QStringLiteral("boot.img")
                                   << QStringLiteral("boot.img");
}

void TestDeviceLayer::expandRenamePattern()
{
    QFETCH(QString, pattern);
    QFETCH(int, index);
    QFETCH(QString, fileName);
    QFETCH(QString, expected);
    QCOMPARE(gf::device::expandRenamePattern(pattern, index, fileName), expected);
}

void TestDeviceLayer::fileHashVerification()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("sample.bin"));
    const QByteArray data = QByteArrayLiteral("XMF Flash Tool device layer test payload");
    QVERIFY(gf::util::writeFile(path, data));
    const QString expected = gf::util::sha256Hex(data);

    QString actual;
    QString error;
    QVERIFY(gf::device::verifyFileHash(path, expected, QStringLiteral("sha256"), &actual, &error));
    QCOMPARE(actual, expected);
    QVERIFY(error.isEmpty());
    QVERIFY(!gf::device::verifyFileHash(path, QString(64, QLatin1Char('0')), QStringLiteral("sha256"),
                            nullptr, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!gf::device::verifyFileHash(dir.filePath(QStringLiteral("missing.bin")), expected,
                            QStringLiteral("sha256"), nullptr, &error));
    QVERIFY(!gf::device::verifyFileHash(path, expected, QStringLiteral("crc32"), nullptr, &error));
}

// ---------------------------------------------------------------------------
// batch queue
// ---------------------------------------------------------------------------

void TestDeviceLayer::batchRunsAllJobs()
{
    BatchJobManager manager(3);
    std::atomic<int> executed{0};
    for (int i = 0; i < 5; ++i) {
        BatchJob job;
        job.id = QString::number(i);
        job.device = QStringLiteral("device-%1").arg(i);
        job.label = job.id;
        job.run = [&executed](BatchJobContext& context) {
            context.setProgress(100);
            ++executed;
            return true;
        };
        manager.addJob(job);
    }
    QCOMPARE(manager.jobCount(), 5);
    const BatchSummary summary = manager.run();
    QCOMPARE(summary.total, 5);
    QCOMPARE(summary.succeeded, 5);
    QCOMPARE(summary.failed, 0);
    QCOMPARE(summary.canceled, 0);
    QCOMPARE(executed.load(), 5);
    QCOMPARE(manager.jobCount(), 0); // queue is drained
    QVERIFY(summary.ok());
}

void TestDeviceLayer::batchRespectsParallelLimit()
{
    BatchJobManager manager(2);
    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    std::mutex gateMutex;
    std::condition_variable gate;

    // Both concurrent jobs wait until at least two of them are active, so the
    // test verifies real concurrency without depending on scheduler timing.
    auto jobBody = [&active, &maxActive, &gateMutex, &gate](BatchJobContext&) {
        const int now = ++active;
        int previous = maxActive.load();
        while (now > previous && !maxActive.compare_exchange_weak(previous, now)) {
        }
        {
            std::unique_lock<std::mutex> lock(gateMutex);
            gate.notify_all();
            gate.wait_for(lock, std::chrono::seconds(2), [&active] { return active.load() >= 2; });
        }
        --active;
        return true;
    };

    for (int i = 0; i < 6; ++i) {
        BatchJob job;
        job.id = QString::number(i);
        job.device = QStringLiteral("dev-%1").arg(i);
        job.label = job.id;
        job.run = jobBody;
        manager.addJob(job);
    }
    const BatchSummary summary = manager.run();
    QCOMPARE(summary.succeeded, 6);
    QVERIFY2(maxActive.load() <= 2, qPrintable(QStringLiteral("max active = %1").arg(maxActive.load())));
    QVERIFY2(maxActive.load() >= 2, qPrintable(QStringLiteral("max active = %1").arg(maxActive.load())));
}

void TestDeviceLayer::batchSerializesSameDevice()
{
    BatchJobManager manager(4);
    std::atomic<int> activeSameDevice{0};
    std::atomic<int> maxActiveSameDevice{0};
    for (int i = 0; i < 4; ++i) {
        BatchJob job;
        job.id = QString::number(i);
        job.device = QStringLiteral("SAME-DEVICE");
        job.label = job.id;
        job.run = [&activeSameDevice, &maxActiveSameDevice](BatchJobContext&) {
            const int now = ++activeSameDevice;
            int previous = maxActiveSameDevice.load();
            while (now > previous && !maxActiveSameDevice.compare_exchange_weak(previous, now)) {
            }
            QThread::msleep(15);
            --activeSameDevice;
            return true;
        };
        manager.addJob(job);
    }
    const BatchSummary summary = manager.run();
    QCOMPARE(summary.succeeded, 4);
    QCOMPARE(maxActiveSameDevice.load(), 1);
}

void TestDeviceLayer::batchRetriesFailedJobs()
{
    BatchJobManager manager(1);
    std::atomic<int> attempts{0};
    BatchJob job;
    job.id = QStringLiteral("flaky");
    job.device = QStringLiteral("A");
    job.label = job.id;
    job.maxAttempts = 3;
    job.run = [&attempts](BatchJobContext&) {
        ++attempts;
        return attempts.load() >= 3;
    };
    manager.addJob(job);
    const BatchSummary summary = manager.run();
    QCOMPARE(summary.total, 1);
    QCOMPARE(summary.succeeded, 1);
    QCOMPARE(summary.failed, 0);
    QCOMPARE(summary.attempts, 3);
    QCOMPARE(attempts.load(), 3);
}

void TestDeviceLayer::batchCancelBeforeRun()
{
    BatchJobManager manager(2);
    std::atomic<int> executed{0};
    for (int i = 0; i < 3; ++i) {
        BatchJob job;
        job.id = QString::number(i);
        job.device = QStringLiteral("dev-%1").arg(i);
        job.label = job.id;
        job.run = [&executed](BatchJobContext&) {
            ++executed;
            return true;
        };
        manager.addJob(job);
    }
    manager.requestCancel();
    const BatchSummary summary = manager.run();
    QCOMPARE(summary.total, 3);
    QCOMPARE(summary.canceled, 3);
    QCOMPARE(summary.succeeded, 0);
    QCOMPARE(executed.load(), 0);
}

void TestDeviceLayer::batchCancelDuringRun()
{
    BatchJobManager manager(1);
    std::atomic<int> executed{0};
    BatchJob first;
    first.id = QStringLiteral("first");
    first.device = QStringLiteral("A");
    first.label = first.id;
    first.run = [&manager, &executed](BatchJobContext&) {
        ++executed;
        manager.requestCancel();
        return true;
    };
    manager.addJob(first);

    BatchJob second;
    second.id = QStringLiteral("second");
    second.device = QStringLiteral("A");
    second.label = second.id;
    second.run = [&executed](BatchJobContext&) {
        ++executed;
        return true;
    };
    manager.addJob(second);

    const BatchSummary summary = manager.run();
    QCOMPARE(summary.total, 2);
    QCOMPARE(summary.succeeded, 1);
    QCOMPARE(summary.canceled, 1);
    QCOMPARE(executed.load(), 1);
}

void TestDeviceLayer::batchAggregatesProgress()
{
    BatchJobManager manager(2);
    QList<int> reported;
    QStringList statuses;
    manager.setProgressCallback([&reported, &statuses](int percent, const QString& status) {
        reported.append(percent);
        statuses.append(status);
    });
    std::atomic<int> counter{0};
    for (int i = 0; i < 4; ++i) {
        BatchJob job;
        job.id = QString::number(i);
        job.device = QStringLiteral("dev-%1").arg(i);
        job.label = job.id;
        job.run = [&counter](BatchJobContext& context) {
            context.setProgress(50);
            ++counter;
            QThread::msleep(5);
            context.setProgress(100);
            return true;
        };
        manager.addJob(job);
    }
    const BatchSummary summary = manager.run();
    QCOMPARE(summary.succeeded, 4);
    QVERIFY(!reported.isEmpty());
    QCOMPARE(reported.last(), 100);
    QVERIFY(statuses.last().contains(QStringLiteral("批量任务完成")));
    // Progress is monotonic and never leaves [0, 100].
    int previous = -1;
    for (int percent : reported) {
        QVERIFY(percent >= 0 && percent <= 100);
        QVERIFY(percent >= previous);
        previous = percent;
    }
}

void TestDeviceLayer::batchReportsFailures()
{
    BatchJobManager manager(2);
    BatchJob failing;
    failing.id = QStringLiteral("bad");
    failing.device = QStringLiteral("A");
    failing.label = QStringLiteral("坏任务");
    failing.maxAttempts = 2;
    failing.run = [](BatchJobContext&) { return false; };
    manager.addJob(failing);

    BatchJob good;
    good.id = QStringLiteral("good");
    good.device = QStringLiteral("B");
    good.label = QStringLiteral("好任务");
    good.run = [](BatchJobContext&) { return true; };
    manager.addJob(good);

    const BatchSummary summary = manager.run();
    QCOMPARE(summary.total, 2);
    QCOMPARE(summary.succeeded, 1);
    QCOMPARE(summary.failed, 1);
    QCOMPARE(summary.attempts, 3); // failing job was attempted twice
    QVERIFY(!summary.ok());
    QCOMPARE(summary.failedJobs.size(), 1);
    QCOMPARE(summary.failedJobs.first(), QStringLiteral("坏任务"));
}

// ---------------------------------------------------------------------------
// tool registration / error behaviour
// ---------------------------------------------------------------------------

namespace {

/// Runs a registered tool handler and returns the Chinese error it threw
/// (empty when it completed without throwing).
QString runHandlerExpectingError(const QString& id, const QVariantMap& options = {})
{
    const auto handler = gf::ToolRegistry::instance().handler(id);
    if (!handler)
        return QStringLiteral("missing handler");
    gf::TaskContext context(1, QStringLiteral("device test"), nullptr);
    try {
        handler(context, options);
    } catch (const std::exception& exception) {
        return QString::fromUtf8(exception.what());
    } catch (...) {
        return QStringLiteral("non standard exception");
    }
    return {};
}

} // namespace

void TestDeviceLayer::driverDetectionShape()
{
    const auto statuses = detectDrivers();
    QCOMPARE(statuses.size(), 6);
    QStringList ids;
    for (const auto& status : statuses) {
        ids.append(driverKindId(status.kind));
        QVERIFY(!driverStateId(status.state).isEmpty());
        QVERIFY2(!status.message.isEmpty(), qPrintable(driverKindName(status.kind)));
    }
    // All six driver families are reported exactly once.
    QCOMPARE(ids, (QStringList{QStringLiteral("qdloader"), QStringLiteral("mtk"),
                               QStringLiteral("sprd"), QStringLiteral("rk"),
                               QStringLiteral("aw"), QStringLiteral("adb")}));
    const QString summary = driverSummary(statuses);
    QVERIFY(summary.contains(QStringLiteral("驱动状态检查")));
    QVERIFY(summary.contains(QStringLiteral("管理员权限")));
}

void TestDeviceLayer::handlerErrorsAreChinese()
{
    // The handler registry must be populated before probing handlers.
    registersDeviceTools();

    // "device.drivers" only reads SetupAPI and must never throw.
    QCOMPARE(runHandlerExpectingError(QStringLiteral("device.drivers")), QString());

    // Skip ADB assertions when a real device happens to be attached.
    const bool hasAdbDevice = !AdbClient::onlineDevices().isEmpty();
    if (!hasAdbDevice) {
        const QString adbError = runHandlerExpectingError(QStringLiteral("adb.devices"));
        QVERIFY(!adbError.isEmpty());
        QVERIFY2(adbError.contains(QStringLiteral("adb"), Qt::CaseInsensitive)
                     || adbError.contains(QStringLiteral("ADB")),
                 qPrintable(adbError));
    }

    const bool hasFastbootDevice = !FastbootClient::devices().isEmpty();
    if (!hasFastbootDevice) {
        const QString fastbootError = runHandlerExpectingError(QStringLiteral("fastboot.devices"));
        QVERIFY(!fastbootError.isEmpty());
        QVERIFY2(fastbootError.contains(QStringLiteral("fastboot"), Qt::CaseInsensitive),
                 qPrintable(fastbootError));
    }

    // Port scanning either reports ports or throws a readable Chinese error,
    // but must never crash.
    const QString portsError = runHandlerExpectingError(QStringLiteral("device.ports"));
    if (!portsError.isEmpty())
        QVERIFY(portsError.contains(QStringLiteral("设备")) || portsError.contains(QStringLiteral("端口")));

    // Driver installation is environment dependent (admin rights, bundled INF
    // packages, driver store state). Only require the tool to be registered and
    // runnable; do not actually install drivers from a unit test.
    QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("device.driver.install")));
    QVERIFY(gf::ToolRegistry::instance().handler(QStringLiteral("device.driver.install")) != nullptr);
}

void TestDeviceLayer::registersDeviceTools()
{
    static bool registered = false;
    if (!registered) {
        gf::registerDeviceTools();
        registered = true;
    }
    auto& registry = gf::ToolRegistry::instance();
    const QStringList expected = {
        QStringLiteral("adb.devices"),   QStringLiteral("adb.server"),
        QStringLiteral("adb.shell"),     QStringLiteral("adb.reboot"),
        QStringLiteral("adb.info"),      QStringLiteral("adb.partitions"),
        QStringLiteral("adb.logs"),      QStringLiteral("adb.wireless"),
        QStringLiteral("adb.forward"),   QStringLiteral("adb.push"),
        QStringLiteral("adb.pull"),      QStringLiteral("adb.install"),
        QStringLiteral("fastboot.devices"), QStringLiteral("fastboot.getvar"),
        QStringLiteral("fastboot.partitions"), QStringLiteral("fastboot.flash"),
        QStringLiteral("fastboot.erase"), QStringLiteral("fastboot.reboot"),
        QStringLiteral("fastboot.lock"), QStringLiteral("fastboot.info"),
        QStringLiteral("fastboot.oem"),  QStringLiteral("device.ports"),
        QStringLiteral("device.drivers"), QStringLiteral("device.driver.install"),
        QStringLiteral("batch.queue"),   QStringLiteral("batch.script"),
        QStringLiteral("batch.verify"),  QStringLiteral("batch.rename"),
    };
    const QStringList allowedCategories = {
        QStringLiteral("image"), QStringLiteral("qcom"), QStringLiteral("mtk"),
        QStringLiteral("bootloader"), QStringLiteral("sprd"), QStringLiteral("rk"),
        QStringLiteral("aw"), QStringLiteral("root"), QStringLiteral("unlock"),
        QStringLiteral("adb"), QStringLiteral("firmware"), QStringLiteral("batch"),
        QStringLiteral("bypass"),
    };
    for (const QString& id : expected) {
        QVERIFY2(registry.has(id), qPrintable(QStringLiteral("missing tool: %1").arg(id)));
        QVERIFY2(static_cast<bool>(registry.handler(id)),
                 qPrintable(QStringLiteral("missing handler: %1").arg(id)));
        const QVariantMap spec = registry.tool(id);
        const QString category = spec.value(QStringLiteral("category")).toString();
        QVERIFY2(allowedCategories.contains(category),
                 qPrintable(QStringLiteral("bad category for %1: %2").arg(id, category)));
        QVERIFY(!spec.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(!spec.value(QStringLiteral("desc")).toString().isEmpty());
    }
    // Device scanning and driver tools live in the bootloader category, ADB and
    // batch tools in their matching categories.
    QCOMPARE(registry.tool(QStringLiteral("device.ports"))
                 .value(QStringLiteral("category"))
                 .toString(),
             QStringLiteral("bootloader"));
    QCOMPARE(registry.tool(QStringLiteral("adb.devices"))
                 .value(QStringLiteral("category"))
                 .toString(),
             QStringLiteral("adb"));
    QCOMPARE(registry.tool(QStringLiteral("batch.queue"))
                 .value(QStringLiteral("category"))
                 .toString(),
             QStringLiteral("batch"));
}

#include "test_device.moc"
GF_TEST_CLASS(TestDeviceLayer)
