// CONTRXL 25 - finds and opens plug-ins: VST3 on Windows, AUv3 on the iPad
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

    // iPad AUv3 plug-ins can only be opened asynchronously (the message thread must keep running).
    // needsAsync() says so; createAsync() opens one and calls back on the message thread.
    using Ready = std::function<void (std::unique_ptr<juce::AudioPluginInstance>, const juce::String& error)>;
    bool needsAsync (const juce::PluginDescription& d) const;
    bool needsAsyncXml (const juce::String& xml) const;
    void createAsync (const juce::PluginDescription& d, double sampleRate, int block, Ready done);
    void createAsyncXml (const juce::String& xml, double sampleRate, int block, Ready done);
    juce::PluginDescription resolve (const juce::String& xml) const;   // the saved description, updated if the plug-in moved

    static juce::String toXml (const juce::PluginDescription& d);
    static bool fromXml (const juce::String& xml, juce::PluginDescription& out);

    // plug-ins sorted for menus: instruments or effects
    juce::Array<juce::PluginDescription> list (bool instruments) const;
    bool hasVst3() const { return formats.getNumFormats() > 0; }
};
} // namespace cx
