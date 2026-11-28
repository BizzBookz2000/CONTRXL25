#include "engine.h"
#include <random>

namespace cx
{
// ------------------------------------------------------------------ snapshots
int effectiveRetro (const Pad& p, const SlotSettings& s) { if (s.rm == 0) return 0; if (s.rm > 0) return s.rm; return p.fx.rm; }

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

SamplePtr SnapshotBuilder::derive (const SamplePtr& src, const FxParams& fx, int rm)
{
    if (! src) return nullptr;
    SamplePtr b = src;
    if (fx.lofi)
    {
        auto in = b; const int bits = fx.bits, rate = fx.rate;
        b = cached (in, "lf" + std::to_string (bits) + "|" + std::to_string (rate), [in, bits, rate] { return dsp::lofi (*in, bits, rate); });
    }
    if (rm > 0) { auto in = b; b = cached (in, "rm" + std::to_string (rm), [in, rm] { return dsp::retroProcess (*in, rm); }); }
    return b;
}

SamplePtr SnapshotBuilder::playBuffer (const Pad& pad) { return derive (pad.buf, pad.fx, effectiveRetro (pad, pad.m)); }

SnapshotPtr SnapshotBuilder::build (const Project& p, const SamplePtr& preview)
{
    ++generation;
    auto s = std::make_shared<Snapshot>();
    s->tracks = p.tracks;
    for (auto& t : p.tracks) if (t.solo) s->trackSoloAny = true;
    s->plen = p.plen; s->pbpm = p.pbpm; s->chain = p.chain; s->s = p.s;
    for (int i = 0; i < NPADS; ++i)
    {
        const Pad& pad = p.pads[(size_t) i];
        RPad& r = s->pads[(size_t) i];
        r.has = pad.buf != nullptr;
        r.muted = pad.muted; r.vol = pad.vol; r.pitch = pad.pitch; r.mg = pad.mg; r.lg = pad.lg; r.pan = pad.pan; r.solo = pad.solo;
        if (pad.solo) s->soloAny = true;
        r.fx = pad.fx; r.fxOn = pad.fx.anyOn();
        if (! r.has) continue;
        r.main.buf = playBuffer (pad);
        r.main.segs = segments (r.main.buf.get(), pad.markers);
        r.main.ownSegs = true;
        r.main.vol = pad.m.vol; r.main.pitch = pad.m.pitch; r.main.ft = pad.m.ft; r.main.fc = pad.m.fc; r.main.pan = pad.m.pan;
        for (auto& L : pad.layers)
        {
            if (! L.buf) continue;
            RSlot ls;
            ls.buf = derive (L.buf, pad.fx, effectiveRetro (pad, L.s));
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
    for (auto it = cache.begin(); it != cache.end();)
    {
        const bool old = generation - it->second.lastUse > 40;
        const bool orphan = it->second.src.use_count() <= 1 && generation - it->second.lastUse > 2;
        if (old || orphan) it = cache.erase (it); else ++it;
    }
    return s;
}

// ------------------------------------------------------------------ engine
Engine::Engine()
{
    for (auto& a : fxPtr) a.store (nullptr);
    for (auto& a : padPeak) a.store (0);
    for (auto& a : padHits) a.store (0);
    outPeak[0] = outPeak[1] = inPeak[0] = inPeak[1] = 0;
}

void Engine::prepare (double sampleRate, int mb)
{
    sr = sampleRate; maxBlock = std::max (128, mb);
    padBus.assign ((size_t) NPADS * 2 * (size_t) maxBlock, 0.0f);
    mL.assign ((size_t) maxBlock, 0.0f); mR.assign ((size_t) maxBlock, 0.0f);
    const int n = (int) (sr * 2);
    crackle.assign ((size_t) n, 0.0f); hiss.assign ((size_t) n, 0.0f);
    std::mt19937 rng (4242); std::uniform_real_distribution<float> U (0.0f, 1.0f);
    float lp = 0;
    for (int i = 0; i < n; ++i) { const float w = U (rng) * 2 - 1; lp += (w - lp) * 0.35f; hiss[(size_t) i] = lp * 1.4f; }
    for (int k = 0; k < 32; ++k)
    {
        const int at = (int) (U (rng) * (float) (n - 400));
        const float a = std::pow (U (rng), 2.2f) * 0.9f + 0.05f, sg = U (rng) < 0.5f ? -1.0f : 1.0f;
        const int len = 30 + (int) (U (rng) * 160);
        for (int i = 0; i < len; ++i) crackle[(size_t) (at + i)] += sg * a * std::exp (-(float) i / (len * 0.18f)) * (U (rng) * 0.6f + 0.4f);
    }
    for (int i = 0; i < n; ++i) if (U (rng) < 0.0025f) crackle[(size_t) i] += (U (rng) * 2 - 1) * 0.18f;
    for (int i = 0; i < 64; ++i) { const float f = (float) i / 64.0f; crackle[(size_t) i] *= f; crackle[(size_t) (n - 1 - i)] *= f; hiss[(size_t) i] *= f; hiss[(size_t) (n - 1 - i)] *= f; }
    for (auto& v : voices) { v.active = false; v.snap.reset(); }
    npend = 0; nclicks = 0; seqPlaying = false; playing.store (false);
}

void Engine::ensureFx (int pad)
{
    if (pad < 0 || pad >= NPADS || fxOwn[(size_t) pad]) return;
    auto fx = std::make_unique<PadFx>();
    fx->prepare (sr, maxBlock);
    fxPtr[(size_t) pad].store (fx.get(), std::memory_order_release);
    fxOwn[(size_t) pad] = std::move (fx);
}

void Engine::setSnapshot (SnapshotPtr s) { std::lock_guard<std::mutex> l (snapLock); pending = std::move (s); }

void Engine::adoptSnapshot()
{
    std::unique_lock<std::mutex> l (snapLock, std::try_to_lock);
    if (! l.owns_lock() || ! pending) return;
    cur = std::move (pending);   // the UI thread keeps old snapshots alive until we drop them, so nothing is freed here
}

float Engine::gainOf (float vel) const
{
    if (cur && ! cur->s.vel) return 0.9f;
    return std::max (0.12f, vel / 127.0f);
}

void Engine::chokeVoice (Voice& v, float seconds)
{
    if (! v.active || (v.releasing && v.relStep >= 1.0f / (seconds * (float) sr))) return;
    v.releasing = true;
    v.relStep = 1.0f / std::max (1.0f, seconds * (float) sr);
}
void Engine::releaseTid (uint32_t tid, float seconds) { if (tid) for (auto& v : voices) if (v.active && v.tid == tid) chokeVoice (v, seconds); }
void Engine::releaseLive (float seconds) { for (auto& v : voices) if (v.active && v.live) chokeVoice (v, seconds); }
void Engine::killAll (float seconds) { for (auto& v : voices) if (v.active) chokeVoice (v, seconds); held.fill (0); }

uint32_t Engine::startVoice (const Snapshot& s, int padIdx, int chop, float semi, float gain, int trk, bool live, uint32_t tid, int layer, bool isLink)
{
    if (padIdx < 0 || padIdx > NPADS) return 0;
    const RPad& p = s.pads[(size_t) padIdx];
    if (! p.has || p.muted) return 0;
    if (layer >= (int) p.layers.size()) return 0;
    const float g0 = gain;
    if (tid == 0) { tid = nextTid++; if (nextTid == 0) nextTid = 1; }
    gain *= p.vol;
    const RSlot& slot = layer < 0 ? p.main : p.layers[(size_t) layer];
    gain *= slot.vol;
    const SampleData* buf = slot.buf.get();
    if (buf == nullptr || buf->length() < 2) return 0;
    double off = 0, dur = buf->duration();
    if (chop >= 0)
    {
        if (layer >= 0 && slot.ownSegs)
        {
            if (chop >= (int) slot.segs.size()) return 0;
            off = slot.segs[(size_t) chop].first; dur = slot.segs[(size_t) chop].second - off;
        }
        else
        {
            const auto& sg = p.main.segs;
            if (chop >= (int) sg.size()) return 0;
            const double kk = layer < 0 ? 1.0 : slot.kk;
            off = sg[(size_t) chop].first * kk; dur = (sg[(size_t) chop].second - sg[(size_t) chop].first) * kk;
        }
    }
    if (dur <= 0.001) return tid;
    const double rate = std::pow (2.0, (clampv (semi + p.pitch, -PR, PR) + slot.pitch) / 12.0);
    // mute group: pads in the same group cut each other off; the very same sound retriggering too
    for (auto& o : voices)
    {
        if (! o.active || o.tid == tid || o.trk != trk || o.startTime > now) continue;
        if ((p.mg != 0 && o.pad->mg == p.mg) || (o.padIdx == padIdx && o.layer == layer && o.chop == chop && o.semi == semi))
            chokeVoice (o, 0.012f);
    }
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.order < v->order) v = &x; }
    Voice& V = *v;
    V = Voice();
    V.active = true; V.snap = cur; V.pad = &p; V.slot = &slot;
    V.padIdx = padIdx; V.layer = layer; V.chop = chop; V.trk = trk; V.semi = semi; V.tid = tid; V.live = live;
    V.src[0] = buf->data (0); V.src[1] = buf->data (1); V.len = buf->length();
    V.stereoSrc = buf->channels() > 1;
    V.pos = off * buf->sampleRate;
    V.endPos = std::min ((double) buf->length() - 1, (off + dur) * buf->sampleRate);
    V.rate = rate * buf->sampleRate / sr;
    V.gain = gain;
    V.atk = std::max (1, (int) (0.002 * sr));
    const double durOut = dur / rate;
    V.finSamples = (float) (std::min (0.005, durOut / 4.0) * sr);
    if (slot.ft != FT_OFF)
    {
        V.hasSlotFilter = true;
        if (slot.ft == FT_LP) V.sfc.setLP (slot.fc, 0.9, sr);
        else if (slot.ft == FT_HP) V.sfc.setHP (slot.fc, 0.9, sr);
        else V.sfc.setBP (slot.fc, 0.9, sr);
    }
    if (std::fabs (slot.pan) > 0.001f)
    {
        V.hasPan = true; V.panX = clampv (slot.pan, -1.0f, 1.0f);
        const float halfPi = 1.5707963f;
        const float x = V.stereoSrc ? (V.panX <= 0 ? V.panX + 1.0f : V.panX) : (V.panX + 1.0f) * 0.5f;
        V.panL = std::cos (x * halfPi); V.panR = std::sin (x * halfPi);
    }
    if (p.fx.vinOn && layer < 0 && ! crackle.empty())
    {
        V.vinyl = true; V.crk = p.fx.vinCrackle * 0.5f; V.hiss = p.fx.vinHiss * 0.05f;
        V.noisePos = (int) ((tid * 2654435761u) % (uint32_t) crackle.size());
    }
    V.startTime = now; V.order = ++voiceOrder;
    if (layer < 0 && padIdx < NPADS) padHits[(size_t) padIdx].fetch_add (1);
    if (layer < 0)
    {
        for (int i = 0; i < (int) p.layers.size(); ++i) startVoice (s, padIdx, chop, semi, g0, trk, live, tid, i, isLink);
        if (p.lg != 0 && ! isLink)
            for (int q = 0; q < NPADS; ++q)
            {
                const RPad& qp = s.pads[(size_t) q];
                if (q == padIdx || qp.lg != p.lg || ! qp.has) continue;
                const int qc = (chop >= 0 && qp.numChops() > chop) ? chop : -1;
                startVoice (s, q, qc, semi, g0, trk, live, tid, -1, true);
            }
    }
    return tid;
}

static inline float hermite (const float* d, int len, double pos) noexcept
{
    const int i = (int) pos; const float t = (float) (pos - i);
    auto at = [d, len] (int k) { return (k >= 0 && k < len) ? d[k] : 0.0f; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

void Engine::renderVoice (Voice& v, float* L, float* R, int n)
{
    const int len = v.len;
    const double rate = v.rate;
    const bool hasPF = v.padIdx < NPADS && v.pad->fx.type != FT_OFF;
    const BQ& pc = padFilter[(size_t) std::min (v.padIdx, NPADS - 1)];
    const int nz = (int) crackle.size();
    for (int i = 0; i < n; ++i)
    {
        if (v.pos >= v.endPos) { v.active = false; break; }
        float l = hermite (v.src[0], len, v.pos);
        float r = v.stereoSrc ? hermite (v.src[1], len, v.pos) : l;
        if (hasPF) { l = v.pf[0].run (pc, l); r = v.pf[1].run (pc, r); }
        if (v.hasSlotFilter) { l = v.sf[0].run (v.sfc, l); r = v.sf[1].run (v.sfc, r); }
        if (v.hasPan)
        {
            if (v.stereoSrc)
            {
                if (v.panX <= 0) { const float nl = l + r * v.panL, nr = r * v.panR; l = nl; r = nr; }
                else { const float nl = l * v.panL, nr = r + l * v.panR; l = nl; r = nr; }
            }
            else { const float m = l; l = m * v.panL; r = m * v.panR; }
        }
        float e = v.age < v.atk ? (float) v.age / (float) v.atk : 1.0f;
        const float remain = (float) ((v.endPos - v.pos) / rate);
        if (remain < v.finSamples) e = std::min (e, remain / std::max (1.0f, v.finSamples));
        if (v.releasing) { v.relLevel -= v.relStep; if (v.relLevel <= 0.0f) { v.active = false; break; } e *= v.relLevel; }
        if (v.vinyl)
        {
            const float ns = crackle[(size_t) v.noisePos] * v.crk + hiss[(size_t) v.noisePos] * v.hiss;
            if (++v.noisePos >= nz) v.noisePos = 0;
            l += ns; r += ns;
        }
        const float g = v.gain * e;
        L[i] += l * g; R[i] += r * g;
        v.pos += rate; ++v.age;
    }
    if (! v.active) v.snap.reset();
}

// ------------------------------------------------------------------ hits
void Engine::recordHit (int pad, int chop, float semi, float gain)
{
    if (! cur || ! seqPlaying || lastStep < 0 || ! recordArmed.load()) return;
    const double sd = 60.0 / cur->bpmOf (lastPat) / 4.0 * sr;
    const double t = (double) now - (offline ? 0 : outLatency.load()) - recOffsetMs.load() * 0.001 * sr;
    EngineEvent e; e.type = EngineEvent::RecHit;
    e.pad = pad; e.chop = chop; e.pat = lastPat; e.semi = semi; e.gain = gain;
    e.pos = lastStep + (t - lastStepTime) / sd;
    // a hit that lands on a step not played yet this time round must not sound again when we get there
    const int len = clampv (cur->plen[(size_t) lastPat], 16, MAXSTEP);
    const int tick = cur->s.quant ? (int) std::lround (e.pos) * TPS : (int) std::lround (e.pos * TPS);
    const int st = ((tick / TPS) % len + len) % len;
    if (st != lastStep && skips.size() < 64) skips.push_back ({ lastPat, st, pad, chop, semi });
    emit (e);
}

void Engine::hit (int pad, int chop, float semi, float vel, bool choke, bool record, int hold)
{
    if (! cur) return;
    const Snapshot& s = *cur;
    const float g = gainOf (vel);
    uint32_t tid = 0;
    if (pad >= 0 && pad <= NPADS && s.pads[(size_t) pad].has)
    {
        if (choke && s.s.choke && ! s.pads[(size_t) pad].muted) releaseLive (0.02f);
        tid = startVoice (s, pad, chop, semi, g, -1, true, 0, -1, false);
    }
    if (hold >= 0 && hold < 256) held[(size_t) hold] = tid;
    if (record && pad < NPADS) recordHit (pad, chop, semi, g);
}

void Engine::handle (const Command& c)
{
    switch (c.type)
    {
        case Command::Hit: hit (c.pad, c.chop, c.semi, c.vel, c.choke, c.record, c.hold); break;
        case Command::Release:
            if (c.hold >= 0 && c.hold < 256) { auto& h = held[(size_t) c.hold]; if (h && cur && cur->s.gate) releaseTid (h, 0.06f); h = 0; }
            break;
        case Command::Preview: releaseLive (0.02f); if (cur) startVoice (*cur, NPADS, -1, 0, 0.9f, -1, true, 0, -1, false); break;
        case Command::Play: startPlay (c.count); break;
        case Command::Stop: stopPlay(); for (auto& r : repeats) r.on = false; break;
        case Command::Panic: killAll (0.03f); for (auto& r : repeats) r.on = false; break;
        case Command::RepeatStart:
            if (c.hold >= 0 && c.hold < 16)
            {
                auto& r = repeats[(size_t) c.hold];
                r.on = true; r.pad = c.pad; r.chop = c.chop; r.semi = c.semi; r.vel = c.vel; r.record = c.record; r.choke = c.choke;
                r.next = (double) now;   // first hit right away
            }
            break;
        case Command::RepeatVel: if (c.hold >= 0 && c.hold < 16) repeats[(size_t) c.hold].vel = c.vel; break;
        case Command::RepeatStop: if (c.hold >= 0 && c.hold < 16) repeats[(size_t) c.hold].on = false; break;
        case Command::CaptureClick:
        {
            const double spb = 60.0 / (cur ? cur->s.bpm : 84.0f) * sr;
            const double t0 = (double) now + 0.06 * sr;
            for (int i = 0; i < 4; ++i) addClick (t0 + i * spb, i == 0);
            capStartAt = t0 + 4 * spb;
            break;
        }
        default: break;
    }
}

void Engine::runRepeats (double until, double& earliest)
{
    if (! cur) return;
    const double step = 60.0 / (effBpm > 0 ? effBpm : cur->s.bpm) / 4.0 * sr;
    const double iv = std::max (0.05, (double) repeatSteps.load()) * step;
    for (auto& r : repeats)
    {
        if (! r.on) continue;
        if (r.next <= until)
        {
            hit (r.pad, r.chop, r.semi, r.vel, r.choke, r.record, -1);
            // follow the sequencer grid while it plays, so repeats land on the beat
            double nx = r.next + iv;
            if (seqPlaying && lastStep >= 0)
            {
                const double base = lastStepTime - lastStep * step;      // where this pattern pass started
                const double k = std::floor (((double) now - base) / iv + 1e-6) + 1;
                nx = base + k * iv;
                if (nx <= (double) now) nx += iv;
            }
            r.next = nx;
        }
        earliest = std::min (earliest, r.next);
    }
}

// ------------------------------------------------------------------ sequencer
void Engine::startPlay (int countBeats)
{
    if (! cur) return;
    if (seqPlaying) { npend = 0; killAll (0.03f); }
    const Snapshot& s = *cur;
    const bool song = ! planMode && s.s.song && ! s.chain.empty();
    const int p0 = planMode ? (plan.empty() ? 0 : plan[0]) : song ? s.chain[0].pat : s.s.pat;
    const double beat = 60.0 / s.bpmOf (p0) * sr;
    const double t0 = planMode ? (double) now : (double) now + 0.05 * sr;
    effBpm = s.bpmOf (p0);
    seqPlaying = true; seqStep = 0; ci = 0; rep = 0; seqPat = p0; lastStep = -1; planIdx = 0; planDone = false;
    nextStep = t0 + countBeats * beat;
    countInEnd = nextStep;
    finishAt = -1;
    skips.clear();
    for (int i = 0; i < countBeats; ++i) addClick (t0 + i * beat, i == 0);
    playing.store (true);
}

void Engine::stopPlay()
{
    seqPlaying = false; npend = 0; nclicks = 0; finishAt = -1;
    killAll (0.03f);
    curStep.store (-1); playing.store (false); countInLeft.store (0);
    lastStep = -1; effBpm = 0;
}

void Engine::scheduleStep (int pat, int step, double t)
{
    const Snapshot& s = *cur;
    const double sd = 60.0 / s.bpmOf (pat) / 4.0 * sr;
    for (int i = 0; i < (int) s.tracks.size(); ++i)
    {
        const Track& T = s.tracks[(size_t) i];
        if (T.mute || (s.trackSoloAny && ! T.solo)) continue;
        const PatternData* d = T.find (pat);
        if (d == nullptr) continue;
        const int pad = d->pad >= 0 ? d->pad : T.pad, chop = d->chop >= -1 ? d->chop : T.chop;
        if (pad < 0 || pad >= NPADS || ! s.pads[(size_t) pad].has) continue;
        if (d->steps[(size_t) step] && npend < (int) pend.size())
            pend[(size_t) npend++] = { t, i, d->semi[(size_t) step], T.vol * 0.9f, pad, d->chopAt[(size_t) step] >= -1 ? d->chopAt[(size_t) step] : chop };
        for (const auto& e : d->ev)
        {
            if (e.t / TPS != step || npend >= (int) pend.size()) continue;
            const int ec = e.c >= -1 ? e.c : chop;
            bool skip = false;
            for (size_t k = 0; k < skips.size(); ++k)
                if (skips[k].pat == pat && skips[k].step == step && skips[k].pad == pad && (skips[k].chop == ec || skips[k].chop == e.c) && skips[k].semi == e.p)
                { skips.erase (skips.begin() + (long) k); skip = true; break; }
            if (skip) continue;
            pend[(size_t) npend++] = { t + (double) (e.t % TPS) / TPS * sd, i, e.p, T.vol * e.v, pad, ec };
        }
    }
    // stale skips (pattern changed, step passed)
    skips.erase (std::remove_if (skips.begin(), skips.end(), [pat, step] (const Skip& k) { return k.pat == pat && k.step == step; }), skips.end());
    if (! planMode && s.s.metro && step % 4 == 0) addClick (t, step == 0);
}

void Engine::advanceSequencer (double until)
{
    if (! cur) return;
    const Snapshot& s = *cur;
    while (seqPlaying && ! planDone && finishAt < 0 && nextStep <= until)
    {
        const bool song = ! planMode && s.s.song && ! s.chain.empty();
        if (song && ci >= (int) s.chain.size()) { ci = 0; rep = 0; seqPat = s.chain[0].pat; seqStep = 0; }
        const int pat = clampv (planMode ? plan[(size_t) planIdx] : song ? seqPat : s.s.pat, 0, NPAT - 1);
        const int len = clampv (s.plen[(size_t) pat], 16, MAXSTEP);
        if (seqStep >= len) seqStep = 0;
        const double sd = 60.0 / s.bpmOf (pat) / 4.0 * sr;
        if (seqStep == 0) effBpm = s.bpmOf (pat);
        scheduleStep (pat, seqStep, nextStep);
        lastStep = seqStep; lastPat = pat; lastStepTime = nextStep;
        curPat.store (pat); curStep.store (seqStep); songIndex.store (song ? ci : -1);
        nextStep += sd; ++seqStep;
        if (seqStep >= len)
        {
            if (planMode) { if (++planIdx >= (int) plan.size()) { planDone = true; break; } }
            else if (song)
            {
                if (++rep >= s.chain[(size_t) clampv (ci, 0, (int) s.chain.size() - 1)].rep)
                {
                    rep = 0; ++ci;
                    if (ci >= (int) s.chain.size()) { if (s.s.loop) ci = 0; else { finishAt = nextStep; break; } }
                    seqPat = s.chain[(size_t) ci].pat;
                }
            }
            else if (! s.s.loop) { finishAt = nextStep; break; }
            seqStep = 0;
        }
    }
}

// ------------------------------------------------------------------ capture
bool Engine::startCapture (bool fromInput, bool useThreshold, float thresholdDb, bool countIn, double maxSeconds)
{
    if (capState.load() != CAP_IDLE) return false;
    capMax = std::max (1024, (int) (maxSeconds * sr));
    capL.assign ((size_t) capMax, 0.0f); capR.assign ((size_t) capMax, 0.0f);
    capLen.store (0); capPreN = 0;
    capInput = fromInput; capThr = useThreshold; capThrLin = std::pow (10.0f, thresholdDb / 20.0f);
    capStartAt = 1.0e18;
    capState.store (countIn ? CAP_COUNT : (useThreshold ? CAP_WAIT : CAP_REC), std::memory_order_release);
    if (countIn) { Command c; c.type = Command::CaptureClick; push (c); }
    return true;
}

void Engine::requestCaptureStop()
{
    int s = capState.load();
    if (s == CAP_COUNT || s == CAP_WAIT || s == CAP_REC) capState.compare_exchange_strong (s, CAP_STOPREQ);
}

SamplePtr Engine::takeCapture()
{
    if (capState.load (std::memory_order_acquire) != CAP_STOPPED) return nullptr;
    const int n = capLen.load();
    SamplePtr res;
    if (n >= 256)
    {
        std::vector<float> l (capL.begin(), capL.begin() + n), r (capR.begin(), capR.begin() + n);
        bool same = true;
        for (int i = 0; i < n && same; ++i) same = std::fabs (l[(size_t) i] - r[(size_t) i]) < 1e-6f;
        res = makeSample (std::move (l), same ? std::vector<float>() : std::move (r), sr);
    }
    capL.clear(); capL.shrink_to_fit(); capR.clear(); capR.shrink_to_fit();
    capState.store (CAP_IDLE);
    return res;
}

void Engine::runCapture (const float* inL, const float* inR, const float* oL, const float* oR, int n)
{
    int st = capState.load (std::memory_order_acquire);
    if (st == CAP_IDLE || st == CAP_STOPPED) return;
    if (st == CAP_STOPREQ) { capState.store (CAP_STOPPED, std::memory_order_release); return; }
    static const float zeros[4096] = {};
    const float* sL = capInput ? (inL ? inL : zeros) : oL;
    const float* sR = capInput ? (inR ? inR : sL) : oR;
    auto pushAudio = [this] (const float* a, const float* b, int start, int count)
    {
        int len = capLen.load();
        const int k = std::min (count, capMax - len);
        if (k > 0) { memcpy (capL.data() + len, a + start, sizeof (float) * (size_t) k); memcpy (capR.data() + len, b + start, sizeof (float) * (size_t) k); len += k; capLen.store (len); }
        if (len >= capMax) { capState.store (CAP_STOPPED, std::memory_order_release); EngineEvent e; e.type = EngineEvent::CaptureFull; emit (e); }
    };
    const double blockStart = (double) now - n;
    int from = 0;
    if (st == CAP_COUNT)
    {
        if (blockStart + n <= capStartAt) return;
        from = clampv ((int) std::lround (capStartAt - blockStart), 0, n);
        st = capThr ? CAP_WAIT : CAP_REC;
        capState.store (st);
    }
    if (st == CAP_WAIT)
    {
        float p2 = 0;
        for (int i = from; i < n; ++i) p2 = std::max (p2, std::max (std::fabs (sL[i]), std::fabs (sR[i])));
        if (p2 >= capThrLin)
        {
            capState.store (CAP_REC);
            if (capPreN > 0) pushAudio (capPre[0], capPre[1], 0, capPreN);
            pushAudio (sL, sR, from, n - from);
        }
        else
        {
            const int k = std::min (1024, n - from);
            for (int i = 0; i < k; ++i) { capPre[0][i] = sL[n - k + i]; capPre[1][i] = sR[n - k + i]; }
            capPreN = k;
        }
        return;
    }
    if (st == CAP_REC) pushAudio (sL, sR, from, n - from);
}

// ------------------------------------------------------------------ processing
void Engine::render (int start, int n)
{
    if (n <= 0) return;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        if (v.padIdx >= 0 && v.padIdx < NPADS)
        {
            float* L = padBus.data() + (size_t) v.padIdx * 2 * (size_t) maxBlock;
            float* R = L + maxBlock;
            if (! padActive[(size_t) v.padIdx]) { std::fill (L, L + maxBlock, 0.0f); std::fill (R, R + maxBlock, 0.0f); padActive[(size_t) v.padIdx] = true; }
            renderVoice (v, L + start, R + start, n);
        }
        else renderVoice (v, mL.data() + start, mR.data() + start, n);
    }
}

static inline void peakInto (std::atomic<float>& a, float v) { float o = a.load (std::memory_order_relaxed); while (v > o && ! a.compare_exchange_weak (o, v)) {} }

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples)
{
    adoptSnapshot();
    int chunkStart = 0;
    bool first = true;
    while (chunkStart < numSamples)
    {
        const int n = std::min (maxBlock, numSamples - chunkStart);
        std::fill (mL.begin(), mL.begin() + n, 0.0f); std::fill (mR.begin(), mR.begin() + n, 0.0f);
        padActive.fill (false);
        if (cur)
            for (int i = 0; i < NPADS; ++i)
            {
                const auto& fx = cur->pads[(size_t) i].fx;
                if (fx.type == FT_LP) padFilter[(size_t) i].setLP (fx.cut, fx.qValue(), sr);
                else if (fx.type == FT_HP) padFilter[(size_t) i].setHP (fx.cut, fx.qValue(), sr);
                else if (fx.type == FT_BP) padFilter[(size_t) i].setBP (fx.cut, fx.qValue(), sr);
            }
        if (first) { Command c; while (cmds.pop (c)) handle (c); first = false; }

        int done = 0;
        while (done < n)
        {
            if (finishAt >= 0 && (double) now >= finishAt) { stopPlay(); EngineEvent e; e.type = EngineEvent::Finished; emit (e); }
            advanceSequencer ((double) now);
            double earliest = 1.0e300;
            for (int k = 0; k < npend;)
            {
                if (pend[(size_t) k].time <= (double) now)
                {
                    const auto P = pend[(size_t) k];
                    pend[(size_t) k] = pend[(size_t) --npend];
                    if (cur && P.trk < (int) cur->tracks.size()) startVoice (*cur, P.pad, P.chop, P.semi, P.gain, P.trk, false, 0, -1, false);
                    continue;
                }
                earliest = std::min (earliest, pend[(size_t) k].time);
                ++k;
            }
            runRepeats ((double) now, earliest);
            int next = n;
            if (seqPlaying && ! planDone && finishAt < 0) next = std::min (next, done + std::max (1, (int) std::ceil (nextStep - (double) now)));
            if (finishAt >= 0) next = std::min (next, done + std::max (1, (int) std::ceil (finishAt - (double) now)));
            if (earliest < 1.0e299) next = std::min (next, done + std::max (1, (int) std::ceil (earliest - (double) now)));
            if (next <= done) next = done + 1;
            next = std::min (next, n);
            render (done, next - done);
            now += next - done;
            done = next;
        }

        // pad channel strips: built-in FX rack, balance, mute/solo -> master
        const double fxBpm = effBpm > 0 ? effBpm : (cur ? cur->s.bpm : 84.0);
        if (cur)
            for (int i = 0; i < NPADS; ++i)
            {
                const RPad& p = cur->pads[(size_t) i];
                PadFx* fx = p.fxOn ? fxPtr[(size_t) i].load (std::memory_order_acquire) : nullptr;
                float* L = padBus.data() + (size_t) i * 2 * (size_t) maxBlock;
                float* R = L + maxBlock;
                if (! padActive[(size_t) i])
                {
                    if (fx == nullptr || fx->quietSamples > (int) (sr * 3.0)) continue;
                    std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
                }
                if (fx != nullptr)
                {
                    fx->process (L, R, n, p.fx, fxBpm);
                    if (padActive[(size_t) i] || fx->lastOutputLevel > 1.0e-5f) fx->quietSamples = 0; else fx->quietSamples += n;
                }
                if (std::fabs (p.pan) > 0.001f)
                {
                    const float gl = p.pan > 0 ? 1.0f - p.pan : 1.0f, gr = p.pan < 0 ? 1.0f + p.pan : 1.0f;
                    for (int k = 0; k < n; ++k) { L[k] *= gl; R[k] *= gr; }
                }
                if (p.muted || (cur->soloAny && ! p.solo)) continue;
                float pk = 0;
                for (int k = 0; k < n; ++k) { mL[(size_t) k] += L[k]; mR[(size_t) k] += R[k]; pk = std::max (pk, std::max (std::fabs (L[k]), std::fabs (R[k]))); }
                peakInto (padPeak[(size_t) i], pk);
            }

        // metronome / count-in clicks
        {
            const double blockStart = (double) now - n;
            for (int k = 0; k < nclicks;)
            {
                const Click c = clicks[(size_t) k];
                const double f = c.accent ? 1600.0 : 1000.0;
                for (int i = 0; i < n; ++i)
                {
                    const double dt = (blockStart + i - c.time) / sr;
                    if (dt < 0 || dt > 0.06) continue;
                    const double g = dt < 0.002 ? 0.0001 * std::pow (3500.0, dt / 0.002) : 0.35 * std::pow (0.0001 / 0.35, (dt - 0.002) / 0.048);
                    const float v = (float) (std::sin (6.283185307179586 * f * dt) * g);
                    mL[(size_t) i] += v; mR[(size_t) i] += v;
                }
                if ((blockStart + n - c.time) / sr > 0.06) { clicks[(size_t) k] = clicks[(size_t) --nclicks]; continue; }
                ++k;
            }
            if (seqPlaying && countInEnd > (double) now && effBpm > 0) countInLeft.store ((int) std::ceil ((countInEnd - (double) now) / (60.0 / effBpm * sr)));
            else if (capState.load (std::memory_order_relaxed) == CAP_COUNT && capStartAt < 1.0e17 && cur)
                countInLeft.store (std::max (0, (int) std::ceil ((capStartAt - (double) now) / (60.0 / cur->s.bpm * sr))));
            else countInLeft.store (0);
        }

        // master: 0.85 -> peak limiter -> master volume
        const float og = (cur ? cur->s.mvol : 0.85f) / 0.85f;
        const float rel = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
        float pl = 0, pr = 0;
        const float* iL = inL ? inL + chunkStart : nullptr;
        const float* iR = inR ? inR + chunkStart : iL;
        const float mon = monitor.load (std::memory_order_relaxed), ig = inputGain.load (std::memory_order_relaxed);
        static thread_local std::vector<float> gIn[2];
        if (iL && (int) gIn[0].size() < n) { gIn[0].resize ((size_t) maxBlock); gIn[1].resize ((size_t) maxBlock); }
        float ipk[2] = { 0, 0 };
        if (iL)
            for (int i = 0; i < n; ++i)
            {
                gIn[0][(size_t) i] = clampv (iL[i] * ig, -1.0f, 1.0f); gIn[1][(size_t) i] = clampv (iR[i] * ig, -1.0f, 1.0f);
                ipk[0] = std::max (ipk[0], std::fabs (gIn[0][(size_t) i])); ipk[1] = std::max (ipk[1], std::fabs (gIn[1][(size_t) i]));
            }
        for (int i = 0; i < n; ++i)
        {
            float l = mL[(size_t) i] * 0.85f, r = mR[(size_t) i] * 0.85f;
            const float x = std::max (std::fabs (l), std::fabs (r));
            const float target = x > 0.98f ? 0.98f / x : 1.0f;
            limEnv = target < limEnv ? target : limEnv + (target - limEnv) * rel;
            l = clampv (l * limEnv * og, -1.0f, 1.0f); r = clampv (r * limEnv * og, -1.0f, 1.0f);
            if (iL && mon > 0) { l = clampv (l + gIn[0][(size_t) i] * mon, -1.0f, 1.0f); r = clampv (r + gIn[1][(size_t) i] * mon, -1.0f, 1.0f); }
            mL[(size_t) i] = l; mR[(size_t) i] = r;
            pl = std::max (pl, std::fabs (l)); pr = std::max (pr, std::fabs (r));
        }
        peakInto (outPeak[0], pl); peakInto (outPeak[1], pr);
        peakInto (inPeak[0], ipk[0]); peakInto (inPeak[1], ipk[1]);
        memcpy (outL + chunkStart, mL.data(), sizeof (float) * (size_t) n);
        memcpy (outR + chunkStart, mR.data(), sizeof (float) * (size_t) n);
        if (capState.load (std::memory_order_relaxed) != CAP_IDLE)
            runCapture (iL ? gIn[0].data() : nullptr, iL ? gIn[1].data() : nullptr, mL.data(), mR.data(), n);
        chunkStart += n;
    }
    blocks.fetch_add (1, std::memory_order_release);
    if (cur)
    {
        const int sel = cur->s.sel;
        const Voice* best = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.releasing && v.padIdx == sel && v.layer < 0 && (best == nullptr || v.order > best->order)) best = &v;
        playhead.store (best ? (float) (best->pos / std::max (1, best->len)) : -1.0f);
        playheadChop.store (best ? best->chop : -1);
    }
}

// ------------------------------------------------------------------ offline render (export)
std::vector<float> Engine::renderPlan (SnapshotPtr snap, const std::vector<int>& planIn, int onlyTrack, int& framesOut,
                                       const std::function<void (float)>& progress, bool trimTail)
{
    auto e = std::make_unique<Engine>();
    e->offline = true;
    e->prepare (SR, 1024);
    auto s = std::make_shared<Snapshot> (*snap);
    if (onlyTrack >= 0)
    {
        for (int i = 0; i < (int) s->tracks.size(); ++i) { s->tracks[(size_t) i].mute = i != onlyTrack; s->tracks[(size_t) i].solo = false; }
        s->trackSoloAny = false;
        for (auto& p : s->pads) p.solo = false;
        s->soloAny = false;
    }
    s->s.mvol = 0.85f;
    for (int i = 0; i < NPADS; ++i) if (s->pads[(size_t) i].fxOn) e->ensureFx (i);
    e->cur = s;
    e->plan = planIn; e->planMode = true;
    double songLen = 0;
    for (int p : planIn) songLen += s->plen[(size_t) p] * 60.0 / s->bpmOf (p) / 4.0;
    e->startPlay (0);
    const int total = (int) std::ceil ((songLen + 3.0) * SR);
    std::vector<float> out ((size_t) total * 2, 0.0f);
    std::vector<float> L (1024), R (1024);
    for (int pos = 0; pos < total; pos += 1024)
    {
        const int n = std::min (1024, total - pos);
        e->process (nullptr, nullptr, L.data(), R.data(), n);
        for (int i = 0; i < n; ++i) { out[(size_t) (pos + i) * 2] = L[(size_t) i]; out[(size_t) (pos + i) * 2 + 1] = R[(size_t) i]; }
        if (progress && (pos / 1024) % 64 == 0) progress ((float) pos / (float) total);
    }
    if (! trimTail) { framesOut = total; return out; }   // stems: all the same length (song + 3 s of tails)
    int last = 0;
    for (int i = total - 1; i >= 0; --i) if (std::fabs (out[(size_t) i * 2]) > 0.0005f || std::fabs (out[(size_t) i * 2 + 1]) > 0.0005f) { last = i; break; }
    framesOut = std::min (total, std::max ((int) std::floor (songLen * SR), last + (int) (0.02 * SR)));
    out.resize ((size_t) framesOut * 2);
    return out;
}
} // namespace cx
