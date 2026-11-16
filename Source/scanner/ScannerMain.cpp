// CONTRXL25_PluginScanner - helper program started by CONTRXL 25 to open ONE plug-in safely.
//   usage: CONTRXL25_PluginScanner <format name> <file or identifier> <result.xml>
// Writes <LIST> with one <PLUGIN> per plug-in found and exits with code 0.
// If the plug-in crashes or hangs, only this process dies and CONTRXL 25 marks the plug-in as failed.
#include <juce_audio_processors/juce_audio_processors.h>
#include <thread>
#include <atomic>

#if JUCE_MAC
namespace juce { void initialiseNSApplication(); }   // in juce_events (juce_MessageManager_mac.mm)
#endif

static void writeResult (const juce::File& f, const juce::OwnedArray<juce::PluginDescription>& found, const juce::String& error)
{
    juce::XmlElement xml ("LIST");
    if (error.isNotEmpty()) xml.setAttribute ("error", error);
    for (auto* d : found)
        if (auto e = d->createXml()) xml.addChildElement (e.release());
    xml.writeTo (f);
}

int main (int argc, char* argv[])
{
    if (argc < 4) { std::fprintf (stderr, "usage: scanner <format> <fileOrIdentifier> <result.xml>\n"); return 2; }
    const juce::String formatName = juce::CharPointer_UTF8 (argv[1]);
    const juce::String id         = juce::CharPointer_UTF8 (argv[2]);
    const juce::File resultFile { juce::String (juce::CharPointer_UTF8 (argv[3])) };

    juce::ScopedJuceInitialiser_GUI juceInit;      // message thread (Audio Units need one)
   #if JUCE_MAC
    // A console program has no NSApplication until one is made. Some plug-ins expect it to exist,
    // and Audio Unit v3 needs it for the message loop below.
    juce::initialiseNSApplication();
    juce::Process::setDockIconVisible (false);     // no Dock icon while scanning
   #endif

    juce::AudioPluginFormatManager formats;
   #if JUCE_PLUGINHOST_VST3
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #endif
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif

    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == formatName) format = f;
    if (format == nullptr) { writeResult (resultFile, {}, "unknown plug-in format " + formatName); return 0; }

    juce::OwnedArray<juce::PluginDescription> found;
    juce::PluginDescription probe;
    probe.fileOrIdentifier = id;
    auto* mm = juce::MessageManager::getInstance();

    if (! format->requiresUnblockedMessageThreadDuringCreation (probe))
    {
        // VST3 and classic Audio Units: open it right here. main() IS the message thread, so no
        // message loop is needed (on macOS a console program has no running loop to rely on).
        format->findAllTypesForFile (found, id);
    }
    else
    {
        // AUv3 and similar: they must be opened off the message thread while the message loop runs.
        std::atomic<bool> done { false };
        std::thread worker ([&]
        {
            format->findAllTypesForFile (found, id);
            done = true;
            mm->callAsync ([mm] { mm->stopDispatchLoop(); });
        });
        mm->runDispatchLoop();
        worker.join();
        if (! done) { writeResult (resultFile, {}, "the message loop stopped early"); std::_Exit (0); }
    }

    writeResult (resultFile, found, found.isEmpty() ? juce::String ("no usable plug-in inside") : juce::String());
    std::fflush (nullptr);
    std::_Exit (0);   // skip plug-in destructors that might misbehave at shutdown; the result is already on disk
}
