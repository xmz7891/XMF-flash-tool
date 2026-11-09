#include "SprdFlow.h"

#include "Util.h"
#include "packets/PacketStore.h"
#include "usb/UsbIo.h"

#include <QFileInfo>
#include <stdexcept>

namespace gf::flows {

namespace {

constexpr qsizetype kChunk = 32 * 1024; // U2S payload limit is 65535 bytes

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
        throw std::runtime_error(QStringLiteral("分区地址表中没有 %1").arg(name).toStdString());
    return table.partitions[index];
}

QByteArray addressSize(quint64 address, quint64 size)
{
    if (address > 0xFFFFFFFFull || size > 0xFFFFFFFFull || address + size > 0x100000000ull)
        throw std::runtime_error("SPRD 地址/大小超出 32 位范围");
    return proto::u32be(quint32(address)) + proto::u32be(quint32(size));
}

} // namespace

SprdOptions SprdOptions::fromPacket(const QString& packetId, const QString& modelName)
{
    SprdOptions options;
    if (packetId.trimmed().isEmpty())
        return options;
    auto& store = packets::PacketStore::instance();
    if (store.packetIds().isEmpty())
        store.refresh();
    const QVariantMap model = store.findModel(packetId, modelName);
    options.fdl1Path = store.modelResource(packetId, modelName, QStringLiteral("FDL1"));
    options.fdl2Path = store.modelResource(packetId, modelName, QStringLiteral("FDL2"));
    options.fdl1Address = quint32(parseAddress(model.value(QStringLiteral("FDL1Addr")).toString(),
                                               options.fdl1Address));
    options.fdl2Address = quint32(parseAddress(model.value(QStringLiteral("FDL2Addr")).toString(),
                                               options.fdl2Address));
    return options;
}

SprdSession::SprdSession(proto::Reporter reporter) : m_reporter(std::move(reporter)) {}

SprdSession::~SprdSession()
{
    if (m_transport) {
        m_transport->close();
        m_transport.reset();
    }
}

void SprdSession::open(const SprdOptions& options)
{
    if (options.fdl1Path.isEmpty() || options.fdl2Path.isEmpty()) {
        throw std::runtime_error(
            "请提供 FDL1/FDL2 文件，或选择引导包中的展锐机型（自动解析路径与地址）");
    }
    const QByteArray fdl1 = util::readFile(options.fdl1Path);
    const QByteArray fdl2 = util::readFile(options.fdl2Path);
    if (fdl1.isEmpty() || fdl2.isEmpty()) {
        throw std::runtime_error(
            QStringLiteral("无法读取 FDL 文件：%1")
                .arg(fdl1.isEmpty() ? options.fdl1Path : options.fdl2Path)
                .toStdString());
    }

    m_reporter.progress(5, QStringLiteral("查找展锐 U2S 端口…"));
    m_transport = proto::openTransport(usb::ChipPlatform::Spreadtrum, options.devicePath,
                                       m_reporter);
    {
        sprd::fdl::Client client(*m_transport, m_reporter);
        client.connect();
        m_reporter.info(QStringLiteral("FDL 版本：%1").arg(client.readVersion()));
        m_reporter.progress(10, QStringLiteral("下载 FDL1…"));
        client.downloadFdl(options.fdl1Address, fdl1, 0, 10, 35);
    }

    m_transport->close();
    m_transport.reset();
    m_reporter.progress(50, QStringLiteral("等待 FDL 端口重新枚举（%1 ms）…").arg(options.waitMs));
    const usb::PortInfo port = sprd::fdl::waitForPort(options.waitMs, m_reporter);
    if (port.devicePath.isEmpty()) {
        throw std::runtime_error(
            QStringLiteral("FDL1 启动后未重新枚举到展锐端口（等待 %1 ms）").arg(options.waitMs)
                .toStdString());
    }
    m_transport.reset(usb::createTransport(port));
    if (!m_transport)
        throw std::runtime_error("当前系统无法创建展锐 USB 传输实例");
    if (!m_transport->open(port)) {
        throw std::runtime_error(
            QStringLiteral("打开 FDL 端口失败：%1").arg(m_transport->errorString()).toStdString());
    }
    sprd::fdl::Client client(*m_transport, m_reporter);
    client.connect();
    m_reporter.progress(55, QStringLiteral("下载 FDL2…"));
    client.downloadFdl(options.fdl2Address, fdl2, 0, 55, 35);
    m_reporter.progress(90, QStringLiteral("FDL2 已启动"));
}

usb::IUsbTransport& SprdSession::transport()
{
    if (!m_transport)
        throw std::runtime_error("展锐 FDL 会话尚未建立");
    return *m_transport;
}

SprdFdlStorageIo::SprdFdlStorageIo(usb::IUsbTransport& transport, PartitionTable table,
                                   proto::Reporter reporter)
    : m_transport(transport),
      m_reporter(std::move(reporter)),
      m_table(std::move(table)),
      m_fdl(transport, m_reporter)
{
}

sprd::u2s::Frame SprdFdlStorageIo::command(quint8 type, const QByteArray& payload,
                                           const QByteArray& writeData)
{
    m_reporter.checkCanceled();
    QByteArray framePayload = payload;
    framePayload.append(writeData);
    if (framePayload.size() > sprd::u2s::kMaxPayload)
        throw std::runtime_error("SPRD 命令负载超过 U2S 帧上限");
    m_fdl.link().send(type, framePayload, 10000);
    sprd::u2s::Frame frame = m_fdl.link().receive(60000);
    if (!frame.valid) {
        throw std::runtime_error(
            QStringLiteral("SPRD FDL 命令 0x%1 失败：%2")
                .arg(type, 2, 16, QLatin1Char('0'))
                .arg(frame.error)
                .toStdString());
    }
    return frame;
}

QByteArray SprdFdlStorageIo::readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    constexpr quint8 kReadRawData = 0x0D;
    QByteArray out;
    quint64 done = 0;
    while (done < size) {
        reporter.checkCanceled();
        const quint64 chunk = qMin<quint64>(kChunk, size - done);
        const sprd::u2s::Frame frame = command(kReadRawData, addressSize(offset + done, chunk));
        QByteArray data = frame.payload;
        if (quint64(data.size()) < chunk) {
            if (data.isEmpty()) {
                data = usb::readExact(m_transport, qint64(chunk), 60000, m_reporter);
            } else {
                throw std::runtime_error(
                    QStringLiteral("SPRD 读取长度不足：期望 %1，实际 %2")
                        .arg(chunk)
                        .arg(data.size())
                        .toStdString());
            }
        }
        out.append(data.left(int(chunk)));
        done += chunk;
        if (size > 0)
            reporter.progress(int(100.0 * done / size));
    }
    return out;
}

void SprdFdlStorageIo::writeRaw(quint64 offset, const QByteArray& data,
                                const proto::Reporter& reporter)
{
    constexpr quint8 kWriteRawData = 0x0E;
    quint64 done = 0;
    while (done < quint64(data.size())) {
        reporter.checkCanceled();
        const quint64 chunk = qMin<quint64>(kChunk, quint64(data.size()) - done);
        const QByteArray header = addressSize(offset + done, chunk);
        const sprd::u2s::Frame frame = command(kWriteRawData, header,
                                               data.mid(int(done), int(chunk)));
        if (!frame.payload.isEmpty() && quint8(frame.payload.at(0)) != 0) {
            throw std::runtime_error(
                QStringLiteral("SPRD 写入失败：状态 0x%1")
                    .arg(quint8(frame.payload.at(0)), 2, 16, QLatin1Char('0'))
                    .toStdString());
        }
        done += chunk;
        reporter.progress(int(100.0 * done / data.size()));
    }
}

void SprdFdlStorageIo::eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter)
{
    constexpr quint8 kEraseFlash = 0x0A;
    if (size == 0)
        return;
    reporter.checkCanceled();
    const sprd::u2s::Frame frame = command(kEraseFlash, addressSize(offset, size));
    if (!frame.payload.isEmpty() && quint8(frame.payload.at(0)) != 0) {
        throw std::runtime_error(
            QStringLiteral("SPRD 擦除失败：状态 0x%1")
                .arg(quint8(frame.payload.at(0)), 2, 16, QLatin1Char('0'))
                .toStdString());
    }
    reporter.progress(100);
}

QByteArray SprdFdlStorageIo::readPartition(const QString& name, const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_table, name);
    reporter.status(QStringLiteral("SPRD 读取分区 %1（%2）")
                        .arg(partition.name, util::formatBytes(qint64(partition.size))));
    return readRaw(partition.offset, partition.size, reporter);
}

void SprdFdlStorageIo::writePartition(const QString& name, const QByteArray& data,
                                      const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_table, name);
    reporter.status(QStringLiteral("SPRD 写入分区 %1（%2）")
                        .arg(partition.name, util::formatBytes(data.size())));
    writeRaw(partition.offset, data, reporter);
}

void SprdFdlStorageIo::erasePartition(const QString& name, const proto::Reporter& reporter)
{
    const PartitionInfo& partition = lookup(m_table, name);
    reporter.status(QStringLiteral("SPRD 擦除分区 %1").arg(partition.name));
    eraseRaw(partition.offset, partition.size, reporter);
}

const PartitionInfo& SprdFdlStorageIo::requirePartition(const QString& name) const
{
    return lookup(m_table, name);
}

PacLoadResult loadPacFile(const QString& path)
{
    PacLoadResult result;
    result.file = util::readFile(path);
    if (result.file.isEmpty()) {
        result.error = QStringLiteral("无法读取 PAC 文件：%1").arg(path);
        return result;
    }
    result.pac = sprd::PacInfo::parse(result.file);
    if (!result.pac.valid) {
        result.error = result.pac.error;
        return result;
    }
    result.table = PartitionTable::fromPac(result.pac);
    if (!result.table.valid) {
        result.error = result.table.error;
        return result;
    }
    result.valid = true;
    return result;
}

void applyAddressMap(PartitionTable& table, const QString& mapText)
{
    const PartitionTable map = PartitionTable::fromPartitionMapText(mapText, table.platform);
    if (!map.valid)
        throw std::runtime_error(
            QStringLiteral("分区地址表无效：%1").arg(map.error).toStdString());
    for (const auto& entry : map.partitions) {
        const int index = table.indexOf(entry.name);
        if (index < 0)
            continue;
        table.partitions[index].offset = entry.offset;
        if (entry.size > 0)
            table.partitions[index].size = entry.size;
        table.partitions[index].hasAddress = true;
    }
}

void applyAddressMapFile(PartitionTable& table, const QString& mapPath)
{
    const QByteArray text = util::readFile(mapPath);
    if (text.isEmpty())
        throw std::runtime_error(
            QStringLiteral("无法读取分区地址表：%1").arg(mapPath).toStdString());
    applyAddressMap(table, QString::fromUtf8(text));
}

int flashPac(PartitionIo& io, const PacLoadResult& loaded, const QStringList& only,
             const proto::Reporter& reporter)
{
    if (!loaded.valid)
        throw std::runtime_error(
            QStringLiteral("PAC 无效：%1").arg(loaded.error).toStdString());

    const PartitionTable table = io.table();
    int written = 0;
    int skipped = 0;
    for (int i = 0; i < loaded.table.partitions.size(); ++i) {
        reporter.checkCanceled();
        const PartitionInfo& source = loaded.table.partitions[i];
        if (!only.isEmpty() && !only.contains(source.name))
            continue;
        const int index = table.indexOf(source.name);
        if (index < 0) {
            ++skipped;
            continue;
        }
        const PartitionInfo& target = table.partitions[index];
        if (!target.hasAddress || target.offset == 0) {
            ++skipped;
            continue;
        }
        QString error;
        const QByteArray data = loaded.pac.imageData(loaded.file, i, &error);
        if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
        reporter.progress(int(100.0 * written / qMax(1, loaded.table.partitions.size())),
                          QStringLiteral("刷写 %1（%2）")
                              .arg(target.name, util::formatBytes(data.size())));
        io.writePartition(target.name, data, reporter);
        ++written;
    }
    if (written == 0) {
        throw std::runtime_error(
            QStringLiteral("PAC 中没有可写入的分区（%1 个镜像缺少地址，请提供分区地址表）")
                .arg(skipped)
                .toStdString());
    }
    reporter.progress(100);
    return written;
}

} // namespace gf::flows
