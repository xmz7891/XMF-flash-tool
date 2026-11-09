#pragma once

namespace gf {

/// Implemented in AwTools.cpp; called by AwRegister.cpp's entry point.
void registerAwToolHandlers();

/// IMAGEWTY (PhoenixSuit) firmware parsing and unpacking tools.
void registerAwFirmwareTools();

} // namespace gf
