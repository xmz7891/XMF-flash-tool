#include "NativeWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>

#if !defined(DWMWA_USE_IMMERSIVE_DARK_MODE)
#  define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#if !defined(DWMWA_WINDOW_CORNER_PREFERENCE)
#  define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#if !defined(DWMWA_SYSTEMBACKDROP_TYPE)
#  define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWCP_ROUND
#  define DWMWCP_ROUND 2
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#  define DWMSBT_TRANSIENTWINDOW 3
#endif
#ifndef DWMSBT_MAINWINDOW
#  define DWMSBT_MAINWINDOW 2
#endif

namespace {
struct AccentPolicy {
    int accentState;
    int accentFlags;
    int gradientColor;
    int animationId;
};
struct WinCompositionAttrData {
    int attribute;
    PVOID data;
    ULONG sizeOfData;
};
using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WinCompositionAttrData*);

HWND handleOf(QWindow* window)
{
    if (!window)
        return nullptr;
    return reinterpret_cast<HWND>(window->winId());
}
} // namespace

namespace gf::native {

void setDarkMode(QWindow* window, bool dark)
{
    HWND hwnd = handleOf(window);
    if (!hwnd)
        return;
    BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
    DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value)); // older builds
}

bool applyGlass(QWindow* window, bool dark)
{
    HWND hwnd = handleOf(window);
    if (!hwnd)
        return false;

    setDarkMode(window, dark);

    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    // Windows 11 22H2+: transient (acrylic) backdrop.
    int backdrop = DWMSBT_TRANSIENTWINDOW;
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    if (SUCCEEDED(hr))
        return true;

    // Windows 10 fallback: acrylic blur behind.
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        auto setAttr = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));
        if (setAttr) {
            AccentPolicy policy{4 /*ACCENT_ENABLE_ACRYLICBLURBEHIND*/, 2,
                                int(dark ? 0xCC14161C : 0xCCF2F2F4), 0};
            WinCompositionAttrData data{19 /*WCA_ACCENT_POLICY*/, &policy, sizeof(policy)};
            if (setAttr(hwnd, &data)) {
                FreeLibrary(user32);
                return true;
            }
        }
        FreeLibrary(user32);
    }
    return false;
}

} // namespace gf::native

#else

namespace gf::native {
bool applyGlass(QWindow*, bool) { return false; }
void setDarkMode(QWindow*, bool) {}
} // namespace gf::native

#endif
