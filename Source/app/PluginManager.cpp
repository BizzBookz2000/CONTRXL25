#include "PluginManager.h"
#include "app/Platform.h"

namespace cx
{
PluginManager::PluginManager()
{
   #if JUCE_PLUGINHOST_VST3
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #endif
   #if JUCE_PLUGINHOST_AU && (JUCE_MAC || JUCE_IOS)
    formats.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());   // Audio Units on the Mac, AUv3 on the iPad
   #endif
   #if ! CX_MOBILE
    // every scan (also JUCE's scan dialog) opens plug-ins in the helper process, so a broken
    // plug-in can't take CONTRXL 25 down; it is put on the skip list instead
    known.setCustomScanner (std::make_unique<OutOfProcessScanner>());
   #endif
}

bool PluginManager::needsAsync (const juce::PluginDescription& d) const
{
    for (auto* f : formats.getFormats())
        if (f->getName() == d.pluginFormatName) return f->requiresUnblockedMessageThreadDuringCreation (d);
    return false;
}

juce::PluginDescription PluginManager::resolve (const juce::String& xml) const
{
    juce::PluginDescription d;
    if (! fromXml (xml, d)) return {};
    for (auto& k : known.getTypes())
        if (k.isDuplicateOf (d) && k.fileOrIdentifier != d.fileOrIdentifier && ! juce::File (d.fileOrIdentifier).exists()) { d = k; break; }
    return d;
}

bool PluginManager::needsAsyncXml (const juce::String& xml) const
{
    const auto d = resolve (xml);
    return d.name.isNotEmpty() && needsAsync (d);
}

void PluginManager::createAsync (const juce::PluginDescription& d, double sampleRate, int block, Ready done)
{
    formats.createPluginInstanceAsync (d, sampleRate > 0 ? sampleRate : 48000.0, juce::jmax (64, block),
        [done] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) { if (done) done (std::move (inst), err); });
}

void PluginManager::createAsyncXml (const juce::String& xml, double sampleRate, int block, Ready done)
{
    const auto d = resolve (xml);
    if (d.name.isEmpty()) { if (done) done (nullptr, "unknown plug-in"); return; }
    createAsync (d, sampleRate, block, std::move (done));
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
    if (needsAsync (d)) { error = "opens asynchronously"; return nullptr; }   // never block (it would hang an iPad)
    return formats.createPluginInstance (d, sampleRate > 0 ? sampleRate : 48000.0, juce::jmax (64, block), error);
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createFromXml (const juce::String& xml, double sampleRate, int block, juce::String& error)
{
    // the plug-in may have moved since the song was saved: look it up by its id
    const auto d = resolve (xml);
    if (d.name.isEmpty()) { error = "unknown plug-in"; return nullptr; }
    if (needsAsync (d)) { error = "opens asynchronously"; return nullptr; }
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
