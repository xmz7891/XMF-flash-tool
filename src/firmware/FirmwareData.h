#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace gf::firmware {

/// 一条可下载的固件发布记录（来自公开数据集或 OTA 接口）。
struct FirmwareRelease {
    QString codename;
    QString name;     // 机型名称（可能包含多个市场名）
    QString version;  // 版本号，例如 V14.0.5.0.TKCNX
    QString branch;   // Stable / Weekly / ...
    QString android;
    QString method;   // Recovery / Fastboot
    QString date;
    QString link;
    QString md5;
    QString sizeText;
    qint64 sizeBytes = -1;

    QVariantMap toVariant() const;
};

/// 品牌官方固件入口（离线数据库）。
struct BrandEntry {
    QString id;
    QString name;
    QString subtitle;
    QVector<QVariantMap> official; // { title, url }
    QStringList notes;
    QString searchHint;
};

/// 机型数据库条目（离线）。
struct ModelEntry {
    QString codename;
    QString brand;
    QString series;
    QStringList names;
};

// ---------------------------------------------------------------------------
// 解析器（纯函数，便于单元测试）
// ---------------------------------------------------------------------------

/// 解析 XiaomiFirmwareUpdater 的 data/latest.yml（扁平的 "key: value" 列表）。
QVector<FirmwareRelease> parseLatestYaml(const QByteArray& data, QString* error = nullptr);
/// 解析 data/devices.yml（codename -> 机型名称列表）。
QHash<QString, QStringList> parseDevicesYaml(const QByteArray& data);
/// 解析离线品牌数据库 brands.json。
QVector<BrandEntry> parseBrandsJson(const QByteArray& data, QString* error = nullptr);
/// 解析离线机型数据库 models.json。
QVector<ModelEntry> parseModelsJson(const QByteArray& data, QString* error = nullptr);
/// 读取默认数据库文件（data/firmware/*.json）。
QVector<BrandEntry> loadOfflineBrands(QString* error = nullptr);
QVector<ModelEntry> loadOfflineModels(QString* error = nullptr);

// ---------------------------------------------------------------------------
// 检索与匹配
// ---------------------------------------------------------------------------

QString normalizeQuery(const QString& text);
/// 机型/代号 -> 代号；找不到时返回原始查询串的大写形式。
QString resolveCodename(const QString& query, const QHash<QString, QStringList>& devices);
/// 在设备表中按机型名/代号模糊搜索，返回 codename 列表。
QStringList searchDeviceCodenames(const QHash<QString, QStringList>& devices, const QString& query,
                                  int limit = 20);
/// 过滤发布记录：query 匹配机型名或代号，branch/method 为空表示不过滤。
QVector<FirmwareRelease> filterReleases(const QVector<FirmwareRelease>& releases, const QString& query,
                                        const QString& branch = {}, const QString& method = {},
                                        int limit = 50, bool newestFirst = true);
/// 在机型数据库中匹配（用于识别固件文件对应的机型）。
QVector<ModelEntry> matchModels(const QVector<ModelEntry>& models, const QString& text);
/// 从固件文件名中提取代号（例如 miui_UMI_V12.5.8.0_...zip -> UMI -> 匹配 umi）。
QString codenameFromFileName(const QString& fileName, const QVector<ModelEntry>& models,
                             const QHash<QString, QStringList>& devices = {});

QString branchText(const QString& branch);
QString releaseSummary(const FirmwareRelease& release, bool includeLink = true);
QString brandDisplayName(const QString& id);

/// 内置的公开数据集地址（XiaomiFirmwareUpdater，公开静态数据，无需登录）。
QString defaultLatestDatasetUrl();
QString defaultDevicesDatasetUrl();

} // namespace gf::firmware
