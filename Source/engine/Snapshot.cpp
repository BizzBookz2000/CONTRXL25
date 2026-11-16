#include "Snapshot.h"
#include "dsp/Offline.h"

namespace cx
{
int effectiveRetro (const Pad& p, const SlotSettings& s)
{
    if (s.rm == 0) return 0;
    if (s.rm > 0) return s.rm;
    return p.fx.rm;
}

SamplePtr SnapshotBuilder::cached (const SamplePtr& src, const std::string& key, const std::function<SamplePtr()>& make)
{
    auto k = std::make_pair ((const void*) src.get(), key);
    auto it = cache.find (k);
    if (it != cache.end()) { it->second.lastUse = generation; return it->second.result; }
    auto r = make();
    if (! r) r = src;
    cache[k] = { src, r, generation };
    return r;
}

SamplePtr SnapshotBuilder::derive (const SamplePtr& src, const StemMix& st, const FxParams& fx, int rm, const dsp::StemStore& stems)
{
    if (! src) return nullptr;
    SamplePtr b = src;
    // 1. stem mix
    if (! st.isUnity())
        if (auto* s = stems.get (src.get()))
        {
            const auto key = "st" + std::to_string (st.lv[0]) + "|" + std::to_string (st.lv[1]) + "|"
                           + std::to_string (st.lv[2]) + "|" + std::to_string (st.lv[3]);
            auto stemsCopy = *s;
            b = cached (src, key, [stemsCopy, st] { return dsp::mixDown (stemsCopy, st); });
        }
    // 2. lo-fi crush
    if (fx.lofi)
    {
        const auto key = "lf" + std::to_string (fx.bits) + "|" + std::to_string (fx.rate);
        auto in = b; const int bits = fx.bits, rate = fx.rate;
        b = cached (in, key, [in, bits, rate] { return dsp::lofi (*in, bits, rate); });
    }
    // 3. retro mode
    if (rm > 0)
    {
        auto in = b;
        b = cached (in, "rm" + std::to_string (rm), [in, rm] { return dsp::retroProcess (*in, rm); });
    }
    return b;
}

SamplePtr SnapshotBuilder::playBuffer (const Pad& pad, const dsp::StemStore& stems)
{
    return derive (pad.buf, pad.m.st, pad.fx, effectiveRetro (pad, pad.m), stems);
}

SamplePtr SnapshotBuilder::layerBuffer (const Pad& pad, const Layer& L, const dsp::StemStore& stems)
{
    return derive (L.buf, L.s.st, pad.fx, effectiveRetro (pad, L.s), stems);
}

SamplePtr SnapshotBuilder::previewBuffer (const Pad& pad, const dsp::StemStore& stems, const TrimState& t, std::vector<float>* markersOut)
{
    auto base = playBuffer (pad, stems);
    if (! base) return nullptr;
    const float a = juce::jlimit (0.0f, 1.0f, t.a), b = juce::jlimit (a + 0.0005f, 1.0f, t.b);
    if (markersOut)
    {
        markersOut->clear();
        for (float m : pad.markers)
            if (m > a && m < b) markersOut->push_back ((m - a) / (b - a));
    }
    const auto key = "pv" + std::to_string (a) + "|" + std::to_string (b) + "|" + std::to_string (t.fi) + "|"
                   + std::to_string (t.fo) + "|" + std::to_string (t.g);
    const float fi = t.fi, fo = t.fo, g = t.g;
    return cached (base, key, [base, a, b, fi, fo, g]
    {
        auto sl = dsp::slice (*base, a * base->duration(), b * base->duration());
        return dsp::trimShape (*sl, fi, fo, g);
    });
}

void SnapshotBuilder::prune()
{
    for (auto it = cache.begin(); it != cache.end();)
    {
        // forget results nobody has asked for in a while, or whose source is gone everywhere else
        // stem mixes and trim previews change on every slider move: keep only the current one
        const auto& key = it->first.second;
        const bool volatileKey = key.rfind ("st", 0) == 0 || key.rfind ("pv", 0) == 0;
        const bool old = generation - it->second.lastUse > (volatileKey ? 1 : 40);
        const bool orphan = it->second.src.use_count() <= 1 && generation - it->second.lastUse > 2;
        if (old || orphan) it = cache.erase (it); else ++it;
    }
}

SnapshotPtr SnapshotBuilder::build (const Project& p, const dsp::StemStore& stems, const TrimState& trim, const SamplePtr& preview)
{
    ++generation;
    auto s = std::make_shared<Snapshot>();
    s->tracks = p.tracks;
    for (auto& t : p.tracks) if (t.solo) s->trackSoloAny = true;
    s->plen = p.plen; s->pbpm = p.pbpm; s->chain = p.chain; s->s = p.s; s->la = p.la; s->retLevel = p.retLevel;

    for (int i = 0; i < NPADS; ++i)
    {
        const Pad& pad = p.pads[(size_t) i];
        RPad& r = s->pads[(size_t) i];
        r.has = pad.buf != nullptr;
        r.muted = pad.muted; r.vol = pad.vol; r.pitch = pad.pitch; r.mg = pad.mg; r.lg = pad.lg;
        r.instRoot = pad.instRoot; r.pan = pad.pan; r.solo = pad.solo; r.send = pad.send;
        if (pad.solo) s->soloAny = true;
        r.fx = pad.fx; r.fxOn = pad.fx.anyOn();
        r.hasKmap = pad.hasKmap;
        r.keyMode = pad.keyMode;
        r.kmap.fill (-1);
        for (auto& kv : pad.kmap) if (kv.first >= 0 && kv.first < 32) r.kmap[(size_t) kv.first] = kv.second;
        if (! r.has) continue;

        // main slot (trim preview replaces it while trimming this pad)
        std::vector<float> mk = pad.markers;
        SamplePtr mb;
        if (trim.on && trim.pad == i && trim.changed()) mb = previewBuffer (pad, stems, trim, &mk);
        else mb = playBuffer (pad, stems);
        r.main.buf = mb;
        r.main.segs = segments (mb.get(), mk);
        r.main.ownSegs = true;
        r.main.vol = pad.m.vol; r.main.pitch = pad.m.pitch; r.main.ft = pad.m.ft; r.main.fc = pad.m.fc; r.main.pan = pad.m.pan;

        for (auto& L : pad.layers)
        {
            if (! L.buf) continue;
            RSlot ls;
            ls.buf = layerBuffer (pad, L, stems);
            ls.ownSegs = ! L.markers.empty();
            if (ls.ownSegs) ls.segs = segments (ls.buf.get(), L.markers);
            ls.kk = L.buf->duration() / std::max (1.0e-6, pad.buf->duration());
            ls.vol = L.s.vol; ls.pitch = L.s.pitch; ls.ft = L.s.ft; ls.fc = L.s.fc; ls.pan = L.s.pan;
            r.layers.push_back (std::move (ls));
        }
    }
    if (preview)
    {
        RPad& r = s->pads[(size_t) NPADS];
        r.has = true; r.main.buf = preview; r.main.segs = segments (preview.get(), {}); r.main.ownSegs = true;
    }
    prune();
    return s;
}
} // namespace cx
