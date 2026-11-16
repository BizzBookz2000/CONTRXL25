#include <atomic>
#include "Model.h"

namespace cx
{
float SampleData::peak() const
{
    if (cachedPeak < 0.0f)
    {
        float pk = 0.0f;
        for (int c = 0; c < channels(); ++c)
            pk = std::max (pk, audio.getMagnitude (c, 0, length()));
        cachedPeak = pk;
    }
    return cachedPeak;
}

int newPluginIid()
{
    static std::atomic<int> next { 1 };
    return next.fetch_add (1);
}

SamplePtr makeSample (juce::AudioBuffer<float>&& audio, double sampleRate)
{
    auto s = std::make_shared<SampleData>();
    s->audio = std::move (audio);
    s->sampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    return s;
}

// ------------------------------------------------------------------ retro names
static const char* const RKEYS[NRETRO]  = { "", "asr10", "mpc60", "mpc3000", "sp1200", "sp202", "sp303", "sp404" };
static const char* const RNAMES[NRETRO] = { "Off", "ASR-10", "MPC60", "MPC3000", "SP-1200", "SP-202", "SP-303", "SP-404" };
static const char* const RDESC[NRETRO]  = {
    "No sampler colouring.",
    "Warm, slightly saturated 16-bit converters, smooth rounded transients, low-mid bloom, gentle top-end roll-off.",
    "12-bit crunch, slow transients, low-mid bump, narrow bandwidth, early-90s boom-bap grit.",
    "Clean but punchy, transformer-like weight, rounded transients, smoothed highs, polished 90s Akai sheen.",
    "12-bit at 26.04 kHz with real aliasing, crunchy transients, gritty texture.",
    "Lo-fi, noisy, smeared transients, heavy aliasing, vinyl-style dirt, soft hazy playback.",
    "Warm, gooey compression, soft clipping, early Roland lo-fi coloration, slight stereo smear.",
    "Punchy mids, vinyl noise and wobble, slight saturation, audible compressor pump, modern lo-fi." };

const char* retroKey  (int i) { return (i >= 0 && i < NRETRO) ? RKEYS[i] : ""; }
const char* retroName (int i) { return (i >= 0 && i < NRETRO) ? RNAMES[i] : "Off"; }
const char* retroDesc (int i) { return (i >= 0 && i < NRETRO) ? RDESC[i] : ""; }
int retroIndex (const juce::String& key)
{
    if (key.isEmpty()) return -1;
    for (int i = 1; i < NRETRO; ++i) if (key == RKEYS[i]) return i;
    return -1;
}

FilterType filterFromString (const juce::String& s)
{
    if (s == "lp") return FT_LP;
    if (s == "bp") return FT_BP;
    if (s == "hp") return FT_HP;
    return FT_OFF;
}
const char* filterToString (int t)
{
    switch (t) { case FT_LP: return "lp"; case FT_BP: return "bp"; case FT_HP: return "hp"; default: return "off"; }
}

// ------------------------------------------------------------------ FxParams <-> JSON (web compatible keys)
namespace
{
    struct FxF { const char* key; float FxParams::* m; };
    struct FxB { const char* key; bool  FxParams::* m; };
    const FxF FXF[] = {
        { "satDrive", &FxParams::satDrive }, { "satMix", &FxParams::satMix },
        { "vinCrackle", &FxParams::vinCrackle }, { "vinHiss", &FxParams::vinHiss }, { "vinWow", &FxParams::vinWow }, { "vinTone", &FxParams::vinTone },
        { "scaDiv", &FxParams::scaDiv }, { "scaAmt", &FxParams::scaAmt }, { "scaMix", &FxParams::scaMix },
        { "choRate", &FxParams::choRate }, { "choDepth", &FxParams::choDepth }, { "choMix", &FxParams::choMix },
        { "flaRate", &FxParams::flaRate }, { "flaDepth", &FxParams::flaDepth }, { "flaFb", &FxParams::flaFb }, { "flaMix", &FxParams::flaMix },
        { "delTime", &FxParams::delTime }, { "delFb", &FxParams::delFb }, { "delTone", &FxParams::delTone }, { "delMix", &FxParams::delMix },
        { "ecoTime", &FxParams::ecoTime }, { "ecoFb", &FxParams::ecoFb }, { "ecoTone", &FxParams::ecoTone }, { "ecoWob", &FxParams::ecoWob }, { "ecoMix", &FxParams::ecoMix },
        { "revSize", &FxParams::revSize }, { "revDamp", &FxParams::revDamp }, { "revMix", &FxParams::revMix } };
    const FxB FXB[] = {
        { "satOn", &FxParams::satOn }, { "vinOn", &FxParams::vinOn }, { "scaOn", &FxParams::scaOn }, { "choOn", &FxParams::choOn },
        { "flaOn", &FxParams::flaOn }, { "delOn", &FxParams::delOn }, { "ecoOn", &FxParams::ecoOn }, { "revOn", &FxParams::revOn },
        { "lofi", &FxParams::lofi } };
}

juce::var FxParams::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", filterToString (type));
    o->setProperty ("cut", cut);
    o->setProperty ("res", res);
    o->setProperty ("bits", bits);
    o->setProperty ("rate", rate);
    if (rm > 0) o->setProperty ("rm", retroKey (rm));
    for (auto& f : FXF) o->setProperty (f.key, this->*(f.m));
    for (auto& b : FXB) o->setProperty (b.key, this->*(b.m));
    return juce::var (o);
}

FxParams FxParams::fromVar (const juce::var& v)
{
    FxParams f;
    if (! v.isObject()) return f;
    f.type = filterFromString (v.getProperty ("type", "off").toString());
    f.cut  = (float) (double) v.getProperty ("cut", f.cut);
    f.res  = (float) (double) v.getProperty ("res", f.res);
    f.bits = (int) v.getProperty ("bits", f.bits);
    f.rate = (int) v.getProperty ("rate", f.rate);
    f.rm   = juce::jmax (0, retroIndex (v.getProperty ("rm", "").toString()));
    for (auto& x : FXF) if (v.hasProperty (x.key)) f.*(x.m) = (float) (double) v.getProperty (x.key, 0.0);
    for (auto& x : FXB) if (v.hasProperty (x.key)) f.*(x.m) = (bool) v.getProperty (x.key, false);
    return f;
}

// ------------------------------------------------------------------ project
Project::Project() { reset(); }

void Project::reset()
{
    for (auto& p : pads) p = Pad();
    tracks.clear();
    for (int i = 0; i < 16; ++i) { Track t; t.pad = i; tracks.push_back (t); }
    plen.fill (16);
    pbpm.fill (0);
    chain.clear();
    const float mv = s.mvol; const int keys = s.keys;
    s = Settings();
    s.mvol = mv; s.keys = keys;     // device-ish preferences survive "New song"
    la = LASettings();
    name = "SESSION_01";
    for (auto& m : masterVfx) m = PluginSlot();
    for (auto& r : sendVfx) for (auto& m : r) m = PluginSlot();
    retLevel = { 1.0f, 1.0f };
}

std::vector<std::pair<double, double>> segments (const SampleData* buf, const std::vector<float>& markers)
{
    std::vector<std::pair<double, double>> out;
    if (buf == nullptr) return out;
    const double d = buf->duration();
    double prev = 0.0;
    for (float m : markers) { out.push_back ({ prev * d, m * d }); prev = m; }
    out.push_back ({ prev * d, d });
    return out;
}

} // namespace cx
