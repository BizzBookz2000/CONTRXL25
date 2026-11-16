// CONTRXL 25 - finds and opens VST3 plug-ins
#pragma once
#include "engine/Plugins.h"

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
    bool hasVst3() const { return formats.getNumFormats() > 0; }
};
} // namespace cx
