#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace gf::device {

/// One line of `fastboot devices`.
struct FastbootDevice {
    QString serial;
    QString mode;      // "fastboot" or "fastbootd"

    bool isFastbootD() const { return mode == QLatin1String("fastbootd"); }
    QString displayName() const;
    QVariantMap toVariant() const;
};

/// Parses `fastboot devices` output; tolerates \r\n, empty lines and the
/// "no permissions" diagnostics printed by some Windows drivers.
QList<FastbootDevice> parseFastbootDevices(const QString& output);

/// Parses `fastboot getvar all` output. fastboot prints variables to stderr
/// with a "(bootloader) " prefix; both prefixed and plain "key: value" lines
/// are accepted (fastbootd omits the prefix).
QMap<QString, QString> parseGetvarOutput(const QString& output);

/// One partition described by `getvar all`.
struct FastbootPartition {
    QString name;
    QString size;       // hex string, e.g. "0x1000000"
    QString type;       // file system / "raw"
    QString slot;       // "yes" when has-slot:<name> is yes
    bool logical = false;

    QVariantMap toVariant() const;
};

/// Builds the partition table from parsed getvar variables.
QList<FastbootPartition> parsePartitionTable(const QMap<QString, QString>& vars);

/// Result of one fastboot invocation.
struct FastbootResult {
    bool ok = false;
    int exitCode = -1;
    QString output;
    QString error;

    bool looksLikeMissingDevice() const;
};

class FastbootClient {
public:
    static bool available();
    static QString fastbootPath();

    static FastbootResult run(const QStringList& arguments, int timeoutMs = 120000);
    static FastbootResult runFor(const QString& serial, const QStringList& arguments,
                                 int timeoutMs = 120000);

    static QList<FastbootDevice> devices(QString* error = nullptr);
    static QString resolveSerial(const QString& requested, QString* error = nullptr);
    static QString summarizeDevices(const QList<FastbootDevice>& devices);

    static FastbootResult getvar(const QString& serial, const QString& key);
    static QMap<QString, QString> getvarAll(const QString& serial, QString* error = nullptr);
    static QList<FastbootPartition> partitions(const QString& serial, QString* error = nullptr);

    /// Resolves a partition's size (bytes) via getvar partition-size:<name>.
    static bool partitionSize(const QString& serial, const QString& name, quint64* size,
                              QString* error = nullptr);

    static FastbootResult flash(const QString& serial, const QString& partition,
                                const QString& image, int timeoutMs = 600000);
    static FastbootResult erase(const QString& serial, const QString& partition);
    static FastbootResult format(const QString& serial, const QString& partition,
                                 const QString& filesystem = QStringLiteral("ext4"));
    static FastbootResult reboot(const QString& serial, const QString& mode);
    static FastbootResult rebootBootloader(const QString& serial);
    static FastbootResult flashingLock(const QString& serial, bool lock);
    static FastbootResult oemCommand(const QString& serial, const QString& command);

    /// Returns a human readable problem description when the battery state makes
    /// flashing/rebooting unsafe, or an empty string when it is fine/unknown.
    static QString batteryIssue(const QString& serial);
    /// Pure variant used by unit tests and by batteryIssue().
    static QString batteryIssueFromVars(const QMap<QString, QString>& vars);
    /// Extracts a partition size from parsed getvar variables (0 when missing).
    static quint64 partitionSizeFromVars(const QMap<QString, QString>& vars, const QString& name);
    /// Normalizes UI display labels ("system — 重启到系统") to fastboot args.
    static QString normalizeRebootMode(const QString& mode);

    /// Dumps getvar all + parsed partitions into a text file. Returns false and
    /// fills `error` on failure.
    static bool exportInfo(const QString& serial, const QString& outDir,
                           QString* savedPath = nullptr, QString* error = nullptr);
};

} // namespace gf::device
