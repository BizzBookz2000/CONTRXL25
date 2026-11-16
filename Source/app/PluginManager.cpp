#include "PluginManager.h"

namespace cx
{
PluginManager::PluginManager()
{
   #if JUCE_PLUGINHOST_VST3
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #endif
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
    // every scan (also JUCE's scan dialog) opens plug-ins in the helper process, so a broken
    // plug-in can't take CONTRXL 25 down; it is put on the skip list instead
    known.setCustomScanner (std::make_unique<OutOfProcessScanner>());
}

juce::AudioPluginFormat* PluginManager::formatNamed (const juce::String& name) const
{
    for (int i = 0; i < formats.getNumFormats(); ++i)
        if (auto* f = formats.getFormat (i); f != nullptr && f->getName() == name) return f;
    return nullptr;
}

ScanOutcome PluginManager::scanOne (juce::AudioPluginFormat& format, const juce::String& id,
                                    juce::OwnedArray<juce::PluginDescription>& found, int timeoutMs,
                                    std::function<bool()> shouldStop)
{
    ScanOutcome o;
    if (PluginScanner::helperAvailable())
        o = PluginScanner::scanOne (format.getName(), id, found, timeoutMs, std::move (shouldStop));
    else
    {
        // no helper (should not happen in a normal build): scan inside the app as before
        format.findAllTypesForFile (found, id);
        o.status = found.isEmpty() ? ScanOutcome::nothingFound : ScanOutcome::ok;
        o.detail = found.isEmpty() ? juce::String ("Failed - no usable plug-in inside") : "OK - " + found[0]->name;
    }
    if (o.status == ScanOutcome::ok)
    {
        known.removeFromBlacklist (id);
        for (auto* d : found) known.addType (*d);
    }
    else if (o.failed())
        known.addToBlacklist (id);
    return o;
}

juce::AudioPluginFormat* PluginManager::formatForFile (const juce::String& path) const
{
    for (int i = 0; i < formats.getNumFormats(); ++i)
        if (auto* f = formats.getFormat (i); f != nullptr && f->fileMightContainThisPluginType (path)) return f;
    return nullptr;
}

juce::Array<juce::AudioPluginFormat*> PluginManager::allFormats() const
{
    juce::Array<juce::AudioPluginFormat*> r;
    for (int i = 0; i < formats.getNumFormats(); ++i) r.add (formats.getFormat (i));
    return r;
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::create (const juce::PluginDescription& d, double sampleRate, int block, juce::String& error)
{
    return formats.createPluginInstance (d, sampleRate > 0 ? sampleRate : 48000.0, juce::jmax (64, block), error);
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createFromXml (const juce::String& xml, double sampleRate, int block, juce::String& error)
{
    juce::PluginDescription d;
    if (! fromXml (xml, d)) { error = "unknown plug-in"; return nullptr; }
    // the plug-in may have moved since the song was saved: look it up by its id
    for (auto& k : known.getTypes())
        if (k.isDuplicateOf (d) && k.fileOrIdentifier != d.fileOrIdentifier && ! juce::File (d.fileOrIdentifier).exists()) { d = k; break; }
    return create (d, sampleRate, block, error);
}

juce::String PluginManager::toXml (const juce::PluginDescription& d)
{
    if (auto x = d.createXml()) return x->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    return {};
}

bool PluginManager::fromXml (const juce::String& xml, juce::PluginDescription& out)
{
    if (auto x = juce::parseXML (xml)) return out.loadFromXml (*x);
    return false;
}

juce::Array<juce::PluginDescription> PluginManager::list (bool instruments) const
{
    juce::Array<juce::PluginDescription> r;
    for (auto& d : known.getTypes()) if (d.isInstrument == instruments) r.add (d);
    std::sort (r.begin(), r.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        const int m = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        return m != 0 ? m < 0 : a.name.compareIgnoreCase (b.name) < 0;
    });
    return r;
}
} // namespace cx
