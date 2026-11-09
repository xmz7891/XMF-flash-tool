#pragma once

// Qualcomm Firehose protocol: XML commands exchanged with the programmer that
// Sahara just loaded. All commands are UTF-8 XML written to the bulk OUT pipe;
// the target answers with `<response value="ACK|NAK" .../>` after zero or more
// `<log value="..."/>` lines. `<program>`/`<read>` additionally move raw sector
// data in the corresponding direction.
//
// Example session:
//   configure(MemoryName="ufs", MaxPayloadSizeToTargetInBytes="1048576")
//   getstorageinfo
//   program/read/erase  <partition>  (SECTOR_SIZE_IN_BYTES = 4096)
//   power value="reset"
//
// The XML attribute names follow the public Firehose command set as implemented
// by qdl/edl. Response framing differs slightly between programmer versions
// (one vs. two ACKs around bulk data), so the streaming helpers expose an
// explicit ack flag; see docs/PROTOCOLS.md.

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::qcom::firehose {

struct StorageConfig {
    QString memoryName = QStringLiteral("ufs"); // ufs / emmc / spinor / nvme / sdcard
    int maxPayloadBytes = 1024 * 1024;
    bool verbose = false;
    bool alwaysValidate = false;
    bool skipStorageInit = false;
    bool zlpAwareHost = true;
};

/// Inline byte patch for the Firehose `<patch>` command.
struct Patch {
    int sectorSize = 4096;
    int physicalPartition = 0;
    quint64 startSector = 0;
    quint64 byteOffset = 0;
    QByteArray value; // raw bytes, written as a little-endian hex string
};

struct Response {
    bool received = false;   // a complete <response ...> was seen
    bool acknowledged = false; // value="ACK"
    bool nak = false;        // value="NAK"
    QString value;           // raw value attribute
    QString raw;             // everything read from the device
    QStringList logs;        // <log value="..."/> contents
    QString error;

    bool ok() const { return received && acknowledged && !nak; }
};

QString wrapCommand(const QString& command);
QString buildConfigureXml(const StorageConfig& config);
QString buildReadXml(const QString& storageType, int physicalPartition, quint64 startSector,
                     quint64 numSectors, int sectorSize);
QString buildProgramXml(const QString& storageType, int physicalPartition, quint64 startSector,
                        quint64 numSectors, int sectorSize, const QString& filename);
QString buildEraseXml(const QString& storageType, int physicalPartition, quint64 startSector,
                      quint64 numSectors, int sectorSize);
QString buildPowerXml(const QString& mode);
/// Firehose `<patch>` command: patch `value` bytes at `byteOffset` inside the
/// sector area starting at `startSector` (used e.g. to fix GPT entries).
QString buildPatchXml(const Patch& patch);
Response parseResponseXml(const QString& raw);

class Client {
public:
    explicit Client(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    /// Sends one already-formed XML command and reads exactly one response.
    Response sendCommand(const QString& xmlCommand, int timeoutMs = 10000);

    Response configure(const StorageConfig& config, int timeoutMs = 10000);
    Response getStorageInfo(int timeoutMs = 10000);
    Response nop(int timeoutMs = 5000);

    /// Reads `numSectors * sectorSize` bytes. Returns the ACK response and fills
    /// `out` with the raw data. `trailingAck` reads an extra response after the
    /// payload for programmer versions that send one.
    Response readSectors(const QString& storageType, int physicalPartition, quint64 startSector,
                         quint64 numSectors, int sectorSize, QByteArray* out,
                         int timeoutMs = 120000, bool trailingAck = false);

    /// Streams `data` in sector aligned chunks. `ackAfterData` controls whether
    /// a second ACK is expected after each raw data block.
    Response programSectors(const QString& storageType, int physicalPartition,
                            quint64 startSector, const QByteArray& data, int sectorSize = 4096,
                            const QString& filename = QStringLiteral("program.bin"),
                            int chunkBytes = 1024 * 1024, bool ackAfterData = true,
                            int timeoutMs = 120000);

    Response eraseSectors(const QString& storageType, int physicalPartition, quint64 startSector,
                          quint64 numSectors, int sectorSize = 4096, int timeoutMs = 60000);

    /// Patches raw bytes inside a sector (no data phase; value goes into XML).
    Response patch(const Patch& patch, int timeoutMs = 10000);

    Response power(const QString& mode = QStringLiteral("reset"), int timeoutMs = 5000);
    Response reset(int timeoutMs = 5000) { return power(QStringLiteral("reset"), timeoutMs); }

    const QString& lastRawResponse() const { return m_lastRaw; }

private:
    QString readResponseXml(int timeoutMs);
    void writeData(const QByteArray& data, int timeoutMs);

    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    QString m_lastRaw;
};

/// True when `raw` contains a complete `<response ...>...</response>` / `<response .../>`.
bool responseComplete(const QByteArray& raw);

} // namespace gf::qcom::firehose
