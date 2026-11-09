#include <QtTest>

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "TestRegistry.h"
#include "ToolRegistry.h"
#include "app/ToolModules.h"

class TestFlowTools : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        gf::registerFlowTools();
    }

    void allExpectedToolsAreRunnable()
    {
        const QStringList expected = {
            QStringLiteral("qcom.flow.partitions"), QStringLiteral("qcom.flow.backup"),
            QStringLiteral("qcom.flow.restore"),    QStringLiteral("qcom.flow.read"),
            QStringLiteral("qcom.flow.write"),      QStringLiteral("qcom.flow.erase"),
            QStringLiteral("qcom.flow.raw"),        QStringLiteral("qcom.flow.slot"),
            QStringLiteral("qcom.flow.checksum"),
            QStringLiteral("mtk.flow.partitions"),  QStringLiteral("mtk.flow.backup"),
            QStringLiteral("mtk.flow.restore"),     QStringLiteral("mtk.flow.read"),
            QStringLiteral("mtk.flow.write"),       QStringLiteral("mtk.flow.erase"),
            QStringLiteral("mtk.flow.flash"),       QStringLiteral("mtk.flow.preloader"),
            QStringLiteral("sprd.flow.partitions"), QStringLiteral("sprd.flow.flash"),
            QStringLiteral("sprd.flow.backup"),     QStringLiteral("sprd.flow.read"),
            QStringLiteral("sprd.flow.erase"),
            QStringLiteral("rk.flow.partitions"),   QStringLiteral("rk.flow.backup"),
            QStringLiteral("rk.flow.restore"),      QStringLiteral("rk.flow.read"),
            QStringLiteral("rk.flow.write"),        QStringLiteral("rk.flow.erase"),
            QStringLiteral("rk.flow.raw"),          QStringLiteral("rk.flow.loader"),
            QStringLiteral("aw.flow.hello"),        QStringLiteral("aw.flow.partitions"),
            QStringLiteral("aw.flow.backup"),       QStringLiteral("aw.flow.restore"),
            QStringLiteral("aw.flow.read"),         QStringLiteral("aw.flow.write"),
            QStringLiteral("aw.flow.exec"),
        };
        for (const auto& id : expected) {
            QVERIFY2(gf::ToolRegistry::instance().has(id), qPrintable(id));
            QVERIFY2(gf::ToolRegistry::instance().handler(id) != nullptr, qPrintable(id));
            const QVariantMap tool = gf::ToolRegistry::instance().tool(id);
            QCOMPARE(tool.value(QStringLiteral("group")).toString(), QStringLiteral("flow"));
            QVERIFY(tool.value(QStringLiteral("runnable")).toBool());
        }
    }

    void everyPlatformHasFlowTools()
    {
        QHash<QString, int> counts;
        for (const auto& entry : gf::ToolRegistry::instance().tools()) {
            const QVariantMap tool = entry.toMap();
            if (tool.value(QStringLiteral("group")).toString() != QLatin1String("flow"))
                continue;
            ++counts[tool.value(QStringLiteral("category")).toString()];
        }
        for (const char* category : {"qcom", "mtk", "sprd", "rk", "aw"}) {
            QVERIFY2(counts.value(QString::fromLatin1(category)) >= 5,
                     qPrintable(QStringLiteral("%1 flow 工具不足").arg(QLatin1String(category))));
        }
    }

    void toolsExposeUsefulFields()
    {
        // Backup tools must expose an output directory implemented with
        // fields::dir; file tools must use fields::path (UI contract).
        const QVariantList backupFields =
            gf::ToolRegistry::instance().fieldsOf(QStringLiteral("qcom.flow.backup"));
        bool hasDir = false;
        for (const auto& entry : backupFields) {
            const QVariantMap field = entry.toMap();
            if (field.value(QStringLiteral("type")).toString() == QStringLiteral("dir"))
                hasDir = true;
        }
        QVERIFY(hasDir);

        const QVariantList readFields =
            gf::ToolRegistry::instance().fieldsOf(QStringLiteral("qcom.flow.read"));
        bool hasPath = false;
        for (const auto& entry : readFields) {
            const QVariantMap field = entry.toMap();
            if (field.value(QStringLiteral("type")).toString() == QStringLiteral("path"))
                hasPath = true;
        }
        QVERIFY(hasPath);

        // Dangerous operations must require an explicit confirmation checkbox.
        const QVariantList eraseFields =
            gf::ToolRegistry::instance().fieldsOf(QStringLiteral("qcom.flow.erase"));
        bool hasConfirm = false;
        for (const auto& entry : eraseFields) {
            const QVariantMap field = entry.toMap();
            if (field.value(QStringLiteral("key")).toString() == QStringLiteral("confirm")
                && field.value(QStringLiteral("type")).toString() == QStringLiteral("bool"))
                hasConfirm = true;
        }
        QVERIFY(hasConfirm);
    }
};

#include "test_flow_tools.moc"
GF_TEST_CLASS(TestFlowTools)
