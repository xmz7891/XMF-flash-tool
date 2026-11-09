#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <functional>

#include "FirmwareCommon.h"
#include "FirmwareData.h"

namespace gf::firmware {

struct HttpResult {
    bool ok = false;
    int status = 0;
    QByteArray body;
    QString error;
    QString finalUrl;
};

/// HTTP GET（小文件 / 数据集），带超时与取消。
HttpResult httpGet(const QString& url, int timeoutMs = 30000, const CancelFn& cancel = {});
/// HTTP POST（Xiaomi OTA 接口）。
HttpResult httpPost(const QString& url, const QByteArray& body, const QString& contentType,
                    int timeoutMs = 30000, const CancelFn& cancel = {});

using DownloadProgressFn = std::function<void(qint64 received, qint64 total)>;

struct DownloadResult {
    bool ok = false;
    QString error;
    QString path;
    qint64 bytes = 0;
    qint64 totalBytes = -1;
    int status = 0;
    bool resumed = false;
};

/// 下载到文件：支持断点续传（Range）、进度回调与取消。
DownloadResult downloadFile(const QString& url, const QString& outputPath, bool resume,
                            const DownloadProgressFn& progress, const CancelFn& cancel,
                            int timeoutMs = 60000);

// --- Xiaomi MIUI OTA（公开接口，实验性） ----------------------------------
QString miuiOtaEndpoint();
/// 构造 miotaV3.php 的 XML 请求体。
QByteArray buildMiuiOtaRequest(const QString& codename, const QString& branch,
                               const QString& version = {}, const QString& locale = QStringLiteral("zh_CN"));
/// 解析 OTA XML 响应中的更新条目（容错解析 <update>/<item>/<release>）。
QVector<FirmwareRelease> parseMiuiOtaResponse(const QByteArray& xml,
                                              const QString& codenameHint = {});
/// 抓取公开数据集（latest.yml + devices.yml），带本地缓存；缓存最新时直接用缓存。
struct DatasetResult {
    bool ok = false;
    QString error;
    QVector<FirmwareRelease> releases;
    QHash<QString, QStringList> devices;
    QString source; // "network" | "cache"
    QString latestUrl;
};

DatasetResult fetchXiaomiDataset(bool forceRefresh, int timeoutMs, const CancelFn& cancel = {},
                                 const ProgressFn& progress = {});

} // namespace gf::firmware
