#include <QtTest>

#include "TestRegistry.h"

#include "app/ToolModules.h"

#include "Sparse.h"
#include "TaskManager.h"
#include "ToolRegistry.h"

#include <QSet>
#include <QTemporaryDir>

class TestImageTools : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 测试进程里只注册一次 image 工具。
        gf::registerImageTools();
    }

    void allToolsValid()
    {
        const QVariantList tools = gf::ToolRegistry::instance().tools();
        QSet<QString> ids;
        int imageTools = 0;
        int readyTools = 0;
        int plannedTools = 0;
        for (const QVariant& raw : tools) {
            const QVariantMap tool = raw.toMap();
            if (tool.value(QStringLiteral("category")).toString() != QStringLiteral("image"))
                continue;
            ++imageTools;
            const QString id = tool.value(QStringLiteral("id")).toString();
            QVERIFY(id.startsWith(QStringLiteral("image.")));
            QVERIFY(!ids.contains(id));
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
        QVERIFY2(imageTools >= 35,
                 qPrintable(QStringLiteral("image 工具数量过少: %1").arg(imageTools)));
        QVERIFY2(readyTools >= 33,
                 qPrintable(QStringLiteral("可用工具数量过少: %1").arg(readyTools)));
        Q_UNUSED(plannedTools); // 占位工具数量随实现推进变化，不强制下限
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.payload.unpack")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.boot.unpack")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.super.split")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.sparse.pack")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.vbmeta.disable")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.gpt.repair")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.zip.info")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.elf.info")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.convert.img2lzo")));
        QVERIFY(gf::ToolRegistry::instance().has(QStringLiteral("image.cpio.unpack")));
    }

    void runHandlerFromRegistry()
    {
        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("image.convert.number"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        QVariantMap options;
        options.insert(QStringLiteral("value"), QStringLiteral("0x1000"));
        handler(ctx, options); // 不应抛异常

        bool threw = false;
        options.insert(QStringLiteral("value"), QStringLiteral("not-a-number"));
        try {
            handler(ctx, options);
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void detectToolRunsOnSparseFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString rawPath = gf::img::joinPath(dir.path(), QStringLiteral("raw.bin"));
        const QString sparsePath = gf::img::joinPath(dir.path(), QStringLiteral("sparse.img"));
        gf::img::writeFileOrThrow(rawPath, QByteArray(4096 * 4, '\0'));
        gf::img::sparse::packFile(rawPath, sparsePath, 4096, true, true);

        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("image.convert.detect"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        QVariantMap options;
        options.insert(QStringLiteral("input"), sparsePath);
        handler(ctx, options); // 识别为 sparse，不应抛异常
    }

    void missingOptionsThrow()
    {
        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("image.sparse.pack"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        bool threw = false;
        try {
            handler(ctx, QVariantMap());
        } catch (const std::exception&) {
            threw = true;
        }
        QVERIFY(threw);
    }

    void batchScanAndExport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString sourceDir = gf::img::joinPath(dir.path(), QStringLiteral("src"));
        const QString rawPath = gf::img::joinPath(sourceDir, QStringLiteral("raw.bin"));
        gf::img::writeFileOrThrow(rawPath, QByteArray(4096 * 3, char(0x21)));
        gf::img::sparse::packFile(rawPath, gf::img::joinPath(sourceDir, QStringLiteral("sparse.img")),
                                  4096, true, true);

        const QString outDir = gf::img::joinPath(dir.path(), QStringLiteral("exp"));
        const auto handler = gf::ToolRegistry::instance().handler(QStringLiteral("image.convert.batchScan"));
        QVERIFY(handler);
        gf::TaskContext ctx(9999, QStringLiteral("test"), &gf::TaskManager::instance());
        QVariantMap options;
        options.insert(QStringLiteral("source"), sourceDir);
        options.insert(QStringLiteral("output"), outDir);
        options.insert(QStringLiteral("copy"), true);
        options.insert(QStringLiteral("hash"), true);
        handler(ctx, options);

        QVERIFY(QFileInfo::exists(gf::img::joinPath(outDir, QStringLiteral("raw.bin"))));
        QVERIFY(QFileInfo::exists(gf::img::joinPath(outDir, QStringLiteral("sparse.img"))));
        const QString manifest =
            QString::fromUtf8(gf::img::readFileOrThrow(
                gf::img::joinPath(outDir, QStringLiteral("xmf_image_manifest.txt"))));
        QVERIFY(manifest.contains(QStringLiteral("sparse.img")));
        QVERIFY(manifest.contains(QStringLiteral("Android sparse image")));
        QVERIFY(manifest.contains(QStringLiteral("raw.bin")));
    }
};

#include "test_image_tools.moc"
GF_TEST_CLASS(TestImageTools)
