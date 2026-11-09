#include "RkTools.h"

#include <QFileInfo>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <stdexcept>

#include "../app/ToolModules.h"
#include "RockUsb.h"
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

std::unique_ptr<usb::IUsbTransport> openRk(const QVariantMap& options,
                                           const proto::Reporter& reporter)
{
    reporter.progress(5, QStringLiteral("查找 Rockchip 端口…"));
    return proto::openTransport(ChipPlatform::Rockchip, deviceOption(options), reporter);
}

void scanPorts(TaskContext& ctx, const QVariantMap& options)
{
    Q_UNUSED(options);
    int matched = 0;
    for (const auto& port : usb::enumerateFlashPorts()) {
        if (usb::platformForHint(port.chipHint) != ChipPlatform::Rockchip)
            continue;
        ++matched;
        ctx.info(QStringLiteral("发现 Rockchip 端口：VID=%1 PID=%2 %3\n  接口：%4")
                     .arg(port.vid, 4, 16, QLatin1Char('0'))
                     .arg(port.pid, 4, 16, QLatin1Char('0'))
                     .arg(rk::deviceModeName(port.pid))
                     .arg(port.devicePath));
    }
    if (matched == 0)
        ctx.warn(proto::noDeviceMessage(ChipPlatform::Rockchip));
    else
        ctx.ok(QStringLiteral("共检测到 %1 个 Rockchip 端口").arg(matched));
}

void deviceInfo(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        auto transport = openRk(options, reporter);
        rk::Client client(*transport, reporter);
        reporter.progress(30, QStringLiteral("读取设备信息…"));
        const rk::DeviceInfo info = client.deviceInfo();
        ctx.info(QStringLiteral("Loader 版本：0x%1").arg(info.version, 8, 16, QLatin1Char('0')));
        if (!info.chip.isEmpty())
            ctx.info(QStringLiteral("芯片标识：%1").arg(info.chip));
        ctx.info(QStringLiteral("原始信息：%1").arg(QString::fromLatin1(info.raw.toHex(' '))));
        reporter.progress(70, QStringLiteral("读取能力信息…"));
        try {
            const QByteArray capability = client.readCapability();
            ctx.info(QStringLiteral("能力信息：%1")
                         .arg(QString::fromLatin1(capability.toHex(' '))));
        } catch (const std::exception& error) {
            ctx.warn(QStringLiteral("读取能力信息失败：%1").arg(QString::fromUtf8(error.what())));
        }
        reporter.progress(100);
        ctx.ok(QStringLiteral("RockUSB 设备信息读取完成"));
    });
}

void readSectors(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString output = options.value(QStringLiteral("output")).toString().trimmed();
        if (output.isEmpty())
            throw std::runtime_error("请选择输出文件");
        const quint32 start = quint32(proto::optionUInt(options, QStringLiteral("start_sector"), 0));
        const quint32 count = quint32(proto::optionUInt(options, QStringLiteral("num_sectors"), 0));
        const int sectorSize = proto::optionInt(options, QStringLiteral("sector_size"), 512);
        if (count == 0)
            throw std::runtime_error("读取扇区数必须大于 0");

        auto transport = openRk(options, reporter);
        rk::Client client(*transport, reporter);
        reporter.progress(30, QStringLiteral("读取扇区数据…"));
        const QByteArray data = client.readSectors(start, count, sectorSize);
        if (!util::ensureDir(QFileInfo(output).absolutePath()) || !util::writeFile(output, data))
            throw std::runtime_error(QStringLiteral("写入文件失败：%1").arg(output).toStdString());
        reporter.progress(100);
        ctx.ok(QStringLiteral("读取完成：%1 扇区，%2 字节 → %3").arg(count).arg(data.size()).arg(output));
    });
}

void writeSectors(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString input = options.value(QStringLiteral("input")).toString().trimmed();
        if (input.isEmpty())
            throw std::runtime_error("请选择要写入的文件");
        QByteArray data = util::readFile(input);
        if (data.isEmpty())
            throw std::runtime_error(QStringLiteral("无法读取文件：%1").arg(input).toStdString());

        const int sectorSize = proto::optionInt(options, QStringLiteral("sector_size"), 512);
        if (sectorSize <= 0)
            throw std::runtime_error("扇区大小必须大于 0");
        const int remainder = data.size() % sectorSize;
        if (remainder != 0) {
            data.append(QByteArray(sectorSize - remainder, '\0'));
            ctx.warn(QStringLiteral("文件已补齐到扇区边界（%1 字节）").arg(data.size()));
        }
        const quint32 start = quint32(proto::optionUInt(options, QStringLiteral("start_sector"), 0));

        auto transport = openRk(options, reporter);
        rk::Client client(*transport, reporter);
        reporter.progress(30, QStringLiteral("写入扇区数据…"));
        client.writeSectors(start, data, sectorSize);
        reporter.progress(100);
        ctx.ok(QStringLiteral("写入完成：起始扇区 %1，%2 字节").arg(start).arg(data.size()));
    });
}

void eraseSectors(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const quint32 start = quint32(proto::optionUInt(options, QStringLiteral("start_sector"), 0));
        const quint32 count = quint32(proto::optionUInt(options, QStringLiteral("num_sectors"), 0));
        if (count == 0)
            throw std::runtime_error("擦除扇区数必须大于 0");
        auto transport = openRk(options, reporter);
        rk::Client client(*transport, reporter);
        reporter.progress(30, QStringLiteral("擦除扇区…"));
        client.eraseSectors(start, count);
        reporter.progress(100);
        ctx.ok(QStringLiteral("擦除完成：起始扇区 %1，共 %2 扇区").arg(start).arg(count));
    });
}

void resetDevice(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString mode = options.value(QStringLiteral("mode")).toString();
        auto transport = openRk(options, reporter);
        rk::Client client(*transport, reporter);
        reporter.progress(50, QStringLiteral("发送重启命令…"));
        const bool loader = mode.contains(QStringLiteral("Loader"));
        client.reset(loader ? 1 : 0);
        reporter.progress(100);
        ctx.ok(loader ? QStringLiteral("已发送重启到 Loader 模式的命令")
                      : QStringLiteral("已发送普通重启命令"));
    });
}

QVariantMap deviceField()
{
    return fields::text(QStringLiteral("device"), QStringLiteral("设备接口（可选）"), QString(),
                        QStringLiteral("留空自动检测 Rockchip 端口"),
                        QStringLiteral("指定 WinUSB 设备接口路径，留空自动选择"), false);
}

QVariantMap sectorSizeField()
{
    return fields::choice(QStringLiteral("sector_size"), QStringLiteral("扇区大小"),
                          {QStringLiteral("512"), QStringLiteral("4096")}, 0,
                          QStringLiteral("RockUSB 扇区通常为 512"));
}

void registerRkDeviceTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("rk.device.info");
        spec.category = QStringLiteral("rk");
        spec.group = QStringLiteral("device");
        spec.name = QStringLiteral("RockUSB 设备信息");
        spec.desc = QStringLiteral("读取 Loader 版本、芯片标识与能力信息");
        spec.icon = QStringLiteral("box");
        spec.keywords = {QStringLiteral("rockusb"), QStringLiteral("rk"),
                         QStringLiteral("设备信息"), QStringLiteral("loader")};
        spec.fields = {deviceField()};
        spec.order = 10;
        addTool(spec, deviceInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("rk.reset");
        spec.category = QStringLiteral("rk");
        spec.group = QStringLiteral("device");
        spec.name = QStringLiteral("重启 / 切换 Loader");
        spec.desc = QStringLiteral("发送 RockUSB 重启命令，可重启到普通模式或 Loader 模式");
        spec.icon = QStringLiteral("rotate-ccw");
        spec.keywords = {QStringLiteral("reset"), QStringLiteral("重启"),
                         QStringLiteral("loader")};
        spec.fields = {deviceField(),
                       fields::choice(QStringLiteral("mode"), QStringLiteral("重启模式"),
                                      {QStringLiteral("普通重启"), QStringLiteral("重启到 Loader")},
                                      0)};
        spec.order = 20;
        addTool(spec, resetDevice);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("rk.read");
        spec.category = QStringLiteral("rk");
        spec.group = QStringLiteral("storage");
        spec.name = QStringLiteral("读取扇区");
        spec.desc = QStringLiteral("按扇区读取存储并保存为文件");
        spec.icon = QStringLiteral("download");
        spec.keywords = {QStringLiteral("read"), QStringLiteral("读取"),
                         QStringLiteral("扇区")};
        spec.fields = {deviceField(),
                       fields::text(QStringLiteral("start_sector"), QStringLiteral("起始扇区"),
                                    QStringLiteral("0")),
                       fields::text(QStringLiteral("num_sectors"), QStringLiteral("扇区数"),
                                    QStringLiteral("1")),
                       sectorSizeField(),
                       fields::path(QStringLiteral("output"), QStringLiteral("输出文件"),
                                    QStringLiteral("*.bin"))};
        spec.order = 30;
        addTool(spec, readSectors);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("rk.write");
        spec.category = QStringLiteral("rk");
        spec.group = QStringLiteral("storage");
        spec.name = QStringLiteral("写入扇区");
        spec.desc = QStringLiteral("把文件写入存储扇区（自动补齐到扇区边界）");
        spec.icon = QStringLiteral("upload");
        spec.keywords = {QStringLiteral("write"), QStringLiteral("写入"),
                         QStringLiteral("刷写")};
        spec.fields = {deviceField(),
                       fields::text(QStringLiteral("start_sector"), QStringLiteral("起始扇区"),
                                    QStringLiteral("0")),
                       sectorSizeField(),
                       fields::path(QStringLiteral("input"), QStringLiteral("写入文件"),
                                    QStringLiteral("*.bin *.img"))};
        spec.order = 40;
        addTool(spec, writeSectors);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("rk.erase");
        spec.category = QStringLiteral("rk");
        spec.group = QStringLiteral("storage");
        spec.name = QStringLiteral("擦除扇区");
        spec.desc = QStringLiteral("按扇区擦除存储区域");
        spec.icon = QStringLiteral("zap");
        spec.keywords = {QStringLiteral("erase"), QStringLiteral("擦除")};
        spec.fields = {deviceField(),
                       fields::text(QStringLiteral("start_sector"), QStringLiteral("起始扇区"),
                                    QStringLiteral("0")),
                       fields::text(QStringLiteral("num_sectors"), QStringLiteral("扇区数"),
                                    QStringLiteral("1"))};
        spec.order = 50;
        addTool(spec, eraseSectors);
    }
}

void registerUtilityTools()
{
    ToolSpec spec;
    spec.id = QStringLiteral("rk.ports.scan");
    spec.category = QStringLiteral("rk");
    spec.group = QStringLiteral("tool");
    spec.name = QStringLiteral("检测 Rockchip 端口");
    spec.desc = QStringLiteral("扫描并列出当前连接的 Rockchip MaskROM/Loader 端口");
    spec.icon = QStringLiteral("usb");
    spec.keywords = {QStringLiteral("端口"), QStringLiteral("扫描"),
                     QStringLiteral("rk")};
    spec.fields = {};
    spec.order = 100;
    addTool(spec, scanPorts);
}

} // namespace

void registerRkToolHandlers()
{
    registerRkDeviceTools();
    registerUtilityTools();
}

} // namespace gf
