#pragma once

// Rockchip RockUSB pipeline: device -> optional parameter.txt partition table
// (or GPT read from the device) -> backup/restore/read/write/erase -> loader
// switch. RockUSB sector commands are real, so everything is replayable with
// MockTransport.

#include <QByteArray>
#include <QString>

#include "FlowOps.h"
#include "rk/RockUsb.h"
#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::flows {

struct RkOptions {
    quint32 sectorSize = 512;
    QString parameterFile; // firmware parameter.txt (optional; otherwise GPT)
};

class RkSession {
public:
    RkSession(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    /// Reads the device info and (when a parameter file was supplied) the
    /// firmware partition table; otherwise the table is read lazily from GPT.
    void open(const RkOptions& options = {});

    rk::Client& client() { return m_client; }
    quint32 sectorSize() const { return m_sectorSize; }

    const PartitionTable& partitionTable();
    void refreshPartitionTable();

    QByteArray readRaw(quint64 offset, quint64 size);
    void writeRaw(quint64 offset, const QByteArray& data);
    void eraseRaw(quint64 offset, quint64 size, const QString& label = {});

    QByteArray readPartition(const QString& name);
    void writePartition(const QString& name, const QByteArray& data);
    void erasePartition(const QString& name);

    void rebootToLoader();
    void reset();

private:
    const PartitionInfo& requirePartition(const QString& name) const;

    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    rk::Client m_client;
    RkOptions m_options;
    quint32 m_sectorSize = 512;
    PartitionTable m_table;
    bool m_tableLoaded = false;
    bool m_opened = false;
};

class RkStorageIo final : public PartitionIo {
public:
    explicit RkStorageIo(RkSession& session) : m_session(session) {}

    QString platform() const override { return QStringLiteral("rk"); }
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
    RkSession& m_session;
};

} // namespace gf::flows
