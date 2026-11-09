#include <QtTest>

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "TestRegistry.h"
#include "ToolRegistry.h"
#include "app/ToolModules.h"

class TestToolRegistration : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Each module's registration entry point must be callable exactly once
        // and must add runnable (handler backed) tools.
        gf::registerQcomTools();
        gf::registerMtkTools();
        gf::registerSprdTools();
        gf::registerRkTools();
        gf::registerAwTools();
    }

    void platformCategoriesExist()
    {
        QSet<QString> ids;
        for (const auto& category : gf::ToolRegistry::instance().categories())
            ids.insert(category.toMap().value(QStringLiteral("id")).toString());
        for (const char* required : {"qcom", "mtk", "sprd", "rk", "aw"})
            QVERIFY2(ids.contains(QString::fromLatin1(required)), required);
    }

    void everyPlatformHasRunnableTools()
    {
        const QVariantList tools = gf::ToolRegistry::instance().tools();
        QHash<QString, int> counts;
        for (const auto& entry : tools) {
            const QVariantMap tool = entry.toMap();
            const QString category = tool.value(QStringLiteral("category")).toString();
            if (category != QLatin1String("qcom") && category != QLatin1String("mtk")
                && category != QLatin1String("sprd") && category != QLatin1String("rk")
                && category != QLatin1String("aw")) {
                continue;
            }
            ++counts[category];
            QVERIFY2(tool.value(QStringLiteral("runnable")).toBool(),
                     qPrintable(QStringLiteral("工具不可运行：%1")
                                    .arg(tool.value(QStringLiteral("id")).toString())));
            QVERIFY(!tool.value(QStringLiteral("name")).toString().isEmpty());
            QVERIFY(!tool.value(QStringLiteral("desc")).toString().isEmpty());
        }
        for (const char* category : {"qcom", "mtk", "sprd", "rk", "aw"}) {
            QVERIFY2(counts.value(QString::fromLatin1(category)) >= 3,
                     qPrintable(QStringLiteral("%1 分类工具数量不足").arg(QLatin1String(category))));
        }
    }

    void expectedToolIdsExist()
    {
        const QStringList expected = {
            QStringLiteral("qcom.sahara.handshake"),
            QStringLiteral("qcom.sahara.upload"),
            QStringLiteral("qcom.elf.parse"),
            QStringLiteral("qcom.firehose.info"),
            QStringLiteral("qcom.firehose.read"),
            QStringLiteral("qcom.firehose.program"),
            QStringLiteral("qcom.firehose.patch"),
            QStringLiteral("qcom.firehose.erase"),
            QStringLiteral("mtk.brom.handshake"),
            QStringLiteral("mtk.brom.read32"),
            QStringLiteral("mtk.da.load"),
            QStringLiteral("mtk.da.memory"),
            QStringLiteral("mtk.preloader.parse"),
            QStringLiteral("mtk.scatter.parse"),
            QStringLiteral("sprd.u2s.handshake"),
            QStringLiteral("sprd.fdl.load"),
            QStringLiteral("sprd.pac.parse"),
            QStringLiteral("rk.device.info"),
            QStringLiteral("rk.reset"),
            QStringLiteral("rk.read"),
            QStringLiteral("rk.write"),
            QStringLiteral("rk.erase"),
            QStringLiteral("aw.fel.hello"),
            QStringLiteral("aw.fel.read"),
            QStringLiteral("aw.fel.write"),
            QStringLiteral("aw.fel.run"),
        };
        for (const auto& id : expected) {
            QVERIFY2(gf::ToolRegistry::instance().has(id), qPrintable(id));
            QVERIFY(gf::ToolRegistry::instance().handler(id) != nullptr);
        }
    }

    void deviceToolsCarryFields()
    {
        // Device-facing tools must expose at least a way to pick the interface
        // (optional) or their required input files; a tool cannot be fieldless
        // unless it is the port scanner.
        for (const char* id : {"qcom.sahara.handshake", "qcom.firehose.info", "mtk.da.load",
                               "sprd.fdl.load", "rk.write", "aw.fel.run"}) {
            const QVariantList fields = gf::ToolRegistry::instance().fieldsOf(
                QString::fromLatin1(id));
            QVERIFY2(!fields.isEmpty(), id);
        }
    }
};

#include "test_tool_registration.moc"
GF_TEST_CLASS(TestToolRegistration)
