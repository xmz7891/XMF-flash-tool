#include "FlowTools.h"

#include "AwFlow.h"
#include "FlowOps.h"
#include "MtkFlow.h"
#include "PartitionTable.h"
#include "QcomFlow.h"
#include "RkFlow.h"
#include "SprdFlow.h"

#include "../../app/ToolModules.h"
#include "TaskManager.h"
#include "ToolRegistry.h"
#include "Util.h"
#include "usb/ProtocolSupport.h"
#include "usb/TaskReporter.h"
#include "usb/ToolOptions.h"
#include "usb/UsbTransport.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <memory>
#include <stdexcept>

namespace gf {

namespace {

using namespace gf::flows;
using usb::ChipPlatform;

// ---------------------------------------------------------------------------
// Option helpers
// ---------------------------------------------------------------------------

QString deviceOption(const QVariantMap& options)
{
    return proto::optionString(options, QStringLiteral("device"));
}

QStringList listOption(const QVariantMap& options, const QString& key)
{
    QString text = proto::optionString(options, key);
    text.replace(QLatin1Char(';'), QLatin1Char(','));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(","));
    QStringList out;
    for (const QString& item : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

QString packetId(const QVariantMap& options)
{
    return proto::optionString(options, QStringLiteral("packet"));
}

QString modelName(const QVariantMap& options)
{
    return proto::optionString(options, QStringLiteral("model"));
}

quint64 numberOption(const QVariantMap& options, const QString& key, quint64 fallback = 0)
{
    return proto::optionUInt(options, key, fallback);
}

void exportPartitionsCsv(const PartitionTable& table, const QString& path)
{
    if (path.trimmed().isEmpty())
        return;
    QByteArray csv = "\xEF\xBB\xBF"; // BOM so Excel reads UTF-8 names correctly
    csv += "index,name,offset,size,sizeHex,type,fileName,hasAddress,isDownload\n";
    for (const auto& partition : table.partitions) {
        csv += QByteArray::number(partition.index) + ',';
        csv += partition.name.toUtf8() + ',';
        csv += QByteArray::number(partition.offset) + ',';
        csv += QByteArray::number(partition.size) + ',';
        csv += QByteArray::number(partition.size, 16) + ',';
        csv += partition.type.toUtf8() + ',';
        csv += partition.fileName.toUtf8() + ',';
        csv += partition.hasAddress ? "true," : "false,";
        csv += partition.isDownload ? "true\n" : "false\n";
    }
    if (!util::ensureDir(QFileInfo(path).absolutePath()) || !util::writeFile(path, csv))
        throw std::runtime_error(QStringLiteral("写入分区表失败：%1").arg(path).toStdString());
}

void logPartitions(TaskContext& ctx, const PartitionTable& table)
{
    ctx.info(QStringLiteral("分区表来源：%1，平台 %2，扇区 %3，共 %4 个分区")
                 .arg(table.source, table.platform)
                 .arg(table.sectorSize)
                 .arg(table.partitions.size()));
    for (const auto& partition : table.partitions) {
        ctx.info(QStringLiteral("%1. %2  偏移 0x%3  大小 0x%4（%5）%6")
                     .arg(partition.index)
                     .arg(partition.name)
                     .arg(partition.offset, 0, 16)
                     .arg(partition.size, 0, 16)
                     .arg(util::formatBytes(qint64(partition.size)))
                     .arg(partition.hasAddress ? QString() : QStringLiteral("  [缺少地址]")));
    }
}

// ---------------------------------------------------------------------------
// Session handles (transport lifetime must outlive the session object)
// ---------------------------------------------------------------------------

struct QcomHandle {
    std::unique_ptr<usb::IUsbTransport> transport;
    std::unique_ptr<QcomSession> session;
};

QcomHandle openQcom(const QVariantMap& options, const proto::Reporter& reporter)
{
    QString programmer = proto::optionString(options, QStringLiteral("programmer"));
    if (programmer.isEmpty())
        programmer = QcomSession::programmerFromPacket(packetId(options), modelName(options));
    if (programmer.isEmpty()) {
        throw std::runtime_error(
            "请选择 Firehose programmer 文件，或填写引导包 id + 机型以自动解析");
    }
    const QByteArray image = util::readFile(programmer);
    if (image.isEmpty())
        throw std::runtime_error(
            QStringLiteral("无法读取 programmer：%1").arg(programmer).toStdString());

    QcomOptions flowOptions;
    QString memory = proto::optionString(options, QStringLiteral("memory"));
    if (memory.isEmpty())
        memory = QcomSession::memoryTypeFromPacket(packetId(options), modelName(options));
    if (!memory.isEmpty())
        flowOptions.memoryName = memory;
    flowOptions.maxPayloadBytes =
        proto::optionInt(options, QStringLiteral("max_payload"), 1024 * 1024);

    QcomHandle handle;
    handle.transport = proto::openTransport(ChipPlatform::Qualcomm, deviceOption(options), reporter);
    handle.session = std::make_unique<QcomSession>(*handle.transport, reporter);
    handle.session->open(image, flowOptions);
    return handle;
}

struct MtkHandle {
    std::unique_ptr<MtkSession> session;
    std::unique_ptr<MtkDaStorageIo> io;
};

MtkHandle openMtk(const QVariantMap& options, const proto::Reporter& reporter)
{
    MtkOptions mtkOptions;
    if (proto::optionString(options, QStringLiteral("da1")).isEmpty()
        || proto::optionString(options, QStringLiteral("da2")).isEmpty()) {
        mtkOptions = MtkOptions::fromPacket(packetId(options), modelName(options));
    }
    const QString da1 = proto::optionString(options, QStringLiteral("da1"));
    const QString da2 = proto::optionString(options, QStringLiteral("da2"));
    if (!da1.isEmpty())
        mtkOptions.da1Path = da1;
    if (!da2.isEmpty())
        mtkOptions.da2Path = da2;
    mtkOptions.devicePath = deviceOption(options);
    mtkOptions.da1Address = quint32(numberOption(options, QStringLiteral("da1_address"),
                                                 mtkOptions.da1Address));
    mtkOptions.da2Address = quint32(numberOption(options, QStringLiteral("da2_address"),
                                                 mtkOptions.da2Address));
    mtkOptions.signatureLength =
        quint32(numberOption(options, QStringLiteral("signature_length"), mtkOptions.signatureLength));
    mtkOptions.daWaitMs = proto::optionInt(options, QStringLiteral("da_wait_ms"), 15000);
    if (mtkOptions.da1Path.isEmpty() || mtkOptions.da2Path.isEmpty())
        throw std::runtime_error("请提供 DA1/DA2 文件，或填写引导包 id + MTK 机型");

    const QString scatterPath = proto::optionString(options, QStringLiteral("scatter"));
    if (scatterPath.isEmpty())
        throw std::runtime_error("请选择 scatter 分区表文件");
    const mtk::ScatterInfo scatter = loadScatterFile(scatterPath);
    if (!scatter.valid) {
        throw std::runtime_error(
            QStringLiteral("scatter 解析失败：%1").arg(scatter.error).toStdString());
    }
    PartitionTable table = PartitionTable::fromScatter(scatter);
    if (!table.valid)
        throw std::runtime_error(QStringLiteral("scatter 分区表无效：%1").arg(table.error).toStdString());

    MtkHandle handle;
    handle.session = std::make_unique<MtkSession>(reporter);
    handle.session->open(mtkOptions);
    handle.io = std::make_unique<MtkDaStorageIo>(handle.session->transport(), table, reporter);
    return handle;
}

struct SprdHandle {
    std::unique_ptr<SprdSession> session;
    std::unique_ptr<SprdFdlStorageIo> io;
    PacLoadResult pac;
};

SprdHandle openSprd(const QVariantMap& options, const proto::Reporter& reporter)
{
    SprdOptions sprdOptions;
    if (proto::optionString(options, QStringLiteral("fdl1")).isEmpty()
        || proto::optionString(options, QStringLiteral("fdl2")).isEmpty()) {
        sprdOptions = SprdOptions::fromPacket(packetId(options), modelName(options));
    }
    const QString fdl1 = proto::optionString(options, QStringLiteral("fdl1"));
    const QString fdl2 = proto::optionString(options, QStringLiteral("fdl2"));
    if (!fdl1.isEmpty())
        sprdOptions.fdl1Path = fdl1;
    if (!fdl2.isEmpty())
        sprdOptions.fdl2Path = fdl2;
    sprdOptions.devicePath = deviceOption(options);
    sprdOptions.fdl1Address = quint32(numberOption(options, QStringLiteral("fdl1_address"),
                                                    sprdOptions.fdl1Address));
    sprdOptions.fdl2Address = quint32(numberOption(options, QStringLiteral("fdl2_address"),
                                                    sprdOptions.fdl2Address));
    sprdOptions.waitMs = proto::optionInt(options, QStringLiteral("wait_ms"), 15000);
    if (sprdOptions.fdl1Path.isEmpty() || sprdOptions.fdl2Path.isEmpty())
        throw std::runtime_error("请提供 FDL1/FDL2 文件，或填写引导包 id + 展锐机型");

    const QString pacPath = proto::optionString(options, QStringLiteral("pac"));
    if (pacPath.isEmpty())
        throw std::runtime_error("请选择 PAC 文件");
    PacLoadResult pac = loadPacFile(pacPath);
    if (!pac.valid)
        throw std::runtime_error(QStringLiteral("PAC 解析失败：%1").arg(pac.error).toStdString());

    const QString mapPath = proto::optionString(options, QStringLiteral("map"));
    if (mapPath.isEmpty()) {
        throw std::runtime_error(
            "PAC 不包含存储地址，请提供分区地址表文件（name,offset,size）");
    }
    applyAddressMapFile(pac.table, mapPath);

    SprdHandle handle;
    handle.pac = pac;
    handle.session = std::make_unique<SprdSession>(reporter);
    handle.session->open(sprdOptions);
    handle.io = std::make_unique<SprdFdlStorageIo>(handle.session->transport(), handle.pac.table,
                                                   reporter);
    return handle;
}

struct RkHandle {
    std::unique_ptr<usb::IUsbTransport> transport;
    std::unique_ptr<RkSession> session;
};

RkHandle openRk(const QVariantMap& options, const proto::Reporter& reporter)
{
    RkOptions rkOptions;
    rkOptions.parameterFile = proto::optionString(options, QStringLiteral("parameter"));
    rkOptions.sectorSize = quint32(proto::optionUInt(options, QStringLiteral("sector_size"), 512));
    RkHandle handle;
    handle.transport = proto::openTransport(ChipPlatform::Rockchip, deviceOption(options), reporter);
    handle.session = std::make_unique<RkSession>(*handle.transport, reporter);
    handle.session->open(rkOptions);
    return handle;
}

struct AwHandle {
    std::unique_ptr<usb::IUsbTransport> transport;
    std::unique_ptr<AwSession> session;
};

AwHandle openAw(const QVariantMap& options, const proto::Reporter& reporter)
{
    AwOptions awOptions;
    awOptions.partitionMapFile = proto::optionString(options, QStringLiteral("map"));
    AwHandle handle;
    handle.transport = proto::openTransport(ChipPlatform::Allwinner, deviceOption(options), reporter);
    handle.session = std::make_unique<AwSession>(*handle.transport, reporter);
    handle.session->open(awOptions);
    return handle;
}

// ---------------------------------------------------------------------------
// Qualcomm tools
// ---------------------------------------------------------------------------

void qcomPartitions(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        auto handle = openQcom(options, reporter);
        const PartitionTable& table = handle.session->partitionTable();
        logPartitions(ctx, table);
        exportPartitionsCsv(table, proto::optionString(options, QStringLiteral("output")));
        ctx.ok(QStringLiteral("已读取 %1 个 GPT 分区").arg(table.partitions.size()));
    });
}

QStringList presetPartitions(const QVariantMap& options, const PartitionTable& table)
{
    QStringList names = listOption(options, QStringLiteral("partitions"));
    const struct {
        const char* key;
        const char* partition;
    } presets[] = {{"userdata", "userdata"}, {"frp", "frp"}, {"metadata", "metadata"},
                   {"misc", "misc"}};
    for (const auto& preset : presets) {
        if (!proto::optionBool(options, QString::fromLatin1(preset.key), false))
            continue;
        if (table.indexOf(QString::fromLatin1(preset.partition)) >= 0
            && !names.contains(QString::fromLatin1(preset.partition))) {
            names.append(QString::fromLatin1(preset.partition));
        }
    }
    return names;
}

void qcomBackup(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("output_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份输出目录");
        auto handle = openQcom(options, reporter);
        QcomFirehoseIo io(*handle.session);
        const PartitionTable table = io.table();
        const QStringList names = presetPartitions(options, table);
        const auto files = backupPartitions(io, directory, names, reporter);
        qint64 total = 0;
        for (const auto& file : files)
            total += file.bytes;
        for (const auto& file : files)
            ctx.info(QStringLiteral("已备份 %1 → %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("备份完成：%1 个分区，%2").arg(files.size()).arg(util::formatBytes(total)));
    });
}

void qcomRestore(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("input_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份目录");
        auto handle = openQcom(options, reporter);
        QcomFirehoseIo io(*handle.session);
        const QStringList names = presetPartitions(options, io.table());
        const auto files = restorePartitions(io, directory, names, reporter);
        for (const auto& file : files)
            ctx.info(QStringLiteral("已恢复 %1 ← %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("恢复完成：%1 个分区").arg(files.size()));
    });
}

void qcomRead(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString partition = proto::optionString(options, QStringLiteral("partition"));
        const QString output = proto::optionString(options, QStringLiteral("output"));
        if (partition.isEmpty() || output.isEmpty())
            throw std::runtime_error("请填写分区名和输出文件");
        auto handle = openQcom(options, reporter);
        QcomFirehoseIo io(*handle.session);
        readPartitionToFile(io, partition, output, reporter);
        ctx.ok(QStringLiteral("分区 %1 已读取到 %2").arg(partition, output));
    });
}

void qcomWrite(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString partition = proto::optionString(options, QStringLiteral("partition"));
        const QString input = proto::optionString(options, QStringLiteral("input"));
        if (partition.isEmpty() || input.isEmpty())
            throw std::runtime_error("请填写分区名和输入镜像");
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("写入分区有风险，请先勾选「确认写入」");
        auto handle = openQcom(options, reporter);
        QcomFirehoseIo io(*handle.session);
        writeFileToPartition(io, partition, input, reporter);
        ctx.ok(QStringLiteral("已写入 %1 → %2").arg(input, partition));
    });
}

void qcomErase(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("擦除分区不可恢复，请先勾选「确认擦除」");
        auto handle = openQcom(options, reporter);
        QcomFirehoseIo io(*handle.session);
        const QStringList names = presetPartitions(options, io.table());
        if (names.isEmpty())
            throw std::runtime_error("请选择要擦除的分区（或勾选 userdata/frp/metadata/misc）");
        erasePartitions(io, names, reporter);
        ctx.ok(QStringLiteral("擦除完成：%1").arg(names.join(QStringLiteral(", "))));
    });
}

void qcomRaw(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString action = proto::optionString(options, QStringLiteral("action"),
                                                   QStringLiteral("读取"));
        const quint64 offset = numberOption(options, QStringLiteral("offset"), 0);
        auto handle = openQcom(options, reporter);
        QcomFirehoseIo io(*handle.session);
        if (action.contains(QStringLiteral("写"))) {
            const QString input = proto::optionString(options, QStringLiteral("input"));
            if (input.isEmpty())
                throw std::runtime_error("请选择要写入的文件");
            const QByteArray data = util::readFile(input);
            if (data.isEmpty())
                throw std::runtime_error(QStringLiteral("无法读取文件：%1").arg(input).toStdString());
            io.writeRaw(offset, data, reporter);
            ctx.ok(QStringLiteral("已写入 0x%1（%2 字节）").arg(offset, 0, 16).arg(data.size()));
        } else {
            quint64 size = numberOption(options, QStringLiteral("size"), 0);
            if (size == 0)
                throw std::runtime_error("请填写读取长度");
            const QByteArray data = io.readRaw(offset, size, reporter);
            const QString output = proto::optionString(options, QStringLiteral("output"));
            if (output.isEmpty())
                throw std::runtime_error("请选择输出文件");
            if (!util::ensureDir(QFileInfo(output).absolutePath()) || !util::writeFile(output, data))
                throw std::runtime_error(QStringLiteral("写入文件失败：%1").arg(output).toStdString());
            ctx.ok(QStringLiteral("已读取 0x%1，%2 字节 → %3").arg(offset, 0, 16).arg(data.size()).arg(output));
        }
    });
}

void qcomChecksum(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString partition = proto::optionString(options, QStringLiteral("partition"));
        if (partition.isEmpty())
            throw std::runtime_error("请填写分区名");
        const bool sha256 = proto::optionString(options, QStringLiteral("algorithm"))
                                .contains(QStringLiteral("SHA"), Qt::CaseInsensitive);
        auto handle = openQcom(options, reporter);
        QcomFirehoseIo io(*handle.session);
        const QString checksum = partitionChecksum(io, partition, sha256, reporter);
        ctx.info(QStringLiteral("%1 校验值（%2）：%3")
                     .arg(partition, sha256 ? QStringLiteral("SHA-256") : QStringLiteral("CRC32"),
                          checksum));
        const QString compare = proto::optionString(options, QStringLiteral("compare"));
        if (!compare.isEmpty()) {
            const QString expected = util::readFile(compare).trimmed();
            if (expected.compare(checksum, Qt::CaseInsensitive) == 0)
                ctx.ok(QStringLiteral("校验值与 %1 一致").arg(compare));
            else
                ctx.warn(QStringLiteral("校验值与 %1 不一致（期望 %2）").arg(compare, expected));
        } else {
            ctx.ok(QStringLiteral("校验完成"));
        }
    });
}

void qcomSlot(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString slot = proto::optionString(options, QStringLiteral("slot"));
        auto handle = openQcom(options, reporter);
        handle.session->setActiveSlot(slot);
        ctx.ok(QStringLiteral("已请求切换到启动槽 _%1").arg(slot));
    });
}

// ---------------------------------------------------------------------------
// MediaTek tools
// ---------------------------------------------------------------------------

void mtkPartitions(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const QString path = proto::optionString(options, QStringLiteral("scatter"));
        if (path.isEmpty())
            throw std::runtime_error("请选择 scatter 文件");
        const mtk::ScatterInfo scatter = loadScatterFile(path);
        const PartitionTable table = PartitionTable::fromScatter(scatter);
        if (!table.valid)
            throw std::runtime_error(QStringLiteral("scatter 解析失败：%1").arg(table.error).toStdString());
        logPartitions(ctx, table);
        exportPartitionsCsv(table, proto::optionString(options, QStringLiteral("output")));
        ctx.ok(QStringLiteral("scatter 分区表：%1 个分区").arg(table.partitions.size()));
    });
}

void mtkBackup(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("output_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份输出目录");
        auto handle = openMtk(options, reporter);
        const QStringList names = listOption(options, QStringLiteral("partitions"));
        const auto files = backupPartitions(*handle.io, directory, names, reporter);
        for (const auto& file : files)
            ctx.info(QStringLiteral("已备份 %1 → %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("备份完成：%1 个分区").arg(files.size()));
    });
}

void mtkRestore(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("input_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份目录");
        auto handle = openMtk(options, reporter);
        const QStringList names = listOption(options, QStringLiteral("partitions"));
        const auto files = restorePartitions(*handle.io, directory, names, reporter);
        for (const auto& file : files)
            ctx.info(QStringLiteral("已恢复 %1 ← %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("恢复完成：%1 个分区").arg(files.size()));
    });
}

void mtkRead(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString partition = proto::optionString(options, QStringLiteral("partition"));
        const QString output = proto::optionString(options, QStringLiteral("output"));
        if (partition.isEmpty() || output.isEmpty())
            throw std::runtime_error("请填写分区名和输出文件");
        auto handle = openMtk(options, reporter);
        readPartitionToFile(*handle.io, partition, output, reporter);
        ctx.ok(QStringLiteral("分区 %1 已读取到 %2").arg(partition, output));
    });
}

void mtkWrite(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString partition = proto::optionString(options, QStringLiteral("partition"));
        const QString input = proto::optionString(options, QStringLiteral("input"));
        if (partition.isEmpty() || input.isEmpty())
            throw std::runtime_error("请填写分区名和输入镜像");
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("写入分区有风险，请先勾选「确认写入」");
        auto handle = openMtk(options, reporter);
        writeFileToPartition(*handle.io, partition, input, reporter);
        ctx.ok(QStringLiteral("已写入 %1 → %2").arg(input, partition));
    });
}

void mtkErase(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("擦除分区不可恢复，请先勾选「确认擦除」");
        auto handle = openMtk(options, reporter);
        const QStringList names = listOption(options, QStringLiteral("partitions"));
        if (names.isEmpty())
            throw std::runtime_error("请填写要擦除的分区（逗号分隔）");
        erasePartitions(*handle.io, names, reporter);
        ctx.ok(QStringLiteral("擦除完成：%1").arg(names.join(QStringLiteral(", "))));
    });
}

void mtkFlashScatter(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("按 scatter 刷写会覆盖分区，请先勾选「确认刷写」");
        const QString scatterPath = proto::optionString(options, QStringLiteral("scatter"));
        const QString firmwareDir = proto::optionString(options, QStringLiteral("firmware_dir"));
        if (scatterPath.isEmpty())
            throw std::runtime_error("请选择 scatter 文件");
        const QStringList only = listOption(options, QStringLiteral("partitions"));
        const ScatterFlashPlan plan = buildScatterFlashPlan(scatterPath, firmwareDir, only);
        if (!plan.valid)
            throw std::runtime_error(QStringLiteral("刷写计划无效：%1").arg(plan.error).toStdString());
        for (const auto& missing : plan.missingFiles)
            ctx.warn(QStringLiteral("固件缺失：%1").arg(missing));
        auto handle = openMtk(options, reporter);
        const int written = flashScatterPlan(*handle.io, plan, reporter);
        ctx.ok(QStringLiteral("scatter 刷写完成：%1 个分区").arg(written));
    });
}

void mtkPreloader(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString action = proto::optionString(options, QStringLiteral("action"),
                                                   QStringLiteral("备份 preloader"));
        const QString scatterPath = proto::optionString(options, QStringLiteral("scatter"));
        if (scatterPath.isEmpty())
            throw std::runtime_error("请选择 scatter 文件");
        auto handle = openMtk(options, reporter);
        if (action.contains(QStringLiteral("刷"))) {
            const QString input = proto::optionString(options, QStringLiteral("input"));
            if (input.isEmpty())
                throw std::runtime_error("请选择 preloader 文件");
            if (!proto::optionBool(options, QStringLiteral("confirm"), false))
                throw std::runtime_error("刷写 preloader 风险很高，请先勾选「确认刷写」");
            writeFileToPartition(*handle.io, QStringLiteral("preloader"), input, reporter);
            ctx.ok(QStringLiteral("preloader 已刷写：%1").arg(input));
        } else {
            const QString directory = proto::optionString(options, QStringLiteral("output_dir"));
            if (directory.isEmpty())
                throw std::runtime_error("请选择备份目录");
            const auto files = backupPartitions(*handle.io, directory, {QStringLiteral("preloader")},
                                                reporter);
            for (const auto& file : files)
                ctx.info(QStringLiteral("preloader 已备份 → %1").arg(file.path));
            ctx.ok(QStringLiteral("preloader 备份完成"));
        }
    });
}

// ---------------------------------------------------------------------------
// Spreadtrum tools
// ---------------------------------------------------------------------------

void sprdPartitions(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const QString pacPath = proto::optionString(options, QStringLiteral("pac"));
        if (pacPath.isEmpty())
            throw std::runtime_error("请选择 PAC 文件");
        PacLoadResult loaded = loadPacFile(pacPath);
        if (!loaded.valid)
            throw std::runtime_error(QStringLiteral("PAC 解析失败：%1").arg(loaded.error).toStdString());
        const QString mapPath = proto::optionString(options, QStringLiteral("map"));
        if (!mapPath.isEmpty())
            applyAddressMapFile(loaded.table, mapPath);
        logPartitions(ctx, loaded.table);
        exportPartitionsCsv(loaded.table, proto::optionString(options, QStringLiteral("output")));
        ctx.ok(QStringLiteral("PAC 镜像表：%1 个镜像").arg(loaded.table.partitions.size()));
    });
}

void sprdFlash(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("刷写 PAC 会覆盖分区，请先勾选「确认刷写」");
        auto handle = openSprd(options, reporter);
        const QStringList only = listOption(options, QStringLiteral("partitions"));
        const int written = flashPac(*handle.io, handle.pac, only, reporter);
        ctx.ok(QStringLiteral("PAC 刷写完成：%1 个镜像").arg(written));
    });
}

void sprdBackup(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("output_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份输出目录");
        auto handle = openSprd(options, reporter);
        const QStringList names = listOption(options, QStringLiteral("partitions"));
        const auto files = backupPartitions(*handle.io, directory, names, reporter);
        for (const auto& file : files)
            ctx.info(QStringLiteral("已备份 %1 → %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("备份完成：%1 个分区").arg(files.size()));
    });
}

void sprdRead(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString partition = proto::optionString(options, QStringLiteral("partition"));
        const QString output = proto::optionString(options, QStringLiteral("output"));
        if (partition.isEmpty() || output.isEmpty())
            throw std::runtime_error("请填写分区名和输出文件");
        auto handle = openSprd(options, reporter);
        readPartitionToFile(*handle.io, partition, output, reporter);
        ctx.ok(QStringLiteral("分区 %1 已读取到 %2").arg(partition, output));
    });
}

void sprdErase(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("擦除分区不可恢复，请先勾选「确认擦除」");
        auto handle = openSprd(options, reporter);
        const QStringList names = listOption(options, QStringLiteral("partitions"));
        if (names.isEmpty())
            throw std::runtime_error("请填写要擦除的分区（逗号分隔）");
        erasePartitions(*handle.io, names, reporter);
        ctx.ok(QStringLiteral("擦除完成：%1").arg(names.join(QStringLiteral(", "))));
    });
}

// ---------------------------------------------------------------------------
// Rockchip tools
// ---------------------------------------------------------------------------

void rkPartitions(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString parameter = proto::optionString(options, QStringLiteral("parameter"));
        PartitionTable table;
        if (!parameter.isEmpty()) {
            const QByteArray text = util::readFile(parameter);
            if (text.isEmpty())
                throw std::runtime_error(
                    QStringLiteral("无法读取 parameter 文件：%1").arg(parameter).toStdString());
            table = PartitionTable::fromParameterText(QString::fromUtf8(text),
                                                      QStringLiteral("rk"));
            if (!table.valid)
                throw std::runtime_error(
                    QStringLiteral("parameter 解析失败：%1").arg(table.error).toStdString());
        } else {
            auto handle = openRk(options, reporter);
            table = handle.session->partitionTable();
        }
        logPartitions(ctx, table);
        exportPartitionsCsv(table, proto::optionString(options, QStringLiteral("output")));
        ctx.ok(QStringLiteral("Rockchip 分区表：%1 个分区").arg(table.partitions.size()));
    });
}

void rkBackup(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("output_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份输出目录");
        auto handle = openRk(options, reporter);
        RkStorageIo io(*handle.session);
        const auto files = backupPartitions(io, directory, listOption(options, QStringLiteral("partitions")),
                                            reporter);
        for (const auto& file : files)
            ctx.info(QStringLiteral("已备份 %1 → %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("备份完成：%1 个分区").arg(files.size()));
    });
}

void rkRestore(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("input_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份目录");
        auto handle = openRk(options, reporter);
        RkStorageIo io(*handle.session);
        const auto files = restorePartitions(io, directory, listOption(options, QStringLiteral("partitions")),
                                             reporter);
        for (const auto& file : files)
            ctx.info(QStringLiteral("已恢复 %1 ← %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("恢复完成：%1 个分区").arg(files.size()));
    });
}

void rkRead(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString partition = proto::optionString(options, QStringLiteral("partition"));
        const QString output = proto::optionString(options, QStringLiteral("output"));
        if (partition.isEmpty() || output.isEmpty())
            throw std::runtime_error("请填写分区名和输出文件");
        auto handle = openRk(options, reporter);
        RkStorageIo io(*handle.session);
        readPartitionToFile(io, partition, output, reporter);
        ctx.ok(QStringLiteral("分区 %1 已读取到 %2").arg(partition, output));
    });
}

void rkWrite(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString partition = proto::optionString(options, QStringLiteral("partition"));
        const QString input = proto::optionString(options, QStringLiteral("input"));
        if (partition.isEmpty() || input.isEmpty())
            throw std::runtime_error("请填写分区名和输入镜像");
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("写入分区有风险，请先勾选「确认写入」");
        auto handle = openRk(options, reporter);
        RkStorageIo io(*handle.session);
        writeFileToPartition(io, partition, input, reporter);
        ctx.ok(QStringLiteral("已写入 %1 → %2").arg(input, partition));
    });
}

void rkErase(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        if (!proto::optionBool(options, QStringLiteral("confirm"), false))
            throw std::runtime_error("擦除分区不可恢复，请先勾选「确认擦除」");
        auto handle = openRk(options, reporter);
        RkStorageIo io(*handle.session);
        const QStringList names = listOption(options, QStringLiteral("partitions"));
        if (names.isEmpty())
            throw std::runtime_error("请填写要擦除的分区（逗号分隔）");
        erasePartitions(io, names, reporter);
        ctx.ok(QStringLiteral("擦除完成：%1").arg(names.join(QStringLiteral(", "))));
    });
}

void rkRaw(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString action = proto::optionString(options, QStringLiteral("action"),
                                                   QStringLiteral("读取"));
        const quint64 offset = numberOption(options, QStringLiteral("offset"), 0);
        auto handle = openRk(options, reporter);
        RkStorageIo io(*handle.session);
        if (action.contains(QStringLiteral("写"))) {
            const QString input = proto::optionString(options, QStringLiteral("input"));
            if (input.isEmpty())
                throw std::runtime_error("请选择要写入的文件");
            const QByteArray data = util::readFile(input);
            if (data.isEmpty())
                throw std::runtime_error(QStringLiteral("无法读取文件：%1").arg(input).toStdString());
            io.writeRaw(offset, data, reporter);
            ctx.ok(QStringLiteral("已写入 0x%1（%2 字节）").arg(offset, 0, 16).arg(data.size()));
        } else {
            quint64 size = numberOption(options, QStringLiteral("size"), 0);
            if (size == 0)
                throw std::runtime_error("请填写读取长度");
            const QByteArray data = io.readRaw(offset, size, reporter);
            const QString output = proto::optionString(options, QStringLiteral("output"));
            if (output.isEmpty())
                throw std::runtime_error("请选择输出文件");
            if (!util::ensureDir(QFileInfo(output).absolutePath()) || !util::writeFile(output, data))
                throw std::runtime_error(QStringLiteral("写入文件失败：%1").arg(output).toStdString());
            ctx.ok(QStringLiteral("已读取 %1 字节 → %2").arg(data.size()).arg(output));
        }
    });
}

void rkLoader(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString action = proto::optionString(options, QStringLiteral("action"),
                                                   QStringLiteral("切换到 Loader"));
        auto handle = openRk(options, reporter);
        if (action.contains(QStringLiteral("Loader"), Qt::CaseInsensitive))
            handle.session->rebootToLoader();
        else
            handle.session->reset();
        ctx.ok(QStringLiteral("已发送重启命令：%1").arg(action));
    });
}

// ---------------------------------------------------------------------------
// Allwinner tools
// ---------------------------------------------------------------------------

void awHello(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        auto transport =
            proto::openTransport(ChipPlatform::Allwinner, deviceOption(options), reporter);
        AwSession session(*transport, reporter);
        const aw::VersionInfo info = session.open(AwOptions{});
        ctx.info(QStringLiteral("FEL BootROM：%1，SoC：%2（0x%3），版本 0x%4")
                     .arg(info.magic, aw::socName(info.id))
                     .arg(info.id, 8, 16, QLatin1Char('0'))
                     .arg(info.version, 8, 16, QLatin1Char('0')));
        ctx.ok(QStringLiteral("全志 FEL 握手成功"));
    });
}

void awPartitions(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const QString mapPath = proto::optionString(options, QStringLiteral("map"));
        if (mapPath.isEmpty())
            throw std::runtime_error("FEL 没有分区表接口，请提供分区地址表文件（name,offset,size）");
        const QByteArray text = util::readFile(mapPath);
        if (text.isEmpty())
            throw std::runtime_error(QStringLiteral("无法读取分区地址表：%1").arg(mapPath).toStdString());
        const PartitionTable table = PartitionTable::fromPartitionMapText(QString::fromUtf8(text),
                                                                          QStringLiteral("aw"));
        if (!table.valid)
            throw std::runtime_error(QStringLiteral("分区地址表无效：%1").arg(table.error).toStdString());
        logPartitions(ctx, table);
        exportPartitionsCsv(table, proto::optionString(options, QStringLiteral("output")));
        ctx.ok(QStringLiteral("全志分区地址表：%1 个分区").arg(table.partitions.size()));
    });
}

void awBackup(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("output_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份输出目录");
        auto handle = openAw(options, reporter);
        AwMemoryIo io(*handle.session);
        const auto files = backupPartitions(io, directory, listOption(options, QStringLiteral("partitions")),
                                            reporter);
        for (const auto& file : files)
            ctx.info(QStringLiteral("已备份 %1 → %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("备份完成：%1 个分区").arg(files.size()));
    });
}

void awRestore(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const QString directory = proto::optionString(options, QStringLiteral("input_dir"));
        if (directory.isEmpty())
            throw std::runtime_error("请选择备份目录");
        auto handle = openAw(options, reporter);
        AwMemoryIo io(*handle.session);
        const auto files = restorePartitions(io, directory, listOption(options, QStringLiteral("partitions")),
                                             reporter);
        for (const auto& file : files)
            ctx.info(QStringLiteral("已恢复 %1 ← %2").arg(file.partition, file.path));
        ctx.ok(QStringLiteral("恢复完成：%1 个分区").arg(files.size()));
    });
}

void awRead(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const quint64 address = numberOption(options, QStringLiteral("address"), 0);
        const quint64 size = numberOption(options, QStringLiteral("size"), 0);
        const QString output = proto::optionString(options, QStringLiteral("output"));
        if (size == 0 || output.isEmpty())
            throw std::runtime_error("请填写读取长度和输出文件");
        auto handle = openAw(options, reporter);
        const QByteArray data = handle.session->readRaw(address, size);
        if (!util::ensureDir(QFileInfo(output).absolutePath()) || !util::writeFile(output, data))
            throw std::runtime_error(QStringLiteral("写入文件失败：%1").arg(output).toStdString());
        ctx.ok(QStringLiteral("已读取 0x%1，%2 字节 → %3").arg(address, 0, 16).arg(data.size()).arg(output));
    });
}

void awWrite(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const quint64 address = numberOption(options, QStringLiteral("address"), 0);
        const QString input = proto::optionString(options, QStringLiteral("input"));
        if (input.isEmpty())
            throw std::runtime_error("请选择要写入的文件");
        const QByteArray data = util::readFile(input);
        if (data.isEmpty())
            throw std::runtime_error(QStringLiteral("无法读取文件：%1").arg(input).toStdString());
        auto handle = openAw(options, reporter);
        handle.session->writeRaw(address, data);
        if (proto::optionBool(options, QStringLiteral("exec"), false)) {
            handle.session->execRaw(address);
            ctx.info(QStringLiteral("已执行 0x%1").arg(address, 0, 16));
        }
        ctx.ok(QStringLiteral("已写入 0x%1（%2 字节）").arg(address, 0, 16).arg(data.size()));
    });
}

void awExec(TaskContext& ctx, const QVariantMap& options)
{
    proto::runGuarded(ctx, [&] {
        const auto reporter = proto::reporterFor(ctx);
        const quint64 address = numberOption(options, QStringLiteral("address"), 0);
        auto handle = openAw(options, reporter);
        handle.session->execRaw(address);
        ctx.ok(QStringLiteral("已执行 0x%1").arg(address, 0, 16));
    });
}

// ---------------------------------------------------------------------------
// Field helpers
// ---------------------------------------------------------------------------

QVariantMap deviceField()
{
    return fields::text(QStringLiteral("device"), QStringLiteral("设备接口（可选）"), QString(),
                        QStringLiteral("留空自动检测对应平台端口"),
                        QStringLiteral("指定 WinUSB 设备接口路径，留空自动选择"), false);
}

QVariantMap packetField()
{
    return fields::text(QStringLiteral("packet"), QStringLiteral("引导包 id（可选）"), QString(),
                        QStringLiteral("例如 package_mtk_edl-master"),
                        QStringLiteral("填写后可自动解析 DA/FDL/programmer 与地址"), false);
}

QVariantMap modelField()
{
    return fields::text(QStringLiteral("model"), QStringLiteral("机型（可选）"), QString(),
                        QStringLiteral("引导包中的机型名"), QStringLiteral("与引导包 id 配合使用"),
                        false);
}

QVariantMap partitionField()
{
    return fields::text(QStringLiteral("partition"), QStringLiteral("分区名"), QString(),
                        QStringLiteral("例如 boot / userdata / super"),
                        QStringLiteral("可先用「读取分区表」查看名称"), true);
}

QVariantMap partitionsField()
{
    return fields::text(QStringLiteral("partitions"), QStringLiteral("分区列表（逗号分隔，空=全部）"),
                        QString(), QStringLiteral("boot,userdata"),
                        QStringLiteral("留空表示全部（备份/恢复）"), false);
}

QVariantMap pathField(const QString& key, const QString& label, const QString& filter = {},
                      const QString& hint = {}, bool required = true)
{
    return fields::path(key, label, filter, hint, required);
}

QVariantMap dirField(const QString& key, const QString& label)
{
    return fields::dir(key, label);
}

QVariantMap confirmField(const QString& text)
{
    return fields::boolean(QStringLiteral("confirm"), text, false);
}

QVariantMap scatterField()
{
    return fields::path(QStringLiteral("scatter"), QStringLiteral("scatter 文件"),
                        QStringLiteral("*.txt"));
}

QVariantMap pacField()
{
    return fields::path(QStringLiteral("pac"), QStringLiteral("PAC 文件"),
                        QStringLiteral("*.pac *.bin"));
}

QVariantMap mapField()
{
    return fields::path(QStringLiteral("map"), QStringLiteral("分区地址表"),
                        QStringLiteral("*.txt *.csv"),
                        QStringLiteral("每行 name,offset,size（支持 0x 十六进制）"), true);
}

QVariantMap programmerField()
{
    return fields::path(QStringLiteral("programmer"), QStringLiteral("Firehose programmer"),
                        QStringLiteral("*.elf *.mbn *.bin *.devprg"),
                        QStringLiteral("留空时按引导包 id + 机型自动解析"), false);
}

QVariantMap memoryField()
{
    return fields::choice(QStringLiteral("memory"), QStringLiteral("存储类型"),
                          {QStringLiteral("ufs"), QStringLiteral("emmc"), QStringLiteral("spinor"),
                           QStringLiteral("nvme"), QStringLiteral("sdcard")},
                          0, QStringLiteral("留空时按引导包机型属性判断"), false);
}

QVariantMap daFields(QVariantList* fieldsOut)
{
    fieldsOut->append(fields::path(QStringLiteral("da1"), QStringLiteral("DA1（可选）"),
                                   QStringLiteral("*.bin"),
                                   QStringLiteral("留空时按引导包机型解析"), false));
    fieldsOut->append(fields::path(QStringLiteral("da2"), QStringLiteral("DA2（可选）"),
                                   QStringLiteral("*.bin"),
                                   QStringLiteral("留空时按引导包机型解析"), false));
    fieldsOut->append(fields::hex(QStringLiteral("da1_address"), QStringLiteral("DA1 地址"),
                                  QStringLiteral("0x200000")));
    fieldsOut->append(fields::hex(QStringLiteral("da2_address"), QStringLiteral("DA2 地址"),
                                  QStringLiteral("0x40000000")));
    return {};
}

QVariantMap fdlFields(QVariantList* fieldsOut)
{
    fieldsOut->append(fields::path(QStringLiteral("fdl1"), QStringLiteral("FDL1（可选）"),
                                   QStringLiteral("*.bin"),
                                   QStringLiteral("留空时按引导包机型解析"), false));
    fieldsOut->append(fields::path(QStringLiteral("fdl2"), QStringLiteral("FDL2（可选）"),
                                   QStringLiteral("*.bin"),
                                   QStringLiteral("留空时按引导包机型解析"), false));
    fieldsOut->append(fields::hex(QStringLiteral("fdl1_address"), QStringLiteral("FDL1 地址"),
                                  QStringLiteral("0x65000800")));
    fieldsOut->append(fields::hex(QStringLiteral("fdl2_address"), QStringLiteral("FDL2 地址"),
                                  QStringLiteral("0x9efffe00")));
    return {};
}

ToolSpec baseSpec(const QString& id, const QString& category, const QString& name,
                  const QString& description, const QString& icon, int order)
{
    ToolSpec spec;
    spec.id = id;
    spec.category = category;
    spec.group = QStringLiteral("flow");
    spec.name = name;
    spec.desc = description;
    spec.icon = icon;
    spec.keywords = {QStringLiteral("流水线"), QStringLiteral("备份"), QStringLiteral("刷写")};
    spec.order = order;
    return spec;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerQcomFlowTools()
{
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.partitions"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 读取分区表"),
                             QStringLiteral("进入 Firehose 后读取设备 GPT 分区表（可导出 CSV）"),
                             QStringLiteral("hard-drive"), 200);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       fields::path(QStringLiteral("output"), QStringLiteral("导出 CSV（可选）"),
                                    QStringLiteral("*.csv"), QString(), false)};
        addTool(spec, qcomPartitions);
    }
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.backup"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 分区备份"),
                             QStringLiteral("备份全部或指定 GPT 分区到目录（<platform>_<partition>_<date>.img）"),
                             QStringLiteral("download"), 201);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       partitionsField(), dirField(QStringLiteral("output_dir"), QStringLiteral("备份目录"))};
        addTool(spec, qcomBackup);
    }
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.restore"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 分区恢复"),
                             QStringLiteral("从备份目录恢复全部或指定分区"),
                             QStringLiteral("upload"), 202);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       partitionsField(), dirField(QStringLiteral("input_dir"), QStringLiteral("备份目录"))};
        addTool(spec, qcomRestore);
    }
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.read"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 读取分区"),
                             QStringLiteral("把指定 GPT 分区读取为镜像文件"), QStringLiteral("download"),
                             203);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       partitionField(),
                       pathField(QStringLiteral("output"), QStringLiteral("输出文件"), QStringLiteral("*.img"))};
        addTool(spec, qcomRead);
    }
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.write"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 写入分区"),
                             QStringLiteral("把镜像文件写入指定 GPT 分区（需确认）"), QStringLiteral("upload"),
                             204);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       partitionField(),
                       pathField(QStringLiteral("input"), QStringLiteral("输入镜像"), QStringLiteral("*.img *.bin")),
                       confirmField(QStringLiteral("确认写入该分区"))};
        addTool(spec, qcomWrite);
    }
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.erase"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 擦除分区"),
                             QStringLiteral("擦除指定分区，或勾选 userdata/frp/metadata/misc 预设"),
                             QStringLiteral("zap"), 205);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       partitionsField(),
                       fields::boolean(QStringLiteral("userdata"), QStringLiteral("擦除 userdata"), false),
                       fields::boolean(QStringLiteral("frp"), QStringLiteral("擦除 frp"), false),
                       fields::boolean(QStringLiteral("metadata"), QStringLiteral("擦除 metadata"), false),
                       fields::boolean(QStringLiteral("misc"), QStringLiteral("擦除 misc"), false),
                       confirmField(QStringLiteral("确认擦除"))};
        addTool(spec, qcomErase);
    }
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.raw"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 偏移读写"),
                             QStringLiteral("按字节偏移直接读写 Firehose 存储"), QStringLiteral("terminal"),
                             206);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                                      {QStringLiteral("读取"), QStringLiteral("写入")}, 0),
                       fields::text(QStringLiteral("offset"), QStringLiteral("偏移（字节）"),
                                    QStringLiteral("0x0")),
                       fields::text(QStringLiteral("size"), QStringLiteral("读取长度（字节）"),
                                    QStringLiteral("0x1000")),
                       pathField(QStringLiteral("input"), QStringLiteral("写入文件（写入时）"),
                                 QStringLiteral("*.bin")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出文件（读取时）"),
                                 QStringLiteral("*.bin"))};
        addTool(spec, qcomRaw);
    }
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.slot"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 设置启动槽"),
                             QStringLiteral("改写 misc 中的 A/B 启动槽（best effort）"),
                             QStringLiteral("rotate-ccw"), 207);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       fields::choice(QStringLiteral("slot"), QStringLiteral("启动槽"),
                                      {QStringLiteral("a"), QStringLiteral("b")}, 0)};
        addTool(spec, qcomSlot);
    }
    {
        auto spec = baseSpec(QStringLiteral("qcom.flow.checksum"),
                             QStringLiteral("qcom"), QStringLiteral("QCOM 分区校验"),
                             QStringLiteral("读取分区并计算 CRC32/SHA-256，可与文件比对"),
                             QStringLiteral("check-check"), 208);
        spec.fields = {deviceField(), packetField(), modelField(), programmerField(), memoryField(),
                       partitionField(),
                       fields::choice(QStringLiteral("algorithm"), QStringLiteral("算法"),
                                      {QStringLiteral("CRC32"), QStringLiteral("SHA-256")}, 0),
                       pathField(QStringLiteral("compare"), QStringLiteral("比对文件（可选）"),
                                 QStringLiteral("*.txt"), QStringLiteral("文件内容为十六进制校验值"),
                                 false)};
        addTool(spec, qcomChecksum);
    }
}

void registerMtkFlowTools()
{
    {
        auto spec = baseSpec(QStringLiteral("mtk.flow.partitions"),
                             QStringLiteral("mtk"), QStringLiteral("MTK 解析 scatter"),
                             QStringLiteral("解析 scatter 分区表（离线，可导出 CSV）"),
                             QStringLiteral("list-checks"), 300);
        spec.fields = {scatterField(),
                       fields::path(QStringLiteral("output"), QStringLiteral("导出 CSV（可选）"),
                                    QStringLiteral("*.csv"), QString(), false)};
        addTool(spec, mtkPartitions);
    }
    {
        QVariantList extra;
        daFields(&extra);
        auto spec = baseSpec(QStringLiteral("mtk.flow.backup"),
                             QStringLiteral("mtk"), QStringLiteral("MTK 分区备份"),
                             QStringLiteral("加载 DA 后按 scatter 分区表备份全部或指定分区"),
                             QStringLiteral("download"), 301);
        spec.fields = {deviceField(), scatterField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionsField(), dirField(QStringLiteral("output_dir"), QStringLiteral("备份目录"))};
        addTool(spec, mtkBackup);
    }
    {
        QVariantList extra;
        daFields(&extra);
        auto spec = baseSpec(QStringLiteral("mtk.flow.restore"),
                             QStringLiteral("mtk"), QStringLiteral("MTK 分区恢复"),
                             QStringLiteral("从备份目录恢复全部或指定 scatter 分区"),
                             QStringLiteral("upload"), 302);
        spec.fields = {deviceField(), scatterField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionsField(), dirField(QStringLiteral("input_dir"), QStringLiteral("备份目录"))};
        addTool(spec, mtkRestore);
    }
    {
        QVariantList extra;
        daFields(&extra);
        auto spec = baseSpec(QStringLiteral("mtk.flow.read"),
                             QStringLiteral("mtk"), QStringLiteral("MTK 读取分区"),
                             QStringLiteral("读取 scatter 分区为镜像文件"), QStringLiteral("download"), 303);
        spec.fields = {deviceField(), scatterField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionField(),
                        pathField(QStringLiteral("output"), QStringLiteral("输出文件"), QStringLiteral("*.img"))};
        addTool(spec, mtkRead);
    }
    {
        QVariantList extra;
        daFields(&extra);
        auto spec = baseSpec(QStringLiteral("mtk.flow.write"),
                             QStringLiteral("mtk"), QStringLiteral("MTK 写入分区"),
                             QStringLiteral("把镜像写入 scatter 分区（需确认）"), QStringLiteral("upload"),
                             304);
        spec.fields = {deviceField(), scatterField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionField(),
                        pathField(QStringLiteral("input"), QStringLiteral("输入镜像"), QStringLiteral("*.img *.bin")),
                        confirmField(QStringLiteral("确认写入该分区"))};
        addTool(spec, mtkWrite);
    }
    {
        QVariantList extra;
        daFields(&extra);
        auto spec = baseSpec(QStringLiteral("mtk.flow.erase"),
                             QStringLiteral("mtk"), QStringLiteral("MTK 擦除分区"),
                             QStringLiteral("擦除 scatter 分区（需确认）"), QStringLiteral("zap"), 305);
        spec.fields = {deviceField(), scatterField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionsField(), confirmField(QStringLiteral("确认擦除"))};
        addTool(spec, mtkErase);
    }
    {
        QVariantList extra;
        daFields(&extra);
        auto spec = baseSpec(QStringLiteral("mtk.flow.flash"),
                             QStringLiteral("mtk"), QStringLiteral("MTK 按 scatter 刷写"),
                             QStringLiteral("按 scatter 的 is_download 项批量刷写固件目录（需确认）"),
                             QStringLiteral("play"), 306);
        spec.fields = {deviceField(), scatterField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {dirField(QStringLiteral("firmware_dir"), QStringLiteral("固件目录")),
                        partitionsField(), confirmField(QStringLiteral("确认按 scatter 刷写"))};
        addTool(spec, mtkFlashScatter);
    }
    {
        QVariantList extra;
        daFields(&extra);
        auto spec = baseSpec(QStringLiteral("mtk.flow.preloader"),
                             QStringLiteral("mtk"), QStringLiteral("MTK preloader 备份/刷写"),
                             QStringLiteral("备份或刷写 preloader 分区（刷写需确认）"), QStringLiteral("cpu"),
                             307);
        spec.fields = {deviceField(), scatterField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                                       {QStringLiteral("备份 preloader"), QStringLiteral("刷写 preloader")}, 0),
                        pathField(QStringLiteral("input"), QStringLiteral("preloader 文件（刷写时）"),
                                  QStringLiteral("*.bin")),
                        dirField(QStringLiteral("output_dir"), QStringLiteral("备份目录（备份时）")),
                        confirmField(QStringLiteral("确认刷写 preloader"))};
        addTool(spec, mtkPreloader);
    }
}

void registerSprdFlowTools()
{
    {
        auto spec = baseSpec(QStringLiteral("sprd.flow.partitions"),
                             QStringLiteral("sprd"), QStringLiteral("SPRD PAC 镜像表"),
                             QStringLiteral("解析 PAC 镜像表并可套用分区地址表（离线）"),
                             QStringLiteral("layers"), 400);
        spec.fields = {pacField(), mapField(),
                       fields::path(QStringLiteral("output"), QStringLiteral("导出 CSV（可选）"),
                                    QStringLiteral("*.csv"), QString(), false)};
        addTool(spec, sprdPartitions);
    }
    {
        QVariantList extra;
        fdlFields(&extra);
        auto spec = baseSpec(QStringLiteral("sprd.flow.flash"),
                             QStringLiteral("sprd"), QStringLiteral("SPRD 刷写 PAC"),
                             QStringLiteral("按 PAC 镜像表 + 分区地址表刷写（需确认）"),
                             QStringLiteral("play"), 401);
        spec.fields = {deviceField(), pacField(), mapField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionsField(), confirmField(QStringLiteral("确认刷写"))};
        addTool(spec, sprdFlash);
    }
    {
        QVariantList extra;
        fdlFields(&extra);
        auto spec = baseSpec(QStringLiteral("sprd.flow.backup"),
                             QStringLiteral("sprd"), QStringLiteral("SPRD 分区备份"),
                             QStringLiteral("通过 FDL 读取分区并备份到目录"), QStringLiteral("download"),
                             402);
        spec.fields = {deviceField(), pacField(), mapField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionsField(), dirField(QStringLiteral("output_dir"), QStringLiteral("备份目录"))};
        addTool(spec, sprdBackup);
    }
    {
        QVariantList extra;
        fdlFields(&extra);
        auto spec = baseSpec(QStringLiteral("sprd.flow.read"),
                             QStringLiteral("sprd"), QStringLiteral("SPRD 读取分区"),
                             QStringLiteral("通过 FDL 读取分区为镜像文件"), QStringLiteral("download"),
                             403);
        spec.fields = {deviceField(), pacField(), mapField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionField(),
                        pathField(QStringLiteral("output"), QStringLiteral("输出文件"), QStringLiteral("*.img"))};
        addTool(spec, sprdRead);
    }
    {
        QVariantList extra;
        fdlFields(&extra);
        auto spec = baseSpec(QStringLiteral("sprd.flow.erase"),
                             QStringLiteral("sprd"), QStringLiteral("SPRD 擦除分区"),
                             QStringLiteral("通过 FDL 擦除分区（需确认）"), QStringLiteral("zap"), 404);
        spec.fields = {deviceField(), pacField(), mapField(), packetField(), modelField()};
        spec.fields += extra;
        spec.fields += {partitionsField(), confirmField(QStringLiteral("确认擦除"))};
        addTool(spec, sprdErase);
    }
}

void registerRkFlowTools()
{
    {
        auto spec = baseSpec(QStringLiteral("rk.flow.partitions"),
                             QStringLiteral("rk"), QStringLiteral("RK 分区表"),
                             QStringLiteral("读取设备 GPT 或解析 parameter.txt 分区表"),
                             QStringLiteral("hard-drive"), 500);
        spec.fields = {deviceField(),
                       fields::path(QStringLiteral("parameter"), QStringLiteral("parameter.txt（可选）"),
                                    QStringLiteral("*.txt"),
                                    QStringLiteral("提供后离线解析，不再读设备 GPT"), false),
                       fields::path(QStringLiteral("output"), QStringLiteral("导出 CSV（可选）"),
                                    QStringLiteral("*.csv"), QString(), false)};
        addTool(spec, rkPartitions);
    }
    {
        auto spec = baseSpec(QStringLiteral("rk.flow.backup"),
                             QStringLiteral("rk"), QStringLiteral("RK 分区备份"),
                             QStringLiteral("备份全部或指定 Rockchip 分区"), QStringLiteral("download"),
                             501);
        spec.fields = {deviceField(),
                       fields::path(QStringLiteral("parameter"), QStringLiteral("parameter.txt（可选）"),
                                    QStringLiteral("*.txt"), QString(), false),
                       partitionsField(), dirField(QStringLiteral("output_dir"), QStringLiteral("备份目录"))};
        addTool(spec, rkBackup);
    }
    {
        auto spec = baseSpec(QStringLiteral("rk.flow.restore"),
                             QStringLiteral("rk"), QStringLiteral("RK 分区恢复"),
                             QStringLiteral("从备份目录恢复全部或指定分区"), QStringLiteral("upload"),
                             502);
        spec.fields = {deviceField(),
                       fields::path(QStringLiteral("parameter"), QStringLiteral("parameter.txt（可选）"),
                                    QStringLiteral("*.txt"), QString(), false),
                       partitionsField(), dirField(QStringLiteral("input_dir"), QStringLiteral("备份目录"))};
        addTool(spec, rkRestore);
    }
    {
        auto spec = baseSpec(QStringLiteral("rk.flow.read"),
                             QStringLiteral("rk"), QStringLiteral("RK 读取分区"),
                             QStringLiteral("把 Rockchip 分区读取为镜像文件"), QStringLiteral("download"),
                             503);
        spec.fields = {deviceField(),
                       fields::path(QStringLiteral("parameter"), QStringLiteral("parameter.txt（可选）"),
                                    QStringLiteral("*.txt"), QString(), false),
                       partitionField(),
                       pathField(QStringLiteral("output"), QStringLiteral("输出文件"), QStringLiteral("*.img"))};
        addTool(spec, rkRead);
    }
    {
        auto spec = baseSpec(QStringLiteral("rk.flow.write"),
                             QStringLiteral("rk"), QStringLiteral("RK 写入分区"),
                             QStringLiteral("把镜像写入 Rockchip 分区（需确认）"), QStringLiteral("upload"),
                             504);
        spec.fields = {deviceField(),
                       fields::path(QStringLiteral("parameter"), QStringLiteral("parameter.txt（可选）"),
                                    QStringLiteral("*.txt"), QString(), false),
                       partitionField(),
                       pathField(QStringLiteral("input"), QStringLiteral("输入镜像"), QStringLiteral("*.img *.bin")),
                       confirmField(QStringLiteral("确认写入该分区"))};
        addTool(spec, rkWrite);
    }
    {
        auto spec = baseSpec(QStringLiteral("rk.flow.erase"),
                             QStringLiteral("rk"), QStringLiteral("RK 擦除分区"),
                             QStringLiteral("擦除 Rockchip 分区（需确认）"), QStringLiteral("zap"), 505);
        spec.fields = {deviceField(),
                       fields::path(QStringLiteral("parameter"), QStringLiteral("parameter.txt（可选）"),
                                    QStringLiteral("*.txt"), QString(), false),
                       partitionsField(), confirmField(QStringLiteral("确认擦除"))};
        addTool(spec, rkErase);
    }
    {
        auto spec = baseSpec(QStringLiteral("rk.flow.raw"),
                             QStringLiteral("rk"), QStringLiteral("RK 偏移读写"),
                             QStringLiteral("按字节偏移读写 RockUSB 存储"), QStringLiteral("terminal"),
                             506);
        spec.fields = {deviceField(),
                       fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                                      {QStringLiteral("读取"), QStringLiteral("写入")}, 0),
                       fields::text(QStringLiteral("offset"), QStringLiteral("偏移（字节）"),
                                    QStringLiteral("0x0")),
                       fields::text(QStringLiteral("size"), QStringLiteral("读取长度（字节）"),
                                    QStringLiteral("0x1000")),
                       pathField(QStringLiteral("input"), QStringLiteral("写入文件"), QStringLiteral("*.bin")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出文件"), QStringLiteral("*.bin"))};
        addTool(spec, rkRaw);
    }
    {
        auto spec = baseSpec(QStringLiteral("rk.flow.loader"),
                             QStringLiteral("rk"), QStringLiteral("RK 重启 / 切换 Loader"),
                             QStringLiteral("发送 RockUSB 重启命令，切换普通模式或 Loader 模式"),
                             QStringLiteral("rotate-ccw"), 507);
        spec.fields = {deviceField(),
                       fields::choice(QStringLiteral("action"), QStringLiteral("操作"),
                                      {QStringLiteral("切换到 Loader"), QStringLiteral("普通重启")}, 0)};
        addTool(spec, rkLoader);
    }
}

void registerAwFlowTools()
{
    {
        auto spec = baseSpec(QStringLiteral("aw.flow.hello"),
                             QStringLiteral("aw"), QStringLiteral("FEL 握手"),
                             QStringLiteral("FEL_VER 握手，读取 BootROM 版本与 SoC"), QStringLiteral("sun"),
                             600);
        spec.fields = {deviceField()};
        addTool(spec, awHello);
    }
    {
        auto spec = baseSpec(QStringLiteral("aw.flow.partitions"),
                             QStringLiteral("aw"), QStringLiteral("全志分区地址表"),
                             QStringLiteral("解析 FEL 分区地址表（name,offset,size）"),
                             QStringLiteral("list-checks"), 601);
        spec.fields = {mapField(),
                       fields::path(QStringLiteral("output"), QStringLiteral("导出 CSV（可选）"),
                                    QStringLiteral("*.csv"), QString(), false)};
        addTool(spec, awPartitions);
    }
    {
        auto spec = baseSpec(QStringLiteral("aw.flow.backup"),
                             QStringLiteral("aw"), QStringLiteral("全志分区备份"),
                             QStringLiteral("按地址表备份全部分区或指定分区"), QStringLiteral("download"),
                             602);
        spec.fields = {deviceField(), mapField(), partitionsField(),
                       dirField(QStringLiteral("output_dir"), QStringLiteral("备份目录"))};
        addTool(spec, awBackup);
    }
    {
        auto spec = baseSpec(QStringLiteral("aw.flow.restore"),
                             QStringLiteral("aw"), QStringLiteral("全志分区恢复"),
                             QStringLiteral("按地址表从备份恢复分区"), QStringLiteral("upload"), 603);
        spec.fields = {deviceField(), mapField(), partitionsField(),
                       dirField(QStringLiteral("input_dir"), QStringLiteral("备份目录"))};
        addTool(spec, awRestore);
    }
    {
        auto spec = baseSpec(QStringLiteral("aw.flow.read"),
                             QStringLiteral("aw"), QStringLiteral("FEL 读取内存"),
                             QStringLiteral("从 FEL 地址读取内存到文件"), QStringLiteral("download"), 604);
        spec.fields = {deviceField(),
                       fields::hex(QStringLiteral("address"), QStringLiteral("地址"),
                                   QStringLiteral("0x40000000")),
                       fields::text(QStringLiteral("size"), QStringLiteral("长度（字节）"),
                                    QStringLiteral("0x1000")),
                       pathField(QStringLiteral("output"), QStringLiteral("输出文件"), QStringLiteral("*.bin"))};
        addTool(spec, awRead);
    }
    {
        auto spec = baseSpec(QStringLiteral("aw.flow.write"),
                             QStringLiteral("aw"), QStringLiteral("FEL 写入 / 执行"),
                             QStringLiteral("把文件写入 FEL 地址，可选执行"), QStringLiteral("upload"),
                             605);
        spec.fields = {deviceField(),
                       fields::hex(QStringLiteral("address"), QStringLiteral("地址"),
                                   QStringLiteral("0x40000000")),
                       pathField(QStringLiteral("input"), QStringLiteral("写入文件"),
                                 QStringLiteral("*.bin *.img")),
                       fields::boolean(QStringLiteral("exec"), QStringLiteral("写入后执行"), false)};
        addTool(spec, awWrite);
    }
    {
        auto spec = baseSpec(QStringLiteral("aw.flow.exec"),
                             QStringLiteral("aw"), QStringLiteral("FEL 执行地址"),
                             QStringLiteral("跳转执行指定 FEL 地址"), QStringLiteral("play"), 606);
        spec.fields = {deviceField(),
                       fields::hex(QStringLiteral("address"), QStringLiteral("地址"),
                                   QStringLiteral("0x40000000"))};
        addTool(spec, awExec);
    }
}

} // namespace

void registerFlowToolHandlers()
{
    registerQcomFlowTools();
    registerMtkFlowTools();
    registerSprdFlowTools();
    registerRkFlowTools();
    registerAwFlowTools();
}

} // namespace gf
