#include "UsbSpeed.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#include <usbioctl.h>
#include <usbiodef.h>
#include <QHash>
#endif
namespace gf::usb {
QString linkSpeedText(quint16 vid, quint16 pid)
{
#ifdef Q_OS_WIN
    HDEVINFO hubs = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_HUB, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hubs == INVALID_HANDLE_VALUE)
        return {};
    QString result;
    for (DWORD index = 0; result.isEmpty(); ++index) {
        SP_DEVICE_INTERFACE_DATA iface{};
        iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(hubs, nullptr, &GUID_DEVINTERFACE_USB_HUB, index, &iface))
            break;
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetail(hubs, &iface, nullptr, 0, &needed, nullptr);
        if (needed == 0)
            continue;
        QByteArray buffer(int(needed), '\0');
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        if (!SetupDiGetDeviceInterfaceDetail(hubs, &iface, detail, needed, nullptr, nullptr))
            continue;
        HANDLE hub = CreateFileW(detail->DevicePath, GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
        if (hub == INVALID_HANDLE_VALUE)
            continue;
        USB_NODE_INFORMATION node{};
        DWORD returned = 0;
        if (!DeviceIoControl(hub, IOCTL_USB_GET_NODE_INFORMATION, &node, sizeof(node),
                             &node, sizeof(node), &returned, nullptr)) {
            CloseHandle(hub);
            continue;
        }
        const int ports = node.u.HubInformation.HubDescriptor.bNumberOfPorts;
        for (int port = 1; port <= ports && result.isEmpty(); ++port) {
            USB_NODE_CONNECTION_INFORMATION_EX conn{};
            conn.ConnectionIndex = ULONG(port);
            if (!DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                                 &conn, sizeof(conn), &conn, sizeof(conn), &returned, nullptr))
                continue;
            if (conn.ConnectionStatus != DeviceConnected)
                continue;
            if (conn.DeviceDescriptor.idVendor != vid || conn.DeviceDescriptor.idProduct != pid)
                continue;
            // Prefer the V2 connection information: the Speed byte can still
            // say 2/3 on a 10 Gbps SuperSpeedPlus link. Operating bits, not
            // capability bits, decide the label.
            USB_NODE_CONNECTION_INFORMATION_EX_V2 v2{};
            v2.ConnectionIndex = ULONG(port);
            v2.Length = sizeof(v2);
            v2.SupportedUsbProtocols.Usb300 = 1;
            if (DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2,
                                &v2, sizeof(v2), &v2, sizeof(v2), &returned, nullptr)) {
                if (v2.Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher) {
                    result = QStringLiteral("USB 10G");
                } else if (v2.Flags.DeviceIsOperatingAtSuperSpeedOrHigher) {
                    result = QStringLiteral("USB3.2");
                }
            }
            if (result.isEmpty()) {
                switch (conn.Speed) {
                case UsbSuperSpeed: result = QStringLiteral("USB3.2"); break;
                case UsbHighSpeed:  result = QStringLiteral("USB2.0"); break;
                case UsbFullSpeed:  result = QStringLiteral("USB1.1"); break;
                case UsbLowSpeed:   result = QStringLiteral("USB1.1"); break;
                default: break;
                }
            }
        }
        CloseHandle(hub);
    }
    SetupDiDestroyDeviceInfoList(hubs);
    return result;
#else
    Q_UNUSED(vid); Q_UNUSED(pid); return {};
#endif
}
}
