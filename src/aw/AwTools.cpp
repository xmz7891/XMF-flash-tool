#include "AwTools.h"

#include <QFileInfo>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <stdexcept>

#include "../app/ToolModules.h"
#include "Fel.h"
#include "TaskManager.h"
#include "ToolRegistry.h"
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

std::unique_ptr<usb::IUsbTransport> openFel(const QVariantMap& options,
                                            const proto::Reporter& reporter)
{
    reporter.progress(5, QStringLiteral("查找全志 FEL 端口…"));
    return proto::openTransport(ChipPlatform::Allwinner, deviceOption(options), reporter);
}

void scanPorts(TaskContext& ctx, const QVariantMap& options)
{
    Q_UNUSED(options);
    int matched = 0;
    for (const auto& port : usb::enumerateFlashPorts()) {
        if (usb::platformForHint(port.chipHint) != ChipPlatform::Allwinner)
            continue;
        ++matched;
        ctx.info(QStringLiteral("发现 FEL 端口：VID=%1 PID=%2 %3\n  接口：%4")
                     .arg(port.vid, 4, 16, QLatin1Char('0'))
                     .arg(port.pid, 4, 16, QLatin1Char('0'))
                     .arg(port.description)
                     .arg(port.devicePath));
    }
    if (matched == 0)
        ctx.warn(proto::noDeviceMessage(ChipPlatform::Allwinner));
    else
        ctx.ok(QStringLiteral("共检测到 %1 个全志 FEL 端口").arg(matched));
}

void felHello(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        auto transport = openFel(options, reporter);
        aw::Client client(*transport, reporter);
        reporter.progress(30, QStringLiteral("FEL_VER 握手…"));
        const aw::VersionInfo info = client.hello();
        ctx.info(QStringLiteral("BootROM：%1").arg(info.magic));
        ctx.info(QStringLiteral("SoC：%1（0x%2）")
                     .arg(aw::socName(info.id))
                     .arg(info.id, 8, 16, QLatin1Char('0')));
        ctx.info(QStringLiteral("FEL 版本：0x%1").arg(info.version, 8, 16, QLatin1Char('0')));
        if (!info.firmware.isEmpty())
            ctx.info(QStringLiteral("固件字：%1")
                         .arg(QString::fromLatin1(info.firmware.toHex(' '))));
        reporter.progress(100);
        ctx.ok(QStringLiteral("全志 FEL 握手成功"));
    });
}

void felRead(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString output = options.value(QStringLiteral("output")).toString().trimmed();
        if (output.isEmpty())
            throw std::runtime_error("请选择输出文件");
        const quint32 address = quint32(proto::optionUInt(options, QStringLiteral("address"), 0));
        quint64 length = proto::optionUInt(options, QStringLiteral("length"), 0);
        if (length == 0)
            throw std::runtime_error("读取长度必须大于 0");
        if (length > 256ull * 1024 * 1024)
            throw std::runtime_error("一次读取不能超过 256 MiB");

        auto transport = openFel(options, reporter);
        aw::Client client(*transport, reporter);
        reporter.progress(30, QStringLiteral("FEL 读取内存…"));
        const QByteArray data = client.read(address, quint32(length));
        if (!util::ensureDir(QFileInfo(output).absolutePath()) || !util::writeFile(output, data))
            throw std::runtime_error(QStringLiteral("写入文件失败：%1").arg(output).toStdString());
        reporter.progress(100);
        ctx.ok(QStringLiteral("读取完成：0x%1，%2 字节 → %3")
                   .arg(address, 0, 16)
                   .arg(data.size())
                   .arg(output));
    });
}

void felWrite(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString input = options.value(QStringLiteral("input")).toString().trimmed();
        if (input.isEmpty())
            throw std::runtime_error("请选择要写入的文件");
        const QByteArray data = util::readFile(input);
        if (data.isEmpty())
            throw std::runtime_error(QStringLiteral("无法读取文件：%1").arg(input).toStdString());
        const quint32 address = quint32(proto::optionUInt(options, QStringLiteral("address"), 0));

        auto transport = openFel(options, reporter);
        aw::Client client(*transport, reporter);
        reporter.progress(10, QStringLiteral("FEL 写入内存…"));
        client.write(address, data, aw::kMaxChunk, 10, 80);
        const bool exec = proto::optionBool(options, QStringLiteral("exec"), false);
        if (exec) {
            reporter.progress(90, QStringLiteral("FEL 执行…"));
            client.exec(address);
        }
        reporter.progress(100);
        ctx.ok(QStringLiteral("写入完成：%1 → 0x%2（%3 字节）%4")
                   .arg(input)
                   .arg(address, 0, 16)
                   .arg(data.size())
                   .arg(exec ? QStringLiteral("，已执行") : QString()));
    });
}

void felRun(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString input = options.value(QStringLiteral("input")).toString().trimmed();
        if (input.isEmpty())
            throw std::runtime_error("请选择要运行的镜像文件");
        const QByteArray data = util::readFile(input);
        if (data.isEmpty())
            throw std::runtime_error(QStringLiteral("无法读取文件：%1").arg(input).toStdString());
        const quint32 address = quint32(proto::optionUInt(options, QStringLiteral("address"), 0));

        auto transport = openFel(options, reporter);
        aw::Client client(*transport, reporter);
        reporter.progress(10, QStringLiteral("FEL 下载并执行…"));
        client.run(address, data);
        reporter.progress(100);
        ctx.ok(QStringLiteral("已下载并执行：%1 → 0x%2（%3 字节）")
                   .arg(input)
                   .arg(address, 0, 16)
                   .arg(data.size()));
    });
}

QVariantMap deviceField()
{
    return fields::text(QStringLiteral("device"), QStringLiteral("设备接口（可选）"), QString(),
                        QStringLiteral("留空自动检测 FEL 端口"),
                        QStringLiteral("指定 WinUSB 设备接口路径，留空自动选择"), false);
}

void registerAwDeviceTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("aw.fel.hello");
        spec.category = QStringLiteral("aw");
        spec.group = QStringLiteral("fel");
        spec.name = QStringLiteral("FEL 握手 / 版本");
        spec.desc = QStringLiteral("FEL_VER 握手，显示 BootROM 版本与 SoC 型号");
        spec.icon = QStringLiteral("sun");
        spec.keywords = {QStringLiteral("fel"), QStringLiteral("全志"),
                         QStringLiteral("握手"), QStringLiteral("awusbfex")};
        spec.fields = {deviceField()};
        spec.order = 10;
        addTool(spec, felHello);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("aw.fel.read");
        spec.category = QStringLiteral("aw");
        spec.group = QStringLiteral("fel");
        spec.name = QStringLiteral("FEL 读取内存");
        spec.desc = QStringLiteral("从指定物理地址读取内存并保存为文件");
        spec.icon = QStringLiteral("download");
        spec.keywords = {QStringLiteral("fel"), QStringLiteral("读取"),
                         QStringLiteral("内存")};
        spec.fields = {deviceField(),
                       fields::hex(QStringLiteral("address"), QStringLiteral("物理地址"),
                                   QStringLiteral("0x40000000")),
                       fields::text(QStringLiteral("length"), QStringLiteral("长度（字节）"),
                                    QStringLiteral("0x1000")),
                       fields::path(QStringLiteral("output"), QStringLiteral("输出文件"),
                                    QStringLiteral("*.bin"))};
        spec.order = 20;
        addTool(spec, felRead);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("aw.fel.write");
        spec.category = QStringLiteral("aw");
        spec.group = QStringLiteral("fel");
        spec.name = QStringLiteral("FEL 写入内存");
        spec.desc = QStringLiteral("把文件写入指定物理地址，可选立即执行");
        spec.icon = QStringLiteral("upload");
        spec.keywords = {QStringLiteral("fel"), QStringLiteral("写入"),
                         QStringLiteral("下载")};
        spec.fields = {deviceField(),
                       fields::hex(QStringLiteral("address"), QStringLiteral("物理地址"),
                                   QStringLiteral("0x40000000")),
                       fields::path(QStringLiteral("input"), QStringLiteral("写入文件"),
                                    QStringLiteral("*.bin *.img")),
                       fields::boolean(QStringLiteral("exec"), QStringLiteral("写入后执行"), false)};
        spec.order = 30;
        addTool(spec, felWrite);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("aw.fel.run");
        spec.category = QStringLiteral("aw");
        spec.group = QStringLiteral("fel");
        spec.name = QStringLiteral("FEL 下载并执行");
        spec.desc = QStringLiteral("下载镜像到内存并跳转执行（PhoenixSuit 风格）");
        spec.icon = QStringLiteral("play");
        spec.keywords = {QStringLiteral("fel"), QStringLiteral("执行"),
                         QStringLiteral("boot0")};
        spec.fields = {deviceField(),
                       fields::hex(QStringLiteral("address"), QStringLiteral("执行地址"),
                                   QStringLiteral("0x40000000")),
                       fields::path(QStringLiteral("input"), QStringLiteral("镜像文件"),
                                    QStringLiteral("*.bin *.img *.fex"))};
        spec.order = 40;
        addTool(spec, felRun);
    }
}

void registerUtilityTools()
{
    ToolSpec spec;
    spec.id = QStringLiteral("aw.ports.scan");
    spec.category = QStringLiteral("aw");
    spec.group = QStringLiteral("tool");
    spec.name = QStringLiteral("检测 FEL 端口");
    spec.desc = QStringLiteral("扫描并列出当前连接的全志 FEL 端口（VID 1F3A）");
    spec.icon = QStringLiteral("usb");
    spec.keywords = {QStringLiteral("端口"), QStringLiteral("扫描"),
                     QStringLiteral("fel")};
    spec.fields = {};
    spec.order = 100;
    addTool(spec, scanPorts);
}

} // namespace

void registerAwToolHandlers()
{
    registerAwDeviceTools();
    registerUtilityTools();
}

} // namespace gf
