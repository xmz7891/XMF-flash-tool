#pragma once

// Spreadtrum / Unisoc U2S (USB to serial) framing used by FDL1/FDL2.
//
// Every message is HDLC framed:
//
//   0x7E | type(1) | length(2, big endian) | payload(length) | crc16(2, BE) | 0x7E
//
// The CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) computed over
// type + length + payload. Byte stuffing escapes the frame delimiter:
//   0x7E -> 0x7D 0x5E
//   0x7D -> 0x7D 0x5D
// (i.e. XOR the escaped byte with 0x20).
//
// `type` is the FDL command code (see Fdl.h). The device answers with a frame
// carrying the same type and a status payload; a first payload byte != 0 means
// failure. Some commands (READ_FLASH, READ_CHIP_TYPE) return their data as the
// payload instead.
//
// TODO(protocols): FDL2 uses the same framing but a richer command set; the
// exact status encoding differs between BROM/FDL1/FDL2 revisions. Validate on
// hardware, see docs/PROTOCOLS.md.

#include <QByteArray>
#include <QString>

#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::sprd::u2s {

inline constexpr quint8 kFlag = 0x7E;
inline constexpr quint8 kEscape = 0x7D;
inline constexpr quint8 kEscapeMask = 0x20;
inline constexpr int kMaxPayload = 0xFFFF;
inline constexpr qsizetype kMaxFrame = 0x10000 + 16;

/// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout).
quint16 crc16(const QByteArray& data);

QByteArray encodeFrame(quint8 type, const QByteArray& payload = {});
QByteArray escape(const QByteArray& data);
QByteArray unescape(const QByteArray& data);

struct Frame {
    bool valid = false;
    quint8 type = 0;
    QByteArray payload;
    QString error;
};

/// Decodes one frame (including both 0x7E delimiters). The parser is tolerant
/// about leading/trailing noise but requires exactly one complete frame.
Frame decodeFrame(const QByteArray& raw);

/// Framed link over IUsbTransport. One Link == one USB re-enumeration.
class Link {
public:
    explicit Link(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    void send(quint8 type, const QByteArray& payload = {}, int timeoutMs = 5000);
    /// Reads one complete frame. Throws std::runtime_error on timeout/bad CRC.
    Frame receive(int timeoutMs = 5000);
    Frame transact(quint8 type, const QByteArray& payload = {}, int timeoutMs = 5000);

private:
    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    QByteArray m_buffer; // bytes that arrived after a frame and belong to the next one
};

} // namespace gf::sprd::u2s
