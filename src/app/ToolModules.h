#pragma once

namespace gf {

// Every backend module registers its tools through one of these entry points.
// They are implemented in the matching src/<module> directory and called once
// from AppController::initialize().

void registerSystemTools();
void registerImageTools();
void registerPacketTools();
void registerDeviceTools();
void registerQcomTools();
void registerMtkTools();
void registerSprdTools();
void registerRkTools();
void registerAwTools();
void registerFlowTools();
void registerRootTools();
void registerBootloaderTools();
void registerUnlockTools();
void registerFirmwareTools();
void registerBatchTools();
void registerBypassTools();

} // namespace gf
