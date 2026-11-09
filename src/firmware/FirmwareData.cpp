#include "FirmwareData.h"

#include "FirmwareCommon.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>

namespace gf::firmware {

namespace {

QString unquoteYaml(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.at(0);
        const QChar last = value.at(value.size() - 1);
        if ((first == QLatin1Char('\'') && last == QLatin1Char('\'')) ||
            (first == QLatin1Char('"') && last == QLatin1Char('"')))
            return value.mid(1, value.size() - 2);
    }
    return value;
}

QString jsonString(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toString();
}

QStringList jsonStringList(const QJsonValue& value)
{
    QStringList out;
    if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty())
                out << text;
        }
    } else if (value.isString()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            out << text;
    }
    return out;
}

} // namespace

QString defaultLatestDatasetUrl()
{
    return QStringLiteral(
        "https://raw.githubusercontent.com/XiaomiFirmwareUpdater/miui-updates-tracker/master/data/latest.yml");
}

QString defaultDevicesDatasetUrl()
{
    return QStringLiteral(
        "https://raw.githubusercontent.com/XiaomiFirmwareUpdater/miui-updates-tracker/master/data/devices.yml");
}

QVariantMap FirmwareRelease::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("codename"), codename);
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("version"), version);
    map.insert(QStringLiteral("branch"), branch);
    map.insert(QStringLiteral("branchText"), branchText(branch));
    map.insert(QStringLiteral("android"), android);
    map.insert(QStringLiteral("method"), method);
    map.insert(QStringLiteral("date"), date);
    map.insert(QStringLiteral("link"), link);
    map.insert(QStringLiteral("md5"), md5);
    map.insert(QStringLiteral("sizeText"), sizeText.isEmpty() ? formatBytes(sizeBytes) : sizeText);
    map.insert(QStringLiteral("sizeBytes"), double(sizeBytes));
    return map;
}

QVector<FirmwareRelease> parseLatestYaml(const QByteArray& data, QString* error)
{
    QVector<FirmwareRelease> out;
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("数据集为空");
        return out;
    }
    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QLatin1Char('\n'));
    QHash<QString, QString> current;
    bool hasEntry = false;

    auto flush = [&] {
        if (!hasEntry || current.isEmpty())
            return;
        FirmwareRelease release;
        release.codename = current.value(QStringLiteral("codename")).trimmed();
        release.name = current.value(QStringLiteral("name")).trimmed();
        release.version = current.value(QStringLiteral("version")).trimmed();
        release.branch = current.value(QStringLiteral("branch")).trimmed();
        release.android = current.value(QStringLiteral("android")).trimmed();
        release.method = current.value(QStringLiteral("method")).trimmed();
        release.date = current.value(QStringLiteral("date")).trimmed();
        release.link = current.value(QStringLiteral("link")).trimmed();
        release.md5 = current.value(QStringLiteral("md5")).trimmed().toLower();
        release.sizeText = current.value(QStringLiteral("size")).trimmed();
        release.sizeBytes = sizeTextToBytes(release.sizeText);
        if (!release.codename.isEmpty() && !release.link.isEmpty())
            out.append(release);
        current.clear();
    };

    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#')))
            continue;
        const bool newItem = line.startsWith(QStringLiteral("- ")) || line.trimmed() == QStringLiteral("-");
        if (newItem) {
            flush();
            hasEntry = true;
            if (line.trimmed() == QStringLiteral("-"))
                continue;
            line = line.mid(2);
        }
        if (!hasEntry)
            continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = unquoteYaml(line.mid(colon + 1));
        if (key.isEmpty() || key.contains(QLatin1Char(' ')))
            continue;
        current.insert(key, value);
    }
    flush();

    if (out.isEmpty() && error)
        *error = QStringLiteral("YAML 数据集解析结果为空（格式可能已变化）");
    return out;
}

QHash<QString, QStringList> parseDevicesYaml(const QByteArray& data)
{
    QHash<QString, QStringList> devices;
    const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    QString currentKey;
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QStringLiteral("- "))) {
            const QString name = unquoteYaml(line.mid(2));
            if (!currentKey.isEmpty() && !name.isEmpty())
                devices[currentKey] << name;
            continue;
        }
        if (line.trimmed().endsWith(QLatin1Char(':')) && !line.startsWith(QLatin1Char(' '))) {
            currentKey = line.trimmed().chopped(1).trimmed();
            if (!currentKey.isEmpty() && !devices.contains(currentKey))
                devices.insert(currentKey, {});
        }
    }
    // 空列表的机型也保留，便于按代号检索。
    return devices;
}

QVector<BrandEntry> parseBrandsJson(const QByteArray& data, QString* error)
{
    QVector<BrandEntry> brands;
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("brands.json 解析失败：%1").arg(parseError.errorString());
        return brands;
    }
    const QJsonObject root = doc.object();
    const QJsonArray array = root.value(QStringLiteral("brands")).toArray();
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject obj = value.toObject();
        BrandEntry entry;
        entry.id = jsonString(obj, QStringLiteral("id")).trimmed().toLower();
        entry.name = jsonString(obj, QStringLiteral("name")).trimmed();
        entry.subtitle = jsonString(obj, QStringLiteral("subtitle")).trimmed();
        entry.searchHint = jsonString(obj, QStringLiteral("searchHint")).trimmed();
        for (const QJsonValue& page : obj.value(QStringLiteral("official")).toArray()) {
            const QJsonObject pageObj = page.toObject();
            QVariantMap map;
            map.insert(QStringLiteral("title"), jsonString(pageObj, QStringLiteral("title")));
            map.insert(QStringLiteral("url"), jsonString(pageObj, QStringLiteral("url")));
            if (!map.value(QStringLiteral("url")).toString().isEmpty())
                entry.official.append(map);
        }
        entry.notes = jsonStringList(obj.value(QStringLiteral("notes")));
        if (entry.id.isEmpty() || entry.name.isEmpty())
            continue;
        brands.append(entry);
    }
    if (brands.isEmpty() && error)
        *error = QStringLiteral("brands.json 中没有有效品牌条目");
    return brands;
}

QVector<ModelEntry> parseModelsJson(const QByteArray& data, QString* error)
{
    QVector<ModelEntry> models;
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("models.json 解析失败：%1").arg(parseError.errorString());
        return models;
    }
    const QJsonArray array = doc.object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject obj = value.toObject();
        ModelEntry entry;
        entry.codename = jsonString(obj, QStringLiteral("codename")).trimmed();
        entry.brand = jsonString(obj, QStringLiteral("brand")).trimmed().toLower();
        entry.series = jsonString(obj, QStringLiteral("series")).trimmed();
        entry.names = jsonStringList(obj.value(QStringLiteral("names")));
        if (entry.codename.isEmpty())
            continue;
        if (entry.names.isEmpty())
            entry.names << entry.codename;
        models.append(entry);
    }
    if (models.isEmpty() && error)
        *error = QStringLiteral("models.json 中没有有效机型条目");
    return models;
}

QVector<BrandEntry> loadOfflineBrands(QString* error)
{
    const QString path = brandsJsonPath();
    const QByteArray data = readFile(path);
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("找不到离线品牌数据库：%1").arg(path);
        return {};
    }
    return parseBrandsJson(data, error);
}

QVector<ModelEntry> loadOfflineModels(QString* error)
{
    const QString path = modelsJsonPath();
    const QByteArray data = readFile(path);
    if (data.isEmpty()) {
        if (error)
            *error = QStringLiteral("找不到离线机型数据库：%1").arg(path);
        return {};
    }
    return parseModelsJson(data, error);
}

QString normalizeQuery(const QString& text)
{
    return text.trimmed().toLower();
}

QString resolveCodename(const QString& query, const QHash<QString, QStringList>& devices)
{
    const QString want = normalizeQuery(query);
    if (want.isEmpty())
        return {};
    for (auto it = devices.constBegin(); it != devices.constEnd(); ++it) {
        if (normalizeQuery(it.key()) == want)
            return it.key();
    }
    for (auto it = devices.constBegin(); it != devices.constEnd(); ++it) {
        for (const QString& name : it.value()) {
            if (normalizeQuery(name) == want)
                return it.key();
        }
    }
    for (auto it = devices.constBegin(); it != devices.constEnd(); ++it) {
        for (const QString& name : it.value()) {
            if (normalizeQuery(name).contains(want))
                return it.key();
        }
    }
    return query.trimmed().toUpper();
}

QStringList searchDeviceCodenames(const QHash<QString, QStringList>& devices, const QString& query,
                                  int limit)
{
    QStringList out;
    const QString want = normalizeQuery(query);
    if (want.isEmpty())
        return out;
    for (auto it = devices.constBegin(); it != devices.constEnd() && out.size() < limit; ++it) {
        bool hit = normalizeQuery(it.key()).contains(want);
        if (!hit) {
            for (const QString& name : it.value()) {
                if (normalizeQuery(name).contains(want)) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit)
            out << it.key();
    }
    return out;
}

QVector<FirmwareRelease> filterReleases(const QVector<FirmwareRelease>& releases, const QString& query,
                                        const QString& branch, const QString& method, int limit,
                                        bool newestFirst)
{
    const QString want = normalizeQuery(query);
    const QString wantBranch = normalizeQuery(branch);
    const QString wantMethod = normalizeQuery(method);
    QVector<FirmwareRelease> out;
    for (const auto& release : releases) {
        if (!want.isEmpty()) {
            const bool hit = normalizeQuery(release.codename).contains(want) ||
                             normalizeQuery(release.name).contains(want) ||
                             normalizeQuery(release.version).contains(want);
            if (!hit)
                continue;
        }
        if (!wantBranch.isEmpty() && normalizeQuery(release.branch) != wantBranch)
            continue;
        if (!wantMethod.isEmpty() && normalizeQuery(release.method) != wantMethod)
            continue;
        out.append(release);
    }
    if (newestFirst) {
        std::stable_sort(out.begin(), out.end(), [](const FirmwareRelease& a, const FirmwareRelease& b) {
            if (a.date != b.date)
                return a.date > b.date;
            return a.version > b.version;
        });
    }
    if (limit > 0 && out.size() > limit)
        out = out.mid(0, limit);
    return out;
}

QVector<ModelEntry> matchModels(const QVector<ModelEntry>& models, const QString& text)
{
    QVector<ModelEntry> out;
    const QString want = normalizeQuery(text);
    if (want.isEmpty())
        return out;
    for (const auto& model : models) {
        bool hit = false;
        if (!model.codename.isEmpty() && want.contains(normalizeQuery(model.codename)))
            hit = true;
        if (!hit) {
            for (const QString& name : model.names) {
                if (want.contains(normalizeQuery(name))) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit)
            out.append(model);
    }
    return out;
}

QString codenameFromFileName(const QString& fileName, const QVector<ModelEntry>& models,
                             const QHash<QString, QStringList>& devices)
{
    static const QRegularExpression separators(QStringLiteral("[^A-Za-z0-9]+"));
    const QStringList tokens = fileName.split(separators, Qt::SkipEmptyParts);
    // 1) 匹配机型数据库中的代号（大小写不敏感）。
    for (const QString& token : tokens) {
        for (const auto& model : models) {
            if (token.compare(model.codename, Qt::CaseInsensitive) == 0)
                return model.codename;
        }
    }
    // 2) 匹配公开设备表。
    for (const QString& token : tokens) {
        for (auto it = devices.constBegin(); it != devices.constEnd(); ++it) {
            if (token.compare(it.key(), Qt::CaseInsensitive) == 0)
                return it.key();
        }
    }
    // 3) 退化：形如 HM2013022 / PD2301 这种「字母+数字」的令牌。
    static const QRegularExpression mixedToken(QStringLiteral("^[A-Za-z]{1,6}\\d{2,8}[A-Za-z0-9]*$"));
    static const QStringList knownPrefixes = {QStringLiteral("MIUI"), QStringLiteral("MI"),
                                              QStringLiteral("REDMI"), QStringLiteral("POCO"),
                                              QStringLiteral("OTA"), QStringLiteral("GLOBAL"),
                                              QStringLiteral("CN"), QStringLiteral("EEA"),
                                              QStringLiteral("IN"), QStringLiteral("RU")};
    for (const QString& token : tokens) {
        if (knownPrefixes.contains(token.toUpper()))
            continue;
        if (token.size() < 3 || token.size() > 12)
            continue;
        if (token.startsWith(QLatin1Char('V')) && token.size() > 1 && token.at(1).isDigit())
            continue; // V14.0.5.0 之类的版本号
        if (mixedToken.match(token).hasMatch())
            return token;
    }
    return {};
}

QString branchText(const QString& branch)
{
    const QString lower = branch.trimmed().toLower();
    if (lower == QStringLiteral("stable"))
        return QStringLiteral("稳定版");
    if (lower == QStringLiteral("weekly"))
        return QStringLiteral("开发版");
    if (lower == QStringLiteral("beta"))
        return QStringLiteral("内测版");
    if (lower.isEmpty())
        return QStringLiteral("未知");
    return branch;
}

QString releaseSummary(const FirmwareRelease& release, bool includeLink)
{
    QStringList parts;
    parts << QStringLiteral("%1（%2）").arg(release.name.isEmpty() ? release.codename : release.name,
                                          release.codename);
    if (!release.version.isEmpty())
        parts << QStringLiteral("版本 %1").arg(release.version);
    if (!release.branch.isEmpty())
        parts << branchText(release.branch);
    if (!release.android.isEmpty())
        parts << QStringLiteral("Android %1").arg(release.android);
    if (!release.method.isEmpty())
        parts << release.method;
    if (release.sizeBytes >= 0)
        parts << formatBytes(release.sizeBytes);
    if (!release.date.isEmpty())
        parts << release.date;
    if (!release.md5.isEmpty())
        parts << QStringLiteral("MD5 %1").arg(release.md5);
    QString text = parts.join(QStringLiteral(" | "));
    if (includeLink && !release.link.isEmpty())
        text += QStringLiteral("\n    下载：%1").arg(release.link);
    return text;
}

QString brandDisplayName(const QString& id)
{
    const QString key = id.trimmed().toLower();
    static const QHash<QString, QString> names = {
        {QStringLiteral("xiaomi"), QStringLiteral("小米")},
        {QStringLiteral("redmi"), QStringLiteral("红米")},
        {QStringLiteral("oppo"), QStringLiteral("OPPO")},
        {QStringLiteral("vivo"), QStringLiteral("vivo")},
        {QStringLiteral("realme"), QStringLiteral("realme")},
        {QStringLiteral("oneplus"), QStringLiteral("一加")},
        {QStringLiteral("samsung"), QStringLiteral("三星")},
        {QStringLiteral("unisoc"), QStringLiteral("紫光展锐")},
        {QStringLiteral("rockchip"), QStringLiteral("瑞芯微")},
        {QStringLiteral("allwinner"), QStringLiteral("全志")},
        {QStringLiteral("honor"), QStringLiteral("荣耀")},
        {QStringLiteral("huawei"), QStringLiteral("华为")},
    };
    return names.value(key, id);
}

} // namespace gf::firmware
