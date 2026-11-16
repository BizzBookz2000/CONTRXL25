// CONTRXL 25 - immutable render snapshot.
// The UI thread builds one of these from the Project every time something changes
// and hands it to the audio engine. The audio thread only ever reads it.
// "Play buffers" here already have the stem mix, lo-fi and retro mode baked in
// (pseudocode 3.3: playBuf(pad) = retroMode(stemMix(pad.buf))).
#pragma once
#include "model/Model.h"
#include "dsp/Stems.h"

namespace cx
{
struct RSlot
{
    SamplePtr buf;                    // play-ready audio
    std::vector<std::pair<double, double>> segs;   // chop segments in seconds of THIS buffer
    bool ownSegs = false;             // layers without markers follow the pad's chops scaled by kk
    double kk = 1.0;                  // slot duration / main duration
    float vol = 1, pitch = 0;
    int ft = FT_OFF; float fc = 2000; float pan = 0;
};

struct RPad
{
    bool has = false, muted = false;
    float vol = 1, pitch = 0;
    int mg = 0, lg = 0;
    FxParams fx;
    bool fxOn = false;
    RSlot main;
    std::vector<RSlot> layers;
    bool hasKmap = false;
    int keyMode = 0;
    std::array<int, 32> kmap {};
    // mixer
    int instRoot = 60;
    float pan = 0;
    bool solo = false;
    std::array<float, NSEND> send {};
    int numChops() const { return has ? (int) main.segs.size() : 0; }
    int chopForKey (int k) const { return hasKmap ? (k >= 0 && k < 32 ? kmap[(size_t) k] : -1) : k; }
};

struct Snapshot
{
    std::array<RPad, NPADS + 1> pads;      // [NPADS] = library preview slot
    std::vector<Track> tracks;
    std::array<int, NPAT> plen {};
    std::array<int, NPAT> pbpm {};
    std::vector<ChainItem> chain;
    Settings s;
    LASettings la;
    bool soloAny = false;
    bool trackSoloAny = false;
    std::array<float, NSEND> retLevel { 1.0f, 1.0f };
    float bpmOf (int p) const { return pbpm[(size_t) p] > 0 ? (float) pbpm[(size_t) p] : s.bpm; }
};
using SnapshotPtr = std::shared_ptr<const Snapshot>;

// non-destructive trim preview (pseudocode 5.4)
struct TrimState
{
    bool on = false; int pad = -1;
    float a = 0, b = 1, fi = 0, fo = 0, g = 0;
    bool changed() const { return a > 0.0001f || b < 0.9999f || fi > 0 || fo > 0 || std::abs (g) > 0.001f; }
};

// Builds snapshots and caches the derived (processed) audio so that a retro mode
// or stem mix is only computed once per sample/setting.
class SnapshotBuilder
{
public:
    SnapshotPtr build (const Project& p, const dsp::StemStore& stems, const TrimState& trim, const SamplePtr& preview = nullptr);

    // the processed audio the pad plays (what the waveform shows / what gets saved when baking)
    SamplePtr playBuffer (const Pad& pad, const dsp::StemStore& stems);
    SamplePtr layerBuffer (const Pad& pad, const Layer& layer, const dsp::StemStore& stems);
    // trim preview result for the main sample: buffer + remapped markers
    SamplePtr previewBuffer (const Pad& pad, const dsp::StemStore& stems, const TrimState& t, std::vector<float>* markersOut);

    void clear() { cache.clear(); }
private:
    struct Entry { SamplePtr src, result; int lastUse = 0; };
    std::map<std::pair<const void*, std::string>, Entry> cache;
    int generation = 0;
    SamplePtr cached (const SamplePtr& src, const std::string& key, const std::function<SamplePtr()>& make);
    SamplePtr derive (const SamplePtr& src, const StemMix& st, const FxParams& fx, int rm, const dsp::StemStore& stems);
    void prune();
};

int effectiveRetro (const Pad& p, const SlotSettings& s);   // rmOf()
} // namespace cx
