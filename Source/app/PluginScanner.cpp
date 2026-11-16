#include "PluginScanner.h"
#include "Platform.h"

namespace cx
{
int PluginScanner::defaultTimeoutMs = 60000;

juce::File PluginScanner::helperExecutable()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_WINDOWS
    return exe.getSiblingFile ("CONTRXL25_PluginScanner.exe");
   #else
    return exe.getSiblingFile ("CONTRXL25_PluginScanner");   // macOS: CONTRXL 25.app/Contents/MacOS/
   #endif
}

ScanOutcome PluginScanner::scanOne (const juce::String& formatName, const juce::String& id,
                                    juce::OwnedArray<juce::PluginDescription>& results, int timeoutMs,
                                    std::function<bool()> shouldStop)
{
    ScanOutcome out;
    const auto helper = helperExecutable();
    if (! helper.existsAsFile()) { out.status = ScanOutcome::noHelper; out.detail = "Scanner helper is missing - rebuild the app"; return out; }

    auto dir = appDataDir().getChildFile ("scan");
    dir.createDirectory();
    const auto resultFile = dir.getNonexistentChildFile ("result", ".xml", false);

    juce::ChildProcess p;
    // no stdout/stderr pipes: chatty plug-ins can't block the helper, and nothing needs reading
    if (! p.start (juce::StringArray { helper.getFullPathName(), formatName, id, resultFile.getFullPathName() }, 0))
    {
        out.status = ScanOutcome::noHelper; out.detail = "Could not start the scanner helper"; return out;
    }
    const auto t0 = juce::Time::getMillisecondCounter();
    while (p.isRunning())
    {
        if (shouldStop && shouldStop())          { p.kill(); out.status = ScanOutcome::stopped; out.detail = "Stopped"; resultFile.deleteFile(); return out; }
        if ((int) (juce::Time::getMillisecondCounter() - t0) > timeoutMs)
        {
            p.kill();
            out.status = ScanOutcome::timedOut;
            out.detail = "Failed - did not answer within " + juce::String (timeoutMs / 1000) + " s";
            resultFile.deleteFile();
            return out;
        }
        juce::Thread::sleep (30);
    }
    const auto code = p.getExitCode();
    std::unique_ptr<juce::XmlElement> xml;
    if (resultFile.existsAsFile()) xml = juce::parseXML (resultFile);
    resultFile.deleteFile();
    if (xml == nullptr || code != 0)
    {
        out.status = ScanOutcome::crashed;
        out.detail = "Failed - the plug-in crashed while opening";
        return out;
    }
    for (auto* e : xml->getChildIterator())
    {
        auto d = std::make_unique<juce::PluginDescription>();
        if (d->loadFromXml (*e)) results.add (std::move (d));
    }
    if (results.isEmpty())
    {
        out.status = ScanOutcome::nothingFound;
        out.detail = xml->getStringAttribute ("error").isNotEmpty() ? "Failed - " + xml->getStringAttribute ("error")
                                                                     : "Failed - no usable plug-in inside";
        return out;
    }
    out.status = ScanOutcome::ok;
    out.detail = "OK - " + results[0]->name + (results.size() > 1 ? " (+" + juce::String (results.size() - 1) + " more)" : juce::String())
               + (results[0]->isInstrument ? "  [instrument]" : "  [effect]");
    return out;
}

bool OutOfProcessScanner::findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                              const juce::String& fileOrIdentifier)
{
    if (! PluginScanner::helperAvailable())
    {
        format.findAllTypesForFile (result, fileOrIdentifier);   // fallback: old in-process scan
        return true;
    }
    const auto o = PluginScanner::scanOne (format.getName(), fileOrIdentifier, result, PluginScanner::defaultTimeoutMs,
                                           [this] { return shouldExit(); });
    if (o.status == ScanOutcome::stopped) return true;           // user cancelled: don't blame the plug-in
    return o.status == ScanOutcome::ok;                           // false = skip it from now on
}
} // namespace cx
