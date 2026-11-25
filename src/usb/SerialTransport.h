#pragma once

// Windows serial (COM) transport for Qualcomm 9008 devices that are bound to
// the Qualcomm USB serial driver instead of WinUSB. QDLoader 9008 enumerates
// as a CDC serial port; Sahara/Firehose speak the same byte protocol over it.

#include <QByteArray>
#include <QString>

#include "UsbTransport.h"

namespace gf::usb {

class SerialTransport final : public IUsbTransport {
public:
    SerialTransport();
    ~SerialTransport() override;

    bool open(const PortInfo& port) override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray& data, int timeoutMs = 5000) override;
    QByteArray read(qint64 maxSize, int timeoutMs = 5000) override;
    bool controlTransfer(quint8 requestType, quint8 request, quint16 value, quint16 index,
                         const QByteArray& data, QByteArray* response = nullptr,
                         int timeoutMs = 5000) override;
    QByteArray readAll(int timeoutMs = 100) override;
    QString errorString() const override;

private:
    void setError(const QString& error);
    void* m_handle = nullptr;
    bool m_open = false;
    QString m_error;
    PortInfo m_port;
};

} // namespace gf::usb
