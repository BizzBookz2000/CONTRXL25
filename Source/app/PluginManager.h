// CONTRXL 25 - finds and opens plug-ins (VST3 everywhere, plus Audio Units on macOS)
#pragma once
#include "engine/Plugins.h"
#include "app/PluginScanner.h"

namespace cx
{
class PluginManager
{
public:
    PluginManager();
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

    // message thread only
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d, double sampleRate, int block, juce::String& error);
    std::unique_ptr<juce::AudioPluginInstance> createFromXml (const juce::String& xml, double sampleRate, int block, juce::String& error);

    static juce::String toXml (const juce::PluginDescription& d);
    static bool fromXml (const juce::String& xml, juce::PluginDescription& out);

    // plug-ins sorted for menus: instruments or effects
    juce::Array<juce::PluginDescription> list (bool instruments) const;
    bool hasVst3() const { return formats.getNumFormats() > 0; }   // true when any plug-in format is available

    // the format that can open this file (.vst3, or .component on macOS), or nullptr
    juce::AudioPluginFormat* formatForFile (const juce::String& path) const;
    // every format, VST3 first (the scan button scans them all)
    juce::Array<juce::AudioPluginFormat*> allFormats() const;
    juce::AudioPluginFormat* formatNamed (const juce::String& name) const;

    // Scan one plug-in file / identifier in the helper process (never crashes the app).
    // Found plug-ins are added to `known`; a plug-in that fails goes on the skip list.
    ScanOutcome scanOne (juce::AudioPluginFormat& format, const juce::String& fileOrIdentifier,
                         juce::OwnedArray<juce::PluginDescription>& found, int timeoutMs = PluginScanner::defaultTimeoutMs,
                         std::function<bool()> shouldStop = {});
    bool isSkipped (const juce::String& fileOrIdentifier) const { return known.getBlacklistedFiles().contains (fileOrIdentifier); }
};
} // namespace cx
