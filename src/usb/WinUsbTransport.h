#pragma once

// Real WinUSB transport (Windows only).
//
// Enumerates USB device interfaces with SetupAPI and performs bulk/control
// transfers with WinUsb_* on a handle opened with FILE_FLAG_OVERLAPPED so every
// operation has a hard timeout (WaitForSingleObject + CancelIoEx).
//
// The protocol layers never include this header: they only see
// gf::usb::IUsbTransport and receive an instance from usb::createTransport().

#include <QByteArray>
#include <QString>

#include "UsbTransport.h"

namespace gf::usb {

class WinUsbTransport final : public IUsbTransport {
public:
    WinUsbTransport();
    ~WinUsbTransport() override;

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

    /// True when the transport has usable bulk IN/OUT endpoints.
    bool hasBulkPipes() const { return m_inPipe != 0 && m_outPipe != 0; }

private:
    void resetState();
    bool pipeRead(void* buffer, quint32 length, quint32* transferred, int timeoutMs);
    bool pipeWrite(const void* buffer, quint32 length, quint32* transferred, int timeoutMs);
    void setError(const QString& message);

    void* m_handle = nullptr;   // HANDLE from CreateFileW
    void* m_winusb = nullptr;   // WINUSB_INTERFACE_HANDLE from WinUsb_Initialize
    quint8 m_inPipe = 0;
    quint8 m_outPipe = 0;
    quint16 m_inMaxPacket = 0;
    quint16 m_outMaxPacket = 0;
    bool m_open = false;
    QString m_error;
    PortInfo m_port;
};

} // namespace gf::usb
