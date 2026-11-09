#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace gf::device {

/// One line of `adb devices -l`.
struct AdbDevice {
    QString serial;
    QString state;        // "device", "offline", "unauthorized", "no", ...
    QString product;
    QString model;
    QString device;
    QString transportId;
    QString usb;

    bool isOnline() const { return state == QLatin1String("device"); }
    bool isUnauthorized() const { return state == QLatin1String("unauthorized"); }
    /// Human readable name: "Pixel 7 (adb-1234)" or just the serial.
    QString displayName() const;
    QVariantMap toVariant() const;
};

/// Parses `adb devices -l` output. Ignores the daemon banners, the
/// "List of devices attached" header and empty lines, and keeps the raw state
/// (including "unauthorized" / "no permissions") so callers can explain it.
QList<AdbDevice> parseAdbDevices(const QString& output);

/// Parses `adb shell getprop` output of the form "[ro.product.model]: [Pixel]".
QMap<QString, QString> parseGetpropOutput(const QString& output);

/// One entry of `ls -l /dev/block/by-name`.
struct PartitionLink {
    QString name;
    QString target;      // /dev/block/sde11 (empty when unknown)
    QString detail;      // raw ls -l detail text
};

/// Parses symlink listings ("lrwxrwxrwx ... boot -> /dev/block/sde11") as well
/// as the plain `ls /dev/block/by-name` form (one name per line).
QList<PartitionLink> parseByNameLinks(const QString& output);

/// Result of one adb invocation.
struct AdbResult {
    bool ok = false;
    int exitCode = -1;
    QString output;
    QString error;

    /// True when it failed only because the device is gone.
    bool looksLikeMissingDevice() const;
};

class AdbClient {
public:
    static bool available();
    static QString adbPath();

    /// Runs adb with arbitrary arguments. Never throws.
    static AdbResult run(const QStringList& arguments, int timeoutMs = 120000);
    static AdbResult runFor(const QString& serial, const QStringList& arguments,
                            int timeoutMs = 120000);

    static QList<AdbDevice> devices(QString* error = nullptr);
    static QList<AdbDevice> onlineDevices(QString* error = nullptr);
    static QStringList deviceSerials();

    static AdbResult startServer();
    static AdbResult killServer();

    static AdbResult shell(const QString& serial, const QString& command, int timeoutMs = 120000);
    static AdbResult execOut(const QString& serial, const QString& command, int timeoutMs = 120000);

    static AdbResult push(const QString& serial, const QString& local, const QString& remote,
                          int timeoutMs = 300000);
    /// Installs an OTA/zip package through recovery sideload (device must be in
    /// "sideload" state). The timeout is generous because sideload streams the zip.
    static AdbResult sideload(const QString& serial, const QString& package,
                              int timeoutMs = 900000);
    /// Waits until adb sees the device again (e.g. after rebooting into recovery).
    static AdbResult waitForDevice(const QString& serial, int timeoutMs = 120000);
    static AdbResult pull(const QString& serial, const QString& remote, const QString& local,
                          int timeoutMs = 300000);

    /// mode: "system" (empty arg), "recovery", "bootloader"/"fastboot",
    /// "fastbootd", "edl", "brom". Unknown modes are passed through verbatim.
    static AdbResult reboot(const QString& serial, const QString& mode);
    static QString rebootArgument(const QString& mode);

    static AdbResult enableTcpip(const QString& serial, int port = 5555);
    static AdbResult connectNetwork(const QString& address, int port = 5555);
    static AdbResult disconnectNetwork(const QString& address, int port = 5555);
    static AdbResult forward(const QString& serial, const QString& local, int remotePort);
    static AdbResult forwardRemove(const QString& serial, const QString& local);
    static AdbResult forwardList(const QString& serial);

    static AdbResult getProp(const QString& serial, const QString& key);
    static QMap<QString, QString> allProps(const QString& serial, QString* error = nullptr);
    static QList<PartitionLink> partitions(const QString& serial, QString* error = nullptr);

    static AdbResult logcat(const QString& serial, int lines = 500, int timeoutMs = 120000);
    static AdbResult dmesg(const QString& serial);
    static AdbResult bugreport(const QString& serial, const QString& outputPath);

    /// Returns `requested` when non-empty, otherwise the only online device.
    /// Fills `error` with a Chinese message when no / several devices exist.
    static QString resolveSerial(const QString& requested, QString* error = nullptr);

    static QString summarizeDevices(const QList<AdbDevice>& devices);

    /// Subset of getprop values shown by the "device info" tool.
    struct DeviceInfo {
        QString serial;
        QString model;
        QString brand;
        QString device;
        QString manufacturer;
        QString androidVersion;
        QString sdk;
        QString fingerprint;
        QString abi;
        QString bootloader;
        QString baseband;

        QStringList toLines() const;
        QVariantMap toVariant() const;
    };

    static DeviceInfo deviceInfo(const QString& serial, QString* error = nullptr);
    static bool exportDeviceInfo(const QString& serial, const QString& outDir,
                                 QString* savedPath = nullptr, QString* error = nullptr);
};

} // namespace gf::device
