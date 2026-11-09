#include <QtTest>

#include "TestRegistry.h"

#include "FirmwareCommon.h"
#include "FirmwareData.h"

#include <QDir>
#include <QFileInfo>

namespace {

QString findRepoPath(const QString& relative)
{
    QString base = QDir::currentPath();
    for (int depth = 0; depth < 7; ++depth) {
        const QString candidate = QDir(base).filePath(relative);
        if (QFileInfo::exists(candidate))
            return candidate;
        QDir up(base);
        if (!up.cdUp())
            break;
        base = up.absolutePath();
    }
    return {};
}

QByteArray sampleLatestYaml()
{
    return QByteArray(
        "- android: '4.2'\n"
        "  branch: Stable\n"
        "  codename: HM2013022\n"
        "  date: 2015-03-26\n"
        "  link: https://bigota.d.miui.com/JHACNBH22.0/miui_HM2_JHACNBH22.0_a4bd134f02_4.2.zip\n"
        "  md5: a4bd134f029bd7d93d1cf595a4edb60a\n"
        "  method: Recovery\n"
        "  name: Redmi 1 TD China\n"
        "  size: 315.9 MB\n"
        "  version: JHACNBH22.0\n"
        "- android: '13'\n"
        "  branch: Weekly\n"
        "  codename: nuwa\n"
        "  date: 2024-02-01\n"
        "  link: https://bigota.d.miui.com/V14.0.4.0.TJBCNXM/miui_NUWA_abc_13.zip\n"
        "  md5: 0123456789abcdef0123456789abcdef\n"
        "  method: Fastboot\n"
        "  name: 小米13\n"
        "  size: 5.2 GB\n"
        "  version: V14.0.4.0.TJBCNXM\n");
}

QByteArray sampleDevicesYaml()
{
    return QByteArray("HM2013022:\n"
                      "- Redmi 1 China\n"
                      "- Redmi 1 TD China\n"
                      "umi:\n"
                      "- Mi 10\n"
                      "- 小米10\n"
                      "nuwa:\n"
                      "- Xiaomi 13\n"
                      "- 小米13\n");
}

gf::firmware::FirmwareRelease makeRelease(const QString& codename, const QString& name,
                                          const QString& version, const QString& branch,
                                          const QString& method, const QString& date,
                                          const QString& link)
{
    gf::firmware::FirmwareRelease release;
    release.codename = codename;
    release.name = name;
    release.version = version;
    release.branch = branch;
    release.method = method;
    release.date = date;
    release.link = link;
    release.sizeBytes = 1024;
    return release;
}

} // namespace

class TestFirmwareData : public QObject
{
    Q_OBJECT

private slots:
    void parseLatestYamlOk()
    {
        QString error;
        const auto releases = gf::firmware::parseLatestYaml(sampleLatestYaml(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(releases.size(), 2);

        const auto& first = releases.at(0);
        QCOMPARE(first.codename, QStringLiteral("HM2013022"));
        QCOMPARE(first.name, QStringLiteral("Redmi 1 TD China"));
        QCOMPARE(first.version, QStringLiteral("JHACNBH22.0"));
        QCOMPARE(first.branch, QStringLiteral("Stable"));
        QCOMPARE(first.android, QStringLiteral("4.2"));
        QCOMPARE(first.method, QStringLiteral("Recovery"));
        QCOMPARE(first.md5, QStringLiteral("a4bd134f029bd7d93d1cf595a4edb60a"));
        QCOMPARE(first.sizeText, QStringLiteral("315.9 MB"));
        QVERIFY(first.sizeBytes > qint64(315.0 * 1024 * 1024));
        QVERIFY(first.sizeBytes < qint64(316.0 * 1024 * 1024));
        QVERIFY(first.link.startsWith(QStringLiteral("https://bigota.d.miui.com/")));

        const auto& second = releases.at(1);
        QCOMPARE(second.codename, QStringLiteral("nuwa"));
        QCOMPARE(second.branch, QStringLiteral("Weekly"));
        QCOMPARE(second.method, QStringLiteral("Fastboot"));
        QVERIFY(second.sizeBytes > qint64(5.0 * 1024 * 1024 * 1024));

        const QVariantMap map = first.toVariant();
        QCOMPARE(map.value(QStringLiteral("branchText")).toString(), QStringLiteral("稳定版"));
        QVERIFY(!gf::firmware::releaseSummary(first).isEmpty());
        QVERIFY(gf::firmware::releaseSummary(first).contains(QStringLiteral("HM2013022")));
        QCOMPARE(gf::firmware::branchText(QStringLiteral("Stable")), QStringLiteral("稳定版"));
        QCOMPARE(gf::firmware::branchText(QStringLiteral("Weekly")), QStringLiteral("开发版"));
    }

    void parseLatestYamlRejectsGarbage()
    {
        QString error;
        const auto releases = gf::firmware::parseLatestYaml(QByteArray("not a yaml dataset"), &error);
        QVERIFY(releases.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void parseDevicesYamlOk()
    {
        const auto devices = gf::firmware::parseDevicesYaml(sampleDevicesYaml());
        QCOMPARE(devices.size(), 3);
        QVERIFY(devices.contains(QStringLiteral("umi")));
        QCOMPARE(devices.value(QStringLiteral("umi")).size(), 2);
        QVERIFY(devices.value(QStringLiteral("umi")).contains(QStringLiteral("小米10")));
        QCOMPARE(devices.value(QStringLiteral("nuwa")).value(0), QStringLiteral("Xiaomi 13"));
    }

    void offlineBrandsDatabase()
    {
        const QString path = findRepoPath(QStringLiteral("data/firmware/brands.json"));
        QVERIFY2(!path.isEmpty(), "找不到 data/firmware/brands.json");
        const QByteArray data = gf::firmware::readFile(path);
        QString error;
        const auto brands = gf::firmware::parseBrandsJson(data, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(brands.size() >= 8);

        bool hasXiaomi = false;
        bool hasOppo = false;
        bool hasSamsung = false;
        for (const auto& brand : brands) {
            QVERIFY(!brand.id.isEmpty());
            QVERIFY(!brand.name.isEmpty());
            QVERIFY(!brand.official.isEmpty());
            for (const auto& page : brand.official) {
                const QString url = page.value(QStringLiteral("url")).toString();
                QVERIFY(url.startsWith(QStringLiteral("https://")));
                QVERIFY(!page.value(QStringLiteral("title")).toString().isEmpty());
            }
            if (brand.id == QStringLiteral("xiaomi"))
                hasXiaomi = true;
            if (brand.id == QStringLiteral("oppo"))
                hasOppo = true;
            if (brand.id == QStringLiteral("samsung"))
                hasSamsung = true;
        }
        QVERIFY(hasXiaomi);
        QVERIFY(hasOppo);
        QVERIFY(hasSamsung);
    }

    void offlineModelsDatabase()
    {
        const QString path = findRepoPath(QStringLiteral("data/firmware/models.json"));
        QVERIFY2(!path.isEmpty(), "找不到 data/firmware/models.json");
        const QByteArray data = gf::firmware::readFile(path);
        QString error;
        const auto models = gf::firmware::parseModelsJson(data, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(models.size() >= 20);
        bool hasUmi = false;
        for (const auto& model : models) {
            QVERIFY(!model.codename.isEmpty());
            QVERIFY(!model.brand.isEmpty());
            QVERIFY(!model.names.isEmpty());
            if (model.codename == QStringLiteral("umi")) {
                hasUmi = true;
                QCOMPARE(model.brand, QStringLiteral("xiaomi"));
                QVERIFY(model.names.contains(QStringLiteral("小米10")));
            }
        }
        QVERIFY(hasUmi);
    }

    void filterReleases()
    {
        QVector<gf::firmware::FirmwareRelease> releases;
        releases << makeRelease(QStringLiteral("umi"), QStringLiteral("小米10"),
                                QStringLiteral("V12.5.8.0"), QStringLiteral("Stable"),
                                QStringLiteral("Recovery"), QStringLiteral("2021-08-01"),
                                QStringLiteral("https://a/umi.zip"));
        releases << makeRelease(QStringLiteral("umi"), QStringLiteral("小米10"),
                                QStringLiteral("21.9.8"), QStringLiteral("Weekly"),
                                QStringLiteral("Fastboot"), QStringLiteral("2021-09-08"),
                                QStringLiteral("https://a/umi.tgz"));
        releases << makeRelease(QStringLiteral("nuwa"), QStringLiteral("小米13"),
                                QStringLiteral("V14.0.4.0"), QStringLiteral("Stable"),
                                QStringLiteral("Fastboot"), QStringLiteral("2024-02-01"),
                                QStringLiteral("https://a/nuwa.tgz"));

        auto all = gf::firmware::filterReleases(releases, QString());
        QCOMPARE(all.size(), 3);
        // 最新在前
        QCOMPARE(all.first().codename, QStringLiteral("nuwa"));

        auto stable = gf::firmware::filterReleases(releases, QString(), QStringLiteral("Stable"));
        QCOMPARE(stable.size(), 2);
        auto weekly = gf::firmware::filterReleases(releases, QString(), QStringLiteral("Weekly"));
        QCOMPARE(weekly.size(), 1);
        auto fastboot = gf::firmware::filterReleases(releases, QString(), QString(),
                                                     QStringLiteral("Fastboot"));
        QCOMPARE(fastboot.size(), 2);
        auto query = gf::firmware::filterReleases(releases, QStringLiteral("小米10"));
        QCOMPARE(query.size(), 2);
        auto noMatch = gf::firmware::filterReleases(releases, QStringLiteral("不存在的机型"));
        QVERIFY(noMatch.isEmpty());
        auto limited = gf::firmware::filterReleases(releases, QString(), QString(), QString(), 1);
        QCOMPARE(limited.size(), 1);
    }

    void resolveCodename()
    {
        const auto devices = gf::firmware::parseDevicesYaml(sampleDevicesYaml());
        QCOMPARE(gf::firmware::resolveCodename(QStringLiteral("小米10"), devices),
                 QStringLiteral("umi"));
        QCOMPARE(gf::firmware::resolveCodename(QStringLiteral("UMI"), devices), QStringLiteral("umi"));
        QCOMPARE(gf::firmware::resolveCodename(QStringLiteral("Xiaomi 13"), devices),
                 QStringLiteral("nuwa"));
        QCOMPARE(gf::firmware::resolveCodename(QStringLiteral("unknown-dev"), devices),
                 QStringLiteral("UNKNOWN-DEV"));
        const auto found = gf::firmware::searchDeviceCodenames(devices, QStringLiteral("redmi"), 5);
        QVERIFY(found.contains(QStringLiteral("HM2013022")));
    }

    void matchModelsAndCodenames()
    {
        QString error;
        const auto models = gf::firmware::parseModelsJson(
            gf::firmware::readFile(findRepoPath(QStringLiteral("data/firmware/models.json"))), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        const auto byName = gf::firmware::matchModels(models, QStringLiteral("我的小米10"));
        QVERIFY(!byName.isEmpty());
        QCOMPARE(byName.first().codename, QStringLiteral("umi"));

        const auto byFile =
            gf::firmware::matchModels(models, QStringLiteral("miui_UMI_V14.0.4.0.TJBCNXM_abc_13.zip"));
        QVERIFY(!byFile.isEmpty());
        QCOMPARE(byFile.first().codename, QStringLiteral("umi"));

        QCOMPARE(gf::firmware::codenameFromFileName(
                     QStringLiteral("miui_UMI_V14.0.4.0.TJBCNXM_abc_13.zip"), models),
                 QStringLiteral("umi"));
        // 机型库未收录时退化为字母+数字代号
        QCOMPARE(gf::firmware::codenameFromFileName(QStringLiteral("miui_HM2013022_abc.zip"), models),
                 QStringLiteral("HM2013022"));

        QCOMPARE(gf::firmware::brandDisplayName(QStringLiteral("xiaomi")), QStringLiteral("小米"));
        QCOMPARE(gf::firmware::brandDisplayName(QStringLiteral("samsung")), QStringLiteral("三星"));
        QCOMPARE(gf::firmware::normalizeQuery(QStringLiteral("  UMI  ")), QStringLiteral("umi"));
    }
};

#include "test_firmware_data.moc"
GF_TEST_CLASS(TestFirmwareData)
