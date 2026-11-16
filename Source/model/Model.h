// CONTRXL 25 - data model
// The whole project is plain data, exactly like the web version (see pseudocode section 2).
// Everything here is owned and edited by the message (UI) thread only.
// The audio thread never touches these objects; it gets an immutable copy
// (engine/Snapshot.h) every time something changes.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>
#include <optional>
#include <vector>
#include <map>

namespace cx
{
constexpr int NPADS   = 64;    // 8 banks A..H x 8 pads
constexpr int NPAT    = 16;    // patterns
constexpr int MAXT    = 64;    // max tracks
constexpr int MAXSTEP = 128;   // 8 bars of 16ths
constexpr int MAXLAY  = 8;     // layers per pad
constexpr float PR    = 24.0f; // pitch knob range +/- semitones
constexpr int TPS     = 24;    // unquantized ticks per step

inline juce::String padLabel (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + (i >> 3))) + juce::String ((i & 7) + 1); }
inline juce::String fmtSt (float v)
{
    const bool whole = std::abs (v - std::round (v)) < 0.005f;
    return (v >= 0 ? "+" : "") + (whole ? juce::String ((int) std::round (v)) : juce::String (v, 2));
}

// ------------------------------------------------------------------ samples
// Audio is immutable once created and shared by reference count. Editing a
// sample (reverse, stretch, trim...) always makes a new SampleData.
struct SampleData
{
    juce::AudioBuffer<float> audio;   // 1 or 2 channels
    double sampleRate = 44100.0;

    int    length()   const { return audio.getNumSamples(); }
    int    channels() const { return audio.getNumChannels(); }
    double duration() const { return length() / sampleRate; }
    mutable float cachedPeak = -1.0f;
    float  peak() const;
};
using SamplePtr = std::shared_ptr<const SampleData>;
SamplePtr makeSample (juce::AudioBuffer<float>&& audio, double sampleRate);

// ------------------------------------------------------------------ plug-ins (VST3)
// A slot that holds a plug-in. iid identifies the running instance: moving a pad keeps the
// instance (and its live state), copying a pad gets a new one built from 'state'.
constexpr int PAD_VFX = 2;     // VST inserts per pad
constexpr int MASTER_VFX = 4;  // VST inserts on the master
constexpr int NSEND = 2;       // send / return buses
constexpr int SEND_VFX = 2;    // VST inserts per return

struct PluginSlot
{
    int iid = 0;                      // 0 = empty
    juce::String xml;                 // juce::PluginDescription as XML
    juce::String name;
    juce::String state;               // base64 plug-in state (filled when saving / copying)
    bool bypass = false, instrument = false;
    bool empty() const { return iid == 0; }
};
int newPluginIid();

// ------------------------------------------------------------------ retro modes
// index 0 = none. Keys match the web version's song files.
constexpr int NRETRO = 8;
const char* retroKey  (int index);   // "", "asr10", ...
const char* retroName (int index);   // "Off", "ASR-10", ...
const char* retroDesc (int index);
int retroIndex (const juce::String& key);   // -1 if unknown/empty

// ------------------------------------------------------------------ pad effects
enum FilterType { FT_OFF = 0, FT_LP, FT_BP, FT_HP };
FilterType filterFromString (const juce::String&);
const char* filterToString (int);

struct FxParams
{
    int   type = FT_OFF;  float cut = 1200.0f, res = 0.25f;     // pad filter
    bool  lofi = false;   int bits = 12; int rate = 26040;       // lo-fi crush
    int   rm = 0;                                                 // retro mode (0 = none)

    bool satOn = false; float satDrive = 0.4f, satMix = 1.0f;
    bool vinOn = false; float vinCrackle = 0.4f, vinHiss = 0.3f, vinWow = 0.25f, vinTone = 0.35f;
    bool scaOn = false; float scaDiv = 3, scaAmt = 0.5f, scaMix = 1.0f;
    bool choOn = false; float choRate = 0.8f, choDepth = 0.5f, choMix = 0.5f;
    bool flaOn = false; float flaRate = 0.3f, flaDepth = 0.6f, flaFb = 0.5f, flaMix = 0.5f;
    bool delOn = false; float delTime = 3, delFb = 0.4f, delTone = 0.6f, delMix = 0.35f;
    bool ecoOn = false; float ecoTime = 380, ecoFb = 0.5f, ecoTone = 0.4f, ecoWob = 0.35f, ecoMix = 0.4f;
    bool revOn = false; float revSize = 0.5f, revDamp = 0.5f, revMix = 0.3f;

    bool anyOn() const { return satOn || vinOn || scaOn || choOn || flaOn || delOn || ecoOn || revOn; }
    float qValue() const { return 0.5f + res * res * 18.0f; }   // same as the web (dB for LP/HP)

    juce::var toVar() const;
    static FxParams fromVar (const juce::var&);
};

// stem levels: vocals, bass, instruments, drums
struct StemMix
{
    std::array<float, 4> lv { 1, 1, 1, 1 };
    bool isUnity() const { return lv[0] == 1 && lv[1] == 1 && lv[2] == 1 && lv[3] == 1; }
};

// settings for the main sample slot (pad.m) or for a layer
struct SlotSettings
{
    float vol = 1.0f, pitch = 0.0f;   // pitch: -36..36 for layers
    int   ft = FT_OFF; float fc = 2000.0f;
    float pan = 0.0f;
    int   rm = -1;                    // -1 = follow pad, 0 = off, 1.. = retro mode override
    StemMix st;
};

struct Layer
{
    SamplePtr buf;
    juce::String name;
    SlotSettings s;
    std::vector<float> markers;
};

struct Pad
{
    float vol = 1.0f, pitch = 0.0f;
    SamplePtr buf;
    juce::String name;
    std::vector<float> markers;       // chop points, 0..1 fractions
    int sel = -1;                     // selected chop
    std::map<int, int> kmap;          // key index -> chop (chop mode)
    bool hasKmap = false;
    FxParams fx;
    int mg = 0, lg = 0;               // mute group, link group
    bool muted = false;
    std::vector<Layer> layers;
    SlotSettings m;                   // main slot settings
    int keyMode = 0;                  // per pad: 0 = KEYS (pitched across the keys), 1 = CHOP (keys play chops)
    // mixer + plug-ins
    PluginSlot inst;                  // a VST instrument played by this pad (pads, keys, sequencer)
    int instRoot = 60;                // MIDI note the pad / root key plays on the instrument
    std::array<PluginSlot, PAD_VFX> vfx;
    float pan = 0.0f;                 // channel pan -1..1
    bool solo = false;
    std::array<float, NSEND> send {};

    int numChops() const { return buf ? (int) markers.size() + 1 : 0; }
    int chopForKey (int key) const
    {
        if (! hasKmap) return key;
        auto it = kmap.find (key);
        return it == kmap.end() ? -1 : it->second;
    }
};

// ------------------------------------------------------------------ sequencer
struct HitEvent { int t = 0; float p = 0; float v = 0.8f; int c = -2; };   // t in 1/24 step; c = chop (-2 = the track's)

struct PatternData
{
    std::array<uint8_t, MAXSTEP> steps {};
    std::array<float,   MAXSTEP> semi {};
    std::array<int16_t, MAXSTEP> chopAt;     // per step: which chop plays (-2 = the track's chop) - keys in chop mode
    std::vector<HitEvent> ev;
    PatternData() { chopAt.fill (-2); }
    int pad = -1, chop = -2;          // this pattern plays another pad / chop than the track's own (-1 / -2 = follow the track)
    bool hasData() const
    {
        if (! ev.empty()) return true;
        for (auto s : steps) if (s) return true;
        return false;
    }
};

struct Track
{
    int pad = 0, chop = -1;
    bool arm = false, mute = false;
    bool solo = false, lock = false;  // lock: live recording never writes into this track
    float vol = 1.0f;
    std::array<std::optional<PatternData>, NPAT> pd;

    PatternData& get (int p) { if (! pd[(size_t) p]) pd[(size_t) p].emplace(); return *pd[(size_t) p]; }
    const PatternData* find (int p) const { return pd[(size_t) p] ? &*pd[(size_t) p] : nullptr; }
    bool hasAnyData() const { for (auto& d : pd) if (d && d->hasData()) return true; return false; }
    // the pad / chop this track plays in pattern p (a pattern can override the track's own)
    int padIn (int p) const { auto* d = find (p); return d && d->pad >= 0 ? d->pad : pad; }
    int chopIn (int p) const { auto* d = find (p); return d && d->chop >= -1 ? d->chop : chop; }
    bool hasDataIn (int p) const { auto* d = find (p); return d && d->hasData(); }
};

struct ChainItem { int pat = 0, rep = 1; };

struct LASettings { bool on = true; float pr = 0; float gain = 50; bool limit = false; };

enum PlayMode { MODE_KEYS = 0, MODE_CHOP = 1 };

struct Settings
{
    float bpm = 84;
    int keys = 25;                    // 25 or 32
    int mode = MODE_KEYS;             // only kept for web song files; the real mode is per pad (Pad::keyMode)
    int oct = 0;                      // -3..3
    int root = 60;
    bool loop = true, metro = true, count = true, quant = false;
    bool vel = true, choke = true, gate = false, autoNorm = true;
    bool song = false;
    int pat = 0;                      // current pattern
    int sel = 0;                      // selected pad
    float mvol = 0.85f;
};

struct LibItem { int id = 0; juce::String name, crate; SamplePtr buf; };

struct Project
{
    std::array<Pad, NPADS> pads;
    std::vector<Track> tracks;
    std::array<int, NPAT> plen;
    std::array<int, NPAT> pbpm;       // 0 = follow project tempo
    std::vector<ChainItem> chain;
    Settings s;
    LASettings la;
    juce::String name = "SESSION_01";
    std::vector<LibItem> lib;                     // only library samples a loaded (web) song brought along; the real library is cx::Library
    std::vector<juce::String> crates { "Drums", "Bass", "Vocals", "Loops", "FX" };
    // mixer: master and return plug-ins
    std::array<PluginSlot, MASTER_VFX> masterVfx;
    std::array<std::array<PluginSlot, SEND_VFX>, NSEND> sendVfx;
    std::array<float, NSEND> retLevel { 1.0f, 1.0f };

    Project();
    void reset();
    float bpmOf (int p) const { return pbpm[(size_t) p] > 0 ? (float) pbpm[(size_t) p] : s.bpm; }
    int keyBase() const { return s.keys == 32 ? 53 : 48; }   // KB()
    int keyShift() const { return s.keys == 32 ? 5 : 0; }
};

// segments (in seconds) of a sample cut at the given markers
std::vector<std::pair<double, double>> segments (const SampleData* buf, const std::vector<float>& markers);

} // namespace cx
