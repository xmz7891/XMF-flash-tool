#include "MtkFlow.h"

#include "Util.h"
#include "packets/PacketStore.h"
#include "usb/UsbIo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <stdexcept>

namespace gf::flows {

namespace {

quint64 parseAddress(const QString& text, quint64 fallback)
{
    QString value = text.trimmed();
    if (value.isEmpty())
        return fallback;
    bool ok = false;
    quint64 parsed = value.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                         ? value.mid(2).toULongLong(&ok, 16)
                         : value.toULongLong(&ok, 10);
    return ok ? parsed : fallback;
}

const PartitionInfo& lookup(const PartitionTable& table, const QString& name)
{
    const int index = table.indexOf(name);
    if (index < 0)
        throw std::runtime_error(QStringLiteral("scatter 中没有分区 %1").arg(name).toStdString());
    return table.partitions[index];
}

} // namespace

MtkOptions MtkOptions::fromPacket(const QString& packetId, const QString& modelName)
{
    MtkOptions options;
    if (packetId.trimmed().isEmpty())
        return options;
    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();
    const QVariantMap model = store.findModel(packetId, modelName);
    options.da1Path = store.modelResource(packetId, modelName, QStringLiteral("DA1"));
    options.da2Path = store.modelResource(packetId, modelName, QStringLiteral("DA2"));
    options.da1Address = quint32(parseAddress(model.value(QStringLiteral("DA1Address")).toString(),
                                              options.da1Address));
    options.da2Address = quint32(parseAddress(model.value(QStringLiteral("DA2Address")).toString(),
                                              options.da2Address));
    return options;
}

QString MtkOptions::preloaderFromPacket(const QString& packetId, const QString& modelName)
{
    if (packetId.trimmed().isEmpty())
        return {};
    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();
    return store.modelResource(packetId, modelName, QStringLiteral("Preloader"));
}

MtkSession::MtkSession(proto::Reporter reporter) : m_reporter(std::move(reporter)) {}

MtkSession::~MtkSession()
{
    if (m_transport) {
        m_transport->close();
        m_transport.reset();
    }
}

void MtkSession::open(const MtkOptions& options)
{
    if (options.da1Path.isEmpty() || options.da2Path.isEmpty()) {
        throw std::runtime_error(
            "请提供 DA1/DA2 文件，或选择引导包中的 MTK 机型（自动解析 DA 路径与地址）");
    }
    const QByteArray da1 = util::readFile(options.da1Path);
    const QByteArray da2 = util::readFile(options.da2Path);
    if (da1.isEmpty() || da2.isEmpty()) {
        throw std::runtime_error(
            QStringLiteral("无法读取 DA 文件：%1")
                .arg(da1.isEmpty() ? options.da1Path : options.da2Path)
                .toStdString());
    }

    m_reporter.progress(5, QStringLiteral("查找 MTK BROM 端口…"));
    m_transport = proto::openTransport(usb::ChipPlatform::MediaTek, options.devicePath,
                                       m_reporter);
    {
        mtk::brom::Client brom(*m_transport, m_reporter);
        brom.handshake();
        m_reporter.progress(10, QStringLiteral("发送 DA1…"));
        brom.sendDa(options.da1Address, da1, options.signatureLength, 15000, 10, 30);
        brom.jumpDa(options.da1Address);
    }

    m_transport->close();
    m_transport.reset();
    m_reporter.progress(45, QStringLiteral("等待 DA 端口重新枚举（%1 ms）…").arg(options.daWaitMs));
    const usb::PortInfo port = mtk::da::waitForDaPort(options.daWaitMs, m_reporter);
    if (port.devicePath.isEmpty()) {
        throw std::runtime_error(
            QStringLiteral("DA1 启动后未重新枚举到 MTK 端口（等待 %1 ms）").arg(options.daWaitMs)
                .toStdString());
    }
    m_transport.reset(usb::createTransport(port));
    if (!m_transport)
        throw std::runtime_error("当前系统无法创建 MTK USB 传输实例");
    if (!m_transport->open(port)) {
        throw std::runtime_error(
            QStringLiteral("打开 DA 端口失败：%1").arg(m_transport->errorString()).toStdString());
    }

    mtk::da::Client daClient(*m_transport, m_reporter);
    m_reporter.progress(50, QStringLiteral("下载 DA2…"));
    daClient.downloadDa2(da2, options.da2Address, options.signatureLength, true, 55, 35);
    m_reporter.progress(90, QStringLiteral("DA2 已启动"));
}

usb::IUsbTransport& MtkSession::transport()
{
    if (!m_transport)
        throw std::runtime_error("MTK DA 会话尚未建立");
    return *m_transport;
}

MtkDaStorageIo::MtkDaStorageIo(usb::IUsbTransport& transport, PartitionTable table,
                               proto::Reporter reporter)
    : m_transport(transport), m_reporter(std::move(reporter)), m_table(std::move(table))
{
}

QByteArray MtkDaStorageIo::command(quint8 command, const QByteArray& payload,
                                   const QByteArray& writeData)
{
    QByteArray header;
    header.append(char(command));
    header += proto::u32be(quint32(payload.size()));
    usb::writeAll(m_transport, header + payload, 10000, m_reporter);
    if (!writeData.isEmpty())
        usb::writeAll(m_transport, writeData, 60000, m_reporter);

    const QByteArray statusBytes = usb::readExact(m_transport, 1, 60000, m_reporter);
    const quint8 status = quint8(statusBytes.at(0));
    const QByteArray lengthBytes = usb::readExact(m_transport, 4, 60000, m_reporter);
    const quint32 length = proto::readU32be(lengthBytes, 0);
    if (length > 32u * 1024u * 1024u)
        throw std::runtime_error("MTK DA 响应长度异常");
    const QByteArray data = length > 0
                                ? usb::readExact(m_transport, length, 60000, m_reporter)
                                : QByteArray();
    if (status != 0) {
        throw std::runtime_error(
            QStringLiteral("MTK DA 存储命令 0x%1 失败：状态 0x%2")
                .arg(command, 2, 16, QLatin1Char('0'))
                .arg(status, 2, 16, QLatin1Char('0'))
                .toStdString());
    }
    return data;
}

QByteArray MtkDaStorageIo::readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    constexpr quint64 kChunk = 1024 * 1024;
    QByteArray out;
    quint64 done = 0;
    while (done < size) {
        reporter.checkCanceled();
        const quint64 chunk = qMin<quint64>(kChunk, size - done);
        QByteArray payload = proto::u64be(offset + done) + proto::u32be(quint32(chunk));
        const QByteArray data = command(kCmdRead, payload);
        if (quint64(data.size()) != chunk) {
            throw std::runtime_error(
                QStringLiteral("MTK DA 读取长度不符：期望 %1，实际 %2").arg(chunk).arg(data.size())
                    .toStdString());
        }
        out.append(data);
        done += chunk;
        if (size > 0)
            reporter.progress(int(100.0 * done / size));
    }
    return out;
}

void MtkDaStorageIo::writeRaw(quint64 offset, const QByteArray& data,
                              const proto::Reporter& reporter)
{
    constexpr quint64 kChunk = 1024 * 1024;
    quint64 done = 0;
    while (done < quint64(data.size())) {
        reporter.checkCanceled();
        const quint64 chunk = qMin<quint64>(kChunk, quint64(data.size()) - done);
        const QByteArray payload = proto::u64be(offset + done) + proto::u32be(quint32(chunk));
        command(kCmdWrite, payload, data.mid(int(done), int(chunk)));
        done += chunk;
        reporter.progress(int(100.0 * done / data.size()));
    }
}

void MtkDaStorageIo::eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    if (size == 0)
        return;
    reporter.checkCanceled();
    const QByteArray payload = proto::u64be(offset) + proto::u32be(quint32(size));
    command(kCmdErase, payload);
    reporter.progress(100);
}

QByteArray MtkDaStorageIo::readPartition(const QString& name, const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_table, name);
    reporter.status(QStringLiteral("MTK 读取分区 %1（%2）")
                        .arg(partition.name, util::formatBytes(qint64(partition.size))));
    return readRaw(partition.offset, partition.size, reporter);
}

void MtkDaStorageIo::writePartition(const QString& name, const QByteArray& data,
                                    const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_table, name);
    reporter.status(QStringLiteral("MTK 写入分区 %1（%2）")
                        .arg(partition.name, util::formatBytes(data.size())));
    writeRaw(partition.offset, data, reporter);
}

void MtkDaStorageIo::erasePartition(const QString& name, const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_table, name);
    reporter.status(QStringLiteral("MTK 擦除分区 %1").arg(partition.name));
    eraseRaw(partition.offset, partition.size, reporter);
}

const PartitionInfo& MtkDaStorageIo::requirePartition(const QString& name) const
{
    return lookup(m_table, name);
}

mtk::ScatterInfo loadScatterFile(const QString& path)
{
    return mtk::ScatterInfo::parseFile(path);
}

ScatterFlashPlan buildScatterFlashPlan(const QString& scatterPath, const QString& firmwareDir,
                                       const QStringList& onlyPartitions)
{
    ScatterFlashPlan plan;
    plan.firmwareDir = firmwareDir.trimmed();
    if (plan.firmwareDir.isEmpty())
        plan.firmwareDir = QFileInfo(scatterPath).absolutePath();

    const mtk::ScatterInfo scatter = loadScatterFile(scatterPath);
    if (!scatter.valid) {
        plan.error = scatter.error;
        return plan;
    }
    plan.table = PartitionTable::fromScatter(scatter);
    if (!plan.table.valid) {
        plan.error = plan.table.error;
        return plan;
    }

    for (const auto& partition : plan.table.partitions) {
        if (!partition.isDownload)
            continue;
        const QString fileName = partition.fileName.trimmed();
        if (fileName.isEmpty() || fileName.compare(QLatin1String("NONE"), Qt::CaseInsensitive) == 0)
            continue;
        if (!onlyPartitions.isEmpty() && !onlyPartitions.contains(partition.name))
            continue;
        ScatterFlashItem item;
        item.partition = partition.name;
        item.file = QDir(plan.firmwareDir).filePath(fileName);
        item.offset = partition.offset;
        item.size = partition.size;
        item.exists = QFile::exists(item.file);
        if (!item.exists)
            plan.missingFiles.append(item.file);
        plan.items.append(item);
    }

    if (plan.items.isEmpty()) {
        plan.error = QStringLiteral("scatter 中没有需要下载的分区（检查 is_download 与 file_name）");
        return plan;
    }
    plan.valid = true;
    return plan;
}

int flashScatterPlan(PartitionIo& io, const ScatterFlashPlan& plan, const proto::Reporter& reporter)
{
    if (!plan.valid)
        throw std::runtime_error(
            QStringLiteral("scatter 刷写计划无效：%1").arg(plan.error).toStdString());
    if (!plan.missingFiles.isEmpty()) {
        QStringList shown = plan.missingFiles.mid(0, 5);
        throw std::runtime_error(
            QStringLiteral("以下固件文件缺失：%1%2")
                .arg(shown.join(QStringLiteral(", ")),
                     plan.missingFiles.size() > shown.size() ? QStringLiteral(" 等") : QString())
                .toStdString());
    }

    int written = 0;
    for (const auto& item : plan.items) {
        reporter.checkCanceled();
        const QByteArray data = util::readFile(item.file);
        if (data.isEmpty())
            throw std::runtime_error(
                QStringLiteral("无法读取固件文件：%1").arg(item.file).toStdString());
        reporter.progress(int(100.0 * written / plan.items.size()),
                          QStringLiteral("刷写 %1（%2）")
                              .arg(item.partition, util::formatBytes(data.size())));
        io.writePartition(item.partition, data, reporter);
        ++written;
    }
    reporter.progress(100);
    return written;
}

} // namespace gf::flows
