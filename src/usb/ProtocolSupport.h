#pragma once

// Shared plumbing for every flashing protocol implementation (src/qcom,
// src/mtk, src/sprd, src/rk, src/aw).
//
// Protocol code must never touch the OS USB stack directly: it only talks to
// gf::usb::IUsbTransport. This header adds the two things every protocol needs
// on top of that interface:
//   * a progress / log / cancellation sink that works both in a worker task
//     (TaskContext adapter lives in TaskReporter.h) and in unit tests, and
//   * port discovery helpers that produce a clear Chinese error message when
//     no matching device is plugged in.

#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>
#include <memory>
#include <stdexcept>

#include "UsbTransport.h"

namespace gf::proto {

/// Thrown by Reporter::checkCanceled() (and by protocol loops) when the user
/// pressed "cancel". Tool handlers translate it into TaskContext::TaskCanceled.
class Canceled : public std::runtime_error {
public:
    Canceled() : std::runtime_error("任务已取消") {}
};

/// Thread-safe, UI-agnostic progress sink. Default-constructed = silent.
class Reporter {
public:
    enum class Level { Debug, Info, Warn, Error };

    using ProgressFn = std::function<void(int, const QString&)>;
    using LogFn = std::function<void(Level, const QString&)>;
    using CancelFn = std::function<bool()>;

    Reporter() = default;
    Reporter(ProgressFn progress, LogFn log, CancelFn canceled)
        : m_progress(std::move(progress)), m_log(std::move(log)), m_canceled(std::move(canceled))
    {
    }

    /// percent: 0..100, or -1 for "indeterminate".
    void progress(int percent, const QString& status = {}) const;
    void status(const QString& text) const { progress(-1, text); }
    void debug(const QString& message) const;
    void info(const QString& message) const;
    void warn(const QString& message) const;
    void error(const QString& message) const;
    bool isCanceled() const;
    /// Throws Canceled when the caller requested cancellation.
    void checkCanceled() const;
    bool isSilent() const { return !m_progress && !m_log; }

    static Reporter silent();

private:
    ProgressFn m_progress;
    LogFn m_log;
    CancelFn m_canceled;
};

/// Human readable platform name, e.g. "高通 9008".
QString platformName(usb::ChipPlatform platform);
/// Short category key used by logs/tools: qcom / mtk / sprd / rk / aw.
QString platformKey(usb::ChipPlatform platform);
/// chipHint value used by usb::PortInfo for this platform.
QString platformChipHint(usb::ChipPlatform platform);

/// The canonical "no device" message for a platform, e.g.
/// 「未检测到 9008 端口，请确认设备已进入 EDL 模式并安装驱动」.
QString noDeviceMessage(usb::ChipPlatform platform);

/// All currently present USB interfaces that belong to `platform`.
QList<usb::PortInfo> portsFor(usb::ChipPlatform platform);

/// Returns the preferred port (or the first present one) or throws
/// std::runtime_error(noDeviceMessage()).
usb::PortInfo requirePort(usb::ChipPlatform platform, const QString& preferredDevicePath = {},
                          const Reporter& reporter = {});

/// Creates + opens a transport for `platform`. Throws readable Chinese errors.
/// The returned transport is owned by the caller.
std::unique_ptr<usb::IUsbTransport> openTransport(usb::ChipPlatform platform,
                                                  const QString& preferredDevicePath = {},
                                                  const Reporter& reporter = {});

/// Small helpers shared by protocol frame builders.
QByteArray u16le(quint16 value);
QByteArray u32le(quint32 value);
QByteArray u64le(quint64 value);
QByteArray u16be(quint16 value);
QByteArray u32be(quint32 value);
QByteArray u64be(quint64 value);
quint16 readU16le(const QByteArray& data, qsizetype offset, bool* ok = nullptr);
quint32 readU32le(const QByteArray& data, qsizetype offset, bool* ok = nullptr);
quint64 readU64le(const QByteArray& data, qsizetype offset, bool* ok = nullptr);
quint16 readU16be(const QByteArray& data, qsizetype offset, bool* ok = nullptr);
quint32 readU32be(const QByteArray& data, qsizetype offset, bool* ok = nullptr);
quint64 readU64be(const QByteArray& data, qsizetype offset, bool* ok = nullptr);

} // namespace gf::proto
