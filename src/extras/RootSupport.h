#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace gf::extras::root {

/// Root 方案（按优先级判断，证据可以同时存在）。
enum class RootEngine {
    None = 0,
    Magisk,
    KernelSU,
    APatch,
    Other,
};

QString engineId(RootEngine engine);
QString engineName(RootEngine engine);

struct RootStatus {
    bool rooted = false;
    RootEngine engine = RootEngine::None;
    QString version;
    QString suPath;
    QString suIdOutput;
    QStringList evidence;
    QMap<QString, QString> props;

    bool verifiedBootLocked = false;
    bool debuggable = false;
    QString buildType;
    QString verifiedBootState;
    QString model;

    QString summary() const;
    QVariantMap toVariant() const;
};

/// 纯函数：根据 getprop、探测到的路径、su 路径/输出与版本输出判断 Root 状态。
RootStatus evaluate(const QMap<QString, QString>& props, const QStringList& foundPaths,
                    const QString& suPath, const QString& suIdOutput,
                    const QString& versionOutput);

/// 通过 ADB 实际探测（需要在线设备）。失败时返回 status.rooted=false 并写 error。
RootStatus detect(const QString& serial, QString* error = nullptr);

// --- APatch kernel inspection ------------------------------------------------

struct KernelImageInfo {
    bool ok = false;
    QString error;
    QString imageKind;       // boot / init_boot / vendor_boot
    int headerVersion = 0;
    qint64 kernelSize = 0;
    qint64 ramdiskSize = 0;
    QString kernelCompression; // raw / gzip / lz4 / zstd / ...
    QString kernelVersion;     // "Linux version ..." banner when present
    QStringList notes;
};

/// Parses an Android boot/init_boot/vendor_boot image and collects the info the
/// APatch workflow needs (kernel payload, compression, kernel version).
KernelImageInfo inspectKernelImage(const QByteArray& imageData);

// --- Magisk 模块 ------------------------------------------------------------

struct MagiskModule {
    QString id;
    QString name;
    QString version;
    QString description;
    bool disabled = false;       // 存在 disable 标志
    bool removePending = false;  // 存在 remove 标志
    bool updatePending = false;  // 存在 update 标志

    QVariantMap toVariant() const;
};

/// 解析 `ls -1 /data/adb/modules` 的输出（目录名即模块 id）。
QStringList parseModuleIds(const QString& listingOutput);
/// 解析模块目录下的文件列表，得到 disable/remove/update 标志。
MagiskModule parseModuleFlags(const QString& moduleId, const QString& flagListingOutput);
/// 解析 module.prop（key=value）。
void applyModuleProp(MagiskModule& module, const QString& propText);

QList<MagiskModule> readModules(const QString& serial, QString* error = nullptr);

QString moduleDirectory(const QString& moduleId);
/// 添加 / 删除模块目录中的标志文件（disable / remove / update）。
bool setModuleFlag(const QString& serial, const QString& moduleId, const QString& flag,
                   bool enabled, QString* error = nullptr);

} // namespace gf::extras::root
