// CONTRXL 25 - safe plug-in scanning.
// Every plug-in is opened by a small helper program ("CONTRXL25_PluginScanner"), one plug-in per run.
// If a plug-in crashes or hangs, only the helper dies: CONTRXL 25 keeps running, the plug-in is
// marked as failed and skipped by later scans (until you choose to retry it).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace cx
{
struct ScanOutcome
{
    enum Status { ok, nothingFound, crashed, timedOut, stopped, noHelper };
    Status status = nothingFound;
    juce::String detail;                         // shown in the scan list
    bool failed() const { return status == crashed || status == timedOut || status == nothingFound; }
};

class PluginScanner
{
public:
    // the helper next to the app (inside CONTRXL 25.app/Contents/MacOS on a Mac)
    static juce::File helperExecutable();
    static bool helperAvailable() { return helperExecutable().existsAsFile(); }

    // Blocking. Safe to call from any thread. shouldStop is polled while waiting.
    static ScanOutcome scanOne (const juce::String& formatName, const juce::String& fileOrIdentifier,
                                juce::OwnedArray<juce::PluginDescription>& results, int timeoutMs,
                                std::function<bool()> shouldStop = {});

    static int defaultTimeoutMs;                 // 60 s: some plug-ins check their licence when they open
};

// Plugged into juce::KnownPluginList, so every scan (including JUCE's own scan dialog) runs in the
// helper. Returning false makes JUCE put the plug-in on the "skip" list (blacklist).
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;
};
} // namespace cx
