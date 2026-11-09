#include "WinUsbTransport.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

#include <setupapi.h>
#include <winusb.h>

#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <iterator>
#include <vector>

namespace gf::usb {

namespace {

// {A5DCBF10-6530-11D2-901F-00C04FB951ED} = GUID_DEVINTERFACE_USB_DEVICE.
// Declared literally so the file does not depend on <usbiodef.h> being pulled
// in by a specific SDK / MinGW header order.
const GUID kUsbDeviceInterfaceGuid = {
    0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};

constexpr DWORD kMaxTransferChunk = 4u * 1024u * 1024u; // 4 MiB per WinUSB call

QString win32ErrorText(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text;
    if (length && buffer) {
        text = QString::fromWCharArray(buffer, int(length)).trimmed();
        LocalFree(buffer);
    }
    if (text.isEmpty())
        text = QStringLiteral("Windows 错误码 %1").arg(code);
    return text;
}

QString readStringProperty(HDEVINFO devInfo, SP_DEVINFO_DATA* info, DWORD property)
{
    wchar_t buffer[1024] = {};
    DWORD type = 0;
    DWORD size = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(devInfo, info, property, &type,
                                           reinterpret_cast<PBYTE>(buffer), sizeof(buffer), &size)) {
        return {};
    }
    if (size < sizeof(wchar_t))
        return {};
    return QString::fromWCharArray(buffer);
}

} // namespace

WinUsbTransport::WinUsbTransport() = default;

WinUsbTransport::~WinUsbTransport()
{
    close();
}

void WinUsbTransport::resetState()
{
    m_handle = nullptr;
    m_winusb = nullptr;
    m_inPipe = 0;
    m_outPipe = 0;
    m_inMaxPacket = 0;
    m_outMaxPacket = 0;
    m_open = false;
}

void WinUsbTransport::setError(const QString& message)
{
    m_error = message;
}

bool WinUsbTransport::open(const PortInfo& port)
{
    close();
    m_port = port;
    m_error.clear();

    if (port.devicePath.isEmpty()) {
        setError(QStringLiteral("设备接口路径为空"));
        return false;
    }

    const QString nativePath = QDir::toNativeSeparators(port.devicePath);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        setError(QStringLiteral("打开设备失败：%1").arg(win32ErrorText(GetLastError())));
        return false;
    }
    m_handle = handle;

    WINUSB_INTERFACE_HANDLE winusb = nullptr;
    if (!WinUsb_Initialize(handle, &winusb)) {
        setError(QStringLiteral("WinUsb_Initialize 失败（设备可能未绑定 WinUSB 驱动）：%1")
                     .arg(win32ErrorText(GetLastError())));
        CloseHandle(handle);
        resetState();
        return false;
    }
    m_winusb = winusb;

    USB_INTERFACE_DESCRIPTOR interfaceDescriptor = {};
    if (!WinUsb_QueryInterfaceSettings(winusb, 0, &interfaceDescriptor)) {
        setError(QStringLiteral("读取 USB 接口描述符失败：%1").arg(win32ErrorText(GetLastError())));
        close();
        return false;
    }

    // EDL/BROM/FEL/RockUSB devices use bulk endpoints; pick the first IN/OUT pair.
    for (UCHAR i = 0; i < interfaceDescriptor.bNumEndpoints; ++i) {
        WINUSB_PIPE_INFORMATION pipe = {};
        if (!WinUsb_QueryPipe(winusb, 0, i, &pipe))
            continue;
        if (pipe.PipeType != UsbdPipeTypeBulk)
            continue;
        if (pipe.PipeId & 0x80) {
            if (m_inPipe == 0) {
                m_inPipe = pipe.PipeId;
                m_inMaxPacket = pipe.MaximumPacketSize;
            }
        } else if (m_outPipe == 0) {
            m_outPipe = pipe.PipeId;
            m_outMaxPacket = pipe.MaximumPacketSize;
        }
    }

    if (m_inPipe == 0 || m_outPipe == 0) {
        setError(QStringLiteral("设备没有可用的批量端点（IN=%1 OUT=%2），可能不是刷机模式")
                     .arg(m_inPipe)
                     .arg(m_outPipe));
        close();
        return false;
    }

    m_open = true;
    return true;
}

void WinUsbTransport::close()
{
    if (m_winusb) {
        WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(m_winusb));
        m_winusb = nullptr;
    }
    if (m_handle) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
    resetState();
}

bool WinUsbTransport::isOpen() const
{
    return m_open;
}

bool WinUsbTransport::pipeRead(void* buffer, quint32 length, quint32* transferred, int timeoutMs)
{
    *transferred = 0;
    if (!m_open || !m_winusb) {
        setError(QStringLiteral("设备未打开"));
        return false;
    }

    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) {
        setError(QStringLiteral("创建同步事件失败：%1").arg(win32ErrorText(GetLastError())));
        return false;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = event;
    DWORD bytes = 0;
    BOOL ok = WinUsb_ReadPipe(static_cast<WINUSB_INTERFACE_HANDLE>(m_winusb), m_inPipe,
                              static_cast<PUCHAR>(buffer), length, &bytes, &overlapped);
    if (!ok) {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            setError(QStringLiteral("USB 读取失败：%1").arg(win32ErrorText(error)));
            CloseHandle(event);
            return false;
        }
        const DWORD wait = WaitForSingleObject(event, timeoutMs > 0 ? DWORD(timeoutMs) : 1u);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(static_cast<HANDLE>(m_handle), &overlapped);
            WaitForSingleObject(event, 1000);
            setError(QStringLiteral("USB 读取超时（%1 ms）").arg(timeoutMs));
            CloseHandle(event);
            return false;
        }
        DWORD completed = 0;
        if (!GetOverlappedResult(static_cast<HANDLE>(m_handle), &overlapped, &completed, FALSE)) {
            setError(QStringLiteral("USB 读取完成状态异常：%1").arg(win32ErrorText(GetLastError())));
            CloseHandle(event);
            return false;
        }
        bytes = completed;
    }

    CloseHandle(event);
    *transferred = bytes;
    return true;
}

bool WinUsbTransport::pipeWrite(const void* buffer, quint32 length, quint32* transferred,
                                int timeoutMs)
{
    *transferred = 0;
    if (!m_open || !m_winusb) {
        setError(QStringLiteral("设备未打开"));
        return false;
    }

    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) {
        setError(QStringLiteral("创建同步事件失败：%1").arg(win32ErrorText(GetLastError())));
        return false;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = event;
    DWORD bytes = 0;
    BOOL ok = WinUsb_WritePipe(static_cast<WINUSB_INTERFACE_HANDLE>(m_winusb), m_outPipe,
                               static_cast<PUCHAR>(const_cast<void*>(buffer)), length, &bytes,
                               &overlapped);
    if (!ok) {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            setError(QStringLiteral("USB 写入失败：%1").arg(win32ErrorText(error)));
            CloseHandle(event);
            return false;
        }
        const DWORD wait = WaitForSingleObject(event, timeoutMs > 0 ? DWORD(timeoutMs) : 1u);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(static_cast<HANDLE>(m_handle), &overlapped);
            WaitForSingleObject(event, 1000);
            setError(QStringLiteral("USB 写入超时（%1 ms）").arg(timeoutMs));
            CloseHandle(event);
            return false;
        }
        DWORD completed = 0;
        if (!GetOverlappedResult(static_cast<HANDLE>(m_handle), &overlapped, &completed, FALSE)) {
            setError(QStringLiteral("USB 写入完成状态异常：%1").arg(win32ErrorText(GetLastError())));
            CloseHandle(event);
            return false;
        }
        bytes = completed;
    }

    CloseHandle(event);
    *transferred = bytes;
    return true;
}

qint64 WinUsbTransport::write(const QByteArray& data, int timeoutMs)
{
    if (!m_open) {
        setError(QStringLiteral("设备未打开"));
        return -1;
    }
    qint64 offset = 0;
    while (offset < data.size()) {
        const quint32 chunk = quint32(std::min<qint64>(data.size() - offset, kMaxTransferChunk));
        quint32 transferred = 0;
        if (!pipeWrite(data.constData() + offset, chunk, &transferred, timeoutMs))
            return offset > 0 ? offset : -1;
        offset += transferred;
        if (transferred == 0)
            break;
    }
    return offset;
}

QByteArray WinUsbTransport::read(qint64 maxSize, int timeoutMs)
{
    if (!m_open) {
        setError(QStringLiteral("设备未打开"));
        return {};
    }
    if (maxSize <= 0)
        return {};

    const quint32 chunk = quint32(std::min<qint64>(maxSize, kMaxTransferChunk));
    QByteArray buffer(int(chunk), Qt::Uninitialized);
    quint32 transferred = 0;
    if (!pipeRead(buffer.data(), chunk, &transferred, timeoutMs))
        return {};
    buffer.truncate(int(transferred));
    return buffer;
}

bool WinUsbTransport::controlTransfer(quint8 requestType, quint8 request, quint16 value,
                                      quint16 index, const QByteArray& data, QByteArray* response,
                                      int timeoutMs)
{
    if (!m_open || !m_winusb) {
        setError(QStringLiteral("设备未打开"));
        return false;
    }

    WINUSB_SETUP_PACKET setup = {};
    setup.RequestType = requestType;
    setup.Request = request;
    setup.Value = value;
    setup.Index = index;
    setup.Length = quint16(data.size());

    const bool directionIn = (requestType & 0x80) != 0;
    QByteArray outBuffer = data;
    if (response && directionIn)
        outBuffer.resize(int(setup.Length));

    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) {
        setError(QStringLiteral("创建同步事件失败：%1").arg(win32ErrorText(GetLastError())));
        return false;
    }

    OVERLAPPED overlapped = {};
    overlapped.hEvent = event;
    DWORD bytes = 0;
    BOOL ok = WinUsb_ControlTransfer(static_cast<WINUSB_INTERFACE_HANDLE>(m_winusb), setup,
                                     outBuffer.isEmpty()
                                         ? nullptr
                                         : reinterpret_cast<PUCHAR>(outBuffer.data()),
                                     DWORD(outBuffer.size()), &bytes, &overlapped);
    if (!ok) {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            setError(QStringLiteral("USB 控制传输失败：%1").arg(win32ErrorText(error)));
            CloseHandle(event);
            return false;
        }
        const DWORD wait = WaitForSingleObject(event, timeoutMs > 0 ? DWORD(timeoutMs) : 1u);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(static_cast<HANDLE>(m_handle), &overlapped);
            WaitForSingleObject(event, 1000);
            setError(QStringLiteral("USB 控制传输超时（%1 ms）").arg(timeoutMs));
            CloseHandle(event);
            return false;
        }
        DWORD completed = 0;
        if (!GetOverlappedResult(static_cast<HANDLE>(m_handle), &overlapped, &completed, FALSE)) {
            setError(QStringLiteral("USB 控制传输完成状态异常：%1")
                         .arg(win32ErrorText(GetLastError())));
            CloseHandle(event);
            return false;
        }
        bytes = completed;
    }

    CloseHandle(event);
    if (response && directionIn) {
        outBuffer.truncate(std::min<int>(int(bytes), outBuffer.size()));
        *response = outBuffer;
    }
    return true;
}

QByteArray WinUsbTransport::readAll(int timeoutMs)
{
    QByteArray out;
    QElapsedTimer timer;
    timer.start();
    while (true) {
        const qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0 && !out.isEmpty())
            break;
        const QByteArray chunk = read(64 * 1024, int(std::max<qint64>(remaining, 1)));
        if (chunk.isEmpty())
            break;
        out.append(chunk);
        if (out.size() > 8 * 1024 * 1024)
            break;
    }
    return out;
}

QString WinUsbTransport::errorString() const
{
    return m_error;
}

// --------------------------------------------------------------------------
// Device enumeration (SetupAPI)
// --------------------------------------------------------------------------

QList<PortInfo> enumerateFlashPorts()
{
    QList<PortInfo> result;
    HDEVINFO devInfo = SetupDiGetClassDevsW(&kUsbDeviceInterfaceGuid, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
        return result;

    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData = {};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devInfo, nullptr, &kUsbDeviceInterfaceGuid, index,
                                         &interfaceData)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;
            continue;
        }

        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, nullptr, 0, &required, nullptr);
        if (required == 0)
            continue;

        std::vector<BYTE> detailBuffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(devInfoData);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &interfaceData, detail, required, nullptr,
                                              &devInfoData)) {
            continue;
        }

        PortInfo port;
        port.devicePath = QString::fromWCharArray(detail->DevicePath);

        // Registry hardware IDs look like USB\VID_05C6&PID_9008&REV_0300. The
        // registry value is REG_MULTI_SZ but fromWCharArray stops at the first
        // NUL, which is exactly the first (most specific) hardware id.
        const QString hardwareId = readStringProperty(devInfo, &devInfoData, SPDRP_HARDWAREID);
        static const QRegularExpression vidPidPattern(
            QStringLiteral("VID_([0-9A-Fa-f]{4}).*PID_([0-9A-Fa-f]{4})"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = vidPidPattern.match(hardwareId);
        if (!match.hasMatch())
            continue;
        port.vid = quint16(match.captured(1).toUShort(nullptr, 16));
        port.pid = quint16(match.captured(2).toUShort(nullptr, 16));
        port.chipHint = chipHintForIds(port.vid, port.pid);
        if (port.chipHint.isEmpty())
            continue; // not one of the supported flashing chipsets

        const QString friendlyName = readStringProperty(devInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        const QString deviceDesc = readStringProperty(devInfo, &devInfoData, SPDRP_DEVICEDESC);
        port.description = friendlyName.isEmpty() ? deviceDesc : friendlyName;
        if (port.description.isEmpty())
            port.description = QStringLiteral("USB 刷机设备");
        port.portName = port.description;

        wchar_t instanceId[512] = {};
        DWORD instanceSize = 0;
        if (SetupDiGetDeviceInstanceIdW(devInfo, &devInfoData, instanceId,
                                        DWORD(std::size(instanceId)), &instanceSize)) {
            const QString id = QString::fromWCharArray(instanceId);
            port.serial = id.section(QLatin1Char('\\'), -1);
        }

        result.append(port);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

} // namespace gf::usb

#endif // _WIN32
