// CONTRXL 25 - words that differ between Windows and macOS (shown in buttons, tips and the status line).
// String-literal macros so they can be glued into other literals: "Undo (" CX_MOD "+Z)".
#pragma once
#include <juce_core/juce_core.h>

#if JUCE_MAC
 #define CX_MOD        "Cmd"          // the shortcut key (JUCE's "command" modifier)
 #define CX_MOD_UP     "CMD"
 #define CX_COPYMOD    "Option"       // hold while dragging to copy
 #define CX_COPYMOD_UP "OPTION"
 #define CX_REDO       "Cmd+Shift+Z"
 #define CX_TRASH      "Trash"
 #define CX_TRASH_UP   "TRASH"
 #define CX_FILEMGR    "Finder"
 #define CX_COMPUTER   "Mac"
 #define CX_VST3_DIR   "/Library/Audio/Plug-Ins/VST3 and ~/Library/Audio/Plug-Ins/VST3"
 #define CX_PLUGIN_KINDS "Audio Unit and VST3"
#else
 #define CX_MOD        "Ctrl"
 #define CX_MOD_UP     "CTRL"
 #define CX_COPYMOD    "Ctrl"
 #define CX_COPYMOD_UP "CTRL"
 #define CX_REDO       "Ctrl+Shift+Z or Ctrl+Y"
 #define CX_TRASH      "Recycle Bin"
 #define CX_TRASH_UP   "RECYCLE BIN"
 #define CX_FILEMGR    "Explorer"
 #define CX_COMPUTER   "PC"
 #define CX_VST3_DIR   "C:\\Program Files\\Common Files\\VST3"
 #define CX_PLUGIN_KINDS "VST3"
#endif

namespace cx
{
// Per-user data folder: %APPDATA%\CONTRXL25 on Windows, ~/Library/Application Support/CONTRXL25 on a Mac.
inline juce::File appDataDir()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("CONTRXL25");
}
}
