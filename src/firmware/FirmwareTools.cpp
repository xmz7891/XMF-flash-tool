// 固件资源工具：小米/红米固件检索与下载、其他品牌官方入口、固件文件识别/解包/哈希/分卷合并。
#include "FirmwareTools.h"

#include "FirmwareCommon.h"
#include "FirmwareData.h"
#include "FirmwareFiles.h"
#include "FirmwareNet.h"

#include "ToolRegistry.h"
#include "TaskManager.h"

#include "core/Util.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <exception>

namespace gf {

namespace {

using gf::firmware::CancelFn;
using gf::firmware::ProgressFn;

QString optText(const QVariantMap& options, const QString& key)
{
    return options.value(key).toString().trimmed();
}

bool optBool(const QVariantMap& options, const QString& key, bool def = false)
{
    if (!options.contains(key))
        return def;
    return options.value(key).toBool();
}

int optInt(const QVariantMap& options, const QString& key, int def)
{
    bool ok = false;
    const int value = options.value(key).toInt(&ok);
    return ok ? value : def;
}

QString requireText(const QVariantMap& options, const QString& key, const QString& label)
{
    const QString value = optText(options, key);
    if (value.isEmpty())
        throw std::runtime_error(QStringLiteral("请填写%1").arg(label).toStdString());
    return value;
}

QStringList splitNames(const QString& text)
{
    static const QRegularExpression separator(QStringLiteral("[,\\s;]+"));
    QStringList out;
    for (const QString& part : text.split(separator, Qt::SkipEmptyParts))
        out << part;
    return out;
}

ProgressFn progressOf(TaskContext& ctx)
{
    return [&ctx](int percent, const QString& status) {
        ctx.setProgress(percent);
        if (!status.isEmpty())
            ctx.setStatus(status);
    };
}

CancelFn cancelOf(TaskContext& ctx)
{
    return [&ctx] { ctx.checkCanceled(); };
}

QString branchFilter(int index)
{
    switch (index) {
    case 1:
        return QStringLiteral("Stable");
    case 2:
        return QStringLiteral("Weekly");
    case 3:
        return QStringLiteral("Beta");
    default:
        return {};
    }
}

QString methodFilter(int index)
{
    switch (index) {
    case 1:
        return QStringLiteral("Recovery");
    case 2:
        return QStringLiteral("Fastboot");
    default:
        return {};
    }
}

QStringList branchOptions()
{
    return {QStringLiteral("全部"), QStringLiteral("稳定版 Stable"), QStringLiteral("开发版 Weekly"),
            QStringLiteral("内测版 Beta")};
}

QStringList methodOptions()
{
    return {QStringLiteral("全部"), QStringLiteral("Recovery 卡刷包"),
            QStringLiteral("Fastboot 线刷包")};
}

QStringList brandOptions()
{
    QString error;
    const QVector<gf::firmware::BrandEntry> brands = gf::firmware::loadOfflineBrands(&error);
    QStringList options;
    for (const auto& brand : brands)
        options << brand.name;
    if (options.isEmpty()) {
        options = {QStringLiteral("小米 / 红米"), QStringLiteral("OPPO"), QStringLiteral("vivo"),
                   QStringLiteral("realme"), QStringLiteral("一加"), QStringLiteral("三星"),
                   QStringLiteral("紫光展锐"), QStringLiteral("瑞芯微"), QStringLiteral("全志")};
    }
    return options;
}

QVariantMap pathField(const QString& key, const QString& label,
                      const QString& filter = QStringLiteral("所有文件 (*)"))
{
    return fields::path(key, label, filter);
}

// ---------------------------------------------------------------------------
// 小米 / 红米：数据集检索
// ---------------------------------------------------------------------------

gf::firmware::DatasetResult requireDataset(TaskContext& ctx, bool forceRefresh)
{
    ctx.info(QStringLiteral("数据源：%1")
                 .arg(gf::firmware::defaultLatestDatasetUrl()));
    gf::firmware::DatasetResult dataset =
        gf::firmware::fetchXiaomiDataset(forceRefresh, 30000, cancelOf(ctx), progressOf(ctx));
    if (!dataset.ok)
        throw std::runtime_error(
            QStringLiteral("获取小米固件数据集失败（网络不可用或数据源不可访问）：%1")
                .arg(dataset.error.isEmpty() ? QStringLiteral("未知错误") : dataset.error)
                .toStdString());
    if (!dataset.error.isEmpty())
        ctx.warn(dataset.error);
    ctx.info(QStringLiteral("数据集来源：%1，发布记录 %2 条，机型 %3 个")
                 .arg(dataset.source == QStringLiteral("cache") ? QStringLiteral("本地缓存")
                                                                : QStringLiteral("在线"),
                      QString::number(dataset.releases.size()),
                      QString::number(dataset.devices.size())));
    return dataset;
}

QVector<gf::firmware::FirmwareRelease> searchReleases(const gf::firmware::DatasetResult& dataset,
                                                      const QString& query, const QString& branch,
                                                      const QString& method, int limit)
{
    QVector<gf::firmware::FirmwareRelease> matches =
        gf::firmware::filterReleases(dataset.releases, query, branch, method, limit);
    if (matches.isEmpty() && !dataset.devices.isEmpty()) {
        const QStringList codenames = gf::firmware::searchDeviceCodenames(dataset.devices, query, 5);
        for (const QString& codename : codenames) {
            matches += gf::firmware::filterReleases(dataset.releases, codename, branch, method, limit);
            if (matches.size() >= limit)
                break;
        }
    }
    return matches;
}

void xiaomiSearch(TaskContext& ctx, const QVariantMap& options)
{
    const QString query = requireText(options, QStringLiteral("query"), QStringLiteral("机型或代号"));
    const QString branch = branchFilter(optInt(options, QStringLiteral("branch"), 0));
    const QString method = methodFilter(optInt(options, QStringLiteral("method"), 0));
    const int limit = qBound(1, optInt(options, QStringLiteral("limit"), 20), 200);
    const bool forceRefresh = optBool(options, QStringLiteral("refresh"), false);
    const gf::firmware::DatasetResult dataset = requireDataset(ctx, forceRefresh);
    const auto matches = searchReleases(dataset, query, branch, method, limit);
    if (matches.isEmpty()) {
        QStringList hints = gf::firmware::searchDeviceCodenames(dataset.devices, query, 8);
        QString hint = hints.isEmpty() ? QString()
                                       : QStringLiteral("；可能相关的代号：%1")
                                             .arg(hints.join(QStringLiteral(", ")));
        throw std::runtime_error(
            QStringLiteral("没有找到「%1」的固件记录%2").arg(query, hint).toStdString());
    }
    ctx.info(QStringLiteral("匹配到 %1 条固件（最多显示 %2 条）：").arg(matches.size()).arg(limit));
    for (int i = 0; i < matches.size(); ++i) {
        ctx.checkCanceled();
        ctx.info(QStringLiteral("[%1] %2").arg(i + 1).arg(gf::firmware::releaseSummary(matches.at(i))));
    }
    ctx.ok(QStringLiteral("检索完成：%1 条。可直接用「小米/红米固件下载」按机型下载")
               .arg(matches.size()));
}

void xiaomiDownload(TaskContext& ctx, const QVariantMap& options)
{
    const QString query = requireText(options, QStringLiteral("query"), QStringLiteral("机型或代号"));
    const QString branch = branchFilter(optInt(options, QStringLiteral("branch"), 0));
    const QString method = methodFilter(optInt(options, QStringLiteral("method"), 0));
    const QString versionText = optText(options, QStringLiteral("version")).toLower();
    const bool verify = optBool(options, QStringLiteral("verify"), true);
    const bool resume = optBool(options, QStringLiteral("resume"), true);
    const bool forceRefresh = optBool(options, QStringLiteral("refresh"), false);
    QString outputDir = optText(options, QStringLiteral("outputDir"));
    if (outputDir.isEmpty())
        outputDir = gf::firmware::downloadDir();

    const gf::firmware::DatasetResult dataset = requireDataset(ctx, forceRefresh);
    auto matches = searchReleases(dataset, query, branch, method, 200);
    if (!versionText.isEmpty()) {
        QVector<gf::firmware::FirmwareRelease> filtered;
        for (const auto& release : matches) {
            if (release.version.toLower().contains(versionText))
                filtered.append(release);
        }
        matches = filtered;
    }
    if (matches.isEmpty())
        throw std::runtime_error(
            QStringLiteral("没有找到可下载的固件：%1").arg(query).toStdString());
    const auto release = matches.first();
    ctx.info(QStringLiteral("已选择：%1").arg(gf::firmware::releaseSummary(release, false)));
    ctx.info(QStringLiteral("直链：%1").arg(release.link));
    if (!gf::firmware::ensureDirectory(outputDir))
        throw std::runtime_error(QStringLiteral("无法创建下载目录：%1").arg(outputDir).toStdString());
    QString fileName = gf::firmware::fileNameFromUrl(release.link);
    if (fileName.isEmpty()) {
        fileName = gf::firmware::sanitizeFileName(
            QStringLiteral("%1_%2.zip").arg(release.codename, release.version));
    }
    const QString outputPath = QDir(outputDir).filePath(fileName);
    if (QFileInfo::exists(outputPath) && release.sizeBytes > 0 &&
        QFileInfo(outputPath).size() == release.sizeBytes) {
        ctx.info(QStringLiteral("文件已存在且大小一致，跳过下载：%1").arg(outputPath));
        ctx.ok(QStringLiteral("固件已存在：%1").arg(outputPath));
        return;
    }
    ctx.info(QStringLiteral("开始下载到：%1（断点续传：%2）")
                 .arg(outputPath, resume ? QStringLiteral("开") : QStringLiteral("关")));
    const auto result = gf::firmware::downloadFile(
        release.link, outputPath, resume,
        [&ctx](qint64 received, qint64 total) {
            if (total > 0) {
                ctx.setProgress(int(10.0 + 85.0 * double(received) / double(total)));
                ctx.setStatus(QStringLiteral("下载 %1 / %2（%3%）")
                                  .arg(gf::firmware::formatBytes(received),
                                       gf::firmware::formatBytes(total))
                                  .arg(int(100.0 * double(received) / double(total))));
            } else {
                ctx.setProgress(-1);
                ctx.setStatus(QStringLiteral("已下载 %1").arg(gf::firmware::formatBytes(received)));
            }
        },
        cancelOf(ctx));
    if (!result.ok) {
        if (result.bytes > 0)
            ctx.warn(QStringLiteral("已保留部分文件（%1），下次可继续下载")
                         .arg(gf::firmware::formatBytes(result.bytes)));
        throw std::runtime_error(
            QStringLiteral("下载失败：%1").arg(result.error.isEmpty() ? QStringLiteral("未知错误")
                                                                     : result.error)
                .toStdString());
    }
    ctx.info(QStringLiteral("下载完成：%1（%2%3）")
                 .arg(outputPath, gf::firmware::formatBytes(result.bytes),
                      result.resumed ? QStringLiteral("，已续传") : QString()));

    if (verify) {
        if (release.md5.isEmpty()) {
            ctx.warn(QStringLiteral("数据集没有提供 MD5，跳过哈希校验"));
        } else {
            ctx.setProgress(96);
            ctx.setStatus(QStringLiteral("校验 MD5"));
            QString actual;
            QString error;
            if (!gf::firmware::verifyFileHash(outputPath, release.md5, &actual, &error, cancelOf(ctx)))
                throw std::runtime_error(
                    QStringLiteral("MD5 校验失败：%1").arg(error).toStdString());
            ctx.info(QStringLiteral("MD5 校验通过：%1").arg(actual));
        }
    }
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("固件已下载：%1").arg(outputPath));
}

void xiaomiOtaCheck(TaskContext& ctx, const QVariantMap& options)
{
    const QString device = requireText(options, QStringLiteral("device"), QStringLiteral("机型代号"));
    const QString version = optText(options, QStringLiteral("version"));
    const QString branch = branchFilter(optInt(options, QStringLiteral("branch"), 0));
    const QString locale = optText(options, QStringLiteral("locale"));
    const QByteArray request =
        gf::firmware::buildMiuiOtaRequest(device, branch, version,
                                         locale.isEmpty() ? QStringLiteral("zh_CN") : locale);
    ctx.info(QStringLiteral("POST %1（公开 MIUI OTA 接口，实验性）")
                 .arg(gf::firmware::miuiOtaEndpoint()));
    const auto response = gf::firmware::httpPost(gf::firmware::miuiOtaEndpoint(), request,
                                                 QStringLiteral("text/xml"), 30000, cancelOf(ctx));
    if (!response.ok)
        throw std::runtime_error(
            QStringLiteral("MIUI OTA 接口查询失败：%1。官方接口可能要求 APP 报文或已改版；"
                           "请改用「小米/红米固件检索」（公开数据集，无需登录）")
                .arg(response.error.isEmpty() ? QStringLiteral("未知错误") : response.error)
                .toStdString());
    const auto releases = gf::firmware::parseMiuiOtaResponse(response.body, device);
    if (releases.isEmpty()) {
        ctx.warn(QStringLiteral("接口返回 HTTP %1，但未解析到更新条目（可能无可用更新或响应格式已变化）")
                     .arg(response.status));
        if (!response.body.isEmpty())
            ctx.debug(QStringLiteral("响应片段：%1").arg(QString::fromUtf8(response.body.left(400))));
        ctx.ok(QStringLiteral("OTA 查询完成：没有可用更新"));
        return;
    }
    for (const auto& release : releases)
        ctx.info(gf::firmware::releaseSummary(release));
    ctx.ok(QStringLiteral("OTA 查询完成：%1 条更新").arg(releases.size()));
}

// ---------------------------------------------------------------------------
// 其他品牌官方入口
// ---------------------------------------------------------------------------

void officialLookup(TaskContext& ctx, const QVariantMap& options)
{
    const QString brandQuery = optText(options, QStringLiteral("brand"));
    const QString modelQuery = optText(options, QStringLiteral("model"));
    QString error;
    const auto brands = gf::firmware::loadOfflineBrands(&error);
    if (brands.isEmpty())
        throw std::runtime_error(
            QStringLiteral("无法读取离线品牌数据库：%1").arg(
                error.isEmpty() ? gf::firmware::brandsJsonPath() : error)
                .toStdString());
    QVector<gf::firmware::BrandEntry> matches;
    for (const auto& brand : brands) {
        const QString name = brand.name.toLower();
        const QString id = brand.id.toLower();
        const QString want = brandQuery.toLower();
        if (want.isEmpty() || name.contains(want) || id.contains(want) ||
            gf::firmware::brandDisplayName(brand.id).toLower().contains(want))
            matches.append(brand);
    }
    if (matches.isEmpty())
        throw std::runtime_error(
            QStringLiteral("品牌数据库中没有「%1」").arg(brandQuery).toStdString());

    for (const auto& brand : matches) {
        ctx.info(QStringLiteral("== %1（%2）==").arg(brand.name, brand.id));
        if (!brand.subtitle.isEmpty())
            ctx.info(brand.subtitle);
        for (const auto& page : brand.official) {
            ctx.info(QStringLiteral("  %1：%2")
                         .arg(page.value(QStringLiteral("title")).toString(),
                              page.value(QStringLiteral("url")).toString()));
        }
        for (const QString& note : brand.notes)
            ctx.info(QStringLiteral("  · %1").arg(note));
        if (!brand.searchHint.isEmpty())
            ctx.info(QStringLiteral("  提示：%1").arg(brand.searchHint));
    }

    if (!modelQuery.isEmpty()) {
        const auto models = gf::firmware::loadOfflineModels(&error);
        auto modelMatches = gf::firmware::matchModels(models, modelQuery);
        if (modelMatches.isEmpty()) {
            ctx.warn(QStringLiteral("离线机型库中没有匹配「%1」的条目（可尝试输入代号，如 umi）")
                         .arg(modelQuery));
        } else {
            for (const auto& model : modelMatches) {
                ctx.info(QStringLiteral("机型匹配：%1（代号 %2，品牌 %3，系列 %4）")
                             .arg(model.names.value(0), model.codename, model.brand, model.series));
            }
        }
    }

    const bool hasXiaomi = std::any_of(matches.begin(), matches.end(), [](const auto& brand) {
        return brand.id == QStringLiteral("xiaomi") || brand.id == QStringLiteral("redmi");
    });
    if (hasXiaomi)
        ctx.info(QStringLiteral("小米/红米固件可直接使用「小米/红米固件检索」获取公开 OTA 直链"));
    ctx.warn(QStringLiteral("官方入口可能需要登录、验证或专用工具；本工具只提供入口与说明，不绕过任何验证"));
    ctx.ok(QStringLiteral("官方固件入口查询完成：%1 个品牌").arg(matches.size()));
}

// ---------------------------------------------------------------------------
// 固件文件工具
// ---------------------------------------------------------------------------

void fileIdentify(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("固件文件路径"));
    if (!QFileInfo::exists(input))
        throw std::runtime_error(QStringLiteral("文件不存在：%1").arg(input).toStdString());
    const auto info = gf::firmware::identifyFirmware(input);
    ctx.info(QStringLiteral("文件：%1").arg(info.fileName));
    ctx.info(QStringLiteral("大小：%1").arg(gf::firmware::formatBytes(info.sizeBytes)));
    ctx.info(QStringLiteral("类型：%1").arg(info.kindText));
    if (!info.hints.isEmpty())
        ctx.info(QStringLiteral("内部格式线索：%1").arg(info.hints.join(QStringLiteral("；"))));
    if (!info.hashSidecar.isEmpty())
        ctx.info(QStringLiteral("发现同目录校验文件：%1").arg(info.hashSidecar));

    QString modelError;
    const auto models = gf::firmware::loadOfflineModels(&modelError);
    const auto matches = gf::firmware::matchModels(models, info.fileName);
    for (const auto& model : matches) {
        ctx.info(QStringLiteral("机型匹配：%1（代号 %2，品牌 %3）")
                     .arg(model.names.value(0), model.codename, model.brand));
    }
    if (matches.isEmpty()) {
        const QString codename = gf::firmware::codenameFromFileName(info.fileName, models);
        if (!codename.isEmpty())
            ctx.info(QStringLiteral("从文件名提取代号：%1").arg(codename));
    }

    if (gf::firmware::kindIsUnpackable(info.kind)) {
        ctx.info(QStringLiteral("建议：使用「固件自动解包」工具解压到目录"));
    } else if (info.kind == gf::firmware::FirmwareKind::Ozip ||
               info.kind == gf::firmware::FirmwareKind::Ofp) {
        ctx.warn(QStringLiteral("OPPO 加密固件暂不支持解包，请使用官方工具"));
    } else if (info.kind == gf::firmware::FirmwareKind::RawImage) {
        const QString hints = info.hints.join(QLatin1Char(' '));
        if (hints.contains(QStringLiteral("payload")))
            ctx.info(QStringLiteral("建议：先用「Payload 解包」（image 模块）导出分区"));
        else if (hints.contains(QStringLiteral("sparse")))
            ctx.info(QStringLiteral("建议：先用「Sparse 解包」（image 模块）转 raw"));
        else if (hints.contains(QStringLiteral("super")))
            ctx.info(QStringLiteral("建议：用「Super 拆分」（image 模块）"));
        else if (hints.contains(QStringLiteral("boot")))
            ctx.info(QStringLiteral("建议：用「Boot 解包」（image 模块）"));
        else if (hints.contains(QStringLiteral("vbmeta")))
            ctx.info(QStringLiteral("建议：用「VBMeta 信息」（image 模块）"));
        else if (hints.contains(QStringLiteral("GPT")))
            ctx.info(QStringLiteral("建议：用「GPT 解析」（image 模块）"));
    }
    ctx.ok(QStringLiteral("固件类型识别完成：%1").arg(info.kindText));
}

void fileUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = requireText(options, QStringLiteral("input"), QStringLiteral("固件文件路径"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出目录"));
    ctx.info(QStringLiteral("开始解包：%1 -> %2").arg(input, output));
    const auto result = gf::firmware::unpackFirmware(input, output, progressOf(ctx), cancelOf(ctx));
    for (const QString& note : result.notes)
        ctx.warn(note);
    if (!result.ok)
        throw std::runtime_error(result.error.isEmpty() ? QStringLiteral("解包失败").toStdString()
                                                        : result.error.toStdString());
    ctx.info(QStringLiteral("类型 %1：文件 %2 个，目录 %3 个，写入 %4")
                 .arg(result.kindText)
                 .arg(result.filesExtracted)
                 .arg(result.dirsCreated)
                 .arg(gf::firmware::formatBytes(result.bytesWritten)));
    for (const QString& path : result.extracted)
        ctx.debug(QStringLiteral("  %1").arg(path));
    if (result.extracted.size() >= 200)
        ctx.info(QStringLiteral("仅显示前 200 个条目"));
    bool hasPayload = false;
    bool hasScatter = false;
    for (const QString& path : result.extracted) {
        const QString lower = path.toLower();
        if (lower.endsWith(QStringLiteral("payload.bin")))
            hasPayload = true;
        if (lower.endsWith(QStringLiteral("scatter.txt")))
            hasScatter = true;
    }
    if (hasPayload)
        ctx.info(QStringLiteral("包含 payload.bin：可用 image 模块「Payload 解包」导出分区"));
    if (hasScatter)
        ctx.info(QStringLiteral("包含 scatter.txt：可在 MTK 工作台直接加载"));
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("固件解包完成：%1").arg(output));
}

void fileHash(TaskContext& ctx, const QVariantMap& options)
{
    QStringList paths;
    const QVariant raw = options.value(QStringLiteral("input"));
    if (raw.typeId() == QMetaType::QVariantList) {
        for (const QVariant& item : raw.toList())
            paths << item.toString();
    } else {
        paths = splitNames(raw.toString());
    }
    if (paths.isEmpty())
        throw std::runtime_error("请选择至少一个固件文件");
    const QString expected = optText(options, QStringLiteral("expected")).toLower();
    const int algorithm = optInt(options, QStringLiteral("algorithm"), 0);
    int index = 0;
    int matched = 0;
    for (const QString& path : paths) {
        ctx.checkCanceled();
        ++index;
        const QFileInfo info(path);
        if (!info.isFile())
            throw std::runtime_error(QStringLiteral("文件不存在：%1").arg(path).toStdString());
        ctx.setStatus(QStringLiteral("计算哈希 %1").arg(info.fileName()));
        const auto hashes = gf::firmware::hashFile(path, cancelOf(ctx));
        ctx.info(QStringLiteral("%1\n  SHA256: %2\n  MD5   : %3\n  大小  : %4")
                     .arg(info.fileName(), hashes.sha256, hashes.md5,
                          gf::firmware::formatBytes(hashes.size)));
        if (!expected.isEmpty()) {
            QString actual;
            if (expected.size() == 32 || algorithm == 2)
                actual = hashes.md5;
            else
                actual = hashes.sha256;
            if (actual.compare(expected, Qt::CaseInsensitive) == 0) {
                ++matched;
                ctx.ok(QStringLiteral("%1 哈希匹配").arg(info.fileName()));
            } else {
                ctx.error(QStringLiteral("%1 哈希不匹配（期望 %2）").arg(info.fileName(), expected));
            }
        }
        ctx.setProgress(int(100.0 * index / paths.size()));
    }
    if (!expected.isEmpty() && matched != paths.size())
        throw std::runtime_error("存在哈希不匹配的文件");
    ctx.ok(QStringLiteral("哈希计算完成：%1 个文件").arg(paths.size()));
}

void fileMergeSplit(TaskContext& ctx, const QVariantMap& options)
{
    QString input = requireText(options, QStringLiteral("input"), QStringLiteral("分卷文件或压缩包"));
    const QString output = requireText(options, QStringLiteral("output"), QStringLiteral("输出文件路径"));
    if (QFileInfo(input).isDir()) {
        const QDir dir(input);
        const QStringList zips = dir.entryList({QStringLiteral("*.zip")}, QDir::Files, QDir::Name);
        const QStringList z01 = dir.entryList({QStringLiteral("*.z01")}, QDir::Files, QDir::Name);
        const QStringList n001 = dir.entryList({QStringLiteral("*.001")}, QDir::Files, QDir::Name);
        if (!zips.isEmpty())
            input = dir.filePath(zips.first());
        else if (!z01.isEmpty())
            input = dir.filePath(z01.first());
        else if (!n001.isEmpty())
            input = dir.filePath(n001.first());
        else
            throw std::runtime_error("目录中没有找到 .zip / .z01 / .001 分卷文件");
    }
    const auto parts = gf::firmware::findSplitParts(input);
    if (parts.size() < 2)
        throw std::runtime_error(
            QStringLiteral("未找到分卷：请选择 .zip 主文件或任意 .z01/.002 分卷").toStdString());
    ctx.info(QStringLiteral("找到 %1 个分卷：").arg(parts.size()));
    for (const QString& part : parts)
        ctx.info(QStringLiteral("  %1（%2）").arg(QFileInfo(part).fileName(),
                                                 gf::firmware::formatBytes(QFileInfo(part).size())));
    const auto result = gf::firmware::mergeSplitFirmware(input, output, progressOf(ctx), cancelOf(ctx));
    if (!result.ok)
        throw std::runtime_error(result.error.isEmpty() ? QStringLiteral("分卷合并失败").toStdString()
                                                        : result.error.toStdString());
    ctx.info(QStringLiteral("已合并 %1 个分卷 -> %2（%3）")
                 .arg(result.parts.size())
                 .arg(output, gf::firmware::formatBytes(result.bytes)));
    const auto info = gf::firmware::identifyFirmware(output);
    ctx.info(QStringLiteral("输出文件类型：%1").arg(info.kindText));
    ctx.ok(QStringLiteral("分卷合并完成：%1").arg(output));
}

void fileMatchModel(TaskContext& ctx, const QVariantMap& options)
{
    const QString query = requireText(options, QStringLiteral("query"), QStringLiteral("文件名或机型"));
    const int limit = qBound(1, optInt(options, QStringLiteral("limit"), 20), 200);
    QString error;
    const auto models = gf::firmware::loadOfflineModels(&error);
    if (models.isEmpty())
        throw std::runtime_error(
            QStringLiteral("无法读取离线机型数据库：%1").arg(
                error.isEmpty() ? gf::firmware::modelsJsonPath() : error)
                .toStdString());
    auto matches = gf::firmware::matchModels(models, query);
    if (matches.isEmpty()) {
        const QString codename = gf::firmware::codenameFromFileName(query, models);
        if (!codename.isEmpty()) {
            ctx.warn(QStringLiteral("文件名中提取到代号 %1，但机型库未收录（可在「小米/红米固件检索」中直接搜索该代号）")
                         .arg(codename));
        } else {
            throw std::runtime_error(
                QStringLiteral("离线机型库中没有匹配「%1」的条目").arg(query).toStdString());
        }
    }
    if (matches.size() > limit)
        matches = matches.mid(0, limit);
    for (const auto& model : matches) {
        ctx.info(QStringLiteral("%1 | 代号 %2 | 品牌 %3 | 系列 %4")
                     .arg(model.names.join(QStringLiteral(" / ")), model.codename,
                          gf::firmware::brandDisplayName(model.brand), model.series));
    }
    ctx.ok(QStringLiteral("机型匹配完成：%1 条").arg(matches.size()));
}

void datasetInfo(TaskContext& ctx, const QVariantMap& options)
{
    const bool refresh = optBool(options, QStringLiteral("refresh"), false);
    const auto dataset = requireDataset(ctx, refresh);
    QHash<QString, int> branches;
    QString newest;
    QSet<QString> codenames;
    for (const auto& release : dataset.releases) {
        branches[release.branch] += 1;
        codenames.insert(release.codename);
        if (release.date > newest)
            newest = release.date;
    }
    ctx.info(QStringLiteral("发布记录：%1 条，机型：%2 个，最新日期：%3")
                 .arg(dataset.releases.size())
                 .arg(codenames.size())
                 .arg(newest.isEmpty() ? QStringLiteral("未知") : newest));
    for (auto it = branches.constBegin(); it != branches.constEnd(); ++it) {
        ctx.info(QStringLiteral("  %1：%2 条")
                     .arg(gf::firmware::branchText(it.key()))
                     .arg(it.value()));
    }
    ctx.info(QStringLiteral("本地缓存目录：%1").arg(gf::firmware::cacheDir()));
    ctx.ok(QStringLiteral("数据集信息读取完成"));
}


void protectedContainerUnpack(TaskContext& ctx, const QVariantMap& options)
{
    const QString input = optText(options, QStringLiteral("input"));
    QString output = optText(options, QStringLiteral("output"));
    if (input.isEmpty() || !QFileInfo::exists(input))
        throw std::runtime_error("请选择 OZIP / OFP 固件文件");
    if (output.isEmpty()) {
        output = QFileInfo(input).absolutePath() + QLatin1Char('/') + QFileInfo(input).completeBaseName()
                 + QStringLiteral("_unpack");
    }
    const gf::firmware::FirmwareFileInfo info = gf::firmware::identifyFirmware(input);
    ctx.info(QStringLiteral("识别结果：%1").arg(info.kindText));
    if (!gf::firmware::kindIsUnpackable(info.kind)) {
        // Encrypted OZIP/OFP containers cannot be opened without the vendor key,
        // and this tool deliberately does not try to break that protection.
        ctx.warn(QStringLiteral("这是加密的 %1 容器，需要厂商官方工具/授权流程解密。").arg(info.kindText));
        ctx.log(0, QStringLiteral("本工具不会绕过厂商签名或加密校验。"));
        ctx.log(0, QStringLiteral("可先用「固件类型识别」查看文件头信息，或改用未加密的 zip / tar.gz 固件。"));
        throw std::runtime_error(QStringLiteral("无法直接解包 %1（加密容器）").arg(QFileInfo(input).fileName()).toStdString());
    }
    const auto result = gf::firmware::unpackFirmware(input, output, progressOf(ctx), cancelOf(ctx));
    for (const QString& note : result.notes)
        ctx.log(0, note);
    if (!result.ok)
        throw std::runtime_error(result.error.toStdString());
    ctx.setProgress(100);
    ctx.ok(QStringLiteral("解包完成：%1 个文件 → %2").arg(result.filesExtracted).arg(result.outputDir));
}



void registerProtectedContainerTools()
{
    ToolSpec ozip;
    ozip.id = QStringLiteral("firmware.file.unpackOzip");
    ozip.category = QStringLiteral("firmware");
    ozip.group = QStringLiteral("file");
    ozip.name = QStringLiteral("OZIP 解包");
    ozip.desc = QStringLiteral("OPPO OZIP 固件解包：zip 兼容容器直接解包，加密容器给出明确说明");
    ozip.icon = QStringLiteral("file-archive");
    ozip.keywords = {QStringLiteral("ozip"), QStringLiteral("oppo"), QStringLiteral("解包")};
    ozip.fields = {pathField(QStringLiteral("input"), QStringLiteral("OZIP 文件"), QStringLiteral("*.ozip")),
                   fields::dir(QStringLiteral("output"), QStringLiteral("输出目录（可选）"), QString(), false)};
    ToolRegistry::instance().add(ozip, protectedContainerUnpack);

    ToolSpec ofp;
    ofp.id = QStringLiteral("firmware.file.unpackOfp");
    ofp.category = QStringLiteral("firmware");
    ofp.group = QStringLiteral("file");
    ofp.name = QStringLiteral("OFP 解包");
    ofp.desc = QStringLiteral("OPPO/OnePlus OFP 固件解包：zip 兼容容器直接解包，加密容器给出明确说明");
    ofp.icon = QStringLiteral("file-archive");
    ofp.keywords = {QStringLiteral("ofp"), QStringLiteral("oppo"), QStringLiteral("解包")};
    ofp.fields = {pathField(QStringLiteral("input"), QStringLiteral("OFP 文件"), QStringLiteral("*.ofp")),
                  fields::dir(QStringLiteral("output"), QStringLiteral("输出目录（可选）"), QString(), false)};
    ToolRegistry::instance().add(ofp, protectedContainerUnpack);
}



} // namespace

void registerFirmwareToolHandlers()
{
    using namespace gf::fields;

    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.xiaomi.search");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("xiaomi");
        spec.name = QStringLiteral("小米 / 红米固件检索");
        spec.desc = QStringLiteral("按机型或代号查询公开 OTA 数据集中的完整包直链（版本/大小/MD5）");
        spec.icon = QStringLiteral("download-cloud");
        spec.keywords = {QStringLiteral("小米"), QStringLiteral("红米"), QStringLiteral("miui"),
                         QStringLiteral("hyperos"), QStringLiteral("固件"), QStringLiteral("ota")};
        spec.fields = {text(QStringLiteral("query"), QStringLiteral("机型或代号"), QString(),
                            QStringLiteral("小米 14 / nuwa / umi"),
                            QStringLiteral("支持中英文机型名与代号"), true),
                       choice(QStringLiteral("branch"), QStringLiteral("版本通道"), branchOptions(), 0),
                       choice(QStringLiteral("method"), QStringLiteral("包类型"), methodOptions(), 0),
                       integer(QStringLiteral("limit"), QStringLiteral("最多显示条数"), 20, 1, 200),
                       boolean(QStringLiteral("refresh"), QStringLiteral("强制刷新数据集"), false)};
        spec.order = 10;
        addTool(spec, xiaomiSearch);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.xiaomi.download");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("xiaomi");
        spec.name = QStringLiteral("小米 / 红米固件下载");
        spec.desc = QStringLiteral("检索并下载最新完整包到下载目录，支持断点续传与 MD5 校验");
        spec.icon = QStringLiteral("download");
        spec.keywords = {QStringLiteral("小米"), QStringLiteral("红米"), QStringLiteral("下载"),
                         QStringLiteral("固件")};
        spec.fields = {text(QStringLiteral("query"), QStringLiteral("机型或代号"), QString(),
                            QStringLiteral("红米 K70 / vermeer"), QString(), true),
                       choice(QStringLiteral("branch"), QStringLiteral("版本通道"), branchOptions(), 0),
                       choice(QStringLiteral("method"), QStringLiteral("包类型"), methodOptions(), 0),
                       text(QStringLiteral("version"), QStringLiteral("版本过滤（可选）"), QString(),
                            QStringLiteral("V14.0"), QStringLiteral("按版本号子串过滤"), false),
                       dir(QStringLiteral("outputDir"), QStringLiteral("保存目录（留空=设置里的下载目录）"),
                           QString(), false),
                       boolean(QStringLiteral("verify"), QStringLiteral("下载后校验 MD5"), true),
                       boolean(QStringLiteral("resume"), QStringLiteral("断点续传"), true),
                       boolean(QStringLiteral("refresh"), QStringLiteral("强制刷新数据集"), false)};
        spec.order = 11;
        addTool(spec, xiaomiDownload);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.xiaomi.otaCheck");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("xiaomi");
        spec.name = QStringLiteral("MIUI OTA 接口查询");
        spec.desc = QStringLiteral("调用官方公开 OTA 接口查询更新（实验性，失败请改用数据集检索）");
        spec.icon = QStringLiteral("globe");
        spec.keywords = {QStringLiteral("ota"), QStringLiteral("miui"), QStringLiteral("更新")};
        spec.fields = {text(QStringLiteral("device"), QStringLiteral("机型代号"), QString(),
                            QStringLiteral("umi"), QStringLiteral("例如 umi / alioth"), true),
                       text(QStringLiteral("version"), QStringLiteral("当前版本（可选）"), QString(),
                            QStringLiteral("V12.5.1.0.RJBCNXM"), QString(), false),
                       choice(QStringLiteral("branch"), QStringLiteral("版本通道"), branchOptions(), 0),
                       choice(QStringLiteral("locale"), QStringLiteral("区域"),
                              {QStringLiteral("zh_CN"), QStringLiteral("en_US"),
                               QStringLiteral("zh_TW")}, 0)};
        spec.order = 12;
        addTool(spec, xiaomiOtaCheck);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.official.lookup");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("brands");
        spec.name = QStringLiteral("品牌官方固件入口");
        spec.desc = QStringLiteral("查询 OPPO/vivo/realme/一加/三星/展锐/瑞芯微/全志 官方固件页面与说明");
        spec.icon = QStringLiteral("globe");
        spec.keywords = {QStringLiteral("官方"), QStringLiteral("固件"), QStringLiteral("oppo"),
                         QStringLiteral("vivo"), QStringLiteral("三星"), QStringLiteral("瑞芯微")};
        spec.fields = {choice(QStringLiteral("brand"), QStringLiteral("品牌"), brandOptions(), 0,
                              QStringLiteral("来源：data/firmware/brands.json")),
                       text(QStringLiteral("model"), QStringLiteral("机型（可选）"), QString(),
                            QStringLiteral("Find X7"), QString(), false)};
        spec.order = 20;
        addTool(spec, officialLookup);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.file.identify");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("file");
        spec.name = QStringLiteral("固件类型识别");
        spec.desc = QStringLiteral("识别 zip/tgz/pac/img/bin/ozip/ofp 等固件类型与内部格式，并匹配机型");
        spec.icon = QStringLiteral("search");
        spec.keywords = {QStringLiteral("识别"), QStringLiteral("固件"), QStringLiteral("类型"),
                         QStringLiteral("机型")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("固件文件"))};
        spec.order = 30;
        addTool(spec, fileIdentify);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.file.unpack");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("file");
        spec.name = QStringLiteral("固件自动解包");
        spec.desc = QStringLiteral("自动解包 zip / tgz / tar / PAC 固件到目录（带进度与取消）");
        spec.icon = QStringLiteral("file-archive");
        spec.keywords = {QStringLiteral("解包"), QStringLiteral("解压"), QStringLiteral("tgz"),
                         QStringLiteral("pac"), QStringLiteral("zip")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("固件文件")),
                       dir(QStringLiteral("output"), QStringLiteral("输出目录"))};
        spec.order = 31;
        addTool(spec, fileUnpack);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.file.hash");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("file");
        spec.name = QStringLiteral("固件哈希校验");
        spec.desc = QStringLiteral("批量计算 SHA256 / MD5，并与期望值比对");
        spec.icon = QStringLiteral("hash");
        spec.keywords = {QStringLiteral("哈希"), QStringLiteral("md5"), QStringLiteral("sha256"),
                         QStringLiteral("校验")};
        spec.fields = {paths(QStringLiteral("input"), QStringLiteral("固件文件列表"),
                             QStringLiteral("所有文件 (*)")),
                       text(QStringLiteral("expected"), QStringLiteral("期望哈希（可选）"), QString(),
                            QStringLiteral("MD5 32 位或 SHA256 64 位"), QString(), false),
                       choice(QStringLiteral("algorithm"), QStringLiteral("按期望值匹配"),
                              {QStringLiteral("自动（按长度）"), QStringLiteral("SHA256"),
                               QStringLiteral("MD5")}, 0)};
        spec.order = 32;
        addTool(spec, fileHash);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.file.mergeSplit");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("file");
        spec.name = QStringLiteral("固件分卷合并");
        spec.desc = QStringLiteral("合并 .z01/.z02/.zip 或 .001/.002 分卷为一个完整固件");
        spec.icon = QStringLiteral("merge");
        spec.keywords = {QStringLiteral("分卷"), QStringLiteral("合并"), QStringLiteral("z01")};
        spec.fields = {pathField(QStringLiteral("input"), QStringLiteral("任一分卷或主 zip 文件")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出文件路径"))};
        spec.order = 33;
        addTool(spec, fileMergeSplit);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.file.matchModel");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("file");
        spec.name = QStringLiteral("机型数据库匹配");
        spec.desc = QStringLiteral("按文件名或机型名匹配离线机型库（代号/品牌/系列）");
        spec.icon = QStringLiteral("list-checks");
        spec.keywords = {QStringLiteral("机型"), QStringLiteral("代号"), QStringLiteral("数据库")};
        spec.fields = {text(QStringLiteral("query"), QStringLiteral("文件名或机型"), QString(),
                            QStringLiteral("miui_UMI_V14.0.4.0.TJBCNXM.zip"),
                            QStringLiteral("可输入完整文件名或机型名"), true),
                       integer(QStringLiteral("limit"), QStringLiteral("最多显示条数"), 20, 1, 200)};
        spec.order = 34;
        addTool(spec, fileMatchModel);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("firmware.dataset.info");
        spec.category = QStringLiteral("firmware");
        spec.group = QStringLiteral("xiaomi");
        spec.name = QStringLiteral("固件数据集信息");
        spec.desc = QStringLiteral("查看公开数据集规模、通道分布与缓存目录");
        spec.icon = QStringLiteral("database");
        spec.keywords = {QStringLiteral("数据集"), QStringLiteral("缓存"), QStringLiteral("统计")};
        spec.fields = {boolean(QStringLiteral("refresh"), QStringLiteral("强制刷新"), false)};
        spec.order = 13;
        addTool(spec, datasetInfo);
    }

    registerProtectedContainerTools();
}

} // namespace gf
