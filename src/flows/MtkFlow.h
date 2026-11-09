#pragma once

// MediaTek pipeline: BROM -> DA1 -> DA2 -> partition operations driven by the
// scatter file that ships with the firmware (the scatter is the authoritative
// partition table on MTK).
//
// The storage command layer of DA2 is not exposed by src/mtk yet. To keep this
// module self contained (as agreed for task-5) it uses an adapter with a small
// experimental DA-storage frame format - see docs/FLASH_PIPELINES.md TODO. All
// flow logic (tables, file naming, plans, progress) is real and mock-tested.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "FlowOps.h"
#include "mtk/Brom.h"
#include "mtk/Da.h"
#include "mtk/Preloader.h"
#include "mtk/Scatter.h"
#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

#include <memory>

namespace gf::flows {

struct MtkOptions {
    QString devicePath;
    QString da1Path;
    QString da2Path;
    quint32 da1Address = 0x00200000;
    quint32 da2Address = 0x40000000;
    quint32 signatureLength = 0;
    int daWaitMs = 15000;

    /// Fills DA paths/addresses from a GeekFlashPacket model when available.
    static MtkOptions fromPacket(const QString& packetId, const QString& modelName);
    static QString preloaderFromPacket(const QString& packetId, const QString& modelName);
};

/// Opens BROM, loads DA1, waits for the DA port and downloads DA2.
class MtkSession {
public:
    explicit MtkSession(proto::Reporter reporter = {});
    ~MtkSession();
    MtkSession(const MtkSession&) = delete;
    MtkSession& operator=(const MtkSession&) = delete;

    void open(const MtkOptions& options);

    usb::IUsbTransport& transport();
    bool isOpen() const { return m_transport != nullptr; }

private:
    proto::Reporter m_reporter;
    std::unique_ptr<usb::IUsbTransport> m_transport;
};

/// Experimental DA2 storage adapter (read/write/erase by byte offset).
class MtkDaStorageIo final : public PartitionIo {
public:
    MtkDaStorageIo(usb::IUsbTransport& transport, PartitionTable table,
                   proto::Reporter reporter = {});

    QString platform() const override { return QStringLiteral("mtk"); }
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

    // DA-storage command codes (experimental, documented in FLASH_PIPELINES.md).
    static constexpr quint8 kCmdRead = 0x01;
    static constexpr quint8 kCmdWrite = 0x02;
    static constexpr quint8 kCmdErase = 0x03;

    /// Exposed for tests: sends one command frame and returns the response data.
    QByteArray command(quint8 command, const QByteArray& payload, const QByteArray& writeData = {});

private:
    const PartitionInfo& requirePartition(const QString& name) const;

    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    PartitionTable m_table;
};

/// Loads a MTK scatter file and builds the flash plan for its download entries.
struct ScatterFlashItem {
    QString partition;
    QString file;
    quint64 offset = 0;
    quint64 size = 0;
    bool exists = false;
};

struct ScatterFlashPlan {
    bool valid = false;
    QString error;
    PartitionTable table;
    QString firmwareDir;
    QList<ScatterFlashItem> items;
    QStringList missingFiles;
};

mtk::ScatterInfo loadScatterFile(const QString& path);
ScatterFlashPlan buildScatterFlashPlan(const QString& scatterPath, const QString& firmwareDir,
                                       const QStringList& onlyPartitions = {});

/// Writes every existing item of the plan; throws when a file is missing.
int flashScatterPlan(PartitionIo& io, const ScatterFlashPlan& plan, const proto::Reporter& reporter);

} // namespace gf::flows
