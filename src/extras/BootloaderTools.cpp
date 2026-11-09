#include "BootloaderSupport.h"

#include "ExtrasUtils.h"

#include "core/TaskManager.h"
#include "core/ToolRegistry.h"
#include "device/FastbootClient.h"

#include "core/Util.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

namespace gf {

namespace {

using namespace gf::extras;
using namespace gf::extras::bootloader;

constexpr const char* kCategory = "bootloader";

QString currentFastbootSerial(const QVariantMap& options)
{
    return requireFastbootSerial(options);
}

void bootloaderInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = currentFastbootSerial(options);
    ctx.setStatus(QStringLiteral("读取 Bootloader 信息"));
    QString error;
    const QMap<QString, QString> vars = gf::device::FastbootClient::getvarAll(serial, &error);
    if (vars.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("读取 Fastboot 变量失败。") : error);
    const BootloaderInfo info = parseBootloaderInfo(vars);
    for (const QString& line : info.summary().split(QLatin1Char('\n')))
        ctx.info(line);
    ctx.ok(QStringLiteral("Bootloader 信息读取完成（%1）")
               .arg(info.product.isEmpty() ? serial : info.product));
}

void bootloaderGetvar(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = currentFastbootSerial(options);
    QString variable = textOption(options, "variable");
    if (variable.isEmpty())
        variable = QStringLiteral("all");
    ctx.setStatus(QStringLiteral("读取变量 %1").arg(variable));
    const auto result = gf::device::FastbootClient::getvar(serial, variable);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 400);
    if (!result.ok)
        fail(result.error.isEmpty() ? QStringLiteral("读取变量失败。") : result.error);
    ctx.ok(QStringLiteral("变量 %1 读取完成。").arg(variable));
}

void bootloaderSlots(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = currentFastbootSerial(options);
    const QString action = choiceOption(options, "action", QStringLiteral("info"));
    if (action == QLatin1String("switch")) {
        if (!boolOption(options, "confirm", false))
            fail(QStringLiteral("切换活动槽位会影响下次启动，请勾选「确认切换槽位」。"));
        QString slot = choiceOption(options, "slot", QStringLiteral("a"));
        if (!isValidSlot(slot))
            fail(QStringLiteral("槽位只能是 a 或 b。"));
        ctx.setStatus(QStringLiteral("切换活动槽位到 %1").arg(slot));
        const auto result =
            gf::device::FastbootClient::runFor(serial, {activeSlotArgument(slot)});
        if (!result.output.isEmpty())
            logOutputLines(ctx, result.output, 50);
        if (!result.ok)
            fail(result.error.isEmpty() ? QStringLiteral("切换槽位失败。") : result.error);
        ctx.ok(QStringLiteral("已切换活动槽位到 %1，设备重启后生效。").arg(slot));
        return;
    }

    ctx.setStatus(QStringLiteral("读取 A/B 槽位状态"));
    QString error;
    const QMap<QString, QString> vars = gf::device::FastbootClient::getvarAll(serial, &error);
    if (vars.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("读取 Fastboot 变量失败。") : error);
    const BootloaderInfo info = parseBootloaderInfo(vars);
    if (info.slotCount <= 1) {
        ctx.warn(QStringLiteral("该设备没有 A/B 双槽位（slot-count=%1）。").arg(info.slotCount));
        ctx.ok(QStringLiteral("当前槽位：%1")
                   .arg(info.currentSlot.isEmpty() ? QStringLiteral("无") : info.currentSlot));
        return;
    }
    for (const SlotState& slot : info.slotStates) {
        ctx.info(QStringLiteral("槽位 %1：%2，%3，剩余重试 %4")
                     .arg(slot.slot,
                          slot.successful ? QStringLiteral("有效") : QStringLiteral("未标记有效"),
                          slot.unbootable ? QStringLiteral("不可启动") : QStringLiteral("可启动"),
                          QString::number(slot.retryCount)));
    }
    ctx.ok(QStringLiteral("当前活动槽位：%1（共 %2 个槽位）")
               .arg(info.currentSlot.isEmpty() ? QStringLiteral("未知") : info.currentSlot)
               .arg(info.slotCount));
}

void bootloaderSetvar(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = currentFastbootSerial(options);
    const QString variable = choiceOption(options, "variable", QStringLiteral("current-slot"));
    // 只允许白名单中的可写变量；其它 bootloader 变量只读，避免误刷参数分区。
    if (variable != QLatin1String("current-slot")) {
        fail(QStringLiteral("变量 %1 不在可写白名单中。当前仅支持通过 --set-active 切换 current-slot；"
                            "其余变量请用「读取 Fastboot 变量」查看。")
                 .arg(variable));
    }
    if (!boolOption(options, "confirm", false))
        fail(QStringLiteral("写入 Bootloader 变量有启动风险，请勾选「确认写入」。"));
    const QString slot = choiceOption(options, "slot", QStringLiteral("a"));
    if (!isValidSlot(slot))
        fail(QStringLiteral("槽位只能是 a 或 b。"));
    ctx.setStatus(QStringLiteral("写入变量 current-slot=%1").arg(slot));
    const auto result = gf::device::FastbootClient::runFor(serial, {activeSlotArgument(slot)});
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 50);
    if (!result.ok)
        fail(result.error.isEmpty() ? QStringLiteral("写入变量失败。") : result.error);
    ctx.ok(QStringLiteral("已写入 current-slot=%1。").arg(slot));
}

void bootloaderUnlock(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = currentFastbootSerial(options);
    const QString action = choiceOption(options, "action", QStringLiteral("unlock"));
    const bool lock = action == QLatin1String("lock");
    if (!boolOption(options, "confirm", false))
        fail(QStringLiteral("解锁/回锁 Bootloader 会清除全部用户数据，请勾选「确认执行」。"));
    ctx.setStatus(lock ? QStringLiteral("回锁 Bootloader") : QStringLiteral("解锁 Bootloader"));
    ctx.warn(QStringLiteral("该操作会触发 data 擦除，且部分机型回锁后无法再解锁，请确认设备为本人所有。"));
    const auto result = gf::device::FastbootClient::flashingLock(serial, lock);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 100);
    if (!result.ok)
        fail(result.error.isEmpty()
                 ? (lock ? QStringLiteral("回锁失败。") : QStringLiteral("解锁失败。"))
                 : result.error);
    if (lock)
        ctx.ok(QStringLiteral("已发送回锁指令，请在设备屏幕上确认（部分设备会清除数据）。"));
    else
        ctx.ok(QStringLiteral("已发送解锁指令，请在设备屏幕上用音量键确认。"));
}

void bootloaderVerify(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = currentFastbootSerial(options);
    ctx.setStatus(QStringLiteral("读取引导校验状态"));
    QString error;
    const QMap<QString, QString> vars = gf::device::FastbootClient::getvarAll(serial, &error);
    if (vars.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("读取 Fastboot 变量失败。") : error);
    const BootloaderInfo info = parseBootloaderInfo(vars);
    ctx.info(QStringLiteral("unlocked=%1  secure=%2  verity=%3  veritymode=%4")
                 .arg(info.unlockedRaw.isEmpty() ? QStringLiteral("?") : info.unlockedRaw,
                      info.secureRaw.isEmpty() ? QStringLiteral("?") : info.secureRaw,
                      info.verityRaw.isEmpty() ? QStringLiteral("?") : info.verityRaw,
                      info.verityMode.isEmpty() ? QStringLiteral("?") : info.verityMode));
    if (!info.avbState.isEmpty())
        ctx.info(QStringLiteral("verified boot state: %1").arg(info.avbState));
    const QString lowerAvb = info.avbState.toLower();
    if (info.unlocked || lowerAvb == QLatin1String("orange") || lowerAvb == QLatin1String("red"))
        ctx.warn(QStringLiteral("引导校验已被绕过/关闭（unlocked 或 verified boot 非 green），"
                                "部分应用可能拒绝运行。"));
    else
        ctx.ok(QStringLiteral("引导校验状态正常（未解锁且 verified boot 未被破坏）。"));
}

void backupBootloaderEnvironment(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = currentFastbootSerial(options);
    QString output = options.value(QStringLiteral("output")).toString().trimmed();
    if (output.isEmpty()) {
        output = util::logsDir() + QStringLiteral("/bootloader-%1-%2.txt")
                                      .arg(serial)
                                      .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    }
    if (!util::ensureDir(QFileInfo(output).absolutePath()))
        throw std::runtime_error("无法创建输出目录");

    ctx.setStatus(QStringLiteral("读取 getvar all…"));
    QString error;
    const QMap<QString, QString> vars = device::FastbootClient::getvarAll(serial, &error);
    if (vars.size() < 2)
        throw std::runtime_error(error.isEmpty()
                                     ? "设备没有返回有效的 bootloader 变量（可能连接中断或端点超时）"
                                     : error.toStdString());
    QString partitionError;
    const auto partitions = device::FastbootClient::partitions(serial, &partitionError);

    QStringList lines;
    lines << QStringLiteral("# XMF bootloader environment backup")
          << QStringLiteral("# device: %1").arg(serial)
          << QStringLiteral("# time: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate))
          << QStringLiteral("# note: 只读快照；不同机型的恢复方式不同，本工具不会盲目写回")
          << QString();
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        lines << QStringLiteral("%1 = %2").arg(it.key(), it.value());
    lines << QString() << QStringLiteral("# partitions: %1").arg(partitions.size());
    for (const auto& partition : partitions) {
        lines << QStringLiteral("%1\t%2\t%3")
                     .arg(partition.name, partition.size,
                          partition.type.isEmpty() ? QStringLiteral("raw") : partition.type);
    }

    QFile file(output);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QStringLiteral("无法写入 %1").arg(output).toStdString());
    file.write(lines.join(QLatin1Char('\n')).toUtf8());
    file.close();
    ctx.info(QStringLiteral("变量 %1 项，分区 %2 个").arg(vars.size()).arg(partitions.size()));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("Bootloader 参数已备份：%1").arg(output));
}


void bootloaderRedTextCheck(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = currentFastbootSerial(options);
    ctx.setStatus(QStringLiteral("读取解锁与启动链状态"));
    QString error;
    const QMap<QString, QString> vars = device::FastbootClient::getvarAll(serial, &error);
    if (vars.size() < 2)
        throw std::runtime_error(error.isEmpty() ? "设备没有返回 getvar 变量" : error.toStdString());

    const QString unlocked = vars.value(QStringLiteral("unlocked")).trimmed().toLower();
    const QString secure = vars.value(QStringLiteral("secure")).trimmed().toLower();
    const QString product = vars.value(QStringLiteral("product"));
    const QString slot = vars.value(QStringLiteral("current-slot"));
    const QString bootloader = vars.value(QStringLiteral("version-bootloader"));
    if (!product.isEmpty())
        ctx.info(QStringLiteral("设备：%1").arg(product));
    if (!slot.isEmpty())
        ctx.info(QStringLiteral("当前槽位：%1").arg(slot));
    if (!bootloader.isEmpty())
        ctx.info(QStringLiteral("Bootloader：%1").arg(bootloader));
    if (!secure.isEmpty())
        ctx.info(QStringLiteral("secure：%1").arg(secure));
    ctx.info(QStringLiteral("unlocked：%1").arg(unlocked.isEmpty() ? QStringLiteral("未知") : unlocked));

    if (unlocked == QLatin1String("yes")) {
        ctx.warn(QStringLiteral("设备已解锁。多数厂商在解锁后会于开机画面显示解锁警告（红字/黄字/黑白警告屏）。"));
        ctx.info(QStringLiteral("官方恢复方式：在开发者选项开启 OEM 解锁后执行 `fastboot flashing lock` 重新上锁，"
                                "会清空用户数据，请先备份。"));
        ctx.warn(QStringLiteral("彻底移除红字需要按机型修改 ABL / param / 参数分区，本工具不提供通用实现，"
                                "请勿刷入来源不明的参数文件。"));
        ctx.ok(QStringLiteral("检测完成：已解锁，红字属于厂商正常提示。"));
    } else {
        ctx.ok(QStringLiteral("设备处于锁定状态，正常启动不会出现解锁红字。"));
    }
    ctx.setProgress(100);
}

void registerPlanned()
{
    ToolSpec spec;
    spec.category = QString::fromLatin1(kCategory);
    spec.maturity = 2;
    spec.group = QStringLiteral("advanced");

    spec.id = QStringLiteral("bootloader.redtext");
    spec.name = QStringLiteral("解锁红字检测与说明");
    spec.desc = QStringLiteral("读取解锁状态并说明红字成因、官方恢复方式（只读检测，不修改启动链）");
    spec.icon = QStringLiteral("alert-triangle");
    spec.keywords = {QStringLiteral("bootloader"), QStringLiteral("red"), QStringLiteral("红字"),
                     QStringLiteral("解锁")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号（留空自动检测）"), {}, {}, {}, false),
    };
    ToolRegistry::instance().add(spec, bootloaderRedTextCheck);

    spec.id = QStringLiteral("bootloader.backup.env");
    spec.name = QStringLiteral("Bootloader 参数备份");
    spec.desc = QStringLiteral("读取 getvar all 与分区表并保存为文本快照（只读，不改动设备）");
    spec.icon = QStringLiteral("database");
    spec.keywords = {QStringLiteral("bootloader"), QStringLiteral("backup"), QStringLiteral("getvar")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号（留空自动检测）"), {}, {}, {}, false),
        fields::path(QStringLiteral("output"), QStringLiteral("输出文件（可选）"), QStringLiteral("*.txt"),
                     QString(), false),
    };
    ToolRegistry::instance().add(spec, backupBootloaderEnvironment);
}

} // namespace

void registerBootloaderTools()
{
    ToolSpec spec;
    spec.category = QString::fromLatin1(kCategory);

    spec.id = QStringLiteral("bootloader.info");
    spec.name = QStringLiteral("Bootloader 信息");
    spec.desc = QStringLiteral("读取 bootloader 版本、产品、槽位与安全状态");
    spec.icon = QStringLiteral("shield-check");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("bootloader"), QStringLiteral("getvar")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
    };
    addTool(spec, bootloaderInfo);
    spec.fields.clear();

    spec.id = QStringLiteral("bootloader.getvar");
    spec.name = QStringLiteral("读取 Fastboot 变量");
    spec.desc = QStringLiteral("读取单个变量或 getvar all（只读）");
    spec.icon = QStringLiteral("file-text");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("getvar")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::text(QStringLiteral("variable"), QStringLiteral("变量名"), QStringLiteral("all"), {},
                     {}, false),
    };
    addTool(spec, bootloaderGetvar);
    spec.fields.clear();

    spec.id = QStringLiteral("bootloader.slots");
    spec.name = QStringLiteral("A/B 槽位");
    spec.desc = QStringLiteral("查看槽位有效性并可切换活动槽位（fastboot --set-active）");
    spec.icon = QStringLiteral("layers");
    spec.keywords = {QStringLiteral("slot"), QStringLiteral("a/b"), QStringLiteral("set-active")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                       {QStringLiteral("info — 查看槽位状态"),
                        QStringLiteral("switch — 切换活动槽位")},
                       0),
        fields::choice(QStringLiteral("slot"), QStringLiteral("目标槽位"),
                       {QStringLiteral("a — 槽位 A"), QStringLiteral("b — 槽位 B")}, 0),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("确认切换槽位"), false),
    };
    addTool(spec, bootloaderSlots);
    spec.fields.clear();

    spec.id = QStringLiteral("bootloader.setvar");
    spec.name = QStringLiteral("写入 Bootloader 变量");
    spec.desc = QStringLiteral("白名单变量写入：仅支持 current-slot（--set-active），其它变量只读");
    spec.icon = QStringLiteral("settings");
    spec.keywords = {QStringLiteral("fastboot"), QStringLiteral("setvar"), QStringLiteral("current-slot")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("variable"), QStringLiteral("变量（白名单）"),
                       {QStringLiteral("current-slot — 切换活动槽位")}, 0),
        fields::choice(QStringLiteral("slot"), QStringLiteral("槽位"),
                       {QStringLiteral("a — 槽位 A"), QStringLiteral("b — 槽位 B")}, 0),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("确认写入"), false),
    };
    addTool(spec, bootloaderSetvar);
    spec.fields.clear();

    spec.id = QStringLiteral("bootloader.unlock");
    spec.name = QStringLiteral("Bootloader 解锁 / 回锁");
    spec.desc = QStringLiteral("fastboot flashing unlock / lock（会清除数据，仅限本人设备）");
    spec.icon = QStringLiteral("unlock");
    spec.keywords = {QStringLiteral("unlock"), QStringLiteral("fastboot"), QStringLiteral("解锁")};
    spec.badge = QStringLiteral("危险");
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                       {QStringLiteral("unlock — 解锁 Bootloader"),
                        QStringLiteral("lock — 回锁 Bootloader")},
                       0),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("我确认这是本人设备且数据会被清除"),
                        false),
    };
    addTool(spec, bootloaderUnlock);
    spec.fields.clear();

    spec.id = QStringLiteral("bootloader.verify");
    spec.name = QStringLiteral("引导校验状态");
    spec.desc = QStringLiteral("读取 unlocked / secure / dm-verity / verified boot 状态");
    spec.icon = QStringLiteral("shield-alert");
    spec.keywords = {QStringLiteral("avb"), QStringLiteral("verity"), QStringLiteral("校验")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
    };
    addTool(spec, bootloaderVerify);
    spec.fields.clear();

    registerPlanned();
}

} // namespace gf
