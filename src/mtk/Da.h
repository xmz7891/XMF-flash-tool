#pragma once

// MediaTek DA (download agent) handling.
//
// A BROM session boots two DA stages:
//   DA1 - small first stage, loaded with SEND_DA (0xD7) into SRAM and started
//         with JUMP_DA (0xD5). The device re-enumerates and exposes the MTK DA
//         USB port (VID 0x0E8D).
//   DA2 - full download agent, loaded into DRAM (typically 0x40000000) and
//         started the same way. After DA2 the flash operations (read/write/
//         erase/partition table) become available.
//
// The DA firmware reuses the BROM command opcodes (0xD0..0xDB) for register
// and memory access. Some DA versions wrap commands into their own checksum
// packet format ("DA protocol"); that layer is TODO and documented in
// docs/PROTOCOLS.md - the command wrappers here match the BROM layout.

#include <QByteArray>

#include "Brom.h"
#include "usb/ProtocolSupport.h"
#include "usb/UsbTransport.h"

namespace gf::mtk::da {

/// Default DA1 load address (2 MiB), matches the resource package DA1Address.
inline constexpr quint32 kDefaultDa1Address = 0x00200000;
/// Default DA2 load address (1 GiB), matches DA2Address in the resource package.
inline constexpr quint32 kDefaultDa2Address = 0x40000000;

/// DA protocol command wrappers around the shared BROM command layout.
class Client {
public:
    explicit Client(usb::IUsbTransport& transport, proto::Reporter reporter = {});

    /// Sends DA2 with SEND_DA and (optionally) jumps to it.
    void downloadDa2(const QByteArray& data, quint32 address = kDefaultDa2Address,
                     quint32 signatureLength = 0, bool jump = true, int progressBase = 0,
                     int progressSpan = 0);

    quint32 read32(quint32 address);
    quint16 read16(quint32 address);
    void write32(quint32 address, quint32 value, bool verify = true);
    void write16(quint32 address, quint16 value, bool verify = true);
    quint32 echo(quint32 value);
    QByteArray getTargetConfig();
    void jump(quint32 address);

private:
    brom::Client m_link;
    proto::Reporter m_reporter;
};

/// Waits for the DA port to re-appear after DA1 starts. Returns an empty
/// PortInfo after `timeoutMs` (caller reports a readable error).
usb::PortInfo waitForDaPort(int timeoutMs, const proto::Reporter& reporter);

} // namespace gf::mtk::da
