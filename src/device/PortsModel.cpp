#include "PortsModel.h"

#include <QCoreApplication>
#include <QMetaObject>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace gf::device {

namespace {

constexpr int kDebounceMs = 400;

} // namespace

PortsMonitor::PortsMonitor(QObject* parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, [this]() {
        if (m_installed)
            emit portsChanged();
    });
}

PortsMonitor::~PortsMonitor()
{
    stop();
}

void PortsMonitor::start()
{
    if (m_installed)
        return;
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installNativeEventFilter(this);
        m_installed = true;
    }
}

void PortsMonitor::stop()
{
    if (!m_installed)
        return;
    if (QCoreApplication* app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
    m_installed = false;
    m_debounce.stop();
}

bool PortsMonitor::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (!m_installed || !message)
        return false;
    const auto* msg = static_cast<MSG*>(message);
    if (msg->message == WM_DEVICECHANGE) {
        // Coalesce bursts of DBT_DEVNODES_CHANGED notifications.
        if (!m_debounce.isActive())
            m_debounce.start();
    }
#else
    Q_UNUSED(message);
#endif
    return false;
}

PortsModel::PortsModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_monitor = new PortsMonitor(this);
    connect(m_monitor, &PortsMonitor::portsChanged, this, &PortsModel::refresh);
    m_monitor->start();
    refresh();
}

PortsModel::~PortsModel()
{
    if (m_monitor)
        m_monitor->stop();
}

int PortsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return int(m_ports.size());
}

QVariant PortsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ports.size())
        return {};
    const PortRecord& port = m_ports.at(index.row());
    switch (role) {
    case PortNameRole: return port.portName();
    case ComPortRole: return port.raw.comPort;
    case DescriptionRole: return port.raw.displayName();
    case KindRole: return portKindId(port.identity.kind);
    case KindLabelRole: return port.identity.label;
    case ChipHintRole: return port.identity.chipHint;
    case ChipLabelRole: return chipHintLabel(port.identity.chipHint);
    case VidPidRole: return formatVidPid(port.raw.vid, port.raw.pid);
    case VidRole: return int(port.raw.vid);
    case PidRole: return int(port.raw.pid);
    case ServiceRole: return port.raw.service;
    case InstanceIdRole: return port.raw.instanceId;
    case IsFlashingRole: return port.identity.isFlashing;
    case IsAdbRole: return port.identity.isAdb;
    case IsFastbootRole: return port.identity.isFastboot;
    case HasProblemRole: return port.raw.hasProblem;
    case ProblemCodeRole: return int(port.raw.problemCode);
    case DataRole: return port.toVariant();
    default: break;
    }
    return {};
}

QHash<int, QByteArray> PortsModel::roleNames() const
{
    return {
        {PortNameRole, "portName"},
        {ComPortRole, "comPort"},
        {DescriptionRole, "description"},
        {KindRole, "kind"},
        {KindLabelRole, "kindLabel"},
        {ChipHintRole, "chipHint"},
        {ChipLabelRole, "chipLabel"},
        {VidPidRole, "vidPid"},
        {VidRole, "vid"},
        {PidRole, "pid"},
        {ServiceRole, "service"},
        {InstanceIdRole, "instanceId"},
        {IsFlashingRole, "isFlashing"},
        {IsAdbRole, "isAdb"},
        {IsFastbootRole, "isFastboot"},
        {HasProblemRole, "hasProblem"},
        {ProblemCodeRole, "problemCode"},
        {DataRole, "data"},
    };
}

QString PortsModel::selectedPort() const
{
    if (m_selected < 0 || m_selected >= m_ports.size())
        return {};
    return m_ports.at(m_selected).portName();
}

void PortsModel::refresh()
{
    const QString previous = m_lockedPort.isEmpty() ? selectedPort() : m_lockedPort;
    const QList<PortRecord> ports = enumeratePorts();
    const bool countChangedNow = ports.size() != m_ports.size();
    beginResetModel();
    m_ports = ports;
    endResetModel();

    int selected = -1;
    if (!previous.isEmpty()) {
        for (int i = 0; i < m_ports.size(); ++i) {
            if (m_ports.at(i).portName().compare(previous, Qt::CaseInsensitive) == 0) {
                selected = i;
                break;
            }
        }
    }
    if (selected != m_selected) {
        m_selected = selected;
        emit selectionChanged();
    }
    if (countChangedNow)
        emit countChanged();
    emit refreshed(int(m_ports.size()));
}

void PortsModel::select(int row)
{
    if (row < 0 || row >= m_ports.size())
        return;
    if (row == m_selected)
        return;
    m_selected = row;
    emit selectionChanged();
}

void PortsModel::selectPort(const QString& portName)
{
    for (int i = 0; i < m_ports.size(); ++i) {
        if (m_ports.at(i).portName().compare(portName, Qt::CaseInsensitive) == 0) {
            select(i);
            return;
        }
    }
    clearSelection();
}

void PortsModel::clearSelection()
{
    if (m_selected < 0)
        return;
    m_selected = -1;
    emit selectionChanged();
}

void PortsModel::lockPort(const QString& portName)
{
    if (m_lockedPort == portName)
        return;
    m_lockedPort = portName;
    emit lockedPortChanged();
    selectPort(portName);
}

void PortsModel::lockRow(int row)
{
    if (row < 0 || row >= m_ports.size())
        return;
    lockPort(m_ports.at(row).portName());
}

void PortsModel::unlock()
{
    if (m_lockedPort.isEmpty())
        return;
    m_lockedPort.clear();
    emit lockedPortChanged();
}

QVariantMap PortsModel::get(int row) const
{
    if (row < 0 || row >= m_ports.size())
        return {};
    return m_ports.at(row).toVariant();
}

QVariantList PortsModel::ports() const
{
    QVariantList list;
    list.reserve(m_ports.size());
    for (const auto& port : m_ports)
        list.append(port.toVariant());
    return list;
}

void PortsModel::setAutoRefresh(bool enabled)
{
    if (m_autoRefresh == enabled)
        return;
    m_autoRefresh = enabled;
    if (m_monitor) {
        if (enabled)
            m_monitor->start();
        else
            m_monitor->stop();
    }
    emit autoRefreshChanged();
}

} // namespace gf::device
