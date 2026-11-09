#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <cstdint>

namespace gf::device {

/// One present Windows PnP device as reported by SetupAPI, before the device
/// layer classifies it. Kept free of Windows types so parsing/classification
/// logic can be unit tested without a real device tree.
struct RawDevice {
    QString instanceId;          // e.g. USB\VID_05C6&PID_9008\6&2a3b4c5d&0&2
    QString description;         // SPDRP_DEVICEDESC
    QString friendlyName;        // SPDRP_FRIENDLYNAME, e.g. "Qualcomm HS-USB QDLoader 9008 (COM5)"
    QStringList hardwareIds;     // SPDRP_HARDWAREID (multi-sz, first entry is the main one)
    QStringList compatibleIds;   // SPDRP_COMPATIBLEIDS
    QString service;             // bound kernel driver service, empty when no driver
    QString className;           // SPDRP_CLASS, e.g. "Ports", "USB", "AndroidUsbDeviceClass"
    QString classGuid;           // SPDRP_CLASSGUID, e.g. "{4d36e978-e325-11ce-bfc1-08002be10318}"
    QString driverKey;           // SPDRP_DRIVER registry key (driver package instance)
    QString manufacturer;        // SPDRP_MFG
    QString locationInfo;        // SPDRP_LOCATION_INFORMATION
    quint16 vid = 0;             // parsed from the hardware id
    quint16 pid = 0;
    quint32 busNumber = 0;
    quint32 address = 0;
    int comPort = 0;             // > 0 for serial ports (COM5 -> 5)
    bool hasProblem = false;     // device node currently reports a problem
    quint32 problemCode = 0;     // CM_PROB_* value, 28 == drivers not installed

    /// First hardware id, or the instance id when there are none.
    QString primaryHardwareId() const;

    /// True when the device node lives on the USB bus.
    bool isUsb() const;

    /// Friendly name when available, otherwise the description.
    QString displayName() const;

    /// All hardware + compatible ids joined with '|' (lower case), handy for
    /// case-insensitive keyword checks.
    QString searchableIds() const;
};

/// Enumerates every present PnP device on Windows. Uses SetupAPI; returns an
/// empty list on non-Windows platforms or when enumeration fails.
QList<RawDevice> enumeratePresentDevices();

/// Parses "USB\VID_05C6&PID_9008&REV_0000" style ids. Returns false when no
/// VID/PID pair is present; out parameters are only written on success.
bool parseVidPid(const QString& hardwareId, quint16* vid, quint16* pid);

/// Parses the first COM port number from a string such as "COM5" or
/// "USB Serial Device (COM12)". Returns 0 when there is no COM port.
int parseComPort(const QString& text);

/// Formats a VID/PID pair as "VID_05C6&PID_9008" (empty for 0/0).
QString formatVidPid(quint16 vid, quint16 pid);

} // namespace gf::device
