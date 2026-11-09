#include "ExtrasUtils.h"

#include "core/TaskManager.h"
#include "core/ToolRegistry.h"
#include "core/Util.h"
#include "device/AdbClient.h"
#include "device/FastbootClient.h"

#include <QFileInfo>

namespace gf {

namespace {

using namespace gf::extras;

void addGuideTool(const ToolSpec& spec, const QStringList& lines)
{
    ToolRegistry::instance().add(spec, [lines](TaskContext& ctx, const QVariantMap&) {
        for (const QString& line : lines)
            ctx.log(0, line);
        ctx.setProgress(100);
        ctx.ok(QStringLiteral("以上为官方办理流程说明，本工具不提供绕过验证的实现"));
    });
}


constexpr const char* kCategory = "unlock";

void requireConfirm(const QVariantMap& options, const QString& action)
{
    if (!boolOption(options, "confirm", false))
        fail(QStringLiteral("%1会清除数据，请先勾选「我确认这是本人设备并了解数据会丢失」。").arg(action));
}

void erasePartition(TaskContext& ctx, const QVariantMap& options, const QString& partition,
                    const QString& action)
{
    requireConfirm(options, action);
    const QString serial = requireFastbootSerial(options);
    ctx.setStatus(QStringLiteral("擦除 %1 分区").arg(partition));
    const auto result = gf::device::FastbootClient::erase(serial, partition);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 50);
    if (!result.ok) {
        const QString hint = result.looksLikeMissingDevice()
                                 ? QStringLiteral("\n请确认设备已进入 Fastboot 模式且属于本人。")
                                 : QString();
        fail((result.error.isEmpty() ? QStringLiteral("擦除 %1 失败。").arg(partition)
                                     : result.error)
             + hint);
    }
    ctx.ok(QStringLiteral("%1 分区已擦除。").arg(partition));
}

void unlockUserdata(TaskContext& ctx, const QVariantMap& options)
{
    requireConfirm(options, QStringLiteral("格式化 / 擦除 userdata"));
    const QString serial = requireFastbootSerial(options);
    const QString method = choiceOption(options, "method", QStringLiteral("erase"));
    ctx.setStatus(QStringLiteral("处理 userdata"));
    const auto result = method == QLatin1String("format")
                            ? gf::device::FastbootClient::format(serial, QStringLiteral("userdata"),
                                                                 QStringLiteral("ext4"))
                            : gf::device::FastbootClient::erase(serial, QStringLiteral("userdata"));
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 60);
    if (!result.ok)
        fail(result.error.isEmpty() ? QStringLiteral("处理 userdata 失败。") : result.error);
    ctx.ok(QStringLiteral("userdata 已%1，首次开机将重新加密并较慢。")
               .arg(method == QLatin1String("format") ? QStringLiteral("格式化") : QStringLiteral("擦除")));
}

void unlockMetadata(TaskContext& ctx, const QVariantMap& options)
{
    erasePartition(ctx, options, QStringLiteral("metadata"),
                   QStringLiteral("清除 metadata（加密元数据）"));
}

void unlockCache(TaskContext& ctx, const QVariantMap& options)
{
    erasePartition(ctx, options, QStringLiteral("cache"), QStringLiteral("擦除 cache"));
}

void unlockFrp(TaskContext& ctx, const QVariantMap& options)
{
    requireConfirm(options, QStringLiteral("擦除 FRP 分区"));
    const QString serial = requireFastbootSerial(options);
    ctx.setStatus(QStringLiteral("擦除 FRP 分区"));
    ctx.warn(QStringLiteral("FRP 擦除只适用于本人设备（忘记自己账号密码、且设备来源合法）。"
                            "绕过他人账号验证属于违法行为，本工具不提供该能力。"));
    const auto result = gf::device::FastbootClient::erase(serial, QStringLiteral("frp"));
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 50);
    if (!result.ok)
        fail(result.error.isEmpty() ? QStringLiteral("擦除 FRP 分区失败。") : result.error);
    ctx.ok(QStringLiteral("FRP 分区已擦除。请使用自己的 Google 账号重新登录设备。"));
}

void unlockDalvik(TaskContext& ctx, const QVariantMap& options)
{
    requireConfirm(options, QStringLiteral("清除 dalvik-cache"));
    const QString serial = requireAdbSerial(options);
    ctx.setStatus(QStringLiteral("清除 dalvik-cache"));
    const QString su = findSu(serial);
    if (su.isEmpty())
        fail(QStringLiteral("清除 dalvik-cache 需要 root（su）。普通 ADB 无法删除 /data/dalvik-cache。"));
    QString error;
    adbSuShell(serial, QStringLiteral("rm -rf /data/dalvik-cache/*"), &error, 120000);
    if (!error.isEmpty())
        fail(QStringLiteral("清除 dalvik-cache 失败：%1").arg(error));
    ctx.ok(QStringLiteral("dalvik-cache 已清除，下次开机首次启动会较慢。"));
}

void unlockSimlock(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    ctx.setStatus(QStringLiteral("读取 SIM / 运营商状态"));
    QString error;
    const QMap<QString, QString> props = gf::device::AdbClient::allProps(serial, &error);
    if (props.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("读取设备属性失败。") : error);

    const QStringList keys = {
        QStringLiteral("gsm.sim.state"),          QStringLiteral("gsm.operator.alpha"),
        QStringLiteral("gsm.sim.operator.alpha"), QStringLiteral("ro.carrier"),
        QStringLiteral("ro.product.carrier"),     QStringLiteral("persist.radio.simlock"),
        QStringLiteral("ro.boot.carrier"),        QStringLiteral("gsm.version.baseband"),
    };
    bool any = false;
    for (const QString& key : keys) {
        if (!props.contains(key))
            continue;
        any = true;
        ctx.info(QStringLiteral("%1 = %2").arg(key, props.value(key)));
    }
    if (!any)
        ctx.warn(QStringLiteral("设备未上报 SIM/运营商属性（可能是无 SIM 卡或系统未暴露）。"));
    ctx.info(QStringLiteral("说明：本工具只读取状态，不执行网络锁解锁。运营商锁请携带购机凭证"
                            "联系运营商或厂商官方客服解锁。"));
    ctx.ok(QStringLiteral("SIM / 运营商状态读取完成。"));
}

void registerPlanned()
{
    ToolSpec spec;
    spec.category = QString::fromLatin1(kCategory);
    spec.maturity = 2;
    spec.group = QStringLiteral("official");

    spec.id = QStringLiteral("unlock.account.official");
    spec.name = QStringLiteral("账号锁官方移除流程");
    spec.desc = QStringLiteral("仅提供各厂商官方账号移除/激活锁解除流程说明（需本人凭证与原始购机信息），"
                               "不包含绕过账号验证的实现");
    spec.icon = QStringLiteral("info");
    spec.keywords = {QStringLiteral("账号锁"), QStringLiteral("frp"), QStringLiteral("官方"),
                     QStringLiteral("流程")};
    spec.badge = QStringLiteral("规划中");
    addGuideTool(spec, {
        QStringLiteral("适用：忘记锁屏账号 / 激活锁，且设备为您本人所有"),
        QStringLiteral("1) 准备购机凭证（发票、订单号）与设备 IMEI（设置-关于手机 或 拨号 *#06#）"),
        QStringLiteral("2) 小米 / 红米：i.mi.com 找回账号；无法找回时联系小米客服提交凭证申请解绑"),
        QStringLiteral("3) OPPO / 一加 / realme：携带购机凭证与身份证前往官方服务中心"),
        QStringLiteral("4) vivo / iQOO：官网服务支持提交激活锁解除申请"),
        QStringLiteral("5) 三星：Samsung Find My Mobile 远程移除，或官方售后处理"),
        QStringLiteral("6) 华为 / 荣耀：华为账号找回或官方客服渠道"),
        QStringLiteral("注意：本工具不会绕过账号验证；非本人设备请走官方渠道"),
    });

    spec.id = QStringLiteral("unlock.sim.official");
    spec.name = QStringLiteral("运营商网络锁官方解锁");
    spec.desc = QStringLiteral("SIM 网络锁解除需要运营商提供的官方解锁码，本版本仅提供流程说明");
    spec.icon = QStringLiteral("radio");
    spec.keywords = {QStringLiteral("sim"), QStringLiteral("运营商"), QStringLiteral("解锁")};
    spec.badge = QStringLiteral("规划中");
    addGuideTool(spec, {
        QStringLiteral("1) 确认设备是否运营商锁定（设置 → 关于手机 → 运营商锁）"),
        QStringLiteral("2) 向运营商提交解锁申请：IMEI、购机凭证、账户信息"),
        QStringLiteral("3) 审核通过后运营商会提供解锁码；换非本网 SIM 后按提示输入"),
        QStringLiteral("4) 部分品牌支持官网自助查询网络锁资格"),
        QStringLiteral("注意：网络锁解除完全由运营商控制，需符合合同条件"),
    });
}

} // namespace

void registerUnlockTools()
{
    ToolSpec spec;
    spec.category = QString::fromLatin1(kCategory);
    spec.badge = QStringLiteral("危险");

    spec.id = QStringLiteral("unlock.userdata");
    spec.name = QStringLiteral("格式化 userdata");
    spec.desc = QStringLiteral("擦除 / 格式化 userdata 分区（清除全部用户数据，仅限本人设备）");
    spec.icon = QStringLiteral("trash-2");
    spec.keywords = {QStringLiteral("userdata"), QStringLiteral("格式化"), QStringLiteral("清除")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("method"), QStringLiteral("方式"),
                       {QStringLiteral("erase — 擦除（快）"),
                        QStringLiteral("format — 格式化为 ext4（更彻底）")},
                       0),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("我确认这是本人设备并了解数据会丢失"),
                        false),
    };
    addTool(spec, unlockUserdata);
    spec.fields.clear();

    spec.id = QStringLiteral("unlock.metadata");
    spec.name = QStringLiteral("清除 metadata");
    spec.desc = QStringLiteral("擦除 metadata 分区（清除加密元数据，常用于解密失败/重刷系统）");
    spec.icon = QStringLiteral("database");
    spec.keywords = {QStringLiteral("metadata"), QStringLiteral("加密"), QStringLiteral("清除")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("我确认这是本人设备并了解数据会丢失"),
                        false),
    };
    addTool(spec, unlockMetadata);
    spec.fields.clear();

    spec.id = QStringLiteral("unlock.cache");
    spec.name = QStringLiteral("擦除 cache");
    spec.desc = QStringLiteral("擦除 cache 分区（OTA 失败 / 启动异常时使用）");
    spec.icon = QStringLiteral("trash-2");
    spec.keywords = {QStringLiteral("cache"), QStringLiteral("擦除")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("确认擦除 cache"), false),
    };
    addTool(spec, unlockCache);
    spec.fields.clear();

    spec.id = QStringLiteral("unlock.frp");
    spec.name = QStringLiteral("擦除 FRP 分区");
    spec.desc = QStringLiteral("擦除 FRP 分区（仅本人设备恢复使用；不提供绕过账号验证的能力）");
    spec.icon = QStringLiteral("shield-alert");
    spec.keywords = {QStringLiteral("frp"), QStringLiteral("恢复出厂"), QStringLiteral("本人")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("我确认这是本人设备并了解数据会丢失"),
                        false),
    };
    addTool(spec, unlockFrp);
    spec.fields.clear();

    spec.id = QStringLiteral("unlock.dalvik");
    spec.name = QStringLiteral("清除 dalvik-cache");
    spec.desc = QStringLiteral("通过 root 清除 dalvik-cache（需要 su）");
    spec.icon = QStringLiteral("trash-2");
    spec.keywords = {QStringLiteral("dalvik"), QStringLiteral("cache"), QStringLiteral("art")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::boolean(QStringLiteral("confirm"), QStringLiteral("确认清除 dalvik-cache"), false),
    };
    addTool(spec, unlockDalvik);
    spec.fields.clear();

    spec.id = QStringLiteral("unlock.simlock");
    spec.name = QStringLiteral("SIM / 运营商状态");
    spec.desc = QStringLiteral("只读查看 SIM 与运营商状态（不执行网络锁解锁）");
    spec.icon = QStringLiteral("smartphone");
    spec.keywords = {QStringLiteral("sim"), QStringLiteral("运营商"), QStringLiteral("只读")};
    spec.badge.clear();
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
    };
    addTool(spec, unlockSimlock);
    spec.fields.clear();

    registerPlanned();
}

} // namespace gf
