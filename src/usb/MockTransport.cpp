#include "MockTransport.h"

#include <QString>

namespace gf::usb {

MockTransport::MockTransport() = default;
MockTransport::~MockTransport() = default;

bool MockTransport::open(const PortInfo& port)
{
    m_port = port;
    m_opened = true;
    m_isOpen = m_openResult;
    m_lastError = m_openResult ? QString() : m_openError;
    return m_openResult;
}

void MockTransport::close()
{
    m_closed = true;
    m_isOpen = false;
}

bool MockTransport::isOpen() const
{
    return m_isOpen;
}

qint64 MockTransport::write(const QByteArray& data, int timeoutMs)
{
    Q_UNUSED(timeoutMs);
    if (m_writeResult < 0 || !m_writeError.isEmpty()) {
        m_lastError = m_writeError.isEmpty() ? QStringLiteral("模拟写入失败") : m_writeError;
        return -1;
    }
    // A configured short write only accepts that many bytes, exactly like a
    // real bulk pipe; the rest must be retried by the caller.
    const qint64 accepted = m_writeResult > 0
                                ? qMin<qint64>(m_writeResult, data.size())
                                : qint64(data.size());
    const QByteArray chunk = data.left(int(accepted));
    m_writes.append(chunk);
    if (m_writeHook) {
        const QByteArray reply = m_writeHook(chunk);
        if (!reply.isEmpty())
            m_readQueue.append(reply);
    }
    return accepted;
}

QByteArray MockTransport::read(qint64 maxSize, int timeoutMs)
{
    Q_UNUSED(timeoutMs);
    ++m_reads;
    if (!m_readError.isEmpty()) {
        m_lastError = m_readError;
        return {};
    }
    if (maxSize <= 0 || m_readQueue.isEmpty())
        return {};
    QByteArray& front = m_readQueue.first();
    if (front.size() <= maxSize) {
        const QByteArray out = front;
        m_readQueue.removeFirst();
        return out;
    }
    const QByteArray out = front.left(int(maxSize));
    front.remove(0, int(maxSize));
    return out;
}

bool MockTransport::controlTransfer(quint8 requestType, quint8 request, quint16 value,
                                    quint16 index, const QByteArray& data, QByteArray* response,
                                    int timeoutMs)
{
    ControlCall call;
    call.requestType = requestType;
    call.request = request;
    call.value = value;
    call.index = index;
    call.data = data;
    call.timeoutMs = timeoutMs;
    m_controls.append(call);

    if (!m_controlResult) {
        m_lastError = m_controlError.isEmpty() ? QStringLiteral("模拟控制传输失败") : m_controlError;
        return false;
    }
    if (response) {
        if (!m_controlResponses.isEmpty())
            *response = m_controlResponses.takeFirst();
        else if (m_autoEchoControl)
            *response = data;
        else
            response->clear();
    }
    return true;
}

QByteArray MockTransport::readAll(int timeoutMs)
{
    Q_UNUSED(timeoutMs);
    QByteArray out;
    for (const auto& chunk : m_readQueue)
        out.append(chunk);
    m_readQueue.clear();
    return out;
}

QString MockTransport::errorString() const
{
    return m_lastError;
}

void MockTransport::setOpenResult(bool ok, const QString& error)
{
    m_openResult = ok;
    m_openError = error;
}

void MockTransport::setWriteResult(qint64 bytesWritten, const QString& error)
{
    m_writeResult = bytesWritten;
    m_writeError = error;
}

void MockTransport::setReadError(const QString& error)
{
    m_readError = error;
}

void MockTransport::setControlResult(bool ok, const QString& error)
{
    m_controlResult = ok;
    m_controlError = error;
}

void MockTransport::queueRead(const QByteArray& bytes)
{
    if (!bytes.isEmpty())
        m_readQueue.append(bytes);
}

void MockTransport::queueControlResponse(const QByteArray& bytes)
{
    m_controlResponses.append(bytes);
}

void MockTransport::setWriteHook(WriteHook hook)
{
    m_writeHook = std::move(hook);
}

void MockTransport::setAutoEchoControl(bool enabled)
{
    m_autoEchoControl = enabled;
}

void MockTransport::clearQueuedReads()
{
    m_readQueue.clear();
}

QByteArray MockTransport::written() const
{
    QByteArray out;
    for (const auto& chunk : m_writes)
        out.append(chunk);
    return out;
}

QByteArray MockTransport::takeWritten()
{
    QByteArray out = written();
    m_writes.clear();
    return out;
}

qsizetype MockTransport::queuedReadBytes() const
{
    qsizetype total = 0;
    for (const auto& chunk : m_readQueue)
        total += chunk.size();
    return total;
}

void MockTransport::reset()
{
    m_port = {};
    m_opened = false;
    m_closed = false;
    m_isOpen = false;
    m_openResult = true;
    m_openError.clear();
    m_writeResult = 0;
    m_writeError.clear();
    m_readError.clear();
    m_controlResult = true;
    m_controlError.clear();
    m_autoEchoControl = false;
    m_lastError.clear();
    m_readQueue.clear();
    m_controlResponses.clear();
    m_writes.clear();
    m_controls.clear();
    m_writeHook = nullptr;
    m_reads = 0;
}

} // namespace gf::usb
