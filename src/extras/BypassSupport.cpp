#include "BypassSupport.h"

#include "ExtrasUtils.h"

namespace gf::extras::bypass {

namespace {

bool validComponentText(const QString& component)
{
    const QString trimmed = component.trimmed();
    const int slash = trimmed.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == trimmed.size() - 1)
        return false;
    const QString package = trimmed.left(slash);
    if (!package.contains(QLatin1Char('.')) || package.contains(QLatin1Char(' ')))
        return false;
    if (trimmed.contains(QLatin1Char('\'')) || trimmed.contains(QLatin1Char(';'))
        || trimmed.contains(QLatin1Char('&')) || trimmed.contains(QLatin1Char('|')))
        return false;
    return true;
}

} // namespace

QList<DialCode> dialCodes()
{
    return {
        {QStringLiteral("4636"), QStringLiteral("*#*#4636#*#*"),
         QStringLiteral("手机信息 / 电池 / 网络"), QStringLiteral("AOSP 测试菜单，多数 Android 可用")},
        {QStringLiteral("06"), QStringLiteral("*#06#"), QStringLiteral("显示 IMEI"),
         QStringLiteral("所有手机通用，只读")},
        {QStringLiteral("6484"), QStringLiteral("*#*#6484#*#*"),
         QStringLiteral("小米硬件检测（CIT）"), QStringLiteral("小米/Redmi/POCO，部分系统版本移除")},
        {QStringLiteral("3646633"), QStringLiteral("*#*#3646633#*#*"),
         QStringLiteral("联发科工程模式"), QStringLiteral("MTK 机型，部分厂商会屏蔽")},
        {QStringLiteral("7378423"), QStringLiteral("*#*#7378423#*#*"),
         QStringLiteral("Sony 服务菜单"), QStringLiteral("Sony Xperia")},
        {QStringLiteral("2664"), QStringLiteral("*#*#2664#*#*"),
         QStringLiteral("HTC 触摸屏测试"), QStringLiteral("HTC（较老机型）")},
        {QStringLiteral("225"), QStringLiteral("*#*#225#*#*"),
         QStringLiteral("版本信息 / 日历"), QStringLiteral("部分厂商 ROM 可用")},
        {QStringLiteral("001"), QStringLiteral("*#*#001#*#*"),
         QStringLiteral("工厂测试入口（部分机型）"), QStringLiteral("不同厂商差异很大，可能无效")},
    };
}

QList<EngineeringActivity> engineeringActivities()
{
    return {
        {QStringLiteral("testing-settings"), QStringLiteral("AOSP 测试设置"),
         QStringLiteral("com.android.settings/.TestingSettings"),
         QStringLiteral("原生 Android 测试菜单，部分 ROM 保留")},
        {QStringLiteral("dev-settings"), QStringLiteral("开发者选项"),
         QStringLiteral("com.android.settings/.Settings$DevelopmentSettingsDashboardActivity"),
         QStringLiteral("通用（设置里已开启开发者选项时可用）")},
        {QStringLiteral("radio-info"), QStringLiteral("网络 / 无线信息"),
         QStringLiteral("com.android.settings/.RadioInfo"),
         QStringLiteral("AOSP 工程信息，含网络类型/频段")},
        {QStringLiteral("mtk-engineer"), QStringLiteral("联发科工程模式"),
         QStringLiteral("com.mediatek.engineermode/.EngineerMode"),
         QStringLiteral("MTK 平台，部分厂商移除")},
        {QStringLiteral("mtk-engineer-sim"), QStringLiteral("联发科 SIM 工具"),
         QStringLiteral("com.mediatek.engineermode/.EngineerModeSim"),
         QStringLiteral("MTK 平台，部分厂商移除")},
        {QStringLiteral("miui-cit"), QStringLiteral("小米 CIT 测试"),
         QStringLiteral("com.miui.cit/.MainActivity"),
         QStringLiteral("小米/Redmi，部分系统版本组件名不同")},
        {QStringLiteral("oppo-engineer"), QStringLiteral("OPPO 工程模式"),
         QStringLiteral("com.oppo.engineermode/.EngineerModeActivity"),
         QStringLiteral("OPPO/一加部分机型")},
    };
}

const DialCode* findDialCode(const QString& id)
{
    static const QList<DialCode> codes = dialCodes();
    for (const DialCode& code : codes) {
        if (code.id.compare(id, Qt::CaseInsensitive) == 0
            || code.code == id)
            return &code;
    }
    return nullptr;
}

const EngineeringActivity* findEngineeringActivity(const QString& id)
{
    static const QList<EngineeringActivity> activities = engineeringActivities();
    for (const EngineeringActivity& activity : activities) {
        if (activity.id.compare(id, Qt::CaseInsensitive) == 0
            || activity.component == id)
            return &activity;
    }
    return nullptr;
}

bool isValidDialCode(const QString& code)
{
    const QString text = code.trimmed();
    if (text.isEmpty() || text.size() > 64)
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit() && ch != QLatin1Char('*') && ch != QLatin1Char('#'))
            return false;
    }
    return true;
}

bool isValidComponent(const QString& component)
{
    return validComponentText(component);
}

QString buildDialIntentCommand(const QString& code)
{
    if (!isValidDialCode(code))
        fail(QStringLiteral("拨号指令只能包含数字、*、#：%1").arg(code));
    return QStringLiteral("am start -a android.intent.action.DIAL -d %1")
        .arg(shellQuote(QStringLiteral("tel:") + code.trimmed()));
}

QString buildActivityStartCommand(const QString& component)
{
    if (!isValidComponent(component))
        fail(QStringLiteral("Activity 需要写成 package/activity 形式：%1").arg(component));
    return QStringLiteral("am start -n %1").arg(shellQuote(component.trimmed()));
}

FactoryInfo factoryInfoFromProps(const QMap<QString, QString>& props)
{
    FactoryInfo info;
    info.model = props.value(QStringLiteral("ro.product.model"));
    info.manufacturer = props.value(QStringLiteral("ro.product.manufacturer"));
    info.brand = props.value(QStringLiteral("ro.product.brand"));
    info.device = props.value(QStringLiteral("ro.product.device"));
    info.productName = props.value(QStringLiteral("ro.product.name"));
    info.hardware = props.value(QStringLiteral("ro.hardware"));
    if (info.hardware.isEmpty())
        info.hardware = props.value(QStringLiteral("ro.boot.hardware"));
    info.board = props.value(QStringLiteral("ro.board.platform"));
    if (info.board.isEmpty())
        info.board = props.value(QStringLiteral("ro.boot.hardware.platform"));
    info.bootloader = props.value(QStringLiteral("ro.bootloader"));
    if (info.bootloader.isEmpty())
        info.bootloader = props.value(QStringLiteral("ro.boot.bootloader"));
    info.baseband = props.value(QStringLiteral("gsm.version.baseband"));
    if (info.baseband.isEmpty())
        info.baseband = props.value(QStringLiteral("ro.boot.baseband"));
    info.serialno = props.value(QStringLiteral("ro.serialno"));
    if (info.serialno.isEmpty())
        info.serialno = props.value(QStringLiteral("ro.boot.serialno"));
    info.buildId = props.value(QStringLiteral("ro.build.id"));
    info.buildDate = props.value(QStringLiteral("ro.build.date"));
    info.buildFingerprint = props.value(QStringLiteral("ro.build.fingerprint"));
    info.androidVersion = props.value(QStringLiteral("ro.build.version.release"));
    info.sdk = props.value(QStringLiteral("ro.build.version.sdk"));
    info.cpuAbi = props.value(QStringLiteral("ro.product.cpu.abi"));
    info.securityPatch = props.value(QStringLiteral("ro.build.version.security_patch"));
    return info;
}

QString factoryInfoText(const FactoryInfo& info)
{
    const auto line = [](const char* label, const QString& value) {
        return QStringLiteral("%1: %2").arg(QString::fromUtf8(label),
                                            value.isEmpty() ? QStringLiteral("未知") : value);
    };
    QStringList lines;
    lines.append(line("型号", info.model));
    lines.append(line("厂商", info.manufacturer));
    lines.append(line("品牌", info.brand));
    lines.append(line("设备代号", info.device));
    lines.append(line("产品名", info.productName));
    lines.append(line("硬件平台", info.hardware));
    lines.append(line("芯片平台", info.board));
    lines.append(line("Bootloader", info.bootloader));
    lines.append(line("基带", info.baseband));
    lines.append(line("序列号", info.serialno));
    lines.append(line("Build ID", info.buildId));
    lines.append(line("构建时间", info.buildDate));
    lines.append(line("Android", info.androidVersion));
    lines.append(line("SDK", info.sdk));
    lines.append(line("CPU ABI", info.cpuAbi));
    lines.append(line("安全补丁", info.securityPatch));
    lines.append(line("指纹", info.buildFingerprint));
    return lines.join(QLatin1Char('\n'));
}

} // namespace gf::extras::bypass
