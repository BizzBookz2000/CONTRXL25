// CONTRXL 25 data model - the same project as the desktop and web versions
// (64 pads in banks A..H, layers, chops, pad FX, tracks x 16 patterns, song chain).
#pragma once
#include "common.h"
#include "json.h"

namespace cx
{
constexpr int NPADS   = 64;
constexpr int NPAT    = 16;
constexpr int MAXT    = 64;
constexpr int MAXSTEP = 128;
constexpr int MAXLAY  = 8;
constexpr float PR    = 24.0f;   // pitch range +/- semitones
constexpr int TPS     = 24;      // ticks per step (live hits keep their exact timing)
constexpr int KEYBASE = 48;      // first key of a 25-key keyboard (C3)

inline std::string padLabel (int i) { return std::string (1, (char) ('A' + (i >> 3))) + std::to_string ((i & 7) + 1); }

constexpr int NRETRO = 8;
const char* retroKey (int i);
const char* retroName (int i);
int retroIndex (const std::string& key);

enum FilterType { FT_OFF = 0, FT_LP, FT_BP, FT_HP };
FilterType filterFromString (const std::string&);
const char* filterToString (int);
const char* filterName (int);

struct FxParams
{
    int type = FT_OFF; float cut = 1200.0f, res = 0.25f;
    bool lofi = false; int bits = 12; int rate = 26040;
    int rm = 0;
    bool satOn = false; float satDrive = 0.4f, satMix = 1.0f;
    bool vinOn = false; float vinCrackle = 0.4f, vinHiss = 0.3f, vinWow = 0.25f, vinTone = 0.35f;
    bool scaOn = false; float scaDiv = 3, scaAmt = 0.5f, scaMix = 1.0f;
    bool choOn = false; float choRate = 0.8f, choDepth = 0.5f, choMix = 0.5f;
    bool flaOn = false; float flaRate = 0.3f, flaDepth = 0.6f, flaFb = 0.5f, flaMix = 0.5f;
    bool delOn = false; float delTime = 3, delFb = 0.4f, delTone = 0.6f, delMix = 0.35f;
    bool ecoOn = false; float ecoTime = 380, ecoFb = 0.5f, ecoTone = 0.4f, ecoWob = 0.35f, ecoMix = 0.4f;
    bool revOn = false; float revSize = 0.5f, revDamp = 0.5f, revMix = 0.3f;
    bool anyOn() const { return satOn || vinOn || scaOn || choOn || flaOn || delOn || ecoOn || revOn; }
    float qValue() const { return 0.5f + res * res * 18.0f; }
    Json toJson() const;
    static FxParams fromJson (const Json&);
};

struct SlotSettings { float vol = 1.0f, pitch = 0.0f; int ft = FT_OFF; float fc = 2000.0f; float pan = 0.0f; int rm = -1; Json st; };

struct Layer { SamplePtr buf; std::string name; SlotSettings s; std::vector<float> markers; };

enum PlayMode { MODE_KEYS = 0, MODE_CHOP = 1 };

struct Pad
{
    float vol = 1.0f, pitch = 0.0f;
    SamplePtr buf;
    std::string name;
    std::vector<float> markers;      // chop points 0..1
    std::map<int, int> kmap; bool hasKmap = false;
    FxParams fx;
    int mg = 0, lg = 0;
    bool muted = false;
    std::vector<Layer> layers;
    SlotSettings m;
    int keyMode = MODE_KEYS;
    float pan = 0.0f;
    bool solo = false;
    std::array<float, 2> send {};
    Json extra;                      // desktop-only fields (VST plug-ins) kept so saving does not lose them
    int numChops() const { return buf ? (int) markers.size() + 1 : 0; }
    bool hasSound() const { return buf != nullptr; }
};

struct HitEvent { int t = 0; float p = 0; float v = 0.8f; int c = -2; };

struct PatternData
{
    std::array<uint8_t, MAXSTEP> steps {};
    std::array<float, MAXSTEP> semi {};
    std::array<int16_t, MAXSTEP> chopAt;
    std::vector<HitEvent> ev;
    int pad = -1, chop = -2;
    PatternData() { chopAt.fill (-2); }
    bool hasData() const { if (! ev.empty()) return true; for (auto s : steps) if (s) return true; return false; }
};

struct Track
{
    int pad = 0, chop = -1;
    bool mute = false, solo = false, lock = false;
    float vol = 1.0f;
    std::array<std::optional<PatternData>, NPAT> pd;
    PatternData& get (int p) { if (! pd[(size_t) p]) pd[(size_t) p].emplace(); return *pd[(size_t) p]; }
    const PatternData* find (int p) const { return pd[(size_t) p] ? &*pd[(size_t) p] : nullptr; }
    bool hasDataIn (int p) const { auto* d = find (p); return d && d->hasData(); }
};

struct ChainItem { int pat = 0, rep = 1; };

struct Settings
{
    float bpm = 84;
    int root = 60;
    bool loop = true, metro = true, count = true, quant = false;
    bool vel = true, choke = true, gate = false, autoNorm = true;
    bool song = false;
    int pat = 0, sel = 0;
    float mvol = 0.85f;
    bool keyPads = false;
    std::array<int, 128> keyPad {};
};

struct Project
{
    std::array<Pad, NPADS> pads;
    std::vector<Track> tracks;
    std::array<int, NPAT> plen;
    std::array<int, NPAT> pbpm;
    std::vector<ChainItem> chain;
    Settings s;
    std::string name = "SESSION_01";
    Json keep;                       // song-file fields this edition does not use (library, plug-ins, LA-2A...) - written back unchanged
    Project() { reset(); }
    void reset();
    float bpmOf (int p) const { return pbpm[(size_t) p] > 0 ? (float) pbpm[(size_t) p] : s.bpm; }
    int trackFor (int pad, bool create);   // the track that plays this pad (chop -1), -1 if none
};

std::vector<std::pair<double, double>> segments (const SampleData* buf, const std::vector<float>& markers);

// CONTRXL 25 song files (.zip with project.json + samples/*.wav) - same as the desktop and web versions
bool saveSong (const Project& p, const std::string& zipPath, std::string& error);
bool loadSong (const std::string& zipPath, Project& p, std::string& message);
} // namespace cx
