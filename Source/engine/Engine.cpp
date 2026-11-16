#include "Engine.h"
#include <cmath>
#include <random>

namespace cx
{
// transport CCs (pseudocode 11): 94/115 play, 95/116 rec, 93/117 stop, 86/113 loop, 87/112 metro, 88/114 tap
static bool isTransportCC (int cc)
{
    switch (cc) { case 94: case 115: case 95: case 116: case 93: case 117: case 86: case 113: case 87: case 112: case 88: case 114: return true; default: return false; }
}

// ------------------------------------------------------------------ compressor
float Compressor::curve (float x) const
{
    // soft knee that starts at the threshold (Web Audio style: the knee lies above the threshold)
    const float T = threshold + knee * 0.5f, W = knee, R = std::max (1.0f, ratio);
    if (W > 0.0f && std::abs (x - T) * 2.0f <= W)
    {
        const float d = x - T + W * 0.5f;
        return x + (1.0f / R - 1.0f) * d * d / (2.0f * W);
    }
    if (x <= T) return x;
    return T + (x - T) / R;
}

void Compressor::process (float* L, float* R, int n) noexcept
{
    const float aA = std::exp (-1.0f / (float) (std::max (0.0001f, attack) * sr));
    const float aR = std::exp (-1.0f / (float) (std::max (0.001f, release) * sr));
    float mk = makeupDb;
    if (autoMakeup) mk += -0.6f * (curve (0.0f) - 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const float x = std::max (std::abs (L[i]), std::abs (R[i]));
        const float xdb = 20.0f * std::log10 (x + 1.0e-9f);
        const float target = std::min (0.0f, curve (xdb) - xdb);
        envDb = target < envDb ? aA * envDb + (1 - aA) * target : aR * envDb + (1 - aR) * target;
        const float g = std::pow (10.0f, (envDb + mk) / 20.0f);
        L[i] *= g; R[i] *= g;
    }
}

// ------------------------------------------------------------------ engine
Engine::Engine()
{
    for (auto& m : noteMap) m.store (MAP_NONE);
    for (auto& m : ccMap) m.store (MAP_NONE);
    for (auto& m : pcMap) m.store (MAP_NONE);
    for (auto& f : fxPtr) f.store (nullptr);
    for (auto& h : padHits) h.store (0);
    for (auto& p : padPeak) p.store (0);
    for (auto& p : retPeak) p.store (0);
}

Engine::~Engine()
{
    // nodes owned by this engine go before the rack that points at them
    for (auto& s : SlotId::all()) rack.at (s).store (nullptr);
    retiredNodes.clear();
    owned.clear();
}

// ------------------------------------------------------------------ plug-ins (UI thread)
PluginNode* Engine::addOwned (std::unique_ptr<PluginNode> node)
{
    if (! node) return nullptr;
    node->prepare (sr, maxBlock, false);
    const juce::ScopedLock sl (pluginLock);
    owned.push_back (std::move (node));
    return owned.back().get();
}

void Engine::retire (PluginNode* node)
{
    if (node == nullptr) return;
    for (auto& s : SlotId::all())
    {
        PluginNode* expected = node;
        rack.at (s).compare_exchange_strong (expected, nullptr);
    }
    const juce::ScopedLock sl (pluginLock);
    for (auto it = owned.begin(); it != owned.end(); ++it)
        if (it->get() == node)
        {
            retiredNodes.push_back ({ std::move (*it), blockCount.load() });
            owned.erase (it);
            break;
        }
}

void Engine::collectGarbage()
{
    const juce::ScopedLock sl (pluginLock);
    const auto b = blockCount.load();
    const bool live = running.load();
    retiredNodes.erase (std::remove_if (retiredNodes.begin(), retiredNodes.end(),
                                        [b, live] (const Retired& r) { return ! live || b > r.at + 2; }), retiredNodes.end());
}

std::vector<PluginNode*> Engine::ownedNodes()
{
    const juce::ScopedLock sl (pluginLock);
    std::vector<PluginNode*> v;
    for (auto& o : owned) v.push_back (o.get());
    return v;
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (512, maxBlockSize);
    padBus.setSize (NPADS * 2, maxBlock);
    masterBus.setSize (2, maxBlock);
    sendBus.setSize (NSEND * 2, maxBlock);
    instBus.setSize (2, maxBlock);
    padBus.clear(); masterBus.clear(); sendBus.clear(); instBus.clear();
    {
        const juce::ScopedLock sl (pluginLock);
        for (auto& o : owned) o->prepare (sr, maxBlock, false);
    }
    nNoteOffs = 0; padHeldNote.fill (0); midiPadHeld.fill (0);
    for (auto& k : keyInst) k.fill (0);

    // vinyl noise loops (2 s), same recipe as vinylNoise() in the web version
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
    for (int i = 0; i < 64; ++i)
    {
        const float f = (float) i / 64.0f;
        crackle[(size_t) i] *= f; crackle[(size_t) (n - 1 - i)] *= f; hiss[(size_t) i] *= f; hiss[(size_t) (n - 1 - i)] *= f;
    }

    la.sr = lim.sr = sr; la.reset(); lim.reset();
    la.autoMakeup = true;
    lim.threshold = -4; lim.knee = 0; lim.ratio = 20; lim.attack = 0.002f; lim.release = 0.08f;
    lim.autoMakeup = true;
    look = LookAhead(); look.la = juce::jlimit (1, LookAhead::MAXLA - 1, (int) std::round (sr * 0.0015));

    {
        const juce::ScopedLock sl (fxAllocLock);
        for (auto& f : fxOwn) if (f) f->prepare (sr, maxBlock);
    }
    for (auto& v : voices) { v.active = false; v.snap.reset(); }
    npend = 0; nclicks = 0; seqPlaying = false; playing.store (false);
}

void Engine::ensureFx (int pad)
{
    if (pad < 0 || pad >= NPADS || fxPtr[(size_t) pad].load() != nullptr) return;
    const juce::ScopedLock sl (fxAllocLock);
    auto fx = std::make_unique<PadFx>();
    fx->prepare (sr, maxBlock);
    fxPtr[(size_t) pad].store (fx.get(), std::memory_order_release);
    fxOwn[(size_t) pad] = std::move (fx);
}

void Engine::setSnapshot (SnapshotPtr s)
{
    const juce::SpinLock::ScopedLockType sl (snapLock);
    pending = std::move (s);
}

void Engine::adoptSnapshot()
{
    const juce::SpinLock::ScopedTryLockType tl (snapLock);
    if (tl.isLocked() && pending)
    {
        cur = std::move (pending);   // the old one is kept alive by the controller's release pool
        pending.reset();
        applyLA (cur->la);
    }
}

void Engine::applyLA (const LASettings& s)
{
    const bool on = s.on;
    la.threshold = on ? -(s.pr / 100.0f) * 40.0f : 0.0f;
    la.knee = s.limit ? 6.0f : 20.0f;
    la.ratio = on ? (s.limit ? 14.0f : 3.5f) : 1.0f;
    la.attack = s.limit ? 0.004f : 0.012f;
    la.release = s.limit ? 0.14f : 0.4f;
    la.makeupDb = on ? ((s.gain - 50.0f) / 50.0f * 12.0f) : 0.0f;
}

bool Engine::push (const Command& c)
{
    const juce::SpinLock::ScopedLockType sl (cmdWriteLock);
    int s1, n1, s2, n2;
    cmdFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 < 1) return false;
    cmdBuf[(size_t) (n1 > 0 ? s1 : s2)] = c;
    cmdFifo.finishedWrite (1);
    return true;
}

void Engine::emit (const EngineEvent& e)
{
    int s1, n1, s2, n2;
    evtFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 < 1) return;
    evtBuf[(size_t) (n1 > 0 ? s1 : s2)] = e;
    evtFifo.finishedWrite (1);
}

bool Engine::pop (EngineEvent& e)
{
    int s1, n1, s2, n2;
    evtFifo.prepareToRead (1, s1, n1, s2, n2);
    if (n1 + n2 < 1) return false;
    e = evtBuf[(size_t) (n1 > 0 ? s1 : s2)];
    evtFifo.finishedRead (1);
    return true;
}

float Engine::gainOf (float vel) const
{
    if (cur && ! cur->s.vel) return 0.9f;
    return std::max (0.12f, vel / 127.0f);
}

// ------------------------------------------------------------------ voices
void Engine::chokeVoice (Voice& v, float seconds)
{
    if (! v.active || (v.releasing && v.relStep >= 1.0f / (seconds * (float) sr))) return;
    v.releasing = true;
    v.relStep = 1.0f / std::max (1.0f, seconds * (float) sr);
}

void Engine::releaseTid (juce::uint32 tid, float seconds)
{
    if (tid == 0) return;
    for (auto& v : voices) if (v.active && v.tid == tid) chokeVoice (v, seconds);
}

void Engine::releaseLive (float seconds)
{
    for (auto& v : voices) if (v.active && v.live) chokeVoice (v, seconds);
    for (auto& h : held) h.fill (0);
}

void Engine::killAll (float seconds)
{
    for (auto& v : voices) if (v.active) chokeVoice (v, seconds);
    for (auto& h : held) h.fill (0);
    instAllOff();
}

// ------------------------------------------------------------------ plug-in instruments (audio thread)
void Engine::instNoteOn (int pad, float semi, int vel, double lenSamples, bool isHeld)
{
    PluginNode* in = instAt (pad);
    if (in == nullptr || ! cur) return;
    const RPad& p = cur->pads[(size_t) pad];
    if (p.muted) return;
    const int note = instNote (p, semi);
    in->addNote (note, vel, curOffset);
    padHits[(size_t) pad].fetch_add (1);
    if (isHeld) padHeldNote[(size_t) pad] = note + 1;
    else if (nNoteOffs < (int) noteOffs.size()) noteOffs[(size_t) nNoteOffs++] = { (double) now + juce::jmax (64.0, lenSamples), pad, note };
}

void Engine::instAllOff()
{
    for (int i = 0; i < NPADS; ++i)
        if (auto* in = instAt (i)) in->allNotesOff (curOffset);
    nNoteOffs = 0; padHeldNote.fill (0); midiPadHeld.fill (0);
    for (auto& k : keyInst) k.fill (0);
}

void Engine::releasePad (int gi)
{
    if (gi < 0 || gi >= NPADS) return;
    auto& h = padHeldNote[(size_t) gi];
    if (h != 0) { if (auto* in = instAt (gi)) in->addNote (h - 1, 0, curOffset); h = 0; }
}

juce::uint32 Engine::startVoice (const Snapshot& s, int padIdx, int chop, float semi, float gain, int trk, bool live,
                                 juce::uint32 tid, int layer, bool isLink)
{
    if (padIdx < 0 || padIdx > NPADS) return 0;   // NPADS = library preview slot
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

    const double rate = std::pow (2.0, (juce::jlimit (-PR, PR, semi + p.pitch) + slot.pitch) / 12.0);

    // choke: same track and (same mute group, or the very same sound)
    for (auto& o : voices)
    {
        if (! o.active || o.tid == tid || o.trk != trk || o.startTime > now) continue;
        if ((p.mg != 0 && o.pad->mg == p.mg) || (o.padIdx == padIdx && o.layer == layer && o.chop == chop && o.semi == semi))
            chokeVoice (o, 0.012f);
    }

    // find a free voice (or steal the oldest)
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& x : voices) if (x.order < v->order) v = &x;
    }
    Voice& V = *v;
    V = Voice();
    V.active = true; V.snap = cur; V.pad = &p; V.slot = &slot;
    V.padIdx = padIdx; V.layer = layer; V.chop = chop; V.trk = trk; V.semi = semi; V.tid = tid; V.live = live;
    V.src[0] = buf->audio.getReadPointer (0);
    V.src[1] = buf->audio.getReadPointer (juce::jmin (1, buf->channels() - 1));
    V.stereoSrc = buf->channels() > 1;
    V.pos = off * buf->sampleRate;
    V.endPos = std::min ((double) buf->length() - 1, (off + dur) * buf->sampleRate);
    V.rate = rate * buf->sampleRate / sr;
    V.gain = gain;
    V.atk = juce::jmax (1, (int) (0.002 * sr));
    const double durOut = dur / rate;
    V.finSamples = (float) (std::min (0.005, durOut / 4.0) * sr);
    if (slot.ft != FT_OFF)
    {
        V.hasSlotFilter = true;
        if (slot.ft == FT_LP) V.sfc.setLP (slot.fc, 0.9, sr);
        else if (slot.ft == FT_HP) V.sfc.setHP (slot.fc, 0.9, sr);
        else V.sfc.setBP (slot.fc, 0.9, sr);
    }
    if (std::abs (slot.pan) > 0.001f)
    {
        V.hasPan = true; V.panX = juce::jlimit (-1.0f, 1.0f, slot.pan);
        const float halfPi = juce::MathConstants<float>::halfPi;
        if (V.stereoSrc)
        {
            const float x = V.panX <= 0 ? V.panX + 1.0f : V.panX;
            V.panL = std::cos (x * halfPi); V.panR = std::sin (x * halfPi);
        }
        else
        {
            const float x = (V.panX + 1.0f) * 0.5f;
            V.panL = std::cos (x * halfPi); V.panR = std::sin (x * halfPi);
        }
    }
    if (p.fx.vinOn && layer < 0 && ! crackle.empty())
    {
        V.vinyl = true; V.crk = p.fx.vinCrackle * 0.5f; V.hiss = p.fx.vinHiss * 0.05f;
        V.noisePos = (int) ((tid * 2654435761u) % (juce::uint32) crackle.size());
    }
    V.startTime = now; V.order = ++voiceOrder;
    if (layer < 0 && padIdx < NPADS) padHits[(size_t) padIdx].fetch_add (1);

    if (layer < 0)
    {
        for (int i = 0; i < (int) p.layers.size(); ++i)
            startVoice (s, padIdx, chop, semi, g0, trk, live, tid, i, isLink);
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
    const SampleData* b = v.slot->buf.get();
    const int len = b->length();
    const double rate = v.rate * (v.live ? std::pow (2.0, bend.load (std::memory_order_relaxed) * 200.0 / 1200.0) : 1.0);
    const bool hasPF = v.pad->fx.type != FT_OFF;
    const BQ& pc = padFilter[(size_t) v.padIdx];
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
                else             { const float nl = l * v.panL, nr = r + l * v.panR; l = nl; r = nr; }
            }
            else { const float m = l; l = m * v.panL; r = m * v.panR; }
        }
        float e = v.age < v.atk ? (float) v.age / (float) v.atk : 1.0f;
        const float remain = (float) ((v.endPos - v.pos) / rate);
        if (remain < v.finSamples) e = std::min (e, remain / std::max (1.0f, v.finSamples));
        if (v.releasing)
        {
            v.relLevel -= v.relStep;
            if (v.relLevel <= 0.0f) { v.active = false; break; }
            e *= v.relLevel;
        }
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
    if (! v.active) v.snap.reset();   // never the last reference (see Controller release pool)
}

// ------------------------------------------------------------------ notes and pads
void Engine::recordHit (int pad, int chop, float semi, float gain, int)
{
    if (! cur || ! seqPlaying || lastStep < 0 || ! recordArmed.load()) return;
    const double sd = 60.0 / cur->bpmOf (lastPat) / 4.0 * sr;
    const double hit = (double) now - outLatency.load() - (limiterOn ? look.la : 0);
    EngineEvent e; e.type = EngineEvent::RecHit;
    e.a = pad; e.b = chop; e.c = lastPat; e.f = semi; e.g = gain;
    e.pos = lastStep + (hit - lastStepTime) / sd;
    emit (e);
}

void Engine::noteOn (int idx, float vel, int tag, int offset)
{
    if (! cur) return;
    const Snapshot& s = *cur;
    const int sel = juce::jlimit (0, NPADS - 1, s.s.sel);
    const RPad& p = s.pads[(size_t) sel];
    const int KB = s.s.keys == 32 ? 53 : 48;
    const int m = KB + idx + 12 * appOct.load();
    const float g = gainOf (vel);
    juce::uint32 tid = 0;
    PluginNode* in = instAt (sel);
    if (p.keyMode == MODE_KEYS || (in != nullptr && ! p.has))
    {
        if (! p.has && in == nullptr) return;
        const float semi = (float) (m - s.s.root);
        if (p.has)
        {
            if (! p.muted && s.s.choke) releaseLive (0.02f);
            tid = startVoice (s, sel, -1, semi, g, -1, true, 0, -1, false);
        }
        if (in != nullptr && ! p.muted && tag >= 0 && tag < 3)
        {
            auto& k = keyInst[(size_t) tag][(size_t) (idx & 255)];
            if (k != 0) { if (auto* o = instAt ((k - 1) / 128)) o->addNote ((k - 1) % 128, 0, curOffset); }
            const int note = instNote (p, semi);
            in->addNote (note, s.s.vel ? juce::jlimit (1, 127, (int) std::round (vel)) : 100, curOffset);
            k = sel * 128 + note + 1;
        }
        recordHit (sel, -1, semi, g, offset);
    }
    else
    {
        const int ci = p.chopForKey (idx);
        if (! p.has || ci < 0 || ci >= p.numChops()) return;
        if (! p.muted && s.s.choke) releaseLive (0.02f);
        tid = startVoice (s, sel, ci, 0, g, -1, true, 0, -1, false);
        EngineEvent e; e.type = EngineEvent::Transport; e.a = 100; e.b = sel; e.c = ci; emit (e);   // chop played
        recordHit (sel, ci, 0, g, offset);
    }
    if (tag >= 0 && tag < 3) held[(size_t) tag][(size_t) (idx & 255)] = tid;
}

void Engine::noteOff (int idx, int tag)
{
    if (tag < 0 || tag >= 3) return;
    auto& h = held[(size_t) tag][(size_t) (idx & 255)];
    if (h != 0 && cur && cur->s.gate) releaseTid (h, 0.06f);
    h = 0;
    auto& k = keyInst[(size_t) tag][(size_t) (idx & 255)];   // plug-in instruments always get their note-off
    if (k != 0) { if (auto* o = instAt ((k - 1) / 128)) o->addNote ((k - 1) % 128, 0, curOffset); k = 0; }
}

void Engine::handleCommand (const Command& c, int offset)
{
    if (! cur && c.type != Command::Stop && c.type != Command::Panic) return;
    switch (c.type)
    {
        case Command::PadHit:
            hitPadLive (juce::jlimit (0, NPADS - 1, c.pad), c.vel, c.chop, c.semi, c.record, offset, c.tag == 1);
            break;
        case Command::PadRelease: releasePad (c.pad); break;
        case Command::Preview:
        {
            const Snapshot& s = *cur;
            releaseLive (0.02f);
            startVoice (s, juce::jlimit (0, NPADS, c.pad), c.chop, 0, 0.9f, -1, true, 0, -1, false);
            break;
        }
        case Command::NoteOn:  noteOn (c.note, c.vel, c.tag, offset); break;
        case Command::NoteOff: noteOff (c.note, c.tag); break;
        case Command::Play:    startPlay ((int) c.f); break;
        case Command::Stop:    stopPlay(); break;
        case Command::Panic:   killAll (0.03f); break;
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

void Engine::hitPadLive (int gi, float vel, int chop, float semi, bool record, int offset, bool isHeld)
{
    if (! cur) return;
    const Snapshot& s = *cur;
    const float g = gainOf (vel);
    if (s.pads[(size_t) gi].has)
    {
        if (! s.pads[(size_t) gi].muted && s.s.choke) releaseLive (0.02f);
        startVoice (s, gi, chop, semi, g, -1, true, 0, -1, false);
    }
    if (instAt (gi) != nullptr)
    {
        releasePad (gi);
        const double step = 60.0 / s.s.bpm / 4.0 * sr;
        instNoteOn (gi, semi, s.s.vel ? juce::jlimit (1, 127, (int) std::round (vel)) : 100, step, isHeld);
    }
    if (record) recordHit (gi, chop, semi, g, offset);
}

void Engine::handleMapped (int t, bool isCC, int d1, int d2, bool on, bool off, int offset)
{
    if (t >= MAP_KEY && t < MAP_KEY + 64)
    {
        const int idx = t - MAP_KEY;
        auto& held = isCC ? ccHeld[(size_t) d1] : midiHeld[(size_t) d1];
        if (on)
        {
            held = idx + 1000;
            EngineEvent e; e.type = EngineEvent::KeyLight; e.a = idx; e.b = 1; emit (e);
            noteOn (idx, isCC ? 100.0f : (float) d2, 2, offset);
        }
        else if (off && held != 0)
        {
            noteOff (held - 1000, 2);
            EngineEvent e; e.type = EngineEvent::KeyLight; e.a = held - 1000; e.b = 0; emit (e);
            held = 0;
        }
        return;
    }
    if (t >= MAP_PAD && t < MAP_PAD + 8)
    {
        if (! on)
        {
            if (off && ! isCC && midiPadHeld[(size_t) d1] != 0) { releasePad (midiPadHeld[(size_t) d1] - 1); midiPadHeld[(size_t) d1] = 0; }
            return;
        }
        const int gi = juce::jlimit (0, NPADS - 1, uiBank.load() * 8 + (t - MAP_PAD));
        hitPadLive (gi, isCC ? 100.0f : (float) d2, -1, 0, true, offset, ! isCC);
        if (! isCC) midiPadHeld[(size_t) d1] = gi + 1;
        EngineEvent e; e.type = EngineEvent::PadSelect; e.a = gi; emit (e);
        return;
    }
    // a button, knob, slider or bank: the UI thread does it
    if (on || isCC)
    {
        EngineEvent e; e.type = EngineEvent::MapFire; e.a = isCC ? 1 : 0; e.b = d1; e.c = d2;
        emit (e);
    }
}

void Engine::handleMidi (const juce::uint8* d, int sz, int offset)
{
    if (sz < 1) return;
    const int st0 = d[0];
    if (st0 == 0xF8 || st0 == 0xFE) return;
    EngineEvent led; led.type = EngineEvent::MidiIn;
    led.a = st0; led.b = sz > 1 ? d[1] : -1; led.c = sz > 2 ? d[2] : -1;
    emit (led);
    if (st0 == 0xFA || st0 == 0xFB) { EngineEvent e; e.type = EngineEvent::Transport; e.a = 1; emit (e); return; }
    if (st0 == 0xFC) { EngineEvent e; e.type = EngineEvent::Transport; e.a = 2; emit (e); return; }
    if (st0 == 0xF0)
    {
        if (sz >= 5 && d[1] == 0x7F && d[3] == 0x06)
        {
            const int c = d[4];
            EngineEvent e; e.type = EngineEvent::Transport;
            e.a = (c == 2 || c == 3) ? 1 : c == 1 ? 2 : (c == 6 || c == 0x44) ? 3 : 0;
            if (e.a) emit (e);
        }
        return;
    }
    if (sz < 2) return;
    const int st = st0 & 0xF0, d1 = d[1] & 127, d2 = sz > 2 ? d[2] : 0;
    const bool on = st == 0x90 && d2 > 0, off = st == 0x80 || (st == 0x90 && d2 == 0), cc = st == 0xB0;
    // MIDI learn: report the control instead of playing it
    const bool pc = st == 0xC0;   // program change: some controllers' buttons send these
    if (learnArmed.load() && (on || cc || pc))
    {
        EngineEvent e; e.type = EngineEvent::Learn; e.a = cc ? 1 : pc ? 2 : 0; e.b = d1; e.c = d2; emit (e);
        return;
    }
    if (pc)
    {
        const int t = pcMap[(size_t) d1].load (std::memory_order_relaxed);
        if (t >= MAP_PAD && t < MAP_PAD + 8) handleMapped (t, true, d1, 127, true, false, offset);
        else if (t != MAP_NONE) { EngineEvent e; e.type = EngineEvent::MapFire; e.a = 2; e.b = d1; e.c = 127; emit (e); }
        return;
    }
    // "set low key": this note becomes the lowest key on screen
    if (lowKeyLearn.load() && on && cur)
    {
        const int KB = cur->s.keys == 32 ? 53 : 48;
        midiOffset.store (KB - d1); hwOct.store (0); lowKeyLearn.store (false);
        EngineEvent e; e.type = EngineEvent::LowKey; e.a = d1; emit (e);
        return;
    }
    // learned links win over the built-in behaviour
    if (on || off)
    {
        const int t = noteMap[(size_t) d1].load (std::memory_order_relaxed);
        if (t != MAP_NONE) { handleMapped (t, false, d1, d2, on, off, offset); return; }
    }
    if (cc)
    {
        const int t = ccMap[(size_t) d1].load (std::memory_order_relaxed);
        if (t != MAP_NONE) { handleMapped (t, true, d1, d2, d2 > 0, d2 == 0, offset); return; }
    }
    if (cc && isTransportCC (d1) && defaultTransportCC.load())
    {
        EngineEvent e; e.type = EngineEvent::TransportCC; e.a = d1; e.b = d2; emit (e);
        return;
    }
    if (st == 0x90 && d2 > 0)
    {
        if (! cur) return;
        const int NK = cur->s.keys, KB = NK == 32 ? 53 : 48;
        int r = d1 + midiOffset.load() - KB;
        int idx;
        if (followOct.load())
        {
            int hs = hwOct.load();
            if (r - 12 * hs < 0 || r - 12 * hs >= NK)
            {
                const int lo = (int) std::ceil ((r - NK + 1) / 12.0), hi = (int) std::floor (r / 12.0);
                const int nh = juce::jmax (lo, juce::jmin (hi, hs));
                const int oct = appOct.load();
                const int no = juce::jlimit (-3, 3, oct + (nh - hs));
                hs = hs + (no - oct);
                hwOct.store (hs);
                if (no != oct) { appOct.store (no); EngineEvent e; e.type = EngineEvent::OctFollow; e.a = no; e.b = hs; emit (e); }
            }
            idx = r - 12 * hs;
        }
        else
        {
            const int pc = ((r % 12) + 12) % 12;
            idx = pc + 12 * juce::jmax (0, juce::jmin ((NK - 1 - pc) / 12, (int) std::floor (r / 12.0)));
        }
        midiHeld[(size_t) d1] = idx + 1000;
        EngineEvent e; e.type = EngineEvent::KeyLight; e.a = idx; e.b = 1; emit (e);
        noteOn (idx, (float) d2, 2, offset);
    }
    else if (st == 0x80 || (st == 0x90 && d2 == 0))
    {
        const int h = midiHeld[(size_t) d1];
        if (h != 0)
        {
            const int idx = h - 1000;
            noteOff (idx, 2);
            EngineEvent e; e.type = EngineEvent::KeyLight; e.a = idx; e.b = 0; emit (e);
            midiHeld[(size_t) d1] = 0;
        }
    }
    else if (st == 0xB0)
    {
        int n = -1;
        if (d1 >= 14 && d1 <= 21) n = d1 - 14; else if (d1 >= 70 && d1 <= 77) n = d1 - 70;
        EngineEvent e;
        if (n >= 0) { e.type = EngineEvent::PitchKnob; e.a = n; e.b = d2; }
        else if (d1 == 1) { e.type = EngineEvent::ModWheel; e.f = d2 / 63.5f - 1.0f; }
        else { e.type = EngineEvent::TransportCC; e.a = d1; e.b = d2; e.c = 1; }   // c = 1: unmapped CC (status only)
        emit (e);
        // mod wheel, sustain, expression ... also go to the selected pad's plug-in instrument
        if (n < 0 && cur)
            if (auto* in = instAt (cur->s.sel)) in->addMidi (juce::MidiMessage::controllerEvent (1, d1, d2), curOffset);
    }
    else if (st == 0xE0)
    {
        const float v = (float) (((d2 << 7) | d1) - 8192) / 8192.0f;
        bend.store (v);
        EngineEvent e; e.type = EngineEvent::Bend; e.f = v; emit (e);
        if (cur)
            if (auto* in = instAt (cur->s.sel)) { in->addMidi (juce::MidiMessage::pitchWheel (1, (d2 << 7) | d1), curOffset); lastBendSent = v; }
    }
    else if ((st == 0xD0 || st == 0xA0) && cur)
    {
        if (auto* in = instAt (cur->s.sel))
            in->addMidi (st == 0xD0 ? juce::MidiMessage::channelPressureChange (1, d1) : juce::MidiMessage::aftertouchChange (1, d1, d2), curOffset);
    }
}

// ------------------------------------------------------------------ sequencer
void Engine::addClick (double t, bool accent)
{
    if (nclicks < (int) clicks.size()) clicks[(size_t) nclicks++] = { t, accent };
}

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
        if (pad < 0 || pad >= NPADS || (! s.pads[(size_t) pad].has && instAt (pad) == nullptr)) continue;
        if (d->steps[(size_t) step] && npend < (int) pend.size())
            pend[(size_t) npend++] = { t, i, d->semi[(size_t) step], T.vol * 0.9f, sd * 0.95, pad, d->chopAt[(size_t) step] >= -1 ? d->chopAt[(size_t) step] : chop };
        for (const auto& e : d->ev)
            if (e.t / TPS == step && npend < (int) pend.size())
                pend[(size_t) npend++] = { t + (double) (e.t % TPS) / TPS * sd, i, e.p, T.vol * e.v, sd * 0.95, pad, e.c >= -1 ? e.c : chop };
    }
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
        const int pat = juce::jlimit (0, NPAT - 1, planMode ? plan[(size_t) planIdx] : song ? seqPat : s.s.pat);
        const int len = juce::jlimit (16, MAXSTEP, s.plen[(size_t) pat]);
        if (seqStep >= len) seqStep = 0;
        const double sd = 60.0 / s.bpmOf (pat) / 4.0 * sr;
        if (seqStep == 0) effBpm = s.bpmOf (pat);
        scheduleStep (pat, seqStep, nextStep);
        lastStep = seqStep; lastPat = pat; lastStepTime = nextStep;
        curPat.store (pat); curStep.store (seqStep);
        nextStep += sd; ++seqStep;
        if (seqStep >= len)
        {
            if (planMode)
            {
                if (++planIdx >= (int) plan.size()) { planDone = true; planEnd = nextStep; break; }
            }
            else if (song)
            {
                if (++rep >= s.chain[(size_t) juce::jlimit (0, (int) s.chain.size() - 1, ci)].rep)
                {
                    rep = 0; ++ci;
                    if (ci >= (int) s.chain.size())
                    {
                        if (s.s.loop) ci = 0;
                        else { finishAt = nextStep; break; }
                    }
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
    capMax = juce::jmax (1024, (int) (maxSeconds * sr));
    capBuf.setSize (2, capMax, false, true, false);
    capLen.store (0); capLevel.store (0); capPreN = 0;
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
        juce::AudioBuffer<float> b (2, n);
        b.copyFrom (0, 0, capBuf, 0, 0, n);
        b.copyFrom (1, 0, capBuf, 1, 0, n);
        res = makeSample (std::move (b), sr);
    }
    capBuf.setSize (0, 0);
    capState.store (CAP_IDLE);
    return res;
}

void Engine::runCapture (const float* const* in, int numIn, const float* oL, const float* oR, int n)
{
    int st = capState.load (std::memory_order_acquire);
    if (st == CAP_IDLE || st == CAP_STOPPED) return;
    if (st == CAP_STOPREQ) { capState.store (CAP_STOPPED, std::memory_order_release); return; }
    const float* sL; const float* sR;
    static const float zeros[8192] = {};
    if (capInput)
    {
        sL = (numIn > 0 && in != nullptr && in[0] != nullptr) ? in[0] : zeros;
        sR = (numIn > 1 && in != nullptr && in[1] != nullptr) ? in[1] : sL;
        if (sL == zeros && n > 8192) return;
    }
    else { sL = oL; sR = oR; }

    float pk = 0;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (sL[i]), std::abs (sR[i])));
    float old = capLevel.load();
    while (pk > old && ! capLevel.compare_exchange_weak (old, pk)) {}

    auto pushAudio = [this] (const float* a, const float* b, int start, int count)
    {
        int len = capLen.load();
        const int k = juce::jmin (count, capMax - len);
        if (k > 0)
        {
            capBuf.copyFrom (0, len, a + start, k);
            capBuf.copyFrom (1, len, b + start, k);
            len += k; capLen.store (len);
        }
        if (len >= capMax)
        {
            capState.store (CAP_STOPPED, std::memory_order_release);
            EngineEvent e; e.type = EngineEvent::CaptureFull; emit (e);
        }
    };
    auto keepPre = [this] (const float* a, const float* b, int start, int count)
    {
        const int k = juce::jmin (1024, count);
        for (int i = 0; i < k; ++i) { capPre[0][i] = a[start + count - k + i]; capPre[1][i] = b[start + count - k + i]; }
        capPreN = k;
    };
    const double blockStart = (double) now - n;
    int from = 0;
    if (st == CAP_COUNT)
    {
        if (blockStart + n <= capStartAt) return;
        from = juce::jlimit (0, n, (int) std::round (capStartAt - blockStart));
        st = capThr ? CAP_WAIT : CAP_REC;
        capState.store (st);
    }
    if (st == CAP_WAIT)
    {
        float p2 = 0;
        for (int i = from; i < n; ++i) p2 = std::max (p2, std::max (std::abs (sL[i]), std::abs (sR[i])));
        if (p2 >= capThrLin)
        {
            capState.store (CAP_REC);
            if (capPreN > 0) pushAudio (capPre[0], capPre[1], 0, capPreN);
            pushAudio (sL, sR, from, n - from);
        }
        else keepPre (sL, sR, from, n - from);
        return;
    }
    if (st == CAP_REC) pushAudio (sL, sR, from, n - from);
}

// ------------------------------------------------------------------ mixer
static void peakInto (std::atomic<float>& a, const float* L, const float* R, int n)
{
    const float p = std::max (juce::FloatVectorOperations::findMaximum (L, n), -juce::FloatVectorOperations::findMinimum (L, n));
    const float q = std::max (juce::FloatVectorOperations::findMaximum (R, n), -juce::FloatVectorOperations::findMinimum (R, n));
    const float v = std::max (p, q);
    float o = a.load (std::memory_order_relaxed);
    while (v > o && ! a.compare_exchange_weak (o, v)) {}
}

void Engine::runStrips (int n)
{
    const double fxBpm = (effBpm > 0 ? effBpm : (cur ? cur->s.bpm : 84.0f));
    curBpm.store ((float) fxBpm);
    for (int k = 0; k < NSEND * 2; ++k) sendBus.clear (k, 0, n);
    std::array<bool, NSEND> sendUsed {};
    if (cur)
        for (int i = 0; i < NPADS; ++i)
        {
            const RPad& p = cur->pads[(size_t) i];
            PluginNode* in = instAt (i);
            PluginNode* ins[PAD_VFX];
            bool anyIns = false;
            for (int k = 0; k < PAD_VFX; ++k) { ins[k] = rack.padFx[(size_t) i][(size_t) k].load (std::memory_order_acquire); anyIns |= ins[k] != nullptr; }
            PadFx* fx = p.fxOn ? fxPtr[(size_t) i].load (std::memory_order_acquire) : nullptr;
            if (! padActive[(size_t) i])
            {
                const bool fxTail = fx != nullptr && fx->quietSamples <= (int) (sr * 3.0);
                if (in == nullptr && ! anyIns && ! fxTail) continue;
                padBus.clear (i * 2, 0, n); padBus.clear (i * 2 + 1, 0, n);
            }
            float* L = padBus.getWritePointer (i * 2);
            float* R = padBus.getWritePointer (i * 2 + 1);
            if (in != nullptr)
            {
                instBus.clear (0, 0, n); instBus.clear (1, 0, n);
                in->process (instBus.getWritePointer (0), instBus.getWritePointer (1), n);
                juce::FloatVectorOperations::addWithMultiply (L, instBus.getReadPointer (0), p.vol, n);
                juce::FloatVectorOperations::addWithMultiply (R, instBus.getReadPointer (1), p.vol, n);
            }
            if (fx != nullptr)
            {
                fx->process (L, R, n, p.fx, fxBpm);
                if (padActive[(size_t) i] || fx->lastOutputLevel > 1.0e-5f) fx->quietSamples = 0; else fx->quietSamples += n;
            }
            for (auto* node : ins) if (node != nullptr) node->process (L, R, n);
            // pan (balance), mute / solo
            if (std::abs (p.pan) > 0.001f)
            {
                juce::FloatVectorOperations::multiply (L, p.pan > 0 ? 1.0f - p.pan : 1.0f, n);
                juce::FloatVectorOperations::multiply (R, p.pan < 0 ? 1.0f + p.pan : 1.0f, n);
            }
            const bool audible = ! p.muted && (! cur->soloAny || p.solo);
            if (! audible) continue;
            peakInto (padPeak[(size_t) i], L, R, n);
            masterBus.addFrom (0, 0, L, n);
            masterBus.addFrom (1, 0, R, n);
            for (int s = 0; s < NSEND; ++s)
                if (p.send[(size_t) s] > 0.0001f)
                {
                    sendBus.addFrom (s * 2, 0, L, n, p.send[(size_t) s]);
                    sendBus.addFrom (s * 2 + 1, 0, R, n, p.send[(size_t) s]);
                    sendUsed[(size_t) s] = true;
                }
        }
    // returns
    for (int s = 0; s < NSEND; ++s)
    {
        bool anyNode = false;
        for (auto& a : rack.send[(size_t) s]) anyNode |= a.load (std::memory_order_relaxed) != nullptr;
        if (! sendUsed[(size_t) s] && ! anyNode) continue;
        float* L = sendBus.getWritePointer (s * 2);
        float* R = sendBus.getWritePointer (s * 2 + 1);
        for (auto& a : rack.send[(size_t) s]) if (auto* node = a.load (std::memory_order_acquire)) node->process (L, R, n);
        const float g = cur ? cur->retLevel[(size_t) s] : 1.0f;
        peakInto (retPeak[(size_t) s], L, R, n);
        masterBus.addFrom (0, 0, L, n, g);
        masterBus.addFrom (1, 0, R, n, g);
    }
    // master inserts
    for (auto& a : rack.master)
        if (auto* node = a.load (std::memory_order_acquire))
            node->process (masterBus.getWritePointer (0), masterBus.getWritePointer (1), n);
}

// ------------------------------------------------------------------ main process
void Engine::render (int start, int n)
{
    if (n <= 0) return;
    float* mL = masterBus.getWritePointer (0, start);
    float* mR = masterBus.getWritePointer (1, start);
    for (auto& v : voices)
    {
        if (! v.active) continue;
        if (v.padIdx >= 0 && v.padIdx < NPADS)
        {
            // every pad has its own channel strip (built-in FX, VST inserts, pan, mute / solo, sends)
            if (! padActive[(size_t) v.padIdx])
            {
                padBus.clear (v.padIdx * 2, 0, chunkLen); padBus.clear (v.padIdx * 2 + 1, 0, chunkLen);
                padActive[(size_t) v.padIdx] = true;
            }
            renderVoice (v, padBus.getWritePointer (v.padIdx * 2, start), padBus.getWritePointer (v.padIdx * 2 + 1, start), n);
        }
        else renderVoice (v, mL, mR, n);   // library preview
    }
}

void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples, const juce::MidiBuffer& midi)
{
    adoptSnapshot();
    running.store (true, std::memory_order_relaxed);
    int chunkStart = 0;
    while (chunkStart < numSamples)
    {
        const int n = juce::jmin (maxBlock, numSamples - chunkStart);
        chunkLen = n; curOffset = 0;
        masterBus.clear (0, n);
        padActive.fill (false);
        // on-screen pitch wheel -> the selected pad's plug-in instrument
        if (cur)
            if (auto* in = instAt (cur->s.sel))
            {
                const float b = bend.load (std::memory_order_relaxed);
                if (std::abs (b - lastBendSent) > 0.0005f)
                {
                    in->addMidi (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, (int) std::round (b * 8192.0f) + 8192)), 0);
                    lastBendSent = b;
                }
            }

        // pad filter coefficients for this block
        if (cur)
            for (int i = 0; i < NPADS; ++i)
            {
                const auto& fx = cur->pads[(size_t) i].fx;
                if (fx.type == FT_LP) padFilter[(size_t) i].setLP (fx.cut, fx.qValue(), sr);
                else if (fx.type == FT_HP) padFilter[(size_t) i].setHP (fx.cut, fx.qValue(), sr);
                else if (fx.type == FT_BP) padFilter[(size_t) i].setBP (fx.cut, fx.qValue(), sr);
            }

        // UI commands (applied at the start of the block)
        if (chunkStart == 0)
        {
            int s1, n1, s2, n2;
            const int avail = cmdFifo.getNumReady();
            cmdFifo.prepareToRead (avail, s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) handleCommand (cmdBuf[(size_t) (s1 + i)], 0);
            for (int i = 0; i < n2; ++i) handleCommand (cmdBuf[(size_t) (s2 + i)], 0);
            cmdFifo.finishedRead (n1 + n2);
        }

        // sub-block loop: MIDI, sequencer steps and scheduled hits all land on their exact sample
        auto midiIt = midi.findNextSamplePosition (chunkStart);
        int done = 0;
        while (done < n)
        {
            curOffset = done;
            while (midiIt != midi.cend() && (*midiIt).samplePosition <= chunkStart + done)
            {
                const auto meta = *midiIt;
                handleMidi (meta.data, meta.numBytes, done);
                ++midiIt;
            }
            if (finishAt >= 0 && (double) now >= finishAt)
            {
                stopPlay();
                EngineEvent e; e.type = EngineEvent::Finished; emit (e);
            }
            advanceSequencer ((double) now);
            double earliest = 1.0e300;
            for (int k = 0; k < npend;)
            {
                if (pend[(size_t) k].time <= (double) now)
                {
                    const auto P = pend[(size_t) k];
                    pend[(size_t) k] = pend[(size_t) --npend];
                    if (cur && P.trk < (int) cur->tracks.size())
                    {
                        startVoice (*cur, P.pad, P.chop, P.semi, P.gain, P.trk, false, 0, -1, false);
                        if (instAt (P.pad) != nullptr)
                            instNoteOn (P.pad, P.semi, juce::jlimit (1, 127, (int) std::round (std::min (1.0f, P.gain / 0.9f) * 127.0f)), P.len, false);
                    }
                    continue;
                }
                earliest = std::min (earliest, pend[(size_t) k].time);
                ++k;
            }
            for (int k = 0; k < nNoteOffs;)
            {
                if (noteOffs[(size_t) k].time <= (double) now)
                {
                    const auto o = noteOffs[(size_t) k];
                    noteOffs[(size_t) k] = noteOffs[(size_t) --nNoteOffs];
                    if (auto* in = instAt (o.pad)) in->addNote (o.note, 0, curOffset);
                    continue;
                }
                earliest = std::min (earliest, noteOffs[(size_t) k].time);
                ++k;
            }
            int next = n;
            if (midiIt != midi.cend()) next = juce::jmin (next, (*midiIt).samplePosition - chunkStart);
            if (seqPlaying && ! planDone && finishAt < 0) next = juce::jmin (next, done + juce::jmax (1, (int) std::ceil (nextStep - (double) now)));
            if (finishAt >= 0) next = juce::jmin (next, done + juce::jmax (1, (int) std::ceil (finishAt - (double) now)));
            if (npend > 0 || nNoteOffs > 0) next = juce::jmin (next, done + juce::jmax (1, (int) std::ceil (earliest - (double) now)));
            if (next <= done) next = done + 1;
            next = juce::jmin (next, n);
            render (done, next - done);
            now += next - done;
            done = next;
        }

        // channel strips -> sends / master, returns, master inserts
        runStrips (n);

        // metronome / count-in clicks
        {
            const double blockStart = (double) now - n;
            float* L = masterBus.getWritePointer (0);
            float* R = masterBus.getWritePointer (1);
            for (int k = 0; k < nclicks;)
            {
                const Click c = clicks[(size_t) k];
                const double f = c.accent ? 1600.0 : 1000.0;
                for (int i = 0; i < n; ++i)
                {
                    const double dt = (blockStart + i - c.time) / sr;
                    if (dt < 0 || dt > 0.06) continue;
                    const double g = dt < 0.002 ? 0.0001 * std::pow (3500.0, dt / 0.002)
                                                : 0.35 * std::pow (0.0001 / 0.35, (dt - 0.002) / 0.048);
                    const float v = (float) (std::sin (juce::MathConstants<double>::twoPi * f * dt) * g);
                    L[i] += v; R[i] += v;
                }
                if ((blockStart + n - c.time) / sr > 0.06) { clicks[(size_t) k] = clicks[(size_t) --nclicks]; continue; }
                ++k;
            }
            if (seqPlaying && countInEnd > (double) now && cur)
            {
                const double beat = 60.0 / effBpm * sr;
                countInLeft.store ((int) std::ceil ((countInEnd - (double) now) / beat));
                countInKind.store (recordArmed.load() ? 1 : 0);
            }
            else if (capState.load (std::memory_order_relaxed) == CAP_COUNT && capStartAt < 1.0e17 && cur)
            {
                const double beat = 60.0 / cur->s.bpm * sr;
                countInLeft.store (juce::jmax (0, (int) std::ceil ((capStartAt - (double) now) / beat)));
                countInKind.store (2);
            }
            else countInLeft.store (0);
        }

        // master chain (pseudocode 3.1): 0.85 -> LA-2A -> make-up -> limiter -> master out
        float* L = masterBus.getWritePointer (0);
        float* R = masterBus.getWritePointer (1);
        juce::FloatVectorOperations::multiply (L, 0.85f, n);
        juce::FloatVectorOperations::multiply (R, 0.85f, n);
        if (limiterOn)
        {
            la.process (L, R, n);
            // look-ahead limiter
            auto& LA = look;
            const float aA = std::exp (-1.0f / (lim.attack * (float) sr)), aR = std::exp (-1.0f / (lim.release * (float) sr));
            const float mk = -0.6f * lim.curve (0.0f);
            for (int i = 0; i < n; ++i)
            {
                const float x = std::max (std::abs (L[i]), std::abs (R[i]));
                const float xdb = 20.0f * std::log10 (x + 1.0e-9f);
                const float target = std::min (0.0f, lim.curve (xdb) - xdb);
                lim.envDb = target < lim.envDb ? aA * lim.envDb + (1 - aA) * target : aR * lim.envDb + (1 - aR) * target;
                const float g = std::pow (10.0f, (lim.envDb + mk) / 20.0f);
                const int rd = (LA.w - LA.la + LookAhead::MAXLA) % LookAhead::MAXLA;
                const float dl = LA.bufL[rd], dr = LA.bufR[rd];
                LA.bufL[LA.w] = L[i]; LA.bufR[LA.w] = R[i];
                LA.w = (LA.w + 1) % LookAhead::MAXLA;
                L[i] = juce::jlimit (-1.0f, 1.0f, dl * g);
                R[i] = juce::jlimit (-1.0f, 1.0f, dr * g);
            }
            laGr.store (la.envDb);
        }
        const float og = (cur ? cur->s.mvol : 0.85f) / 0.85f;
        juce::FloatVectorOperations::multiply (L, og, n);
        juce::FloatVectorOperations::multiply (R, og, n);

        // meters (UI reads and resets)
        {
            const float pl = juce::FloatVectorOperations::findMaximum (L, n), nl = -juce::FloatVectorOperations::findMinimum (L, n);
            const float pr = juce::FloatVectorOperations::findMaximum (R, n), nr = -juce::FloatVectorOperations::findMinimum (R, n);
            const float a = std::max (pl, nl), b = std::max (pr, nr);
            float o = meterL.load(); while (a > o && ! meterL.compare_exchange_weak (o, a)) {}
            o = meterR.load();       while (b > o && ! meterR.compare_exchange_weak (o, b)) {}
            const float m = std::max (a, b);
            o = masterPeak.load();   while (m > o && ! masterPeak.compare_exchange_weak (o, m)) {}
        }

        // output
        for (int c = 0; c < numOut; ++c)
        {
            if (out[c] == nullptr) continue;
            if (c < 2) juce::FloatVectorOperations::copy (out[c] + chunkStart, c == 0 ? L : R, n);
            else juce::FloatVectorOperations::clear (out[c] + chunkStart, n);
        }

        // sampling (input or resample of what we just played)
        if (capState.load (std::memory_order_relaxed) != CAP_IDLE)
        {
            const float* inPtrs[2] = { nullptr, nullptr };
            for (int c = 0; c < juce::jmin (2, numIn); ++c) inPtrs[c] = in[c] != nullptr ? in[c] + chunkStart : nullptr;
            runCapture (inPtrs, numIn, L, R, n);
        }
        chunkStart += n;
    }
    blockCount.fetch_add (1, std::memory_order_release);

    // playhead of the selected pad's newest main voice
    if (cur)
    {
        const int sel = cur->s.sel;
        const Voice* best = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.releasing && v.padIdx == sel && v.layer < 0)
                if (best == nullptr || v.order > best->order) best = &v;
        playhead.store (best != nullptr ? (float) (best->pos / std::max (1, best->slot->buf->length())) : -1.0f);
    }
}

// ------------------------------------------------------------------ offline render
juce::AudioBuffer<float> Engine::renderPlan (SnapshotPtr snap, const std::vector<int>& planIn, const std::vector<int>& onlyTracks,
                                             double sampleRate, bool limit,
                                             const std::function<void (float)>& progress, const std::function<bool()>& cancel,
                                             double* songSecondsOut, PluginSet* plugins)
{
    auto e = std::make_unique<Engine>();
    e->prepare (sampleRate, 1024);
    e->limiterOn = limit;
    int pluginLatency = 0;
    if (plugins != nullptr)
    {
        for (auto& kv : plugins->nodes)
            if (kv.second)
            {
                kv.second->proc->reset();   // prepared (sampleRate, 1024, non-realtime) on the message thread when cloned
                e->rack.at (kv.first).store (kv.second.get());
            }
        pluginLatency = plugins->masterLatency();
    }
    struct Detach { Engine& en; ~Detach() { for (auto& sl : SlotId::all()) en.rack.at (sl).store (nullptr); } } detach { *e };
    auto s = std::make_shared<Snapshot> (*snap);
    if (! onlyTracks.empty())
        for (int i = 0; i < (int) s->tracks.size(); ++i)
            if (std::find (onlyTracks.begin(), onlyTracks.end(), i) == onlyTracks.end()) s->tracks[(size_t) i].mute = true;
    if (! onlyTracks.empty()) { for (auto& t : s->tracks) t.solo = false; s->trackSoloAny = false; }   // the list already says what plays
    for (int i = 0; i < NPADS; ++i) if (s->pads[(size_t) i].fxOn) e->ensureFx (i);
    e->cur = s;
    e->applyLA (s->la);
    e->plan = planIn; e->planMode = true;
    double songLen = 0;
    for (int p : planIn) songLen += s->plen[(size_t) p] * 60.0 / s->bpmOf (p) / 4.0;
    if (songSecondsOut) *songSecondsOut = songLen;
    e->startPlay (0);

    const int skip = (limit ? e->look.la : 0) + pluginLatency;
    const int total = (int) std::ceil ((songLen + 3.0) * sampleRate) + skip;
    juce::AudioBuffer<float> outBuf (2, total);
    outBuf.clear();
    juce::MidiBuffer noMidi;
    const int B = 1024;
    for (int pos = 0; pos < total; pos += B)
    {
        const int n = juce::jmin (B, total - pos);
        float* outs[2] = { outBuf.getWritePointer (0, pos), outBuf.getWritePointer (1, pos) };
        e->process (nullptr, 0, outs, 2, n, noMidi);
        if (progress && (pos / B) % 64 == 0) progress ((float) pos / (float) total);
        if (cancel && (pos / B) % 64 == 0 && cancel()) return {};
    }
    // remove the limiter look-ahead and trim the silent tail (keep at least the song length)
    const int avail = total - skip;
    const float* L = outBuf.getReadPointer (0, skip);
    const float* R = outBuf.getReadPointer (1, skip);
    int last = 0;
    for (int i = avail - 1; i >= 0; --i) if (std::abs (L[i]) > 0.0005f || std::abs (R[i]) > 0.0005f) { last = i; break; }
    const int end = juce::jmin (avail, juce::jmax ((int) std::floor (songLen * sampleRate), last + (int) (0.02 * sampleRate)));
    juce::AudioBuffer<float> res (2, juce::jmax (1, end));
    res.copyFrom (0, 0, outBuf, 0, skip, juce::jmax (1, end));
    res.copyFrom (1, 0, outBuf, 1, skip, juce::jmax (1, end));
    return res;
}
} // namespace cx
