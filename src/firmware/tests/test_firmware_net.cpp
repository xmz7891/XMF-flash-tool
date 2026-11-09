#include <QtTest>

#include "TestRegistry.h"

#include "FirmwareNet.h"

class TestFirmwareNet : public QObject
{
    Q_OBJECT

private slots:
    void buildOtaRequest()
    {
        const QByteArray xml = gf::firmware::buildMiuiOtaRequest(
            QStringLiteral("umi"), QStringLiteral("Stable"), QStringLiteral("V12.5.1.0.RJBCNXM"),
            QStringLiteral("zh_CN"));
        const QString text = QString::fromUtf8(xml);
        QVERIFY(text.startsWith(QStringLiteral("<?xml")));
        QVERIFY(text.contains(QStringLiteral("<mi_ota_request>")));
        QVERIFY(text.contains(QStringLiteral("<device>umi</device>")));
        QVERIFY(text.contains(QStringLiteral("<branch>stable</branch>")));
        QVERIFY(text.contains(QStringLiteral("<version>V12.5.1.0.RJBCNXM</version>")));
        QVERIFY(text.contains(QStringLiteral("<rom_version>V12.5.1.0.RJBCNXM</rom_version>")));
        QVERIFY(text.contains(QStringLiteral("<locale>zh_CN</locale>")));

        // 不提供版本时不写版本字段
        const QByteArray minimal = gf::firmware::buildMiuiOtaRequest(QStringLiteral("nuwa"), {});
        QVERIFY(!QString::fromUtf8(minimal).contains(QStringLiteral("<version>")));
        QVERIFY(QString::fromUtf8(minimal).contains(QStringLiteral("<device>nuwa</device>")));
    }

    void parseOtaResponse()
    {
        const QByteArray xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<miui_ota>\n"
            "  <update>\n"
            "    <version>V14.0.4.0.TJBCNXM</version>\n"
            "    <link>https://bigota.d.miui.com/V14.0.4.0/miui_NUWA_abc_13.zip</link>\n"
            "    <size>5.2 GB</size>\n"
            "    <md5>0123456789abcdef0123456789abcdef</md5>\n"
            "    <android>13</android>\n"
            "    <branch>Stable</branch>\n"
            "    <method>Recovery</method>\n"
            "    <date>2024-02-01</date>\n"
            "  </update>\n"
            "  <update>\n"
            "    <version>V14.0.3.0.TJBCNXM</version>\n"
            "    <url>https://bigota.d.miui.com/V14.0.3.0/old.zip</url>\n"
            "    <hash>feedfacefeedfacefeedfacefeedface</hash>\n"
            "  </update>\n"
            "</miui_ota>\n";
        const auto releases = gf::firmware::parseMiuiOtaResponse(xml, QStringLiteral("nuwa"));
        QCOMPARE(releases.size(), 2);
        QCOMPARE(releases.at(0).codename, QStringLiteral("nuwa"));
        QCOMPARE(releases.at(0).version, QStringLiteral("V14.0.4.0.TJBCNXM"));
        QCOMPARE(releases.at(0).link, QStringLiteral("https://bigota.d.miui.com/V14.0.4.0/miui_NUWA_abc_13.zip"));
        QCOMPARE(releases.at(0).md5, QStringLiteral("0123456789abcdef0123456789abcdef"));
        QCOMPARE(releases.at(0).android, QStringLiteral("13"));
        QCOMPARE(releases.at(0).branch, QStringLiteral("Stable"));
        QVERIFY(releases.at(0).sizeBytes > qint64(5.0 * 1024 * 1024 * 1024));
        QCOMPARE(releases.at(1).link, QStringLiteral("https://bigota.d.miui.com/V14.0.3.0/old.zip"));
        QCOMPARE(releases.at(1).md5, QStringLiteral("feedfacefeedfacefeedfacefeedface"));

        QVERIFY(gf::firmware::parseMiuiOtaResponse(QByteArray("<html>not xml</html>"),
                                                  QStringLiteral("umi"))
                    .isEmpty());
        QVERIFY(gf::firmware::parseMiuiOtaResponse(QByteArray(), {}).isEmpty());
    }

    void invalidUrlReturnsError()
    {
        const auto result = gf::firmware::httpGet(QStringLiteral("not-a-valid-url"), 3000);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }

    void liveDatasetFetch()
    {
        // 先做一次短超时探测，网络不可用时立即跳过（保证离线环境测试全绿且不拖慢）。
        const auto probe = gf::firmware::httpGet(gf::firmware::defaultLatestDatasetUrl(), 8000);
        if (!probe.ok)
            QSKIP("网络不可用，跳过公开数据集在线检查");
        const auto dataset = gf::firmware::fetchXiaomiDataset(false, 20000);
        if (!dataset.ok)
            QSKIP("公开数据集不可用，跳过在线检查");
        QVERIFY(dataset.releases.size() > 1000);
        QVERIFY(!dataset.devices.isEmpty());

        const auto umi = gf::firmware::filterReleases(dataset.releases, QStringLiteral("umi"), {}, {},
                                                     10);
        if (umi.isEmpty())
            QSKIP("数据集当前没有 umi 机型记录（可能已迁移），跳过细项断言");
        QVERIFY(!umi.first().link.isEmpty());
        QVERIFY(umi.first().link.startsWith(QStringLiteral("https://")));
        if (!umi.first().md5.isEmpty())
            QCOMPARE(umi.first().md5.size(), 32);
    }
};

#include "test_firmware_net.moc"
GF_TEST_CLASS(TestFirmwareNet)
