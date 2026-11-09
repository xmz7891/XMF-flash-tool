#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "DeviceEnumerator.h"

namespace gf::device {

/// What a detected port/device is most likely used for.
enum class PortKind {
    Unknown = 0,
    Serial,          // plain COM port (modem, USB serial adapter)
    Adb,             // Android Debug Bridge interface
    Fastboot,        // Android bootloader (fastboot) interface
    FastbootD,       // userspace fastbootd interface
    Mtp,             // Android media transfer / composite device
    QdLoader,        // Qualcomm EDL 9008
    MtkBrom,         // MediaTek BROM
    MtkPreloader,    // MediaTek preloader
    SprdFdl,         // Spreadtrum FDL / research download
    Rockchip,        // Rockchip loader / maskrom
    Allwinner,       // Allwinner FEL
};

/// Classification result for one device node. Pure data: no Windows APIs, so
/// it can be unit tested with hand-built RawDevice values.
struct PortIdentity {
    PortKind kind = PortKind::Unknown;
    QString chipHint;      // "qcom-edl", "mtk-brom", "sprd-fdl", "rk", "aw-fel", "adb", "fastboot"
    QString label;         // Chinese short label, e.g. "高通 EDL 9008"
    bool isFlashing = false;
    bool isAdb = false;
    bool isFastboot = false;
};

/// Identifies a raw device node from its description, service, hardware ids
/// and VID/PID pair.
PortIdentity classifyPort(const RawDevice& device);

/// Known chip hint for a VID/PID pair; empty when the pair is not a flashing id.
QString chipHintForVidPid(quint16 vid, quint16 pid);

QString portKindId(PortKind kind);
QString portKindLabel(PortKind kind);
QString chipHintLabel(const QString& chipHint);

/// One enumerated port with its classification.
struct PortRecord {
    RawDevice raw;
    PortIdentity identity;

    int comPort() const { return raw.comPort; }
    QString portName() const;
    QString chipHint() const { return identity.chipHint; }
    bool isFlashing() const { return identity.isFlashing; }
    QVariantMap toVariant() const;
};

/// Enumerates present USB / serial devices and classifies them. Cheap enough
/// (single SetupAPI pass) to be called on refresh.
QList<PortRecord> enumeratePorts();

QList<PortRecord> flashingPorts(const QList<PortRecord>& ports);
QList<PortRecord> adbPorts(const QList<PortRecord>& ports);
QList<PortRecord> fastbootPorts(const QList<PortRecord>& ports);

/// Chinese one-line summary such as "COM5 · 高通 EDL 9008（qcom-edl）".
QString portSummaryLine(const PortRecord& port);
/// Multi-line Chinese summary for logs, including a header line.
QString summarizePorts(const QList<PortRecord>& ports);

} // namespace gf::device
