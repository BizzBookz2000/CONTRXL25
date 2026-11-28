// CONTRXL 25 - MPC Sample edition: controls, actions and lights.
#include "app.h"
#include <ctime>
#include <random>

namespace cx
{
const uint8_t S::PADNOTE[16] = { 37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53 };
static const struct { const char* name; float steps; } REPEAT_RATES[] = {
    { "1/4", 4.0f }, { "1/8", 2.0f }, { "1/8T", 4.0f / 3.0f }, { "1/16", 1.0f }, { "1/16T", 2.0f / 3.0f }, { "1/32", 0.5f }, { "1/32T", 1.0f / 3.0f } };
constexpr int NREPEAT = 7;

App::App (Hw& h, Engine& e, const std::string& cardDir) : hw (h), eng (e), card (cardDir)
{
    root = card + "/CONTRXL25";
    songsDir = root + "/Songs"; samplesDir = root + "/Samples"; recDir = root + "/Recordings"; exportDir = root + "/Exports";
    for (auto* d : { &root, &songsDir, &samplesDir, &recDir, &exportDir }) io::mkdirs (*d);
    ccLast.fill (64); ledCC.fill (-1);
    for (auto& p : ledPad) p = { 255, 255, 255 };
}

App::~App() { if (job.joinable()) job.join(); }

// ------------------------------------------------------------------ start-up
void App::start()
{
    loadPrefs();
    eng.recOffsetMs.store (prefs.recOffsetMs);
    eng.monitor.store (prefs.monitor);
    eng.inputGain.store (std::pow (10.0f, prefs.inputGainDb / 20.0f));
    eng.repeatSteps.store (REPEAT_RATES[clampv (prefs.repeatIdx, 0, NREPEAT - 1)].steps);
    eng.outLatency.store (hw.latencyFrames());
    splashUntil = nowSeconds() + 1.6;
    // where the knobs and fader are right now (the surface answers with CC 19, 16-18, 20)
    const uint8_t req[] = { 0xF0, 0x47, 0x7F, 0x5C, 0x42, 0x00, 0x00, 0xF7 };
    hw.sendSurface (req, sizeof req);
    browseTo (io::isDir (prefs.browseDir) ? prefs.browseDir : samplesDir);
    const std::string marker = root + "/.opening";
    if (io::exists (marker))
    {
        // the last start stopped while opening a song: do not try that song again
        ::remove (marker.c_str());
        makeFactoryKit();
        say ("The last song did not open - started with the kit", 5);
    }
    else if (! prefs.lastSong.empty() && io::exists (prefs.lastSong)) openSong (prefs.lastSong);
    else makeFactoryKit();
    rebuildSnapshot();
    sendLeds (true);
}

void App::loadPrefs()
{
    std::string t;
    if (! io::readFile (root + "/settings.json", t)) return;
    const Json j = Json::parse (t);
    prefs.faderMode = clampv ((int) j.get ("faderMode", 0), 0, 2);
    prefs.midiMode = clampv ((int) j.get ("midiMode", 0), 0, 1);
    prefs.recOffsetMs = (float) clampv (j.get ("recOffsetMs", 0.0), -100.0, 100.0);
    prefs.input = clampv ((int) j.get ("input", 1), 1, 2);
    prefs.sampleSrc = clampv ((int) j.get ("sampleSrc", 0), 0, 1);
    prefs.thrOn = j.getb ("thrOn", false);
    prefs.thrDb = (float) clampv (j.get ("thrDb", -30.0), -60.0, 0.0);
    prefs.monitor = (float) clampv (j.get ("monitor", 0.0), 0.0, 1.0);
    prefs.inputGainDb = (float) clampv (j.get ("inputGainDb", 0.0), -12.0, 24.0);
    prefs.repeatIdx = clampv ((int) j.get ("repeatIdx", 3), 0, NREPEAT - 1);
    prefs.lastSong = j.gets ("lastSong", "");
    prefs.browseDir = j.gets ("browseDir", "");
    prefs.autoAudition = j.getb ("autoAudition", true);
}

void App::savePrefs()
{
    Json j = Json::object();
    j.set ("faderMode", prefs.faderMode); j.set ("midiMode", prefs.midiMode); j.set ("recOffsetMs", prefs.recOffsetMs);
    j.set ("input", prefs.input); j.set ("sampleSrc", prefs.sampleSrc); j.set ("thrOn", prefs.thrOn); j.set ("thrDb", prefs.thrDb);
    j.set ("monitor", prefs.monitor); j.set ("inputGainDb", prefs.inputGainDb); j.set ("repeatIdx", prefs.repeatIdx);
    j.set ("lastSong", prefs.lastSong); j.set ("browseDir", browseDir); j.set ("autoAudition", prefs.autoAudition);
    io::writeFileAtomic (root + "/settings.json", j.dump (true));
}

void App::saveOnExit()
{
    if (job.joinable()) job.join();
    savePrefs();
}

// ------------------------------------------------------------------ factory kit (made in code, so the mod needs no sample files)
void App::makeFactoryKit()
{
    const std::string kitDir = samplesDir + "/CONTRXL Kit";
    io::mkdirs (kitDir);
    std::mt19937 rng (25);
    std::uniform_real_distribution<float> U (-1.0f, 1.0f);
    auto env = [] (double t, double a, double d) { return t < a ? t / a : std::exp (-(t - a) / d); };
    auto make = [&] (double secs, const std::function<float (double, int)>& f) -> SamplePtr
    {
        const int n = (int) (secs * SR);
        std::vector<float> l ((size_t) n);
        for (int i = 0; i < n; ++i) l[(size_t) i] = f (i / SR, i);
        float pk = 0; for (float v : l) pk = std::max (pk, std::fabs (v));
        if (pk > 0) for (auto& v : l) v *= 0.89f / pk;
        for (int i = 0; i < std::min (n, 64); ++i) l[(size_t) (n - 1 - i)] *= i / 64.0f;
        return makeSample (std::move (l), {}, SR);
    };
    struct Item { const char* name; SamplePtr s; int mg; };
    double ph = 0;
    float lp = 0, hp = 0, bp1 = 0, bp2 = 0;
    std::vector<Item> kit;
    kit.push_back ({ "Kick", make (0.6, [&] (double t, int) { const double f = 48 + 110 * std::exp (-t / 0.045); ph += f / SR; return (float) (std::sin (6.2831853 * ph) * env (t, 0.001, 0.22) + 0.25 * U (rng) * std::exp (-t / 0.004)); }), 0 });
    ph = 0;
    kit.push_back ({ "Snare", make (0.35, [&] (double t, int) { ph += 185 / SR; const float n = U (rng); bp1 += (n - bp1) * 0.45f; hp = n - bp1; return (float) (0.55 * std::sin (6.2831853 * ph) * env (t, 0.001, 0.05) + 0.9 * hp * env (t, 0.001, 0.09)); }), 0 });
    kit.push_back ({ "Clap", make (0.5, [&] (double t, int) { const float n = U (rng); bp1 += (n - bp1) * 0.3f; bp2 += (bp1 - bp2) * 0.3f; const float b = bp1 - bp2; double e = 0; for (int k = 0; k < 3; ++k) { const double tt = t - k * 0.011; if (tt >= 0) e = std::max (e, std::exp (-tt / 0.006)); } e = std::max (e, 0.55 * std::exp (-std::max (0.0, t - 0.022) / 0.11)); return (float) (b * e * 3); }), 0 });
    kit.push_back ({ "Hat Closed", make (0.12, [&] (double t, int) { const float n = U (rng); lp += (n - lp) * 0.6f; return (float) ((n - lp) * env (t, 0.0005, 0.025)); }), 1 });
    kit.push_back ({ "Hat Open", make (0.6, [&] (double t, int) { const float n = U (rng); lp += (n - lp) * 0.6f; return (float) ((n - lp) * env (t, 0.0005, 0.2)); }), 1 });
    ph = 0;
    kit.push_back ({ "Rim", make (0.12, [&] (double t, int) { ph += 820 / SR; return (float) (std::sin (6.2831853 * ph) * env (t, 0.0005, 0.012) + 0.4 * U (rng) * env (t, 0.0005, 0.004)); }), 0 });
    ph = 0;
    kit.push_back ({ "Tom", make (0.5, [&] (double t, int) { const double f = 95 + 60 * std::exp (-t / 0.06); ph += f / SR; return (float) (std::sin (6.2831853 * ph) * env (t, 0.001, 0.16)); }), 0 });
    kit.push_back ({ "Shaker", make (0.18, [&] (double t, int) { const float n = U (rng); bp1 += (n - bp1) * 0.7f; return (float) ((n - bp1) * env (t, 0.02, 0.04)); }), 0 });
    ph = 0;
    kit.push_back ({ "808 C2", make (1.6, [&] (double t, int) { const double f = 65.406 * (1 + 0.6 * std::exp (-t / 0.03)); ph += f / SR; return (float) std::tanh (1.8 * std::sin (6.2831853 * ph) * env (t, 0.002, 0.55)); }), 0 });
    {
        const double fr[] = { 130.81, 155.56, 196.0, 233.08, 293.66 };   // Cm9 around C3
        double phs[5] = {};
        kit.push_back ({ "Stab Cm9", make (1.2, [&] (double t, int)
        {
            double s = 0;
            for (int k = 0; k < 5; ++k) { phs[k] += fr[k] * 2 / SR; s += 2.0 * (phs[k] - std::floor (phs[k] + 0.5)); }
            const float c = (float) (0.05 + 0.5 * std::exp (-t / 0.18));
            lp += ((float) s - lp) * c;
            return (float) (lp * env (t, 0.004, 0.45));
        }), 0 });
    }
    ph = 0;
    kit.push_back ({ "Pluck C4", make (0.8, [&] (double t, int) { ph += 261.63 / SR; const double sq = std::sin (6.2831853 * ph) + 0.35 * std::sin (6.2831853 * ph * 2) + 0.15 * std::sin (6.2831853 * ph * 3); return (float) (sq * env (t, 0.002, 0.2)); }), 0 });
    ph = 0;
    kit.push_back ({ "Cowbell", make (0.4, [&] (double t, int) { ph += 1.0 / SR; const double s = (std::fmod (ph * 562, 1.0) < 0.5 ? 1 : -1) + (std::fmod (ph * 845, 1.0) < 0.5 ? 1 : -1); bp1 += ((float) s - bp1) * 0.4f; return (float) (bp1 * env (t, 0.001, 0.09)); }), 0 });

    P.reset();
    for (int i = 0; i < (int) kit.size(); ++i)
    {
        const std::string path = kitDir + "/" + std::string (i < 9 ? "0" : "") + std::to_string (i + 1) + " " + kit[(size_t) i].name + ".wav";
        if (! io::exists (path)) io::writeWav (path, *kit[(size_t) i].s, 24);
        Pad& pd = P.pads[(size_t) i];
        pd.buf = kit[(size_t) i].s; pd.name = kit[(size_t) i].name; pd.mg = kit[(size_t) i].mg;
    }
    P.s.bpm = 90; P.s.root = 48;   // the 808 is a C2: pad / key C plays it as recorded
    P.pads[8].keyMode = MODE_KEYS;
    auto put = [this] (int pad, std::initializer_list<int> steps, float v)
    {
        const int ti = P.trackFor (pad, true);
        if (ti < 0) return;
        auto& d = P.tracks[(size_t) ti].get (0);
        for (int s : steps) d.ev.push_back ({ s * TPS, 0, v, -2 });
    };
    put (0, { 0, 7, 10 }, 1.0f); put (1, { 4, 12 }, 0.95f); put (2, { 12 }, 0.6f);
    put (3, { 0, 2, 4, 6, 8, 10, 12 }, 0.7f); put (4, { 14 }, 0.7f); put (7, { 3, 11 }, 0.45f);
    P.name = "SESSION_01";
    songPath.clear(); unsaved = false;
    touch(); unsaved = false;
}

// ------------------------------------------------------------------ helpers
void App::say (const std::string& s, double seconds) { message = s; messageUntil = nowSeconds() + seconds; logf ("%s", s.c_str()); }

void App::pushUndo() { undoP = std::make_unique<Project> (P); redoP.reset(); }
void App::undo()
{
    if (undoP) { redoP = std::make_unique<Project> (P); P = *undoP; undoP.reset(); say ("Undo"); }
    else if (redoP) { undoP = std::make_unique<Project> (P); P = *redoP; redoP.reset(); say ("Redo"); }
    else { say ("Nothing to undo"); return; }
    touch();
}

void App::rebuildSnapshot()
{
    for (int i = 0; i < NPADS; ++i) if (P.pads[(size_t) i].fx.anyOn()) eng.ensureFx (i);
    auto s = builder.build (P);
    eng.setSnapshot (s);
    keepAlive.push_back (s);
    // old snapshots are released here (UI thread) once the audio thread no longer uses them
    while (keepAlive.size() > 2 && keepAlive.front().use_count() == 1) keepAlive.pop_front();
    while (keepAlive.size() > 64) keepAlive.pop_front();
    snapDirty = false;
}

std::string App::padName (int gi) const
{
    const Pad& p = P.pads[(size_t) gi];
    return p.buf ? (p.name.empty() ? std::string ("sample") : p.name) : std::string();
}

void App::select (int gi)
{
    gi = clampv (gi, 0, NPADS - 1);
    if (P.s.sel == gi) return;
    P.s.sel = gi;
    chopSel = 0; chopPage = 0;
    snapDirty = true;
    const int bp = gi / 16;
    if (bp != bankPage) bankPage = bp;
}

int App::nextEmptyPad (int from) const
{
    for (int k = 0; k < NPADS; ++k) { const int i = (from + k) % NPADS; if (! P.pads[(size_t) i].buf) return i; }
    return -1;
}

void App::setPadSample (int gi, SamplePtr s, const std::string& name)
{
    Pad& p = P.pads[(size_t) gi];
    const int mg = p.mg, lg = p.lg;
    p = Pad();
    p.buf = std::move (s); p.name = name; p.mg = mg; p.lg = lg;
    touch();
}

void App::setPage (Page p)
{
    if (p == pg) { if (p != Page::Main) { pg = prevPg == p ? Page::Main : prevPg; if (pg == p) pg = Page::Main; } return; }
    if (pg != Page::Menu && pg != Page::Open && pg != Page::Settings && pg != Page::About) prevPg = pg;
    pg = p; listSel = 0; listTop = 0;
    if (p == Page::Sample) hw.setInput (prefs.sampleSrc == 0 ? prefs.input : 0);
    else if (prefs.monitor <= 0 && eng.capState.load() == Engine::CAP_IDLE) hw.setInput (0);
    if (p == Page::Open) refreshOpenList();
    if (p == Page::Browser) browseTo (browseDir);
    if (p == Page::Seq) seqBar = std::max (0, std::min (seqBar, P.plen[(size_t) P.s.pat] / 16 - 1));
}

// ------------------------------------------------------------------ MIDI in
void App::onMidi (const MidiMsg& m)
{
    const uint8_t st = m.b[0], hi = st & 0xF0, chn = st & 0x0F;
    if (m.src == 0)
    {
        if (chn == 9 && (hi == 0x90 || hi == 0x80 || hi == 0xA0))
        {
            int pad = -1;
            for (int i = 0; i < 16; ++i) if (S::PADNOTE[i] == m.b[1]) pad = i;
            if (pad < 0) return;
            if (hi == 0xA0) padPressure (pad, m.b[2]);
            else if (hi == 0x90 && m.b[2] > 0) padDown (pad, m.b[2]);
            else padUp (pad);
            return;
        }
        if (chn == 0 && (hi == 0x90 || hi == 0x80)) { button (m.b[1], hi == 0x90 && m.b[2] > 0); return; }
        if (chn == 0 && hi == 0xB0) { control (m.b[1], m.b[2]); return; }
        return;
    }
    // external MIDI: keyboards, pads, transport
    if (st == 0xFA || st == 0xFB) { play (st == 0xFA); return; }
    if (st == 0xFC) { stop(); return; }
    if (hi == 0x90 && m.b[2] > 0) externalNote (m.b[1], m.b[2], true);
    else if (hi == 0x80 || (hi == 0x90 && m.b[2] == 0)) externalNote (m.b[1], 0, false);
}

void App::externalNote (int note, int vel, bool on)
{
    if (! on) { Command c; c.type = Command::Release; c.hold = note & 127; eng.push (c); return; }
    const bool rec = eng.playing.load() && eng.recordArmed.load();
    Command c; c.type = Command::Hit; c.vel = (float) vel; c.hold = note & 127; c.record = rec;
    if (prefs.midiMode == 1)
    {
        const int gi = note - 36;
        if (gi < 0 || gi >= NPADS) return;
        c.pad = gi; eng.push (c); flash[(size_t) gi] = 1;
        return;
    }
    if (P.s.keyPads)
    {
        const int idx = note - KEYBASE;
        const int gi = (idx >= 0 && idx < 128) ? P.s.keyPad[(size_t) idx] - 1 : -1;
        if (gi < 0) return;
        c.pad = gi; c.choke = true; eng.push (c);
        return;
    }
    const Pad& p = selPad();
    c.pad = P.s.sel; c.choke = true;
    if (p.keyMode == MODE_CHOP)
    {
        const int idx = note - KEYBASE;
        int ci = idx;
        if (p.hasKmap) { auto it = p.kmap.find (idx); ci = it == p.kmap.end() ? -1 : it->second; }
        if (ci < 0 || ci >= p.numChops()) return;
        c.chop = ci; chopSel = ci;
    }
    else c.semi = (float) (note + 12 * oct - P.s.root);
    eng.push (c);
}

void App::padPressure (int p, int value)
{
    if (! repeatHeld || ! padHeld[(size_t) p]) return;
    Command c; c.type = Command::RepeatVel; c.hold = p; c.vel = (float) std::max (8, value); eng.push (c);
}

void App::padDown (int p, int vel)
{
    padHeld[(size_t) p] = true; padHeldAt[(size_t) p] = nowSeconds();
    const int gi = bankPage * 16 + p;
    if (! confirmText.empty()) return;
    if (muteHeld) { Pad& pd = P.pads[(size_t) gi]; pd.muted = ! pd.muted; say (padLabel (gi) + (pd.muted ? " muted" : " unmuted")); touch(); return; }
    if (pg == Page::Seq && stepMode && ! shift)
    {
        const int step = seqBar * 16 + p;
        if (eraseHeld) { clearStep (step); return; }
        for (int q = 0; q < 16; ++q)
            if (q != p && padHeld[(size_t) q] && padHeldAt[(size_t) q] < padHeldAt[(size_t) p]) { stepCopy (seqBar * 16 + q, step); return; }
        stepToggle (step, vel);
        return;
    }
    if (eraseHeld) { erasePadInPattern (gi); return; }
    if (shift) { select (gi); say ("Selected " + padLabel (gi) + (P.pads[(size_t) gi].buf ? " - " + padName (gi) : " (empty)")); return; }

    const bool rec = eng.playing.load() && eng.recordArmed.load();
    Command c; c.vel = (float) vel; c.record = rec; c.hold = 200 + p;
    const bool chopPage_ = pg == Page::Chop;
    const KeyMode mode = chopPage_ ? KM_CHOP : km;
    if (mode == KM_PADS)
    {
        c.pad = gi;
        if (pg == Page::Browser) { select (gi); }
        else select (gi);
        flash[(size_t) gi] = 1;
    }
    else if (mode == KM_KEYS)
    {
        c.pad = P.s.sel; c.choke = true;
        c.semi = (float) (p + 12 * oct);   // pad 1 = the sample as recorded
    }
    else
    {
        const int ci = (chopPage_ ? 0 : chopPage * 16) + p;
        if (! selPad().buf || ci >= selPad().numChops()) return;
        c.pad = P.s.sel; c.chop = ci; c.choke = true; chopSel = ci;
    }
    if (repeatHeld) { c.type = Command::RepeatStart; c.hold = p; }
    else c.type = Command::Hit;
    eng.push (c);
}

void App::padUp (int p)
{
    padHeld[(size_t) p] = false;
    Command c; c.type = Command::Release; c.hold = 200 + p; eng.push (c);
    Command r; r.type = Command::RepeatStop; r.hold = p; eng.push (r);
}

void App::control (int cc, int value)
{
    int delta = 0;
    if (ccKnown[(size_t) cc]) delta = value - ccLast[(size_t) cc];
    ccLast[(size_t) cc] = value; ccKnown[(size_t) cc] = true;
    switch (cc)
    {
        case S::CC_WHEEL: wheel (value >= 64 ? value - 128 : value); return;
        case S::CC_VOLUME: hwVolume = (value / 127.0f) * (value / 127.0f); hw.setMainVolume (hwVolume); return;
        case S::CC_FADER:
        {
            Pad& pd = selPad();
            if (prefs.faderMode == 0) { pd.vol = value / 100.0f; say (padLabel (sel()) + " level " + std::to_string ((int) std::lround (pd.vol * 100))); }
            else if (prefs.faderMode == 1)
            {
                if (pd.fx.type == FT_OFF) pd.fx.type = FT_LP;
                pd.fx.cut = (float) (40.0 * std::pow (18000.0 / 40.0, value / 127.0));
                say (padLabel (sel()) + " filter " + std::to_string ((int) pd.fx.cut) + " Hz");
            }
            else { P.s.mvol = value / 127.0f; say ("Master " + std::to_string ((int) std::lround (P.s.mvol * 100))); }
            touch();
            return;
        }
        case S::CC_K1: case S::CC_K2: case S::CC_K3:
            if (delta != 0 && std::abs (delta) < 40) knob (cc - S::CC_K1, delta);
            return;
        default: return;
    }
}

void App::button (int note, bool down)
{
    switch (note)
    {
        case S::SHIFT: shift = down; return;
        case S::ERASE: eraseHeld = down; if (down && shift) { eraseHeld = false; undo(); } return;
        case S::MUTE: muteHeld = down; return;
        case S::NOTEREPEAT:
            repeatHeld = down;
            if (down && shift) { prefs.repeatIdx = (prefs.repeatIdx + 1) % NREPEAT; eng.repeatSteps.store (REPEAT_RATES[prefs.repeatIdx].steps); say (std::string ("Note repeat ") + REPEAT_RATES[prefs.repeatIdx].name); }
            if (! down) for (int p = 0; p < 16; ++p) { Command r; r.type = Command::RepeatStop; r.hold = p; eng.push (r); }
            return;
        default: break;
    }
    if (! down) return;
    if (! confirmText.empty())
    {
        if (note == S::PUSH || note == S::B3) { auto f = std::move (confirmYes); confirmText.clear(); if (f) f(); }
        else if (note == S::B1 || note == S::STOP || note == S::MINUS) { confirmText.clear(); confirmYes = nullptr; say ("Cancelled"); }
        return;
    }
    if (splash) return;
    switch (note)
    {
        case S::PLAY: if (shift) { P.s.song = ! P.s.song && ! P.chain.empty(); touch(); say (P.s.song ? "Song mode on" : "Pattern mode"); } else play (true); return;
        case S::STOP: stop(); return;
        case S::SEQREC: recordButton(); return;
        case S::TAPTEMPO: if (shift) { P.s.metro = ! P.s.metro; touch(); say (P.s.metro ? "Metronome on" : "Metronome off"); } else tapTempo(); return;
        case S::SAMPLEREC: sampleRecord(); return;
        case S::PADBANK: bankPage = (bankPage + (shift ? 3 : 1)) % 4; say ("Banks " + std::string (1, (char) ('A' + bankPage * 2)) + " + " + std::string (1, (char) ('B' + bankPage * 2))); return;
        case S::LEVELS16:
            if (shift) { oct = 0; chopPage = 0; say ("Octave / chop page reset"); return; }
            km = (KeyMode) ((km + 1) % 3);
            if (km == KM_KEYS) selPad().keyMode = MODE_KEYS;
            if (km == KM_CHOP) selPad().keyMode = MODE_CHOP;
            say (km == KM_PADS ? "PADS: 16 pads per bank" : km == KM_KEYS ? "KEYS: pads play " + padLabel (sel()) + " in semitones" : "CHOP: pads play the chops of " + padLabel (sel()));
            return;
        case S::SEQ: setPage (Page::Seq); return;
        case S::LOOP: if (shift) { P.s.loop = ! P.s.loop; touch(); say (P.s.loop ? "Loop on" : "Loop off"); } else setPage (Page::Song); return;
        case S::SAMPLE: setPage (Page::Sample); return;
        case S::CHOP: setPage (Page::Chop); return;
        case S::PADFX: setPage (Page::PadEdit); return;
        case S::KNOBFX: setPage (Page::Mixer); return;
        case S::SAMPLESELECT: setPage (Page::Browser); return;
        case S::B1: softKey (0); return;
        case S::B2: softKey (1); return;
        case S::B3: softKey (2); return;
        case S::PLUS: plusMinus (1); return;
        case S::MINUS: plusMinus (-1); return;
        case S::PUSH:
        {
            if (shift) { setPage (pg == Page::Menu ? Page::Main : Page::Menu); return; }
            if (pg == Page::Main) { setPage (Page::Menu); return; }
            if (pg == Page::Chop) { Command c; c.type = Command::Hit; c.pad = sel(); c.chop = chopSel; c.vel = 110; eng.push (c); return; }
            if (pg == Page::Browser)
            {
                if (browseSel < 0 || browseSel >= (int) browseList.size()) return;
                const auto& e = browseList[(size_t) browseSel];
                if (e.name == "..") { browseTo (io::parentOf (browseDir), io::fileName (browseDir)); return; }
                if (e.dir) { browseTo (browseDir + "/" + e.name); return; }
                if (io::hasExt (e.name, ".zip")) { const auto path = browseDir + "/" + e.name; ask ("Open song " + e.name + "?", [this, path] { openSong (path); }); return; }
                loadSample (browseDir + "/" + e.name, 0);
                return;
            }
            if (pg == Page::Seq) { stepMode = ! stepMode; say (stepMode ? "STEP: pads edit steps" : "LIVE: pads play"); return; }
            auto r = rows();
            if (listSel >= 0 && listSel < (int) r.size())
            {
                auto& row = r[(size_t) listSel];
                if (row.action) row.action();
                else if (row.adjust) row.adjust (1, false);
            }
            return;
        }
        default: return;
    }
}

void App::wheel (int d)
{
    if (! confirmText.empty() || d == 0) return;
    switch (pg)
    {
        case Page::Main: select (clampv (sel() + d, 0, NPADS - 1)); return;
        case Page::Chop: chopSel = clampv (chopSel + d, 0, std::max (0, selPad().numChops() - 1)); return;
        case Page::Seq:
        {
            // walk through the pads that have hits in this pattern (plus the selected one)
            std::vector<int> pads;
            for (auto& t : P.tracks) if (t.hasDataIn (P.s.pat) && std::find (pads.begin(), pads.end(), t.pad) == pads.end()) pads.push_back (t.pad);
            for (int i = 0; i < NPADS; ++i) if (P.pads[(size_t) i].buf && std::find (pads.begin(), pads.end(), i) == pads.end()) pads.push_back (i);
            std::sort (pads.begin(), pads.end());
            if (pads.empty()) return;
            auto it = std::find (pads.begin(), pads.end(), sel());
            int idx = it == pads.end() ? 0 : (int) (it - pads.begin());
            select (pads[(size_t) clampv (idx + d, 0, (int) pads.size() - 1)]);
            return;
        }
        case Page::Song: songSel = clampv (songSel + d, 0, std::max (0, (int) P.chain.size() - 1)); return;
        case Page::Mixer: select (clampv (sel() + d, 0, NPADS - 1)); return;
        case Page::Browser:
            browseSel = clampv (browseSel + d, 0, std::max (0, (int) browseList.size() - 1));
            if (prefs.autoAudition && browseSel < (int) browseList.size() && ! browseList[(size_t) browseSel].dir && io::hasExt (browseList[(size_t) browseSel].name, ".wav")
                && browseList[(size_t) browseSel].size < 30 * 1024 * 1024)
            {
                auto s = io::loadAudioFile (browseDir + "/" + browseList[(size_t) browseSel].name);
                if (s) { auto snap = builder.build (P, s); eng.setSnapshot (snap); keepAlive.push_back (snap); Command c; c.type = Command::Preview; eng.push (c); }
            }
            return;
        case Page::Open: listSel = clampv (listSel + d, 0, std::max (0, (int) openList.size() - 1)); return;
        default:
        {
            const int n = (int) rows().size();
            listSel = clampv (listSel + d, 0, std::max (0, n - 1));
            return;
        }
    }
}

void App::plusMinus (int dir)
{
    switch (pg)
    {
        case Page::Main: case Page::Seq:
            if (pg == Page::Main && km == KM_KEYS) { oct = clampv (oct + dir, -3, 3); say ("Octave " + std::string (oct > 0 ? "+" : "") + std::to_string (oct)); return; }
            if (pg == Page::Main && km == KM_CHOP) { chopPage = clampv (chopPage + dir, 0, 7); say ("Chops " + std::to_string (chopPage * 16 + 1) + "-" + std::to_string (chopPage * 16 + 16)); return; }
            P.s.pat = clampv (P.s.pat + dir, 0, NPAT - 1);
            seqBar = 0; touch(); unsaved = true;
            say ("Pattern " + std::to_string (P.s.pat + 1) + (eng.playing.load() ? " (plays next)" : ""));
            return;
        case Page::Song:
            if (songSel < (int) P.chain.size()) { auto& c = P.chain[(size_t) songSel]; if (shift) c.rep = clampv (c.rep + dir, 1, 16); else c.pat = clampv (c.pat + dir, 0, NPAT - 1); touch(); }
            return;
        case Page::Browser:
            if (dir < 0) browseTo (io::parentOf (browseDir), io::fileName (browseDir));
            else if (browseSel < (int) browseList.size() && browseList[(size_t) browseSel].dir) browseTo (browseDir + "/" + browseList[(size_t) browseSel].name);
            return;
        case Page::Chop: chopSel = clampv (chopSel + dir, 0, std::max (0, selPad().numChops() - 1)); return;
        case Page::Mixer: select (clampv (sel() + dir, 0, NPADS - 1)); return;
        case Page::Open: listSel = clampv (listSel + dir, 0, std::max (0, (int) openList.size() - 1)); return;
        default:
        {
            auto r = rows();
            if (listSel < (int) r.size() && r[(size_t) listSel].adjust) r[(size_t) listSel].adjust (dir, shift);
            return;
        }
    }
}

void App::knob (int k, int delta)
{
    const int dir = delta;
    Pad& pd = selPad();
    auto padTouch = [this] { touch(); };
    switch (pg)
    {
        case Page::Main: case Page::Mixer:
            if (k == 0) { if (pg == Page::Mixer) { pd.vol = clampv (pd.vol + dir * 0.01f, 0.0f, 2.0f); say (padLabel (sel()) + " level " + std::to_string ((int) std::lround (pd.vol * 100))); } else { pd.pitch = clampv (pd.pitch + dir * (shift ? 0.1f : 0.5f), -PR, PR); say (padLabel (sel()) + " tune " + strf ("%+.1f", pd.pitch)); } }
            else if (k == 1) { if (pg == Page::Mixer) { pd.pan = clampv (pd.pan + dir * 0.02f, -1.0f, 1.0f); say (padLabel (sel()) + " pan " + strf ("%+d", (int) std::lround (pd.pan * 50))); } else { pd.vol = clampv (pd.vol + dir * 0.01f, 0.0f, 2.0f); say (padLabel (sel()) + " level " + std::to_string ((int) std::lround (pd.vol * 100))); } }
            else
            {
                if (pg == Page::Mixer) { P.s.mvol = clampv (P.s.mvol + dir * 0.01f, 0.0f, 1.0f); say ("Master " + std::to_string ((int) std::lround (P.s.mvol * 100))); }
                else
                {
                    if (pd.fx.type == FT_OFF) { pd.fx.type = FT_LP; pd.fx.cut = 18000; }
                    pd.fx.cut = clampv (pd.fx.cut * std::pow (2.0f, dir / 24.0f), 40.0f, 18000.0f);
                    say (padLabel (sel()) + " filter " + std::to_string ((int) pd.fx.cut) + " Hz");
                }
            }
            padTouch();
            return;
        case Page::Seq:
            if (k == 0)
            {
                static const int LENS[4] = { 16, 32, 64, 128 };
                int i = 0; while (i < 3 && LENS[i] < P.plen[(size_t) P.s.pat]) ++i;
                static int acc = 0; acc += dir;
                if (std::abs (acc) >= 6) { i = clampv (i + (acc > 0 ? 1 : -1), 0, 3); acc = 0; P.plen[(size_t) P.s.pat] = LENS[i]; seqBar = std::min (seqBar, LENS[i] / 16 - 1); touch(); say ("Pattern length " + std::to_string (LENS[i] / 16) + " bar" + (LENS[i] > 16 ? "s" : "")); }
            }
            else if (k == 1) { static int acc = 0; acc += dir; if (std::abs (acc) >= 6) { P.s.quant = acc > 0; acc = 0; touch(); say (P.s.quant ? "Quantize on: hits land on 1/16" : "Quantize off: hits keep their timing"); } }
            else { P.s.bpm = clampv (std::round ((P.s.bpm + dir * (shift ? 0.1f : 0.5f)) * 10) / 10, 40.0f, 240.0f); touch(); say (strf ("Tempo %.1f BPM", P.s.bpm)); }
            return;
        case Page::Song:
            if (songSel < (int) P.chain.size())
            {
                static int acc = 0; acc += dir;
                if (std::abs (acc) < 4) return;
                const int s = acc > 0 ? 1 : -1; acc = 0;
                auto& c = P.chain[(size_t) songSel];
                if (k == 0) c.pat = clampv (c.pat + s, 0, NPAT - 1); else if (k == 1) c.rep = clampv (c.rep + s, 1, 16);
                touch();
            }
            if (k == 2) { P.s.bpm = clampv (std::round ((P.s.bpm + dir * 0.5f) * 10) / 10, 40.0f, 240.0f); touch(); say (strf ("Tempo %.1f BPM", P.s.bpm)); }
            return;
        case Page::Sample:
            if (k == 0) { prefs.thrDb = clampv (prefs.thrDb + dir * 0.5f, -60.0f, 0.0f); prefs.thrOn = true; say (strf ("Threshold %.1f dB", prefs.thrDb)); }
            else if (k == 1) { prefs.inputGainDb = clampv (prefs.inputGainDb + dir * 0.25f, -12.0f, 24.0f); eng.inputGain.store (std::pow (10.0f, prefs.inputGainDb / 20.0f)); say (strf ("Input gain %+.1f dB", prefs.inputGainDb)); }
            else { prefs.monitor = clampv (prefs.monitor + dir * 0.01f, 0.0f, 1.0f); eng.monitor.store (prefs.monitor); say (prefs.monitor > 0 ? "Monitor " + std::to_string ((int) std::lround (prefs.monitor * 100)) : std::string ("Monitor off")); }
            return;
        case Page::Chop:
        {
            if (! pd.buf) return;
            const int n = pd.numChops();
            if (k == 2) { pd.pitch = clampv (pd.pitch + dir * 0.5f, -PR, PR); padTouch(); return; }
            // move the start (K1, coarse) of the selected chop
            const int mi = chopSel - 1;
            if (mi < 0 || mi >= (int) pd.markers.size()) { say ("Chop 1 starts at the beginning - pick another chop"); return; }
            const float step = (k == 0 ? 0.004f : 0.0002f) * dir;
            const float lo = mi > 0 ? pd.markers[(size_t) mi - 1] + 0.0005f : 0.0005f;
            const float hi = mi + 1 < (int) pd.markers.size() ? pd.markers[(size_t) mi + 1] - 0.0005f : 0.9995f;
            pd.markers[(size_t) mi] = clampv (pd.markers[(size_t) mi] + step, lo, hi);
            (void) n;
            padTouch();
            return;
        }
        case Page::PadEdit:
        {
            static int acc[3] = { 0, 0, 0 };
            acc[k] += dir;
            if (std::abs (acc[k]) < 2) return;
            const int s = acc[k] > 0 ? 1 : -1; acc[k] = 0;
            auto r = rows();
            if (k == 0) { if (listSel < (int) r.size() && r[(size_t) listSel].adjust) r[(size_t) listSel].adjust (s, shift); }
            else if (k == 1) { pd.vol = clampv (pd.vol + s * 0.01f, 0.0f, 2.0f); padTouch(); }
            else { pd.pitch = clampv (pd.pitch + s * (shift ? 0.1f : 1.0f), -PR, PR); padTouch(); }
            return;
        }
        default:
        {
            static int acc = 0; acc += dir;
            if (std::abs (acc) < 3) return;
            const int s = acc > 0 ? 1 : -1; acc = 0;
            if (k == 0) wheel (s);
            else { auto r = rows(); if (listSel < (int) r.size() && r[(size_t) listSel].adjust) r[(size_t) listSel].adjust (s, shift); }
            return;
        }
    }
}

void App::softKey (int k)
{
    switch (pg)
    {
        case Page::Main:
            if (k == 0) { P.s.vel = ! P.s.vel; say (P.s.vel ? "Velocity on: harder hits are louder" : "Velocity off: every hit is full"); }
            else if (k == 1) { P.s.choke = ! P.s.choke; say (P.s.choke ? "Choke on (KEYS / CHOP / MIDI keys)" : "Choke off: notes ring together"); }
            else { P.s.gate = ! P.s.gate; say (P.s.gate ? "Gate on: sounds stop when you let go" : "Gate off: sounds play to the end"); }
            touch();
            return;
        case Page::PadEdit: editTab = k; listSel = 0; listTop = 0; return;
        case Page::Sample:
            if (k == 0) { prefs.sampleSrc = 0; prefs.input = prefs.input == 1 ? 2 : 1; hw.setInput (prefs.input); say (prefs.input == 1 ? "Input: LINE IN" : "Input: built-in MIC"); }
            else if (k == 1) { prefs.sampleSrc = 1 - prefs.sampleSrc; hw.setInput (prefs.sampleSrc == 0 ? prefs.input : 0); say (prefs.sampleSrc ? "RESAMPLE: records what CONTRXL plays" : "Records the input"); }
            else { prefs.thrOn = ! prefs.thrOn; say (prefs.thrOn ? strf ("Threshold on (%.0f dB): waits for sound", prefs.thrDb) : "Threshold off: records at once"); }
            savePrefs();
            return;
        case Page::Chop:
            if (k == 0)
            {
                static int mode = 0;
                mode = (mode + 1) % 5;
                autoChop (mode);
            }
            else if (k == 1)
            {
                Pad& pd = selPad();
                if (! pd.buf) return;
                if (shift)
                {
                    if (chopSel < 1 || chopSel - 1 >= (int) pd.markers.size()) return;
                    pushUndo(); pd.markers.erase (pd.markers.begin() + chopSel - 1); chopSel = std::max (0, chopSel - 1); touch(); say ("Chop removed");
                    return;
                }
                auto segs = segments (pd.buf.get(), pd.markers);
                if (chopSel >= (int) segs.size()) return;
                const float mid = (float) ((segs[(size_t) chopSel].first + segs[(size_t) chopSel].second) * 0.5 / pd.buf->duration());
                pushUndo(); pd.markers.push_back (mid); std::sort (pd.markers.begin(), pd.markers.end()); touch();
                say ("Split chop " + std::to_string (chopSel + 1));
            }
            else { if (shift) trimToChop(); else chopsToPads(); }
            return;
        case Page::Seq:
            if (k == 0) { stepMode = ! stepMode; say (stepMode ? "STEP: pads edit the steps of " + padLabel (sel()) : "LIVE: pads play"); }
            else if (k == 1) { const int bars = P.plen[(size_t) P.s.pat] / 16; seqBar = (seqBar + (shift ? bars - 1 : 1)) % bars; say ("Bar " + std::to_string (seqBar + 1) + " of " + std::to_string (bars)); }
            else
            {
                if (shift)
                {
                    ask ("Clear pattern " + std::to_string (P.s.pat + 1) + "?", [this]
                    {
                        pushUndo();
                        for (auto& t : P.tracks) t.pd[(size_t) P.s.pat].reset();
                        touch(); say ("Pattern cleared (Shift+Erase = undo)");
                    });
                    return;
                }
                // copy this pattern to the next empty one
                int to = -1;
                for (int i = 1; i < NPAT && to < 0; ++i)
                {
                    const int q = (P.s.pat + i) % NPAT; bool used = false;
                    for (auto& t : P.tracks) if (t.hasDataIn (q)) used = true;
                    if (! used) to = q;
                }
                if (to < 0) { say ("No empty pattern left"); return; }
                pushUndo();
                for (auto& t : P.tracks) t.pd[(size_t) to] = t.pd[(size_t) P.s.pat];
                P.plen[(size_t) to] = P.plen[(size_t) P.s.pat];
                P.s.pat = to; touch();
                say ("Copied to pattern " + std::to_string (to + 1));
            }
            return;
        case Page::Song:
            if (k == 0)
            {
                ChainItem c; c.pat = P.chain.empty() ? P.s.pat : P.chain[(size_t) songSel].pat;
                const int at = P.chain.empty() ? 0 : songSel + 1;
                P.chain.insert (P.chain.begin() + at, c); songSel = at; touch();
            }
            else if (k == 1) { if (! P.chain.empty()) { P.chain.erase (P.chain.begin() + songSel); songSel = clampv (songSel, 0, std::max (0, (int) P.chain.size() - 1)); if (P.chain.empty()) P.s.song = false; touch(); } }
            else { P.s.song = ! P.s.song && ! P.chain.empty(); touch(); say (P.s.song ? "Song mode: PLAY plays the chain" : (P.chain.empty() ? "Add patterns first (B1)" : "Pattern mode")); }
            return;
        case Page::Mixer:
        {
            Pad& pd = selPad();
            if (k == 0) pd.muted = ! pd.muted;
            else if (k == 1) pd.solo = ! pd.solo;
            else { pd.vol = 1; pd.pan = 0; }
            touch();
            return;
        }
        case Page::Browser:
            if (browseSel >= (int) browseList.size()) return;
            if (k == 0) { prefs.autoAudition = ! prefs.autoAudition; say (prefs.autoAudition ? "Audition on" : "Audition off"); return; }
            if (browseList[(size_t) browseSel].dir || ! io::hasExt (browseList[(size_t) browseSel].name, ".wav")) { say ("Pick a WAV file"); return; }
            loadSample (browseDir + "/" + browseList[(size_t) browseSel].name, k == 1 ? 1 : 2);
            return;
        case Page::Menu: case Page::Settings: case Page::About: case Page::Open:
            if (k == 0) { setPage (Page::Main); return; }
            if (pg == Page::Open && k == 2 && listSel < (int) openList.size()) { const auto path = songsDir + "/" + openList[(size_t) listSel]; ask ("Open " + openList[(size_t) listSel] + "?" + (unsaved ? " Unsaved changes are lost." : ""), [this, path] { openSong (path); }); }
            else if (pg != Page::Open) { auto r = rows(); if (listSel < (int) r.size()) { if (r[(size_t) listSel].action) r[(size_t) listSel].action(); else if (r[(size_t) listSel].adjust) r[(size_t) listSel].adjust (k == 1 ? -1 : 1, false); } }
            return;
        default: return;
    }
}

// ------------------------------------------------------------------ transport
void App::play (bool)
{
    if (eng.playing.load()) { Command c; c.type = Command::Play; eng.push (c); say ("Restart"); return; }
    if (snapDirty) rebuildSnapshot();
    Command c; c.type = Command::Play; c.count = (eng.recordArmed.load() && P.s.count) ? 4 : 0; eng.push (c);
}

void App::stop()
{
    const double t = nowSeconds();
    if (! eng.playing.load() && t - stopTapAt < 0.6) { Command c; c.type = Command::Panic; eng.push (c); say ("All sounds off"); }
    Command c; c.type = Command::Stop; eng.push (c);
    if (eng.recordArmed.load()) eng.recordArmed.store (false);
    stopTapAt = t;
}

void App::recordButton()
{
    const bool on = ! eng.recordArmed.load();
    if (on) pushUndo();
    eng.recordArmed.store (on);
    if (on && ! eng.playing.load()) { if (snapDirty) rebuildSnapshot(); Command c; c.type = Command::Play; c.count = P.s.count ? 4 : 0; eng.push (c); say (P.s.count ? "Recording after the count-in" : "Recording"); }
    else say (on ? "Overdub on" : "Recording off");
}

void App::tapTempo()
{
    const double t = nowSeconds();
    if (! taps.empty() && t - taps.back() > 2.0) taps.clear();
    taps.push_back (t);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double iv = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        P.s.bpm = clampv ((float) std::round (600.0 / iv) / 10.0f, 40.0f, 240.0f);
        touch(); say (strf ("Tempo %.1f BPM", P.s.bpm));
    }
    else say ("Tap again to set the tempo");
}

// ------------------------------------------------------------------ sampling
void App::sampleRecord()
{
    const int st = eng.capState.load();
    if (st == Engine::CAP_IDLE)
    {
        if (pg != Page::Sample) setPage (Page::Sample);
        const bool input = prefs.sampleSrc == 0;
        if (input) hw.setInput (prefs.input);
        if (snapDirty) rebuildSnapshot();
        eng.startCapture (input, prefs.thrOn && input, prefs.thrDb, false, 60.0);
        say (prefs.thrOn && input ? "Waiting for sound above the threshold..." : "Recording - press SAMPLE RECORD to stop");
    }
    else if (st == Engine::CAP_COUNT || st == Engine::CAP_WAIT || st == Engine::CAP_REC) eng.requestCaptureStop();
}

void App::finishCapture()
{
    auto s = eng.takeCapture();
    if (! s) { say ("Nothing recorded"); return; }
    if (P.s.autoNorm && s->peak() > 1e-4f) s = dsp::normalized (*s);
    // an empty selected pad takes it; otherwise the next empty pad
    int gi = selPad().buf ? nextEmptyPad (sel()) : sel();
    if (gi < 0) { say ("All 64 pads are full - clear one first"); return; }
    time_t now = time (nullptr); struct tm tmv; localtime_r (&now, &tmv);
    char nm[64]; strftime (nm, sizeof nm, "%Y%m%d_%H%M%S", &tmv);
    const std::string name = std::string (prefs.sampleSrc ? "RESAMPLE " : "REC ") + std::string (nm).substr (9);
    pushUndo();
    setPadSample (gi, s, name);
    select (gi);
    const std::string path = recDir + "/" + (prefs.sampleSrc ? "RESAMPLE_" : "REC_") + nm + ".wav";
    std::thread ([s, path] { io::writeWav (path, *s, 24); }).detach();
    say (strf ("%.1f s on %s - saved in Recordings", s->duration(), padLabel (gi).c_str()));
}

// ------------------------------------------------------------------ sequencer editing
int App::selTrack (bool create) { return P.trackFor (sel(), create); }

void App::addRecordedHit (const EngineEvent& e)
{
    const int pat = clampv (e.pat, 0, NPAT - 1);
    const int len = P.plen[(size_t) pat];
    if (e.pos < -0.5) return;
    int tick = P.s.quant ? (int) std::lround (e.pos) * TPS : (int) std::lround (e.pos * TPS);
    tick = ((tick % (len * TPS)) + len * TPS) % (len * TPS);
    const int ti = P.trackFor (e.pad, true);
    if (ti < 0) { say ("All 64 tracks are used"); return; }
    auto& d = P.tracks[(size_t) ti].get (pat);
    const int c = e.chop >= 0 ? e.chop : -2;
    for (auto& x : d.ev) if (std::abs (x.t - tick) < 3 && x.p == e.semi && x.c == c) { x.v = std::max (x.v, e.gain); touch(); return; }
    d.ev.push_back ({ tick, e.semi, e.gain, c });
    touch();
}

void App::stepToggle (int step, int vel)
{
    const int pat = P.s.pat;
    if (step >= P.plen[(size_t) pat]) return;
    if (! selPad().buf) { say (padLabel (sel()) + " is empty - SHIFT + pad picks another"); return; }
    pushUndo();
    const int ti = selTrack (true);
    if (ti < 0) { say ("All 64 tracks are used"); return; }
    auto& d = P.tracks[(size_t) ti].get (pat);
    bool had = d.steps[(size_t) step] != 0;
    for (auto& e : d.ev) if (e.t / TPS == step) had = true;
    if (had)
    {
        d.steps[(size_t) step] = 0;
        d.ev.erase (std::remove_if (d.ev.begin(), d.ev.end(), [step] (const HitEvent& e) { return e.t / TPS == step; }), d.ev.end());
    }
    else
    {
        const float semi = km == KM_KEYS ? (float) (12 * oct) : 0.0f;
        d.ev.push_back ({ step * TPS, semi, P.s.vel ? std::max (0.12f, vel / 127.0f) : 0.9f, km == KM_CHOP ? chopSel : -2 });
        Command c; c.type = Command::Hit; c.pad = sel(); c.vel = (float) vel; c.semi = semi; c.chop = km == KM_CHOP ? chopSel : -1;
        if (! eng.playing.load()) eng.push (c);
    }
    touch();
}

void App::stepCopy (int from, int to)
{
    const int ti = selTrack (false);
    if (ti < 0) return;
    auto& d = P.tracks[(size_t) ti].get (P.s.pat);
    std::vector<HitEvent> add;
    for (auto& e : d.ev) if (e.t / TPS == from) { HitEvent n = e; n.t = to * TPS + e.t % TPS; add.push_back (n); }
    d.ev.erase (std::remove_if (d.ev.begin(), d.ev.end(), [to] (const HitEvent& e) { return e.t / TPS == to; }), d.ev.end());
    d.steps[(size_t) to] = d.steps[(size_t) from]; d.semi[(size_t) to] = d.semi[(size_t) from]; d.chopAt[(size_t) to] = d.chopAt[(size_t) from];
    for (auto& e : add) d.ev.push_back (e);
    touch();
    say ("Copied step " + std::to_string (from + 1) + " to " + std::to_string (to + 1));
}

void App::clearStep (int step)
{
    const int ti = selTrack (false);
    if (ti < 0) return;
    pushUndo();
    auto& d = P.tracks[(size_t) ti].get (P.s.pat);
    d.steps[(size_t) step] = 0;
    d.ev.erase (std::remove_if (d.ev.begin(), d.ev.end(), [step] (const HitEvent& e) { return e.t / TPS == step; }), d.ev.end());
    touch();
    say ("Cleared step " + std::to_string (step + 1));
}

void App::erasePadInPattern (int gi)
{
    bool any = false;
    for (auto& t : P.tracks) if (t.pad == gi && t.hasDataIn (P.s.pat)) any = true;
    if (! any) return;
    pushUndo();
    for (auto& t : P.tracks) if (t.pad == gi) t.pd[(size_t) P.s.pat].reset();
    touch();
    say ("Erased " + padLabel (gi) + " from pattern " + std::to_string (P.s.pat + 1) + " (Shift+Erase = undo)");
}

// ------------------------------------------------------------------ chops
void App::autoChop (int mode)
{
    Pad& pd = selPad();
    if (! pd.buf) { say ("Load a sample first"); return; }
    pushUndo();
    pd.markers.clear();
    static const int N[5] = { 0, 4, 8, 16, 0 };
    if (mode >= 1 && mode <= 3) for (int i = 1; i < N[mode]; ++i) pd.markers.push_back ((float) i / N[mode]);
    if (mode == 4) pd.markers = dsp::transientMarkers (*pd.buf, 16);
    chopSel = 0;
    touch();
    say (mode == 0 ? "Chops cleared" : mode == 4 ? strf ("%d chops at the transients", pd.numChops()) : strf ("%d equal chops", pd.numChops()));
}

void App::chopsToPads()
{
    Pad& src = selPad();
    if (! src.buf || src.markers.empty()) { say ("Make chops first (B1)"); return; }
    const auto segs = segments (src.buf.get(), src.markers);
    pushUndo();
    int placed = 0, first = -1, at = sel();
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const int gi = nextEmptyPad (at);
        if (gi < 0) break;
        auto sl = dsp::slice (*src.buf, segs[i].first, segs[i].second);
        Pad& np = P.pads[(size_t) gi];
        np = Pad(); np.buf = sl; np.name = src.name.substr (0, 12) + " " + std::to_string (i + 1);
        np.fx = src.fx; np.pitch = src.pitch;
        if (first < 0) first = gi;
        at = gi; ++placed;
    }
    touch();
    say (placed ? strf ("%d chops on pads %s to %s", placed, padLabel (first).c_str(), padLabel (at).c_str()) : std::string ("No empty pads left"));
}

void App::trimToChop()
{
    Pad& pd = selPad();
    if (! pd.buf) return;
    const auto segs = segments (pd.buf.get(), pd.markers);
    if (chopSel >= (int) segs.size()) return;
    pushUndo();
    pd.buf = dsp::slice (*pd.buf, segs[(size_t) chopSel].first, segs[(size_t) chopSel].second);
    pd.markers.clear(); pd.kmap.clear(); pd.hasKmap = false; chopSel = 0;
    touch();
    say ("Trimmed to the chop");
}

// ------------------------------------------------------------------ files
void App::browseTo (const std::string& dir0, const std::string& selectName)
{
    std::string dir = dir0;
    if (dir.size() < card.size() || dir.compare (0, card.size(), card) != 0 || ! io::isDir (dir)) dir = io::isDir (samplesDir) ? samplesDir : card;
    browseDir = dir;
    browseList.clear();
    if (dir != card) browseList.push_back ({ "..", true, 0 });
    for (auto& e : io::listDir (dir))
        if (e.dir || io::hasExt (e.name, ".wav") || io::hasExt (e.name, ".zip")) browseList.push_back (e);
    browseSel = 0;
    for (size_t i = 0; i < browseList.size(); ++i) if (browseList[i].name == selectName) browseSel = (int) i;
}

void App::refreshOpenList()
{
    openList.clear();
    for (auto& e : io::listDir (songsDir)) if (! e.dir && io::hasExt (e.name, ".zip")) openList.push_back (e.name);
    listSel = 0;
    for (size_t i = 0; i < openList.size(); ++i) if (songsDir + "/" + openList[i] == songPath) listSel = (int) i;
}

void App::loadSample (const std::string& path, int mode)
{
    std::string err;
    auto s = io::loadAudioFile (path, &err);
    if (! s) { say ("Cannot load: " + err); return; }
    const std::string name = io::baseName (path);
    pushUndo();
    if (mode == 2)
    {
        Pad& pd = selPad();
        if (! pd.buf) { setPadSample (sel(), s, name); say ("Loaded " + name + " on " + padLabel (sel())); return; }
        if ((int) pd.layers.size() >= MAXLAY) { say ("A pad holds 8 layers"); return; }
        Layer L; L.buf = s; L.name = name; pd.layers.push_back (L); touch();
        say (strf ("Layer %d on %s: %s", (int) pd.layers.size(), padLabel (sel()).c_str(), name.c_str()));
        return;
    }
    int gi = sel();
    if (mode == 1) { gi = nextEmptyPad (sel()); if (gi < 0) { say ("No empty pad"); return; } }
    setPadSample (gi, s, name);
    select (gi);
    say ("Loaded " + name + " on " + padLabel (gi));
}

void App::runJob (const std::string& title, std::function<std::function<void()>()> work)
{
    if (jobRunning.load()) { say ("Busy - wait a moment"); return; }
    if (job.joinable()) job.join();
    jobTitle = title; jobProgress.store (0); jobRunning.store (true);
    job = std::thread ([this, work]
    {
        auto done = work();
        std::lock_guard<std::mutex> l (jobLock);
        jobDone = std::move (done);
        jobRunning.store (false);
    });
}

void App::openSong (const std::string& path)
{
    Command st; st.type = Command::Stop; eng.push (st);
    runJob ("Opening " + io::fileName (path), [this, path]() -> std::function<void()>
    {
        auto np = std::make_shared<Project>();
        std::string msg;
        const std::string marker = root + "/.opening";
        io::writeFileAtomic (marker, path);
        const bool ok = loadSong (path, *np, msg);
        ::remove (marker.c_str());
        return [this, ok, np, msg, path]
        {
            if (! ok) { say ("Cannot open: " + msg, 4); return; }
            P = std::move (*np);
            songPath = path; unsaved = false; prefs.lastSong = path; savePrefs();
            undoP.reset(); redoP.reset();
            km = selPad().keyMode == MODE_CHOP ? KM_CHOP : KM_PADS;
            bankPage = sel() / 16; seqBar = 0; songSel = 0;
            snapDirty = true; builder.clear();
            say (msg, 3);
            if (pg == Page::Open) setPage (Page::Main);
        };
    });
}

void App::saveSong (bool asNew)
{
    std::string path = songPath;
    if (asNew || path.empty() || path.compare (0, songsDir.size(), songsDir) != 0)
    {
        // a new name: SESSION_01, SESSION_02 ...
        for (int n = 1; n < 1000; ++n)
        {
            const std::string nm = strf ("SESSION_%02d", n);
            if (! io::exists (songsDir + "/" + nm + ".zip")) { P.name = nm; path = songsDir + "/" + nm + ".zip"; break; }
        }
    }
    auto copy = std::make_shared<Project> (P);
    runJob ("Saving " + io::fileName (path), [this, copy, path]() -> std::function<void()>
    {
        std::string err;
        const bool ok = cx::saveSong (*copy, path, err);
        return [this, ok, err, path]
        {
            if (! ok) { say ("Save failed: " + err, 4); return; }
            songPath = path; unsaved = false; prefs.lastSong = path; savePrefs();
            say ("Saved " + io::fileName (path) + " in CONTRXL25/Songs", 3);
        };
    });
}

void App::newSong()
{
    Command st; st.type = Command::Stop; eng.push (st);
    pushUndo();
    const float mv = P.s.mvol;
    P.reset(); P.s.mvol = mv;
    songPath.clear(); unsaved = false; km = KM_PADS; bankPage = 0; seqBar = 0;
    snapDirty = true;
    say ("New song - empty pads (Shift+Erase = undo)");
}

void App::exportAudio (int what)
{
    if (snapDirty) rebuildSnapshot();
    auto snap = builder.build (P);
    std::vector<int> plan;
    if (what == 1 && ! P.chain.empty()) for (auto& c : P.chain) for (int r = 0; r < c.rep; ++r) plan.push_back (c.pat);
    else plan.push_back (P.s.pat);
    std::vector<int> tracks;
    if (what == 2)
        for (int i = 0; i < (int) P.tracks.size(); ++i)
        {
            bool used = false;
            for (int p : plan) if (P.tracks[(size_t) i].hasDataIn (p)) used = true;
            if (used) tracks.push_back (i);
        }
    const std::string base = exportDir + "/" + (P.name.empty() ? std::string ("SESSION") : P.name);
    const std::string tag = what == 1 && ! P.chain.empty() ? "song" : strf ("P%02d", P.s.pat + 1);
    std::vector<std::string> trackNames;
    for (int ti : tracks) { const int pad = P.tracks[(size_t) ti].pad; trackNames.push_back (padLabel (pad) + "_" + (P.pads[(size_t) pad].name.empty() ? "pad" : P.pads[(size_t) pad].name)); }
    runJob (what == 2 ? "Exporting stems" : "Exporting " + tag, [this, snap, plan, tracks, base, tag, trackNames, what]() -> std::function<void()>
    {
        int files = 0;
        auto write = [&] (int only, const std::string& path, float p0, float span)
        {
            int frames = 0;
            auto inter = Engine::renderPlan (snap, plan, only, frames, [this, p0, span] (float p) { jobProgress.store (p0 + p * span); }, only < 0);
            SampleData s; s.sampleRate = SR; s.ch[0].resize ((size_t) frames); s.ch[1].resize ((size_t) frames);
            for (int i = 0; i < frames; ++i) { s.ch[0][(size_t) i] = inter[(size_t) i * 2]; s.ch[1][(size_t) i] = inter[(size_t) i * 2 + 1]; }
            if (io::writeWav (path, s, 24)) ++files;
        };
        if (what == 2)
        {
            io::mkdirs (base + "_" + tag + "_stems");
            for (size_t k = 0; k < tracks.size(); ++k)
            {
                std::string n = trackNames[k];
                for (auto& ch : n) if (! isalnum ((unsigned char) ch) && ch != '-' && ch != '_') ch = '_';
                write (tracks[k], base + "_" + tag + "_stems/" + strf ("%02d_", (int) k + 1) + n + ".wav", (float) k / (float) tracks.size(), 1.0f / (float) tracks.size());
            }
        }
        else write (-1, base + "_" + tag + ".wav", 0, 1);
        return [this, files, what]
        {
            say (files ? strf (what == 2 ? "%d stems saved in CONTRXL25/Exports" : "Saved in CONTRXL25/Exports (%d file)", files) : std::string ("Export failed"), 4);
        };
    });
}

// ------------------------------------------------------------------ main loop work
void App::processEvents()
{
    EngineEvent e;
    while (eng.pop (e))
    {
        if (e.type == EngineEvent::RecHit) addRecordedHit (e);
        else if (e.type == EngineEvent::CaptureFull) say ("60 seconds - recording stopped");
        else if (e.type == EngineEvent::Finished) say ("End of song");
    }
}

void App::update()
{
    const double t = nowSeconds();
    if (splash && t > splashUntil && ! jobRunning.load()) splash = false;
    processEvents();
    if (eng.capState.load() == Engine::CAP_STOPPED) finishCapture();
    {
        std::function<void()> done;
        { std::lock_guard<std::mutex> l (jobLock); done = std::move (jobDone); jobDone = nullptr; }
        if (done) { if (job.joinable()) job.join(); done(); }
    }
    if (snapDirty && ! jobRunning.load()) rebuildSnapshot();
    for (int i = 0; i < NPADS; ++i)
    {
        const uint32_t h = eng.padHits[(size_t) i].load();
        if (h != lastHits[(size_t) i]) { lastHits[(size_t) i] = h; flash[(size_t) i] = 1.0f; }
    }
    {
        const float k = (float) std::exp (-(t - lastLv) / 0.12);   // meters fall back over ~0.1 s
        lastLv = t;
        for (int ch = 0; ch < 2; ++ch)
        {
            lvIn[ch] = std::max (eng.inPeak[ch].exchange (0), lvIn[ch] * k);
            lvOut[ch] = std::max (eng.outPeak[ch].exchange (0), lvOut[ch] * k);
        }
        for (int i = 0; i < NPADS; ++i) lvPad[(size_t) i] = std::max (eng.padPeak[(size_t) i].exchange (0), lvPad[(size_t) i] * k);
    }
    if (t - lastLeds > 0.03) { sendLeds(); lastLeds = t; }
}

bool App::wantsDraw() const { return true; }

// ------------------------------------------------------------------ lights
void App::padColour (int i, int r, int g, int b)
{
    std::array<uint8_t, 3> v { (uint8_t) clampv (r, 0, 127), (uint8_t) clampv (g, 0, 127), (uint8_t) clampv (b, 0, 127) };
    if (ledPadKnown[(size_t) i] && ledPad[(size_t) i] == v) return;
    ledPad[(size_t) i] = v; ledPadKnown[(size_t) i] = true;
    const uint8_t m[] = { 0xF0, 0x47, 0x7F, 0x5C, 0x65, 0x00, 0x04, (uint8_t) i, v[0], v[1], v[2], 0xF7 };
    hw.sendSurface (m, sizeof m);
}

void App::ledButton (int note, int value)
{
    if (ledCC[(size_t) note] == value) return;
    ledCC[(size_t) note] = value;
    const uint8_t m[] = { 0xB0, (uint8_t) note, (uint8_t) value };
    hw.sendSurface (m, 3);
}

void App::lightsOff()
{
    for (int i = 0; i < 17; ++i) padColour (i, 0, 0, 0);
    for (int n : { 0, 1, 6, 7, 9, 11, 35, 36, 40, 43, 53, 73, 74, 75, 80, 81, 82, 83, 84 }) ledButton (n, 0);
    for (int b = 57; b <= 64; ++b) ledButton (b, 0);
}

void App::sendLeds (bool force)
{
    if (force) { ledPadKnown.fill (false); ledCC.fill (-1); }
    const double t = nowSeconds();
    const bool playing = eng.playing.load();
    const int step = eng.curStep.load();
    // pads
    for (int p = 0; p < 16; ++p)
    {
        const int gi = bankPage * 16 + p;
        const Col bc = BANKCOL[gi >> 3];
        int r = (bc >> 16) & 255, g = (bc >> 8) & 255, b = bc & 255;
        float lvl = 0;
        if (splash) { const float ph = (float) std::fmod (t * 1.5 + p * 0.06, 1.0); lvl = 0.15f + 0.6f * (0.5f + 0.5f * std::sin (ph * 6.283f)); r = 244; g = 162; b = 29; }
        else if (pg == Page::Seq && stepMode)
        {
            const int st = seqBar * 16 + p;
            const int ti = P.trackFor (sel(), false);
            float v = 0;
            if (ti >= 0)
                if (auto* d = P.tracks[(size_t) ti].find (P.s.pat))
                {
                    if (d->steps[(size_t) st]) v = 0.9f;
                    for (auto& e : d->ev) if (e.t / TPS == st) v = std::max (v, e.v);
                }
            const Col sc = BANKCOL[sel() >> 3];
            r = (sc >> 16) & 255; g = (sc >> 8) & 255; b = sc & 255;
            lvl = v > 0 ? 0.55f + 0.45f * std::min (1.0f, v) : 0.0f;
            if (st >= P.plen[(size_t) P.s.pat]) lvl = 0;
            if (playing && step == st) { r = g = b = 255; lvl = 0.75f; }
            else if (v == 0 && p % 4 == 0 && st < P.plen[(size_t) P.s.pat]) { r = g = b = 255; lvl = 0.22f; }
        }
        else if (pg == Page::Chop || km == KM_CHOP)
        {
            const int ci = (pg == Page::Chop ? 0 : chopPage * 16) + p;
            const bool has = selPad().buf && ci < selPad().numChops();
            const Col sc = BANKCOL[sel() >> 3];
            r = (sc >> 16) & 255; g = (sc >> 8) & 255; b = sc & 255;
            lvl = has ? (ci == chopSel ? 0.85f : 0.42f) : 0.0f;
            if (has && eng.playheadChop.load() == ci && eng.playhead.load() >= 0) { lvl = 1.0f; }
        }
        else if (km == KM_KEYS)
        {
            const int n = P.s.root + p + 12 * oct;
            const bool cNote = n % 12 == 0, black = (n % 12 == 1 || n % 12 == 3 || n % 12 == 6 || n % 12 == 8 || n % 12 == 10);
            if (p == 0) { r = 244; g = 162; b = 29; lvl = 0.8f; }
            else if (cNote) { r = 244; g = 162; b = 29; lvl = 0.5f; }
            else if (black) { r = 40; g = 90; b = 255; lvl = 0.42f; }
            else { r = 200; g = 200; b = 220; lvl = 0.42f; }
            if (padHeld[(size_t) p]) lvl = 1.0f;
        }
        else
        {
            const bool has = P.pads[(size_t) gi].buf != nullptr;
            lvl = has ? 0.42f : 0.0f;
            if (gi == sel()) { r = 244; g = 162; b = 29; lvl = has ? 0.8f : 0.5f; }
            if (P.pads[(size_t) gi].muted) { r = 200; g = 0; b = 0; lvl = 0.35f; }
            lvl = std::max (lvl, flash[(size_t) gi] * 1.0f);
            if (flash[(size_t) gi] > 0 && gi != sel()) { const Col c2 = BANKCOL[gi >> 3]; r = (c2 >> 16) & 255; g = (c2 >> 8) & 255; b = c2 & 255; }
        }
        lvl = lvl * lvl;   // LED drive is linear, eyes are not
        padColour (p, (int) std::lround (r * 127 / 255.0f * lvl), (int) std::lround (g * 127 / 255.0f * lvl), (int) std::lround (b * 127 / 255.0f * lvl));
    }
    padColour (16, 120, 75, 10);   // fader light: CONTRXL orange
    // bank lights: the two banks on the pads
    for (int b = 0; b < 8; ++b) ledButton (57 + b, (b / 2 == bankPage) ? 127 : 0);
    // transport
    ledButton (S::PLAY, playing ? 2 : 1);
    ledButton (S::STOP, playing ? 1 : 2);
    ledButton (S::SEQREC, eng.recordArmed.load() ? ((eng.countInLeft.load() > 0 && std::fmod (t, 0.5) < 0.25) ? 0 : 2) : 0);
    const int cap = eng.capState.load();
    ledButton (S::SAMPLEREC, cap == Engine::CAP_REC ? 2 : (cap == Engine::CAP_WAIT || cap == Engine::CAP_COUNT) ? (std::fmod (t, 0.4) < 0.2 ? 2 : 0) : 0);
    // tap tempo blinks on the beat
    const double beat = 60.0 / P.s.bpm;
    const bool on = playing ? (step >= 0 && step % 4 == 0) : (std::fmod (t, beat) < beat * 0.25);
    ledButton (S::TAPTEMPO, on ? 2 : 1);
    // page buttons: the open page lights white
    auto pb = [this] (int note, bool open) { ledButton (note, open ? 2 : 0); };
    pb (S::SEQ, pg == Page::Seq); pb (S::SAMPLE, pg == Page::Sample); pb (S::CHOP, pg == Page::Chop); pb (S::PADFX, pg == Page::PadEdit);
    pb (S::KNOBFX, pg == Page::Mixer); pb (S::SAMPLESELECT, pg == Page::Browser); pb (S::LOOP, pg == Page::Song);
    ledButton (S::LEVELS16, km == KM_PADS ? 0 : km == KM_KEYS ? 2 : 4);
    ledButton (S::ERASE, eraseHeld ? 4 : 0);
    ledButton (S::MUTE, muteHeld ? 4 : 0);
    ledButton (S::NOTEREPEAT, repeatHeld ? 2 : 0);
    ledButton (S::PADBANK, 1);
    // level meters right of the screen: output (or input on the Sample page)
    auto mask = [] (float v)
    {
        const float db = 20.0f * std::log10 (std::max (1e-6f, v));
        int m = 0;
        const float th[5] = { -36, -18, -9, -3, -0.5f };
        for (int i = 0; i < 5; ++i) if (db >= th[i]) m |= 1 << i;
        return m;
    };
    for (int c = 0; c < 2; ++c) ledButton (83 + c, mask (pg == Page::Sample && prefs.sampleSrc == 0 ? lvIn[c] : lvOut[c]));
    for (auto& f : flash) f = std::max (0.0f, f - 0.09f);
}
} // namespace cx
