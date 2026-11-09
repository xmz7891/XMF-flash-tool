#pragma once

// Qualcomm Sahara protocol (the first stage of every EDL session).
//
// Frame layout: every packet starts with an 8-byte little-endian header
//     u32 command; u32 length;            // length = TOTAL packet size incl. header
// followed by `length - 8` payload bytes.
//
// Handshake
//   target -> host : HELLO (0x01), length 0x30 (48) = header + 10 u32:
//                    version, min_version, max_cmd_length, mode, reserved[6]
//   host -> target : HELLO_RESPONSE (0x02), same 48-byte layout.
//
// Image transfer (host streams the programmer file, e.g. devprg ELF)
//   target -> host : READ_DATA (0x03) / READ_DATA_64 (0x12).
//                    IMPORTANT: for these two packets the header `length` is the
//                    number of image bytes the target wants next, not the packet
//                    size (the packet has no payload).
//   host -> target : that many raw image bytes
//   host -> target : END_IMAGE_TRANSFER (0x04) / _64 (0x13), payload:
//                    u32 image_id; u32 status;   (total packet size 16)
//   target -> host : DONE (0x05) / DONE_64 (0x14), same 16-byte payload layout.
//   host -> target : DONE_RESPONSE (0x06) / _64 (0x15), same payload.
//
// Optional jump
//   host -> target : EXECUTE (0x0D): u32 image_id; u32 address;       (size 16)
//                    EXECUTE_64 (0x19): u32 image_id; u64 address;    (size 20)
//   target -> host : EXECUTE_RESPONSE (0x0E)/(0x1A): image_id, status.
//
// The exact payload fields of END_IMAGE_TRANSFER/DONE/EXECUTE follow the widely
// used open-source EDL clients; they still need validation against a real 9008
// device (see docs/PROTOCOLS.md TODO). Everything is mock-tested.

#include <QByteArray>
#include <QString>

#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::qcom::sahara {

// Command numbers (Sahara protocol, host<->target).
inline constexpr quint32 kCmdHello = 0x01;
inline constexpr quint32 kCmdHelloResponse = 0x02;
inline constexpr quint32 kCmdReadData = 0x03;
inline constexpr quint32 kCmdEndImageTransfer = 0x04;
inline constexpr quint32 kCmdDone = 0x05;
inline constexpr quint32 kCmdDoneResponse = 0x06;
inline constexpr quint32 kCmdReset = 0x07;
inline constexpr quint32 kCmdResetResponse = 0x08;
inline constexpr quint32 kCmdMemoryDebug = 0x09;
inline constexpr quint32 kCmdMemoryRead = 0x0A;
inline constexpr quint32 kCmdReadyForImage = 0x0B;
inline constexpr quint32 kCmdSwitchToDdi = 0x0C;
inline constexpr quint32 kCmdExecute = 0x0D;
inline constexpr quint32 kCmdExecuteResponse = 0x0E;
// 64-bit variants used when the target advertises Sahara v3.
inline constexpr quint32 kCmdReadData64 = 0x12;
inline constexpr quint32 kCmdEndImageTransfer64 = 0x13;
inline constexpr quint32 kCmdDone64 = 0x14;
inline constexpr quint32 kCmdDoneResponse64 = 0x15;
inline constexpr quint32 kCmdExecute64 = 0x19;
inline constexpr quint32 kCmdExecuteResponse64 = 0x1A;

// Sahara modes reported in HELLO.
inline constexpr quint32 kModeImageTxPending = 0;
inline constexpr quint32 kModeImageTxComplete = 1;
inline constexpr quint32 kModeMemoryDebug = 2;
inline constexpr quint32 kModeCommand = 3;

inline constexpr quint32 kHelloPacketSize = 48;   // header + 10 u32
inline constexpr quint32 kDefaultMaxCommandLength = 0x1000;
inline constexpr quint32 kHostMaxCommandLength = 0x10000;

struct PacketHeader {
    quint32 command = 0;
    quint32 length = 0; // total packet size (or requested byte count for READ_DATA)
};

struct DeviceHello {
    quint32 version = 0;
    quint32 minVersion = 0;
    quint32 maxCommandLength = 0;
    quint32 mode = 0;
    bool valid = false;
};

// Pure byte builders, also used by the unit tests to script the target side.
QByteArray buildPacket(quint32 command, const QByteArray& payload = {});
QByteArray buildHelloResponse(const DeviceHello& hello);
QByteArray buildHelloPacket(quint32 version = 2, quint32 minVersion = 1,
                            quint32 maxCommandLength = kDefaultMaxCommandLength,
                            quint32 mode = kModeImageTxPending);
QByteArray buildReadDataRequest(quint32 byteCount, bool is64 = false);
QByteArray buildEndImageTransfer(quint32 imageId = 0, bool is64 = false);
QByteArray buildDonePacket(quint32 imageId = 0, quint32 status = 0, bool is64 = false);
QByteArray buildDoneResponse(quint32 imageId = 0, quint32 status = 0, bool is64 = false);
QByteArray buildExecutePacket(quint64 address, quint32 imageId = 0, bool is64 = false);
QByteArray buildExecuteResponsePacket(quint32 imageId = 0, quint32 status = 0, bool is64 = false);
PacketHeader parseHeader(const QByteArray& bytes);

class Client {
public:
    struct UploadResult {
        bool success = false;
        qint64 bytesTransferred = 0;
        quint32 imageId = 0;
        quint32 status = 0;
        QString error;
    };

    explicit Client(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    /// Reads the target HELLO, validates it and sends HELLO_RESPONSE.
    DeviceHello handshake(int timeoutMs = 5000);

    /// Streams `image` (an ELF/MBN programmer) answering the target's READ_DATA
    /// requests until DONE. Does not require a prior handshake() call.
    UploadResult upload(const QByteArray& image, int timeoutMs = 5000, int progressBase = 10,
                        int progressSpan = 80);

    /// Sends EXECUTE and waits for EXECUTE_RESPONSE.
    void execute(quint64 address, quint32 imageId = 0, int timeoutMs = 5000);

    /// Sends RESET (used after a successful Firehose session). Devices usually
    /// disconnect instead of replying, so waiting is optional.
    void reset(bool waitForResponse = false, int timeoutMs = 1000);

    const DeviceHello& device() const { return m_device; }
    bool handshakeDone() const { return m_handshakeDone; }
    const QByteArray& lastImageData() const { return m_image; }

private:
    void sendPacket(quint32 command, const QByteArray& payload, int timeoutMs);
    PacketHeader readHeader(int timeoutMs);
    QByteArray readBody(quint32 totalLength, int timeoutMs);
    void sendEndImageTransfer(quint32 imageId, bool is64);
    void sendDoneResponse(quint32 imageId, quint32 status, bool is64);

    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    DeviceHello m_device;
    bool m_handshakeDone = false;
    QByteArray m_image;
};

} // namespace gf::qcom::sahara
