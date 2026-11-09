#pragma once

// Scripted, hardware-free IUsbTransport used by every protocol unit test.
//
// Typical use:
//     MockTransport usb;
//     usb.queueRead(Sahara::helloPacket());          // answer the next read()
//     SaharaClient client(usb);
//     client.handshake();
//     QCOMPARE(usb.writes().first(), expectedHostHelloResponse);
//
// Reads are served from a FIFO queue of byte chunks. A chunk larger than the
// requested size is split (the tail stays queued) so `readExact()` loops behave
// like on a real bulk pipe. An empty queue means "nothing arrived within the
// timeout", i.e. read() returns an empty QByteArray.
//
// A write hook can be installed to emulate a device state machine:
//     usb.setWriteHook([](const QByteArray& hostFrame) {
//         return fakeDeviceReply(hostFrame);
//     });
// Whatever the hook returns is appended to the read queue.

#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>

#include "UsbTransport.h"

namespace gf::usb {

class MockTransport final : public IUsbTransport {
public:
    struct ControlCall {
        quint8 requestType = 0;
        quint8 request = 0;
        quint16 value = 0;
        quint16 index = 0;
        QByteArray data;
        int timeoutMs = 0;
    };
    using WriteHook = std::function<QByteArray(const QByteArray&)>;

    MockTransport();
    ~MockTransport() override;

    // --- IUsbTransport ---
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

    // --- scripting ---
    void setOpenResult(bool ok, const QString& error = {});
    /// 0 = all bytes, -1 = write error. Default: accept everything.
    void setWriteResult(qint64 bytesWritten, const QString& error = {});
    void setReadError(const QString& error);
    void setControlResult(bool ok, const QString& error = {});
    void queueRead(const QByteArray& bytes);
    void queueControlResponse(const QByteArray& bytes);
    void setWriteHook(WriteHook hook);
    void setAutoEchoControl(bool enabled);
    void clearQueuedReads();

    // --- observations ---
    const QList<QByteArray>& writes() const { return m_writes; }
    QByteArray written() const;         // concatenation of all writes
    QByteArray takeWritten();           // concatenation + clears the record
    const QList<ControlCall>& controls() const { return m_controls; }
    const PortInfo& openedPort() const { return m_port; }
    int writeCount() const { return int(m_writes.size()); }
    int readCount() const { return m_reads; }
    bool wasOpened() const { return m_opened; }
    bool wasClosed() const { return m_closed; }
    qsizetype queuedReadBytes() const;
    void reset();

private:
    PortInfo m_port;
    bool m_openResult = true;
    bool m_opened = false;
    bool m_closed = false;
    bool m_isOpen = false;
    QString m_openError;
    qint64 m_writeResult = 0;
    QString m_writeError;
    QString m_readError;
    bool m_controlResult = true;
    QString m_controlError;
    bool m_autoEchoControl = false;
    QString m_lastError;
    QList<QByteArray> m_readQueue;
    QList<QByteArray> m_controlResponses;
    QList<QByteArray> m_writes;
    QList<ControlCall> m_controls;
    WriteHook m_writeHook;
    int m_reads = 0;
};

} // namespace gf::usb
