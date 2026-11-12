#include <QGuiApplication>
#include <QFontDatabase>
#include <QCommandLineParser>
#include <QDir>
#include <QQuickStyle>
#include <QIcon>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QScreen>
#include <QTimer>

#include "AppController.h"
#include "FreeNotice.h"

#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "Log.h"
#include "NativeWindow.h"
#include "TaskManager.h"
#include "ToolRegistry.h"
#include "Util.h"

#include <cstdio>

using namespace gf;

int main(int argc, char* argv[])
{
    QQuickWindow::setDefaultAlphaBuffer(true);
    QGuiApplication app(argc, argv);
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/images/xmf_icon.png")));
    QGuiApplication::setOrganizationName(QStringLiteral("XMF"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("xmf.flash"));
    QGuiApplication::setApplicationName(QStringLiteral("XMF Flash Tool"));
    QGuiApplication::setApplicationVersion(QStringLiteral(XMF_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("XMF Flash Tool"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // ---- fonts -------------------------------------------------------------
    // Desktop utility typography: YaHei UI at 12px (Inter only for numerals).
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/InterVariable.ttf"));
    {
        QFont font = app.font();
        font.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI"),
                          QStringLiteral("Inter")});
        font.setPixelSize(12);
        QGuiApplication::setFont(font);
    }

    // ---- command line ------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("XMF Flash Tool"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Capture the main window and quit"),
                               QStringLiteral("path"));
    QCommandLineOption shotDelayOpt(QStringLiteral("screenshot-delay"), QStringLiteral("Delay before capture (ms)"),
                                    QStringLiteral("ms"), QStringLiteral("1200"));
    QCommandLineOption pageOpt(QStringLiteral("page"), QStringLiteral("Open a page by nav index before running"),
                               QStringLiteral("index"));
    QCommandLineOption themeOpt(QStringLiteral("theme"), QStringLiteral("Force appearance: dark or light"),
                                QStringLiteral("mode"));
    QCommandLineOption modeOpt(QStringLiteral("mode"), QStringLiteral("Open a built-in workbench: miflash|spflash|spdflash|rkdevtool|phoenixsuit"),
                               QStringLiteral("id"));
    QCommandLineOption embedOpt(QStringLiteral("embed"), QStringLiteral("Embed an official tool by id (miflash|spflashtool|sprd)"),
                                QStringLiteral("id"));
    QCommandLineOption flashModeOpt(QStringLiteral("flash-mode"),
                                    QStringLiteral("Open a flash workbench: fastboot:<platform>|recovery:<platform>|qualcomm-edl|miflash|spflash|spdflash|rkdevtool|phoenixsuit"),
                                    QStringLiteral("id"));
    QCommandLineOption runToolOpt(QStringLiteral("run-tool"), QStringLiteral("Run a registered tool headlessly (for testing)"),
                                  QStringLiteral("id"));
    QCommandLineOption toolOptionOpt(QStringLiteral("option"), QStringLiteral("Tool option as key=value (repeatable)"),
                                     QStringLiteral("key=value"));
    QCommandLineOption dumpToolsOpt(QStringLiteral("dump-tools"),
                                    QStringLiteral("Print the registered tool list as JSON and exit"));
    QCommandLineOption diagnosticsOpt(QStringLiteral("diagnostics"),
                                      QStringLiteral("Write the diagnostics report and exit"));
    QCommandLineOption autoReadOpt(QStringLiteral("auto-read"),
                                   QStringLiteral("Open the flash center and read device partitions"));
    QCommandLineOption adbCmdOpt(QStringLiteral("adb-cmd"),
                                 QStringLiteral("Run one adb shell command and print the output"),
                                 QStringLiteral("command"));
    QCommandLineOption fastbootCmdOpt(QStringLiteral("fastboot-cmd"),
                                      QStringLiteral("Run one fastboot command and print the output"),
                                      QStringLiteral("arguments"));
    QCommandLineOption acceptDisclaimerOpt(QStringLiteral("accept-disclaimer"),
                                           QStringLiteral("Accept the disclaimer (persisted) and exit"));
    QCommandLineOption noAutoSwitchOpt(QStringLiteral("no-auto-switch"),
                                       QStringLiteral("Disable automatic flash-mode switching for this launch"));
    QCommandLineOption previewDisclaimerOpt(QStringLiteral("preview-disclaimer"),
                                            QStringLiteral("Force the first-run disclaimer to show"));
    QCommandLineOption batchToolOpt(QStringLiteral("batch-tool"),
                                    QStringLiteral("Run a tool over --batch-files and exit"), QStringLiteral("id"));
    QCommandLineOption batchFieldOpt(QStringLiteral("batch-field"),
                                     QStringLiteral("Option key that receives the file path"),
                                     QStringLiteral("key"));
    QCommandLineOption batchFilesOpt(QStringLiteral("batch-files"),
                                     QStringLiteral("Semicolon separated file list"),
                                     QStringLiteral("files"));
    QCommandLineOption batchParallelOpt(QStringLiteral("batch-parallel"),
                                        QStringLiteral("Parallel task limit (default 2)"),
                                        QStringLiteral("n"));
    parser.addOption(shotOpt);
    parser.addOption(shotDelayOpt);
    parser.addOption(pageOpt);
    parser.addOption(themeOpt);
    parser.addOption(modeOpt);
    parser.addOption(embedOpt);
    parser.addOption(flashModeOpt);
    parser.addOption(runToolOpt);
    parser.addOption(toolOptionOpt);
    parser.addOption(dumpToolsOpt);
    parser.addOption(diagnosticsOpt);
    parser.addOption(autoReadOpt);
    parser.addOption(adbCmdOpt);
    parser.addOption(fastbootCmdOpt);
    parser.addOption(acceptDisclaimerOpt);
    parser.addOption(noAutoSwitchOpt);
    parser.addOption(previewDisclaimerOpt);
    parser.addOption(batchToolOpt);
    parser.addOption(batchFieldOpt);
    parser.addOption(batchFilesOpt);
    parser.addOption(batchParallelOpt);
    parser.process(app);

    // Mandatory integrity gate: without the encrypted free-notice module the
    // application must not load any UI or tooling.
    if (!gf::notice::verify()) {
        qCritical("XMF: free-notice integrity check failed; refusing to start");
        fprintf(stderr, "XMF: free-notice integrity check failed; refusing to start\n");
        return 4;
    }

    // ---- caches and logs ---------------------------------------------------
    QDir().mkpath(util::appDataDir());

    // ---- backend -----------------------------------------------------------
    AppController& controller = AppController::instance();
    if (parser.isSet(themeOpt)) {
        const QString mode = parser.value(themeOpt).toLower();
        if (mode == QLatin1String("dark") || mode == QLatin1String("light"))
            controller.settings()->setThemeMode(mode);
    }
    const bool noAutoSwitch = parser.isSet(noAutoSwitchOpt);
    controller.initialize();

    if (parser.isSet(acceptDisclaimerOpt)) {
        controller.acceptDisclaimer();
        printf("disclaimer accepted\n");
        return 0;
    }

    // Normal GUI launches are single-instance: a second launch focuses the
    // existing window instead of fighting over USB handles / logs.
    const bool guiMode = !parser.isSet(shotOpt) && !parser.isSet(runToolOpt)
                         && !parser.isSet(dumpToolsOpt) && !parser.isSet(diagnosticsOpt)
                         && !parser.isSet(batchToolOpt) && !parser.isSet(adbCmdOpt)
                         && !parser.isSet(fastbootCmdOpt);
    if (parser.isSet(previewDisclaimerOpt))
        controller.previewDisclaimer();
    else if (!guiMode)
        controller.skipDisclaimerForAutomation();
    const QString instanceName = QStringLiteral("XMF-Flash-Tool-Instance");
    if (guiMode) {
        QLocalSocket probe;
        probe.connectToServer(instanceName);
        if (probe.waitForConnected(300)) {
            probe.write("activate\n");
            probe.waitForBytesWritten(200);
            probe.disconnectFromServer();
            return 0;
        }
    }
    QLocalServer instanceServer;
    if (guiMode) {
        QLocalServer::removeServer(instanceName);
        instanceServer.listen(instanceName);
    }

    if (parser.isSet(adbCmdOpt) || parser.isSet(fastbootCmdOpt)) {
        const bool adb = parser.isSet(adbCmdOpt);
        const QVariantMap result = adb
            ? controller.runAdbCommand(QString(), parser.value(adbCmdOpt))
            : controller.runFastbootCommand(QString(), parser.value(fastbootCmdOpt));
        const QString output = result.value(QStringLiteral("output")).toString().trimmed();
        if (!output.isEmpty())
            printf("%s\n", output.toUtf8().constData());
        if (!result.value(QStringLiteral("ok")).toBool()) {
            fprintf(stderr, "%s\n", result.value(QStringLiteral("error")).toString().toUtf8().constData());
            return 2;
        }
        return 0;
    }

    if (parser.isSet(batchToolOpt)) {
        QStringList files = parser.value(batchFilesOpt).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        if (files.isEmpty() || !parser.isSet(batchFieldOpt)) {
            fprintf(stderr, "batch: --batch-field and --batch-files are required\n");
            return 4;
        }
        const int parallel = parser.isSet(batchParallelOpt)
                                 ? parser.value(batchParallelOpt).toInt()
                                 : 2;
        QEventLoop loop;
        bool finished = false;
        QObject::connect(&controller, &gf::AppController::batchStateChanged, &app,
                         [&controller, &loop, &finished]() {
                             const QVariantMap state = controller.batchState();
                             if (state.value(QStringLiteral("active")).toBool())
                                 return;
                             finished = true;
                             loop.quit();
                         });
        QVariantList fileList;
        for (const QString& file : files)
            fileList.append(file);
        const int total = controller.startBatch(parser.value(batchToolOpt),
                                                parser.value(batchFieldOpt), fileList, {}, parallel);
        if (total <= 0) {
            fprintf(stderr, "batch: failed to start\n");
            return 4;
        }
        if (!finished)
            loop.exec();
        const QVariantMap state = controller.batchState();
        printf("BATCH total=%d done=%d failed=%d parallel=%d\n", total,
               state.value(QStringLiteral("done")).toInt(),
               state.value(QStringLiteral("failed")).toInt(),
               state.value(QStringLiteral("parallel")).toInt());
        return state.value(QStringLiteral("failed")).toInt() > 0 ? 2 : 0;
    }

    if (parser.isSet(diagnosticsOpt)) {
        const QString path = controller.writeDiagnostics();
        printf("%s\n", path.toUtf8().constData());
        return path.isEmpty() ? 3 : 0;
    }

    if (parser.isSet(dumpToolsOpt)) {
        QJsonArray array;
        for (const QVariant& entry : gf::ToolRegistry::instance().tools()) {
            const QVariantMap tool = entry.toMap();
            QJsonObject object;
            object.insert(QStringLiteral("id"), tool.value(QStringLiteral("id")).toString());
            object.insert(QStringLiteral("category"), tool.value(QStringLiteral("category")).toString());
            object.insert(QStringLiteral("group"), tool.value(QStringLiteral("group")).toString());
            object.insert(QStringLiteral("name"), tool.value(QStringLiteral("name")).toString());
            object.insert(QStringLiteral("runnable"), tool.value(QStringLiteral("runnable")).toBool());
            object.insert(QStringLiteral("desc"), tool.value(QStringLiteral("desc")).toString());
            array.append(object);
        }
        QJsonObject root;
        root.insert(QStringLiteral("count"), array.size());
        root.insert(QStringLiteral("tools"), array);
        printf("%s\n", QJsonDocument(root).toJson(QJsonDocument::Indented).constData());
        return 0;
    }

    // Headless tool runner: run one ToolRegistry handler without any UI. This is
    // used for automated verification of the real device paths (fastboot/adb).
    if (parser.isSet(runToolOpt)) {
        QVariantMap options;
        for (const QString& raw : parser.values(toolOptionOpt)) {
            const int eq = raw.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            options.insert(raw.left(eq), raw.mid(eq + 1));
        }
        auto& tasks = gf::TaskManager::instance();
        QObject::connect(&tasks, &gf::TaskManager::taskLog, &app,
                         [](int, int level, const QString& message) {
                             fprintf(stdout, "[%d] %s\n", level, qPrintable(message));
                             fflush(stdout);
                         });
        QObject::connect(&tasks, &gf::TaskManager::taskFinished, &app,
                         [&app](int, bool ok, const QString& summary) {
                             fprintf(stdout, "RESULT %s: %s\n", ok ? "OK" : "FAIL", qPrintable(summary));
                             fflush(stdout);
                             QCoreApplication::exit(ok ? 0 : 2);
                         });
        const int taskId = controller.runTool(parser.value(runToolOpt), options);
        if (taskId <= 0) {
            fprintf(stderr, "tool did not start\n");
            return 4;
        }
        return app.exec();
    }

    qmlRegisterSingletonInstance("XMF", 1, 0, "App", &controller);
    qmlRegisterUncreatableType<gf::LogModel>("XMF", 1, 0, "LogModel", QStringLiteral("internal"));
    qmlRegisterUncreatableType<gf::Settings>("XMF", 1, 0, "Settings", QStringLiteral("internal"));
    qmlRegisterUncreatableType<gf::TaskManager>("XMF", 1, 0, "TaskManager", QStringLiteral("internal"));
    qmlRegisterUncreatableType<gf::packets::PacketStore>("XMF", 1, 0, "PacketStore", QStringLiteral("internal"));
    qmlRegisterUncreatableType<gf::FirmwareWorkspace>("XMF", 1, 0, "FirmwareWorkspace", QStringLiteral("internal"));
    qmlRegisterUncreatableType<gf::FirmwareBrowser>("XMF", 1, 0, "FirmwareBrowser", QStringLiteral("internal"));
    qmlRegisterUncreatableType<gf::OfficialTools>("XMF", 1, 0, "OfficialTools", QStringLiteral("internal"));
    qmlRegisterUncreatableType<gf::EmbeddedApp>("XMF", 1, 0, "EmbeddedApp", QStringLiteral("internal"));
    qmlRegisterUncreatableType<gf::device::PortsModel>("XMF", 1, 0, "PortsModel", QStringLiteral("internal"));

    // ---- UI ----------------------------------------------------------------
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [](const QUrl& url) {
            fprintf(stderr, "QML object creation failed: %s\n", qPrintable(url.toString()));
            QCoreApplication::exit(2);
        },
        Qt::QueuedConnection);
    engine.rootContext()->setContextProperty(QStringLiteral("XMF_NO_AUTO_SWITCH"), noAutoSwitch);
    engine.loadFromModule("XMF", "Main");
    if (engine.rootObjects().isEmpty())
        return 3;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window) {
        if (guiMode) {
            QObject::connect(&instanceServer, &QLocalServer::newConnection, window,
                             [&instanceServer, window]() {
                                 while (QLocalSocket* connection = instanceServer.nextPendingConnection()) {
                                     connection->deleteLater();
                                     window->show();
                                     window->raise();
                                     window->requestActivate();
                                 }
                             });
        }
        // Keep the window opaque: transparent/layered windows cannot host the
        // embedded vendor tools (their child HWNDs would not paint).
        window->setIcon(QIcon(QStringLiteral(":/images/xmf_icon.png")));
        controller.embedded()->setMainWindow(window);
        QTimer::singleShot(60, window, [window, &controller]() {
            native::applyGlass(window, controller.darkTheme());
        });
        QObject::connect(&controller, &AppController::darkThemeChanged, window, [window, &controller]() {
            native::applyGlass(window, controller.darkTheme());
        });

        if (parser.isSet(flashModeOpt)) {
            const QString modeId = parser.value(flashModeOpt);
            QTimer::singleShot(120, window, [window, modeId]() {
                QMetaObject::invokeMethod(window, "openFlashMode", Q_ARG(QVariant, modeId));
            });
        }
        if (parser.isSet(pageOpt)) {
            const int pageIndex = parser.value(pageOpt).toInt();
            QTimer::singleShot(120, window, [window, pageIndex]() {
                QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, pageIndex));
            });
        }
        if (parser.isSet(embedOpt)) {
            const QString toolId = parser.value(embedOpt);
            QTimer::singleShot(320, window, [window, toolId]() {
                QMetaObject::invokeMethod(window, "openEmbeddedTool", Q_ARG(QVariant, toolId));
            });
        }
        if (parser.isSet(autoReadOpt)) {
            QTimer::singleShot(900, window, [window]() {
                QMetaObject::invokeMethod(window, "autoReadDevicePartitions");
            });
        }
        if (parser.isSet(modeOpt)) {
            const QString modeId = parser.value(modeOpt);
            QTimer::singleShot(220, window, [window, modeId]() {
                QMetaObject::invokeMethod(window, "openModePage", Q_ARG(QVariant, modeId));
            });
        }

        if (parser.isSet(shotOpt)) {
            const QString path = parser.value(shotOpt);
            const int delay = parser.value(shotDelayOpt).toInt();
            QTimer::singleShot(delay, window, [window, path]() {
                const QImage image = window->grabWindow();
                if (!image.isNull())
                    image.save(path);
                QCoreApplication::quit();
            });
        }
    }

    return app.exec();
}

