#include "BootloaderSupport.h"

#include <algorithm>

#include "ExtrasUtils.h"

namespace gf::extras::bootloader {

namespace {

bool truthy(const QString& value)
{
    const QString text = value.trimmed().toLower();
    return text == QLatin1String("yes") || text == QLatin1String("true")
           || text == QLatin1String("1") || text == QLatin1String("unlocked")
           || text == QLatin1String("on");
}

QString slotSuffix(const QString& key, const QString& prefix)
{
    QString suffix = key.mid(prefix.size()).trimmed();
    while (suffix.startsWith(QLatin1Char('_')))
        suffix.remove(0, 1);
    while (suffix.startsWith(QLatin1Char(':')))
        suffix.remove(0, 1);
    return suffix;
}

} // namespace

QVariantMap SlotState::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("slot"), slot);
    map.insert(QStringLiteral("successful"), successful);
    map.insert(QStringLiteral("unbootable"), unbootable);
    map.insert(QStringLiteral("retryCount"), retryCount);
    return map;
}

QList<SlotState> parseSlotStates(const QMap<QString, QString>& vars)
{
    QMap<QString, SlotState> states;
    auto touch = [&states](const QString& slot) -> SlotState& {
        SlotState& state = states[slot];
        state.slot = slot;
        state.present = true;
        return state;
    };
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        const QString& key = it.key();
        const QString& value = it.value().trimmed();
        if (key.startsWith(QStringLiteral("slot-successful:"))) {
            touch(slotSuffix(key, QStringLiteral("slot-successful:"))).successful = truthy(value);
        } else if (key.startsWith(QStringLiteral("slot-unbootable:"))) {
            touch(slotSuffix(key, QStringLiteral("slot-unbootable:"))).unbootable = truthy(value);
        } else if (key.startsWith(QStringLiteral("slot-retry-count:"))) {
            bool ok = false;
            const int count = value.toInt(&ok);
            if (ok)
                touch(slotSuffix(key, QStringLiteral("slot-retry-count:"))).retryCount = count;
        }
    }
    const QString current = vars.value(QStringLiteral("current-slot")).trimmed();
    if (!current.isEmpty())
        touch(current);

    QList<SlotState> result;
    for (auto it = states.constBegin(); it != states.constEnd(); ++it)
        result.append(it.value());
    std::sort(result.begin(), result.end(),
              [](const SlotState& a, const SlotState& b) { return a.slot < b.slot; });
    return result;
}

BootloaderInfo parseBootloaderInfo(const QMap<QString, QString>& vars)
{
    BootloaderInfo info;
    info.versionBootloader = vars.value(QStringLiteral("version-bootloader"));
    if (info.versionBootloader.isEmpty())
        info.versionBootloader = vars.value(QStringLiteral("version-bootloader"));
    info.product = vars.value(QStringLiteral("product"));
    info.serialno = vars.value(QStringLiteral("serialno"));
    info.currentSlot = vars.value(QStringLiteral("current-slot")).trimmed();
    info.baseband = vars.value(QStringLiteral("baseband"));
    if (info.baseband.isEmpty())
        info.baseband = vars.value(QStringLiteral("version-baseband"));
    info.hardware = vars.value(QStringLiteral("hw-revision"));
    if (info.hardware.isEmpty())
        info.hardware = vars.value(QStringLiteral("variant"));

    bool slotOk = false;
    const int slotCount = vars.value(QStringLiteral("slot-count")).toInt(&slotOk);
    if (slotOk && slotCount > 0)
        info.slotCount = slotCount;
    else if (!info.currentSlot.isEmpty())
        info.slotCount = 2;

    info.unlockedRaw = vars.value(QStringLiteral("unlocked"));
    info.secureRaw = vars.value(QStringLiteral("secure"));
    info.verityRaw = vars.value(QStringLiteral("verity"));
    info.verityMode = vars.value(QStringLiteral("veritymode"));
    info.avbState = vars.value(QStringLiteral("ro.boot.verifiedbootstate"));
    if (info.avbState.isEmpty())
        info.avbState = vars.value(QStringLiteral("verified"));

    info.unlocked = truthy(info.unlockedRaw);
    info.secure = truthy(info.secureRaw);
    const QString verity = info.verityRaw.trimmed().toLower();
    info.verityEnabled = !verity.isEmpty() && verity != QLatin1String("disabled")
                         && verity != QLatin1String("false") && verity != QLatin1String("0")
                         && !verity.contains(QStringLiteral("disable"));

    info.slotStates = parseSlotStates(vars);
    return info;
}

QString securitySummary(const BootloaderInfo& info)
{
    QStringList lines;
    lines.append(QStringLiteral("Bootloader：%1")
                     .arg(info.versionBootloader.isEmpty() ? QStringLiteral("未知")
                                                           : info.versionBootloader));
    if (!info.product.isEmpty())
        lines.append(QStringLiteral("产品：%1").arg(info.product));
    if (!info.serialno.isEmpty())
        lines.append(QStringLiteral("序列号：%1").arg(info.serialno));
    lines.append(QStringLiteral("解锁状态：%1%2")
                     .arg(info.unlocked ? QStringLiteral("已解锁") : QStringLiteral("未解锁"),
                          info.unlockedRaw.isEmpty()
                              ? QString()
                              : QStringLiteral("（unlocked=%1）").arg(info.unlockedRaw)));
    lines.append(QStringLiteral("Secure Boot：%1，dm-verity：%2，verity mode：%3")
                     .arg(info.secure ? QStringLiteral("开启") : QStringLiteral("关闭"),
                          info.verityEnabled ? QStringLiteral("开启") : QStringLiteral("关闭"),
                          info.verityMode.isEmpty() ? QStringLiteral("未知") : info.verityMode));
    if (!info.avbState.isEmpty())
        lines.append(QStringLiteral("Verified Boot 状态：%1").arg(info.avbState));
    if (info.slotCount > 1) {
        lines.append(QStringLiteral("A/B 槽位：共 %1 个，当前 %2")
                         .arg(info.slotCount)
                         .arg(info.currentSlot.isEmpty() ? QStringLiteral("未知")
                                                         : info.currentSlot));
        for (const SlotState& slot : info.slotStates) {
            lines.append(QStringLiteral("  槽位 %1：%2，%3，剩余重试 %4")
                             .arg(slot.slot,
                                  slot.successful ? QStringLiteral("有效")
                                                  : QStringLiteral("未标记有效"),
                                  slot.unbootable ? QStringLiteral("不可启动")
                                                  : QStringLiteral("可启动"),
                                  QString::number(slot.retryCount)));
        }
    } else {
        lines.append(QStringLiteral("单槽位设备（无 A/B 切换）"));
    }
    lines.append(QStringLiteral("提示：解锁/回锁 bootloader 会清除用户数据，请确认设备为本人所有。"));
    return lines.join(QLatin1Char('\n'));
}

QString BootloaderInfo::summary() const
{
    return securitySummary(*this);
}

QVariantMap BootloaderInfo::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("versionBootloader"), versionBootloader);
    map.insert(QStringLiteral("product"), product);
    map.insert(QStringLiteral("serialno"), serialno);
    map.insert(QStringLiteral("currentSlot"), currentSlot);
    map.insert(QStringLiteral("slotCount"), slotCount);
    map.insert(QStringLiteral("unlocked"), unlocked);
    map.insert(QStringLiteral("unlockedRaw"), unlockedRaw);
    map.insert(QStringLiteral("secure"), secure);
    map.insert(QStringLiteral("verityEnabled"), verityEnabled);
    map.insert(QStringLiteral("verityMode"), verityMode);
    map.insert(QStringLiteral("avbState"), avbState);
    QVariantList slotList;
    for (const SlotState& slot : slotStates)
        slotList.append(slot.toVariant());
    map.insert(QStringLiteral("slotStates"), slotList);
    return map;
}

bool isValidSlot(const QString& slot)
{
    const QString id = slot.trimmed().toLower();
    return id == QLatin1String("a") || id == QLatin1String("b");
}

QString activeSlotArgument(const QString& slot)
{
    return QStringLiteral("--set-active=") + slot.trimmed().toLower();
}

QStringList knownReadVariables()
{
    return {
        QStringLiteral("version"),          QStringLiteral("version-bootloader"),
        QStringLiteral("product"),          QStringLiteral("serialno"),
        QStringLiteral("current-slot"),     QStringLiteral("slot-count"),
        QStringLiteral("unlocked"),         QStringLiteral("secure"),
        QStringLiteral("verity"),           QStringLiteral("veritymode"),
        QStringLiteral("battery-voltage"),  QStringLiteral("hw-revision"),
        QStringLiteral("variant"),          QStringLiteral("baseband"),
        QStringLiteral("max-download-size"),QStringLiteral("all"),
    };
}

} // namespace gf::extras::bootloader
