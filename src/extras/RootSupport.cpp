#include "RootSupport.h"

#include "image/BootImage.h"
#include "image/Compression.h"

#include <QRegularExpression>

#include "ExtrasUtils.h"

#include "device/AdbClient.h"

namespace gf::extras::root {

namespace {

QString findKernelBanner(const QByteArray& data)
{
    const int at = data.indexOf(QByteArrayLiteral("Linux version "));
    if (at < 0)
        return {};
    QByteArray raw = data.mid(at, 160);
    const int zero = raw.indexOf('\0');
    if (zero > 0)
        raw.truncate(zero);
    const int newline = raw.indexOf('\n');
    if (newline > 0)
        raw.truncate(newline);
    return QString::fromLatin1(raw).trimmed();
}

} // namespace

KernelImageInfo inspectKernelImage(const QByteArray& imageData)
{
    KernelImageInfo info;
    if (imageData.isEmpty()) {
        info.error = QStringLiteral("镜像为空");
        return info;
    }
    img::boot::Image image;
    try {
        image = img::boot::parse(imageData);
    } catch (const std::exception& e) {
        info.error = QString::fromUtf8(e.what());
        return info;
    }
    info.imageKind = image.kindText();
    info.headerVersion = int(image.headerVersion);
    info.kernelSize = image.kernel.size();
    info.ramdiskSize = image.ramdisk.size();
    if (!image.kernel.isEmpty()) {
        const img::compress::Format format = img::compress::detect(image.kernel);
        info.kernelCompression = format == img::compress::Format::Unknown
                                     ? QStringLiteral("raw")
                                     : img::compress::formatName(format);
        info.kernelVersion = findKernelBanner(image.kernel);
    } else {
        info.notes << QStringLiteral("该镜像不含 kernel（init_boot / vendor_boot 需要配合 boot 分区）");
    }
    if (info.kernelVersion.isEmpty())
        info.notes << QStringLiteral("内核中未找到 Linux version 版本串（可能是未压缩 Image 或已剥离）");
    if (image.headerVersion >= 3 && !image.vendorRamdisks.isEmpty())
        info.notes << QStringLiteral("vendor_boot 含 %1 个 vendor ramdisk").arg(image.vendorRamdisks.size());
    info.ok = true;
    return info;
}



QString engineId(RootEngine engine)
{
    switch (engine) {
    case RootEngine::Magisk: return QStringLiteral("magisk");
    case RootEngine::KernelSU: return QStringLiteral("kernelsu");
    case RootEngine::APatch: return QStringLiteral("apatch");
    case RootEngine::Other: return QStringLiteral("other");
    case RootEngine::None: break;
    }
    return QStringLiteral("none");
}

QString engineName(RootEngine engine)
{
    switch (engine) {
    case RootEngine::Magisk: return QStringLiteral("Magisk");
    case RootEngine::KernelSU: return QStringLiteral("KernelSU");
    case RootEngine::APatch: return QStringLiteral("APatch");
    case RootEngine::Other: return QStringLiteral("其他 Root 方案");
    case RootEngine::None: break;
    }
    return QStringLiteral("未 Root");
}

QString RootStatus::summary() const
{
    QStringList lines;
    lines.append(rooted ? QStringLiteral("Root 状态：已 Root（%1）").arg(engineName(engine))
                        : QStringLiteral("Root 状态：未 Root"));
    if (!version.isEmpty())
        lines.append(QStringLiteral("版本：%1").arg(version));
    if (!suPath.isEmpty())
        lines.append(QStringLiteral("su 路径：%1").arg(suPath));
    if (!suIdOutput.isEmpty())
        lines.append(QStringLiteral("su -c id：%1").arg(suIdOutput.split(QLatin1Char('\n')).first()));
    if (!evidence.isEmpty())
        lines.append(QStringLiteral("判定依据：%1").arg(evidence.join(QStringLiteral("；"))));
    if (!model.isEmpty())
        lines.append(QStringLiteral("设备型号：%1").arg(model));
    lines.append(QStringLiteral("构建类型：%1，可调试：%2")
                     .arg(buildType.isEmpty() ? QStringLiteral("?") : buildType,
                          debuggable ? QStringLiteral("是") : QStringLiteral("否")));
    lines.append(QStringLiteral("引导锁定：%1，verified boot：%2")
                     .arg(verifiedBootLocked ? QStringLiteral("已锁定") : QStringLiteral("未锁定"),
                          verifiedBootState.isEmpty() ? QStringLiteral("未知") : verifiedBootState));
    return lines.join(QLatin1Char('\n'));
}

QVariantMap RootStatus::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("rooted"), rooted);
    map.insert(QStringLiteral("engine"), engineId(engine));
    map.insert(QStringLiteral("engineName"), engineName(engine));
    map.insert(QStringLiteral("version"), version);
    map.insert(QStringLiteral("suPath"), suPath);
    map.insert(QStringLiteral("evidence"), evidence);
    map.insert(QStringLiteral("verifiedBootLocked"), verifiedBootLocked);
    map.insert(QStringLiteral("debuggable"), debuggable);
    map.insert(QStringLiteral("buildType"), buildType);
    map.insert(QStringLiteral("verifiedBootState"), verifiedBootState);
    map.insert(QStringLiteral("model"), model);
    return map;
}

RootStatus evaluate(const QMap<QString, QString>& props, const QStringList& foundPaths,
                    const QString& suPath, const QString& suIdOutput,
                    const QString& versionOutput)
{
    RootStatus status;
    status.props = props;
    status.suPath = suPath.trimmed();
    status.suIdOutput = suIdOutput.trimmed();
    status.model = props.value(QStringLiteral("ro.product.model"));
    status.buildType = props.value(QStringLiteral("ro.build.type"));
    status.debuggable = props.value(QStringLiteral("ro.debuggable")) == QLatin1String("1");
    status.verifiedBootLocked = props.value(QStringLiteral("ro.boot.flash.locked"))
                                == QLatin1String("1");
    status.verifiedBootState = props.value(QStringLiteral("ro.boot.verifiedbootstate"));

    const QString idLower = suIdOutput.toLower();
    if (idLower.contains(QStringLiteral("uid=0"))) {
        status.rooted = true;
        status.evidence.append(QStringLiteral("su -c id 返回 uid=0"));
    }

    const QString versionText = versionOutput.trimmed();
    if (!versionText.isEmpty()) {
        for (const QString& line : versionText.split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (status.version.isEmpty())
                status.version = trimmed;
        }
    }

    bool magisk = false;
    bool kernelSu = false;
    bool apatch = false;
    for (const QString& path : foundPaths) {
        const QString lower = path.toLower();
        if (lower.contains(QStringLiteral("magisk"))) {
            magisk = true;
            status.evidence.append(QStringLiteral("发现 %1").arg(path));
        } else if (lower.contains(QStringLiteral("/ksu")) || lower.contains(QStringLiteral("ksud"))
                   || lower.contains(QStringLiteral("kernelsu"))) {
            kernelSu = true;
            status.evidence.append(QStringLiteral("发现 %1").arg(path));
        } else if (lower.contains(QStringLiteral("apatch")) || lower.endsWith(QStringLiteral("/apd"))) {
            apatch = true;
            status.evidence.append(QStringLiteral("发现 %1").arg(path));
        }
    }
    const QString versionLower = versionText.toLower();
    if (versionLower.contains(QStringLiteral("magisk")))
        magisk = true;
    if (versionLower.contains(QStringLiteral("kernelsu")) || versionLower.contains(QStringLiteral("ksu")))
        kernelSu = true;
    if (versionLower.contains(QStringLiteral("apatch")))
        apatch = true;

    if (apatch) {
        status.engine = RootEngine::APatch;
        status.rooted = true;
    } else if (kernelSu) {
        status.engine = RootEngine::KernelSU;
        status.rooted = true;
    } else if (magisk) {
        status.engine = RootEngine::Magisk;
        status.rooted = true;
    } else if (status.rooted) {
        status.engine = RootEngine::Other;
    }
    return status;
}

RootStatus detect(const QString& serial, QString* error)
{
    RootStatus status;
    QString propsError;
    const QMap<QString, QString> props = gf::device::AdbClient::allProps(serial, &propsError);
    if (props.isEmpty()) {
        if (error)
            *error = propsError.isEmpty() ? QStringLiteral("读取设备属性失败。") : propsError;
        return status;
    }

    const QString suPath = findSu(serial);
    QString suIdOutput;
    if (!suPath.isEmpty()) {
        const QString command =
            QStringLiteral("%1 -c id 2>&1").arg(shellQuote(suPath));
        const QString output = adbShellRaw(serial, command, 25000);
        if (output.contains(QStringLiteral("uid=0")))
            suIdOutput = output.trimmed();
    }

    static const QStringList candidatePaths = {
        QStringLiteral("/system/bin/magisk"),  QStringLiteral("/system/xbin/magisk"),
        QStringLiteral("/sbin/magisk"),        QStringLiteral("/debug_ramdisk/magisk"),
        QStringLiteral("/data/adb/magisk/magisk"), QStringLiteral("/data/adb/ksu"),
        QStringLiteral("/data/adb/ksud"),      QStringLiteral("/data/adb/apatch"),
        QStringLiteral("/data/adb/apd"),       QStringLiteral("/data/adb/ksu/bin/ksud"),
    };
    QStringList quoted;
    for (const QString& candidate : candidatePaths)
        quoted.append(shellQuote(candidate));
    const QString probe = QStringLiteral("for p in %1; do [ -e \"$p\" ] && echo \"$p\"; done")
                              .arg(quoted.join(QLatin1Char(' ')));
    const QString probeOutput = adbShellRaw(serial, probe, 20000);
    QStringList foundPaths;
    for (const QString& line : probeOutput.split(QLatin1Char('\n'))) {
        const QString path = line.trimmed();
        if (!path.isEmpty() && !path.contains(QLatin1Char(' ')))
            foundPaths.append(path);
    }

    const QString versionOutput = adbShellRaw(
        serial,
        QStringLiteral("magisk -v 2>/dev/null; cat /data/adb/ksu/version 2>/dev/null; "
                       "cat /data/adb/apatch/version 2>/dev/null; "
                       "cat /data/adb/apd/version 2>/dev/null"),
        20000);

    status = evaluate(props, foundPaths, suPath, suIdOutput, versionOutput);
    if (error)
        error->clear();
    return status;
}

QVariantMap MagiskModule::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), id);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("version"), version);
    map.insert(QStringLiteral("description"), description);
    map.insert(QStringLiteral("disabled"), disabled);
    map.insert(QStringLiteral("removePending"), removePending);
    map.insert(QStringLiteral("updatePending"), updatePending);
    return map;
}

QStringList parseModuleIds(const QString& listingOutput)
{
    QStringList ids;
    for (const QString& line : listingOutput.split(QLatin1Char('\n'))) {
        QString id = line.trimmed();
        if (id.isEmpty() || id.startsWith(QLatin1Char('.')))
            continue;
        if (id.endsWith(QLatin1Char('/')) || id.endsWith(QLatin1Char(':')))
            id.chop(1);
        if (id.contains(QLatin1Char('/')) || id.contains(QLatin1Char(' ')))
            continue; // 只接受目录名
        if (!ids.contains(id))
            ids.append(id);
    }
    return ids;
}

MagiskModule parseModuleFlags(const QString& moduleId, const QString& flagListingOutput)
{
    MagiskModule module;
    module.id = moduleId;
    for (const QString& line : flagListingOutput.split(QLatin1Char('\n'))) {
        const QString name = line.trimmed();
        if (name == QLatin1String("disable"))
            module.disabled = true;
        else if (name == QLatin1String("remove"))
            module.removePending = true;
        else if (name == QLatin1String("update"))
            module.updatePending = true;
    }
    return module;
}

void applyModuleProp(MagiskModule& module, const QString& propText)
{
    const QMap<QString, QString> props = parseKeyValueText(propText);
    if (props.contains(QStringLiteral("name")))
        module.name = props.value(QStringLiteral("name"));
    if (props.contains(QStringLiteral("version")))
        module.version = props.value(QStringLiteral("version"));
    if (props.contains(QStringLiteral("versionCode")) && module.version.isEmpty())
        module.version = props.value(QStringLiteral("versionCode"));
    if (props.contains(QStringLiteral("description")))
        module.description = props.value(QStringLiteral("description"));
}

QString moduleDirectory(const QString& moduleId)
{
    return QStringLiteral("/data/adb/modules/") + moduleId;
}

QList<MagiskModule> readModules(const QString& serial, QString* error)
{
    QList<MagiskModule> modules;
    QString suError;
    const QString listing = adbSuShell(serial, QStringLiteral("ls -1 /data/adb/modules 2>/dev/null"),
                                       &suError, 30000);
    if (listing.isEmpty()) {
        if (!suError.isEmpty()) {
            if (error)
                *error = QStringLiteral("读取模块列表失败（需要 root）：%1").arg(suError);
            return {};
        }
        if (devicePathExists(serial, QStringLiteral("/data/adb/modules"))) {
            if (error)
                *error = QStringLiteral("模块目录为空。");
            return {};
        }
        if (error)
            *error = QStringLiteral("设备上不存在 /data/adb/modules，Magisk/KernelSU 可能尚未安装。");
        return {};
    }

    for (const QString& id : parseModuleIds(listing)) {
        const QString dir = moduleDirectory(id);
        const QString flagListing =
            adbSuShell(serial, QStringLiteral("ls -1 %1 2>/dev/null").arg(shellQuote(dir)),
                       nullptr, 20000);
        MagiskModule module = parseModuleFlags(id, flagListing);
        const QString propText =
            adbSuShell(serial, QStringLiteral("cat %1/module.prop 2>/dev/null").arg(shellQuote(dir)),
                       nullptr, 20000);
        applyModuleProp(module, propText);
        modules.append(module);
    }
    if (error)
        error->clear();
    return modules;
}

bool setModuleFlag(const QString& serial, const QString& moduleId, const QString& flag,
                   bool enabled, QString* error)
{
    const QString dir = moduleDirectory(moduleId);
    const QString command = enabled
                                ? QStringLiteral("touch %1").arg(shellQuote(dir + QLatin1Char('/') + flag))
                                : QStringLiteral("rm -f %1").arg(shellQuote(dir + QLatin1Char('/') + flag));
    QString suError;
    adbSuShell(serial, command, &suError, 20000);
    if (!suError.isEmpty()) {
        if (error)
            *error = suError;
        return false;
    }
    if (error)
        error->clear();
    return true;
}

} // namespace gf::extras::root
