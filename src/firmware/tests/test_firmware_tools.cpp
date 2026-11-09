#include <QtTest>

#include "TestRegistry.h"

#include "app/ToolModules.h"

#include "FirmwareFiles.h"
#include "TaskManager.h"
#include "ToolRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

class TestFirmwareTools : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        gf::registerFirmwareTools();
    }

    void allToolsValid()
    {
        const QVariantList tools = gf::ToolRegistry::instance().tools();
        QSet<QString> ids;
        int firmwareTools = 0;
        int readyTools = 0;
        int plannedTools = 0;
        for (const QVariant& raw : tools) {
            const QVariantMap tool = raw.toMap();
            if (tool.value(QStringLiteral("category")).toString() != QStringLiteral("firmware"))
                continue;
            ++firmwareTools;
            const QString id = tool.value(QStringLiteral("id")).toString();
            QVERIFY(id.startsWith(QStringLiteral("firmware.")) || id.startsWith(QStringLiteral("miflash.")));
            QVERIFY2(!ids.contains(id), qPrintable(QStringLiteral("重复 id: %1").arg(id)));
            ids.insert(id);
            QVERIFY(!tool.value(QStringLiteral("name")).toString().isEmpty());
            QVERIFY(!tool.value(QStringLiteral("desc")).toString().isEmpty());
            QVERIFY(!tool.value(QStringLiteral("icon")).toString().isEmpty());
            QVERIFY(!tool.value(QStringLiteral("fields")).toList().isEmpty());
            const int maturity = tool.value(QStringLiteral("maturity")).toInt();
            if (maturity == 0) {
                ++readyTools;
                QVERIFY2(tool.value(QStringLiteral("runnable")).toBool(),
                         qPrintable(QStringLiteral("工具缺少 handler: %1").arg(id)));
                QVERIFY(gf::ToolRegistry::instance().handler(id) != nullptr);
            } else {
                ++plannedTools;
                QVERIFY(!tool.value(QStringLiteral("runnable")).toBool());
                QVERIFY(!tool.value(QStringLiteral("badge")).toString().isEmpty());
            }
        }
        QVERIFY2(firmwareTools >= 10,
                 qPrintable(QStringLiteral("firmware 工具数量过少: %1").arg(firmwareTools)));
        QVERIFY(readyTools >= 8);
        Q_UNUSED(plannedTools);
        const QStringList required = {
            QStringLiteral("firmware.xiaomi.search"),
            QStringLiteral("firmware.xiaomi.download"),
            QStringLiteral("firmware.official.lookup"),
            QStringLiteral("firmware.file.identify"),
            QStringLiteral("firmware.file.unpack"),
            QStringLiteral("firmware.file.hash"),
            QStringLiteral("firmware.file.mergeSplit"),
            QStringLiteral("firmware.file.matchModel"),
        };
        for (const QString& id : required)
            QVERIFY2(gf::ToolRegistry::instance().has(id), qPrintable(id));
    }

    void identifyHandler()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray sparse(4096, '\0');
        sparse[0] = char(0x3A);
        sparse[1] = char(0xFF);
        sparse[2] = char(0x26);
        sparse[3] = char(0xED);
        const QString path = QDir(dir.path()).filePath(QStringLiteral("system.img"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(sparse);
        file.close();

        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("firmware.file.identify"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        QVariantMap options;
        options.insert(QStringLiteral("input"), path);
        handler(ctx, options); // 不应抛异常
    }

    void matchModelHandler()
    {
        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("firmware.file.matchModel"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        QVariantMap options;
        options.insert(QStringLiteral("query"), QStringLiteral("miui_UMI_V14.0.4.0.TJBCNXM_abc_13.zip"));
        handler(ctx, options);
    }

    void officialLookupHandler()
    {
        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("firmware.official.lookup"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        QVariantMap options;
        options.insert(QStringLiteral("brand"), QStringLiteral("OPPO"));
        handler(ctx, options);

        bool threw = false;
        options.insert(QStringLiteral("brand"), QStringLiteral("不存在的品牌"));
        try {
            handler(ctx, options);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void mergeSplitHandler()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto write = [&](const QString& name, const QByteArray& data) {
            const QString path = QDir(dir.path()).filePath(name);
            QFile file(path);
            file.open(QIODevice::WriteOnly | QIODevice::Truncate);
            file.write(data);
            return path;
        };
        write(QStringLiteral("rom.z01"), QByteArray("AAAA"));
        write(QStringLiteral("rom.z02"), QByteArray("BB"));
        write(QStringLiteral("rom.zip"), QByteArray("ZZ"));

        const auto handler =
            gf::ToolRegistry::instance().handler(QStringLiteral("firmware.file.mergeSplit"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        const QString output = QDir(dir.path()).filePath(QStringLiteral("merged.zip"));
        QVariantMap options;
        options.insert(QStringLiteral("input"), QDir(dir.path()).filePath(QStringLiteral("rom.z01")));
        options.insert(QStringLiteral("output"), output);
        handler(ctx, options);
        QVERIFY(QFileInfo::exists(output));
        QFile merged(output);
        QVERIFY(merged.open(QIODevice::ReadOnly));
        QCOMPARE(merged.readAll(), QByteArray("AAAABBZZ"));
    }

    void hashHandler()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("fw.bin"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4096, 'F'));
        file.close();

        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("firmware.file.hash"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        QVariantList paths;
        paths << path;
        QVariantMap options;
        options.insert(QStringLiteral("input"), paths);
        handler(ctx, options);

        options.insert(QStringLiteral("expected"), QStringLiteral("00000000000000000000000000000000"));
        bool threw = false;
        try {
            handler(ctx, options);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void datasetHandlerOfflineFriendly()
    {
        // 数据集工具在无网时应抛出中文错误（不允许崩溃）；在网络可用时只校验不抛异常。
        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("firmware.xiaomi.search"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        QVariantMap options;
        options.insert(QStringLiteral("query"), QStringLiteral("umi"));
        options.insert(QStringLiteral("limit"), 3);
        try {
            handler(ctx, options);
        } catch (const std::exception& e) {
            const QString message = QString::fromUtf8(e.what());
            QVERIFY(!message.isEmpty());
            QVERIFY(message.contains(QStringLiteral("数据集")) ||
                    message.contains(QStringLiteral("网络")) ||
                    message.contains(QStringLiteral("固件")));
        }
    }
};

#include "test_firmware_tools.moc"
GF_TEST_CLASS(TestFirmwareTools)
