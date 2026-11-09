#pragma once

// Allwinner FEL protocol (PhoenixSuit / sunxi-tools compatible).
//
// FEL runs in the BootROM of Allwinner SoCs (VID 1F3A, PID 0xEFE8). Every
// operation is:
//
//   1) host -> device: a 16-byte little-endian request
//        0x00 u32 request   (FEL_VER / FEL_READ / FEL_WRITE / FEL_EXEC / ...)
//        0x04 u32 length    (data phase length in bytes)
//        0x08 u32 address   (target address / offset)
//        0x0C u32 reserved  (0)
//   2) data phase (optional):
//        FEL_WRITE: host -> device, `length` bytes
//        FEL_READ : device -> host, `length` bytes
//   3) device -> host: an 8-byte response (status u32, tag u32); status 0 = OK.
//
// FEL_VER additionally returns a version blob in the data phase (32 bytes):
//   magic[8] = "AWUSBFEX", version u32, id u32 (SoC id), firmware u32[4].
//
// Transfers are chunked (16 KiB) because the BootROM bulk endpoint rejects
// larger requests. Addresses are physical DRAM addresses.
//
// TODO(protocols): response size (8 vs 4 bytes) differs on a few BROM builds;
// the client accepts a short response and only validates the status word.
// Verify against hardware, see docs/PROTOCOLS.md.

#include <QByteArray>
#include <QString>

#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::aw {

inline constexpr quint32 kHello = 0x001; // aka FEL_VER
inline constexpr quint32 kVer = 0x001;
inline constexpr quint32 kRead = 0x011;
inline constexpr quint32 kWrite = 0x012;
inline constexpr quint32 kExec = 0x013;
inline constexpr quint32 kRun = 0x014;
inline constexpr quint32 kNotify = 0x015;

inline constexpr quint32 kRequestSize = 16;
inline constexpr quint32 kResponseSize = 8;
inline constexpr quint32 kVersionSize = 32;
inline constexpr qsizetype kMaxChunk = 16 * 1024;

struct Request {
    quint32 request = 0;
    quint32 length = 0;
    quint32 address = 0;
    quint32 reserved = 0;

    QByteArray encode() const;
    static Request decode(const QByteArray& raw);
};

struct Response {
    bool valid = false;
    quint32 status = 0;
    quint32 tag = 0;
    QByteArray raw;

    bool ok() const { return valid && status == 0; }
    static Response decode(const QByteArray& raw);
};

QByteArray encodeRequest(quint32 request, quint32 length = 0, quint32 address = 0);

struct VersionInfo {
    bool valid = false;
    QString magic;
    quint32 version = 0;
    quint32 id = 0; // SoC id
    QByteArray firmware;
    QString error;
};

VersionInfo parseVersion(const QByteArray& payload);
QString socName(quint32 id);

class Client {
public:
    explicit Client(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    /// FEL_VER / HELLO: returns the BootROM version + SoC id.
    VersionInfo hello(int timeoutMs = 5000);

    QByteArray read(quint32 address, quint32 length, int timeoutMs = 30000);
    void write(quint32 address, const QByteArray& data, qsizetype chunkSize = kMaxChunk,
               int progressBase = 0, int progressSpan = 0, int timeoutMs = 30000);
    void exec(quint32 address, int timeoutMs = 5000);

    /// write(addr, image) followed by exec(addr).
    void run(quint32 address, const QByteArray& image, int timeoutMs = 30000);

private:
    Response transfer(const Request& request, const QByteArray& outData, QByteArray* inData,
                      int timeoutMs);
    Response readResponse(int timeoutMs);
    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
};

} // namespace gf::aw
