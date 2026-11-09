#pragma once

#include <QWindow>

namespace gf::native {

/// Applies the Windows 11 "acrylic / mica" backdrop, title bar colour and
/// rounded corners to a top-level window. `dark` selects the dark or light
/// system appearance. Returns false when no glass effect could be applied.
bool applyGlass(QWindow* window, bool dark = true);

/// Toggles the dark/light title bar & system border.
void setDarkMode(QWindow* window, bool dark);

} // namespace gf::native
