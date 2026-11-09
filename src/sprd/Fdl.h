#pragma once

// Spreadtrum FDL command layer on top of the U2S frame transport.
//
// Command codes (FDL1/FDL2 share the numbering; some FDL2 revisions add
// commands not listed here). The payload is big-endian.
//
//   0x00 CONNECT        []                                   -> status
//   0x01 START_DATA     [addr u32][size u32][data_type u8]    -> status
//   0x02 MIDST_DATA     [chunk bytes]                         -> no reply
//   0x03 END_DATA       [checksum? u32]                       -> status
//   0x04 EXEC_DATA      [addr u32]                            -> status
//   0x05 NORMAL_RESET   []                                    -> (device resets)
//   0x06 READ_FLASH     [addr u32][size u32]                  -> raw data (some FDLs)
//   0x07 READ_CHIP_TYPE []                                    -> chip type u32
//   0x0A ERASE_FLASH    [addr u32][size u32]                  -> status
//   0x10 READ_VERSION   []                                    -> ASCII version
//
// Status payload: first byte 0x00 = OK, anything else = FDL error code.
//
// TODO(protocols): checksum/ACK details and READ_FLASH framing differ between
// FDL1 and FDL2 revisions; verify against a real U2S/C1 device and update
// docs/PROTOCOLS.md.

#include <QByteArray>
#include <QString>

#include "U2s.h"
#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::sprd::fdl {

inline constexpr quint8 kConnect = 0x00;
inline constexpr quint8 kStartData = 0x01;
inline constexpr quint8 kMidstData = 0x02;
inline constexpr quint8 kEndData = 0x03;
inline constexpr quint8 kExecData = 0x04;
inline constexpr quint8 kNormalReset = 0x05;
inline constexpr quint8 kReadFlash = 0x06;
inline constexpr quint8 kReadChipType = 0x07;
inline constexpr quint8 kReadNvitem = 0x08;
inline constexpr quint8 kChangeBaud = 0x09;
inline constexpr quint8 kEraseFlash = 0x0A;
inline constexpr quint8 kReadFlashSil = 0x0B;
inline constexpr quint8 kReadStatus = 0x0C;
inline constexpr quint8 kReadRawData = 0x0D;
inline constexpr quint8 kWriteRawData = 0x0E;
inline constexpr quint8 kSetBaud = 0x0F;
inline constexpr quint8 kReadVersion = 0x10;
inline constexpr quint8 kCheckBaud = 0x11;
inline constexpr quint8 kPowerOff = 0x12;

inline constexpr int kDefaultTimeoutMs = 5000;
inline constexpr qsizetype kDataChunk = 16 * 1024;

QByteArray encodeAddressSize(quint32 address, quint32 size);

class Client {
public:
    explicit Client(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    void connect(int timeoutMs = kDefaultTimeoutMs);
    QString readVersion(int timeoutMs = kDefaultTimeoutMs);
    quint32 readChipType(int timeoutMs = kDefaultTimeoutMs);

    void startData(quint32 address, quint32 size, quint8 dataType = 0,
                   int timeoutMs = kDefaultTimeoutMs);
    void midstData(const QByteArray& chunk, int timeoutMs = kDefaultTimeoutMs);
    void endData(quint32 checksum = 0, int timeoutMs = kDefaultTimeoutMs);
    void execData(quint32 address, int timeoutMs = kDefaultTimeoutMs);
    void eraseFlash(quint32 address, quint32 size, int timeoutMs = kDefaultTimeoutMs);
    QByteArray readFlash(quint32 address, quint32 size, int timeoutMs = 10000);
    void normalReset(int timeoutMs = kDefaultTimeoutMs);

    /// START_DATA -> MIDST_DATA* -> END_DATA -> EXEC_DATA for one FDL image.
    void downloadFdl(quint32 address, const QByteArray& image, quint8 dataType = 0,
                     int progressBase = 0, int progressSpan = 0);

    /// Escape hatch for vendor/FDL2-specific commands (e.g. partition-table
    /// queries whose opcode differs between FDL revisions). Sends one framed
    /// command and returns the raw response frame.
    u2s::Frame rawCommand(quint8 type, const QByteArray& payload = {}, int timeoutMs = 5000);

    u2s::Link& link() { return m_link; }

private:
    void expectOk(quint8 command, const u2s::Frame& frame, const QString& action);
    u2s::Frame command(quint8 type, const QByteArray& payload, int timeoutMs);
    usb::IUsbTransport& m_transport;
    u2s::Link m_link;
    proto::Reporter m_reporter;
};

/// Polls for a Spreadtrum port after FDL1 starts (it re-enumerates).
usb::PortInfo waitForPort(int timeoutMs, const proto::Reporter& reporter);

} // namespace gf::sprd::fdl
