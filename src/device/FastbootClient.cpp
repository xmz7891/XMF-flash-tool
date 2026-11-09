#include "FastbootClient.h"

#include "PlatformTools.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

#include "core/Util.h"

namespace gf::device {

namespace {

FastbootResult toResult(const ToolRunResult& tool, int timeoutMs)
{
    FastbootResult result;
    result.exitCode = tool.proc.exitCode;
    result.output = QString::fromLocal8Bit(tool.combined()).trimmed();
    if (!tool.error.isEmpty()) {
        result.ok = false;
        result.error = tool.error;
    } else if (tool.proc.timedOut) {
        result.ok = false;
        result.error = QStringLiteral("fastboot 执行超时（%1 秒）").arg(timeoutMs / 1000);
    } else {
        result.ok = tool.proc.exitCode == 0;
        if (!result.ok)
            result.error = tool.failureText();
    }
    return result;
}

} // namespace

QString FastbootDevice::displayName() const
{
    if (mode.isEmpty())
        return serial;
    return QStringLiteral("%1 (%2)").arg(serial, mode);
}

QVariantMap FastbootDevice::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("serial"), serial);
    map.insert(QStringLiteral("mode"), mode);
    map.insert(QStringLiteral("isFastbootD"), isFastbootD());
    map.insert(QStringLiteral("displayName"), displayName());
    return map;
}

QList<FastbootDevice> parseFastbootDevices(const QString& output)
{
    QList<FastbootDevice> devices;
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        const QString lower = line.toLower();
        if (lower.startsWith(QStringLiteral("fastboot:")) || lower.startsWith(QStringLiteral("error:"))
            || lower.startsWith(QStringLiteral("warning:")) || lower.startsWith(QStringLiteral("< waiting"))
            || lower.contains(QStringLiteral("no permissions")))
            continue;
        const QStringList parts = line.split(whitespace, Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        if (parts.size() == 1 && !line.contains(QLatin1Char('\t')))
            continue; // never a valid `fastboot devices` row
        FastbootDevice device;
        device.serial = parts.first();
        device.mode = parts.value(1);
        if (device.mode.isEmpty())
            device.mode = QStringLiteral("fastboot");
        devices.append(device);
    }
    return devices;
}

QMap<QString, QString> parseGetvarOutput(const QString& output)
{
    QMap<QString, QString> vars;
    // fastboot getvar encodes some variables with a nested key, e.g.
    // "partition-size:boot:0x04000000". Keep those keys intact.
    static const QSet<QString> nestedKeys = {
        QStringLiteral("partition-size"), QStringLiteral("partition-type"),
        QStringLiteral("has-slot"),      QStringLiteral("is-logical"),
        QStringLiteral("slot-successful"), QStringLiteral("slot-unbootable"),
        QStringLiteral("slot-retry-count"), QStringLiteral("slot-count"),
        QStringLiteral("slot-suffix"),
    };
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("(bootloader) ")))
            line = line.mid(13).trimmed();
        if (line.startsWith(QStringLiteral("Finished.")) || line.startsWith(QStringLiteral("Sending"))
            || line.startsWith(QStringLiteral("OKAY")) || line.startsWith(QStringLiteral("FAILED")))
            continue;
        // "getvar:all FAILED (remote: ...)" (transport errors include a colon,
        // so it must be rejected before the generic key/value split).
        if (line.contains(QStringLiteral("FAILED")))
            continue;
        const int firstColon = line.indexOf(QLatin1Char(':'));
        if (firstColon <= 0)
            continue;
        const QString head = line.left(firstColon).trimmed();
        const QString rest = line.mid(firstColon + 1);
        QString key = head;
        QString value;
        if (nestedKeys.contains(head) && rest.contains(QLatin1Char(':'))) {
            const int secondColon = rest.indexOf(QLatin1Char(':'));
            key = head + QLatin1Char(':') + rest.left(secondColon).trimmed();
            value = rest.mid(secondColon + 1).trimmed();
        } else {
            value = rest.trimmed();
        }
        if (key.isEmpty() || key.contains(QLatin1Char(' ')))
            continue;
        vars.insert(key, value);
    }
    return vars;
}

QList<FastbootPartition> parsePartitionTable(const QMap<QString, QString>& vars)
{
    QMap<QString, FastbootPartition> byName;
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        const QString& key = it.key();
        const QString& value = it.value();
        auto ensure = [&byName](const QString& name) -> FastbootPartition& {
            return byName[name];
        };
        if (key.startsWith(QStringLiteral("partition-size:"))) {
            const QString name = key.mid(15);
            ensure(name).size = value;
        } else if (key.startsWith(QStringLiteral("partition-type:"))) {
            const QString name = key.mid(15);
            ensure(name).type = value;
        } else if (key.startsWith(QStringLiteral("has-slot:"))) {
            const QString name = key.mid(9);
            ensure(name).slot = value;
        } else if (key.startsWith(QStringLiteral("is-logical:"))) {
            const QString name = key.mid(11);
            ensure(name).logical = value.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
        }
    }
    QList<FastbootPartition> partitions;
    for (auto it = byName.constBegin(); it != byName.constEnd(); ++it) {
        FastbootPartition partition = it.value();
        partition.name = it.key();
        partitions.append(partition);
    }
    std::sort(partitions.begin(), partitions.end(),
              [](const FastbootPartition& a, const FastbootPartition& b) { return a.name < b.name; });
    return partitions;
}

QVariantMap FastbootPartition::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), name);
    map.insert(QStringLiteral("size"), size);
    map.insert(QStringLiteral("type"), type);
    map.insert(QStringLiteral("slot"), slot);
    map.insert(QStringLiteral("logical"), logical);
    return map;
}

bool FastbootResult::looksLikeMissingDevice() const
{
    const QString text = (error + QLatin1Char(' ') + output).toLower();
    return text.contains(QStringLiteral("no devices"))
           || text.contains(QStringLiteral("waiting for device"))
           || text.contains(QStringLiteral("no permissions"))
           || text.contains(QStringLiteral("more than one device"))
           || text.contains(QStringLiteral("device not found"));
}

bool FastbootClient::available()
{
    return locateTool(QStringLiteral("fastboot")).isValid();
}

QString FastbootClient::fastbootPath()
{
    return locateTool(QStringLiteral("fastboot")).path;
}

FastbootResult FastbootClient::run(const QStringList& arguments, int timeoutMs)
{
    return toResult(runFastboot(arguments, timeoutMs), timeoutMs);
}

FastbootResult FastbootClient::runFor(const QString& serial, const QStringList& arguments,
                                      int timeoutMs)
{
    QStringList args;
    if (!serial.trimmed().isEmpty())
        args << QStringLiteral("-s") << serial.trimmed();
    args << arguments;
    return run(args, timeoutMs);
}

QList<FastbootDevice> FastbootClient::devices(QString* error)
{
    const FastbootResult result = run({QStringLiteral("devices")}, 60000);
    if (!result.ok) {
        if (error)
            *error = result.error.isEmpty() ? noFastbootDeviceMessage() : result.error;
        return {};
    }
    if (error)
        error->clear();
    return parseFastbootDevices(result.output);
}

QString FastbootClient::resolveSerial(const QString& requested, QString* error)
{
    const QString trimmed = requested.trimmed();
    if (!trimmed.isEmpty()) {
        if (error)
            error->clear();
        return trimmed;
    }
    QString deviceError;
    const auto all = devices(&deviceError);
    if (all.isEmpty()) {
        if (error)
            *error = deviceError.isEmpty() ? noFastbootDeviceMessage() : deviceError;
        return {};
    }
    if (all.size() > 1) {
        QStringList serials;
        for (const auto& device : all)
            serials.append(device.serial);
        if (error)
            *error = QStringLiteral("检测到多台 Fastboot 设备，请在「序列号」中指定一台：%1")
                         .arg(serials.join(QStringLiteral("、")));
        return {};
    }
    if (error)
        error->clear();
    return all.first().serial;
}

QString FastbootClient::summarizeDevices(const QList<FastbootDevice>& devices)
{
    if (devices.isEmpty())
        return noFastbootDeviceMessage();
    QStringList lines;
    lines.append(QStringLiteral("检测到 %1 台 Fastboot 设备：").arg(devices.size()));
    for (const auto& device : devices)
        lines.append(QStringLiteral("  • %1").arg(device.displayName()));
    return lines.join(QLatin1Char('\n'));
}

FastbootResult FastbootClient::getvar(const QString& serial, const QString& key)
{
    return runFor(serial, {QStringLiteral("getvar"), key}, 60000);
}

QMap<QString, QString> FastbootClient::getvarAll(const QString& serial, QString* error)
{
    const FastbootResult result = runFor(serial, {QStringLiteral("getvar"), QStringLiteral("all")},
                                         120000);
    // fastboot prints getvar answers on stderr: parse stdout and stderr together.
    const QMap<QString, QString> vars =
        parseGetvarOutput(result.output + QLatin1Char('\n') + result.error);
    if (!vars.isEmpty() && !result.ok && vars.size() < 2) {
        if (error)
            *error = result.error.isEmpty() ? QStringLiteral("读取 Fastboot 变量失败") : result.error;
        return {};
    }
    if (vars.isEmpty()) {
        if (error)
            *error = result.error.isEmpty()
                         ? QStringLiteral("读取 Fastboot 变量失败，请确认设备处于 Fastboot 模式。")
                         : result.error;
        return {};
    }
    if (error)
        error->clear();
    return vars;
}

QList<FastbootPartition> FastbootClient::partitions(const QString& serial, QString* error)
{
    QString varError;
    const auto vars = getvarAll(serial, &varError);
    if (vars.isEmpty()) {
        if (error)
            *error = varError;
        return {};
    }
    const auto table = parsePartitionTable(vars);
    if (table.isEmpty() && error)
        *error = QStringLiteral("设备未报告任何分区信息（getvar all 中没有 partition-size 项）。");
    return table;
}

bool FastbootClient::partitionSize(const QString& serial, const QString& name, quint64* size,
                                   QString* error)
{
    if (!size)
        return false;
    *size = 0;
    const QString key = QStringLiteral("partition-size:") + name;
    const FastbootResult result = runFor(serial, {QStringLiteral("getvar"),
                                                  QStringLiteral("partition-size:") + name},
                                         15000);
    const QMap<QString, QString> vars =
        parseGetvarOutput(result.output + QLatin1Char('\n') + result.error);
    const quint64 parsed = partitionSizeFromVars(vars, name);
    if (parsed == 0) {
        if (error)
            *error = QStringLiteral("设备上找不到分区 %1").arg(name);
        return false;
    }
    *size = parsed;
    return true;
}

FastbootResult FastbootClient::flash(const QString& serial, const QString& partition,
                                     const QString& image, int timeoutMs)
{
    if (!QFileInfo::exists(image))
        return {false, -1, {}, QStringLiteral("镜像文件不存在：%1").arg(image)};
    return runFor(serial,
                  {QStringLiteral("flash"), partition, QDir::toNativeSeparators(image)},
                  timeoutMs);
}

FastbootResult FastbootClient::erase(const QString& serial, const QString& partition)
{
    return runFor(serial, {QStringLiteral("erase"), partition}, 120000);
}

FastbootResult FastbootClient::format(const QString& serial, const QString& partition,
                                      const QString& filesystem)
{
    return runFor(serial,
                  {QStringLiteral("format"), partition, filesystem}, 300000);
}

FastbootResult FastbootClient::reboot(const QString& serial, const QString& mode)
{
    // The UI passes display labels such as "system — 重启到系统", so normalize
    // before building the fastboot argument list.
    const QString id = normalizeRebootMode(mode);
    if (id == QLatin1String("edl") || id == QLatin1String("brom"))
        return oemCommand(serial, QStringLiteral("edl"));
    if (id == QLatin1String("poweroff") || id == QLatin1String("off"))
        return oemCommand(serial, QStringLiteral("poweroff"));
    QStringList args{QStringLiteral("reboot")};
    if (id == QLatin1String("system")) {
        // plain reboot
    } else if (id == QLatin1String("bootloader") || id == QLatin1String("fastboot")) {
        args << QStringLiteral("bootloader");
    } else if (id == QLatin1String("fastbootd")) {
        args << QStringLiteral("fastboot");
    } else if (!id.isEmpty()) {
        args << id;
    }
    return runFor(serial, args, 60000);
}

FastbootResult FastbootClient::rebootBootloader(const QString& serial)
{
    return runFor(serial, {QStringLiteral("reboot-bootloader")}, 60000);
}

FastbootResult FastbootClient::flashingLock(const QString& serial, bool lock)
{
    return runFor(serial,
                  {QStringLiteral("flashing"), lock ? QStringLiteral("lock")
                                                    : QStringLiteral("unlock")},
                  60000);
}

QString FastbootClient::batteryIssueFromVars(const QMap<QString, QString>& vars)
{
    if (vars.isEmpty())
        return {};
    const QString socOk = vars.value(QStringLiteral("battery-soc-ok")).trimmed().toLower();
    bool voltageOk = false;
    const int voltage = vars.value(QStringLiteral("battery-voltage")).trimmed().toInt(&voltageOk);
    if (socOk == QLatin1String("no")) {
        return QStringLiteral("电池电量状态异常（battery-soc-ok=no，电压 %1 mV），刷写/重启可能中途断电")
            .arg(voltageOk ? QString::number(voltage) : QStringLiteral("未知"));
    }
    if (voltageOk && voltage > 0 && voltage < 3600) {
        return QStringLiteral("电池电压偏低（%1 mV），建议先充电再刷写").arg(voltage);
    }
    return {};
}

QString FastbootClient::batteryIssue(const QString& serial)
{
    return batteryIssueFromVars(getvarAll(serial));
}

quint64 FastbootClient::partitionSizeFromVars(const QMap<QString, QString>& vars,
                                              const QString& name)
{
    const QString value = vars.value(QStringLiteral("partition-size:") + name);
    bool ok = false;
    const quint64 parsed = value.toULongLong(&ok, 0);
    return ok ? parsed : 0;
}

QString FastbootClient::normalizeRebootMode(const QString& mode)
{
    const QString raw = mode.trimmed();
    const QString lower = raw.toLower();
    if (lower.contains(QStringLiteral("fastbootd")))
        return QStringLiteral("fastbootd");
    if (lower.contains(QStringLiteral("bootloader")))
        return QStringLiteral("bootloader");
    if (lower.contains(QStringLiteral("recovery")))
        return QStringLiteral("recovery");
    if (lower.contains(QStringLiteral("edl")) || lower.contains(QStringLiteral("9008")))
        return QStringLiteral("edl");
    if (lower.contains(QStringLiteral("poweroff")) || raw.contains(QStringLiteral("关机")))
        return QStringLiteral("poweroff");
    if (lower.contains(QStringLiteral("system")) || raw.contains(QStringLiteral("系统")))
        return QStringLiteral("system");
    return lower.section(QLatin1Char(' '), 0, 0);
}

FastbootResult FastbootClient::oemCommand(const QString& serial, const QString& command)
{
    return runFor(serial, {QStringLiteral("oem"), command}, 120000);
}

bool FastbootClient::exportInfo(const QString& serial, const QString& outDir,
                                QString* savedPath, QString* error)
{
    QString varError;
    const auto vars = getvarAll(serial, &varError);
    if (vars.isEmpty()) {
        if (error)
            *error = varError.isEmpty() ? noFastbootDeviceMessage() : varError;
        return false;
    }
    QString dir = outDir.trimmed();
    if (dir.isEmpty())
        dir = util::appDataDir() + QStringLiteral("/fastboot_info");
    if (!util::ensureDir(dir)) {
        if (error)
            *error = QStringLiteral("无法创建输出目录：%1").arg(dir);
        return false;
    }
    QString safeSerial = serial;
    safeSerial.replace(QRegularExpression(QStringLiteral("[^0-9A-Za-z._-]")), QStringLiteral("_"));
    const QString path = util::uniquePath(
        QStringLiteral("%1/fastboot_info_%2.txt").arg(dir, safeSerial));
    QStringList lines;
    lines.append(QStringLiteral("Fastboot 设备信息"));
    lines.append(QStringLiteral("序列号: %1").arg(serial));
    lines.append(QStringLiteral("读取时间: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    lines.append(QString());
    lines.append(QStringLiteral("== 分区表 =="));
    for (const auto& partition : parsePartitionTable(vars)) {
        lines.append(QStringLiteral("%1\tsize=%2\ttype=%3%4")
                         .arg(partition.name, partition.size, partition.type,
                              partition.logical ? QStringLiteral("\tlogical") : QString()));
    }
    lines.append(QString());
    lines.append(QStringLiteral("== getvar all =="));
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        lines.append(QStringLiteral("%1: %2").arg(it.key(), it.value()));
    if (!util::writeFile(path, (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8())) {
        if (error)
            *error = QStringLiteral("写入 Fastboot 信息文件失败：%1").arg(path);
        return false;
    }
    if (savedPath)
        *savedPath = path;
    if (error)
        error->clear();
    return true;
}

} // namespace gf::device
