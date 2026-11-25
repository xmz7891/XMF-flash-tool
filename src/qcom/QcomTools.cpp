#include "QcomTools.h"

#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <stdexcept>

#include "../app/ToolModules.h"
#include "Elf.h"
#include "QedlBootstrap.h"
#include "Firehose.h"
#include "Sahara.h"
#include "TaskManager.h"
#include "ToolRegistry.h"
#include "Util.h"
#include "usb/ProtocolSupport.h"
#include "usb/TaskReporter.h"
#include "usb/ToolOptions.h"
#include "usb/UsbTransport.h"
#include "flows/QcomFlow.h"

namespace gf {

namespace {

using usb::ChipPlatform;

constexpr auto kDeviceKey = "device";

QString deviceOption(const QVariantMap& options)
{
    return options.value(QString::fromLatin1(kDeviceKey)).toString().trimmed();
}

QString clipped(const QString& text, int maxLength = 3000)
{
    return text.size() > maxLength ? text.left(maxLength) + QStringLiteral("…") : text;
}

QString responseFailure(const qcom::firehose::Response& response)
{
    if (!response.error.isEmpty())
        return response.error;
    if (response.nak)
        return QStringLiteral("设备返回 NAK");
    if (!response.logs.isEmpty())
        return response.logs.join(QStringLiteral(" | "));
    if (!response.value.isEmpty())
        return QStringLiteral("设备返回 %1").arg(response.value);
    return QStringLiteral("设备未响应");
}

void ensureAck(const qcom::firehose::Response& response, const QString& action)
{
    if (!response.ok()) {
        throw std::runtime_error(
            QStringLiteral("%1失败：%2").arg(action, responseFailure(response)).toStdString());
    }
}

struct FirehoseSession {
    std::unique_ptr<usb::IUsbTransport> transport;
    std::unique_ptr<qcom::sahara::Client> sahara;
    std::unique_ptr<qcom::firehose::Client> firehose;
};

/// Sahara handshake + programmer upload + Firehose configure. This is the
/// common prologue of every device-side Qualcomm tool.
FirehoseSession openFirehoseSession(const QVariantMap& options,
                                    const proto::Reporter& reporter)
{
    const QString programmerPath = options.value(QStringLiteral("programmer")).toString().trimmed();
    if (programmerPath.isEmpty()) {
        throw std::runtime_error(
            "请选择 Firehose programmer 文件（devprg ELF/MBN），可在引导包中获取");
    }
    const QByteArray image = util::readFile(programmerPath);
    if (image.isEmpty()) {
        throw std::runtime_error(
            QStringLiteral("无法读取 programmer 文件：%1").arg(programmerPath).toStdString());
    }
    const qcom::ElfImage elf = qcom::ElfImage::parse(image);
    if (!elf.valid) {
        throw std::runtime_error(
            QStringLiteral("programmer 不是有效的 ELF/MBN 文件：%1").arg(elf.error).toStdString());
    }
    reporter.info(qcom::describeElf(elf));

    reporter.progress(5, QStringLiteral("查找 9008 端口…"));
    QString qedlError;
    if (!qcom::bootstrapLoader(programmerPath, deviceOption(options), &qedlError))
        reporter.debug(QStringLiteral("qedl bootstrap skipped: %1").arg(qedlError));
    FirehoseSession session;
    session.transport = proto::openTransport(ChipPlatform::Qualcomm, deviceOption(options), reporter);
    session.sahara = std::make_unique<qcom::sahara::Client>(*session.transport, reporter);

    session.firehose = std::make_unique<qcom::firehose::Client>(*session.transport, reporter);
    qcom::firehose::StorageConfig config;
    const QString memory = options.value(QStringLiteral("memory")).toString().trimmed();
    if (!memory.isEmpty())
        config.memoryName = memory;
    config.maxPayloadBytes = proto::optionInt(options, QStringLiteral("max_payload"), 1024 * 1024);

    reporter.progress(10, QStringLiteral("Sahara 握手…"));
    try {
        session.sahara->handshake();
        reporter.progress(15, QStringLiteral("Sahara 上传 programmer…"));
        const auto upload = session.sahara->upload(image, 10000, 15, 45);
        if (!upload.success) {
            throw std::runtime_error(
                QStringLiteral("Sahara 上传 programmer 失败：%1").arg(upload.error).toStdString());
        }
    } catch (const std::exception& error) {
        // A previous QFIL/MiFlash/XMF session may have left the device running
        // a Firehose programmer. It then has no Sahara HELLO but answers XML.
        reporter.warn(QStringLiteral("Sahara 未响应（%1），尝试直接连接已有 Firehose…")
                          .arg(QString::fromUtf8(error.what())));
        const auto direct = session.firehose->configure(config);
        if (!direct.ok()) {
            throw std::runtime_error(
                QStringLiteral("Sahara 握手失败，且设备不在 Firehose 模式：%1")
                    .arg(QString::fromUtf8(error.what())).toStdString());
        }
        reporter.info(QStringLiteral("设备已在 Firehose 模式，跳过 Sahara 上传"));
        return session;
    }

    reporter.progress(65, QStringLiteral("Firehose 配置…"));
    const auto configure = session.firehose->configure(config);
    ensureAck(configure, QStringLiteral("Firehose configure"));
    reporter.info(QStringLiteral("Firehose 就绪（存储类型 %1）").arg(config.memoryName));
    return session;
}

void scanPorts(TaskContext& ctx, const QVariantMap& options)
{
    Q_UNUSED(options);
    const auto ports = usb::enumerateFlashPorts();
    int matched = 0;
    for (const auto& port : ports) {
        if (usb::platformForHint(port.chipHint) != ChipPlatform::Qualcomm)
            continue;
        ++matched;
        ctx.info(QStringLiteral("发现 9008 端口：VID=%1 PID=%2 %3\n  接口：%4")
                     .arg(port.vid, 4, 16, QLatin1Char('0'))
                     .arg(port.pid, 4, 16, QLatin1Char('0'))
                     .arg(port.description)
                     .arg(port.devicePath));
    }
    if (matched == 0)
        ctx.warn(proto::noDeviceMessage(ChipPlatform::Qualcomm));
    else
        ctx.ok(QStringLiteral("共检测到 %1 个高通 EDL 端口").arg(matched));
}

void saharaHandshake(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        reporter.progress(10, QStringLiteral("查找 9008 端口…"));
        auto transport =
            proto::openTransport(ChipPlatform::Qualcomm, deviceOption(options), reporter);
        qcom::sahara::Client client(*transport, reporter);
        const auto hello = client.handshake();
        ctx.info(QStringLiteral("目标协议版本 %1（最低 %2），最大命令长度 %3，模式 %4")
                     .arg(hello.version)
                     .arg(hello.minVersion)
                     .arg(hello.maxCommandLength)
                     .arg(hello.mode));
        reporter.progress(100);
        ctx.ok(QStringLiteral("Sahara 握手成功，设备已就绪等待 programmer"));
    });
}

void saharaUpload(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString path = options.value(QStringLiteral("programmer")).toString().trimmed();
        if (path.isEmpty())
            throw std::runtime_error("请选择要上传的 programmer 文件");

        const QByteArray image = util::readFile(path);
        if (image.isEmpty())
            throw std::runtime_error(
                QStringLiteral("无法读取 programmer 文件：%1").arg(path).toStdString());

        const qcom::ElfImage elf = qcom::ElfImage::parse(image);
        if (!elf.valid) {
            throw std::runtime_error(
                QStringLiteral("programmer 不是有效的 ELF/MBN 文件：%1").arg(elf.error).toStdString());
        }
        reporter.info(qcom::describeElf(elf));
        for (const auto& segment : elf.loadableSegments()) {
            ctx.debug(QStringLiteral("段：文件偏移 0x%1，加载地址 0x%2，大小 %3 字节")
                          .arg(segment.offset, 0, 16)
                          .arg(segment.physicalAddress, 0, 16)
                          .arg(segment.fileSize));
        }

        reporter.progress(5, QStringLiteral("查找 9008 端口…"));
        auto transport =
            proto::openTransport(ChipPlatform::Qualcomm, deviceOption(options), reporter);
        qcom::sahara::Client client(*transport, reporter);
        const auto result = client.upload(image, 10000, 10, 85);
        if (!result.success)
            throw std::runtime_error(
                QStringLiteral("Sahara 上传失败：%1").arg(result.error).toStdString());

        reporter.progress(100);
        ctx.ok(QStringLiteral("Sahara 上传完成，共 %1 字节（%2）")
                   .arg(result.bytesTransferred)
                   .arg(qcom::describeElf(elf)));
    });
}

void firehoseInfo(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        auto session = openFirehoseSession(options, reporter);
        reporter.progress(70, QStringLiteral("读取存储信息…"));
        const auto info = session.firehose->getStorageInfo();
        for (const auto& log : info.logs)
            ctx.info(log);
        if (!info.raw.isEmpty())
            ctx.info(QStringLiteral("存储信息：%1").arg(clipped(info.raw, 4000)));
        if (!info.ok() && !info.nak) {
            // getstorageinfo is optional in some programmer versions; report but
            // do not fail the whole session when only the query is unsupported.
            ctx.warn(QStringLiteral("getstorageinfo 未被支持：%1").arg(responseFailure(info)));
        } else if (info.nak) {
            ctx.warn(QStringLiteral("设备返回 NAK：%1").arg(responseFailure(info)));
        }

        if (proto::optionBool(options, QStringLiteral("reset"), false)) {
            reporter.progress(95, QStringLiteral("重启设备…"));
            const auto reset = session.firehose->reset();
            if (reset.ok())
                ctx.info(QStringLiteral("已发送重启命令"));
            else
                ctx.warn(QStringLiteral("重启命令未确认（设备可能已断开）：%1")
                             .arg(responseFailure(reset)));
        }
        reporter.progress(100);
        ctx.ok(QStringLiteral("Firehose 会话已建立"));
    });
}

void firehoseRead(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString output = options.value(QStringLiteral("output")).toString().trimmed();
        if (output.isEmpty())
            throw std::runtime_error("请选择输出文件");
        const quint64 startSector = proto::optionUInt(options, QStringLiteral("start_sector"), 0);
        const quint64 numSectors = proto::optionUInt(options, QStringLiteral("num_sectors"), 0);
        if (numSectors == 0)
            throw std::runtime_error("读取扇区数必须大于 0");
        const int sectorSize = proto::optionInt(options, QStringLiteral("sector_size"), 4096);

        auto session = openFirehoseSession(options, reporter);
        reporter.progress(70, QStringLiteral("读取扇区数据…"));
        QByteArray data;
        const auto response = session.firehose->readSectors(
            options.value(QStringLiteral("memory")).toString(), 0, startSector, numSectors,
            sectorSize, &data);
        ensureAck(response, QStringLiteral("Firehose 读取"));

        if (!util::ensureDir(QFileInfo(output).absolutePath())
            || !util::writeFile(output, data)) {
            throw std::runtime_error(
                QStringLiteral("写入输出文件失败：%1").arg(output).toStdString());
        }
        reporter.progress(100);
        ctx.ok(QStringLiteral("读取完成：%1 扇区，%2 字节 → %3")
                   .arg(numSectors)
                   .arg(data.size())
                   .arg(output));
    });
}

void firehoseProgram(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString input = options.value(QStringLiteral("input")).toString().trimmed();
        if (input.isEmpty())
            throw std::runtime_error("请选择要写入的文件");
        const QByteArray data = util::readFile(input);
        if (data.isEmpty())
            throw std::runtime_error(
                QStringLiteral("无法读取输入文件：%1").arg(input).toStdString());

        const quint64 startSector = proto::optionUInt(options, QStringLiteral("start_sector"), 0);
        const int sectorSize = proto::optionInt(options, QStringLiteral("sector_size"), 4096);
        const bool ackAfterData = proto::optionBool(options, QStringLiteral("ack_after_data"), true);
        QString filename = options.value(QStringLiteral("filename")).toString().trimmed();
        if (filename.isEmpty())
            filename = QFileInfo(input).fileName();

        auto session = openFirehoseSession(options, reporter);
        reporter.progress(70, QStringLiteral("写入扇区数据…"));
        const auto response = session.firehose->programSectors(
            options.value(QStringLiteral("memory")).toString(), 0, startSector, data, sectorSize,
            filename, 1024 * 1024, ackAfterData);
        ensureAck(response, QStringLiteral("Firehose 写入"));
        reporter.progress(100);
        ctx.ok(QStringLiteral("写入完成：%1 → 起始扇区 %2（%3 字节）")
                   .arg(input)
                   .arg(startSector)
                   .arg(data.size()));
    });
}

void firehoseErase(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const quint64 startSector = proto::optionUInt(options, QStringLiteral("start_sector"), 0);
        const quint64 numSectors = proto::optionUInt(options, QStringLiteral("num_sectors"), 0);
        if (numSectors == 0)
            throw std::runtime_error("擦除扇区数必须大于 0");
        const int sectorSize = proto::optionInt(options, QStringLiteral("sector_size"), 4096);

        auto session = openFirehoseSession(options, reporter);
        reporter.progress(70, QStringLiteral("擦除扇区…"));
        const auto response = session.firehose->eraseSectors(
            options.value(QStringLiteral("memory")).toString(), 0, startSector, numSectors,
            sectorSize);
        ensureAck(response, QStringLiteral("Firehose 擦除"));
        reporter.progress(100);
        ctx.ok(QStringLiteral("擦除完成：起始扇区 %1，共 %2 扇区").arg(startSector).arg(numSectors));
    });
}

QByteArray parseHexBytes(const QString& text)
{
    QString cleaned = text;
    cleaned.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
    cleaned.remove(QLatin1Char(' '));
    cleaned.remove(QLatin1Char('\n'));
    cleaned.remove(QLatin1Char('\t'));
    if (cleaned.isEmpty())
        throw std::runtime_error("请填写要写入的十六进制字节，例如 0x00112233");
    const QByteArray bytes = QByteArray::fromHex(cleaned.toLatin1());
    if (bytes.isEmpty())
        throw std::runtime_error("十六进制字节格式不正确");
    return bytes;
}

void firehosePatch(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QByteArray value =
            parseHexBytes(options.value(QStringLiteral("value")).toString());
        qcom::firehose::Patch patch;
        patch.sectorSize = proto::optionInt(options, QStringLiteral("sector_size"), 4096);
        patch.startSector = proto::optionUInt(options, QStringLiteral("start_sector"), 0);
        patch.byteOffset = proto::optionUInt(options, QStringLiteral("byte_offset"), 0);

        auto session = openFirehoseSession(options, reporter);
        reporter.progress(70, QStringLiteral("发送 Firehose 补丁…"));
        const auto response = session.firehose->patch(patch);
        ensureAck(response, QStringLiteral("Firehose 补丁"));
        reporter.progress(100);
        ctx.ok(QStringLiteral("补丁完成：扇区 %1 偏移 %2，写入 %3 字节")
                   .arg(patch.startSector)
                   .arg(patch.byteOffset)
                   .arg(value.size()));
    });
}

void elfParse(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString path = options.value(QStringLiteral("file")).toString().trimmed();
        if (path.isEmpty())
            throw std::runtime_error("请选择 ELF/MBN 文件");
        const QByteArray data = util::readFile(path);
        if (data.isEmpty())
            throw std::runtime_error(QStringLiteral("无法读取文件：%1").arg(path).toStdString());

        reporter.progress(20, QStringLiteral("解析 ELF 头…"));
        const qcom::ElfImage elf = qcom::ElfImage::parse(data);
        if (!elf.valid)
            throw std::runtime_error(
                QStringLiteral("解析失败：%1").arg(elf.error).toStdString());

        ctx.info(qcom::describeElf(elf));
        int index = 0;
        for (const auto& segment : elf.segments) {
            if (segment.type != 1) {
                ctx.debug(QStringLiteral("段 %1：类型 %2（非 PT_LOAD），文件大小 %3")
                              .arg(index)
                              .arg(segment.type)
                              .arg(segment.fileSize));
                ++index;
                continue;
            }
            ctx.info(QStringLiteral("段 %1：加载地址 0x%2，文件偏移 0x%3，文件 %4 字节，内存 %5 字节，权限 %6")
                         .arg(index)
                         .arg(segment.physicalAddress, 0, 16)
                         .arg(segment.offset, 0, 16)
                         .arg(segment.fileSize)
                         .arg(segment.memorySize)
                         .arg(segment.flags, 0, 16));
            ++index;
        }
        reporter.progress(100);
        ctx.ok(QStringLiteral("ELF 解析完成：%1 个程序头，%2 个可加载段，共 %3 字节")
                   .arg(elf.segments.size())
                   .arg(elf.loadableSegments().size())
                   .arg(elf.totalLoadSize()));
    });
}

QVariantMap deviceField()
{
    return fields::text(QStringLiteral("device"), QStringLiteral("设备接口（可选）"), QString(),
                        QStringLiteral("留空自动检测第一个 9008 端口"),
                        QStringLiteral("指定 WinUSB 设备接口路径，留空自动选择"), false);
}

QVariantMap programmerField()
{
    return fields::path(QStringLiteral("programmer"), QStringLiteral("Firehose programmer"),
                        QStringLiteral("*.elf *.mbn *.bin *.devprg"),
                        QStringLiteral("引导包内的 devprg/prog_firehose_*.elf"));
}

QVariantMap memoryField()
{
    return fields::choice(QStringLiteral("memory"), QStringLiteral("存储类型"),
                          {QStringLiteral("emmc"), QStringLiteral("ufs"), QStringLiteral("spinor"),
                           QStringLiteral("nvme"), QStringLiteral("sdcard")},
                          0, QStringLiteral("与设备实际存储一致，否则 Firehose 会拒绝"));
}

QVariantMap sectorSizeField()
{
    return fields::choice(QStringLiteral("sector_size"), QStringLiteral("扇区大小"),

                          {QStringLiteral("4096"), QStringLiteral("512"), QStringLiteral("8192")},
                          0, QStringLiteral("eMMC 通常 512，UFS 通常 4096"));
}

void registerSaharaTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.sahara.handshake");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("sahara");
        spec.name = QStringLiteral("Sahara 握手");
        spec.desc = QStringLiteral("与 9008 设备完成 Sahara HELLO 握手，显示协议版本");
        spec.icon = QStringLiteral("cpu");
        spec.keywords = {QStringLiteral("sahara"), QStringLiteral("9008"),
                         QStringLiteral("握手"), QStringLiteral("edl")};
        spec.fields = {deviceField()};
        spec.order = 10;
        addTool(spec, saharaHandshake);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.sahara.upload");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("sahara");
        spec.name = QStringLiteral("Sahara 上传 programmer");
        spec.desc = QStringLiteral("解析 devprg ELF 并通过 Sahara 上传到设备");
        spec.icon = QStringLiteral("upload");
        spec.keywords = {QStringLiteral("sahara"), QStringLiteral("devprg"),
                         QStringLiteral("programmer"), QStringLiteral("elf")};
        spec.fields = {deviceField(), programmerField()};
        spec.order = 20;
        addTool(spec, saharaUpload);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.elf.parse");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("tool");
        spec.name = QStringLiteral("devprg / ELF 解析");
        spec.desc = QStringLiteral("离线解析高通 programmer ELF/MBN 的加载段与地址");
        spec.icon = QStringLiteral("file-text");
        spec.keywords = {QStringLiteral("elf"), QStringLiteral("mbn"),
                         QStringLiteral("devprg"), QStringLiteral("解析")};
        spec.fields = {fields::path(QStringLiteral("file"), QStringLiteral("ELF/MBN 文件"),
                                    QStringLiteral("*.elf *.mbn *.bin *.devprg"))};
        spec.order = 90;
        addTool(spec, elfParse);
    }
}



QString qedlPath()
{
    return util::dataDir() + QStringLiteral("/thirdparty/qedl/qedl.exe");
}

void runQedl(TaskContext& ctx, const QStringList& args, const QString& title)
{
    const QString exe = qedlPath();
    if (!QFileInfo::exists(exe))
        throw std::runtime_error("qedl.exe 未随包提供");
    QProcess process;
    process.setWorkingDirectory(QFileInfo(exe).absolutePath());
    process.setProcessChannelMode(QProcess::MergedChannels);
    ctx.info(QStringLiteral("%1：qedl %2").arg(title, args.join(QLatin1Char(' '))));
    process.start(exe, args);
    if (!process.waitForStarted(5000))
        throw std::runtime_error("无法启动 qedl.exe");
    while (!process.waitForFinished(500)) {
        ctx.checkCanceled();
        if (process.state() == QProcess::NotRunning) break;
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
        throw std::runtime_error("qedl 执行超时");
    }
    const QString output = QString::fromLocal8Bit(process.readAll()).trimmed();
    if (!output.isEmpty()) {
        for (const QString& line : output.split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty()) ctx.info(line.trimmed());
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error(QStringLiteral("qedl 失败，退出码 %1").arg(process.exitCode()).toStdString());
}

void qedlGpt(TaskContext& ctx, const QVariantMap& options)
{
    QStringList args;
    const QString loader = options.value(QStringLiteral("programmer")).toString().trimmed();
    if (!loader.isEmpty()) args << QStringLiteral("--loader") << loader;
    const QString port = options.value(QStringLiteral("port")).toString().trimmed();
    if (!port.isEmpty()) args << QStringLiteral("--port") << port;
    args << QStringLiteral("gpt");
    runQedl(ctx, args, QStringLiteral("qedl 读取 GPT"));
    ctx.ok(QStringLiteral("qedl GPT 读取完成"));
}

void qedlDump(TaskContext& ctx, const QVariantMap& options)
{
    const QString partition = options.value(QStringLiteral("partition")).toString().trimmed();
    const QString output = options.value(QStringLiteral("output")).toString().trimmed();
    if (partition.isEmpty() || output.isEmpty())
        throw std::runtime_error("请填写分区名和输出文件");
    QStringList args;
    const QString loader = options.value(QStringLiteral("programmer")).toString().trimmed();
    if (!loader.isEmpty()) args << QStringLiteral("--loader") << loader;
    const QString port = options.value(QStringLiteral("port")).toString().trimmed();
    if (!port.isEmpty()) args << QStringLiteral("--port") << port;
    args << QStringLiteral("dump") << partition << output;
    runQedl(ctx, args, QStringLiteral("qedl 导出分区"));
    ctx.ok(QStringLiteral("qedl 已导出 %1 → %2").arg(partition, output));
}

void qcomPartitionsXml(TaskContext& ctx, const QVariantMap& options)
{
    const QString programmerPath = options.value(QStringLiteral("programmer")).toString().trimmed();
    const QString output = options.value(QStringLiteral("output")).toString().trimmed();
    if (output.isEmpty())
        throw std::runtime_error("请选择输出的 rawprogram XML 文件");
    const QByteArray image = util::readFile(programmerPath);
    if (image.isEmpty())
        throw std::runtime_error("无法读取 programmer 文件");
    auto transport = proto::openTransport(ChipPlatform::Qualcomm, deviceOption(options), {});
    if (!transport)
        throw std::runtime_error(proto::noDeviceMessage(ChipPlatform::Qualcomm).toStdString());
    flows::QcomOptions qopts;
    const QString memory = options.value(QStringLiteral("memory")).toString().trimmed();
    if (!memory.isEmpty()) qopts.memoryName = memory;
    flows::QcomSession session(*transport);
    session.open(image, qopts);
    const flows::PartitionTable table = session.partitionTable();
    if (!table.valid)
        throw std::runtime_error("设备未返回有效 GPT");
    QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<data>\n");
    for (const flows::PartitionInfo& part : table.partitions) {
        if (part.size == 0) continue;
        const quint64 sectors = part.size / table.sectorSize;
        xml += QStringLiteral("  <program SECTOR_SIZE_IN_BYTES=\"%1\" filename=\"\" "
                              "physical_partition_number=\"0\" start_sector=\"%2\" "
                              "num_partition_sectors=\"%3\" />\n")
                   .arg(table.sectorSize).arg(part.offset / table.sectorSize).arg(sectors);
    }
    xml += QStringLiteral("</data>\n");
    if (!util::writeFile(output, xml.toUtf8()))
        throw std::runtime_error("写入 XML 失败");
    ctx.ok(QStringLiteral("已生成 rawprogram XML：%1（%2 个分区）").arg(output).arg(table.partitions.size()));
}

void registerFirehoseTools()
{
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.firehose.info");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("firehose");
        spec.name = QStringLiteral("Firehose 存储信息");
        spec.desc = QStringLiteral("加载 programmer 进入 Firehose，读取存储信息，可选重启");
        spec.icon = QStringLiteral("hard-drive");
        spec.keywords = {QStringLiteral("firehose"), QStringLiteral("storage"),
                         QStringLiteral("存储"), QStringLiteral("配置")};
        spec.fields = {deviceField(), programmerField(), memoryField(),
                       fields::boolean(QStringLiteral("reset"), QStringLiteral("完成后重启设备"),
                                       false),
                       fields::integer(QStringLiteral("max_payload"),
                                       QStringLiteral("最大负载字节"), 1024 * 1024, 4096,
                                       16 * 1024 * 1024)};
        spec.order = 30;
        addTool(spec, firehoseInfo);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.firehose.read");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("firehose");
        spec.name = QStringLiteral("Firehose 读取扇区");
        spec.desc = QStringLiteral("按起始扇区读取存储数据并保存为文件");
        spec.icon = QStringLiteral("download");
        spec.keywords = {QStringLiteral("firehose"), QStringLiteral("read"),
                         QStringLiteral("读取"), QStringLiteral("扇区")};
        spec.fields = {deviceField(), programmerField(), memoryField(),
                       fields::text(QStringLiteral("start_sector"), QStringLiteral("起始扇区"),
                                    QStringLiteral("0")),
                       fields::text(QStringLiteral("num_sectors"), QStringLiteral("读取扇区数"),
                                    QStringLiteral("1")),
                       sectorSizeField(),
                       fields::path(QStringLiteral("output"), QStringLiteral("输出文件"),
                                    QStringLiteral("*.bin"))};
        spec.order = 40;
        addTool(spec, firehoseRead);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.firehose.program");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("firehose");
        spec.name = QStringLiteral("Firehose 写入扇区");
        spec.desc = QStringLiteral("把文件按扇区写入存储（自动补齐扇区对齐）");
        spec.icon = QStringLiteral("upload");
        spec.keywords = {QStringLiteral("firehose"), QStringLiteral("program"),
                         QStringLiteral("写入"), QStringLiteral("刷写")};
        spec.fields = {deviceField(), programmerField(), memoryField(),
                       fields::text(QStringLiteral("start_sector"), QStringLiteral("起始扇区"),
                                    QStringLiteral("0")),
                       sectorSizeField(),
                       fields::path(QStringLiteral("input"), QStringLiteral("写入文件"),
                                    QStringLiteral("*.bin *.img")),
                       fields::text(QStringLiteral("filename"), QStringLiteral("目标文件名"),
                                    QString(), QStringLiteral("设备端用于日志显示"),
                                    QStringLiteral("留空使用本地文件名"), false),
                       fields::boolean(QStringLiteral("ack_after_data"),
                                       QStringLiteral("数据块后等待 ACK"), true,
                                       QStringLiteral("关闭可兼容只回复一次 ACK 的 programmer"))};
        spec.order = 50;
        addTool(spec, firehoseProgram);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.firehose.patch");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("firehose");
        spec.name = QStringLiteral("Firehose 补丁");
        spec.desc = QStringLiteral("在指定扇区偏移处写入少量字节（如修正 GPT/分区表）");
        spec.icon = QStringLiteral("settings");
        spec.keywords = {QStringLiteral("firehose"), QStringLiteral("patch"),
                         QStringLiteral("补丁"), QStringLiteral("gpt")};
        spec.fields = {deviceField(), programmerField(), memoryField(),
                       fields::text(QStringLiteral("start_sector"), QStringLiteral("起始扇区"),
                                    QStringLiteral("0")),
                       fields::text(QStringLiteral("byte_offset"), QStringLiteral("扇区内偏移"),
                                    QStringLiteral("0")),
                       sectorSizeField(),
                       fields::text(QStringLiteral("value"), QStringLiteral("写入字节（十六进制）"),
                                    QStringLiteral("0x00"), QStringLiteral("如 0x00112233"),
                                    QStringLiteral("按字节顺序填写，自动解析为二进制") )};
        spec.order = 55;
        addTool(spec, firehosePatch);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.firehose.erase");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("firehose");
        spec.name = QStringLiteral("Firehose 擦除扇区");
        spec.desc = QStringLiteral("按扇区擦除存储区域");
        spec.icon = QStringLiteral("zap");
        spec.keywords = {QStringLiteral("firehose"), QStringLiteral("erase"),
                         QStringLiteral("擦除")};
        spec.fields = {deviceField(), programmerField(), memoryField(),
                       fields::text(QStringLiteral("start_sector"), QStringLiteral("起始扇区"),
                                    QStringLiteral("0")),
                       fields::text(QStringLiteral("num_sectors"), QStringLiteral("擦除扇区数"),
                                    QStringLiteral("1")),
                       sectorSizeField()};
        spec.order = 60;
        addTool(spec, firehoseErase);
    }


    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.qedl.gpt");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("qedl");
        spec.name = QStringLiteral("qedl 读取 GPT");
        spec.desc = QStringLiteral("使用开源 qedl 后端握手/加载 loader 并读取 GPT");
        spec.icon = QStringLiteral("terminal");
        spec.keywords = {QStringLiteral("qedl"), QStringLiteral("gpt"), QStringLiteral("sahara")};
        spec.fields = {programmerField(),
                       fields::text(QStringLiteral("port"), QStringLiteral("COM 端口（可选）"), {}, {}, {}, false)};
        spec.order = 36;
        addTool(spec, qedlGpt);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.qedl.dump");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("qedl");
        spec.name = QStringLiteral("qedl 导出分区");
        spec.desc = QStringLiteral("使用开源 qedl 后端导出分区为镜像（支持冷启动 loader）");
        spec.icon = QStringLiteral("download");
        spec.keywords = {QStringLiteral("qedl"), QStringLiteral("dump"), QStringLiteral("backup")};
        spec.fields = {programmerField(),
                       fields::text(QStringLiteral("partition"), QStringLiteral("分区名"), {}),
                       fields::path(QStringLiteral("output"), QStringLiteral("输出镜像"), QStringLiteral("*.img")),
                       fields::text(QStringLiteral("port"), QStringLiteral("COM 端口（可选）"), {}, {}, {}, false)};
        spec.order = 37;
        addTool(spec, qedlDump);
    }
    {
        ToolSpec spec;
        spec.id = QStringLiteral("qcom.partitions.xml");
        spec.category = QStringLiteral("qcom");
        spec.group = QStringLiteral("firehose");
        spec.name = QStringLiteral("生成 rawprogram XML");
        spec.desc = QStringLiteral("读取设备 GPT 并生成 QFIL/QFIL 风格的 rawprogram XML");
        spec.icon = QStringLiteral("file-code");
        spec.keywords = {QStringLiteral("xml"), QStringLiteral("rawprogram"), QStringLiteral("qfil")};
        spec.fields = {deviceField(), programmerField(), memoryField(),
                       fields::path(QStringLiteral("output"), QStringLiteral("输出 XML"),
                                    QStringLiteral("*.xml"))};
        spec.order = 35;
        addTool(spec, qcomPartitionsXml);
    }
}

void registerUtilityTools()
{
    ToolSpec spec;
    spec.id = QStringLiteral("qcom.ports.scan");
    spec.category = QStringLiteral("qcom");
    spec.group = QStringLiteral("tool");
    spec.name = QStringLiteral("检测 9008 端口");
    spec.desc = QStringLiteral("扫描并列出当前连接的高通 EDL 设备接口");
    spec.icon = QStringLiteral("usb");
    spec.keywords = {QStringLiteral("端口"), QStringLiteral("9008"),
                     QStringLiteral("设备"), QStringLiteral("扫描")};
    spec.fields = {};
    spec.order = 100;
    addTool(spec, scanPorts);
}

} // namespace

void registerQcomToolHandlers()
{
    registerSaharaTools();
    registerFirehoseTools();
    registerUtilityTools();
}

} // namespace gf
