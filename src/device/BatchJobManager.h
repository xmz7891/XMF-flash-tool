#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

namespace gf::device {

class BatchJobManager;

/// Opaque per-job runtime shared with the running job. Defined in the .cpp.
struct BatchJobRuntime;

/// Context handed to a batch job. Only one job per device runs at a time, but
/// different devices run in parallel, so every method is thread safe.
class BatchJobContext {
public:
    ~BatchJobContext();

    QString jobId() const;
    QString device() const;
    QString label() const;
    int attempt() const;      // 1 based

    /// 0..100 aggregated into the overall batch progress.
    void setProgress(int percent);
    int progress() const;

    bool isCanceled() const;
    /// Throws BatchJobCanceled when the batch was canceled.
    void checkCanceled() const;
    void log(const QString& message) const;

    class BatchJobCanceled {};

private:
    friend class BatchJobManager;
    BatchJobContext(std::shared_ptr<BatchJobRuntime> runtime, int attempt);

    std::shared_ptr<BatchJobRuntime> d;
    int m_attempt = 1;
};

/// One unit of work. `device` groups jobs: jobs sharing a device are executed
/// serially, different devices can run concurrently.
struct BatchJob {
    QString id;                 // unique-ish id used in logs
    QString device;             // serial / device key, empty = no serialization
    QString label;              // user visible name
    int maxAttempts = 1;        // >= 1, failed jobs are retried
    std::function<bool(BatchJobContext&)> run;   // true = success
};

struct BatchSummary {
    int total = 0;
    int succeeded = 0;
    int failed = 0;
    int canceled = 0;
    int attempts = 0;
    QStringList failedJobs;

    bool ok() const { return failed == 0 && canceled == 0; }
    QString toString() const;
};

/// Device queue with per-device serialization, global parallelism limit,
/// aggregated progress, cancellation and retries. UI-free: the tool handler
/// supplies progress/log callbacks and a TaskContext cancel flag.
class BatchJobManager {
public:
    using ProgressCallback = std::function<void(int percent, const QString& status)>;
    using LogCallback = std::function<void(const QString& message, bool isError)>;

    explicit BatchJobManager(int parallelLimit = 2);
    ~BatchJobManager();
    BatchJobManager(const BatchJobManager&) = delete;
    BatchJobManager& operator=(const BatchJobManager&) = delete;

    void setParallelLimit(int limit);
    int parallelLimit() const;

    void addJob(BatchJob job);
    int jobCount() const;
    QList<BatchJob> jobs() const;
    void clear();

    void setProgressCallback(ProgressCallback callback);
    void setLogCallback(LogCallback callback);

    /// Thread safe; running jobs observe it through BatchJobContext.
    void requestCancel();
    bool isCanceled() const;

    /// Runs all queued jobs and blocks until they finish or are canceled.
    /// Queued jobs are cleared afterwards. Safe to call from a worker thread.
    BatchSummary run();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// ---------------------------------------------------------------------------
// Batch helpers (pure, covered by unit tests)
// ---------------------------------------------------------------------------

struct HashManifestEntry {
    QString hash;
    QString path;
};

/// Parses checksum manifests. Supported line formats:
///   <hash>  <path>            (sha256sum style, also accepts " *path")
///   SHA256 (<path>) = <hash>  (Windows certutil style)
/// Blank lines and # comments are ignored.
QList<HashManifestEntry> parseHashManifest(const QString& text);

/// Expands a batch rename pattern for one file.
/// Tokens: {index}, {index2}, {index3}, {name}, {ext}, {filename}.
/// The original extension is appended when the pattern does not reference it.
/// Characters that are illegal in Windows file names are replaced by '_'.
QString expandRenamePattern(const QString& pattern, int index, const QString& fileName);

/// Computes the hash of a file and compares it with `expectedHash`
/// (case insensitive). algorithm: "sha256" or "md5". Fills actualHash/error.
bool verifyFileHash(const QString& path, const QString& expectedHash,
                    const QString& algorithm, QString* actualHash = nullptr,
                    QString* error = nullptr);

} // namespace gf::device
