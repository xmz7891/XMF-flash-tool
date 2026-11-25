#include "SerialTransport.h"

#ifdef _WIN32
#include <windows.h>

#include <algorithm>

namespace gf::usb {

namespace {
QString comNameFrom(const QString& value)
{
    const int at = value.indexOf(QStringLiteral("COM"), 0, Qt::CaseInsensitive);
    if (at < 0)
        return {};
    int end = at + 3;
    while (end < value.size() && value.at(end).isDigit())
        ++end;
    if (end <= at + 3)
        return {};
    return value.mid(at, end - at).toUpper();
}
} // namespace

SerialTransport::SerialTransport() = default;
SerialTransport::~SerialTransport() { close(); }

bool SerialTransport::open(const PortInfo& port)
{
    close();
    m_port = port;
    QString com = comNameFrom(port.devicePath);
    if (com.isEmpty())
        com = comNameFrom(port.portName);
    if (com.isEmpty()) {
        setError(QStringLiteral("没有可用的 9008 串口名"));
        return false;
    }

    const QString path = QStringLiteral("\\\\.\\") + com;
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                                0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        setError(QStringLiteral("打开串口 %1 失败：%2").arg(com).arg(GetLastError()));
        return false;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        setError(QStringLiteral("读取串口 %1 参数失败").arg(com));
        CloseHandle(handle);
        return false;
    }
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(handle, &dcb)) {
        setError(QStringLiteral("设置串口 %1 参数失败").arg(com));
        CloseHandle(handle);
        return false;
    }
    SetupComm(handle, 256 * 1024, 256 * 1024);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);
    m_handle = handle;
    m_open = true;
    m_error.clear();
    return true;
}

void SerialTransport::close()
{
    if (m_handle) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
    m_open = false;
}

bool SerialTransport::isOpen() const { return m_open; }

qint64 SerialTransport::write(const QByteArray& data, int timeoutMs)
{
    if (!m_open || !m_handle) {
        setError(QStringLiteral("串口未打开"));
        return -1;
    }
    COMMTIMEOUTS to = {};
    to.WriteTotalTimeoutConstant = timeoutMs > 0 ? DWORD(timeoutMs) : 1000;
    to.WriteTotalTimeoutMultiplier = 0;
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = MAXDWORD;
    to.ReadTotalTimeoutConstant = 1;
    SetCommTimeouts(static_cast<HANDLE>(m_handle), &to);

    qint64 total = 0;
    while (total < data.size()) {
        DWORD written = 0;
        const DWORD want = DWORD(std::min<qint64>(data.size() - total, 64 * 1024));
        if (!WriteFile(static_cast<HANDLE>(m_handle), data.constData() + total, want, &written,
                       nullptr)) {
            setError(QStringLiteral("串口写入失败：%1").arg(GetLastError()));
            return total > 0 ? total : -1;
        }
        if (written == 0) {
            setError(QStringLiteral("串口写入超时"));
            return total > 0 ? total : -1;
        }
        total += written;
    }
    return total;
}

QByteArray SerialTransport::read(qint64 maxSize, int timeoutMs)
{
    if (!m_open || !m_handle) {
        setError(QStringLiteral("串口未打开"));
        return {};
    }
    if (maxSize <= 0)
        return {};
    COMMTIMEOUTS to = {};
    // A short inter-byte gap ends the read once a packet has arrived; the
    // total constant is the hard per-call upper bound.
    to.ReadIntervalTimeout = 50;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = timeoutMs > 0 ? DWORD(timeoutMs) : 1;
    to.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(static_cast<HANDLE>(m_handle), &to);

    QByteArray out;
    out.resize(static_cast<int>(std::min<qint64>(maxSize, 1024 * 1024)));
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(m_handle), out.data(), DWORD(out.size()), &got, nullptr)) {
        const DWORD error = GetLastError();
        if (error == ERROR_OPERATION_ABORTED || error == ERROR_SEM_TIMEOUT)
            return {};
        setError(QStringLiteral("串口读取失败：%1").arg(error));
        return {};
    }
    out.resize(int(got));
    if (got == 0)
        setError(QStringLiteral("串口读取超时（%1 ms）").arg(timeoutMs));
    return out;
}

bool SerialTransport::controlTransfer(quint8, quint8, quint16, quint16, const QByteArray&,
                                      QByteArray*, int)
{
    setError(QStringLiteral("9008 串口不支持控制传输"));
    return false;
}

QByteArray SerialTransport::readAll(int timeoutMs)
{
    QByteArray all;
    while (true) {
        const QByteArray part = read(256 * 1024, timeoutMs);
        if (part.isEmpty())
            break;
        all.append(part);
    }
    return all;
}

QString SerialTransport::errorString() const { return m_error; }
void SerialTransport::setError(const QString& error) { m_error = error; }

} // namespace gf::usb
#else
namespace gf::usb {
SerialTransport::SerialTransport() = default;
SerialTransport::~SerialTransport() = default;
bool SerialTransport::open(const PortInfo&) { return false; }
void SerialTransport::close() {}
bool SerialTransport::isOpen() const { return false; }
qint64 SerialTransport::write(const QByteArray&, int) { return -1; }
QByteArray SerialTransport::read(qint64, int) { return {}; }
bool SerialTransport::controlTransfer(quint8, quint8, quint16, quint16, const QByteArray&,
                                      QByteArray*, int) { return false; }
QByteArray SerialTransport::readAll(int) { return {}; }
QString SerialTransport::errorString() const { return {}; }
} // namespace gf::usb
#endif
