#pragma once

#include <QAbstractListModel>
#include <QAbstractNativeEventFilter>
#include <QTimer>

#include "Ports.h"

namespace gf::device {

/// Watches the native WM_DEVICECHANGE message and emits portsChanged() after a
/// short debounce, so USB plug/unplug events refresh the port list without
/// polling. Must be created on the GUI thread.
class PortsMonitor : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit PortsMonitor(QObject* parent = nullptr);
    ~PortsMonitor() override;

    void start();
    void stop();
    bool isActive() const { return m_installed; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void portsChanged();

private:
    QTimer m_debounce;
    bool m_installed = false;
};

/// QML friendly list model of detected ports (COM / USB / boot modes).
/// Data is refreshed synchronously from SetupAPI (~a few ms) and automatically
/// on device change when autoRefresh is enabled. Roles are also exported as a
/// QVariantMap through get()/ports() for simple property bindings.
class PortsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString selectedPort READ selectedPort NOTIFY selectionChanged)
    Q_PROPERTY(QString lockedPort READ lockedPort NOTIFY lockedPortChanged)
    Q_PROPERTY(bool autoRefresh READ autoRefresh WRITE setAutoRefresh NOTIFY autoRefreshChanged)

public:
    enum Roles {
        PortNameRole = Qt::UserRole + 1,
        ComPortRole,
        DescriptionRole,
        KindRole,
        KindLabelRole,
        ChipHintRole,
        ChipLabelRole,
        VidPidRole,
        VidRole,
        PidRole,
        ServiceRole,
        InstanceIdRole,
        IsFlashingRole,
        IsAdbRole,
        IsFastbootRole,
        HasProblemRole,
        ProblemCodeRole,
        DataRole,
    };

    explicit PortsModel(QObject* parent = nullptr);
    ~PortsModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_ports.size()); }
    QString selectedPort() const;
    QString lockedPort() const { return m_lockedPort; }
    bool autoRefresh() const { return m_autoRefresh; }
    void setAutoRefresh(bool enabled);

public slots:
    /// Re-enumerates all ports. Safe to call from QML / GUI thread.
    void refresh();
    void select(int row);
    void selectPort(const QString& portName);
    void clearSelection();
    /// Pins a port by name; refresh() keeps selecting it while it is present.
    void lockPort(const QString& portName);
    void lockRow(int row);
    void unlock();
    QVariantMap get(int row) const;
    QVariantList ports() const;

signals:
    void countChanged();
    void selectionChanged();
    void lockedPortChanged();
    void autoRefreshChanged();
    void refreshed(int count);

private:
    QList<PortRecord> m_ports;
    int m_selected = -1;
    QString m_lockedPort;
    bool m_autoRefresh = true;
    PortsMonitor* m_monitor = nullptr;
};

} // namespace gf::device
