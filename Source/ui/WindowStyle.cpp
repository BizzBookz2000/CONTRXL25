#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <dwmapi.h>
 #pragma comment (lib, "dwmapi.lib")
#endif
#include "WindowStyle.h"

namespace cx::ui
{
void makeTitleBarDark (juce::Component& window)
{
   #if defined (_WIN32)
    auto* peer = window.getPeer();
    if (peer == nullptr) return;
    auto hwnd = (HWND) peer->getNativeHandle();
    if (hwnd == nullptr) return;
    BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (19 on older Windows 10 builds)
    if (FAILED (DwmSetWindowAttribute (hwnd, 20, &dark, sizeof (dark))))
        DwmSetWindowAttribute (hwnd, 19, &dark, sizeof (dark));
    // Windows 11: pure black caption and border, light caption text (ignored on Windows 10)
    COLORREF black = RGB (0, 0, 0), text = RGB (232, 232, 238);
    DwmSetWindowAttribute (hwnd, 35 /* DWMWA_CAPTION_COLOR */, &black, sizeof (black));
    DwmSetWindowAttribute (hwnd, 34 /* DWMWA_BORDER_COLOR */,  &black, sizeof (black));
    DwmSetWindowAttribute (hwnd, 36 /* DWMWA_TEXT_COLOR */,    &text,  sizeof (text));
   #else
    juce::ignoreUnused (window);
   #endif
}
}
