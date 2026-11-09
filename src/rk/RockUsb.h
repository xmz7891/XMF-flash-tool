#pragma once

// Rockchip RockUSB protocol (rkdeveloptool compatible).
//
// RockUSB is a USB-Mass-Storage-like command protocol: every command is a
// 31-byte CBW (Command Block Wrapper), optionally followed by OUT data, and is
// acknowledged by a 13-byte CSW (Command Status Wrapper).
//
// CBW (little endian):
//   0x00 u32 signature      = 0x43425355 ("USBC")
//   0x04 u32 tag            (echoed by the CSW)
//   0x08 u32 dataLength     (bytes transferred in the data phase)
//   0x0C u8  flags          (0x80 = IN, 0x00 = OUT)
//   0x0D u8  lun            (0)
//   0x0E u8  cbLength       (length of the command block)
//   0x0F u8[16] command block (opcode + parameters)
//
// CSW (little endian):
//   0x00 u32 signature      = 0x53425355 ("USBS")
//   0x04 u32 tag
//   0x08 u32 residue
//   0x0C u8  status         (0 = passed, 1 = failed, 2 = phase error)
//
// Sector command block layout used by rkdeveloptool:
//   cb[0] = opcode, cb[1] = 0, cb[2..5] = start sector (u32 LE),
//   cb[6..7] = sector count (u16 LE), cb[8] = 0.
//
// Vendor opcodes (rkdeveloptool RKComm.h):
//   0x01 READ_FLASH_ID, 0x04 READ_SECTOR, 0x05 WRITE_SECTOR,
//   0x06 ERASE_SECTOR, 0x07 READ_SDRAM, 0x08 WRITE_SDRAM, 0x09 EXEC_SDRAM,
//   0x0A DEVICE_INFO, 0x0B READ_CAPABILITY, 0x0C READ_FLASH_INFO,
//   0x14 READ_LBA, 0x15 WRITE_LBA, 0x16 ERASE_LBA,
//   0x1A READ_CHIP_INFO, 0x1B SET_RESET_FLAG.
//
// TODO(protocols): DEVICE_INFO/RESET payload details vary by loader revision;
// validated only against scripts so far (see docs/PROTOCOLS.md).

#include <QByteArray>
#include <QString>

#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::rk {

inline constexpr quint32 kCbwSignature = 0x43425355; // little endian "USBC"
inline constexpr quint32 kCswSignature = 0x53425355; // little endian "USBS"
inline constexpr int kCbwSize = 31;
inline constexpr int kCswSize = 13;
inline constexpr quint8 kDirectionOut = 0x00;
inline constexpr quint8 kDirectionIn = 0x80;
inline constexpr quint32 kSectorSize = 512;

inline constexpr quint8 kCmdReadFlashId = 0x01;
inline constexpr quint8 kCmdTestBadBlock = 0x02;
inline constexpr quint8 kCmdReadSector = 0x04;
inline constexpr quint8 kCmdWriteSector = 0x05;
inline constexpr quint8 kCmdEraseSector = 0x06;
inline constexpr quint8 kCmdReadSdram = 0x07;
inline constexpr quint8 kCmdWriteSdram = 0x08;
inline constexpr quint8 kCmdExecSdram = 0x09;
inline constexpr quint8 kCmdDeviceInfo = 0x0A;
inline constexpr quint8 kCmdReadCapability = 0x0B;
inline constexpr quint8 kCmdReadFlashInfo = 0x0C;
inline constexpr quint8 kCmdReadLba = 0x14;
inline constexpr quint8 kCmdWriteLba = 0x15;
inline constexpr quint8 kCmdEraseLba = 0x16;
inline constexpr quint8 kCmdReadChipInfo = 0x1A;
inline constexpr quint8 kCmdSetResetFlag = 0x1B;

struct Cbw {
    quint32 signature = kCbwSignature;
    quint32 tag = 0;
    quint32 transferLength = 0;
    quint8 flags = kDirectionOut;
    quint8 lun = 0;
    quint8 cbLength = 0;
    QByteArray cb; // exactly 16 bytes once encoded

    QByteArray encode() const;
    static Cbw decode(const QByteArray& raw);
};

struct Csw {
    bool valid = false;
    quint32 signature = 0;
    quint32 tag = 0;
    quint32 residue = 0;
    quint8 status = 0;
    QString error;

    bool ok() const { return valid && status == 0; }
    static Csw decode(const QByteArray& raw);
};

/// Builds a CBW with `opcode` in cb[0] and `parameters` in cb[1..].
Cbw makeCbw(quint8 opcode, quint32 transferLength, bool directionIn,
            const QByteArray& parameters = {}, quint32 tag = 1);
Cbw makeReadSector(quint32 startSector, quint32 count, int sectorSize = int(kSectorSize));
Cbw makeWriteSector(quint32 startSector, quint32 count, int sectorSize = int(kSectorSize));
Cbw makeEraseSector(quint32 startSector, quint32 count);
Cbw makeDeviceInfo(quint32 transferLength = 128);

struct DeviceInfo {
    quint32 version = 0;
    QString chip;
    QByteArray raw;
};

class Client {
public:
    explicit Client(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    DeviceInfo deviceInfo(int timeoutMs = 5000);
    QByteArray readCapability(int timeoutMs = 5000);
    quint32 readFlashId(int timeoutMs = 5000);
    QByteArray readChipInfo(int timeoutMs = 5000);

    QByteArray readSectors(quint32 startSector, quint32 count, int sectorSize = int(kSectorSize),
                           int timeoutMs = 60000);
    void writeSectors(quint32 startSector, const QByteArray& data,
                      int sectorSize = int(kSectorSize), int timeoutMs = 60000);
    void eraseSectors(quint32 startSector, quint32 count, int sectorSize = int(kSectorSize),
                      int timeoutMs = 60000);

    QByteArray readLba(quint64 lba, quint32 count, int timeoutMs = 60000);
    void writeLba(quint64 lba, const QByteArray& data, int timeoutMs = 60000);

    /// flag 0 = normal reboot, 1 = reboot into Loader/MaskROM (vendor specific).
    void reset(quint8 flag = 0, int timeoutMs = 5000);
    void rebootToLoader(int timeoutMs = 5000) { reset(1, timeoutMs); }

    quint32 nextTag() const { return m_nextTag; }

private:
    Csw transfer(const Cbw& cbw, const QByteArray& outData, QByteArray* inData, int timeoutMs);
    usb::IUsbTransport& m_transport;
    proto::Reporter m_reporter;
    quint32 m_nextTag = 1;
};

/// Human readable Rockchip USB mode from the PID (loader/maskrom variants).
QString deviceModeName(quint16 pid);

} // namespace gf::rk
