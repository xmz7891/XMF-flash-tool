#include "RootImage.h"
#include "RootSupport.h"

#include "image/BootImage.h"

#include <QDir>

#include "ExtrasUtils.h"

#include "BootImage.h"
#include "ImageCommon.h"
#include "Cpio.h"
#include "Compression.h"

#include "core/TaskManager.h"
#include "core/ToolRegistry.h"
#include "core/Util.h"
#include "device/AdbClient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>

#include <stdexcept>

namespace gf {

namespace {

using namespace gf::extras;
using namespace gf::extras::root;

constexpr const char* kRootCategory = "root";

void rootStatus(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    ctx.setStatus(QStringLiteral("检测 Root 状态"));
    QString error;
    const RootStatus status = detect(serial, &error);
    if (!error.isEmpty())
        fail(error);
    for (const QString& line : status.summary().split(QLatin1Char('\n')))
        ctx.info(line);
    if (!status.rooted)
        ctx.warn(QStringLiteral("设备未 Root。可先用「Boot 镜像修补」生成修补镜像，再通过 Fastboot 刷入 boot/init_boot。"));
    ctx.ok(status.rooted ? QStringLiteral("已 Root（%1）").arg(engineName(status.engine))
                         : QStringLiteral("未检测到 Root。"));
}

QString effectiveEngineLabel(const QString& engine)
{
    if (engine == QLatin1String("magisk"))
        return QStringLiteral("magiskinit");
    if (engine == QLatin1String("ksu"))
        return QStringLiteral("KernelSU 注入文件");
    return QStringLiteral("ramdisk 注入文件");
}

void rootBootPatch(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = textOption(options, "input");
    const QString payloadPath = textOption(options, "payload");
    if (input.isEmpty() || !QFileInfo::exists(input))
        fail(QStringLiteral("请选择存在的 boot / init_boot 镜像。"));
    if (payloadPath.isEmpty() || !QFileInfo::exists(payloadPath))
        fail(QStringLiteral("请选择要注入的 magiskinit / KernelSU 文件。"));

    const QString engine = choiceOption(options, "engine", QStringLiteral("magisk"));
    const QByteArray bootData = gf::img::readFileOrThrow(input);
    const QByteArray payload = gf::img::readFileOrThrow(payloadPath);
    if (payload.isEmpty())
        fail(QStringLiteral("注入文件为空：%1").arg(payloadPath));

    ctx.setProgress(10);
    ctx.setStatus(QStringLiteral("解析 boot 镜像"));
    ctx.checkCanceled();
    ctx.info(QStringLiteral("输入镜像：%1（%2）").arg(input, util::formatBytes(bootData.size())));
    ctx.info(QStringLiteral("注入文件：%1（%2，%3）")
                 .arg(payloadPath, util::formatBytes(payload.size()), effectiveEngineLabel(engine)));

    PatchOptions patch;
    patch.payload = payload;
    patch.payloadLabel = effectiveEngineLabel(engine);
    patch.backupInit = boolOption(options, "backupInit", true);
    if (engine == QLatin1String("magisk")) {
        const QString sha1 = QString::fromLatin1(
            QCryptographicHash::hash(bootData, QCryptographicHash::Sha1).toHex());
        patch.magiskConfig = QStringLiteral("KEEPVERITY=%1\nKEEPFORCEENCRYPT=%2\nSHA1=%3\n")
                                 .arg(boolOption(options, "keepVerity", true) ? QStringLiteral("true")
                                                                              : QStringLiteral("false"),
                                      boolOption(options, "keepForceEncrypt", false)
                                          ? QStringLiteral("true")
                                          : QStringLiteral("false"),
                                      sha1)
                                 .toUtf8();
    }

    ctx.setProgress(30);
    ctx.setStatus(QStringLiteral("解包 ramdisk 并注入"));
    PatchOutcome outcome;
    const QByteArray patched = patchBootImage(bootData, patch, &outcome);
    ctx.checkCanceled();

    // 二次解析确认输出是合法的 boot 镜像。
    ctx.setStatus(QStringLiteral("校验修补结果"));
    gf::img::boot::Image verify = gf::img::boot::parse(patched);
    if (verify.ramdisk.isEmpty())
        fail(QStringLiteral("修补结果异常：输出镜像没有 ramdisk。"));

    const QString dir = ensureOutputDir(textOption(options, "out"), QStringLiteral("root_patch"));
    QString name = textOption(options, "outputName");
    if (name.isEmpty()) {
        const QFileInfo info(input);
        name = info.completeBaseName() + QStringLiteral("_patched");
    }
    if (!name.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
        name += QStringLiteral(".img");
    const QString outputPath = gf::img::uniqueOutputPath(QStringLiteral("%1/%2").arg(dir, name));
    gf::img::writeFileOrThrow(outputPath, patched);

    for (const QString& change : outcome.changes)
        ctx.info(change);
    ctx.info(QStringLiteral("ramdisk 格式：%1，原 init SHA1：%2")
                 .arg(outcome.ramdiskFormat, outcome.initSha1.isEmpty() ? QStringLiteral("无")
                                                                        : outcome.initSha1));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("修补完成：%1（%2）。请用 Fastboot 刷入对应分区后重启。")
               .arg(outputPath, util::formatBytes(patched.size())));
}

void rootPropPatch(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = textOption(options, "input");
    if (input.isEmpty() || !QFileInfo::exists(input))
        fail(QStringLiteral("请选择存在的 boot / init_boot 镜像。"));
    const QString propsText = textOption(options, "props");
    const QMap<QString, QString> overrides = root::parsePropLines(propsText);
    if (overrides.isEmpty())
        fail(QStringLiteral("请填写要修改的属性（每行 key=value）。"));

    const QString target = choiceOption(options, "target", QStringLiteral("default.prop"));
    QStringList targets;
    if (target == QLatin1String("both"))
        targets = {QStringLiteral("default.prop"), QStringLiteral("prop.default")};
    else
        targets = {target};

    ctx.setProgress(10);
    ctx.setStatus(QStringLiteral("解析 boot 镜像"));
    const QByteArray bootData = gf::img::readFileOrThrow(input);
    gf::img::boot::Image image = gf::img::boot::parse(bootData);
    if (image.kind == gf::img::boot::ImageKind::VendorBoot)
        fail(QStringLiteral("vendor_boot 暂不支持属性修改，请改用 boot / init_boot。"));
    if (image.ramdisk.isEmpty())
        fail(QStringLiteral("该镜像没有 ramdisk。"));

    ctx.setProgress(30);
    ctx.setStatus(QStringLiteral("解包 ramdisk"));
    QString format;
    lz4::Frame frame = lz4::Frame::Unknown;
    const QByteArray plain = decompressRamdiskAuto(image.ramdisk, &format, &frame);
    if (!gf::img::boot::isCpio(plain))
        fail(QStringLiteral("ramdisk 解压后不是 cpio 归档。"));

    ctx.setProgress(50);
    ctx.setStatus(QStringLiteral("修改属性文件"));
    QStringList changes;
    QString error;
    const QByteArray patchedCpio = applyPropertyOverrides(plain, targets, overrides, &changes, &error);
    if (patchedCpio.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("修改属性失败。") : error);
    for (const QString& change : changes)
        ctx.info(QStringLiteral("已修改 %1").arg(change));

    ctx.setProgress(75);
    ctx.setStatus(QStringLiteral("重新打包"));
    image.ramdisk = compressRamdiskLike(patchedCpio, format, frame);
    const QByteArray patched = gf::img::boot::serialize(image);

    const QString dir = ensureOutputDir(textOption(options, "out"), QStringLiteral("root_patch"));
    QString name = textOption(options, "outputName");
    if (name.isEmpty()) {
        const QFileInfo info(input);
        name = info.completeBaseName() + QStringLiteral("_props");
    }
    if (!name.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
        name += QStringLiteral(".img");
    const QString outputPath = gf::img::uniqueOutputPath(QStringLiteral("%1/%2").arg(dir, name));
    gf::img::writeFileOrThrow(outputPath, patched);
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("属性修改完成：%1（%2，ramdisk %3）")
               .arg(outputPath, util::formatBytes(patched.size()), format));
}

void rootModules(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    ctx.setStatus(QStringLiteral("读取 Magisk / KernelSU 模块"));
    QString error;
    const QList<MagiskModule> modules = readModules(serial, &error);
    if (!error.isEmpty())
        fail(error);
    if (modules.isEmpty()) {
        ctx.warn(QStringLiteral("设备上没有已安装的 Magisk/KernelSU 模块。"));
        ctx.ok(QStringLiteral("模块列表为空。"));
        return;
    }
    for (const MagiskModule& module : modules) {
        QString state;
        if (module.removePending)
            state = QStringLiteral("待卸载");
        else if (module.disabled)
            state = QStringLiteral("已禁用");
        else
            state = QStringLiteral("已启用");
        ctx.info(QStringLiteral("%1  [%2]  %3 %4")
                     .arg(module.id, state, module.name,
                          module.version.isEmpty() ? QString() : QStringLiteral("v") + module.version));
        if (!module.description.isEmpty())
            ctx.debug(QStringLiteral("    %1").arg(module.description));
    }
    ctx.ok(QStringLiteral("共读取到 %1 个模块。").arg(modules.size()));
}

void rootModule(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString action = choiceOption(options, "action", QStringLiteral("list"));
    const QString moduleId = textOption(options, "module");
    const bool reboot = boolOption(options, "reboot", false);

    if (action == QLatin1String("install")) {
        const QString zip = textOption(options, "zip");
        if (zip.isEmpty() || !QFileInfo::exists(zip))
            fail(QStringLiteral("请选择存在的 Magisk 模块 zip 文件。"));
        const QString remote = QStringLiteral("/data/local/tmp/%1").arg(QFileInfo(zip).fileName());
        ctx.setStatus(QStringLiteral("推送模块"));
        const auto push = gf::device::AdbClient::push(serial, zip, remote);
        if (!push.ok)
            fail(QStringLiteral("推送模块失败：%1") 
                     .arg(push.error.isEmpty() ? push.output : push.error));
        ctx.setStatus(QStringLiteral("安装模块"));
        QString error;
        const QString output = adbSuShell(
            serial,
            QStringLiteral("magisk --install-module %1").arg(shellQuote(remote)), &error, 120000);
        if (!error.isEmpty())
            fail(QStringLiteral("安装模块失败：%1\n%2").arg(error, output));
        gf::device::AdbClient::shell(serial, QStringLiteral("rm -f %1").arg(shellQuote(remote)), 20000);
        if (reboot)
            gf::device::AdbClient::reboot(serial, QStringLiteral("system"));
        ctx.ok(QStringLiteral("模块已安装：%1%2")
                   .arg(QFileInfo(zip).fileName(), reboot ? QStringLiteral("，设备正在重启") : QString()));
        return;
    }

    if (moduleId.isEmpty())
        fail(QStringLiteral("请填写模块 id（可在「模块列表」工具中查看）。"));
    if (action == QLatin1String("uninstall")) {
        QString error;
        if (!setModuleFlag(serial, moduleId, QStringLiteral("remove"), true, &error))
            fail(QStringLiteral("标记卸载失败：%1").arg(error));
        if (reboot)
            gf::device::AdbClient::reboot(serial, QStringLiteral("system"));
        ctx.ok(QStringLiteral("已标记卸载模块 %1，重启后生效%2")
                   .arg(moduleId, reboot ? QStringLiteral("（设备正在重启）") : QString()));
        return;
    }
    if (action == QLatin1String("disable") || action == QLatin1String("enable")) {
        const bool disable = action == QLatin1String("disable");
        QString error;
        if (!setModuleFlag(serial, moduleId, QStringLiteral("disable"), disable, &error))
            fail(QStringLiteral("修改模块状态失败：%1").arg(error));
        ctx.ok(QStringLiteral("模块 %1 已%2，重启后生效。")
                   .arg(moduleId, disable ? QStringLiteral("禁用") : QStringLiteral("启用")));
        return;
    }
    fail(QStringLiteral("不支持的操作：%1").arg(action));
}


void rootHideCheck(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    ctx.setStatus(QStringLiteral("检查 Root 痕迹"));
    QString error;
    const RootStatus status = detect(serial, &error);
    if (!error.isEmpty())
        fail(error);
    if (!status.rooted) {
        ctx.ok(QStringLiteral("设备未检测到 Root，无需隐藏处理。"));
        return;
    }
    ctx.info(QStringLiteral("检测到 Root：%1 %2").arg(engineName(status.engine), status.version));
    if (!status.suPath.isEmpty())
        ctx.warn(QStringLiteral("可见 su 路径：%1").arg(status.suPath));
    for (const QString& line : status.evidence)
        ctx.log(0, QStringLiteral("证据：%1").arg(line));
    ctx.setProgress(70);

    switch (status.engine) {
    case RootEngine::Magisk:
        ctx.info(QStringLiteral("Magisk：启用 Zygisk + DenyList，并把目标应用加入 DenyList；需要更彻底时可配合 Shamiko 等模块。"));
        break;
    case RootEngine::KernelSU:
        ctx.info(QStringLiteral("KernelSU：在 KernelSU 管理器中为目标应用关闭 Root 授权（卸载挂载）。"));
        break;
    case RootEngine::APatch:
        ctx.info(QStringLiteral("APatch：在 APatch 管理器中把目标应用加入排除列表。"));
        break;
    default:
        ctx.info(QStringLiteral("其它 Root 方案：请使用对应管理器的隐藏/排除功能。"));
        break;
    }
    ctx.warn(QStringLiteral("本工具只做痕迹检测与操作说明，不会修改系统文件；隐藏配置完成后可再次运行本工具复查。"));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("Root 痕迹检查完成（%1）").arg(engineName(status.engine)));
}


void rootApatchInspect(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = textOption(options, "input");
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择 boot.img / init_boot.img");
    QFile file(input);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(QStringLiteral("无法打开 %1").arg(input).toStdString());
    const QByteArray data = file.readAll();
    file.close();
    if (data.size() > 512 * 1024 * 1024)
        throw std::runtime_error("镜像大于 512MB，请确认选择的是 boot/init_boot 镜像");

    const KernelImageInfo info = inspectKernelImage(data);
    if (!info.ok)
        throw std::runtime_error(info.error.toStdString());
    ctx.info(QStringLiteral("镜像类型：%1 · 头部版本 v%2").arg(info.imageKind).arg(info.headerVersion));
    ctx.info(QStringLiteral("kernel：%1 · 压缩格式 %2")
                 .arg(util::formatBytes(info.kernelSize), info.kernelCompression.isEmpty()
                                                               ? QStringLiteral("无 kernel")
                                                               : info.kernelCompression));
    ctx.info(QStringLiteral("ramdisk：%1").arg(util::formatBytes(info.ramdiskSize)));
    if (!info.kernelVersion.isEmpty())
        ctx.info(QStringLiteral("内核版本：%1").arg(info.kernelVersion));
    for (const QString& note : info.notes)
        ctx.warn(note);
    if (info.kernelSize <= 0)
        throw std::runtime_error("该镜像不包含 kernel，APatch 需要 boot 分区的内核镜像");

    QString outputDir = textOption(options, "output");
    if (outputDir.isEmpty())
        outputDir = QFileInfo(input).absolutePath() + QStringLiteral("/apatch_backup");
    if (!util::ensureDir(outputDir))
        throw std::runtime_error(QStringLiteral("无法创建目录 %1").arg(outputDir).toStdString());

    img::boot::Image image = img::boot::parse(data);
    const QString stem = QFileInfo(input).completeBaseName();
    const QString kernelPath = QDir(outputDir).filePath(stem + QStringLiteral("_kernel.bin"));
    const QString ramdiskPath = QDir(outputDir).filePath(stem + QStringLiteral("_ramdisk.bin"));
    if (!util::writeFile(kernelPath, image.kernel))
        throw std::runtime_error("写入 kernel 失败");
    util::writeFile(ramdiskPath, image.ramdisk);

    QStringList report;
    report << QStringLiteral("XMF APatch 内核检查报告")
           << QStringLiteral("源文件: %1").arg(QFileInfo(input).absoluteFilePath())
           << QStringLiteral("镜像类型: %1 (v%2)").arg(info.imageKind).arg(info.headerVersion)
           << QStringLiteral("kernel 大小: %1 字节").arg(info.kernelSize)
           << QStringLiteral("kernel 压缩: %1").arg(info.kernelCompression)
           << QStringLiteral("kernel 版本: %1").arg(info.kernelVersion)
           << QStringLiteral("ramdisk 大小: %1 字节").arg(info.ramdiskSize)
           << QStringLiteral("")
           << QStringLiteral("说明：APatch 的实际修补需要在 APatch 管理器中选择 boot 镜像完成；")
           << QStringLiteral("本工具只负责提取与备份 kernel/ramdisk，不修改镜像内容。");
    const QString reportPath = QDir(outputDir).filePath(stem + QStringLiteral("_apatch_report.txt"));
    util::writeFile(reportPath, report.join(QLatin1Char('\n')).toUtf8());

    ctx.setProgress(100);
    ctx.ok(QStringLiteral("已备份 kernel/ramdisk 到 %1").arg(outputDir));
    ctx.info(QStringLiteral("报告：%1").arg(reportPath));
    ctx.warn(QStringLiteral("APatch 修补请在 APatch 管理器中完成（本工具不修改内核镜像）"));
}

void registerPlannedRootTools()
{
    ToolSpec spec;
    spec.category = QString::fromLatin1(kRootCategory);
    spec.group = QStringLiteral("advanced");
    spec.maturity = 2;

    spec.id = QStringLiteral("root.hide");
    spec.name = QStringLiteral("Root 痕迹检测 / 隐藏检查");
    spec.desc = QStringLiteral("检测 Root 方案、su 路径与可见证据，并给出对应管理器的隐藏配置步骤（只读）");
    spec.icon = QStringLiteral("eye-off");
    spec.keywords = {QStringLiteral("root"), QStringLiteral("hide"), QStringLiteral("隐藏"), QStringLiteral("检测")};
    ToolRegistry::instance().add(spec, rootHideCheck);

    spec.id = QStringLiteral("root.apatch.patch");
    spec.name = QStringLiteral("APatch 内核检查 / 备份");
    spec.desc = QStringLiteral("检查 boot 镜像的内核/压缩/版本并备份 kernel、ramdisk（修补需用 APatch 管理器）");
    spec.icon = QStringLiteral("flask-conical");
    spec.keywords = {QStringLiteral("apatch"), QStringLiteral("kernel"), QStringLiteral("内核")};
    spec.fields = {
        fields::path(QStringLiteral("input"), QStringLiteral("boot.img / init_boot.img"),
                     QStringLiteral("*.img *.bin")),
        fields::dir(QStringLiteral("output"), QStringLiteral("备份目录（可选）"), QString(), false),
    };
    ToolRegistry::instance().add(spec, rootApatchInspect);
}

} // namespace

void registerRootTools()
{
    ToolSpec spec;
    spec.category = QString::fromLatin1(kRootCategory);

    spec.id = QStringLiteral("root.status");
    spec.name = QStringLiteral("Root 状态检测");
    spec.desc = QStringLiteral("检测 Magisk / KernelSU / APatch / su 状态、引导锁定与构建信息");
    spec.icon = QStringLiteral("shield-check");
    spec.keywords = {QStringLiteral("root"), QStringLiteral("magisk"), QStringLiteral("ksu"),
                     QStringLiteral("apatch")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
    };
    addTool(spec, rootStatus);
    spec.fields.clear();

    spec.id = QStringLiteral("root.boot.patch");
    spec.name = QStringLiteral("Boot 镜像修补");
    spec.desc = QStringLiteral("用 magiskinit / KernelSU 文件修补 boot/init_boot ramdisk 并输出新镜像");
    spec.icon = QStringLiteral("package");
    spec.keywords = {QStringLiteral("magisk"), QStringLiteral("root"), QStringLiteral("boot"),
                     QStringLiteral("patch"), QStringLiteral("修补")};
    spec.fields = {
        fields::path(QStringLiteral("input"), QStringLiteral("boot / init_boot 镜像"),
                     QStringLiteral("*.img")),
        fields::path(QStringLiteral("payload"), QStringLiteral("注入文件（magiskinit 等）"),
                     QStringLiteral("*")),
        fields::choice(QStringLiteral("engine"), QStringLiteral("修补方案"),
                       {QStringLiteral("magisk — Magisk magiskinit 注入"),
                        QStringLiteral("ksu — KernelSU 提供的注入文件"),
                        QStringLiteral("generic — 通用 ramdisk init 替换")},
                       0),
        fields::boolean(QStringLiteral("backupInit"), QStringLiteral("备份原始 init 到 .backup"), true),
        fields::boolean(QStringLiteral("keepVerity"), QStringLiteral("保留 dm-verity"), true),
        fields::boolean(QStringLiteral("keepForceEncrypt"), QStringLiteral("保留强制加密"), false),
        fields::dir(QStringLiteral("out"), QStringLiteral("输出目录"), QStringLiteral("留空使用用户数据目录"), false),
        fields::text(QStringLiteral("outputName"), QStringLiteral("输出文件名"), {}, {}, {}, false),
    };
    addTool(spec, rootBootPatch);
    spec.fields.clear();

    spec.id = QStringLiteral("root.prop.patch");
    spec.name = QStringLiteral("ramdisk 属性修改");
    spec.desc = QStringLiteral("修改 boot ramdisk 中 default.prop / prop.default 的属性并输出新镜像");
    spec.icon = QStringLiteral("file-cog");
    spec.keywords = {QStringLiteral("prop"), QStringLiteral("default.prop"), QStringLiteral("ramdisk")};
    spec.fields = {
        fields::path(QStringLiteral("input"), QStringLiteral("boot / init_boot 镜像"),
                     QStringLiteral("*.img")),
        fields::text(QStringLiteral("props"), QStringLiteral("属性（每行 key=value）"),
                     QStringLiteral("ro.debuggable=1")),
        fields::choice(QStringLiteral("target"), QStringLiteral("目标文件"),
                       {QStringLiteral("default.prop — 仅 default.prop"),
                        QStringLiteral("prop.default — 仅 prop.default"),
                        QStringLiteral("both — 两个都修改")},
                       0),
        fields::dir(QStringLiteral("out"), QStringLiteral("输出目录"), QStringLiteral("留空使用用户数据目录"), false),
        fields::text(QStringLiteral("outputName"), QStringLiteral("输出文件名"), {}, {}, {}, false),
    };
    addTool(spec, rootPropPatch);
    spec.fields.clear();

    spec.id = QStringLiteral("root.modules");
    spec.name = QStringLiteral("模块列表");
    spec.desc = QStringLiteral("读取 Magisk / KernelSU 模块列表与启用状态");
    spec.icon = QStringLiteral("list-checks");
    spec.keywords = {QStringLiteral("magisk"), QStringLiteral("module"), QStringLiteral("模块")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
    };
    addTool(spec, rootModules);
    spec.fields.clear();

    spec.id = QStringLiteral("root.module");
    spec.name = QStringLiteral("模块管理");
    spec.desc = QStringLiteral("安装 / 卸载 / 禁用 / 启用 Magisk 模块（需要 root）");
    spec.icon = QStringLiteral("package");
    spec.keywords = {QStringLiteral("magisk"), QStringLiteral("module"), QStringLiteral("模块")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                       {QStringLiteral("install — 安装模块 zip"),
                        QStringLiteral("uninstall — 卸载模块"),
                        QStringLiteral("disable — 禁用模块"),
                        QStringLiteral("enable — 启用模块")},
                       0),
        fields::text(QStringLiteral("module"), QStringLiteral("模块 id"), {}, QStringLiteral("卸载/禁用/启用时填写"),
                     {}, false),
        fields::path(QStringLiteral("zip"), QStringLiteral("模块 zip"), QStringLiteral("*.zip"),
                     QStringLiteral("安装时填写"), false),
        fields::boolean(QStringLiteral("reboot"), QStringLiteral("完成后重启设备"), false),
    };
    addTool(spec, rootModule);
    spec.fields.clear();

    registerPlannedRootTools();
}

} // namespace gf
