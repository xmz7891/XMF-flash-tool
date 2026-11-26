// Advanced user-facing workflows built on the existing protocol backends:
// OPPO/OnePlus/realme packages, multi-brand ROM downloads, scrcpy, OCDT and
// TWRP partition operations.
#include "ProTools.h"

#include "../app/ToolModules.h"
#include "core/TaskManager.h"
#include "core/ToolRegistry.h"
#include "device/AdbClient.h"
#include "device/FastbootClient.h"
#include "firmware/FirmwareCommon.h"
#include "firmware/FirmwareNet.h"
#include "Util.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QTemporaryDir>
#include <QThread>
#include <QXmlStreamReader>

#include <stdexcept>

namespace gf {
namespace {

QString str(const QVariantMap& o, const QString& k, const QString& def = {})
{
    const QString v = o.value(k).toString().trimmed();
    return v.isEmpty() ? def : v;
}
bool flag(const QVariantMap& o, const QString& k, bool def = false)
{
    return o.contains(k) ? o.value(k).toBool() : def;
}
int num(const QVariantMap& o, const QString& k, int def = 0)
{
    bool ok = false;
    const int v = o.value(k).toInt(&ok);
    return ok ? v : def;
}
QString downloadDir()
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (d.isEmpty()) d = QDir::homePath() + "/Downloads";
    return d + QStringLiteral("/XMF");
}
QStringList archiveEntries(const QString& path)
{
    QProcess p;
    p.start(QStringLiteral("tar"), {QStringLiteral("-tf"), path});
    if (!p.waitForFinished(15000)) { p.kill(); return {}; }
    if (p.exitCode() != 0) return {};
    QStringList out;
    for (const QString& line : QString::fromLocal8Bit(p.readAllStandardOutput()).split(QLatin1Char('\n'))) {
        const QString s = line.trimmed();
        if (!s.isEmpty()) out.append(s);
    }
    return out;
}
QString brandFromText(const QString& text)
{
    const QString t = text.toLower();
    if (t.contains("oneplus") || t.contains("一加")) return QStringLiteral("OnePlus");
    if (t.contains("realme") || t.contains("真我")) return QStringLiteral("realme");
    if (t.contains("oppo") || t.contains("欧加")) return QStringLiteral("OPPO");
    if (t.contains("xiaomi") || t.contains("redmi") || t.contains("小米")) return QStringLiteral("Xiaomi");
    return {};
}
QString modelFromText(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("([A-Z]{2,4}?[0-9]{3,5}|[A-Z]{2,4}-[A-Z0-9]{3,})"));
    const auto m = re.match(text.toUpper());
    return m.hasMatch() ? m.captured(1) : QString();
}
bool safePartition(const QString& p)
{
    static const QStringList blocked = {
        QStringLiteral("system"), QStringLiteral("system_ext"), QStringLiteral("product"),
        QStringLiteral("vendor"), QStringLiteral("super"), QStringLiteral("userdata"),
        QStringLiteral("metadata"), QStringLiteral("persist"), QStringLiteral("modem"),
        QStringLiteral("modemst1"), QStringLiteral("modemst2"), QStringLiteral("fsg"),
        QStringLiteral("fsc"), QStringLiteral("xbl"), QStringLiteral("abl")
    };
    return !blocked.contains(p);
}
QString partitionFromFile(const QString& file)
{
    QString name = QFileInfo(file).completeBaseName();
    name.replace(QRegularExpression(QStringLiteral("[-_.](a|b|slot[ab])$")), QString());
    return name;
}
QString findScrcpy()
{
    const QStringList candidates = {
        QDir(util::dataDir()).filePath(QStringLiteral("thirdparty/oss/scrcpy/scrcpy.exe")),
        QDir(util::dataDir()).filePath(QStringLiteral("thirdparty/scrcpy/scrcpy.exe")),
        QStringLiteral("scrcpy.exe")
    };
    for (const QString& c : candidates) {
        if (c == QStringLiteral("scrcpy.exe") || QFileInfo::exists(c))
            return c;
    }
    // Release bundles nest the portable scrcpy build (scrcpy-win64-vX.Y.Z).
    const QString oss = QDir(util::dataDir()).filePath(QStringLiteral("thirdparty/oss/scrcpy"));
    QDirIterator it(oss, {QStringLiteral("scrcpy.exe")}, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) return it.next();
    return {};
}

void inspectPackage(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = str(options, QStringLiteral("path"));
    if (path.isEmpty()) throw std::runtime_error("请选择 OPPO / 一加 / realme 固件包或解包目录");
    QFileInfo info(path);
    if (!info.exists()) throw std::runtime_error("路径不存在：" + path.toStdString());
    QStringList entries;
    if (info.isDir()) {
        QDirIterator it(path, {QStringLiteral("*.img"), QStringLiteral("*.bin"),
                               QStringLiteral("*.mbn"), QStringLiteral("*.xml"),
                               QStringLiteral("*.txt"), QStringLiteral("*.prop")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) entries.append(QDir(path).relativeFilePath(it.next()));
    } else {
        entries = archiveEntries(path);
    }
    int images = 0;
    QStringList important;
    for (const QString& e : entries) {
        const QString lower = e.toLower();
        if (lower.endsWith(".img") || lower.endsWith(".bin")) ++images;
        if (lower.endsWith(".img") && (lower.contains("boot") || lower.contains("vbmeta")
            || lower.contains("super") || lower.contains("dtbo") || lower.contains("xbl")))
            important.append(e);
    }
    const QString all = path + QLatin1Char('\n') + entries.join(QLatin1Char('\n'));
    const QString brand = brandFromText(all);
    ctx.info(QStringLiteral("包类型：%1").arg(info.isDir() ? "已解包目录" : info.suffix()));
    ctx.info(QStringLiteral("品牌：%1").arg(brand.isEmpty() ? "未识别" : brand));
    ctx.info(QStringLiteral("机型候选：%1").arg(modelFromText(info.fileName()).isEmpty()
        ? "未识别" : modelFromText(info.fileName())));
    ctx.info(QStringLiteral("镜像数量：%1").arg(images));
    if (!important.isEmpty())
        ctx.info(QStringLiteral("关键镜像：%1").arg(important.mid(0, 10).join(QStringLiteral("、"))));
    ctx.ok(QStringLiteral("包检查完成；下一步可生成刷写计划"));
}

void planPackage(TaskContext& ctx, const QVariantMap& options)
{
    const QString dir = str(options, QStringLiteral("dir"));
    if (dir.isEmpty()) throw std::runtime_error("请选择已解包的固件目录");
    if (!QDir(dir).exists()) throw std::runtime_error("目录不存在：" + dir.toStdString());
    QStringList images;
    QDirIterator it(dir, {QStringLiteral("*.img")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) images.append(it.next());
    if (images.isEmpty()) throw std::runtime_error("目录中没有 .img 文件；请先解包 OFP/OZIP/OTA");
    const QStringList preferred = {QStringLiteral("boot"), QStringLiteral("init_boot"),
                                   QStringLiteral("vendor_boot"), QStringLiteral("dtbo"),
                                   QStringLiteral("vbmeta"), QStringLiteral("vbmeta_system"),
                                   QStringLiteral("super"), QStringLiteral("modem"),
                                   QStringLiteral("system"), QStringLiteral("product")};
    QStringList plan;
    for (const QString& p : preferred)
        for (const QString& f : images)
            if (partitionFromFile(f) == p)
                plan.append(QStringLiteral("%1 <- %2").arg(p, QFileInfo(f).fileName()));
    for (const QString& f : images) {
        const QString p = partitionFromFile(f);
        if (!preferred.contains(p) && !p.isEmpty())
            plan.append(QStringLiteral("%1 <- %2").arg(p, QFileInfo(f).fileName()));
    }
    ctx.info(QStringLiteral("刷写计划 %1 项：").arg(plan.size()));
    for (const QString& line : plan) ctx.info(QStringLiteral("  %1").arg(line));
    const QString out = str(options, QStringLiteral("output"));
    if (!out.isEmpty()) {
        util::ensureDir(QFileInfo(out).absolutePath());
        util::writeFile(out, ("# XMF OPLUS flash plan\n" + plan.join('\n') + '\n').toUtf8());
        ctx.ok(QStringLiteral("计划已写入 %1").arg(out));
    } else {
        ctx.ok("计划生成完成");
    }
}

void runOplusFlash(TaskContext& ctx, const QVariantMap& options)
{
    if (!flag(options, QStringLiteral("confirm")))
        throw std::runtime_error("危险操作：请勾选“我确认执行刷写”后再运行");
    const QString dir = str(options, QStringLiteral("dir"));
    if (dir.isEmpty()) throw std::runtime_error("请选择已解包的固件目录");
    const QString mode = str(options, QStringLiteral("mode"), QStringLiteral("boot"));
    QString serial = str(options, QStringLiteral("serial"));
    if (serial.isEmpty()) {
        const auto devices = device::FastbootClient::devices();
        if (devices.isEmpty()) throw std::runtime_error("没有 Fastboot 设备；请先进入 Bootloader/FastbootD");
        serial = devices.first().serial;
    }
    QDirIterator it(dir, {QStringLiteral("*.img")}, QDir::Files, QDirIterator::Subdirectories);
    QList<QPair<QString, QString>> tasks;
    while (it.hasNext()) {
        const QString file = it.next();
        const QString part = partitionFromFile(file);
        if (part.isEmpty()) continue;
        if (mode == QStringLiteral("boot") && !safePartition(part)) continue;
        if (mode == QStringLiteral("slim") && !QStringList{QStringLiteral("boot"), QStringLiteral("init_boot"),
                QStringLiteral("vendor_boot"), QStringLiteral("dtbo"), QStringLiteral("vbmeta")}.contains(part))
            continue;
        if (mode == QStringLiteral("after") && QStringList{QStringLiteral("super"), QStringLiteral("system"),
                QStringLiteral("userdata")}.contains(part)) continue;
        tasks.append({part, file});
    }
    if (tasks.isEmpty()) throw std::runtime_error("没有符合当前模式的镜像");
    ctx.info(QStringLiteral("模式：%1 · 设备：%2 · 镜像 %3 个").arg(mode, serial).arg(tasks.size()));
    int done = 0;
    for (const auto& t : tasks) {
        ctx.setStatus(QStringLiteral("Fastboot 刷写 %1 …").arg(t.first));
        const auto result = device::FastbootClient::runFor(
            serial, {QStringLiteral("flash"), t.first, t.second}, 900000);
        if (!result.ok) {
            throw std::runtime_error(QStringLiteral("刷写 %1 失败：%2")
                .arg(t.first, result.error.isEmpty() ? result.output.left(200) : result.error)
                .toStdString());
        }
        ctx.ok(QStringLiteral("已刷写 %1").arg(t.first));
        ctx.setProgress(int(100.0 * (++done) / tasks.size()));
    }
    ctx.ok("欧加刷写流程完成");
}

void otaInspect(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = str(options, QStringLiteral("path"));
    if (path.isEmpty()) throw std::runtime_error("请选择 OTA / 线刷包");
    QFileInfo info(path);
    if (!info.exists()) throw std::runtime_error("文件不存在：" + path.toStdString());
    const QStringList entries = archiveEntries(path);
    static const QRegularExpression ver(QStringLiteral("([0-9]+\\.[0-9]+(?:\\.[0-9]+){0,3})"));
    const auto m = ver.match(info.fileName());
    ctx.info(QStringLiteral("文件：%1（%2 MB）").arg(info.fileName())
        .arg(info.size() / 1024.0 / 1024.0, 0, 'f', 1));
    ctx.info(QStringLiteral("版本候选：%1").arg(m.hasMatch() ? m.captured(1) : "未识别"));
    ctx.info(QStringLiteral("包内条目：%1").arg(entries.size()));
    if (!entries.isEmpty())
        ctx.info(QStringLiteral("前几项：%1").arg(entries.mid(0, 8).join(QStringLiteral("、"))));
    ctx.ok("OTA 包信息读取完成；降级前请核对 ARB 与目标版本");
}

void romDownload(TaskContext& ctx, const QVariantMap& options)
{
    const QString url = str(options, QStringLiteral("url"));
    if (url.isEmpty()) throw std::runtime_error("请填写 ROM / OTA 下载地址");
    const QString dir = str(options, QStringLiteral("outputDir"), downloadDir());
    util::ensureDir(dir);
    QString fileName = str(options, QStringLiteral("fileName"));
    if (fileName.isEmpty()) fileName = QUrl(url).fileName();
    if (fileName.isEmpty()) fileName = QStringLiteral("rom_download.bin");
    const QString out = QDir(dir).filePath(fileName);
    ctx.info(QStringLiteral("下载到：%1").arg(out));
    const auto result = firmware::downloadFile(url, out, flag(options, QStringLiteral("resume"), true),
        [&](qint64 received, qint64 total) {
            ctx.setProgress(total > 0 ? int(received * 100 / total) : 0);
            ctx.setStatus(QStringLiteral("下载 %1 / %2")
                .arg(firmware::formatBytes(received), firmware::formatBytes(total)));
        }, {}, 120000);
    if (!result.ok) throw std::runtime_error(QStringLiteral("下载失败：%1").arg(result.error).toStdString());
    ctx.ok(QStringLiteral("下载完成：%1（%2）").arg(out, firmware::formatBytes(result.bytes)));
}

void launchScrcpy(TaskContext& ctx, const QVariantMap& options)
{
    const QString exe = findScrcpy();
    if (exe.isEmpty()) throw std::runtime_error("未找到 scrcpy.exe；完整包中应包含 data/thirdparty/oss/scrcpy");
    QStringList args;
    const QString size = str(options, QStringLiteral("maxSize"), QStringLiteral("1600"));
    if (size != QStringLiteral("native")) args << QStringLiteral("--max-size=%1").arg(size);
    const int fps = num(options, QStringLiteral("fps"), 60);
    if (fps > 0) args << QStringLiteral("--max-fps=%1").arg(fps);
    const int bitrate = num(options, QStringLiteral("bitrate"), 8);
    if (bitrate > 0) args << QStringLiteral("--video-bit-rate=%1M").arg(bitrate);
    const QString title = str(options, QStringLiteral("title"));
    if (!title.isEmpty()) args << QStringLiteral("--window-title=%1").arg(title);
    if (flag(options, QStringLiteral("virtualKeys"), true)) args << QStringLiteral("--keyboard=uhid");
    if (flag(options, QStringLiteral("stayAwake"), true)) args << QStringLiteral("--stay-awake");
    if (!flag(options, QStringLiteral("audio"), false)) args << QStringLiteral("--no-audio");
    const QString record = str(options, QStringLiteral("record"));
    if (!record.isEmpty()) args << QStringLiteral("--record=%1").arg(record);
    if (flag(options, QStringLiteral("alwaysOnTop"), false)) args << QStringLiteral("--always-on-top");
    ctx.info(QStringLiteral("启动：%1 %2").arg(exe, args.join(QLatin1Char(' '))));
    QProcess p;
    p.setProgram(exe);
    p.setArguments(args);
    if (!p.startDetached()) throw std::runtime_error("scrcpy 启动失败");
    ctx.ok("scrcpy 已启动；窗口参数已应用");
}

void mediaBackup(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = str(options, QStringLiteral("serial"));
    const QString dir = str(options, QStringLiteral("outputDir"));
    if (serial.isEmpty() || dir.isEmpty()) throw std::runtime_error("请填写设备序列号与输出目录");
    util::ensureDir(dir);
    const QStringList paths = {QStringLiteral("/sdcard/DCIM"), QStringLiteral("/sdcard/Pictures"),
                               QStringLiteral("/sdcard/Movies"), QStringLiteral("/sdcard/Download")};
    int ok = 0;
    for (const QString& remote : paths) {
        const QString name = QFileInfo(remote).fileName();
        const auto r = device::AdbClient::pull(serial, remote, QDir(dir).filePath(name), 1800000);
        if (r.ok) { ++ok; ctx.ok(QStringLiteral("已拉取 %1").arg(remote)); }
        else ctx.warn(QStringLiteral("跳过 %1：%2").arg(remote, r.error));
    }
    if (ok == 0) throw std::runtime_error("没有可拉取的媒体目录；请检查 ADB 权限");
    ctx.ok(QStringLiteral("媒体备份完成：%1 个目录").arg(ok));
}

void contactsBackup(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = str(options, QStringLiteral("serial"));
    const QString out = str(options, QStringLiteral("output"));
    if (serial.isEmpty() || out.isEmpty()) throw std::runtime_error("请填写设备序列号与输出文件");
    const auto r = device::AdbClient::shell(serial,
        QStringLiteral("content query --uri content://contacts/phones"), 60000);
    if (!r.ok) throw std::runtime_error(("读取联系人失败：" + r.error).toStdString());
    util::ensureDir(QFileInfo(out).absolutePath());
    if (!util::writeFile(out, r.output.toUtf8())) throw std::runtime_error("写入联系人失败");
    ctx.ok(QStringLiteral("联系人已导出：%1").arg(out));
}

void gkiUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = str(options, QStringLiteral("input"));
    const QString output = str(options, QStringLiteral("output"));
    if (input.isEmpty() || output.isEmpty()) throw std::runtime_error("请选择 boot 镜像与输出目录");
    auto handler = ToolRegistry::instance().handler(QStringLiteral("image.boot.unpack"));
    if (!handler) throw std::runtime_error("boot 解包后端不可用");
    QVariantMap opts;
    opts.insert(QStringLiteral("input"), input);
    opts.insert(QStringLiteral("output"), output);
    opts.insert(QStringLiteral("decompress"), true);
    opts.insert(QStringLiteral("extractCpio"), false);
    handler(ctx, opts);
    const QString kernel = QDir(output).filePath(QStringLiteral("kernel"));
    if (!QFileInfo::exists(kernel)) throw std::runtime_error("boot 中没有 kernel，无法生成 GKI Image");
    const QString image = QDir(output).filePath(QStringLiteral("Image"));
    if (QFileInfo(kernel).absoluteFilePath() != QFileInfo(image).absoluteFilePath())
        QFile::copy(kernel, image);
    QFile info(QDir(output).filePath(QStringLiteral("gki.txt")));
    if (info.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        info.write("Generic Kernel Image (GKI) extracted by XMF\n");
        info.write("kernel source: " + kernel.toUtf8() + "\n");
        info.write("Image: " + image.toUtf8() + "\n");
    }
    ctx.ok(QStringLiteral("GKI 解包完成：%1 / Image").arg(output));
}

void gkiPack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = str(options, QStringLiteral("input"));
    const QString kernel = str(options, QStringLiteral("kernel"));
    const QString output = str(options, QStringLiteral("output"));
    if (input.isEmpty() || kernel.isEmpty() || output.isEmpty())
        throw std::runtime_error("请选择原始 boot、GKI Image 与输出 boot");
    auto handler = ToolRegistry::instance().handler(QStringLiteral("image.boot.repack"));
    if (!handler) throw std::runtime_error("boot 重打包后端不可用");
    QVariantMap opts;
    opts.insert(QStringLiteral("input"), input);
    opts.insert(QStringLiteral("output"), output);
    opts.insert(QStringLiteral("kernel"), kernel);
    const QString ramdisk = str(options, QStringLiteral("ramdisk"));
    if (!ramdisk.isEmpty()) opts.insert(QStringLiteral("ramdisk"), ramdisk);
    handler(ctx, opts);
    ctx.ok(QStringLiteral("GKI boot 重打包完成：%1").arg(output));
}

void avbSign(TaskContext& ctx, const QVariantMap& options)
{
    const QString image = str(options, QStringLiteral("image"));
    const QString key = str(options, QStringLiteral("key"));
    const QString partition = str(options, QStringLiteral("partition"));
    if (image.isEmpty() || key.isEmpty() || partition.isEmpty())
        throw std::runtime_error("请填写镜像、私钥与分区名");
    QString avbtool = str(options, QStringLiteral("avbtool"));
    if (avbtool.isEmpty()) avbtool = QStandardPaths::findExecutable(QStringLiteral("avbtool"));
    if (avbtool.isEmpty()) avbtool = QStandardPaths::findExecutable(QStringLiteral("avbtool.py"));
    if (avbtool.isEmpty())
        throw std::runtime_error("未找到 avbtool。请安装 AOSP avbtool 并填写其路径（AVB 签名必须使用官方工具）");
    QStringList args = {QStringLiteral("add_hash_footer"), QStringLiteral("--image"), image,
                        QStringLiteral("--partition_name"), partition,
                        QStringLiteral("--key"), key};
    const QString algorithm = str(options, QStringLiteral("algorithm"), QStringLiteral("SHA256_RSA4096"));
    args << QStringLiteral("--algorithm") << algorithm;
    const int size = num(options, QStringLiteral("partitionSize"), 0);
    if (size > 0) args << QStringLiteral("--partition_size") << QString::number(size);
    ctx.info(QStringLiteral("执行：%1 %2").arg(avbtool, args.join(QLatin1Char(' '))));
    QProcess p;
    p.start(avbtool, args);
    if (!p.waitForStarted(5000)) throw std::runtime_error("avbtool 启动失败");
    while (!p.waitForFinished(1000)) {
        if (ctx.isCanceled()) { p.kill(); throw std::runtime_error("已取消"); }
    }
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput() + p.readAllStandardError());
    if (p.exitCode() != 0) throw std::runtime_error(("AVB 签名失败：" + out).toStdString());
    ctx.info(out.trimmed());
    ctx.ok(QStringLiteral("AVB 签名完成：%1").arg(image));
}

QString curlPath()
{
    QString c = QStandardPaths::findExecutable(QStringLiteral("curl"));
    if (c.isEmpty()) c = QStandardPaths::findExecutable(QStringLiteral("curl.exe"));
    return c;
}

bool curlSingle(const QString& curl, const QString& url, const QString& out)
{
    QProcess p;
    p.start(curl, {QStringLiteral("-L"), QStringLiteral("--fail"), QStringLiteral("--silent"),
                   QStringLiteral("--show-error"), QStringLiteral("-o"), out, url});
    if (!p.waitForStarted(10000)) return false;
    if (!p.waitForFinished(1800000)) { p.kill(); return false; }
    return p.exitCode() == 0 && QFileInfo::exists(out);
}

void parallelDownload(TaskContext& ctx, const QVariantMap& options)
{
    const QString url = str(options, QStringLiteral("url"));
    if (url.isEmpty()) throw std::runtime_error("请填写下载地址");
    const QString dir = str(options, QStringLiteral("outputDir"), downloadDir());
    util::ensureDir(dir);
    QString name = str(options, QStringLiteral("fileName"));
    if (name.isEmpty()) name = QUrl(url).fileName();
    if (name.isEmpty()) name = QStringLiteral("rom_download.bin");
    util::ensureDir(dir);
    const QString out = QDir(dir).filePath(name);
    const int threads = qBound(1, num(options, QStringLiteral("threads"), 4), 16);
    const QString curl = curlPath();
    if (curl.isEmpty() || threads <= 1) {
        // fallback to the single-stream resume implementation
        if (!curlSingle(curl, url, out))
            throw std::runtime_error("单线程下载失败（curl 返回错误）");
        ctx.ok(QStringLiteral("下载完成：%1").arg(out));
        return;
    }
    // HEAD to discover size and range support
    QProcess head;
    head.start(curl, {QStringLiteral("-L"), QStringLiteral("-sI"), url});
    head.waitForFinished(30000);
    const QString headers = QString::fromLocal8Bit(head.readAllStandardOutput());
    qint64 total = -1;
    bool ranges = false;
    for (const QString& line : headers.split(QLatin1Char('\n'))) {
        const QString l = line.toLower();
        if (l.startsWith(QStringLiteral("content-length:")))
            total = line.mid(line.indexOf(QLatin1Char(':')) + 1).trimmed().toLongLong();
        if (l.contains(QStringLiteral("accept-ranges")) && l.contains(QStringLiteral("bytes")))
            ranges = true;
    }
    if (total <= 0 || !ranges) {
        ctx.warn("服务器不支持 Range，退回单线程断点续传");
        if (!curlSingle(curl, url, out))
            throw std::runtime_error("单线程下载失败（curl 返回错误）");
        ctx.ok(QStringLiteral("下载完成：%1").arg(out));
        return;
    }
    QTemporaryDir tmp;
    if (!tmp.isValid()) throw std::runtime_error("无法创建临时目录");
    const qint64 chunk = (total + threads - 1) / threads;
    QList<QProcess*> procs;
    QStringList parts;
    ctx.info(QStringLiteral("多线程下载：%1 线程 · %2").arg(threads).arg(firmware::formatBytes(total)));
    for (int i = 0; i < threads; ++i) {
        const qint64 start = i * chunk;
        const qint64 end = qMin(total - 1, (i + 1) * chunk - 1);
        if (start > end) break;
        const QString part = tmp.filePath(QStringLiteral("part_%1").arg(i));
        parts << part;
        auto* p = new QProcess();
        p->start(curl, {QStringLiteral("-L"), QStringLiteral("--fail"), QStringLiteral("--silent"),
                        QStringLiteral("--show-error"), QStringLiteral("--max-time"), QStringLiteral("300"), QStringLiteral("--range"),
                        QStringLiteral("%1-%2").arg(start).arg(end), QStringLiteral("-o"), part, url});
        procs.append(p);
    }
    for (auto* p : procs) {
        if (!p->waitForStarted(5000)) {
            ctx.warn(QStringLiteral("curl 启动失败：%1（程序 %2）").arg(p->errorString(), curl));
            qDeleteAll(procs);
            throw std::runtime_error("curl 分块进程启动失败");
        }
    }
    int done = 0;
    while (done < procs.size()) {
        if (ctx.isCanceled()) {
            for (auto* p : procs) p->kill();
            qDeleteAll(procs);
            throw std::runtime_error("已取消");
        }
        qint64 got = 0;
        done = 0;
        for (int i = 0; i < procs.size(); ++i) {
            if (procs[i]->state() != QProcess::NotRunning)
                procs[i]->waitForFinished(0);
            if (procs[i]->state() == QProcess::NotRunning) ++done;
            got += QFileInfo(parts[i]).size();
        }
        ctx.setProgress(int(got * 100 / total));
        ctx.setStatus(QStringLiteral("多线程下载 %1 / %2 · %3 线程")
            .arg(firmware::formatBytes(got), firmware::formatBytes(total)).arg(threads));
        QThread::msleep(200);
    }
    for (auto* p : procs) {
        if (p->state() != QProcess::NotRunning) p->waitForFinished(1000);
        if (p->exitCode() != 0) {
            const QString err = QString::fromLocal8Bit(p->readAllStandardError());
            qDeleteAll(procs);
            throw std::runtime_error(("分块下载失败：" + err).toStdString());
        }
    }
    QFile merged(out);
    if (!merged.open(QIODevice::WriteOnly | QIODevice::Truncate))
        throw std::runtime_error("无法写入输出文件");
    for (const QString& part : parts) {
        QFile f(part);
        if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("分块文件缺失");
        while (!f.atEnd()) merged.write(f.read(4 * 1024 * 1024));
    }
    merged.close();
    qDeleteAll(procs);
    if (QFileInfo(out).size() != total)
        throw std::runtime_error("合并后大小与 Content-Length 不一致");
    ctx.ok(QStringLiteral("多线程下载完成：%1（%2）").arg(out, firmware::formatBytes(total)));
}

void oemUnlock(TaskContext& ctx, const QVariantMap& options)
{
    if (!flag(options, QStringLiteral("confirm")))
        throw std::runtime_error("危险操作：请勾选确认后再执行 OEM 解锁");
    const QString strategy = str(options, QStringLiteral("strategy"), QStringLiteral("auto"));
    const QString serial = str(options, QStringLiteral("serial"));
    QStringList commands;
    if (strategy == QLatin1String("xiaomi") || strategy == QLatin1String("auto"))
        commands << QStringLiteral("flashing unlock");
    if (strategy == QLatin1String("oplus") || strategy == QLatin1String("auto"))
        commands << QStringLiteral("flashing unlock");
    if (strategy == QLatin1String("generic") || strategy == QLatin1String("auto"))
        commands << QStringLiteral("oem unlock");
    QString lastError;
    for (const QString& command : commands) {
        const auto r = device::FastbootClient::runFor(serial, command.split(QLatin1Char(' ')), 60000);
        if (r.ok) {
            ctx.ok(QStringLiteral("OEM 解锁命令成功：%1").arg(command));
            return;
        }
        lastError = r.error.isEmpty() ? r.output : r.error;
        ctx.warn(QStringLiteral("策略 %1 未成功：%2").arg(command, lastError.left(160)));
    }
    throw std::runtime_error(("OEM 解锁失败；设备可能要求厂商解锁码 / 账号绑定：" + lastError).toStdString());
}

void inspectOcdt(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = str(options, QStringLiteral("path"));
    if (path.isEmpty()) throw std::runtime_error("请选择 OCDT 文件或目录");
    QFileInfo info(path);
    if (!info.exists()) throw std::runtime_error("路径不存在：" + path.toStdString());
    QStringList files;
    if (info.isDir()) {
        QDirIterator it(path, {QStringLiteral("*.xml"), QStringLiteral("*.json"), QStringLiteral("*.txt"),
                               QStringLiteral("*.ocdt")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files.append(it.next());
    } else {
        files.append(path);
    }
    if (files.isEmpty()) throw std::runtime_error("没有找到 OCDT 文本/XML/JSON 文件");
    int shown = 0;
    for (const QString& f : files) {
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray data = file.read(256 * 1024);
        ctx.info(QStringLiteral("%1（%2 字节）").arg(QFileInfo(f).fileName()).arg(QFileInfo(f).size()));
        const QString text = QString::fromUtf8(data);
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            if (line.trimmed().isEmpty()) continue;
            ctx.info(QStringLiteral("  %1").arg(line.trimmed()));
            if (++shown >= 40) break;
        }
    }
    ctx.ok(QStringLiteral("OCDT 读取完成：%1 个文件").arg(files.size()));
}

void generateOcdt(TaskContext& ctx, const QVariantMap& options)
{
    const QString dir = str(options, QStringLiteral("dir"));
    const QString out = str(options, QStringLiteral("output"));
    if (dir.isEmpty() || out.isEmpty()) throw std::runtime_error("请选择镜像目录与输出 XML");
    QDirIterator it(dir, {QStringLiteral("*.img")}, QDir::Files, QDirIterator::Subdirectories);
    QStringList images;
    while (it.hasNext()) images.append(it.next());
    if (images.isEmpty()) throw std::runtime_error("目录中没有 .img");
    QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<ocdt>\n  <device brand=\"OPLUS\"/>\n");
    for (const QString& f : images) {
        xml += QStringLiteral("  <partition name=\"%1\" file=\"%2\" size=\"%3\"/>\n")
                   .arg(partitionFromFile(f), QFileInfo(f).fileName()).arg(QFileInfo(f).size());
    }
    xml += QStringLiteral("</ocdt>\n");
    util::ensureDir(QFileInfo(out).absolutePath());
    if (!util::writeFile(out, xml.toUtf8())) throw std::runtime_error("写入失败：" + out.toStdString());
    ctx.ok(QStringLiteral("OCDT 模板已生成：%1（%2 个分区）").arg(out).arg(images.size()));
}

void twrpPartitions(TaskContext& ctx, const QVariantMap& options)
{
    QString serial = str(options, QStringLiteral("serial"));
    const auto devices = device::AdbClient::devices();
    if (serial.isEmpty()) {
        for (const auto& d : devices)
            if (d.state == QStringLiteral("recovery") || d.state == QStringLiteral("sideload"))
                serial = d.serial;
        if (serial.isEmpty() && !devices.isEmpty()) serial = devices.first().serial;
    }
    if (serial.isEmpty()) throw std::runtime_error("没有 ADB 设备；请进入 TWRP Recovery");
    const auto result = device::AdbClient::shell(serial, QStringLiteral("ls -l /dev/block/by-name"), 30000);
    if (!result.ok) throw std::runtime_error("读取分区失败：" + result.error.toStdString());
    ctx.info(QStringLiteral("设备 %1 的分区：").arg(serial));
    int count = 0;
    for (const QString& line : result.output.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) continue;
        ctx.info(QStringLiteral("  %1").arg(line.trimmed()));
        ++count;
    }
    ctx.ok(QStringLiteral("TWRP 分区读取完成：%1 行").arg(count));
}

void twrpBackup(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = str(options, QStringLiteral("serial"));
    const QString part = str(options, QStringLiteral("partition"));
    const QString out = str(options, QStringLiteral("output"));
    if (serial.isEmpty() || part.isEmpty() || out.isEmpty())
        throw std::runtime_error("请填写设备、分区与输出文件");
    const QString remote = QStringLiteral("/sdcard/xmf_twrp_%1.img").arg(part);
    const auto dd = device::AdbClient::shell(serial,
        QStringLiteral("dd if=/dev/block/by-name/%1 of=%2 bs=4M").arg(part, remote), 900000);
    if (!dd.ok) throw std::runtime_error(("TWRP dd 失败：" + dd.error).toStdString());
    util::ensureDir(QFileInfo(out).absolutePath());
    const auto pull = device::AdbClient::pull(serial, remote, out, 900000);
    device::AdbClient::shell(serial, QStringLiteral("rm -f %1").arg(remote), 30000);
    if (!pull.ok) throw std::runtime_error(("拉取备份失败：" + pull.error).toStdString());
    ctx.ok(QStringLiteral("TWRP 分区备份完成：%1（%2 字节）").arg(out).arg(QFileInfo(out).size()));
}

void twrpRestore(TaskContext& ctx, const QVariantMap& options)
{
    if (!flag(options, QStringLiteral("confirm")))
        throw std::runtime_error("危险操作：请勾选确认后再恢复分区");
    const QString serial = str(options, QStringLiteral("serial"));
    const QString part = str(options, QStringLiteral("partition"));
    const QString image = str(options, QStringLiteral("image"));
    if (serial.isEmpty() || part.isEmpty() || image.isEmpty())
        throw std::runtime_error("请填写设备、分区与镜像");
    const QString remote = QStringLiteral("/sdcard/xmf_twrp_restore.img");
    const auto push = device::AdbClient::push(serial, image, remote, 900000);
    if (!push.ok) throw std::runtime_error(("推送镜像失败：" + push.error).toStdString());
    const auto dd = device::AdbClient::shell(serial,
        QStringLiteral("dd if=%1 of=/dev/block/by-name/%2 bs=4M").arg(remote, part), 900000);
    device::AdbClient::shell(serial, QStringLiteral("rm -f %1").arg(remote), 30000);
    if (!dd.ok) throw std::runtime_error(("TWRP 写入失败：" + dd.error).toStdString());
    ctx.ok(QStringLiteral("TWRP 分区恢复完成：%1").arg(part));
}

void twrpErase(TaskContext& ctx, const QVariantMap& options)
{
    if (!flag(options, QStringLiteral("confirm")))
        throw std::runtime_error("危险操作：请勾选确认后再擦除分区");
    const QString serial = str(options, QStringLiteral("serial"));
    const QString part = str(options, QStringLiteral("partition"));
    const int mb = num(options, QStringLiteral("mb"), 0);
    if (serial.isEmpty() || part.isEmpty() || mb <= 0)
        throw std::runtime_error("请填写设备、分区与擦除大小（MB）");
    if (!safePartition(part)) throw std::runtime_error("该分区在保护列表内，拒绝擦除");
    const auto dd = device::AdbClient::shell(serial,
        QStringLiteral("dd if=/dev/zero of=/dev/block/by-name/%1 bs=1M count=%2").arg(part).arg(mb), 900000);
    if (!dd.ok) throw std::runtime_error(("TWRP 擦除失败：" + dd.error).toStdString());
    ctx.ok(QStringLiteral("TWRP 分区已擦除：%1（%2 MB）").arg(part).arg(mb));
}

} // namespace

void registerProTools()
{
    {
        ToolSpec s;
        s.id = QStringLiteral("oplus.package.inspect");
        s.category = QStringLiteral("firmware");
        s.group = QStringLiteral("oplus");
        s.name = QStringLiteral("欧加固件包检查");
        s.desc = QStringLiteral("识别 OPPO / 一加 / realme 全量包、售后包、OFP/OZIP/OTA 与镜像");
        s.icon = QStringLiteral("package-search");
        s.keywords = {QStringLiteral("oppo"), QStringLiteral("一加"), QStringLiteral("realme"), QStringLiteral("全量包")};
        s.fields = {fields::path(QStringLiteral("path"), QStringLiteral("固件包 / 解包目录"),
                                 QStringLiteral("固件 (*.zip *.ofp *.ozip *.img);;所有文件 (*)"))};
        s.order = 10;
        addTool(s, inspectPackage);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("oplus.flash.plan");
        s.category = QStringLiteral("firmware");
        s.group = QStringLiteral("oplus");
        s.name = QStringLiteral("欧加刷写计划");
        s.desc = QStringLiteral("扫描解包目录，按分区与安全顺序生成 Fastboot 刷写计划");
        s.icon = QStringLiteral("list-ordered");
        s.fields = {fields::dir(QStringLiteral("dir"), QStringLiteral("解包目录")),
                    fields::path(QStringLiteral("output"), QStringLiteral("保存计划"),
                                 QStringLiteral("文本 (*.txt)"), QString(), false)};
        s.order = 11;
        addTool(s, planPackage);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("oplus.flash.run");
        s.category = QStringLiteral("firmware");
        s.group = QStringLiteral("oplus");
        s.name = QStringLiteral("欧加 Fastboot 刷写");
        s.desc = QStringLiteral("按计划刷写 boot / super / 全量；需要 Fastboot 设备与二次确认");
        s.icon = QStringLiteral("flame");
        s.badge = QStringLiteral("危险");
        s.keywords = {QStringLiteral("刷写"), QStringLiteral("全量包"), QStringLiteral("售后包"), QStringLiteral("救砖")};
        s.fields = {fields::dir(QStringLiteral("dir"), QStringLiteral("解包目录")),
                    fields::choice(QStringLiteral("mode"), QStringLiteral("模式"),
                                   {QStringLiteral("boot"), QStringLiteral("slim"), QStringLiteral("after"),
                                    QStringLiteral("full")}, 0),
                    fields::text(QStringLiteral("serial"), QStringLiteral("设备序列号（空=自动）"),
                                 QString(), QString(), QString(), false),
                    fields::boolean(QStringLiteral("confirm"), QStringLiteral("我确认执行刷写"), false)};
        s.order = 12;
        addTool(s, runOplusFlash);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("oplus.ota.inspect");
        s.category = QStringLiteral("firmware");
        s.group = QStringLiteral("oplus");
        s.name = QStringLiteral("欧加 OTA / 降级检查");
        s.desc = QStringLiteral("读取 OTA/线刷包版本线索，检查降级与 ARB 前置信息");
        s.icon = QStringLiteral("history");
        s.fields = {fields::path(QStringLiteral("path"), QStringLiteral("OTA / 线刷包"),
                                 QStringLiteral("固件 (*.zip *.ofp *.ozip);;所有文件 (*)"))};
        s.order = 13;
        addTool(s, otaInspect);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("firmware.rom.download");
        s.category = QStringLiteral("firmware");
        s.group = QStringLiteral("rom");
        s.name = QStringLiteral("ROM 资源下载");
        s.desc = QStringLiteral("从官方/镜像 URL 下载 OTA、线刷包或 boot，支持断点续传");
        s.icon = QStringLiteral("download-cloud");
        s.keywords = {QStringLiteral("ROM"), QStringLiteral("OTA"), QStringLiteral("下载"), QStringLiteral("续传")};
        s.fields = {fields::text(QStringLiteral("url"), QStringLiteral("下载地址")),
                    fields::dir(QStringLiteral("outputDir"), QStringLiteral("保存目录"), QStringLiteral("留空=下载目录/XMF"), false),
                    fields::text(QStringLiteral("fileName"), QStringLiteral("文件名"), QString(), QString(), QString(), false),
                    fields::boolean(QStringLiteral("resume"), QStringLiteral("断点续传"), true)};
        s.order = 14;
        addTool(s, romDownload);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("scrcpy.launch");
        s.category = QStringLiteral("adb");
        s.group = QStringLiteral("scrcpy");
        s.name = QStringLiteral("scrcpy 投屏参数");
        s.desc = QStringLiteral("分辨率、帧率、码率、窗口标题、虚拟按键与录制开关");
        s.icon = QStringLiteral("monitor-smartphone");
        s.keywords = {QStringLiteral("投屏"), QStringLiteral("scrcpy"), QStringLiteral("虚拟按键")};
        s.fields = {fields::choice(QStringLiteral("maxSize"), QStringLiteral("分辨率"),
                                   {QStringLiteral("native"), QStringLiteral("1920"), QStringLiteral("1600"),
                                    QStringLiteral("1280"), QStringLiteral("1024")}, 2),
                    fields::integer(QStringLiteral("fps"), QStringLiteral("帧率"), 60, 0, 240),
                    fields::integer(QStringLiteral("bitrate"), QStringLiteral("码率 (Mbps)"), 8, 1, 100),
                    fields::text(QStringLiteral("title"), QStringLiteral("窗口标题"), QStringLiteral("XMF 投屏"),
                                 QString(), QString(), false),
                    fields::boolean(QStringLiteral("virtualKeys"), QStringLiteral("虚拟按键"), true),
                    fields::boolean(QStringLiteral("stayAwake"), QStringLiteral("保持唤醒"), true),
                    fields::boolean(QStringLiteral("audio"), QStringLiteral("转发音频"), false),
                    fields::text(QStringLiteral("record"), QStringLiteral("录制文件（可选）"), QString(), QString(), QString(), false)};
        s.order = 15;
        addTool(s, launchScrcpy);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("oplus.ocdt.inspect");
        s.category = QStringLiteral("firmware");
        s.group = QStringLiteral("ocdt");
        s.name = QStringLiteral("OCDT 分析");
        s.desc = QStringLiteral("读取 OPLUS OCDT XML/JSON/目录，输出设备与分区字段");
        s.icon = QStringLiteral("file-search");
        s.fields = {fields::path(QStringLiteral("path"), QStringLiteral("OCDT 文件 / 目录"),
                                 QStringLiteral("OCDT (*.xml *.json *.txt *.ocdt);;所有文件 (*)"))};
        s.order = 16;
        addTool(s, inspectOcdt);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("oplus.ocdt.generate");
        s.category = QStringLiteral("firmware");
        s.group = QStringLiteral("ocdt");
        s.name = QStringLiteral("OCDT 模板生成");
        s.desc = QStringLiteral("从解包镜像目录生成 OCDT XML 模板（实验性，需人工核对）");
        s.icon = QStringLiteral("file-plus");
        s.maturity = 1;
        s.fields = {fields::dir(QStringLiteral("dir"), QStringLiteral("镜像目录")),
                    fields::path(QStringLiteral("output"), QStringLiteral("输出 XML"),
                                 QStringLiteral("XML (*.xml)"))};
        s.order = 17;
        addTool(s, generateOcdt);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("twrp.partitions");
        s.category = QStringLiteral("adb");
        s.group = QStringLiteral("twrp");
        s.name = QStringLiteral("TWRP 分区读取");
        s.desc = QStringLiteral("在 TWRP Recovery / Sideload 下读取 by-name 分区表");
        s.icon = QStringLiteral("table");
        s.fields = {fields::text(QStringLiteral("serial"), QStringLiteral("设备序列号（空=自动）"),
                                 QString(), QString(), QString(), false)};
        s.order = 18;
        addTool(s, twrpPartitions);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("twrp.backup");
        s.category = QStringLiteral("adb");
        s.group = QStringLiteral("twrp");
        s.name = QStringLiteral("TWRP 分区备份");
        s.desc = QStringLiteral("dd 备份 by-name 分区并拉回电脑");
        s.icon = QStringLiteral("save");
        s.fields = {fields::text(QStringLiteral("serial"), QStringLiteral("设备序列号")),
                    fields::text(QStringLiteral("partition"), QStringLiteral("分区名")),
                    fields::path(QStringLiteral("output"), QStringLiteral("保存文件"),
                                 QStringLiteral("镜像 (*.img)"))};
        s.order = 19;
        addTool(s, twrpBackup);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("twrp.restore");
        s.category = QStringLiteral("adb");
        s.group = QStringLiteral("twrp");
        s.name = QStringLiteral("TWRP 分区恢复");
        s.desc = QStringLiteral("dd 写回 by-name 分区；危险，需要确认");
        s.icon = QStringLiteral("upload");
        s.badge = QStringLiteral("危险");
        s.fields = {fields::text(QStringLiteral("serial"), QStringLiteral("设备序列号")),
                    fields::text(QStringLiteral("partition"), QStringLiteral("分区名")),
                    fields::path(QStringLiteral("image"), QStringLiteral("镜像"), QStringLiteral("镜像 (*.img)")),
                    fields::boolean(QStringLiteral("confirm"), QStringLiteral("我确认写入"), false)};
        s.order = 20;
        addTool(s, twrpRestore);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("twrp.erase");
        s.category = QStringLiteral("adb");
        s.group = QStringLiteral("twrp");
        s.name = QStringLiteral("TWRP 分区擦除");
        s.desc = QStringLiteral("dd 清零指定分区；保护列表拒绝执行");
        s.icon = QStringLiteral("eraser");
        s.badge = QStringLiteral("危险");
        s.fields = {fields::text(QStringLiteral("serial"), QStringLiteral("设备序列号")),
                    fields::text(QStringLiteral("partition"), QStringLiteral("分区名")),
                    fields::integer(QStringLiteral("mb"), QStringLiteral("擦除大小 MB"), 0, 0, 65536),
                    fields::boolean(QStringLiteral("confirm"), QStringLiteral("我确认擦除"), false)};
        s.order = 21;
        addTool(s, twrpErase);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("adb.media.backup");
        s.category = QStringLiteral("adb");
        s.group = QStringLiteral("backup");
        s.name = QStringLiteral("相册 / 视频备份");
        s.desc = QStringLiteral("从 DCIM / Pictures / Movies / Download 拉取到电脑");
        s.icon = QStringLiteral("images");
        s.fields = {fields::text(QStringLiteral("serial"), QStringLiteral("设备序列号")),
                    fields::dir(QStringLiteral("outputDir"), QStringLiteral("输出目录"))};
        s.order = 22;
        addTool(s, mediaBackup);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("adb.contacts.backup");
        s.category = QStringLiteral("adb");
        s.group = QStringLiteral("backup");
        s.name = QStringLiteral("通讯录导出");
        s.desc = QStringLiteral("通过 content provider 导出联系人文本；需要授权");
        s.icon = QStringLiteral("contact");
        s.fields = {fields::text(QStringLiteral("serial"), QStringLiteral("设备序列号")),
                    fields::path(QStringLiteral("output"), QStringLiteral("输出文件"),
                                 QStringLiteral("文本 (*.txt)"))};
        s.order = 23;
        addTool(s, contactsBackup);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("image.gki.unpack");
        s.category = QStringLiteral("image");
        s.group = QStringLiteral("gki");
        s.name = QStringLiteral("GKI 制作 · 解包");
        s.desc = QStringLiteral("从 boot 镜像提取 kernel 并生成 GKI Image / 元数据");
        s.icon = QStringLiteral("cpu");
        s.fields = {fields::path(QStringLiteral("input"), QStringLiteral("boot 镜像"), QStringLiteral("镜像 (*.img)")),
                    fields::dir(QStringLiteral("output"), QStringLiteral("输出目录"))};
        s.order = 24;
        addTool(s, gkiUnpack);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("image.gki.pack");
        s.category = QStringLiteral("image");
        s.group = QStringLiteral("gki");
        s.name = QStringLiteral("GKI 制作 · 重打包");
        s.desc = QStringLiteral("把 GKI Image 换入原 boot，保留 ramdisk / cmdline");
        s.icon = QStringLiteral("package");
        s.fields = {fields::path(QStringLiteral("input"), QStringLiteral("原始 boot 镜像"), QStringLiteral("镜像 (*.img)")),
                    fields::path(QStringLiteral("kernel"), QStringLiteral("GKI Image"), QStringLiteral("所有文件 (*)")),
                    fields::path(QStringLiteral("output"), QStringLiteral("输出 boot 镜像"), QStringLiteral("镜像 (*.img)")),
                    fields::path(QStringLiteral("ramdisk"), QStringLiteral("替换 ramdisk（可选）"),
                                 QStringLiteral("所有文件 (*)"), QString(), false)};
        s.order = 25;
        addTool(s, gkiPack);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("image.avb.sign");
        s.category = QStringLiteral("image");
        s.group = QStringLiteral("avb");
        s.name = QStringLiteral("AVB 签名");
        s.desc = QStringLiteral("调用 AOSP avbtool 给分区添加 hash footer 并签名");
        s.icon = QStringLiteral("shield-check");
        s.maturity = 1;
        s.fields = {fields::path(QStringLiteral("image"), QStringLiteral("镜像"), QStringLiteral("镜像 (*.img)")),
                    fields::path(QStringLiteral("key"), QStringLiteral("私钥"), QStringLiteral("密钥 (*.pem *.key)")),
                    fields::text(QStringLiteral("partition"), QStringLiteral("分区名")),
                    fields::choice(QStringLiteral("algorithm"), QStringLiteral("算法"),
                                   {QStringLiteral("SHA256_RSA2048"), QStringLiteral("SHA256_RSA4096"),
                                    QStringLiteral("SHA256_RSA8192")}, 1),
                    fields::text(QStringLiteral("avbtool"), QStringLiteral("avbtool 路径"), QString(), QString(), QString(), false),
                    fields::integer(QStringLiteral("partitionSize"), QStringLiteral("分区大小（可选）"), 0, 0, 2147483647)};
        s.order = 26;
        addTool(s, avbSign);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("firmware.rom.multidownload");
        s.category = QStringLiteral("firmware");
        s.group = QStringLiteral("rom");
        s.name = QStringLiteral("ROM 多线程下载");
        s.desc = QStringLiteral("HTTP Range 分块并发下载，支持断点回退与进度");
        s.icon = QStringLiteral("download-cloud");
        s.fields = {fields::text(QStringLiteral("url"), QStringLiteral("下载地址")),
                    fields::dir(QStringLiteral("outputDir"), QStringLiteral("保存目录"), QStringLiteral("留空=下载目录/XMF"), false),
                    fields::text(QStringLiteral("fileName"), QStringLiteral("文件名"), QString(), QString(), QString(), false),
                    fields::integer(QStringLiteral("threads"), QStringLiteral("线程数"), 4, 1, 16)};
        s.order = 27;
        addTool(s, parallelDownload);
    }
    {
        ToolSpec s;
        s.id = QStringLiteral("oem.unlock.strategy");
        s.category = QStringLiteral("unlock");
        s.group = QStringLiteral("oem");
        s.name = QStringLiteral("强开 OEM · 厂商策略");
        s.desc = QStringLiteral("按小米 / 欧加 / 通用顺序执行 fastboot 解锁命令");
        s.icon = QStringLiteral("unlock");
        s.badge = QStringLiteral("危险");
        s.fields = {fields::choice(QStringLiteral("strategy"), QStringLiteral("策略"),
                                   {QStringLiteral("auto"), QStringLiteral("xiaomi"),
                                    QStringLiteral("oplus"), QStringLiteral("generic")}, 0),
                    fields::text(QStringLiteral("serial"), QStringLiteral("设备序列号（空=自动）"),
                                 QString(), QString(), QString(), false),
                    fields::boolean(QStringLiteral("confirm"), QStringLiteral("我确认解锁 Bootloader"), false)};
        s.order = 28;
        addTool(s, oemUnlock);
    }
}
} // namespace gf
