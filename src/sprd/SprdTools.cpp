#include "SprdTools.h"

#include <QFileInfo>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <stdexcept>

#include "../app/ToolModules.h"
#include "Fdl.h"
#include "Pac.h"
#include "TaskManager.h"
#include "ToolRegistry.h"
#include "U2s.h"
#include "Util.h"
#include "usb/ProtocolSupport.h"
#include "usb/TaskReporter.h"
#include "usb/ToolOptions.h"
#include "usb/UsbTransport.h"

namespace gf {

namespace {

using usb::ChipPlatform;

QString deviceOption(const QVariantMap& options)
{
    return options.value(QStringLiteral("device")).toString().trimmed();
}

std::unique_ptr<usb::IUsbTransport> openSprd(const QVariantMap& options,
                                             const proto::Reporter& reporter)
{
    reporter.progress(5, QStringLiteral("查找展锐 U2S 端口…"));
    return proto::openTransport(ChipPlatform::Spreadtrum, deviceOption(options), reporter);
}

QString pacImageName(const sprd::PacInfo& info, int index)
{
    const auto& image = info.images[index];
    QString name = image.name;
    if (name.isEmpty())
        name = QStringLiteral("%1_%2").arg(sprd::pacImageTypeName(image.type)).arg(index);
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    name.replace(QLatin1Char(':'), QLatin1Char('_'));
    return name;
}

void scanPorts(TaskContext& ctx, const QVariantMap& options)
{
    Q_UNUSED(options);
    int matched = 0;
    for (const auto& port : usb::enumerateFlashPorts()) {
        if (usb::platformForHint(port.chipHint) != ChipPlatform::Spreadtrum)
            continue;
        ++matched;
        ctx.info(QStringLiteral("发现展锐端口：VID=%1 PID=%2 %3\n  接口：%4")
                     .arg(port.vid, 4, 16, QLatin1Char('0'))
                     .arg(port.pid, 4, 16, QLatin1Char('0'))
                     .arg(port.description)
                     .arg(port.devicePath));
    }
    if (matched == 0)
        ctx.warn(proto::noDeviceMessage(ChipPlatform::Spreadtrum));
    else
        ctx.ok(QStringLiteral("共检测到 %1 个展锐下载端口").arg(matched));
}

void u2sHandshake(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        auto transport = openSprd(options, reporter);
        sprd::fdl::Client client(*transport, reporter);
        client.connect();
        reporter.progress(70, QStringLiteral("读取 FDL 版本…"));
        const QString version = client.readVersion();
        ctx.info(QStringLiteral("FDL 版本：%1").arg(version));
        try {
            const quint32 chip = client.readChipType();
            ctx.info(QStringLiteral("芯片类型：0x%1").arg(chip, 8, 16, QLatin1Char('0')));
        } catch (const std::exception& error) {
            ctx.warn(QStringLiteral("读取芯片类型失败（部分 FDL 不支持）：%1")
                         .arg(QString::fromUtf8(error.what())));
        }
        reporter.progress(100);
        ctx.ok(QStringLiteral("展锐 U2S 握手成功"));
    });
}

void fdlLoad(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString fdl1Path = options.value(QStringLiteral("fdl1")).toString().trimmed();
        const QString fdl2Path = options.value(QStringLiteral("fdl2")).toString().trimmed();
        if (fdl1Path.isEmpty() || fdl2Path.isEmpty())
            throw std::runtime_error("请选择 FDL1 与 FDL2 文件");
        const QByteArray fdl1 = util::readFile(fdl1Path);
        const QByteArray fdl2 = util::readFile(fdl2Path);
        if (fdl1.isEmpty() || fdl2.isEmpty()) {
            throw std::runtime_error(
                QStringLiteral("无法读取 FDL 文件：%1")
                    .arg(fdl1.isEmpty() ? fdl1Path : fdl2Path)
                    .toStdString());
        }
        const quint32 fdl1Address =
            quint32(proto::optionUInt(options, QStringLiteral("fdl1_address"), 0x65000800));
        const quint32 fdl2Address =
            quint32(proto::optionUInt(options, QStringLiteral("fdl2_address"), 0x9EFFFE00));
        const int waitMs = proto::optionInt(options, QStringLiteral("wait_ms"), 15000);

        auto transport = openSprd(options, reporter);
        sprd::fdl::Client client(*transport, reporter);
        client.connect();
        reporter.info(QStringLiteral("FDL 版本：%1").arg(client.readVersion()));
        reporter.progress(10, QStringLiteral("下载 FDL1…"));
        client.downloadFdl(fdl1Address, fdl1, 0, 10, 40);

        reporter.progress(50, QStringLiteral("等待 FDL 端口重新枚举（最多 %1 ms）…").arg(waitMs));
        transport->close();
        transport.reset();

        const usb::PortInfo port = sprd::fdl::waitForPort(waitMs, reporter);
        if (port.devicePath.isEmpty()) {
            throw std::runtime_error(
                QStringLiteral("FDL1 启动后未重新枚举到展锐端口（等待 %1 ms）").arg(waitMs).toStdString());
        }
        reporter.info(QStringLiteral("FDL 端口已就绪：%1").arg(port.devicePath));
        transport.reset(usb::createTransport(port));
        if (!transport)
            throw std::runtime_error("当前系统无法创建展锐 USB 传输实例");
        if (!transport->open(port))
            throw std::runtime_error(
                QStringLiteral("打开 FDL 端口失败：%1").arg(transport->errorString()).toStdString());

        sprd::fdl::Client client2(*transport, reporter);
        client2.connect();
        reporter.progress(55, QStringLiteral("下载 FDL2…"));
        client2.downloadFdl(fdl2Address, fdl2, 0, 55, 40);
        reporter.progress(100);
        ctx.ok(QStringLiteral("FDL1/FDL2 加载完成，设备已进入下载模式"));
    });
}

void pacParse(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString path = options.value(QStringLiteral("file")).toString().trimmed();
        if (path.isEmpty())
            throw std::runtime_error("请选择 PAC 文件");
        const QByteArray file = util::readFile(path);
        if (file.isEmpty())
            throw std::runtime_error(QStringLiteral("无法读取 PAC 文件：%1").arg(path).toStdString());

        reporter.progress(30, QStringLiteral("解析 PAC 头与镜像表…"));
        const sprd::PacInfo info = sprd::PacInfo::parse(file);
        if (!info.valid)
            throw std::runtime_error(QStringLiteral("解析失败：%1").arg(info.error).toStdString());

        ctx.info(sprd::describePac(info));
        for (const auto& warning : info.warnings)
            ctx.warn(warning);
        for (int index = 0; index < info.images.size(); ++index) {
            const auto& image = info.images[index];
            ctx.info(QStringLiteral("%1. %2 [%3]：偏移 0x%4，大小 %5 字节")
                         .arg(index)
                         .arg(pacImageName(info, index))
                         .arg(sprd::pacImageTypeName(image.type))
                         .arg(image.offset, 0, 16)
                         .arg(image.size));
        }

        const QString outputDir = options.value(QStringLiteral("output")).toString().trimmed();
        if (!outputDir.isEmpty()) {
            if (!util::ensureDir(outputDir))
                throw std::runtime_error(
                    QStringLiteral("无法创建输出目录：%1").arg(outputDir).toStdString());
            int written = 0;
            for (int index = 0; index < info.images.size(); ++index) {
                reporter.checkCanceled();
                QString error;
                const QByteArray data = info.imageData(file, index, &error);
                if (!error.isEmpty()) {
                    ctx.warn(error);
                    continue;
                }
                const QString target = outputDir + QStringLiteral("/") + pacImageName(info, index);
                if (!util::writeFile(target, data))
                    throw std::runtime_error(
                        QStringLiteral("写入镜像失败：%1").arg(target).toStdString());
                ctx.info(QStringLiteral("已导出：%1（%2 字节）").arg(target).arg(data.size()));
                ++written;
                reporter.progress(30 + int(60.0 * (index + 1) / info.images.size()));
            }
            ctx.info(QStringLiteral("共导出 %1 个镜像").arg(written));
        }
        reporter.progress(100);
        ctx.ok(QStringLiteral("PAC 解析完成：%1 个镜像").arg(info.images.size()));
    });
}

QVariantMap deviceField()
{
    return fields::text(QStringLiteral("device"), QStringLiteral("设备接口（可选）"), QString(),
                        QStringLiteral("留空自动检测展锐 U2S 端口"),
                        QStringLiteral("指定 WinUSB 设备接口路径，留空自动选择"), false);
}

void registerFdlTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("sprd.u2s.handshake");
        spec.category = QStringLiteral("sprd");
        spec.group = QStringLiteral("fdl");
        spec.name = QStringLiteral("U2S 握手");
        spec.desc = QStringLiteral("连接展锐 U2S/FDL，读取 FDL 版本与芯片类型");
        spec.icon = QStringLiteral("layers");
        spec.keywords = {QStringLiteral("u2s"), QStringLiteral("fdl"),
                         QStringLiteral("展锐"), QStringLiteral("握手")};
        spec.fields = {deviceField()};
        spec.order = 10;
        addTool(spec, u2sHandshake);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("sprd.fdl.load");
        spec.category = QStringLiteral("sprd");
        spec.group = QStringLiteral("fdl");
        spec.name = QStringLiteral("加载 FDL1 / FDL2");
        spec.desc = QStringLiteral("依次下载并启动 FDL1、FDL2，进入展锐下载模式");
        spec.icon = QStringLiteral("upload");
        spec.keywords = {QStringLiteral("fdl1"), QStringLiteral("fdl2"),
                         QStringLiteral("加载"), QStringLiteral("展锐")};
        spec.fields = {deviceField(),
                       fields::path(QStringLiteral("fdl1"), QStringLiteral("FDL1 文件"),
                                    QStringLiteral("*.bin *.fdl")),
                       fields::hex(QStringLiteral("fdl1_address"), QStringLiteral("FDL1 地址"),
                                   QStringLiteral("0x65000800"),
                                   QStringLiteral("引导包 Model 的 FDL1Addr")),
                       fields::path(QStringLiteral("fdl2"), QStringLiteral("FDL2 文件"),
                                    QStringLiteral("*.bin *.fdl")),
                       fields::hex(QStringLiteral("fdl2_address"), QStringLiteral("FDL2 地址"),
                                   QStringLiteral("0x9efffe00"),
                                   QStringLiteral("引导包 Model 的 FDL2Addr")),
                       fields::integer(QStringLiteral("wait_ms"),
                                       QStringLiteral("重枚举等待（ms）"), 15000, 1000, 60000)};
        spec.order = 20;
        addTool(spec, fdlLoad);
    }
}

void registerPacTools()
{
    ToolSpec spec;
    spec.id = QStringLiteral("sprd.pac.parse");
    spec.category = QStringLiteral("sprd");
    spec.group = QStringLiteral("pac");
    spec.name = QStringLiteral("PAC 解析 / 解包");
    spec.desc = QStringLiteral("解析展锐 PAC 容器头与镜像表，可导出全部镜像");
    spec.icon = QStringLiteral("file-text");
    spec.keywords = {QStringLiteral("pac"), QStringLiteral("解包"),
                     QStringLiteral("展锐"), QStringLiteral("固件")};
    spec.fields = {fields::path(QStringLiteral("file"), QStringLiteral("PAC 文件"),
                                QStringLiteral("*.pac *.Pac *.bin")),
                   fields::dir(QStringLiteral("output"), QStringLiteral("导出目录（可选）"),
                               QStringLiteral("留空只显示镜像列表"), false)};
    spec.order = 30;
    addTool(spec, pacParse);
}

void registerUtilityTools()
{
    ToolSpec spec;
    spec.id = QStringLiteral("sprd.ports.scan");
    spec.category = QStringLiteral("sprd");
    spec.group = QStringLiteral("tool");
    spec.name = QStringLiteral("检测展锐端口");
    spec.desc = QStringLiteral("扫描并列出当前连接的展锐 U2S/FDL 端口");
    spec.icon = QStringLiteral("usb");
    spec.keywords = {QStringLiteral("端口"), QStringLiteral("扫描"),
                     QStringLiteral("展锐")};
    spec.fields = {};
    spec.order = 100;
    addTool(spec, scanPorts);
}

} // namespace

void registerSprdToolHandlers()
{
    registerFdlTools();
    registerPacTools();
    registerUtilityTools();
}

} // namespace gf
