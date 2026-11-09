#include "FirmwareNet.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

namespace gf::firmware {

namespace {

constexpr const char* kUserAgent = "XMF-Flash-Tool/0.1 (firmware search; +https://xmf.flash)";

void applyRequestHeaders(QNetworkRequest& request)
{
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
}

/// 运行事件循环直到 reply 结束；取消回调抛出的异常会被转换为 abort。
/// 返回 true 表示用户取消，调用方应重新抛出取消异常。
bool waitForReply(QNetworkReply* reply, int timeoutMs, const CancelFn& cancel)
{
    QEventLoop loop;
    bool canceled = false;
    QTimer timer;
    timer.setInterval(100);
    QObject::connect(&timer, &QTimer::timeout, reply, [&] {
        if (!cancel)
            return;
        try {
            cancel();
        } catch (...) {
            canceled = true;
            reply->abort();
            loop.quit();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start();
    if (timeoutMs > 0)
        QTimer::singleShot(timeoutMs, &loop, [&] {
            if (reply->isRunning()) {
                reply->abort();
                loop.quit();
            }
        });
    if (!reply->isFinished())
        loop.exec();
    return canceled;
}

QString networkErrorText(QNetworkReply* reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400)
        return QStringLiteral("HTTP %1（%2）").arg(status).arg(reply->errorString());
    return reply->errorString();
}

} // namespace

HttpResult httpGet(const QString& url, int timeoutMs, const CancelFn& cancel)
{
    HttpResult result;
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    applyRequestHeaders(request);
    if (timeoutMs > 0)
        request.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = manager.get(request);
    const bool canceled = waitForReply(reply, timeoutMs, cancel);
    if (canceled) {
        reply->deleteLater();
        if (cancel)
            cancel();
    }
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.finalUrl = reply->url().toString();
    if (reply->error() != QNetworkReply::NoError) {
        result.error = networkErrorText(reply);
    } else if (result.status < 200 || result.status >= 300) {
        result.error = QStringLiteral("HTTP %1").arg(result.status);
    } else {
        result.ok = true;
    }
    reply->deleteLater();
    return result;
}

HttpResult httpPost(const QString& url, const QByteArray& body, const QString& contentType,
                    int timeoutMs, const CancelFn& cancel)
{
    HttpResult result;
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    applyRequestHeaders(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (timeoutMs > 0)
        request.setTransferTimeout(timeoutMs);
    QNetworkReply* reply = manager.post(request, body);
    const bool canceled = waitForReply(reply, timeoutMs, cancel);
    if (canceled) {
        reply->deleteLater();
        if (cancel)
            cancel();
    }
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.finalUrl = reply->url().toString();
    if (reply->error() != QNetworkReply::NoError) {
        result.error = networkErrorText(reply);
    } else if (result.status < 200 || result.status >= 300) {
        result.error = QStringLiteral("HTTP %1").arg(result.status);
    } else {
        result.ok = true;
    }
    reply->deleteLater();
    return result;
}

DownloadResult downloadFile(const QString& url, const QString& outputPath, bool resume,
                            const DownloadProgressFn& progress, const CancelFn& cancel,
                            int timeoutMs)
{
    DownloadResult result;
    result.path = outputPath;
    const QFileInfo target(outputPath);
    if (!ensureDirectory(target.absolutePath())) {
        result.error = QStringLiteral("无法创建下载目录：%1").arg(target.absolutePath());
        return result;
    }

    qint64 existing = 0;
    if (resume && target.exists() && target.isFile())
        existing = target.size();

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    applyRequestHeaders(request);
    if (timeoutMs > 0)
        request.setTransferTimeout(timeoutMs);
    if (existing > 0)
        request.setRawHeader("Range", "bytes=" + QByteArray::number(existing) + "-");

    QNetworkReply* reply = manager.get(request);

    QFile file;
    bool decided = false;
    bool openFailed = false;
    qint64 base = 0;
    qint64 contentLength = -1;
    qint64 received = 0;

    auto decide = [&]() -> bool {
        if (decided)
            return true;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 && status != 206)
            return false;
        const bool append = existing > 0 && status == 206;
        base = append ? existing : 0;
        const QIODevice::OpenMode mode =
            append ? (QIODevice::WriteOnly | QIODevice::Append) : (QIODevice::WriteOnly | QIODevice::Truncate);
        if (!file.open(mode)) {
            openFailed = true;
            reply->abort();
            return false;
        }
        const QVariant lengthHeader = reply->header(QNetworkRequest::ContentLengthHeader);
        const qint64 length = lengthHeader.isValid() ? lengthHeader.toLongLong() : -1;
        contentLength = length >= 0 ? length : -1;
        received = base;
        result.status = status;
        result.resumed = append;
        if (progress) {
            progress(received, contentLength >= 0 ? base + contentLength : -1);
        }
        decided = true;
        return true;
    };

    auto drain = [&]() {
        if (!decide()) {
            reply->readAll();
            return;
        }
        const QByteArray chunk = reply->readAll();
        if (!chunk.isEmpty() && file.isOpen()) {
            file.write(chunk);
            received += chunk.size();
            if (progress)
                progress(received, contentLength >= 0 ? base + contentLength : -1);
        }
    };

    QObject::connect(reply, &QNetworkReply::readyRead, reply, drain);
    QEventLoop loop;
    bool canceled = false;
    QTimer timer;
    timer.setInterval(100);
    QObject::connect(&timer, &QTimer::timeout, reply, [&] {
        if (!cancel)
            return;
        try {
            cancel();
        } catch (...) {
            canceled = true;
            reply->abort();
            loop.quit();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start();
    if (timeoutMs > 0)
        QTimer::singleShot(timeoutMs, &loop, [&] {
            if (reply->isRunning()) {
                reply->abort();
                loop.quit();
            }
        });
    if (!reply->isFinished())
        loop.exec();
    drain();
    if (canceled) {
        if (file.isOpen())
            file.close();
        reply->deleteLater();
        if (cancel)
            cancel();
    }

    if (openFailed) {
        result.error = QStringLiteral("无法写入文件：%1").arg(outputPath);
        reply->deleteLater();
        return result;
    }
    if (!decided) {
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.error = networkErrorText(reply);
        if (file.isOpen())
            file.close();
        reply->deleteLater();
        return result;
    }
    if (file.isOpen())
        file.close();

    if (reply->error() != QNetworkReply::NoError) {
        result.error = networkErrorText(reply);
        result.bytes = received;
        reply->deleteLater();
        return result;
    }
    const qint64 expected = contentLength >= 0 ? base + contentLength : -1;
    if (expected >= 0 && received != expected) {
        result.error = QStringLiteral("下载不完整：收到 %1 / 期望 %2 字节")
                           .arg(received)
                           .arg(expected);
        result.bytes = received;
        reply->deleteLater();
        return result;
    }
    result.ok = true;
    result.bytes = received;
    result.totalBytes = expected;
    reply->deleteLater();
    return result;
}

QString miuiOtaEndpoint()
{
    return QStringLiteral("https://update.miui.com/updates/miotaV3.php");
}

QByteArray buildMiuiOtaRequest(const QString& codename, const QString& branch, const QString& version,
                               const QString& locale)
{
    QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mi_ota_request>\n"
                                 "  <header>\n    <params>\n");
    xml += QStringLiteral("      <locale>%1</locale>\n").arg(locale.isEmpty() ? QStringLiteral("zh_CN") : locale);
    xml += QStringLiteral("      <device>%1</device>\n").arg(codename.trimmed());
    xml += QStringLiteral("      <mode>miui</mode>\n");
    if (!branch.trimmed().isEmpty())
        xml += QStringLiteral("      <branch>%1</branch>\n").arg(branch.trimmed().toLower());
    if (!version.trimmed().isEmpty()) {
        xml += QStringLiteral("      <version>%1</version>\n").arg(version.trimmed());
        xml += QStringLiteral("      <rom_version>%1</rom_version>\n").arg(version.trimmed());
    }
    xml += QStringLiteral("    </params>\n  </header>\n</mi_ota_request>\n");
    return xml.toUtf8();
}

QVector<FirmwareRelease> parseMiuiOtaResponse(const QByteArray& xml, const QString& codenameHint)
{
    QVector<FirmwareRelease> releases;
    QXmlStreamReader reader(xml);
    QHash<QString, QString> fields;
    bool inUpdate = false;
    QString currentElement;

    auto flush = [&] {
        if (fields.isEmpty())
            return;
        FirmwareRelease release;
        release.codename = codenameHint;
        release.version = fields.value(QStringLiteral("version"));
        release.link = fields.value(QStringLiteral("link"));
        if (release.link.isEmpty())
            release.link = fields.value(QStringLiteral("url"));
        if (release.link.isEmpty())
            release.link = fields.value(QStringLiteral("updateurl"));
        release.md5 = fields.value(QStringLiteral("md5"));
        if (release.md5.isEmpty())
            release.md5 = fields.value(QStringLiteral("hash"));
        release.android = fields.value(QStringLiteral("android"));
        release.branch = fields.value(QStringLiteral("branch"));
        release.method = fields.value(QStringLiteral("method"));
        release.sizeText = fields.value(QStringLiteral("size"));
        release.sizeBytes = sizeTextToBytes(release.sizeText);
        release.date = fields.value(QStringLiteral("date"));
        if (!release.link.isEmpty() && !release.version.isEmpty())
            releases.append(release);
        fields.clear();
    };

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString().toLower();
            if (name == QStringLiteral("update") || name == QStringLiteral("item") ||
                name == QStringLiteral("release") || name == QStringLiteral("updatelist")) {
                flush();
                inUpdate = true;
                continue;
            }
            if (inUpdate && (name == QStringLiteral("version") || name == QStringLiteral("link") ||
                             name == QStringLiteral("url") || name == QStringLiteral("updateurl") ||
                             name == QStringLiteral("md5") || name == QStringLiteral("hash") ||
                             name == QStringLiteral("android") || name == QStringLiteral("branch") ||
                             name == QStringLiteral("method") || name == QStringLiteral("size") ||
                             name == QStringLiteral("date"))) {
                currentElement = name;
                fields.insert(name, reader.readElementText().trimmed());
                currentElement.clear();
                continue;
            }
        } else if (reader.isEndElement()) {
            const QString name = reader.name().toString().toLower();
            if (name == QStringLiteral("update") || name == QStringLiteral("item") ||
                name == QStringLiteral("release") || name == QStringLiteral("updatelist")) {
                flush();
                inUpdate = false;
            }
        }
    }
    flush();
    return releases;
}

namespace {

QString cachePath(const QString& name)
{
    return QDir(cacheDir()).filePath(name);
}

bool cacheFresh(const QString& path, int maxAgeSeconds)
{
    const QFileInfo info(path);
    if (!info.exists())
        return false;
    const QDateTime modified = info.lastModified();
    if (!modified.isValid())
        return true;
    return modified.secsTo(QDateTime::currentDateTimeUtc()) < maxAgeSeconds;
}

} // namespace

DatasetResult fetchXiaomiDataset(bool forceRefresh, int timeoutMs, const CancelFn& cancel,
                                 const ProgressFn& progress)
{
    DatasetResult result;
    const QString latestPath = cachePath(QStringLiteral("latest.yml"));
    const QString devicesPath = cachePath(QStringLiteral("devices.yml"));
    const bool cached = cacheFresh(latestPath, 12 * 60 * 60) &&
                        cacheFresh(devicesPath, 12 * 60 * 60);

    auto loadCache = [&]() -> bool {
        const QByteArray latest = readFile(latestPath);
        if (latest.isEmpty())
            return false;
        QString error;
        const QVector<FirmwareRelease> releases = parseLatestYaml(latest, &error);
        if (releases.isEmpty())
            return false;
        result.releases = releases;
        result.devices = parseDevicesYaml(readFile(devicesPath));
        result.ok = true;
        result.source = QStringLiteral("cache");
        return true;
    };

    if (!forceRefresh && cached && loadCache())
        return result;

    reportProgress(progress, 5, QStringLiteral("下载小米机型数据集"));
    const HttpResult devicesResponse = httpGet(defaultDevicesDatasetUrl(), timeoutMs, cancel);
    reportProgress(progress, 25, QStringLiteral("下载小米固件数据集（约 1.2MB）"));
    const HttpResult latestResponse = httpGet(defaultLatestDatasetUrl(), timeoutMs, cancel);

    if (!latestResponse.ok) {
        if (loadCache()) {
            result.error = QStringLiteral("网络不可用，已使用本地缓存（%1）").arg(latestResponse.error);
            return result;
        }
        result.error = latestResponse.error.isEmpty() ? QStringLiteral("数据集下载失败")
                                                      : latestResponse.error;
        return result;
    }
    QString parseError;
    result.releases = parseLatestYaml(latestResponse.body, &parseError);
    if (result.releases.isEmpty()) {
        if (loadCache()) {
            result.error = QStringLiteral("数据集解析失败，已使用本地缓存：%1").arg(parseError);
            return result;
        }
        result.error = parseError.isEmpty() ? QStringLiteral("数据集解析失败") : parseError;
        return result;
    }
    if (devicesResponse.ok)
        result.devices = parseDevicesYaml(devicesResponse.body);
    writeFile(latestPath, latestResponse.body);
    if (devicesResponse.ok)
        writeFile(devicesPath, devicesResponse.body);
    result.ok = true;
    result.source = QStringLiteral("network");
    reportProgress(progress, 60, QStringLiteral("数据集解析完成"));
    return result;
}

} // namespace gf::firmware
