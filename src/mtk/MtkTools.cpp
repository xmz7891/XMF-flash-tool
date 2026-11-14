#include "MtkTools.h"

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <stdexcept>

#include "../app/ToolModules.h"
#include "Brom.h"
#include "Da.h"
#include "Preloader.h"
#include "Scatter.h"
#include "LegacyPmt.h"
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

struct DaSession {
    std::unique_ptr<usb::IUsbTransport> transport;
    std::unique_ptr<mtk::da::Client> da;
};

/// Full BROM -> DA1 -> re-enumerate -> DA2 flow. This is the prologue of every
/// DA-level MTK operation.
DaSession openDaSession(const QVariantMap& options, const proto::Reporter& reporter)
{
    const QString da1Path = options.value(QStringLiteral("da1")).toString().trimmed();
    const QString da2Path = options.value(QStringLiteral("da2")).toString().trimmed();
    if (da1Path.isEmpty() || da2Path.isEmpty()) {
        throw std::runtime_error(
            "请选择 DA1 与 DA2 文件（引导包中“/DA/...”路径），可从引导包页面复制");
    }
    const QByteArray da1 = util::readFile(da1Path);
    const QByteArray da2 = util::readFile(da2Path);
    if (da1.isEmpty() || da2.isEmpty()) {
        throw std::runtime_error(
            QStringLiteral("无法读取 DA 文件：%1").arg(da1.isEmpty() ? da1Path : da2Path).toStdString());
    }

    const quint32 da1Address =
        quint32(proto::optionUInt(options, QStringLiteral("da1_address"), mtk::da::kDefaultDa1Address));
    const quint32 da2Address =
        quint32(proto::optionUInt(options, QStringLiteral("da2_address"), mtk::da::kDefaultDa2Address));
    const quint32 signatureLength =
        quint32(proto::optionUInt(options, QStringLiteral("signature_length"), 0));

    reporter.progress(5, QStringLiteral("查找 MTK BROM 端口…"));
    DaSession session;
    session.transport = proto::openTransport(ChipPlatform::MediaTek, deviceOption(options), reporter);

    mtk::brom::Client brom(*session.transport, reporter);
    brom.handshake();
    reporter.progress(10, QStringLiteral("发送 DA1…"));
    brom.sendDa(da1Address, da1, signatureLength, 15000, 10, 30);
    reporter.progress(42, QStringLiteral("启动 DA1…"));
    brom.jumpDa(da1Address);

    const int waitMs = proto::optionInt(options, QStringLiteral("da_wait_ms"), 15000);
    reporter.progress(45, QStringLiteral("等待 DA 端口重新枚举（最多 %1 ms）…").arg(waitMs));
    session.transport->close();
    session.transport.reset();

    const usb::PortInfo port = mtk::da::waitForDaPort(waitMs, reporter);
    if (port.devicePath.isEmpty()) {
        throw std::runtime_error(
            QStringLiteral("DA1 启动后未重新枚举到 MTK 端口（等待 %1 ms）。"
                           "请检查驱动，或在选项里指定设备接口后重试")
                .arg(waitMs)
                .toStdString());
    }
    reporter.info(QStringLiteral("DA 端口已就绪：%1").arg(port.devicePath));
    session.transport.reset(usb::createTransport(port));
    if (!session.transport) {
        throw std::runtime_error("当前系统无法创建 MTK USB 传输实例");
    }
    if (!session.transport->open(port)) {
        throw std::runtime_error(
            QStringLiteral("打开 DA 端口失败：%1").arg(session.transport->errorString()).toStdString());
    }

    session.da = std::make_unique<mtk::da::Client>(*session.transport, reporter);
    reporter.progress(50, QStringLiteral("下载 DA2…"));
    session.da->downloadDa2(da2, da2Address, signatureLength, true, 50, 30);
    reporter.progress(85, QStringLiteral("DA2 已启动"));
    return session;
}

void scanPorts(TaskContext& ctx, const QVariantMap& options)
{
    Q_UNUSED(options);
    int matched = 0;
    for (const auto& port : usb::enumerateFlashPorts()) {
        if (usb::platformForHint(port.chipHint) != ChipPlatform::MediaTek)
            continue;
        ++matched;
        ctx.info(QStringLiteral("发现 MTK 端口（%1）：VID=%2 PID=%3 %4\n  接口：%5")
                     .arg(port.chipHint)
                     .arg(port.vid, 4, 16, QLatin1Char('0'))
                     .arg(port.pid, 4, 16, QLatin1Char('0'))
                     .arg(port.description)
                     .arg(port.devicePath));
    }
    if (matched == 0)
        ctx.warn(proto::noDeviceMessage(ChipPlatform::MediaTek));
    else
        ctx.ok(QStringLiteral("共检测到 %1 个 MTK 端口").arg(matched));
}

void bromHandshake(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        reporter.progress(10, QStringLiteral("查找 MTK BROM 端口…"));
        auto transport = proto::openTransport(ChipPlatform::MediaTek, deviceOption(options), reporter);
        mtk::brom::Client brom(*transport, reporter);
        brom.handshake();
        reporter.progress(60, QStringLiteral("读取目标配置…"));
        const QByteArray config = brom.getTargetConfig();
        ctx.info(QStringLiteral("目标配置（%1 字节）：%2")
                     .arg(config.size())
                     .arg(QString::fromLatin1(config.toHex(' '))));
        reporter.progress(100);
        ctx.ok(QStringLiteral("MTK BROM 握手成功"));
    });
}

void bromRead32(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const quint32 address = quint32(proto::optionUInt(options, QStringLiteral("address"), 0));
        auto transport = proto::openTransport(ChipPlatform::MediaTek, deviceOption(options), reporter);
        mtk::brom::Client brom(*transport, reporter);
        brom.handshake();
        const quint32 value = brom.read32(address);
        ctx.info(QStringLiteral("读取 0x%1 = 0x%2")
                     .arg(address, 8, 16, QLatin1Char('0'))
                     .arg(value, 8, 16, QLatin1Char('0')));
        reporter.progress(100);
        ctx.ok(QStringLiteral("读取成功"));
    });
}

void daLoad(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        auto session = openDaSession(options, reporter);
        reporter.progress(100);
        ctx.ok(QStringLiteral("DA1/DA2 加载完成，设备已进入 DA 模式"));
    });
}

void daMemory(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString action = options.value(QStringLiteral("action")).toString();
        const quint32 address = quint32(proto::optionUInt(options, QStringLiteral("address"), 0));
        const quint32 value = quint32(proto::optionUInt(options, QStringLiteral("value"), 0));

        auto session = openDaSession(options, reporter);
        reporter.progress(90, QStringLiteral("执行 DA 命令…"));

        if (action == QLatin1String("写 32 位")) {
            session.da->write32(address, value, true);
            ctx.info(QStringLiteral("写入 0x%1 = 0x%2（已回读校验）")
                         .arg(address, 8, 16, QLatin1Char('0'))
                         .arg(value, 8, 16, QLatin1Char('0')));
        } else if (action == QLatin1String("Echo")) {
            const quint32 echoed = session.da->echo(value);
            ctx.info(QStringLiteral("Echo 0x%1 → 0x%2")
                         .arg(value, 8, 16, QLatin1Char('0'))
                         .arg(echoed, 8, 16, QLatin1Char('0')));
            if (echoed != value)
                throw std::runtime_error("DA Echo 回显不一致");
        } else if (action == QLatin1String("目标配置")) {
            const QByteArray config = session.da->getTargetConfig();
            ctx.info(QStringLiteral("目标配置：%1").arg(QString::fromLatin1(config.toHex(' '))));
        } else {
            const quint32 read = session.da->read32(address);
            ctx.info(QStringLiteral("读取 0x%1 = 0x%2")
                         .arg(address, 8, 16, QLatin1Char('0'))
                         .arg(read, 8, 16, QLatin1Char('0')));
        }
        reporter.progress(100);
        ctx.ok(QStringLiteral("DA 命令执行完成"));
    });
}

void preloaderParse(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString path = options.value(QStringLiteral("file")).toString().trimmed();
        if (path.isEmpty())
            throw std::runtime_error("请选择 preloader 文件");
        const QByteArray data = util::readFile(path);
        if (data.isEmpty())
            throw std::runtime_error(QStringLiteral("无法读取文件：%1").arg(path).toStdString());

        reporter.progress(30, QStringLiteral("解析 preloader 头…"));
        const mtk::PreloaderInfo info = mtk::PreloaderInfo::parse(data);
        if (!info.valid)
            throw std::runtime_error(QStringLiteral("解析失败：%1").arg(info.error).toStdString());

        ctx.info(mtk::describePreloader(info));
        ctx.info(QStringLiteral("头字段：magic=0x%1, header=0x%2, imageSize=%3, @0x24=0x%4, "
                                "@0x28=0x%5, @0x2C=0x%6, @0x30=0x%7, @0x34=0x%8")
                     .arg(info.magic, 0, 16)
                     .arg(info.headerSize, 0, 16)
                     .arg(info.imageSize)
                     .arg(info.fieldAt24, 0, 16)
                     .arg(info.fieldAt28, 0, 16)
                     .arg(info.fieldAt2C, 0, 16)
                     .arg(info.fieldAt30, 0, 16)
                     .arg(info.fieldAt34, 0, 16));
        for (const auto& section : info.sections) {
            ctx.info(QStringLiteral("段：偏移 0x%1，长度 %2，类型 %3")
                         .arg(section.offset, 0, 16)
                         .arg(section.length)
                         .arg(section.type));
        }
        for (const auto& warning : info.warnings)
            ctx.warn(warning);
        reporter.progress(100);
        ctx.ok(QStringLiteral("preloader 解析完成，代码从 0x%1 开始（共 %2 字节）")
                   .arg(info.sectionsEnd, 0, 16)
                   .arg(data.size()));
    });
}

QString csvCell(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

void scatterParse(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString path = options.value(QStringLiteral("file")).toString().trimmed();
        if (path.isEmpty())
            throw std::runtime_error("请选择 scatter 文件");
        reporter.progress(20, QStringLiteral("解析 scatter 文件…"));
        const mtk::ScatterInfo info = mtk::ScatterInfo::parseFile(path);
        if (!info.valid)
            throw std::runtime_error(QStringLiteral("解析失败：%1").arg(info.error).toStdString());

        ctx.info(mtk::describeScatter(info));
        for (const auto& warning : info.warnings)
            ctx.warn(warning);
        for (const auto& partition : info.partitions) {
            ctx.info(QStringLiteral("%1 %2：%3  起始 0x%4（物理 0x%5），大小 0x%6，下载=%7，区域=%8")
                         .arg(partition.index.isEmpty() ? QStringLiteral("-") : partition.index,
                              partition.name,
                              partition.fileName.isEmpty() ? QStringLiteral("(无文件)")
                                                           : partition.fileName)
                         .arg(partition.linearStart, 0, 16)
                         .arg(partition.physicalStart, 0, 16)
                         .arg(partition.size, 0, 16)
                         .arg(partition.isDownload ? QStringLiteral("是") : QStringLiteral("否"))
                         .arg(partition.region));
        }

        const QString output = options.value(QStringLiteral("output")).toString().trimmed();
        if (!output.isEmpty()) {
            QByteArray csv;
            csv += "index,name,file_name,is_download,type,linear_start_addr,"
                   "physical_start_addr,size,region,storage,operation_type\n";
            for (const auto& partition : info.partitions) {
                csv += csvCell(partition.index).toUtf8();
                csv += ',';
                csv += csvCell(partition.name).toUtf8();
                csv += ',';
                csv += csvCell(partition.fileName).toUtf8();
                csv += partition.isDownload ? ",true," : ",false,";
                csv += csvCell(partition.type).toUtf8();
                csv += ',';
                csv += QByteArray::number(partition.linearStart);
                csv += ',';
                csv += QByteArray::number(partition.physicalStart);
                csv += ',';
                csv += QByteArray::number(partition.size);
                csv += ',';
                csv += csvCell(partition.region).toUtf8();
                csv += ',';
                csv += csvCell(partition.storage).toUtf8();
                csv += ',';
                csv += csvCell(partition.operationType).toUtf8();
                csv += '\n';
            }
            if (!util::ensureDir(QFileInfo(output).absolutePath()) || !util::writeFile(output, csv))
                throw std::runtime_error(QStringLiteral("写入 CSV 失败：%1").arg(output).toStdString());
            ctx.info(QStringLiteral("分区表已导出：%1").arg(output));
        }

        reporter.progress(100);
        ctx.ok(QStringLiteral("scatter 解析完成：%1 个分区").arg(info.partitions.size()));
    });
}

QVariantMap deviceField()
{
    return fields::text(QStringLiteral("device"), QStringLiteral("设备接口（可选）"), QString(),
                        QStringLiteral("留空自动检测 MTK BROM 端口"),
                        QStringLiteral("指定 WinUSB 设备接口路径，留空自动选择"), false);
}

QVariantMap da1Field()
{
    return fields::path(QStringLiteral("da1"), QStringLiteral("DA1 文件"),
                        QStringLiteral("*.bin"),
                        QStringLiteral("引导包 DA/.../xxx_DA1.bin"));
}

QVariantMap da2Field()
{
    return fields::path(QStringLiteral("da2"), QStringLiteral("DA2 文件"), QStringLiteral("*.bin"),
                        QStringLiteral("引导包 DA/.../xxx_DA2.bin"));
}

void registerBromTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("mtk.brom.handshake");
        spec.category = QStringLiteral("mtk");
        spec.group = QStringLiteral("brom");
        spec.name = QStringLiteral("BROM 握手");
        spec.desc = QStringLiteral("发送 0xA0 握手并读取 MTK 目标配置");
        spec.icon = QStringLiteral("microchip");
        spec.keywords = {QStringLiteral("brom"), QStringLiteral("mtk"),
                         QStringLiteral("握手"), QStringLiteral("0xa0")};
        spec.fields = {deviceField()};
        spec.order = 10;
        addTool(spec, bromHandshake);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("mtk.brom.read32");
        spec.category = QStringLiteral("mtk");
        spec.group = QStringLiteral("brom");
        spec.name = QStringLiteral("BROM 读取 32 位");
        spec.desc = QStringLiteral("通过 BROM 命令 0xD1 读取 32 位寄存器");
        spec.icon = QStringLiteral("search");
        spec.keywords = {QStringLiteral("brom"), QStringLiteral("read32"),
                         QStringLiteral("寄存器")};
        spec.fields = {deviceField(),
                       fields::hex(QStringLiteral("address"), QStringLiteral("地址"),
                                   QStringLiteral("0x0"))};
        spec.order = 20;
        addTool(spec, bromRead32);
    }
}

void registerDaTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("mtk.da.load");
        spec.category = QStringLiteral("mtk");
        spec.group = QStringLiteral("da");
        spec.name = QStringLiteral("加载 DA1 / DA2");
        spec.desc = QStringLiteral("BROM 发送 DA1 并跳转，等待重枚举后下载并启动 DA2");
        spec.icon = QStringLiteral("upload");
        spec.keywords = {QStringLiteral("da"), QStringLiteral("da1"),
                         QStringLiteral("da2"), QStringLiteral("下载")};
        spec.fields = {deviceField(), da1Field(), da2Field(),
                       fields::hex(QStringLiteral("da1_address"), QStringLiteral("DA1 地址"),
                                   QStringLiteral("0x200000"),
                                   QStringLiteral("引导包 Model 的 DA1Address")),
                       fields::hex(QStringLiteral("da2_address"), QStringLiteral("DA2 地址"),
                                   QStringLiteral("0x40000000"),
                                   QStringLiteral("引导包 Model 的 DA2Address")),
                       fields::hex(QStringLiteral("signature_length"),
                                   QStringLiteral("签名长度"), QStringLiteral("0")),
                       fields::integer(QStringLiteral("da_wait_ms"),
                                       QStringLiteral("DA 重枚举等待（ms）"), 15000, 1000, 60000)};
        spec.order = 30;
        addTool(spec, daLoad);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("mtk.da.memory");
        spec.category = QStringLiteral("mtk");
        spec.group = QStringLiteral("da");
        spec.name = QStringLiteral("DA 内存读写");
        spec.desc = QStringLiteral("加载 DA 后通过 DA 协议读写寄存器 / 内存、Echo、读目标配置");
        spec.icon = QStringLiteral("memory-stick");
        spec.keywords = {QStringLiteral("da"), QStringLiteral("read32"),
                         QStringLiteral("write32"), QStringLiteral("echo")};
        spec.fields = {deviceField(), da1Field(), da2Field(),
                       fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                                      {QStringLiteral("读 32 位"), QStringLiteral("写 32 位"),
                                       QStringLiteral("Echo"), QStringLiteral("目标配置")},
                                      0),
                       fields::hex(QStringLiteral("address"), QStringLiteral("地址"),
                                   QStringLiteral("0x0")),
                       fields::hex(QStringLiteral("value"), QStringLiteral("写入值"),
                                   QStringLiteral("0x0"))};
        spec.order = 40;
        addTool(spec, daMemory);
    }
}


void legacyPmtParse(TaskContext& ctx, const QVariantMap& options)
{
    const QString path = options.value(QStringLiteral("file")).toString().trimmed();
    if (path.isEmpty())
        throw std::runtime_error("select a USER image / firmware dump");
    const QByteArray data = util::readFile(path);
    if (data.isEmpty())
        throw std::runtime_error("cannot read USER image");
    const mtk::LegacyPmtInfo info = mtk::parseLegacyPmt(data);
    if (!info.valid)
        throw std::runtime_error(QStringLiteral("legacy PMT parse failed: %1").arg(info.error).toStdString());
    ctx.info(mtk::describeLegacyPmt(info));
    for (const auto& entry : info.entries) {
        ctx.info(QStringLiteral("%1  offset=0x%2  size=0x%3  flags=0x%4")
                     .arg(entry.name)
                     .arg(entry.offset, 0, 16)
                     .arg(entry.size, 0, 16)
                     .arg(entry.maskFlags, 0, 16));
    }
    ctx.ok(QStringLiteral("legacy PMT parsed: %1 partition(s)").arg(info.entries.size()));
}

void registerParserTools()
{
    using namespace gf::fields;
    {
        ToolSpec spec;
        spec.id = QStringLiteral("mtk.legacy.pmt");
        spec.category = QStringLiteral("mtk");
        spec.group = QStringLiteral("tool");
        spec.name = QStringLiteral("Legacy PMT / MPT");
        spec.desc = QStringLiteral("Parse MediaTek legacy PT/MPT partition table at the end of a USER image");
        spec.icon = QStringLiteral("table-2");
        spec.keywords = {QStringLiteral("pmt"), QStringLiteral("mpt"), QStringLiteral("legacy"),
                         QStringLiteral("mtk"), QStringLiteral("partition")};
        spec.fields = {path(QStringLiteral("file"), QStringLiteral("USER image / dump"),
                            QStringLiteral("*.bin *.img *.*"))};
        spec.order = 55;
        addTool(spec, legacyPmtParse);
    }

    {
        ToolSpec spec;
        spec.id = QStringLiteral("mtk.preloader.parse");
        spec.category = QStringLiteral("mtk");
        spec.group = QStringLiteral("tool");
        spec.name = QStringLiteral("Preloader 解析");
        spec.desc = QStringLiteral("校验 MTK preloader 头、MMM 段表与镜像大小");
        spec.icon = QStringLiteral("file-text");
        spec.keywords = {QStringLiteral("preloader"), QStringLiteral("解析"),
                         QStringLiteral("mtk")};
        spec.fields = {fields::path(QStringLiteral("file"), QStringLiteral("preloader 文件"),
                                    QStringLiteral("*.bin"))};
        spec.order = 50;
        addTool(spec, preloaderParse);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("mtk.scatter.parse");
        spec.category = QStringLiteral("mtk");
        spec.group = QStringLiteral("tool");
        spec.name = QStringLiteral("Scatter 解析");
        spec.desc = QStringLiteral("解析 MTK scatter 分区表，可导出 CSV");
        spec.icon = QStringLiteral("list-checks");
        spec.keywords = {QStringLiteral("scatter"), QStringLiteral("分区表"),
                         QStringLiteral("mtk")};
        spec.fields = {fields::path(QStringLiteral("file"), QStringLiteral("scatter 文件"),
                                    QStringLiteral("*.txt *.xml")),
                       fields::text(QStringLiteral("output"), QStringLiteral("导出 CSV（可选）"),
                                    QString(), QStringLiteral("留空只显示结果"),
                                    QStringLiteral("分区表 CSV 输出路径"), false)};
        spec.order = 60;
        addTool(spec, scatterParse);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("mtk.ports.scan");
        spec.category = QStringLiteral("mtk");
        spec.group = QStringLiteral("tool");
        spec.name = QStringLiteral("检测 MTK 端口");
        spec.desc = QStringLiteral("扫描并列出当前连接的 MTK BROM/PreLoader/DA 端口");
        spec.icon = QStringLiteral("usb");
        spec.keywords = {QStringLiteral("端口"), QStringLiteral("扫描"),
                         QStringLiteral("mtk")};
        spec.fields = {};
        spec.order = 100;
        addTool(spec, scanPorts);
    }
}

} // namespace

void registerMtkToolHandlers()
{
    registerBromTools();
    registerDaTools();
    registerParserTools();
}

} // namespace gf
