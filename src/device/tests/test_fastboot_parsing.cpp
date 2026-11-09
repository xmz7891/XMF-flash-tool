#include <QtTest>

#include <QMap>

#include "TestRegistry.h"
#include "device/FastbootClient.h"

#include <QMap>

using namespace gf::device;

// Regression fixture captured from a real Qualcomm device in fastboot mode:
// fastboot prints variables to stderr with a "(bootloader) " prefix and nests
// the partition name inside the variable, e.g. "partition-size:boot: 0x...".
class TestFastbootParsing : public QObject {
    Q_OBJECT

private slots:
    void getvarAllParsesStderrLines()
    {
        const QString output =
            QStringLiteral("(bootloader) product:uke\n"
                           "(bootloader) current-slot:a\n"
                           "(bootloader) slot-count:2\n"
                           "(bootloader) unlocked:no\n"
                           "(bootloader) secure:yes\n"
                           "(bootloader) battery-voltage:3392\n"
                           "(bootloader) variant:SMP UFS\n"
                           "(bootloader) erase-block-size: 0x1000\n"
                           "(bootloader) partition-type:persist:raw\n"
                           "(bootloader) partition-size:persist: 0x2000000\n"
                           "(bootloader) partition-type:boot:raw\n"
                           "(bootloader) partition-size:boot: 0x6000000\n"
                           "(bootloader) has-slot:boot:yes\n"
                           "(bootloader) partition-size:super: 0x400000000\n"
                           "Finished. Total time: 0.005s\n");
        const QMap<QString, QString> vars = parseGetvarOutput(output);
        QCOMPARE(vars.value(QStringLiteral("product")), QStringLiteral("uke"));
        QCOMPARE(vars.value(QStringLiteral("current-slot")), QStringLiteral("a"));
        QCOMPARE(vars.value(QStringLiteral("unlocked")), QStringLiteral("no"));
        QCOMPARE(vars.value(QStringLiteral("partition-size:persist")), QStringLiteral("0x2000000"));
        QCOMPARE(vars.value(QStringLiteral("partition-type:persist")), QStringLiteral("raw"));
        QCOMPARE(vars.value(QStringLiteral("partition-size:boot")), QStringLiteral("0x6000000"));
        QCOMPARE(vars.value(QStringLiteral("has-slot:boot")), QStringLiteral("yes"));
    }

    void partitionTableUsesNestedVariables()
    {
        const QString output =
            QStringLiteral("(bootloader) partition-type:persist:raw\n"
                           "(bootloader) partition-size:persist: 0x2000000\n"
                           "(bootloader) partition-type:boot:raw\n"
                           "(bootloader) partition-size:boot: 0x6000000\n"
                           "(bootloader) has-slot:boot:yes\n"
                           "(bootloader) partition-size:super: 0x400000000\n");
        const QList<FastbootPartition> parts = parsePartitionTable(parseGetvarOutput(output));
        QCOMPARE(parts.size(), 3);
        // sorted by name: boot, persist, super
        QCOMPARE(parts.at(0).name, QStringLiteral("boot"));
        QCOMPARE(parts.at(0).size, QStringLiteral("0x6000000"));
        QCOMPARE(parts.at(0).slot, QStringLiteral("yes"));
        QCOMPARE(parts.at(1).name, QStringLiteral("persist"));
        QCOMPARE(parts.at(2).name, QStringLiteral("super"));
        QCOMPARE(parts.at(2).size, QStringLiteral("0x400000000"));
    }

    void failedDiagnosticsAreIgnored()
    {
        const QString output = QStringLiteral(
            "getvar:all                                         FAILED (AdbWriteEndpointSync failed: (121))\n"
            "Finished. Total time: 5.005s\n");
        const QMap<QString, QString> vars = parseGetvarOutput(output);
        QVERIFY(vars.isEmpty());
    }

    void batteryIssueDetection()
    {
        QMap<QString, QString> low;
        low.insert(QStringLiteral("battery-soc-ok"), QStringLiteral("no"));
        low.insert(QStringLiteral("battery-voltage"), QStringLiteral("3300"));
        QVERIFY(!FastbootClient::batteryIssueFromVars(low).isEmpty());

        QMap<QString, QString> lowVoltage;
        lowVoltage.insert(QStringLiteral("battery-soc-ok"), QStringLiteral("yes"));
        lowVoltage.insert(QStringLiteral("battery-voltage"), QStringLiteral("3500"));
        QVERIFY(!FastbootClient::batteryIssueFromVars(lowVoltage).isEmpty());

        QMap<QString, QString> healthy;
        healthy.insert(QStringLiteral("battery-soc-ok"), QStringLiteral("yes"));
        healthy.insert(QStringLiteral("battery-voltage"), QStringLiteral("4000"));
        QVERIFY(FastbootClient::batteryIssueFromVars(healthy).isEmpty());

        QVERIFY(FastbootClient::batteryIssueFromVars({}).isEmpty());
    }

    void partitionSizeParsing()
    {
        QMap<QString, QString> vars;
        vars.insert(QStringLiteral("partition-size:boot_a"), QStringLiteral("0x6000000"));
        vars.insert(QStringLiteral("partition-size:super"), QStringLiteral("0x400000000"));
        QCOMPARE(FastbootClient::partitionSizeFromVars(vars, QStringLiteral("boot_a")),
                 quint64(100663296));
        QCOMPARE(FastbootClient::partitionSizeFromVars(vars, QStringLiteral("super")),
                 quint64(17179869184ULL));
        QCOMPARE(FastbootClient::partitionSizeFromVars(vars, QStringLiteral("missing")), quint64(0));
    }

    void rebootModeNormalization()
    {
        QCOMPARE(FastbootClient::normalizeRebootMode(QStringLiteral("system — 重启到系统")),
                 QStringLiteral("system"));
        QCOMPARE(FastbootClient::normalizeRebootMode(QStringLiteral("bootloader — 重启到 Bootloader")),
                 QStringLiteral("bootloader"));
        QCOMPARE(FastbootClient::normalizeRebootMode(QStringLiteral("fastbootd — 重启到 Fastbootd")),
                 QStringLiteral("fastbootd"));
        QCOMPARE(FastbootClient::normalizeRebootMode(QStringLiteral("edl — 重启到 EDL 9008")),
                 QStringLiteral("edl"));
        QCOMPARE(FastbootClient::normalizeRebootMode(QStringLiteral("poweroff — 关机")),
                 QStringLiteral("poweroff"));
        QCOMPARE(FastbootClient::normalizeRebootMode(QStringLiteral("recovery")),
                 QStringLiteral("recovery"));
    }

    void devicesOutputIsTolerant()
    {
        const QString output = QStringLiteral("d251427e\tfastboot\nd251427e\tfastbootd\n\n");
        const QList<FastbootDevice> devices = parseFastbootDevices(output);
        QCOMPARE(devices.size(), 2);
        QCOMPARE(devices.at(0).serial, QStringLiteral("d251427e"));
        QVERIFY(!devices.at(0).isFastbootD());
        QVERIFY(devices.at(1).isFastbootD());
    }

    void hexSizeParsingHandlesLongValues()
    {
        bool ok = false;
        // super partitions can exceed 32 bit on large UFS devices
        const quint64 size = QStringLiteral("0x400000000").toULongLong(&ok, 0);
        QVERIFY(ok);
        QCOMPARE(size, quint64(17179869184ULL));
    }
};

GF_TEST_CLASS(TestFastbootParsing)

#include "test_fastboot_parsing.moc"
