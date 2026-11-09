#pragma once

// MediaTek BROM protocol.
//
// Transport: plain bulk/reference USB, no framing.
//   * handshake: host writes the single byte 0xA0, the boot ROM answers 0x5A.
//   * every command is one opcode byte followed by a big-endian payload.
//   * read commands answer with the requested bytes (big-endian).
//   * write commands echo the written value; the client validates the echo.
//   * addresses are always big-endian u32.
//
// Commands used here (MediaTek boot ROM):
//   0xD0 READ16   [addr u32 BE]                       -> 2 bytes
//   0xD1 READ32   [addr u32 BE]                       -> 4 bytes
//   0xD2 WRITE16  [addr u32 BE][value u16 BE]         -> 2 bytes echo
//   0xD4 WRITE32  [addr u32 BE][value u32 BE]         -> 4 bytes echo
//   0xD5 JUMP_DA  [addr u32 BE]                       -> no reply
//   0xD6 JUMP_BL  [addr u32 BE]                       -> no reply
//   0xD7 SEND_DA  [addr u32 BE][len u32 BE][siglen u32 BE][data...] -> 2 bytes status
//   0xD8 GET_TARGET_CONFIG                            -> 8 bytes
//   0xDA ECHO     [value u32 BE]                      -> 4 bytes echo
//   0xDB JUMP     [addr u32 BE]                       -> no reply
//
// TODO(protocols): DA1/DA2 are downloaded in chunks; some boot ROM versions
// report a checksum instead of the 2-byte 0x0000 status. Validate on hardware
// and see docs/PROTOCOLS.md.

#include <QByteArray>
#include <QString>

#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::mtk::brom {

inline constexpr quint8 kHandshakeCommand = 0xA0;
inline constexpr quint8 kHandshakeReply = 0x5A;

inline constexpr quint8 kCmdRead16 = 0xD0;
inline constexpr quint8 kCmdRead32 = 0xD1;
inline constexpr quint8 kCmdWrite16 = 0xD2;
inline constexpr quint8 kCmdWrite32 = 0xD4;
inline constexpr quint8 kCmdJumpDa = 0xD5;
inline constexpr quint8 kCmdJumpBl = 0xD6;
inline constexpr quint8 kCmdSendDa = 0xD7;
inline constexpr quint8 kCmdGetTargetConfig = 0xD8;
inline constexpr quint8 kCmdEcho = 0xDA;
inline constexpr quint8 kCmdJump = 0xDB;

inline constexpr int kDefaultTimeoutMs = 5000;

/// Builds one command frame (opcode + payload). Exposed for the unit tests.
QByteArray frame(quint8 command, const QByteArray& payload = {});

class Client {
public:
    explicit Client(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    /// 0xA0/0x5A handshake. Throws when the device is not a MediaTek BROM.
    void handshake(int timeoutMs = kDefaultTimeoutMs);

    quint16 read16(quint32 address, int timeoutMs = kDefaultTimeoutMs);
    quint32 read32(quint32 address, int timeoutMs = kDefaultTimeoutMs);
    void write16(quint32 address, quint16 value, int timeoutMs = kDefaultTimeoutMs);
    void write32(quint32 address, quint32 value, int timeoutMs = kDefaultTimeoutMs);
    quint32 echo(quint32 value, int timeoutMs = kDefaultTimeoutMs);

    /// 8-byte target configuration blob (chip id / version / etc.).
    QByteArray getTargetConfig(int timeoutMs = kDefaultTimeoutMs);

    /// Downloads `data` to `address` with SEND_DA. `signatureLength` is 0 for
    /// unsigned (engineering) DAs.
    void sendDa(quint32 address, const QByteArray& data, quint32 signatureLength = 0,
                int timeoutMs = 15000, int progressBase = 0, int progressSpan = 0);

    void jumpDa(quint32 address, int timeoutMs = kDefaultTimeoutMs);
    void jumpBl(quint32 address, int timeoutMs = kDefaultTimeoutMs);
    void jump(quint32 address, int timeoutMs = kDefaultTimeoutMs);

private:
    void sendCommand(quint8 command, const QByteArray& payload, int timeoutMs);
    QByteArray readBytes(qint64 count, int timeoutMs);
    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
};

} // namespace gf::mtk::brom
