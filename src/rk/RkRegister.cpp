#include "../app/ToolModules.h"

#include "RkTools.h"

namespace gf {

void registerRkTools()
{
    registerRkToolHandlers();
    registerRkFirmwareTools();
}

} // namespace gf
