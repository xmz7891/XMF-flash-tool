#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace gf::extras::bypass {

/// 常见工程/测试模式拨号指令。
struct DialCode {
    QString id;      // 菜单/表单里的稳定 id，例如 "4636"
    QString code;    // 实际拨号内容，例如 "*#*#4636#*#*"
    QString name;    // 中文名称
    QString note;    // 适用机型/注意事项
};

/// 常见工程模式 Activity（不同系统版本可能不存在）。
struct EngineeringActivity {
    QString id;
    QString name;
    QString component;  // "package/activity"
    QString note;
};

struct FactoryInfo {
    QString model;
    QString manufacturer;
    QString brand;
    QString device;
    QString productName;
    QString hardware;
    QString board;
    QString bootloader;
    QString baseband;
    QString serialno;
    QString buildId;
    QString buildDate;
    QString buildFingerprint;
    QString androidVersion;
    QString sdk;
    QString cpuAbi;
    QString securityPatch;
};

QList<DialCode> dialCodes();
QList<EngineeringActivity> engineeringActivities();

const DialCode* findDialCode(const QString& id);
const EngineeringActivity* findEngineeringActivity(const QString& id);

bool isValidDialCode(const QString& code);
bool isValidComponent(const QString& component);

/// 生成在设备端执行的拨号命令（`am start ... DIAL -d 'tel:...'`）。
QString buildDialIntentCommand(const QString& code);
/// 生成启动工程 Activity 的命令（`am start -n pkg/activity`）。
QString buildActivityStartCommand(const QString& component);

FactoryInfo factoryInfoFromProps(const QMap<QString, QString>& props);
QString factoryInfoText(const FactoryInfo& info);

} // namespace gf::extras::bypass
