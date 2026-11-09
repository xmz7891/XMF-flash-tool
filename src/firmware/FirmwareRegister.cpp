#include "../app/ToolModules.h"

#include "FirmwareTools.h"

namespace gf {

// Firmware resource tools (vendor search, download links, local library).
// 真实工具在 FirmwareTools.cpp 中实现，这里只负责挂载。
void registerFirmwareTools()
{
    registerFirmwareToolHandlers();
}

} // namespace gf
