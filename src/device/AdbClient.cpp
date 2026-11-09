#include "AdbClient.h"

#include "PlatformTools.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "core/Util.h"

namespace gf::device {

namespace {

QString cleanOutput(const QString& text)
{
    return text.trimmed();
}

} // namespace

QString AdbDevice::displayName() const
{
    QString name = model;
    if (name.isEmpty())
        name = product;
    if (name.isEmpty())
        name = device;
    if (name.isEmpty())
        return serial;
    return QStringLiteral("%1 (%2)").arg(name, serial);
}

QVariantMap AdbDevice::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("serial"), serial);
    map.insert(QStringLiteral("state"), state);
    map.insert(QStringLiteral("product"), product);
    map.insert(QStringLiteral("model"), model);
    map.insert(QStringLiteral("device"), device);
    map.insert(QStringLiteral("transportId"), transportId);
    map.insert(QStringLiteral("usb"), usb);
    map.insert(QStringLiteral("online"), isOnline());
    map.insert(QStringLiteral("displayName"), displayName());
    return map;
}

QList<AdbDevice> parseAdbDevices(const QString& output)
{
    QList<AdbDevice> devices;
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    static const QStringList knownStates = {
        QStringLiteral("device"), QStringLiteral("offline"), QStringLiteral("unauthorized"),
        QStringLiteral("recovery"), QStringLiteral("sideload"), QStringLiteral("bootloader"),
        QStringLiteral("host"), QStringLiteral("no"), QStringLiteral("connecting"),
        QStringLiteral("authorizing"), QStringLiteral("rescue"),
    };

    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        const QString lower = line.toLower();
        if (lower.startsWith(QStringLiteral("list of devices")))
            continue;
        if (line.startsWith(QLatin1Char('*')))
            continue; // daemon / server banner
        if (lower.startsWith(QStringLiteral("adb:")) || lower.startsWith(QStringLiteral("error:"))
            || lower.startsWith(QStringLiteral("warning:"))
            || lower.startsWith(QStringLiteral("adb server")) || lower.startsWith(QStringLiteral("daemon")))
            continue;

        const QStringList parts = line.split(whitespace, Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        AdbDevice device;
        device.serial = parts.at(0);
        device.state = parts.at(1);
        if (device.serial.compare(QStringLiteral("adb"), Qt::CaseInsensitive) == 0
            && device.state.compare(QStringLiteral("server"), Qt::CaseInsensitive) == 0)
            continue;
        if (!knownStates.contains(device.state, Qt::CaseInsensitive)
            && !device.state.contains(QStringLiteral("permission"), Qt::CaseInsensitive))
            continue;

        for (int i = 2; i < parts.size(); ++i) {
            const QString& token = parts.at(i);
            const int colon = token.indexOf(QLatin1Char(':'));
            if (colon <= 0)
                continue;
            const QString key = token.left(colon);
            const QString value = token.mid(colon + 1);
            if (key == QLatin1String("product"))
                device.product = value;
            else if (key == QLatin1String("model"))
                device.model = value;
            else if (key == QLatin1String("device"))
                device.device = value;
            else if (key == QLatin1String("transport_id"))
                device.transportId = value;
            else if (key == QLatin1String("usb"))
                device.usb = value;
        }
        devices.append(device);
    }
    return devices;
}

QMap<QString, QString> parseGetpropOutput(const QString& output)
{
    QMap<QString, QString> props;
    static const QRegularExpression re(QStringLiteral("^\\[([^\\]]+)\\]\\s*:\\s*\\[(.*)\\]\\s*$"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        const QRegularExpressionMatch match = re.match(line);
        if (!match.hasMatch())
            continue;
        props.insert(match.captured(1), match.captured(2));
    }
    return props;
}

QList<PartitionLink> parseByNameLinks(const QString& output)
{
    QList<PartitionLink> links;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("total")))
            continue;
        const int arrowPos = line.indexOf(QStringLiteral("->"));
        if (arrowPos > 0) {
            // Covers both `ls -l` lines ("... boot -> /dev/block/sde11") and the
            // bare "boot -> /dev/block/sde11" form.
            const QString left = line.left(arrowPos).trimmed();
            const QString right = line.mid(arrowPos + 2).trimmed();
            PartitionLink link;
            link.name = left.section(QLatin1Char(' '), -1).trimmed();
            link.target = right.section(QLatin1Char(' '), 0, 0).trimmed();
            link.detail = line;
            if (!link.name.isEmpty())
                links.append(link);
            continue;
        }
        const QStringList parts =
            line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        if (parts.size() == 1) {
            // `ls /dev/block/by-name` prints one bare partition name per line.
            PartitionLink link;
            link.name = parts.first();
            link.detail = line;
            links.append(link);
            continue;
        }
        const QString last = parts.last();
        if (last.startsWith(QStringLiteral("/dev/block/")) && parts.size() >= 2) {
            PartitionLink link;
            link.name = parts.at(parts.size() - 2);
            link.target = last;
            link.detail = line;
            links.append(link);
        }
    }
    return links;
}

bool AdbResult::looksLikeMissingDevice() const
{
    const QString text = (error + QLatin1Char(' ') + output).toLower();
    return text.contains(QStringLiteral("no devices"))
           || text.contains(QStringLiteral("no emulators"))
           || text.contains(QStringLiteral("device not found"))
           || text.contains(QStringLiteral("device offline"))
           || text.contains(QStringLiteral("more than one device"))
           || text.contains(QStringLiteral("unauthorized"));
}

bool AdbClient::available()
{
    return locateTool(QStringLiteral("adb")).isValid();
}

QString AdbClient::adbPath()
{
    return locateTool(QStringLiteral("adb")).path;
}

AdbResult AdbClient::run(const QStringList& arguments, int timeoutMs)
{
    const ToolRunResult tool = runAdb(arguments, timeoutMs);
    AdbResult result;
    result.exitCode = tool.proc.exitCode;
    result.output = cleanOutput(QString::fromLocal8Bit(tool.combined()));
    if (!tool.error.isEmpty()) {
        result.error = tool.error;
        result.ok = false;
    } else if (tool.proc.timedOut) {
        result.error = QStringLiteral("adb 执行超时（%1 秒）").arg(timeoutMs / 1000);
        result.ok = false;
    } else {
        result.ok = tool.proc.exitCode == 0;
        if (!result.ok)
            result.error = tool.failureText();
    }
    return result;
}

AdbResult AdbClient::runFor(const QString& serial, const QStringList& arguments, int timeoutMs)
{
    QStringList args;
    if (!serial.trimmed().isEmpty())
        args << QStringLiteral("-s") << serial.trimmed();
    args << arguments;
    return run(args, timeoutMs);
}

QList<AdbDevice> AdbClient::devices(QString* error)
{
    AdbResult result = run({QStringLiteral("devices"), QStringLiteral("-l")}, 60000);
    if (!result.ok) {
        // The daemon may not be running yet; start it once and retry.
        run({QStringLiteral("start-server")}, 60000);
        result = run({QStringLiteral("devices"), QStringLiteral("-l")}, 60000);
    }
    if (!result.ok) {
        if (error)
            *error = result.error.isEmpty() ? noAdbDeviceMessage() : result.error;
        return {};
    }
    if (error)
        error->clear();
    return parseAdbDevices(result.output);
}

QList<AdbDevice> AdbClient::onlineDevices(QString* error)
{
    QList<AdbDevice> all = devices(error);
    QList<AdbDevice> online;
    for (const auto& device : all) {
        if (device.isOnline())
            online.append(device);
    }
    return online;
}

QStringList AdbClient::deviceSerials()
{
    QStringList serials;
    const auto online = onlineDevices();
    for (const auto& device : online)
        serials.append(device.serial);
    return serials;
}

AdbResult AdbClient::startServer()
{
    return run({QStringLiteral("start-server")}, 60000);
}

AdbResult AdbClient::killServer()
{
    return run({QStringLiteral("kill-server")}, 30000);
}

AdbResult AdbClient::shell(const QString& serial, const QString& command, int timeoutMs)
{
    return runFor(serial, {QStringLiteral("shell"), command}, timeoutMs);
}

AdbResult AdbClient::execOut(const QString& serial, const QString& command, int timeoutMs)
{
    return runFor(serial, {QStringLiteral("exec-out"), command}, timeoutMs);
}

AdbResult AdbClient::push(const QString& serial, const QString& local, const QString& remote,
                          int timeoutMs)
{
    return runFor(serial, {QStringLiteral("push"), local, remote}, timeoutMs);
}

AdbResult AdbClient::sideload(const QString& serial, const QString& package, int timeoutMs)
{
    if (package.isEmpty() || !QFileInfo::exists(package)) {
        AdbResult result;
        result.ok = false;
        result.error = QStringLiteral("安装包不存在：%1").arg(package);
        return result;
    }
    return runFor(serial, {QStringLiteral("sideload"), package}, timeoutMs);
}

AdbResult AdbClient::waitForDevice(const QString& serial, int timeoutMs)
{
    if (serial.trimmed().isEmpty())
        return run({QStringLiteral("wait-for-device")}, timeoutMs);
    return runFor(serial, {QStringLiteral("wait-for-device")}, timeoutMs);
}

AdbResult AdbClient::pull(const QString& serial, const QString& remote, const QString& local,
                          int timeoutMs)
{
    return runFor(serial, {QStringLiteral("pull"), remote, local}, timeoutMs);
}

QString AdbClient::rebootArgument(const QString& mode)
{
    const QString id = mode.trimmed().toLower();
    if (id.isEmpty() || id == QLatin1String("system") || id == QLatin1String("android"))
        return {};
    if (id == QLatin1String("fastboot"))
        return QStringLiteral("bootloader");
    if (id == QLatin1String("fastbootd"))
        return QStringLiteral("fastboot");
    if (id == QLatin1String("edl"))
        return QStringLiteral("edl");
    if (id == QLatin1String("brom"))
        return QStringLiteral("brom");
    return id;
}

AdbResult AdbClient::reboot(const QString& serial, const QString& mode)
{
    QStringList args{QStringLiteral("reboot")};
    const QString argument = rebootArgument(mode);
    if (!argument.isEmpty())
        args << argument;
    return runFor(serial, args, 60000);
}

AdbResult AdbClient::enableTcpip(const QString& serial, int port)
{
    return runFor(serial, {QStringLiteral("tcpip"), QString::number(port)}, 60000);
}

AdbResult AdbClient::connectNetwork(const QString& address, int port)
{
    return run({QStringLiteral("connect"), QStringLiteral("%1:%2").arg(address).arg(port)}, 60000);
}

AdbResult AdbClient::disconnectNetwork(const QString& address, int port)
{
    return run({QStringLiteral("disconnect"), QStringLiteral("%1:%2").arg(address).arg(port)}, 30000);
}

AdbResult AdbClient::forward(const QString& serial, const QString& local, int remotePort)
{
    const QString localSpec = local.startsWith(QStringLiteral("tcp:")) ? local
                                                                       : QStringLiteral("tcp:") + local;
    return runFor(serial,
                  {QStringLiteral("forward"), localSpec,
                   QStringLiteral("tcp:%1").arg(remotePort)},
                  30000);
}

AdbResult AdbClient::forwardRemove(const QString& serial, const QString& local)
{
    return runFor(serial, {QStringLiteral("forward"), QStringLiteral("--remove"), local}, 30000);
}

AdbResult AdbClient::forwardList(const QString& serial)
{
    return runFor(serial, {QStringLiteral("forward"), QStringLiteral("--list")}, 30000);
}

AdbResult AdbClient::getProp(const QString& serial, const QString& key)
{
    return shell(serial, QStringLiteral("getprop %1").arg(key), 30000);
}

QMap<QString, QString> AdbClient::allProps(const QString& serial, QString* error)
{
    const AdbResult result = shell(serial, QStringLiteral("getprop"), 60000);
    if (!result.ok) {
        if (error)
            *error = result.error.isEmpty() ? noAdbDeviceMessage() : result.error;
        return {};
    }
    const auto props = parseGetpropOutput(result.output);
    if (props.isEmpty() && error)
        *error = QStringLiteral("读取设备属性失败，请确认设备已开启 USB 调试并处于在线状态。");
    return props;
}

QList<PartitionLink> AdbClient::partitions(const QString& serial, QString* error)
{
    static const QStringList candidates = {
        QStringLiteral("ls -l /dev/block/by-name"),
        QStringLiteral("ls /dev/block/by-name"),
        QStringLiteral("ls -l /dev/block/bootdevice/by-name"),
        QStringLiteral("ls -l /dev/block/platform/soc/7824900.sdhci/by-name"),
    };
    QString lastError;
    for (const QString& command : candidates) {
        const AdbResult result = shell(serial, command, 30000);
        if (!result.ok) {
            lastError = result.error;
            continue;
        }
        const auto links = parseByNameLinks(result.output);
        if (!links.isEmpty()) {
            if (error)
                error->clear();
            return links;
        }
    }
    if (error)
        *error = lastError.isEmpty()
                     ? QStringLiteral("无法读取分区表：设备上未找到 /dev/block/by-name。")
                     : QStringLiteral("无法读取分区表：%1").arg(lastError);
    return {};
}

AdbResult AdbClient::logcat(const QString& serial, int lines, int timeoutMs)
{
    return shell(serial, QStringLiteral("logcat -d -t %1").arg(lines), timeoutMs);
}

AdbResult AdbClient::dmesg(const QString& serial)
{
    return shell(serial, QStringLiteral("dmesg"), 60000);
}

AdbResult AdbClient::bugreport(const QString& serial, const QString& outputPath)
{
    const QString dir = QFileInfo(outputPath).absolutePath();
    util::ensureDir(dir);
    return runFor(serial, {QStringLiteral("bugreport"), QDir::toNativeSeparators(outputPath)},
                  600000);
}

QString AdbClient::resolveSerial(const QString& requested, QString* error)
{
    const QString trimmed = requested.trimmed();
    if (!trimmed.isEmpty()) {
        if (error)
            error->clear();
        return trimmed;
    }
    QString deviceError;
    const auto online = onlineDevices(&deviceError);
    if (online.isEmpty()) {
        if (error)
            *error = deviceError.isEmpty() ? noAdbDeviceMessage() : deviceError;
        return {};
    }
    if (online.size() > 1) {
        QStringList serials;
        for (const auto& device : online)
            serials.append(device.serial);
        if (error)
            *error = QStringLiteral("检测到多台 ADB 设备，请在「序列号」中指定一台：%1")
                         .arg(serials.join(QStringLiteral("、")));
        return {};
    }
    if (error)
        error->clear();
    return online.first().serial;
}

QString AdbClient::summarizeDevices(const QList<AdbDevice>& devices)
{
    if (devices.isEmpty())
        return noAdbDeviceMessage();
    QStringList lines;
    lines.append(QStringLiteral("检测到 %1 台 ADB 设备：").arg(devices.size()));
    for (const auto& device : devices) {
        const QString suffix = device.isOnline() ? QString()
                                                 : QStringLiteral("（状态：%1）").arg(device.state);
        lines.append(QStringLiteral("  • %1%2").arg(device.displayName(), suffix));
    }
    return lines.join(QLatin1Char('\n'));
}

QStringList AdbClient::DeviceInfo::toLines() const
{
    QStringList lines;
    lines.append(QStringLiteral("序列号    : %1").arg(serial));
    lines.append(QStringLiteral("厂商      : %1").arg(brand));
    lines.append(QStringLiteral("型号      : %1").arg(model));
    lines.append(QStringLiteral("设备代号  : %1").arg(device));
    lines.append(QStringLiteral("制造商    : %1").arg(manufacturer));
    lines.append(QStringLiteral("Android   : %1 (SDK %2)").arg(androidVersion, sdk));
    lines.append(QStringLiteral("CPU ABI   : %1").arg(abi));
    lines.append(QStringLiteral("Bootloader: %1").arg(bootloader));
    lines.append(QStringLiteral("基带      : %1").arg(baseband));
    lines.append(QStringLiteral("指纹      : %1").arg(fingerprint));
    return lines;
}

QVariantMap AdbClient::DeviceInfo::toVariant() const
{
    QVariantMap map;
    map.insert(QStringLiteral("serial"), serial);
    map.insert(QStringLiteral("brand"), brand);
    map.insert(QStringLiteral("model"), model);
    map.insert(QStringLiteral("device"), device);
    map.insert(QStringLiteral("manufacturer"), manufacturer);
    map.insert(QStringLiteral("androidVersion"), androidVersion);
    map.insert(QStringLiteral("sdk"), sdk);
    map.insert(QStringLiteral("fingerprint"), fingerprint);
    map.insert(QStringLiteral("abi"), abi);
    map.insert(QStringLiteral("bootloader"), bootloader);
    map.insert(QStringLiteral("baseband"), baseband);
    return map;
}

AdbClient::DeviceInfo AdbClient::deviceInfo(const QString& serial, QString* error)
{
    DeviceInfo info;
    info.serial = serial;
    QString propsError;
    const QMap<QString, QString> props = allProps(serial, &propsError);
    if (props.isEmpty()) {
        if (error)
            *error = propsError.isEmpty() ? noAdbDeviceMessage() : propsError;
        return info;
    }
    auto value = [&props](const char* key) {
        return props.value(QString::fromLatin1(key));
    };
    info.model = value("ro.product.model");
    info.brand = value("ro.product.brand");
    info.device = value("ro.product.device");
    info.manufacturer = value("ro.product.manufacturer");
    info.androidVersion = value("ro.build.version.release");
    info.sdk = value("ro.build.version.sdk");
    info.fingerprint = value("ro.build.fingerprint");
    info.abi = value("ro.product.cpu.abi");
    info.bootloader = value("ro.bootloader");
    if (info.bootloader.isEmpty())
        info.bootloader = value("ro.boot.bootloader");
    info.baseband = value("gsm.version.baseband");
    if (info.baseband.isEmpty())
        info.baseband = value("ro.boot.baseband");
    if (error)
        error->clear();
    return info;
}

bool AdbClient::exportDeviceInfo(const QString& serial, const QString& outDir,
                                 QString* savedPath, QString* error)
{
    const DeviceInfo info = deviceInfo(serial, error);
    if (info.model.isEmpty() && info.fingerprint.isEmpty())
        return false;
    QString dir = outDir.trimmed();
    if (dir.isEmpty())
        dir = util::appDataDir() + QStringLiteral("/device_info");
    if (!util::ensureDir(dir)) {
        if (error)
            *error = QStringLiteral("无法创建输出目录：%1").arg(dir);
        return false;
    }
    QString safeSerial = serial;
    safeSerial.replace(QRegularExpression(QStringLiteral("[^0-9A-Za-z._-]")), QStringLiteral("_"));
    const QString path = util::uniquePath(
        QStringLiteral("%1/device_info_%2.txt").arg(dir, safeSerial));
    const QString text = info.toLines().join(QLatin1Char('\n')) + QLatin1Char('\n');
    if (!util::writeFile(path, text.toUtf8())) {
        if (error)
            *error = QStringLiteral("写入设备信息文件失败：%1").arg(path);
        return false;
    }
    if (savedPath)
        *savedPath = path;
    if (error)
        error->clear();
    return true;
}

} // namespace gf::device
