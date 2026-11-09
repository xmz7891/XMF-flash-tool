#pragma once

namespace gf {

/// Implemented in RkTools.cpp; called by RkRegister.cpp's entry point.
void registerRkToolHandlers();

/// RKFW / RKAF firmware parsing and unpacking tools.
void registerRkFirmwareTools();

} // namespace gf
