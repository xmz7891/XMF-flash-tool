#pragma once

// Small blocking-I/O helpers shared by the protocol layers. They are written
// against gf::usb::IUsbTransport only, so the MockTransport used by the unit
// tests exercises exactly the same code paths as real hardware.

#include <QByteArray>

#include "ProtocolSupport.h"
#include "UsbTransport.h"

namespace gf::usb {

/// Writes all bytes, retrying short writes. Throws std::runtime_error on error.
void writeAll(IUsbTransport& transport, const QByteArray& data, int timeoutMs = 5000,
              const proto::Reporter& reporter = {});

/// Reads exactly `size` bytes. A short/incomplete read is retried until the
/// timeout budget is exhausted; throws std::runtime_error on timeout.
QByteArray readExact(IUsbTransport& transport, qint64 size, int timeoutMs = 5000,
                     const proto::Reporter& reporter = {});

/// Reads whatever is available in one bulk transfer (up to maxSize).
QByteArray readSome(IUsbTransport& transport, qint64 maxSize, int timeoutMs = 5000);

/// Keeps reading until `terminator` has been seen at the end of the accumulated
/// data or the timeout expires. Returns the accumulated bytes (never throws for
/// a timeout, the caller decides what a partial answer means).
QByteArray readUntil(IUsbTransport& transport, const QByteArray& terminator, int timeoutMs,
                     qsizetype maxSize = 4 * 1024 * 1024);

/// Discards any pending input; used before starting a new protocol phase.
void drainInput(IUsbTransport& transport, int timeoutMs = 50);

/// Reads exactly one packet payload of `size` bytes and validates it.
QByteArray readPacket(IUsbTransport& transport, qint64 size, int timeoutMs = 5000);

} // namespace gf::usb
