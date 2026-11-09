#pragma once

// Bridge between gf::proto::Reporter and gf::TaskContext. Only tool handlers
// (which already depend on gf_core) use this header; the protocol classes stay
// independent from the task engine and therefore unit-testable.

#include <functional>

#include "ProtocolSupport.h"
#include "TaskManager.h"

namespace gf::proto {

/// Returns a Reporter that forwards progress/logs/cancel to the task context.
Reporter reporterFor(TaskContext& ctx);

/// Executes `body`, translating protocol cancellation into the exception the
/// TaskManager expects. All other std::exceptions propagate unchanged.
void runGuarded(TaskContext& ctx, const std::function<void()>& body);

} // namespace gf::proto
