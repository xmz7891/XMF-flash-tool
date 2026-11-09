#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace gf::extras::bootloader {

/// fastboot getvar all 解析出的引导/安全状态。
struct SlotState {
    QString slot;              // "a" / "b"
    bool successful = false;   // slot-successful
    bool unbootable = false;   // slot-unbootable
    int retryCount = 0;        // slot-retry-count
    bool present = false;

    QVariantMap toVariant() const;
};

struct BootloaderInfo {
    QString versionBootloader;
    QString product;
    QString serialno;
    QString currentSlot;
    int slotCount = 1;
    QString baseband;
    QString hardware;
    QString unlockedRaw;
    QString secureRaw;
    QString verityRaw;
    QString verityMode;
    QString avbState;           // verified boot 状态（如有）
    bool unlocked = false;
    bool secure = false;
    bool verityEnabled = false;
    QList<SlotState> slotStates;

    QString summary() const;
    QVariantMap toVariant() const;
};

/// 从 `fastboot getvar all` 的键值表解析状态（纯函数，可单测）。
BootloaderInfo parseBootloaderInfo(const QMap<QString, QString>& vars);
QList<SlotState> parseSlotStates(const QMap<QString, QString>& vars);
QString securitySummary(const BootloaderInfo& info);

bool isValidSlot(const QString& slot);
/// 生成 `--set-active=a` 参数。
QString activeSlotArgument(const QString& slot);
/// 可读的 bootloader 变量白名单（只读 getvar 时用于提示）。
QStringList knownReadVariables();

} // namespace gf::extras::bootloader
