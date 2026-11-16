// CONTRXL 25 - native window frame styling
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace cx::ui
{
// Windows 10/11: dark (black) native title bar and border, like other dark apps. No-op elsewhere.
// Call after the window is visible.
void makeTitleBarDark (juce::Component& window);
}
