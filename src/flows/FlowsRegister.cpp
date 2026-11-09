#include "../app/ToolModules.h"

#include "FlowTools.h"
#include "MiFlashFlow.h"

namespace gf {

// Cross-platform flashing pipelines (backup / restore / read / write / erase).
void registerFlowTools()
{
    registerFlowToolHandlers();
    registerMiFlashFlowTools();
}

} // namespace gf
