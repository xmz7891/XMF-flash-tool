#pragma once

// Spreadtrum / Unisoc pipeline: FDL1 -> FDL2 -> PAC image table operations.
//
// PAC files carry image payloads but no storage addresses. A partition map
// (name,offset,size) is therefore required for write/read/erase; the map is
// loaded from a text file or supplied by the caller. The FDL storage command
// layer is an experimental adapter (same situation as MTK DA, see
// docs/FLASH_PIPELINES.md TODO); all planning/table logic is real.

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "FlowOps.h"
#include "sprd/Fdl.h"
#include "sprd/Pac.h"
#include "sprd/U2s.h"
#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

#include <memory>

namespace gf::flows {

struct SprdOptions {
    QString devicePath;
    QString fdl1Path;
    QString fdl2Path;
    quint32 fdl1Address = 0x65000800;
    quint32 fdl2Address = 0x9EFFFE00;
    int waitMs = 15000;

    static SprdOptions fromPacket(const QString& packetId, const QString& modelName);
};

class SprdSession {
public:
    explicit SprdSession(proto::Reporter reporter = {});
    ~SprdSession();
    SprdSession(const SprdSession&) = delete;
    SprdSession& operator=(const SprdSession&) = delete;

    void open(const SprdOptions& options);

    usb::IUsbTransport& transport();
    bool isOpen() const { return m_transport != nullptr; }

private:
    proto::Reporter m_reporter;
    std::unique_ptr<usb::IUsbTransport> m_transport;
};

class SprdFdlStorageIo final : public PartitionIo {
public:
    SprdFdlStorageIo(usb::IUsbTransport& transport, PartitionTable table,
                     proto::Reporter reporter = {});

    QString platform() const override { return QStringLiteral("sprd"); }
    PartitionTable table() override { return m_table; }
    QByteArray readPartition(const QString& name, const proto::Reporter& reporter) override;
    void writePartition(const QString& name, const QByteArray& data,
                        const proto::Reporter& reporter) override;
    void erasePartition(const QString& name, const proto::Reporter& reporter) override;
    QByteArray readRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) override;
    void writeRaw(quint64 offset, const QByteArray& data,
                  const proto::Reporter& reporter) override;
    void eraseRaw(quint64 offset, quint64 size, const proto::Reporter& reporter) override;
    quint32 sectorSize() const override { return m_table.sectorSize ? m_table.sectorSize : 512; }

    /// Exposed for tests.
    sprd::u2s::Frame command(quint8 type, const QByteArray& payload,
                             const QByteArray& writeData = {});

private:
    const PartitionInfo& requirePartition(const QString& name) const;

    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    PartitionTable m_table;
    sprd::fdl::Client m_fdl;
};

struct PacLoadResult {
    bool valid = false;
    QString error;
    QByteArray file;
    sprd::PacInfo pac;
    PartitionTable table;
};

PacLoadResult loadPacFile(const QString& path);

/// Applies a partition address map text (name,offset,size) to a PAC-based table.
void applyAddressMap(PartitionTable& table, const QString& mapText);
void applyAddressMapFile(PartitionTable& table, const QString& mapPath);

/// Writes the PAC images that have addresses; `only` empty = all images.
int flashPac(PartitionIo& io, const PacLoadResult& loaded, const QStringList& only,
             const proto::Reporter& reporter);

} // namespace gf::flows
