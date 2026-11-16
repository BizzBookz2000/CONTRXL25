// Checks the safe plug-in scanner: plug-ins that crash or hang must not take this process down.
//   ScanTest <folder with .vst3 plug-ins> [timeout seconds]
// Prints one line per plug-in and the skip list, then runs a whole-folder scan the way the
// Plug-ins window does (KnownPluginList + PluginDirectoryScanner with the out-of-process scanner).
#include <juce_audio_processors/juce_audio_processors.h>
#include "app/PluginManager.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: ScanTest <folder> [timeoutSeconds]\n"); return 2; }
    const juce::File folder { juce::String (juce::CharPointer_UTF8 (argv[1])) };
    const int timeoutMs = (argc > 2 ? juce::String (argv[2]).getIntValue() : 10) * 1000;
    std::printf ("helper: %s (%s)\n", cx::PluginScanner::helperExecutable().getFullPathName().toRawUTF8(),
                 cx::PluginScanner::helperAvailable() ? "found" : "MISSING");

    cx::PluginManager pm;
    auto* vst3 = pm.formatNamed ("VST3");
    if (vst3 == nullptr) { std::printf ("no VST3 format\n"); return 1; }

    // 1. one by one (the "Choose & scan" tab)
    const auto ids = vst3->searchPathsForPlugins (juce::FileSearchPath (folder.getFullPathName()), true, true);
    std::printf ("\n1. one at a time (%d found)\n", ids.size());
    for (auto& id : ids)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        const auto t0 = juce::Time::getMillisecondCounter();
        const auto o = pm.scanOne (*vst3, id, found, timeoutMs);
        std::printf ("  %-28s %-60s %5.1f s\n", juce::File (id).getFileName().toRawUTF8(), o.detail.toRawUTF8(),
                     (juce::Time::getMillisecondCounter() - t0) / 1000.0);
    }
    std::printf ("  in list: %d   skip list: %d\n", pm.known.getNumTypes(), pm.known.getBlacklistedFiles().size());

    // 2. whole folder, like JUCE's scan dialog (fresh list, so everything is scanned again)
    cx::PluginScanner::defaultTimeoutMs = timeoutMs;
    cx::PluginManager pm2;
    juce::PluginDirectoryScanner scanner (pm2.known, *pm2.formatNamed ("VST3"), juce::FileSearchPath (folder.getFullPathName()),
                                          true, juce::File(), true);
    juce::String name;
    std::printf ("\n2. whole-folder scan\n");
    while (scanner.scanNextFile (true, name)) std::printf ("  scanned %s\n", name.toRawUTF8());
    std::printf ("  in list: %d\n", pm2.known.getNumTypes());
    for (auto& t : pm2.known.getTypes()) std::printf ("    OK      %s (%s)\n", t.name.toRawUTF8(), t.isInstrument ? "instrument" : "effect");
    for (auto& f : pm2.known.getBlacklistedFiles()) std::printf ("    SKIPPED %s\n", juce::File (f).getFileName().toRawUTF8());
    for (auto& f : scanner.getFailedFiles()) std::printf ("    FAILED  %s\n", juce::File (f).getFileName().toRawUTF8());
    std::printf ("\nScanTest finished - this process survived every plug-in.\n");
    return 0;
}
