#include "../app/ToolModules.h"

#include "core/ToolRegistry.h"

namespace gf {

// Root / bootloader / unlock / bypass tool registrations live in their own
// translation units (RootTools.cpp, BootloaderTools.cpp, UnlockTools.cpp,
// BypassTools.cpp) so this file only keeps the AppController entry points that
// do not have a dedicated module.
//
// registerBatchTools(): the batch queue tools (batch.queue / batch.script /
// batch.verify / batch.rename) are owned by the device layer and registered by
// registerDeviceTools(). AppController still calls this hook, so it must stay
// idempotent and never re-register the same tool ids.
void registerBatchTools()
{
    // Nothing to do: see comment above. Kept as a real definition so the app
    // links without the old empty stub in src/app/SystemTools.cpp.
}

} // namespace gf
