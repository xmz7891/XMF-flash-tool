#include <QtTest>

#include <QByteArray>

#include "TestRegistry.h"
#include "usb/MockTransport.h"
#include "usb/ProtocolSupport.h"
#include "usb/TaskReporter.h"
#include "usb/UsbIo.h"
#include "usb/UsbTransport.h"

namespace {

QByteArray repeatByte(char value, int count)
{
    return QByteArray(count, value);
}

} // namespace

class TestUsbTransport : public QObject {
    Q_OBJECT

private slots:
    void chipHints_data()
    {
        QTest::addColumn<int>("vid");
        QTest::addColumn<int>("pid");
        QTest::addColumn<QString>("expected");

        QTest::newRow("qcom-9008") << 0x05C6 << 0x9008 << QStringLiteral("qcom-edl");
        QTest::newRow("qcom-900e") << 0x05C6 << 0x900E << QStringLiteral("qcom-edl");
        QTest::newRow("mtk-brom") << 0x0E8D << 0x0003 << QStringLiteral("mtk-brom");
        QTest::newRow("mtk-preloader") << 0x0E8D << 0x2000 << QStringLiteral("mtk-preloader");
        QTest::newRow("sprd") << 0x1782 << 0x4D00 << QStringLiteral("sprd-fdl");
        QTest::newRow("rk") << 0x2207 << 0x110A << QStringLiteral("rk");
        QTest::newRow("aw") << 0x1F3A << 0xEFE8 << QStringLiteral("aw-fel");
        QTest::newRow("unknown") << 0x1234 << 0x5678 << QString();
        QTest::newRow("zero") << 0 << 0 << QString();
    }

    void chipHints()
    {
        QFETCH(int, vid);
        QFETCH(int, pid);
        QFETCH(QString, expected);
        QCOMPARE(gf::usb::chipHintForIds(quint16(vid), quint16(pid)), expected);
    }

    void platformForHint_data()
    {
        QTest::addColumn<QString>("hint");
        QTest::addColumn<int>("expected");

        auto row = [this](const char* name, const char* hint, gf::usb::ChipPlatform platform) {
            QTest::newRow(name) << QString::fromLatin1(hint) << int(platform);
        };
        row("qcom", "qcom-edl", gf::usb::ChipPlatform::Qualcomm);
        row("mtk-brom", "mtk-brom", gf::usb::ChipPlatform::MediaTek);
        row("mtk-preloader", "mtk-preloader", gf::usb::ChipPlatform::MediaTek);
        row("mtk-da", "mtk-da", gf::usb::ChipPlatform::MediaTek);
        row("sprd", "sprd-fdl", gf::usb::ChipPlatform::Spreadtrum);
        row("rk", "rk", gf::usb::ChipPlatform::Rockchip);
        row("aw", "aw-fel", gf::usb::ChipPlatform::Allwinner);
        row("unknown", "", gf::usb::ChipPlatform::Unknown);
    }

    void platformForHint()
    {
        QFETCH(QString, hint);
        QFETCH(int, expected);
        QCOMPARE(int(gf::usb::platformForHint(hint)), expected);
    }

    void mockOpenClose()
    {
        gf::usb::MockTransport transport;
        QVERIFY(!transport.isOpen());
        gf::usb::PortInfo port;
        port.devicePath = QStringLiteral("\\\\?\\usb#vid_05c6&pid_9008");
        port.chipHint = QStringLiteral("qcom-edl");
        QVERIFY(transport.open(port));
        QVERIFY(transport.isOpen());
        QCOMPARE(transport.openedPort().devicePath, port.devicePath);
        transport.close();
        QVERIFY(transport.wasClosed());
        QVERIFY(!transport.isOpen());
    }

    void mockOpenFailure()
    {
        gf::usb::MockTransport transport;
        transport.setOpenResult(false, QStringLiteral("模拟打开失败"));
        QVERIFY(!transport.open({}));
        QCOMPARE(transport.errorString(), QStringLiteral("模拟打开失败"));
    }

    void mockReadQueueSplitsChunks()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(repeatByte('A', 10));
        QCOMPARE(transport.read(4, 100), repeatByte('A', 4));
        QCOMPARE(transport.read(4, 100), repeatByte('A', 4));
        QCOMPARE(transport.read(4, 100), repeatByte('A', 2));
        QVERIFY(transport.read(4, 100).isEmpty());
        QCOMPARE(transport.readCount(), 4);
    }

    void mockWriteHookProducesReplies()
    {
        gf::usb::MockTransport transport;
        transport.setWriteHook([](const QByteArray& written) {
            return written.toUpper();
        });
        transport.write(QByteArrayLiteral("hello"));
        QCOMPARE(transport.read(16, 100), QByteArrayLiteral("HELLO"));
        QCOMPARE(transport.writeCount(), 1);
        QCOMPARE(transport.writes().first(), QByteArrayLiteral("hello"));
    }

    void mockWriteFailure()
    {
        gf::usb::MockTransport transport;
        transport.setWriteResult(-1, QStringLiteral("管道错误"));
        QCOMPARE(transport.write(QByteArrayLiteral("x")), qint64(-1));
        QCOMPARE(transport.errorString(), QStringLiteral("管道错误"));
    }

    void readExactLoopsUntilComplete()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArrayLiteral("1234"));
        transport.queueRead(QByteArrayLiteral("5678"));
        const QByteArray data = gf::usb::readExact(transport, 8, 500);
        QCOMPARE(data, QByteArrayLiteral("12345678"));
    }

    void readExactTimesOut()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArrayLiteral("12"));
        bool threw = false;
        try {
            gf::usb::readExact(transport, 8, 20);
        } catch (const std::exception& error) {
            threw = true;
            QVERIFY(QString::fromUtf8(error.what()).contains(QStringLiteral("超时")));
        }
        QVERIFY(threw);
    }

    void writeAllRetriesShortWrites()
    {
        gf::usb::MockTransport transport;
        transport.setWriteResult(3); // every call only accepts 3 bytes
        gf::usb::writeAll(transport, QByteArrayLiteral("0123456789"), 100);
        QCOMPARE(transport.written(), QByteArrayLiteral("0123456789"));
        QCOMPARE(transport.writeCount(), 4);
    }

    void readUntilTerminator()
    {
        gf::usb::MockTransport transport;
        transport.queueRead(QByteArrayLiteral("<a>"));
        transport.queueRead(QByteArrayLiteral("</response>"));
        const QByteArray data =
            gf::usb::readUntil(transport, QByteArrayLiteral("</response>"), 200);
        QCOMPARE(data, QByteArrayLiteral("<a></response>"));
    }

    void controlTransferRecordsAndResponds()
    {
        gf::usb::MockTransport transport;
        transport.queueControlResponse(QByteArrayLiteral("\x01\x02"));
        QByteArray response;
        QVERIFY(transport.controlTransfer(0xC0, 0x01, 1, 2, QByteArray(), &response, 100));
        QCOMPARE(response, QByteArrayLiteral("\x01\x02"));
        QCOMPARE(transport.controls().size(), 1);
        QCOMPARE(transport.controls().first().request, quint8(0x01));
    }

    void reporterForwardsAndCancels()
    {
        int progress = -1;
        QString status;
        QStringList logs;
        bool canceled = false;
        gf::proto::Reporter reporter(
            [&](int percent, const QString& text) {
                progress = percent;
                status = text;
            },
            [&](gf::proto::Reporter::Level, const QString& text) { logs.append(text); },
            [&] { return canceled; });

        reporter.progress(42, QStringLiteral("处理中"));
        reporter.info(QStringLiteral("一条日志"));
        QCOMPARE(progress, 42);
        QCOMPARE(status, QStringLiteral("处理中"));
        QCOMPARE(logs, QStringList{QStringLiteral("一条日志")});

        reporter.checkCanceled(); // not canceled yet -> no throw
        canceled = true;
        QVERIFY(reporter.isCanceled());
        bool threw = false;
        try {
            reporter.checkCanceled();
        } catch (const gf::proto::Canceled&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void noDeviceMessagesAreChinese()
    {
        const QString qcom = gf::proto::noDeviceMessage(gf::usb::ChipPlatform::Qualcomm);
        QVERIFY(qcom.contains(QStringLiteral("9008")));
        QVERIFY(qcom.contains(QStringLiteral("EDL")));
        for (auto platform : {gf::usb::ChipPlatform::Qualcomm, gf::usb::ChipPlatform::MediaTek,
                              gf::usb::ChipPlatform::Spreadtrum, gf::usb::ChipPlatform::Rockchip,
                              gf::usb::ChipPlatform::Allwinner}) {
            const QString message = gf::proto::noDeviceMessage(platform);
            QVERIFY(!message.isEmpty());
            QVERIFY(message.contains(QStringLiteral("未检测到")));
        }
    }

    void createTransportWithoutPort()
    {
        gf::usb::PortInfo emptyPort;
        QVERIFY(gf::usb::createTransport(emptyPort) == nullptr);
    }

    void enumerateDoesNotCrash()
    {
        // No hardware is required: enumeration must simply return a list.
        const auto ports = gf::usb::enumerateFlashPorts();
        for (const auto& port : ports) {
            QVERIFY(!port.chipHint.isEmpty());
            QVERIFY(gf::usb::platformForHint(port.chipHint) != gf::usb::ChipPlatform::Unknown);
        }
    }
};

#include "test_usb_transport.moc"
GF_TEST_CLASS(TestUsbTransport)
