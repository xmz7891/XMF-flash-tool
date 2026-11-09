#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>

namespace gf::usb {

/// USB descriptor of a candidate flashing device.
struct PortInfo {
    QString portName;       // e.g. "COM5" or "UsbDk-1" / WinUSB path
    QString description;
    quint16 vid = 0;
    quint16 pid = 0;
    QString serial;
    QString chipHint;       // "qcom-edl" / "mtk-brom" / "sprd-fdl" / "rk" / "aw-fel"
    QString devicePath;     // WinUSB device interface path
    int busNumber = 0;
    int address = 0;
};

enum class ChipPlatform { Unknown, Qualcomm, MediaTek, Spreadtrum, Rockchip, Allwinner };

/// Abstract bulk/control transport. All protocol code depends on this only.
class IUsbTransport {
public:
    virtual ~IUsbTransport() = default;
    virtual bool open(const PortInfo& port) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual qint64 write(const QByteArray& data, int timeoutMs = 5000) = 0;
    virtual QByteArray read(qint64 maxSize, int timeoutMs = 5000) = 0;
    virtual bool controlTransfer(quint8 requestType, quint8 request, quint16 value, quint16 index,
                                 const QByteArray& data, QByteArray* response = nullptr,
                                 int timeoutMs = 5000) = 0;
    virtual QByteArray readAll(int timeoutMs = 100) = 0;
    virtual QString errorString() const = 0;
};

/// Enumerates USB devices that belong to one of the supported flashing chipsets.
QList<PortInfo> enumerateFlashPorts();

/// Parses the chip hint from a USB VID/PID pair (0000/0000 = unknown).
QString chipHintForIds(quint16 vid, quint16 pid);
ChipPlatform platformForHint(const QString& hint);

/// Returns a transport for the given port. The caller owns it.
IUsbTransport* createTransport(const PortInfo& port);

} // namespace gf::usb
