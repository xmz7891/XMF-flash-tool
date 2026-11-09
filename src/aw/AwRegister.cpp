#include "../app/ToolModules.h"

#include "AwTools.h"

namespace gf {

void registerAwTools()
{
    registerAwToolHandlers();
    registerAwFirmwareTools();
}

} // namespace gf
