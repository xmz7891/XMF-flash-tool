#include "BypassSupport.h"

#include "ExtrasUtils.h"

#include "core/TaskManager.h"
#include "core/ToolRegistry.h"
#include "core/Util.h"
#include "device/AdbClient.h"

#include <QFileInfo>

namespace gf {

namespace {

using namespace gf::extras;
using namespace gf::extras::bypass;

void addGuideTool(const ToolSpec& spec, const QStringList& lines)
{
    ToolRegistry::instance().add(spec, [lines](TaskContext& ctx, const QVariantMap&) {
        for (const QString& line : lines)
            ctx.log(0, line);
        ctx.setProgress(100);
        ctx.ok(QStringLiteral("以上为官方办理流程说明，本工具不提供绕过验证的实现"));
    });
}


constexpr const char* kCategory = "bypass";

void bypassCodes(TaskContext& ctx, const QVariantMap&)
{
    ctx.setStatus(QStringLiteral("列出工程模式拨号指令"));
    for (const DialCode& code : dialCodes())
        ctx.info(QStringLiteral("%1  %2 — %3").arg(code.code, code.name, code.note));
    ctx.info(QStringLiteral("说明：拨号指令由系统拨号盘识别，不是所有机型/系统版本都保留；"
                            "可用「拨号指令」工具通过 ADB 尝试打开。"));
    ctx.ok(QStringLiteral("共整理 %1 个常见工程模式入口。").arg(dialCodes().size()));
}

void bypassDial(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString custom = textOption(options, "code");
    QString code = custom;
    QString name;
    if (code.isEmpty()) {
        const QString id = choiceOption(options, "preset", QStringLiteral("4636"));
        const DialCode* entry = findDialCode(id);
        if (!entry)
            fail(QStringLiteral("未知的拨号指令预设：%1").arg(id));
        code = entry->code;
        name = entry->name;
    } else if (!isValidDialCode(code)) {
        fail(QStringLiteral("自定义拨号指令只能包含数字、*、#：%1").arg(code));
    }
    const QString command = buildDialIntentCommand(code);
    ctx.setStatus(QStringLiteral("在设备上打开 %1").arg(code));
    ctx.info(QStringLiteral("执行：%1").arg(command));
    const auto result = gf::device::AdbClient::shell(serial, command, 30000);
    if (!result.output.isEmpty())
        logOutputLines(ctx, result.output, 50);
    const QString combined = (result.output + QLatin1Char('\n') + result.error).toLower();
    if (!result.ok || combined.contains(QStringLiteral("error"))) {
        ctx.warn(QStringLiteral("ADB 未能自动打开该入口（可能被 ROM 屏蔽或没有拨号应用）。"
                                "也可以手动在拨号盘输入：%1")
                     .arg(code));
        if (!result.ok && result.error.isEmpty() == false)
            ctx.debug(result.error);
    } else {
        ctx.info(QStringLiteral("设备屏幕应已弹出对应工程/测试界面（部分入口需要手动点击）。"));
    }
    ctx.ok(name.isEmpty() ? QStringLiteral("拨号指令已发送：%1").arg(code)
                          : QStringLiteral("已发送「%1」：%2").arg(name, code));
}

void bypassActivities(TaskContext& ctx, const QVariantMap&)
{
    ctx.setStatus(QStringLiteral("列出工程模式 Activity"));
    for (const EngineeringActivity& activity : engineeringActivities()) {
        ctx.info(QStringLiteral("%1  →  %2").arg(activity.name, activity.component));
        ctx.debug(QStringLiteral("    %1").arg(activity.note));
    }
    ctx.info(QStringLiteral("说明：Activity 组件名来自常见 ROM，厂商可能改名或移除；"
                            "可用「工程模式」工具逐个尝试。"));
    ctx.ok(QStringLiteral("共整理 %1 个工程模式入口。").arg(engineeringActivities().size()));
}

void bypassFactory(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    const QString action = choiceOption(options, "action", QStringLiteral("list"));
    const QString custom = textOption(options, "component");
    if (action == QLatin1String("launch") || !custom.isEmpty()) {
        QString component = custom;
        QString name;
        if (component.isEmpty()) {
            const QString id = choiceOption(options, "activity", QStringLiteral("testing-settings"));
            const EngineeringActivity* activity = findEngineeringActivity(id);
            if (!activity)
                fail(QStringLiteral("未知的工程模式预设：%1").arg(id));
            component = activity->component;
            name = activity->name;
        } else if (!isValidComponent(component)) {
            fail(QStringLiteral("自定义组件需要写成 package/activity：%1").arg(component));
        }
        const QString command = buildActivityStartCommand(component);
        ctx.setStatus(QStringLiteral("启动 %1").arg(component));
        ctx.info(QStringLiteral("执行：%1").arg(command));
        const auto result = gf::device::AdbClient::shell(serial, command, 30000);
        if (!result.output.isEmpty())
            logOutputLines(ctx, result.output, 50);
        const QString combined = (result.output + QLatin1Char('\n') + result.error).toLower();
        if (!result.ok || combined.contains(QStringLiteral("error"))
            || combined.contains(QStringLiteral("unable"))) {
            ctx.warn(QStringLiteral("该入口在当前 ROM 上不可用（组件不存在或没有权限）。"
                                    "可换用其它预设，或先执行「列出工程模式入口」查看。"));
            ctx.ok(QStringLiteral("已尝试启动 %1，但系统未确认成功。").arg(component));
        } else {
            ctx.ok(QStringLiteral("已启动「%1」（%2）。")
                       .arg(name.isEmpty() ? component : name, component));
        }
        return;
    }

    for (const EngineeringActivity& activity : engineeringActivities())
        ctx.info(QStringLiteral("%1  →  %2").arg(activity.name, activity.component));
    ctx.ok(QStringLiteral("工程模式入口列表读取完成（选择 launch 操作可尝试启动）。"));
}

void bypassDeviceInfo(TaskContext& ctx, const QVariantMap& options)
{
    const QString serial = requireAdbSerial(options);
    ctx.setStatus(QStringLiteral("读取机型与出厂信息"));
    QString error;
    const QMap<QString, QString> props = gf::device::AdbClient::allProps(serial, &error);
    if (props.isEmpty())
        fail(error.isEmpty() ? QStringLiteral("读取设备属性失败。") : error);
    const FactoryInfo info = factoryInfoFromProps(props);
    const QString text = factoryInfoText(info);
    for (const QString& line : text.split(QLatin1Char('\n')))
        ctx.info(line);

    if (boolOption(options, "export", false)) {
        const QString dir = ensureOutputDir(textOption(options, "out"), QStringLiteral("factory_info"));
        QString token = info.model.isEmpty() ? serial : info.model;
        for (QChar& ch : token) {
            if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_'))
                ch = QLatin1Char('_');
        }
        const QString path = uniqueOutputFile(dir, QStringLiteral("factory_") + token, QStringLiteral("info"));
        saveTextFile(path, text + QLatin1Char('\n'));
        ctx.info(QStringLiteral("已导出：%1").arg(path));
    }
    ctx.ok(QStringLiteral("机型/出厂信息读取完成（%1）。")
               .arg(info.model.isEmpty() ? serial : info.model));
}

void registerPlanned()
{
    ToolSpec spec;
    spec.category = QString::fromLatin1(kCategory);
    spec.maturity = 2;
    spec.group = QStringLiteral("official");

    spec.id = QStringLiteral("bypass.frp.guide");
    spec.name = QStringLiteral("FRP 官方恢复流程说明");
    spec.desc = QStringLiteral("各品牌「恢复出厂设置保护」的官方处理渠道与材料清单（需本人凭证），"
                               "不包含任何绕过账号验证的实现");
    spec.icon = QStringLiteral("info");
    spec.keywords = {QStringLiteral("frp"), QStringLiteral("官方"), QStringLiteral("流程")};
    addGuideTool(spec, {
        QStringLiteral("FRP（恢复出厂设置保护）是 Google 与厂商的账号验证机制"),
        QStringLiteral("官方途径 1：登录设备此前绑定的 Google / 厂商账号完成验证"),
        QStringLiteral("官方途径 2：携带购机凭证前往品牌官方售后，由售后解除 FRP"),
        QStringLiteral("小米 / OPPO / vivo / 三星 等品牌售后均可凭凭证处理"),
        QStringLiteral("注意：本工具不提供绕过 FRP 的实现；非本人设备请勿尝试"),
    });

    spec.id = QStringLiteral("bypass.carrier.guide");
    spec.name = QStringLiteral("运营商/厂商工程权限说明");
    spec.desc = QStringLiteral("工程模式与网络锁相关权限均由厂商/运营商控制，本工具仅汇总官方申请流程（规划中）");
    spec.icon = QStringLiteral("radio");
    spec.keywords = {QStringLiteral("工程模式"), QStringLiteral("运营商"), QStringLiteral("官方")};
    addGuideTool(spec, {
        QStringLiteral("工程模式 / 网络锁 / 运营商参数均由厂商与运营商控制"),
        QStringLiteral("官方申请：向品牌售后或运营商客服提交设备信息与用途说明"),
        QStringLiteral("小米：售后申请工程模式权限；三星：官方服务中心"),
        QStringLiteral("部分品牌提供官网工单渠道，需提供购机凭证与 IMEI"),
        QStringLiteral("注意：本工具不提供隐藏工程指令或提权实现"),
    });
}

} // namespace

void registerBypassTools()
{
    ToolSpec spec;
    spec.category = QString::fromLatin1(kCategory);

    spec.id = QStringLiteral("bypass.codes");
    spec.name = QStringLiteral("工程模式拨号指令");
    spec.desc = QStringLiteral("常见 *#*#...#*#* 工程/测试入口列表（只读提示）");
    spec.icon = QStringLiteral("terminal");
    spec.keywords = {QStringLiteral("工程模式"), QStringLiteral("拨号"), QStringLiteral("*#*#")};
    addTool(spec, bypassCodes);

    spec.id = QStringLiteral("bypass.dial");
    spec.name = QStringLiteral("打开工程模式拨号");
    spec.desc = QStringLiteral("通过 ADB 在设备上打开常见工程模式拨号入口");
    spec.icon = QStringLiteral("smartphone");
    spec.keywords = {QStringLiteral("工程模式"), QStringLiteral("dial"), QStringLiteral("adb")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("preset"), QStringLiteral("预设入口"),
                       {QStringLiteral("4636 — 手机信息 / 电池 / 网络"),
                        QStringLiteral("06 — 显示 IMEI"),
                        QStringLiteral("6484 — 小米硬件检测（CIT）"),
                        QStringLiteral("3646633 — 联发科工程模式"),
                        QStringLiteral("7378423 — Sony 服务菜单"),
                        QStringLiteral("2664 — HTC 触摸屏测试"),
                        QStringLiteral("225 — 版本信息 / 日历"),
                        QStringLiteral("001 — 工厂测试入口（部分机型）")},
                       0),
        fields::text(QStringLiteral("code"), QStringLiteral("自定义拨号指令"), {}, QStringLiteral("例如 *#*#4636#*#*"),
                     QStringLiteral("填写后忽略预设"), false),
    };
    addTool(spec, bypassDial);
    spec.fields.clear();

    spec.id = QStringLiteral("bypass.activities");
    spec.name = QStringLiteral("工程模式入口列表");
    spec.desc = QStringLiteral("常见工程/测试 Activity 组件列表");
    spec.icon = QStringLiteral("list-checks");
    spec.keywords = {QStringLiteral("activity"), QStringLiteral("工程模式"), QStringLiteral("am start")};
    addTool(spec, bypassActivities);

    spec.id = QStringLiteral("bypass.factory");
    spec.name = QStringLiteral("工程模式启动");
    spec.desc = QStringLiteral("尝试通过 am start 启动常见工程/工厂模式 Activity");
    spec.icon = QStringLiteral("wrench");
    spec.keywords = {QStringLiteral("factory"), QStringLiteral("工程模式"), QStringLiteral("am start")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                       {QStringLiteral("list — 仅列出入口"),
                        QStringLiteral("launch — 尝试启动所选入口")},
                       0),
        fields::choice(QStringLiteral("activity"), QStringLiteral("入口"),
                       {QStringLiteral("testing-settings — AOSP 测试设置"),
                        QStringLiteral("dev-settings — 开发者选项"),
                        QStringLiteral("radio-info — 网络 / 无线信息"),
                        QStringLiteral("mtk-engineer — 联发科工程模式"),
                        QStringLiteral("mtk-engineer-sim — 联发科 SIM 工具"),
                        QStringLiteral("miui-cit — 小米 CIT 测试"),
                        QStringLiteral("oppo-engineer — OPPO 工程模式")},
                       0),
        fields::text(QStringLiteral("component"), QStringLiteral("自定义组件"), {},
                     QStringLiteral("package/activity，填写后忽略预设"), {}, false),
    };
    addTool(spec, bypassFactory);
    spec.fields.clear();

    spec.id = QStringLiteral("bypass.deviceinfo");
    spec.name = QStringLiteral("机型 / 出厂信息");
    spec.desc = QStringLiteral("读取机型、芯片平台、出厂 Build、基带、序列号等信息并可导出");
    spec.icon = QStringLiteral("cpu");
    spec.keywords = {QStringLiteral("机型"), QStringLiteral("出厂"), QStringLiteral("build")};
    spec.fields = {
        fields::text(QStringLiteral("serial"), QStringLiteral("序列号"), {}, {}, {}, false),
        fields::boolean(QStringLiteral("export"), QStringLiteral("导出到文本文件"), false),
        fields::dir(QStringLiteral("out"), QStringLiteral("输出目录"), QStringLiteral("留空使用用户数据目录"), false),
    };
    addTool(spec, bypassDeviceInfo);
    spec.fields.clear();

    registerPlanned();
}

} // namespace gf
