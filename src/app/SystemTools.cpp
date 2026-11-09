#include "ToolModules.h"
#include "ToolRegistry.h"
#include "Util.h"

#include "image/Compression.h"
#include "packets/PacketStore.h"

#include "device/AdbClient.h"
#include "device/FastbootClient.h"
#include "device/Ports.h"

#include <QFile>
#include <QTemporaryDir>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QSysInfo>
#include <QProcess>
#include <QStorageInfo>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace gf {

namespace {

void systemInfoTool(TaskContext& ctx, const QVariantMap& options)
{
    Q_UNUSED(options);
    ctx.info(QStringLiteral("正在采集系统信息…"));
    ctx.setProgress(10);

    const QString product = QSysInfo::prettyProductName();
    const QString kernel = QSysInfo::kernelVersion();
    const QString arch = QSysInfo::currentCpuArchitecture();
    const auto* screen = QGuiApplication::primaryScreen();
    const QString screenInfo = screen ? QStringLiteral("%1 × %2 @ %3x")
                                           .arg(screen->geometry().width())
                                           .arg(screen->geometry().height())
                                           .arg(screen->devicePixelRatio())
                                     : QStringLiteral("unknown");

    ctx.setProgress(40);
    ctx.info(QStringLiteral("系统: %1").arg(product));
    ctx.info(QStringLiteral("内核: %1  架构: %2").arg(kernel, arch));
    ctx.info(QStringLiteral("屏幕: %1").arg(screenInfo));
    ctx.info(QStringLiteral("进程位数: %1").arg(sizeof(void*) == 8 ? QStringLiteral("64-bit")
                                                                 : QStringLiteral("32-bit")));
    ctx.info(QStringLiteral("管理员权限: %1").arg(util::isAdmin() ? QStringLiteral("是")
                                                                   : QStringLiteral("否")));

    ctx.setProgress(70);
    const QStorageInfo storage = QStorageInfo::root();
    ctx.info(QStringLiteral("磁盘: %1  可用 %2 / %3")
                 .arg(QDir::toNativeSeparators(storage.rootPath()),
                      util::formatBytes(storage.bytesAvailable()), util::formatBytes(storage.bytesTotal())));

    ctx.setProgress(100);
    ctx.ok(QStringLiteral("系统信息采集完成"));
}

void hashTool(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = options.value(QStringLiteral("file")).toString();
    const QString algo = options.value(QStringLiteral("algo")).toString();
    ctx.info(QStringLiteral("计算 %1 哈希: %2").arg(algo, path));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(QStringLiteral("无法打开文件: %1").arg(path).toStdString());

    const QCryptographicHash::Algorithm algorithm =
        algo == QLatin1String("MD5") ? QCryptographicHash::Md5
        : algo == QLatin1String("SHA1") ? QCryptographicHash::Sha1
                                        : QCryptographicHash::Sha256;
    QCryptographicHash hash(algorithm);
    const qint64 total = file.size();
    qint64 read = 0;
    QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
    while (!file.atEnd()) {
        ctx.checkCanceled();
        const qint64 n = file.read(buffer.data(), buffer.size());
        if (n <= 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), n));
        read += n;
        ctx.setProgress(total > 0 ? int(read * 100 / total) : 100);
    }
    const QString digest = QString::fromLatin1(hash.result().toHex());
    ctx.ok(QStringLiteral("%1 = %2").arg(algo, digest));
    ctx.info(QStringLiteral("文件大小: %1").arg(util::formatBytes(total)));
}


void payloadDumperGoTool(TaskContext& ctx, const QVariantMap& options)
{
    const QString payload = options.value(QStringLiteral("payload")).toString();
    const QString outDir = options.value(QStringLiteral("output")).toString();
    if (payload.isEmpty())
        throw std::runtime_error("请选择 payload.bin");
    if (outDir.isEmpty())
        throw std::runtime_error("请选择输出目录");

    const QString exe = util::dataDir() + QStringLiteral("/thirdparty/oss/payload-dumper-go/payload-dumper-go.exe");
    if (!QFileInfo::exists(exe))
        throw std::runtime_error("未找到 payload-dumper-go.exe，请检查 data/thirdparty/oss 目录");
    util::ensureDir(outDir);

    ctx.info(QStringLiteral("使用开源工具 payload-dumper-go 解包: %1").arg(payload));
    QProcess process;
    process.setWorkingDirectory(QFileInfo(exe).absolutePath());
    QStringList args{payload, QStringLiteral("-o"), outDir};
    const QString partitions = options.value(QStringLiteral("partitions")).toString().trimmed();
    if (!partitions.isEmpty())
        args << QStringLiteral("-partitions") << partitions;
    process.start(exe, args);
    if (!process.waitForStarted(10000))
        throw std::runtime_error("无法启动 payload-dumper-go");

    ctx.setStatus(QStringLiteral("正在解包 payload…"));
    qint64 totalMs = 0;
    while (process.state() != QProcess::NotRunning) {
        ctx.checkCanceled();
        process.waitForReadyRead(200);
        const QByteArray chunk = process.readAllStandardOutput() + process.readAllStandardError();
        if (!chunk.isEmpty()) {
            const QString text = QString::fromUtf8(chunk).trimmed();
            for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QString trimmed = line.trimmed();
                if (trimmed.isEmpty())
                    continue;
                ctx.info(trimmed.left(400));
                if (trimmed.contains(QLatin1Char('%'))) {
                    const int percent = trimmed.section(QLatin1Char('%'), 0, 0).section(QLatin1Char(' '), -1).toInt();
                    if (percent > 0 && percent <= 100)
                        ctx.setProgress(percent);
                }
            }
        }
        totalMs += 200;
        if (totalMs > 30 * 60 * 1000)
            throw std::runtime_error("payload 解包超时");
    }
    if (process.exitStatus() != QProcess::NormalExit) {
        process.kill();
        throw std::runtime_error("payload-dumper-go 异常退出");
    }
    if (process.exitCode() != 0)
        throw std::runtime_error(QStringLiteral("payload-dumper-go 返回错误码 %1").arg(process.exitCode()).toStdString());
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("解包完成，输出目录: %1").arg(outDir));
}


void selfTestTool(TaskContext& ctx, const QVariantMap& options)
{
    Q_UNUSED(options);
    int passed = 0;
    int warned = 0;
    int critical = 0;

    auto check = [&](const QString& name, bool cond, const QString& detail = {}, bool isCritical = false) {
        if (cond) {
            ++passed;
            ctx.ok(detail.isEmpty() ? name : QStringLiteral("%1 · %2").arg(name, detail));
        } else if (isCritical) {
            ++critical;
            ctx.error(detail.isEmpty() ? name : QStringLiteral("%1 · %2").arg(name, detail));
        } else {
            ++warned;
            ctx.warn(detail.isEmpty() ? name : QStringLiteral("%1 · %2").arg(name, detail));
        }
    };

    ctx.setStatus(QStringLiteral("检查目录与文件…"));
    for (const auto& pair : std::initializer_list<std::pair<QString, QString>>{
             {QStringLiteral("数据目录"), util::dataDir()},
             {QStringLiteral("日志目录"), util::logsDir()},
             {QStringLiteral("缓存目录"), util::cacheDir()}}) {
        const bool ok = util::ensureDir(pair.second);
        check(pair.first, ok, pair.second, true);
    }

    QTemporaryDir probe;
    bool writable = false;
    if (probe.isValid()) {
        QFile file(probe.filePath(QStringLiteral("probe.txt")));
        writable = file.open(QIODevice::WriteOnly) && file.write("xmf", 3) == 3;
    }
    check(QStringLiteral("临时目录可写"), writable, probe.path(), true);
    ctx.setProgress(15);

    const QVariantList tools = ToolRegistry::instance().tools();
    int runnable = 0;
    for (const QVariant& entry : tools)
        if (entry.toMap().value(QStringLiteral("runnable")).toBool())
            ++runnable;
    check(QStringLiteral("工具注册表"), runnable > 150,
          QStringLiteral("%1 / %2 可运行").arg(runnable).arg(tools.size()), true);
    ctx.setProgress(30);

    // Bundled binaries (version-only invocations, no device access).
    const QString dataDir = util::dataDir();
    struct BinaryCheck {
        QString label;
        QString path;
    };
    const QList<BinaryCheck> binaries = {
        {QStringLiteral("adb 可执行文件"), dataDir + QStringLiteral("/platform-tools/adb.exe")},
        {QStringLiteral("fastboot 可执行文件"), dataDir + QStringLiteral("/platform-tools/fastboot.exe")},
        {QStringLiteral("zstd 可执行文件"), dataDir + QStringLiteral("/thirdparty/oss/zstd/zstd.exe")},
    };
    for (const BinaryCheck& binary : binaries) {
        const bool exists = QFileInfo::exists(binary.path);
        check(binary.label, exists, QFileInfo(binary.path).absoluteFilePath(), true);
        if (!exists)
            continue;
        const util::ProcessResult result =
            util::runProcess(binary.path, {QStringLiteral("--version")}, 5000);
        check(QStringLiteral("%1 可执行").arg(binary.label),
              result.started && result.exitCode == 0,
              QString::fromLocal8Bit(result.stdoutData + result.stderrData).left(40).trimmed());
    }
    ctx.setProgress(50);

    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();
    check(QStringLiteral("引导包数量"), store.packetIds().size() >= 3,
          QStringLiteral("%1 个 / %2 机型").arg(store.packetIds().size()).arg(store.modelCount()));

    const QString mtkDa = store.modelResource(QStringLiteral("package_mtk_edl-master"),
                                              QStringLiteral("MT6895"), QStringLiteral("DA1"));
    check(QStringLiteral("MTK DA 资源可解出"), QFileInfo(mtkDa).size() > 1000,
          QFileInfo(mtkDa).fileName());
    const QString qcLoader = store.modelResource(QStringLiteral("package_qualcomm_edl-master"),
                                                 QStringLiteral("439全系"), QStringLiteral("Loader"));
    check(QStringLiteral("QC Loader 资源可解出"), QFileInfo(qcLoader).size() > 1000,
          QFileInfo(qcLoader).fileName());
    ctx.setProgress(70);

    int driverCount = 0;
    QDirIterator driverIterator(dataDir + QStringLiteral("/drivers"), {QStringLiteral("*.inf")},
                                QDir::Files, QDirIterator::Subdirectories);
    while (driverIterator.hasNext()) {
        driverIterator.next();
        ++driverCount;
    }
    check(QStringLiteral("随附驱动包"), driverCount > 0, QStringLiteral("%1 个 inf").arg(driverCount));

    const QList<device::PortRecord> ports = device::enumeratePorts();
    int flashingPorts = 0;
    for (const device::PortRecord& port : ports)
        if (port.isFlashing() || port.identity.isAdb || port.identity.isFastboot)
            ++flashingPorts;
    check(QStringLiteral("设备端口枚举"), !ports.isEmpty(),
          QStringLiteral("%1 个设备节点，其中刷机/调试 %2 个").arg(ports.size()).arg(flashingPorts));

    QString adbError;
    const auto adbDevices = device::AdbClient::devices(&adbError);
    check(QStringLiteral("ADB 可执行文件"), device::AdbClient::available(),
          device::AdbClient::adbPath(), true);
    check(QStringLiteral("Fastboot 可执行文件"), device::FastbootClient::available(),
          device::FastbootClient::fastbootPath(), true);
    ctx.info(QStringLiteral("当前连接：ADB %1 台 · Fastboot %2 台")
                 .arg(adbDevices.size())
                 .arg(device::FastbootClient::devices().size()));

    ctx.setProgress(85);
    QByteArray sample;
    sample.reserve(65536);
    for (int i = 0; i < 65536; ++i)
        sample.append(char(i % 251));
    const QByteArray compressed = img::compress::gzipCompress(sample);
    const QByteArray restored = img::compress::gzipDecompress(compressed);
    check(QStringLiteral("gzip 往返校验"), !compressed.isEmpty() && restored == sample,
          QStringLiteral("%1 → %2 字节").arg(sample.size()).arg(compressed.size()), true);
    ctx.setProgress(100);

    const QString summary = QStringLiteral("自检完成：通过 %1 项，警告 %2 项，失败 %3 项")
                                .arg(passed).arg(warned).arg(critical);
    if (critical > 0)
        throw std::runtime_error(summary.toStdString());
    ctx.ok(summary);
}

} // namespace

void registerSystemTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("system.info");
        spec.category = QStringLiteral("batch");
        spec.name = QStringLiteral("系统信息采集");
        spec.desc = QStringLiteral("采集系统、屏幕、磁盘与权限信息，便于粘贴到反馈中");
        spec.icon = QStringLiteral("activity");
        spec.keywords = {QStringLiteral("系统"), QStringLiteral("信息"), QStringLiteral("环境")};
        spec.order = 10;
        addTool(spec, systemInfoTool);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("system.hash");
        spec.category = QStringLiteral("batch");
        spec.name = QStringLiteral("文件哈希校验");
        spec.desc = QStringLiteral("计算镜像 / 固件的 SHA256、SHA1、MD5 校验和");
        spec.icon = QStringLiteral("hash");
        spec.keywords = {QStringLiteral("hash"), QStringLiteral("校验"), QStringLiteral("sha256")};
        spec.fields = {
            fields::path(QStringLiteral("file"), QStringLiteral("文件路径")),
            fields::choice(QStringLiteral("algo"), QStringLiteral("算法"),
                           {QStringLiteral("SHA256"), QStringLiteral("SHA1"), QStringLiteral("MD5")}, 0),
        };
        spec.order = 20;
        addTool(spec, hashTool);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.payloadDumperGo");
        spec.category = QStringLiteral("image");
        spec.name = QStringLiteral("Payload 解包（开源工具）");
        spec.desc = QStringLiteral("调用 ssut/payload-dumper-go 解包 payload.bin，兼容差分包之外的完整包");
        spec.icon = QStringLiteral("file-archive");
        spec.keywords = {QStringLiteral("payload"), QStringLiteral("dumper"), QStringLiteral("开源")};
        spec.fields = {
            fields::path(QStringLiteral("payload"), QStringLiteral("payload.bin")),
            fields::dir(QStringLiteral("output"), QStringLiteral("输出目录")),
            fields::text(QStringLiteral("partitions"), QStringLiteral("仅解这些分区（逗号分隔，可空）"), QString(),
                         QStringLiteral("boot,vbmeta")),
        };
        spec.order = 30;
        addTool(spec, payloadDumperGoTool);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("system.selfTest");
        spec.category = QStringLiteral("batch");
        spec.group = QStringLiteral("system");
        spec.name = QStringLiteral("环境自检");
        spec.desc = QStringLiteral("检查目录、内置 adb/fastboot/zstd、引导包、驱动与 gzip 往返");
        spec.icon = QStringLiteral("stethoscope");
        spec.keywords = {QStringLiteral("自检"), QStringLiteral("诊断"), QStringLiteral("selftest")};
        spec.fields = {};
        addTool(spec, selfTestTool);
    }

}

} // namespace gf
