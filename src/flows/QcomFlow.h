#pragma once

// Qualcomm EDL flashing pipeline: 9008 port -> Sahara handshake -> programmer
// (devprg from a GeekFlashPacket resource pack or an explicit file) -> Firehose
// configure -> partition table (GPT read from the device) -> backup/restore/
// read/write/erase/checksum/A-B slot operations.
//
// Everything is driven through gf::usb::IUsbTransport and can therefore be
// replayed with MockTransport in the unit tests.

#include <QByteArray>
#include <QString>

#include "FlowOps.h"
#include "qcom/Firehose.h"
#include "qcom/Sahara.h"
#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::flows {

struct QcomOptions {
    QString memoryName = QStringLiteral("ufs"); // ufs / emmc / spinor / nvme / sdcard
    int maxPayloadBytes = 1024 * 1024;
    quint32 sectorSize = 4096; // initial guess; GPT detection corrects it
    bool verbose = false;
};

class QcomSession {
public:
    QcomSession(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    /// Sahaha handshake + programmer upload + Firehose configure.
    void open(const QByteArray& programmer, const QcomOptions& options = {});

    qcom::sahara::Client& sahara() { return m_sahara; }
    qcom::firehose::Client& firehose() { return m_firehose; }
    const QString& memoryName() const { return m_options.memoryName; }

    /// Reads the primary GPT from the device (cached). Throws a readable error
    /// when no partition table can be found.
    const PartitionTable& partitionTable();
    void refreshPartitionTable();

    quint32 sectorSize() const { return m_sectorSize; }

    QByteArray readRaw(quint64 offset, quint64 size);
    void writeRaw(quint64 offset, const QByteArray& data);
    void eraseRaw(quint64 offset, quint64 size, const QString& label = {});

    QByteArray readPartition(const QString& name);
    void writePartition(const QString& name, const QByteArray& data);
    void erasePartition(const QString& name);

    /// Patches the bootloader control block's slot suffix in the misc
    /// partition (best effort, documented in FLASH_PIPELINES.md).
    void setActiveSlot(const QString& slot);

    /// Programmer resolution from the resource pack (empty when unavailable).
    static QString programmerFromPacket(const QString& packetId, const QString& modelName);
    static QString memoryTypeFromPacket(const QString& packetId, const QString& modelName);

private:
    QByteArray readWithSectorSize(quint64 offset, quint64 size, quint32 sectorSize);
    const PartitionInfo& requirePartition(const QString& name) const;

    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    qcom::sahara::Client m_sahara;
    qcom::firehose::Client m_firehose;
    QcomOptions m_options;
    quint32 m_sectorSize = 4096;
    PartitionTable m_table;
    bool m_tableLoaded = false;
    bool m_opened = false;
};

class QcomFirehoseIo final : public PartitionIo {
public:
    explicit QcomFirehoseIo(QcomSession& session) : m_session(session) {}

    QString platform() const override { return QStringLiteral("qcom"); }
    PartitionTable table() override { return m_session.partitionTable(); }
    QByteArray readPartition(const QString& name, const proto::Reporter& reporter) override;
    void writePartition(const QString& name, const QByteArray& data,
                        const proto::Reporter& reporter) override;
    void erasePartition(const QString& name, const proto::Reporter& reporter) override;
    QByteArray readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) override;
    void writeRaw(quint64 offset, const QByteArray& data,
                  const proto::Reporter& reporter) override;
    void eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) override;
    quint32 sectorSize() const override { return m_session.sectorSize(); }

private:
    QcomSession& m_session;
};

} // namespace gf::flows
