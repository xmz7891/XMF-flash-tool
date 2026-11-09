#include "MiFlashFlow.h"

#include "ToolRegistry.h"
#include "Util.h"
#include "device/FastbootClient.h"
#include "usb/ProtocolSupport.h"
#include "usb/ToolOptions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>

#include <algorithm>
#include <stdexcept>

namespace gf {

namespace {

using namespace gf::fields;

// Partition flash order of the stock MiFlash scripts. Everything else keeps the
// archive order after these entries.
const QStringList& flashPriority()
{
    static const QStringList order = {
        QStringLiteral("gpt"),        QStringLiteral("xbl"),       QStringLiteral("xbl_ab"),
        QStringLiteral("xbl_config"), QStringLiteral("abl"),       QStringLiteral("aop"),
        QStringLiteral("aop_config"), QStringLiteral("tz"),        QStringLiteral("hyp"),
        QStringLiteral("keymaster"),  QStringLiteral("cmnlib"),    QStringLiteral("cmnlib64"),
        QStringLiteral("devcfg"),     QStringLiteral("featenabler"), QStringLiteral("qupfw"),
        QStringLiteral("uefisecapp"), QStringLiteral("imagefv"),   QStringLiteral("multiimgoem"),
        QStringLiteral("quec"),       QStringLiteral("boot"),      QStringLiteral("dtbo"),
        QStringLiteral("vbmeta"),     QStringLiteral("vbmeta_system"), QStringLiteral("vbmeta_vendor"),
        QStringLiteral("super"),      QStringLiteral("cust"),      QStringLiteral("modem"),
        QStringLiteral("dsp"),        QStringLiteral("logfs"),     QStringLiteral("storsec"),
    };
    return order;
}

QString tarExecutable()
{
    const QString systemTar = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(systemTar))
        return systemTar;
    return QStringLiteral("tar");
}

struct ExtractResult {
    bool ok = false;
    QString error;
    QString dir;
};

/// Windows 10/11 ships bsdtar, which streams multi-gigabyte packages without
/// loading them into memory. The temp dir is owned by the caller.
ExtractResult extractPackage(const QString& packagePath, const QString& targetDir,
                             const std::function<void(const QString&)>& log)
{
    ExtractResult result;
    result.dir = targetDir;
    QProcess tar;
    tar.setProgram(tarExecutable());
    tar.setArguments({QStringLiteral("-xf"), QDir::toNativeSeparators(packagePath),
                      QStringLiteral("-C"), QDir::toNativeSeparators(targetDir)});
    tar.setProcessChannelMode(QProcess::MergedChannels);
    tar.start();
    if (!tar.waitForStarted(5000)) {
        result.error = QStringLiteral("无法启动系统 tar.exe，请确认 Windows 10/11 系统组件完整");
        return result;
    }
    while (!tar.waitForFinished(200)) {
        if (tar.state() == QProcess::NotRunning)
            break;
        // keep reading so the pipe never fills up
        const QByteArray chunk = tar.readAllStandardOutput();
        if (!chunk.isEmpty() && log)
            log(QString::fromLocal8Bit(chunk).trimmed());
    }
    const QByteArray tail = tar.readAllStandardOutput();
    if (!tail.isEmpty() && log)
        log(QString::fromLocal8Bit(tail).trimmed());
    if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        result.error = QStringLiteral("线刷包解压失败（tar 退出码 %1）").arg(tar.exitCode());
        return result;
    }
    result.ok = true;
    return result;
}

struct ImageEntry {
    QString partition;
    QString path;
    qint64 size = 0;
};

QString partitionNameFor(const QString& fileName)
{
    QString base = QFileInfo(fileName).completeBaseName();
    // Stock packages use e.g. "boot.img", "vbmeta_system.img"; other files such
    // as "flash_all.bat" are ignored by the caller.
    return base;
}

QList<ImageEntry> collectImages(const QString& dir, QString* error)
{
    QList<ImageEntry> images;
    QDir root(dir);
    const QFileInfoList files =
        root.entryInfoList({QStringLiteral("*.img")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& file : files) {
        ImageEntry entry;
        entry.partition = partitionNameFor(file.fileName());
        entry.path = file.absoluteFilePath();
        entry.size = file.size();
        images.append(entry);
    }
    // Some packages keep the images in an "images" subdirectory.
    if (images.isEmpty()) {
        const QDir imagesDir(root.filePath(QStringLiteral("images")));
        if (imagesDir.exists()) {
            const QFileInfoList nested = imagesDir.entryInfoList(
                {QStringLiteral("*.img")}, QDir::Files | QDir::Readable, QDir::Name);
            for (const QFileInfo& file : nested) {
                ImageEntry entry;
                entry.partition = partitionNameFor(file.fileName());
                entry.path = file.absoluteFilePath();
                entry.size = file.size();
                images.append(entry);
            }
        }
    }
    if (images.isEmpty() && error)
        *error = QStringLiteral("线刷包内没有找到 .img 镜像");
    return images;
}

void sortByFlashOrder(QList<ImageEntry>* images)
{
    const QStringList& order = flashPriority();
    std::stable_sort(images->begin(), images->end(),
                     [&order](const ImageEntry& a, const ImageEntry& b) {
                         const int ai = order.indexOf(a.partition);
                         const int bi = order.indexOf(b.partition);
                         const int ra = ai < 0 ? order.size() : ai;
                         const int rb = bi < 0 ? order.size() : bi;
                         return ra < rb;
                     });
}

QString fastbootSerial(const QVariantMap& options, QString* error)
{
    QString serial = proto::optionString(options, QStringLiteral("serial"));
    if (!serial.isEmpty())
        return serial;
    serial = device::FastbootClient::resolveSerial(QString(), error);
    if (serial.isEmpty() && error)
        *error = QStringLiteral("没有检测到 Fastboot 设备，请先把手机切到 Fastboot / Bootloader 模式");
    return serial;
}

QVariantMap inspectPackage(const QVariantMap& options)
{
    const QString packagePath = proto::optionString(options, QStringLiteral("package"));
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    QVariantList entries;
    result.insert(QStringLiteral("entries"), entries);

    if (packagePath.isEmpty() || !QFileInfo::exists(packagePath)) {
        result.insert(QStringLiteral("error"), QStringLiteral("线刷包不存在：%1").arg(packagePath));
        return result;
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        result.insert(QStringLiteral("error"), QStringLiteral("无法创建临时解压目录"));
        return result;
    }
    const ExtractResult extract = extractPackage(packagePath, dir.path(), nullptr);
    if (!extract.ok) {
        result.insert(QStringLiteral("error"), extract.error);
        return result;
    }
    QString error;
    const QList<ImageEntry> images = collectImages(dir.path(), &error);
    if (images.isEmpty()) {
        result.insert(QStringLiteral("error"), error);
        return result;
    }
    QVariantList list;
    qint64 total = 0;
    for (const ImageEntry& image : images) {
        QVariantMap map;
        map.insert(QStringLiteral("partition"), image.partition);
        map.insert(QStringLiteral("path"), image.path);
        map.insert(QStringLiteral("size"), image.size);
        map.insert(QStringLiteral("sizeText"), util::formatBytes(image.size));
        list.append(map);
        total += image.size;
    }
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("entries"), list);
    result.insert(QStringLiteral("totalSize"), total);
    result.insert(QStringLiteral("summary"),
                  QStringLiteral("%1 个镜像 · %2").arg(list.size()).arg(util::formatBytes(total)));
    return result;
}

void runFlash(TaskContext& ctx, const QVariantMap& options)
{
    const QString packagePath = proto::optionString(options, QStringLiteral("package"));
    const QString mode = proto::optionString(options, QStringLiteral("mode"),
                                             QStringLiteral("clean"));
    const bool reboot = options.value(QStringLiteral("reboot"), true).toBool();

    if (packagePath.isEmpty() || !QFileInfo::exists(packagePath))
        throw std::runtime_error(QStringLiteral("线刷包不存在：%1").arg(packagePath).toStdString());

    QString serialError;
    const QString serial = fastbootSerial(options, &serialError);
    if (serial.isEmpty())
        throw std::runtime_error(serialError.toStdString());

    // Flashing with a flat battery can brick a device halfway through.
    const QString batteryIssue = device::FastbootClient::batteryIssue(serial);
    if (!batteryIssue.isEmpty()) {
        ctx.error(batteryIssue);
        throw std::runtime_error(
            (batteryIssue + QStringLiteral("；请先充电到安全电量后再刷写")).toStdString());
    }

    ctx.log(1, QStringLiteral("目标设备：%1").arg(serial));
    ctx.log(1, QStringLiteral("刷机模式：%1").arg(
        mode == QLatin1String("keep") ? QStringLiteral("保留用户数据")
        : mode == QLatin1String("clean-lock") ? QStringLiteral("全部删除并锁 BL")
                                              : QStringLiteral("全部删除")));

    QTemporaryDir dir;
    if (!dir.isValid())
        throw std::runtime_error("无法创建临时解压目录");

    ctx.setStatus(QStringLiteral("解压线刷包…"));
    const ExtractResult extract = extractPackage(packagePath, dir.path(), [&ctx](const QString& line) {
        if (!line.isEmpty())
            ctx.log(0, line);
    });
    if (!extract.ok)
        throw std::runtime_error(extract.error.toStdString());
    ctx.checkCanceled();

    QString error;
    QList<ImageEntry> images = collectImages(dir.path(), &error);
    if (images.isEmpty())
        throw std::runtime_error(error.toStdString());
    sortByFlashOrder(&images);

    // Optional subset (single partition actions from the workbench): comma
    // separated partition names, case insensitive.
    const QString partitionsOption = proto::optionString(options, QStringLiteral("partitions"));
    if (!partitionsOption.trimmed().isEmpty()) {
        QStringList wanted = partitionsOption.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString& name : wanted)
            name = name.trimmed();
        QList<ImageEntry> filtered;
        for (const ImageEntry& image : images) {
            for (const QString& name : wanted) {
                if (name.compare(image.partition, Qt::CaseInsensitive) == 0) {
                    filtered.append(image);
                    break;
                }
            }
        }
        if (filtered.isEmpty())
            throw std::runtime_error(
                QStringLiteral("线刷包里没有这些分区：%1").arg(partitionsOption).toStdString());
        images = filtered;
    }
    ctx.log(1, QStringLiteral("已解析 %1 个镜像，开始刷写").arg(images.size()));

    int flashed = 0;
    for (const ImageEntry& image : images) {
        ctx.checkCanceled();
        const int percent = int(100.0 * flashed / images.size());
        ctx.setProgress(percent);
        ctx.setStatus(QStringLiteral("刷写 %1（%2/%3）").arg(image.partition).arg(flashed + 1).arg(images.size()));
        const device::FastbootResult result =
            device::FastbootClient::flash(serial, image.partition, image.path, 900000);
        if (!result.ok) {
            const QString detail = result.error.isEmpty() ? result.output : result.error;
            ctx.error(QStringLiteral("刷写 %1 失败：%2").arg(image.partition, detail.trimmed()));
            throw std::runtime_error(
                QStringLiteral("刷写 %1 失败：%2").arg(image.partition, detail.trimmed()).toStdString());
        }
        ++flashed;
        ctx.ok(QStringLiteral("%1 已刷写").arg(image.partition));
    }
    ctx.setProgress(94);

    if (mode == QLatin1String("clean") || mode == QLatin1String("clean-lock")) {
        ctx.setStatus(QStringLiteral("清除用户数据…"));
        for (const QString& partition : {QStringLiteral("userdata"), QStringLiteral("metadata"),
                                         QStringLiteral("cache")}) {
            const device::FastbootResult erased =
                device::FastbootClient::erase(serial, partition);
            if (erased.ok)
                ctx.log(1, QStringLiteral("%1 已清除").arg(partition));
            else
                ctx.warn(QStringLiteral("%1 清除跳过：%2").arg(
                    partition, (erased.error.isEmpty() ? erased.output : erased.error).trimmed()));
        }
    }

    if (mode == QLatin1String("clean-lock")) {
        ctx.setStatus(QStringLiteral("重新锁定 Bootloader…"));
        const device::FastbootResult lock = device::FastbootClient::flashingLock(serial, true);
        if (lock.ok)
            ctx.ok(QStringLiteral("Bootloader 已重新锁定"));
        else
            ctx.warn(QStringLiteral("锁定失败（部分机型需在手机上确认）：%1")
                         .arg((lock.error.isEmpty() ? lock.output : lock.error).trimmed()));
    }

    if (reboot) {
        ctx.setStatus(QStringLiteral("重启设备…"));
        const device::FastbootResult rebootResult = device::FastbootClient::reboot(serial, QString());
        if (!rebootResult.ok)
            ctx.warn(QStringLiteral("自动重启失败，请手动重启：%1")
                         .arg((rebootResult.error.isEmpty() ? rebootResult.output
                                                            : rebootResult.error).trimmed()));
    }

    ctx.setProgress(100);
    ctx.setStatus(QStringLiteral("刷写完成"));
    ctx.ok(QStringLiteral("完成：共刷写 %1 个镜像").arg(flashed));
}

} // namespace

void registerMiFlashFlowTools()
{
    auto& registry = ToolRegistry::instance();

    {
        ToolSpec spec;
        spec.id = QStringLiteral("miflash.inspect");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("miflash");
        spec.name = QStringLiteral("解析小米线刷包");
        spec.desc = QStringLiteral("列出 tgz 线刷包内的分区镜像与大小（不刷写）");
        spec.icon = QStringLiteral("package-search");
        spec.keywords = {QStringLiteral("miflash"), QStringLiteral("tgz"), QStringLiteral("小米")};
        spec.fields = {path(QStringLiteral("package"), QStringLiteral("线刷包"), QStringLiteral("*.tgz *.tar.gz"))};
        registry.add(spec, [](TaskContext& ctx, const QVariantMap& options) {
            ctx.setStatus(QStringLiteral("解析线刷包…"));
            const QVariantMap result = inspectPackage(options);
            if (!result.value(QStringLiteral("ok")).toBool())
                throw std::runtime_error(result.value(QStringLiteral("error")).toString().toStdString());
            for (const QVariant& entry : result.value(QStringLiteral("entries")).toList()) {
                const QVariantMap map = entry.toMap();
                ctx.log(1, QStringLiteral("%1 · %2").arg(map.value(QStringLiteral("partition")).toString(),
                                                         map.value(QStringLiteral("sizeText")).toString()));
            }
            ctx.setStatus(QStringLiteral("解析完成"));
        });
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("miflash.flash");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("miflash");
        spec.name = QStringLiteral("MiFlash 线刷");
        spec.desc = QStringLiteral("按 MiFlash 模式刷写 tgz 线刷包（全部删除 / 保留数据 / 删除并锁 BL）");
        spec.icon = QStringLiteral("zap");
        spec.keywords = {QStringLiteral("miflash"), QStringLiteral("fastboot"), QStringLiteral("线刷")};
        spec.fields = {
            path(QStringLiteral("package"), QStringLiteral("线刷包"), QStringLiteral("*.tgz *.tar.gz")),
            choice(QStringLiteral("mode"), QStringLiteral("刷机模式"),
                   {QStringLiteral("全部删除"), QStringLiteral("保留用户数据"),
                    QStringLiteral("全部删除并锁 BL")},
                   0),
            text(QStringLiteral("serial"), QStringLiteral("序列号（留空自动检测）"), {}, {}, {}, false),
            fields::boolean(QStringLiteral("reboot"), QStringLiteral("刷完自动重启"), true),
        };
        registry.add(spec, [](TaskContext& ctx, const QVariantMap& options) {
            // The UI passes the Chinese display text; normalize to the internal id.
            QVariantMap normalized = options;
            const QString modeText = proto::optionString(options, QStringLiteral("mode"));
            QString mode = QStringLiteral("clean");
            if (modeText.contains(QStringLiteral("保留")) || modeText == QLatin1String("keep"))
                mode = QStringLiteral("keep");
            else if (modeText.contains(QStringLiteral("锁")) || modeText == QLatin1String("clean-lock"))
                mode = QStringLiteral("clean-lock");
            normalized.insert(QStringLiteral("mode"), mode);
            runFlash(ctx, normalized);
        });
    }
}

} // namespace gf
