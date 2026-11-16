// CONTRXL 25 - what differs between the Windows/desktop and the iPad (iOS) builds
#pragma once
#include <juce_core/juce_core.h>

// CX_MOBILE: touch screen, sandboxed files, AUv3 plug-ins (iPad). CONTRXL_FORCE_MOBILE lets the desktop
// build run the same code paths for testing.
#if JUCE_IOS || JUCE_ANDROID || defined (CONTRXL_FORCE_MOBILE)
 #define CX_MOBILE 1
#else
 #define CX_MOBILE 0
#endif

namespace cx
{
// the plug-in standard this build hosts, for messages and buttons
inline juce::String pluginKind()
{
   #if JUCE_IOS
    return "AUv3";
   #elif JUCE_PLUGINHOST_AU && JUCE_MAC
    return "AU / VST3";
   #else
    return "VST3";
   #endif
}

// where an iPad keeps everything the user should see in the Files app (On My iPad > CONTRXL 25)
inline juce::File userFilesFolder()
{
   #if CX_MOBILE
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
   #else
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("CONTRXL 25");
   #endif
}
} // namespace cx
