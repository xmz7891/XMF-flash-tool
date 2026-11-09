#include "TaskReporter.h"

namespace gf::proto {

Reporter reporterFor(TaskContext& ctx)
{
    return Reporter(
        [&ctx](int percent, const QString& status) {
            if (percent >= 0)
                ctx.setProgress(percent);
            if (!status.isEmpty())
                ctx.setStatus(status);
        },
        [&ctx](Reporter::Level level, const QString& message) {
            switch (level) {
            case Reporter::Level::Debug:
                ctx.debug(message);
                break;
            case Reporter::Level::Info:
                ctx.info(message);
                break;
            case Reporter::Level::Warn:
                ctx.warn(message);
                break;
            case Reporter::Level::Error:
                ctx.error(message);
                break;
            }
        },
        [&ctx] { return ctx.isCanceled(); });
}

void runGuarded(TaskContext& ctx, const std::function<void()>& body)
{
    try {
        body();
    } catch (const Canceled&) {
        throw TaskContext::TaskCanceled{};
    }
}

} // namespace gf::proto
