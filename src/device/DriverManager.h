#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "DeviceEnumerator.h"

namespace gf::device {

/// Driver families the device layer checks. Mirrors the boot modes supported by
/// the five platform backends plus ADB.
enum class DriverKind {
    QdLoader = 0,   // Qualcomm HS-USB QDLoader 9008
    Mtk,            // MediaTek Preloader / BROM
    Sprd,           // Unisoc / Spreadtrum FDL
    Rockchip,       // Rockchip loader / maskrom
    Allwinner,      // Allwinner FEL
    Adb,            // Android ADB interface / Google USB driver
};

QString driverKindId(DriverKind kind);
QString driverKindName(DriverKind kind);
/// Suggested driver package / INF for installation hints.
QString driverKindHint(DriverKind kind);

struct DriverMatch {
    DriverKind kind;
    QString service;    // bound service on that device node (may be empty)
};

/// Pure classifier: which driver families does this PnP node belong to?
/// Exposed for unit tests; detectDrivers() wraps it with SetupAPI enumeration.
QList<DriverMatch> matchDriverBindings(const RawDevice& device);

enum class DriverState {
    Absent = 0,   // no matching device node at all
    Missing,      // node present but no driver bound / problem code 28
    Bound,        // node present and driver service is loaded
};

QString driverStateId(DriverState state);

struct DriverStatus {
    DriverKind kind = DriverKind::QdLoader;
    DriverState state = DriverState::Absent;
    QStringList devices;       // matching device display names
    QStringList services;
    QStringList hardwareIds;
    QString message;           // Chinese one-line status
    bool requiresAdmin = false;

    bool bound() const { return state == DriverState::Bound; }
    QVariantMap toVariant() const;
};

/// Enumerates PnP devices once and reports the binding state of all six
/// families. Safe to call from a background thread.
QList<DriverStatus> detectDrivers();

QString driverStatusLine(const DriverStatus& status);
QString driverSummary(const QList<DriverStatus>& statuses);

/// <exe>/data/drivers — where the bundled .inf packages live.
QString driversDir();
/// Recursively finds *.inf files under dir (defaults to driversDir()).
QStringList findDriverInfs(const QString& dir = {});

/// Installs one INF with pnputil /add-driver <inf> /install.
/// Requires administrator; on failure returns false and fills `error` with a
/// Chinese explanation (including the pnputil output tail).
bool installDriverInf(const QString& infPath, QString* output = nullptr,
                      QString* error = nullptr);

/// Installs every INF under dir. Returns the number of successful packages.
int installAllDriverInfs(const QString& dir, QStringList* log = nullptr,
                         QString* error = nullptr);

} // namespace gf::device
