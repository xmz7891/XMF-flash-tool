#include "DeviceEnumerator.h"

#include <QRegularExpression>

#include <algorithm>
#include <iterator>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <cfgmgr32.h>
#  include <setupapi.h>
// The headers above require setupapi / cfgmgr32; they are linked via CMake.
#endif

namespace gf::device {

QString RawDevice::primaryHardwareId() const
{
    if (!hardwareIds.isEmpty())
        return hardwareIds.first();
    return instanceId;
}

bool RawDevice::isUsb() const
{
    if (instanceId.startsWith(QStringLiteral("USB\\"), Qt::CaseInsensitive))
        return true;
    for (const QString& id : hardwareIds) {
        if (id.startsWith(QStringLiteral("USB\\"), Qt::CaseInsensitive))
            return true;
    }
    for (const QString& id : compatibleIds) {
        if (id.startsWith(QStringLiteral("USB\\"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QString RawDevice::displayName() const
{
    if (!friendlyName.isEmpty())
        return friendlyName;
    if (!description.isEmpty())
        return description;
    return instanceId;
}

QString RawDevice::searchableIds() const
{
    QStringList all;
    all.reserve(hardwareIds.size() + compatibleIds.size() + 1);
    all.append(hardwareIds);
    all.append(compatibleIds);
    return all.join(QLatin1Char('|')).toLower();
}

bool parseVidPid(const QString& hardwareId, quint16* vid, quint16* pid)
{
    static const QRegularExpression re(
        QStringLiteral("VID_([0-9A-Fa-f]{4}).{0,64}?PID_([0-9A-Fa-f]{4})"));
    const QRegularExpressionMatch match = re.match(hardwareId);
    if (!match.hasMatch())
        return false;
    bool okVid = false;
    bool okPid = false;
    const uint v = match.captured(1).toUInt(&okVid, 16);
    const uint p = match.captured(2).toUInt(&okPid, 16);
    if (!okVid || !okPid)
        return false;
    if (vid)
        *vid = quint16(v);
    if (pid)
        *pid = quint16(p);
    return true;
}

int parseComPort(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("(?i)\\bCOM(\\d{1,3})\\b"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return 0;
    bool ok = false;
    const int value = match.captured(1).toInt(&ok);
    return ok ? value : 0;
}

QString formatVidPid(quint16 vid, quint16 pid)
{
    if (vid == 0 && pid == 0)
        return {};
    return QStringLiteral("VID_%1&PID_%2")
        .arg(vid, 4, 16, QLatin1Char('0'))
        .arg(pid, 4, 16, QLatin1Char('0'))
        .toUpper();
}

#ifdef Q_OS_WIN

namespace {

QString propertyString(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property)
{
    DWORD type = 0;
    DWORD size = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type, nullptr, 0, &size);
    if (size == 0)
        return {};
    QByteArray buffer(int(size) + sizeof(wchar_t), '\0');
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type,
                                           reinterpret_cast<PBYTE>(buffer.data()), size, nullptr))
        return {};
    if (type == REG_SZ || type == REG_EXPAND_SZ)
        return QString::fromWCharArray(reinterpret_cast<const wchar_t*>(buffer.constData()));
    return {};
}

QStringList propertyMultiString(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property)
{
    DWORD type = 0;
    DWORD size = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type, nullptr, 0, &size);
    if (size == 0)
        return {};
    QByteArray buffer(int(size) + sizeof(wchar_t) * 2, '\0');
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type,
                                           reinterpret_cast<PBYTE>(buffer.data()), size, nullptr))
        return {};
    QStringList result;
    const auto* chars = reinterpret_cast<const wchar_t*>(buffer.constData());
    QString current;
    for (const wchar_t* p = chars; *p; ++p) {
        if (*p == L'\0') {
            if (!current.isEmpty())
                result.append(current);
            current.clear();
        } else {
            current.append(QChar(*p));
        }
    }
    if (!current.isEmpty())
        result.append(current);
    return result;
}

quint32 propertyDword(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property, bool* ok = nullptr)
{
    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    const BOOL result = SetupDiGetDeviceRegistryPropertyW(
        set, &data, property, &type, reinterpret_cast<PBYTE>(&value), size, nullptr);
    if (ok)
        *ok = result && type == REG_DWORD;
    if (result && type == REG_DWORD)
        return value;
    // Some classes store the address as a string.
    const QString text = propertyString(set, data, property);
    if (!text.isEmpty()) {
        bool parsed = false;
        const uint number = text.toUInt(&parsed, 0);
        if (parsed) {
            if (ok)
                *ok = true;
            return number;
        }
    }
    if (ok)
        *ok = false;
    return 0;
}

int readComPortFromRegistry(HDEVINFO set, SP_DEVINFO_DATA& data)
{
    HKEY key = SetupDiOpenDevRegKey(set, &data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE)
        return 0;
    wchar_t buffer[128] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG result = RegQueryValueExW(key, L"PortName", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        return 0;
    return parseComPort(QString::fromWCharArray(buffer));
}

void queryDeviceStatus(DEVINST instance, bool* hasProblem, quint32* problemCode)
{
    if (hasProblem)
        *hasProblem = false;
    if (problemCode)
        *problemCode = 0;
    ULONG status = 0;
    ULONG problem = 0;
    if (CM_Get_DevNode_Status(&status, &problem, instance, 0) == CR_SUCCESS) {
        if ((status & DN_HAS_PROBLEM) != 0) {
            if (hasProblem)
                *hasProblem = true;
            if (problemCode)
                *problemCode = problem;
        }
    }
}

} // namespace

QList<RawDevice> enumeratePresentDevices()
{
    QList<RawDevice> devices;

    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE)
        return devices;

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set, index, &data); ++index) {
        RawDevice device;
        wchar_t instanceId[1024] = {};
        if (SetupDiGetDeviceInstanceIdW(set, &data, instanceId,
                                        DWORD(std::size(instanceId)), nullptr))
            device.instanceId = QString::fromWCharArray(instanceId);

        device.description = propertyString(set, data, SPDRP_DEVICEDESC);
        device.friendlyName = propertyString(set, data, SPDRP_FRIENDLYNAME);
        device.hardwareIds = propertyMultiString(set, data, SPDRP_HARDWAREID);
        device.compatibleIds = propertyMultiString(set, data, SPDRP_COMPATIBLEIDS);
        device.service = propertyString(set, data, SPDRP_SERVICE);
        device.className = propertyString(set, data, SPDRP_CLASS);
        device.classGuid = propertyString(set, data, SPDRP_CLASSGUID);
        device.driverKey = propertyString(set, data, SPDRP_DRIVER);
        device.manufacturer = propertyString(set, data, SPDRP_MFG);
        device.locationInfo = propertyString(set, data, SPDRP_LOCATION_INFORMATION);
        device.busNumber = propertyDword(set, data, SPDRP_BUSNUMBER);
        device.address = propertyDword(set, data, SPDRP_ADDRESS);
        device.comPort = readComPortFromRegistry(set, data);
        queryDeviceStatus(data.DevInst, &device.hasProblem, &device.problemCode);

        const QString id = device.primaryHardwareId();
        if (parseVidPid(id, &device.vid, &device.pid) == false && !device.friendlyName.isEmpty())
            parseVidPid(device.friendlyName, &device.vid, &device.pid);

        if (device.instanceId.isEmpty() && device.description.isEmpty())
            continue;
        devices.append(device);
    }
    SetupDiDestroyDeviceInfoList(set);
    return devices;
}

#else // !Q_OS_WIN

QList<RawDevice> enumeratePresentDevices()
{
    return {};
}

#endif

} // namespace gf::device
