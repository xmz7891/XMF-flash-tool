#pragma once

// Allwinner FEL pipeline. FEL exposes raw memory read/write/execute only; there
// is no partition-table command, so partition operations use a user supplied
// map (name,offset,size). Erase is emulated by writing zeroes (best effort).

#include <QByteArray>
#include <QString>

#include "FlowOps.h"
#include "aw/Fel.h"
#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::flows {

struct AwOptions {
    QString partitionMapFile; // name,offset,size (hex supported)
};

class AwSession {
public:
    AwSession(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    /// FEL_VER handshake; logs BootROM version and SoC id.
    aw::VersionInfo open(const AwOptions& options = {});

    aw::Client& client() { return m_client; }
    quint32 sectorSize() const { return 512; }

    const PartitionTable& partitionTable();
    void refreshPartitionTable();

    QByteArray readRaw(quint64 address, quint64 size);
    void writeRaw(quint64 address, const QByteArray& data);
    void eraseRaw(quint64 address, quint64 size, const QString& label = {});
    void execRaw(quint64 address);

private:
    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    aw::Client m_client;
    AwOptions m_options;
    PartitionTable m_table;
    bool m_opened = false;
};

class AwMemoryIo final : public PartitionIo {
public:
    explicit AwMemoryIo(AwSession& session) : m_session(session) {}

    QString platform() const override { return QStringLiteral("aw"); }
    PartitionTable table() override { return m_session.partitionTable(); }
    QByteArray readPartition(const QString& name, const proto::Reporter& reporter) override;
    void writePartition(const QString& name, const QByteArray& data,
                        const proto::Reporter& reporter) override;
    void erasePartition(const QString& name, const proto::Reporter& reporter) override;
    QByteArray readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) override;
    void writeRaw(quint64 offset, const QByteArray& data,
                  const proto::Reporter& reporter) override;
    void eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) override;
    quint32 sectorSize() const override { return 512; }

private:
    AwSession& m_session;
};

} // namespace gf::flows
