// Headless test of the controller's live-recording rules (which track a recorded hit goes into).
#include "app/Controller.h"
#include <iostream>

using namespace cx;
static int fails = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++fails;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    Engine engine;
    Controller ctl (engine);
    auto& P = ctl.P;
    for (auto& t : P.tracks) { t.pd.fill (std::nullopt); t.arm = false; }
    auto hit = [&] (int pad, int pat, double pos = 0.0)
    {
        EngineEvent e; e.type = EngineEvent::RecHit; e.a = pad; e.b = -1; e.c = pat; e.f = 0; e.g = 0.8f; e.pos = pos;
        ctl.applyRecHit (e);
    };
    auto stepsIn = [&] (int track, int pat) { auto* d = P.tracks[(size_t) track].find (pat); return d ? (int) d->ev.size() : 0; };

    // pattern 1: track 1 holds A1
    P.tracks[0].arm = true; ctl.ui.lane = 0;
    hit (0, 0);
    check (stepsIn (0, 0) == 1 && P.tracks[0].pad == 0, "pattern 1: track 1 records A1");
    // pattern 2: the same track takes A5 for pattern 2 only
    P.s.pat = 1;
    hit (4, 1);
    check (stepsIn (0, 1) == 1 && P.tracks[0].padIn (1) == 4 && P.tracks[0].padIn (0) == 0, "pattern 2: armed track 1 records A5 (its pattern-1 pad stays A1)");
    // several armed tracks: each new pad takes the next armed, empty track
    for (auto& t : P.tracks) t.arm = false;
    P.tracks[4].arm = true; P.tracks[5].arm = true; ctl.ui.lane = 9;
    hit (10, 1); hit (11, 1, 2.0); hit (10, 1, 4.0);
    check (stepsIn (4, 1) == 2 && P.tracks[4].padIn (1) == 10 && stepsIn (5, 1) == 1 && P.tracks[5].padIn (1) == 11,
           "two armed tracks: A11 -> track 5, A12 -> track 6, A11 again -> track 5");
    // the selected armed track wins when it can take the pad
    P.tracks[7].arm = true; ctl.ui.lane = 7;
    hit (20, 1);
    check (stepsIn (7, 1) == 1 && P.tracks[7].padIn (1) == 20, "the selected armed track gets the hit");
    // locked tracks are never written
    for (auto& t : P.tracks) t.arm = false;
    P.tracks[8].arm = true; P.tracks[8].lock = true; P.tracks[9].arm = true; ctl.ui.lane = 8;
    hit (30, 1);
    check (stepsIn (8, 1) == 0 && stepsIn (9, 1) == 1, "a locked track is skipped, the next armed track records");
    P.tracks[9].lock = true;
    hit (31, 1, 3.0);
    check (stepsIn (8, 1) == 0 && stepsIn (9, 1) == 1, "all armed tracks locked: nothing is recorded");
    // an armed track that already holds another pad in this pattern is not overwritten
    P.tracks[9].lock = false; for (auto& t : P.tracks) t.arm = false; P.tracks[9].arm = true; ctl.ui.lane = 9;
    hit (40, 1, 5.0);
    check (P.tracks[9].padIn (1) == 30 && stepsIn (9, 1) == 1, "a track holding another pad in this pattern keeps it");
    // nothing armed
    for (auto& t : P.tracks) t.arm = false;
    hit (2, 1);
    check (P.tracks[2].find (1) == nullptr, "nothing armed: nothing recorded");
    // mixer helpers
    ctl.armPadTrack (11);
    check (P.tracks[5].arm, "mixer R arms the track that plays that pad");
    ctl.armPadTrack (50);
    const int l50 = ctl.trackForPad (50);
    check (l50 >= 0 && P.tracks[(size_t) l50].arm, "mixer R on a pad without a track picks an empty one");
    // chop-mode keys stay on one track (each note keeps its own chop)
    for (auto& t : P.tracks) { t.pd.fill (std::nullopt); t.arm = false; t.lock = false; }
    P.s.pat = 2; P.tracks[0].arm = true; P.tracks[1].arm = true; ctl.ui.lane = 0;
    auto hitChop = [&] (int pad, int chop, double pos)
    {
        EngineEvent e; e.type = EngineEvent::RecHit; e.a = pad; e.b = chop; e.c = 2; e.f = 0; e.g = 0.8f; e.pos = pos;
        ctl.applyRecHit (e);
    };
    hitChop (6, 0, 0.0); hitChop (6, 3, 1.0); hitChop (6, 5, 2.0);
    {
        auto* d = P.tracks[0].find (2);
        check (d && d->ev.size() == 3 && P.tracks[1].find (2) == nullptr, "keys in chop mode: all notes on ONE track");
        bool chopsKept = d && d->ev.size() == 3;
        if (chopsKept) for (auto& x : d->ev) chopsKept &= (x.c == -2 || x.c == 3 || x.c == 5);
        check (chopsKept, "each note keeps the chop that was played");
    }
    // a pad's own armed track wins over the selected empty track
    P.tracks[3].arm = true; ctl.ui.lane = 1;   // track 2 selected + empty, track 4 plays A4
    hit (3, 2, 3.0);
    check (stepsIn (3, 2) == 1 && P.tracks[1].find (2) == nullptr, "A4 goes to armed track 4 (its own), not the selected empty track");
    // layer mode: every armed track records
    ctl.ui.recAllArmed = true;
    hit (3, 2, 6.0);
    check (stepsIn (3, 2) == 2 && stepsIn (0, 2) == 4 && stepsIn (1, 2) == 1, "Rec all armed: every armed track records the hit");
    ctl.ui.recAllArmed = false;

    // ---- MIDI: channel-specific links, MPK mini IV profile (pads on a channel, knobs, transport actions)
    {
        auto send = [&] (int st, int d1, int d2) { const juce::uint8 m[3] { (juce::uint8) st, (juce::uint8) d1, (juce::uint8) d2 }; engine.injectMidiForTest (m, 3); };
        auto drain = [&] (EngineEvent::Type want, int& a) { EngineEvent e; bool got = false; while (engine.pop (e)) { if (e.type == want && ! got) { a = e.a; got = true; } ctl.handleEventForTest (e); } return got; };
        int a = -1;
        ctl.midiMap.clear();
        ctl.midiMap["n93.4"] = MidiTarget { "action", 0, "playstop", "MPK Stop/Play" };
        ctl.pushMidiMap();
        ctl.statusText = {};
        send (0x93, 93, 127); drain (EngineEvent::MapFire, a);
        check (ctl.statusText.startsWith ("PLAY"), "a transport button learned on channel 4 runs Play/Stop");
        ctl.statusText = {};
        send (0x90, 93, 127); drain (EngineEvent::MapFire, a);
        check (ctl.statusText.isEmpty(), "the same note on another channel does not");
        check (Controller::sigName ("n93.4").contains ("ch4"), "link names show the channel");

        engine.padChannel.store (10);
        for (int k = 0; k < 16; ++k) engine.padNotes[(size_t) k].store (36 + k);
        ctl.ui.bank = 0; engine.uiBank.store (0);
        a = -1; send (0x99, 38, 100);
        check (drain (EngineEvent::PadSelect, a) && a == 2, "pad channel: note 38 plays pad 3 of this bank");
        send (0x89, 38, 0);
        a = -1; send (0x99, 45, 100);
        check (drain (EngineEvent::PadSelect, a) && a == 9, "bank B pad 2 plays pad 2 of the next bank");
        send (0x89, 45, 0);
        a = -1; send (0x90, 38, 100);
        check (! drain (EngineEvent::PadSelect, a), "the same note on the keys channel stays a key");

        for (int k = 0; k < 8; ++k) engine.knobCC[(size_t) k].store (70 + k);
        engine.knobRelative.store (false);
        ctl.ui.bank = 0; engine.uiBank.store (0);
        send (0xB0, 71, 127); drain (EngineEvent::PitchKnob, a);
        check (P.pads[1].pitch > 0, "knob 2 (absolute) turns pad 2's pitch up");
        engine.knobRelative.store (true);
        const float p0 = P.pads[2].pitch;
        for (int i = 0; i < 4; ++i) { send (0xB0, 72, 1); drain (EngineEvent::PitchKnobRel, a); }
        check (P.pads[2].pitch == p0 + 2, "endless knob 3: 4 clicks up = +2 st");
        for (int i = 0; i < 2; ++i) { send (0xB0, 72, 127); drain (EngineEvent::PitchKnobRel, a); }
        check (P.pads[2].pitch == p0 + 1, "endless knob 3: 2 clicks down = -1 st");
        // an endless knob in "absolute" mode: detected after 2 identical messages, no jump to -24 / +24
        engine.knobRelative.store (false);
        const float p3 = P.pads[3].pitch;
        for (int i = 0; i < 6; ++i) { send (0xB0, 73, 1); drain (EngineEvent::PitchKnob, a); }
        check (P.pads[3].pitch == p3 + 3, "endless knob found by itself: 6 clicks = +3 st (got " + juce::String (P.pads[3].pitch) + ")");
        for (int i = 0; i < 4; ++i) { send (0xB0, 73, 127); drain (EngineEvent::PitchKnob, a); }
        check (P.pads[3].pitch == p3 + 1, "and 4 clicks back = -2 st");
        // an absolute knob reaches the middle
        for (int v : { 20, 40, 64 }) { send (0xB0, 74, v); drain (EngineEvent::PitchKnob, a); }
        check (std::abs (P.pads[4].pitch) <= 1, "absolute knob in the middle = about 0 st (got " + juce::String (P.pads[4].pitch) + ")");
        send (0xB0, 74, 96); drain (EngineEvent::PitchKnob, a);
        check (P.pads[4].pitch > 10 && P.pads[4].pitch < 14, "absolute knob at 3/4 = about +12 st");

        // transport buttons toggle
        ctl.midiMap["n90.4"] = MidiTarget { "action", 0, "play", "Play" };
        ctl.midiMap["n91.4"] = MidiTarget { "action", 0, "stop", "Stop" };
        ctl.midiMap["n92.4"] = MidiTarget { "action", 0, "rec", "Rec" };
        ctl.pushMidiMap();
        auto press = [&] (int n) { send (0x93, n, 127); drain (EngineEvent::MapFire, a); send (0x83, n, 0); drain (EngineEvent::MapFire, a); };
        // (no audio device here, so each press comes 0.2 s after the last: past the double-press guard, inside the "just sent" window)
        auto pressT = [&] (int n) { juce::Thread::sleep (200); press (n); };
        ctl.stop(); juce::Thread::sleep (420);
        pressT (90); check (ctl.playingNow(), "Play starts");
        pressT (90); check (! ctl.playingNow(), "Play again stops");
        pressT (91); check (ctl.playingNow(), "Stop while stopped starts");
        pressT (91); check (! ctl.playingNow(), "Stop stops");
        pressT (92); const bool r1 = ctl.ui.rec; pressT (92);
        check (r1 && ! ctl.ui.rec, "Record again turns recording off");
        // a latching button: 1st push sends note on, 2nd push sends note off
        ctl.midiMap["n94.4"] = MidiTarget { "action", 0, "play", "Play" };
        ctl.midiMap["n95.4"] = MidiTarget { "action", 0, "rec", "Rec" };
        ctl.pushMidiMap();
        ctl.stop(); juce::Thread::sleep (420);
        send (0x93, 94, 127); drain (EngineEvent::MapFire, a);
        check (ctl.playingNow(), "latching Play: on = play");
        juce::Thread::sleep (380);
        send (0x83, 94, 0); drain (EngineEvent::MapFire, a);
        check (! ctl.playingNow(), "latching Play: off (2nd push) = stop");
        if (ctl.ui.rec) ctl.toggleRec();
        send (0x93, 95, 127); drain (EngineEvent::MapFire, a); juce::Thread::sleep (380);
        const bool lr = ctl.ui.rec;
        send (0x93, 95, 0); drain (EngineEvent::MapFire, a);
        check (lr && ! ctl.ui.rec, "latching Record: 2nd push turns recording off");
        ctl.stop(); juce::Thread::sleep (200);
        // a momentary button: quick release does nothing
        send (0x93, 90, 127); drain (EngineEvent::MapFire, a); send (0x83, 90, 0); drain (EngineEvent::MapFire, a);
        check (ctl.playingNow(), "momentary Play: release doesn't stop it");
        ctl.stop(); juce::Thread::sleep (420);
        // a press sent twice (a note and MMC) counts once
        ctl.stop(); juce::Thread::sleep (420);
        send (0x93, 90, 127); drain (EngineEvent::MapFire, a);
        { const juce::uint8 mmc[6] { 0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7 }; engine.injectMidiForTest (mmc, 6); EngineEvent e; while (engine.pop (e)) ctl.handleEventForTest (e); }
        check (ctl.playingNow(), "note + MMC play from one press = play (not play then stop)");
        ctl.stop();
        // pad pressure (aftertouch) floods: they must not bury the pad hits
        {
            engine.padChannel.store (10);
            ctl.ui.bank = 0; engine.uiBank.store (0);
            for (int i = 0; i < 3000; ++i) send (0xA9, 37, i & 127);
            a = -1; send (0x99, 37, 100);
            int queued = 0; EngineEvent e; bool sel = false;
            while (engine.pop (e)) { ++queued; if (e.type == EngineEvent::PadSelect && e.a == 1) sel = true; }
            check (sel && queued < 50, "3000 pad-pressure messages: the pad hit still gets through (" + juce::String (queued) + " events queued)");
            send (0x89, 37, 0); while (engine.pop (e)) {}
        }
        engine.padChannel.store (0);
        for (auto& k : engine.knobCC) k.store (-1);
    }

    // ---- your own keyboard size: 49 keys
    {
        check (keyBaseFor (25) == 48 && keyBaseFor (32) == 53 && keyBaseFor (49) == 36 && keyBaseFor (61) == 36 && keyBaseFor (88) == 21,
               "first-key notes: 25 = C3, 32 = F3, 49 / 61 = C2, 88 = A0");
        ctl.setKeyboard (49);
        check (P.s.keys == 49, "keyboard set to 49 keys");
        ctl.setKeyboard (200); check (P.s.keys == 88, "too many keys is capped at 88");
        ctl.setKeyboard (49);
        juce::AudioBuffer<float> b (1, 49000); b.clear();
        ctl.P.s.sel = 0; ctl.P.pads[0].buf = makeSample (std::move (b), 44100.0); ctl.publish();
        ctl.equalChops (49);
        check (P.pads[0].numChops() == 49, "chop to 49: 49 equal chops");
        ctl.chopsToKeys();
        check (P.pads[0].keyMode == MODE_CHOP && ctl.statusText.contains ("1-49"), "all 49 chops fit on the keys");
        ctl.publish();
        int a = -1;
        auto keyLight = [&] (int note)
        {
            const juce::uint8 m[3] { 0x90, (juce::uint8) note, 100 }; engine.injectMidiForTest (m, 3);
            const juce::uint8 o[3] { 0x80, (juce::uint8) note, 0 }; engine.injectMidiForTest (o, 3);
            EngineEvent e; int got = -1; while (engine.pop (e)) { if (e.type == EngineEvent::KeyLight && e.b == 1 && got < 0) got = e.a; ctl.handleEventForTest (e); }
            return got;
        };
        engine.prepare (44100, 256);
        { juce::AudioBuffer<float> out (2, 256); juce::MidiBuffer mb; engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, mb); }
        engine.followOct.store (false); engine.midiOffset.store (0); engine.appOct.store (0); P.s.oct = 0;
        a = keyLight (36); check (a == 0, "49 keys: MIDI note 36 (C2) = key 1 (got " + juce::String (a) + ")");
        a = keyLight (84); check (a == 48, "49 keys: MIDI note 84 (C6) = key 49 (got " + juce::String (a) + ")");
        ctl.setKeyboard (25);
    }

    // ---- sample level meter: the engine measures the audio input all the time
    {
        engine.prepare (44100, 256);
        juce::AudioBuffer<float> inB (2, 256), out (2, 256);
        for (int i = 0; i < 256; ++i) { inB.setSample (0, i, 0.5f * std::sin (i * 0.1f)); inB.setSample (1, i, -0.25f * std::sin (i * 0.1f)); }
        juce::MidiBuffer mb;
        engine.inPeak[0].store (0); engine.inPeak[1].store (0);
        engine.process (inB.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 256, mb);
        const float l = engine.inPeak[0].exchange (0), r = engine.inPeak[1].exchange (0);
        check (std::abs (l - 0.5f) < 0.01f && std::abs (r - 0.25f) < 0.01f, "input meter: left 0.5 / right 0.25 (got " + juce::String (l) + " / " + juce::String (r) + ")");
        check (engine.inChannels.load() == 2, "input meter: 2 input channels");
        const float* mono[1] { inB.getReadPointer (0) };
        engine.process (mono, 1, out.getArrayOfWritePointers(), 2, 256, mb);
        check (engine.inPeak[1].exchange (0) > 0.49f, "mono input shows on both bars");
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, mb);
        check (engine.inChannels.load() == 0, "no input device: the meter says so");
    }

    // ---- Sample records into the next empty pad, never over a sample
    {
        for (auto& pd : P.pads) { pd.buf.reset(); pd.layers.clear(); pd.inst = {}; }
        juce::AudioBuffer<float> b1 (1, 1000); b1.clear();
        P.pads[3].buf = makeSample (std::move (b1), 44100.0);
        P.pads[4].buf = P.pads[3].buf;
        ctl.selectPad (3);
        check (ctl.padForNewSample() == 5, "A4 has a sample (A5 too): the next empty pad is A6");
        ctl.selectPad (2);
        check (ctl.padForNewSample() == 2, "an empty selected pad is used as it is");
        P.pads[63].buf = P.pads[3].buf; ctl.selectPad (63);
        check (ctl.padForNewSample() == 0, "H8 in use: goes round to A1");
        for (auto& pd : P.pads) pd.buf = P.pads[3].buf;
        check (ctl.padForNewSample() == -1, "all 64 pads in use: nowhere to sample");
        ctl.startSampling (true);
        check (! ctl.isSampling() && ctl.statusText.contains ("ALL 64 PADS"), "...and Sample says so instead of recording over one");
        for (auto& pd : P.pads) pd.buf.reset();
        ctl.selectPad (0);
    }

    // ---- pads on keys
    {
        for (auto& pd : P.pads) { pd.buf.reset(); pd.layers.clear(); pd.inst = {}; }
        juce::AudioBuffer<float> b1 (1, 4000); for (int i = 0; i < 4000; ++i) b1.setSample (0, i, 0.3f * std::sin (i * 0.05f));
        auto smp = makeSample (std::move (b1), 44100.0);
        ctl.setKeyboard (25);
        for (int gi : { 0, 1, 2, 3, 4, 5, 6, 7 }) P.pads[(size_t) gi].buf = smp;
        ctl.padsToKeys();
        check (P.s.keyPads && P.s.padOnKey (0) == 0 && P.s.padOnKey (7) == 7 && P.s.padOnKey (8) == -1, "8 pads -> keys 1-8");
        for (int gi = 8; gi < 16; ++gi) P.pads[(size_t) gi].buf = smp;
        ctl.padsToKeys();
        check (P.s.padOnKey (15) == 15 && P.s.padOnKey (16) == -1, "16 pads -> keys 1-16 (B1-B8 on keys 9-16)");
        for (int gi = 16; gi < 40; ++gi) P.pads[(size_t) gi].buf = smp;
        ctl.padsToKeys();
        check (P.s.padOnKey (24) == 24 && P.s.padOnKey (25) == -1 && ctl.statusText.contains ("MORE THAN"), "40 pads on 25 keys: the first 25, and it says so");
        ctl.setKeyboard (61); ctl.padsToKeys();
        check (P.s.padOnKey (39) == 39 && P.s.padOnKey (40) == -1, "61 keys: all 40 pads fit");
        ctl.setKeyboard (25);
        for (int gi = 0; gi < NPADS; ++gi) if (gi != 3 && gi != 20) P.pads[(size_t) gi].buf.reset();
        ctl.padsToKeys();
        check (P.s.padOnKey (0) == 3 && P.s.padOnKey (1) == 20, "gaps are skipped: A4 on key 1, C5 on key 2");
        ctl.setPadOnKey (5, 20);
        check (P.s.padOnKey (5) == 20, "a pad dropped on key 6 plays from key 6");
        ctl.movePad (20, 30, false);
        check (P.s.padOnKey (1) == 30 && P.s.padOnKey (5) == 30, "moving a pad: its keys follow it");
        // the engine: key 1 plays pad A4
        engine.prepare (44100, 256);
        juce::AudioBuffer<float> out (2, 256); juce::MidiBuffer mb;
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, mb);
        const auto h3 = engine.padHits[3].load(), hs = engine.padHits[(size_t) P.s.sel].load();
        ctl.noteOn (0, 100, 0);
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, mb);
        check (engine.padHits[3].load() == h3 + 1, "key 1 plays A4");
        check (out.getMagnitude (0, 256) > 0.01f, "...and it sounds");
        ctl.noteOff (0, 0);
        // a song keeps the map
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cx_keypads.zip");
        juce::String err; io::saveSong (P, f, err);
        Project q; juce::String msg; io::loadSong (f, q, msg);
        check (q.s.keyPads && q.s.padOnKey (0) == 3 && q.s.padOnKey (5) == 30, "saved in the song");
        f.deleteFile();
        ctl.setMode (MODE_KEYS);
        check (! P.s.keyPads && P.s.padOnKey (0) == 3, "KEYS mode turns pads-on-keys off (the map stays for PADS)");
        ctl.setKeysPlayPads (true); check (P.s.keyPads, "PADS turns it back on");
        ctl.clearKeys(); check (! P.s.keyPads && P.s.padOnKey (0) == -1, "Clear keys clears it");
        juce::ignoreUnused (hs);
        for (auto& pd : P.pads) pd.buf.reset();
    }

    // Rec timing tap test: taps that arrive ~0.7 step late (Bluetooth-like) set the correction
    {
        ctl.setRecOffset (0);
        P.s.bpm = 84; for (auto& b : P.pbpm) b = 0;
        const bool metroWas = P.s.metro;
        ctl.startTimingTest();
        check (ctl.timingTestRunning() && ctl.engine.calibrating.load(), "timing test: running, engine reports taps");
        const double stepMs = 60000.0 / 84.0 / 4.0;
        const double lates[] = { 0.70, 0.66, 0.74, 0.69, 3.0, 0.71, 0.68, 0.72 };   // one stray tap (3 steps = a beat late)
        int k = 0;
        for (double d : lates)
        {
            EngineEvent ev; ev.type = EngineEvent::CalibHit; ev.a = 0; ev.f = 84.0f; ev.pos = (k % 4) * 4 + d; ++k;
            ctl.handleEventForTest (ev);
        }
        // the stray 3.0 is nearest to the NEXT click (-1 step) and is dropped by the middle-half average
        check (! ctl.timingTestRunning() && ! ctl.engine.calibrating.load(), "timing test: stops after 8 taps");
        check (std::abs (ctl.recOffset() - 0.70 * stepMs) < 6.0, "timing test: correction = " + juce::String (ctl.recOffset(), 0) + " ms (expected ~" + juce::String (0.7 * stepMs, 0) + ")");
        check (P.s.metro == metroWas, "timing test: metronome setting restored");
        // early hits near the loop end (pos 15.8) count as 0.2 step early
        ctl.setRecOffset (0);
        ctl.startTimingTest();
        for (int i = 0; i < 8; ++i) { EngineEvent ev; ev.type = EngineEvent::CalibHit; ev.f = 84.0f; ev.pos = 15.8; ctl.handleEventForTest (ev); }
        check (std::abs (ctl.recOffset() + 0.2 * stepMs) < 2.0, "timing test: early taps across the loop end = negative correction");
        ctl.setRecOffset (0);
    }

    // clearing live (unquantized) hits from one step with a click
    {
        P.s.pat = 3; ctl.ui.page = 0;
        auto& d = P.tracks[0].get (3);
        d.steps.fill (0); d.ev.clear();
        d.ev.push_back ({ 4 * TPS + 2, 0, 0.8f, -2 });     // live hit on step 5
        d.ev.push_back ({ 4 * TPS + 20, 0, 0.8f, -2 });    // another one, same step
        d.ev.push_back ({ 9 * TPS + 1, 0, 0.8f, -2 });     // live hit on step 10
        d.steps[4] = 1;                                     // and a penciled hit on step 5
        check (ctl.stepHasEv (0, 4) && ! ctl.stepOn (0, 9), "live hits sit in steps 5 and 10");
        ctl.setStep (0, 9, true);                           // left click on a live step: adds a penciled hit, live hit stays
        check (ctl.stepOn (0, 9) && ctl.stepHasEv (0, 9), "left click no longer clears live hits");
        ctl.setStep (0, 9, false);
        check (ctl.clearStep (0, 9), "right click clears step 10");
        check (! ctl.stepHasEv (0, 9) && ctl.stepHasEv (0, 4), "right click clears the live hit on step 10 only");
        ctl.clearStep (0, 4);
        check (! ctl.stepHasEv (0, 4) && ! ctl.stepOn (0, 4) && P.tracks[0].find (3)->ev.empty(), "right click clears step 5: live hits and the penciled hit");
        check (! ctl.clearStep (0, 4), "right click on an empty step changes nothing");
        ctl.undo();
        check (ctl.stepHasEv (0, 4) && ctl.stepOn (0, 4), "undo brings them back");
        // copy step 5 (penciled + 2 live hits, pitch) to steps 9 and 13, and to track 2 step 1
        P.tracks[0].get (3).semi[4] = 5.0f;
        ctl.copyStep (0, 4, 0, 8); ctl.copyStep (0, 4, 0, 12, false); ctl.copyStep (0, 4, 1, 0, false);
        auto liveIn = [&] (int l, int st) { int n = 0; if (auto* dd = P.tracks[(size_t) l].find (3)) for (auto& x : dd->ev) if (x.t / TPS == st) ++n; return n; };
        const auto& d3 = *P.tracks[0].find (3);
        check (d3.steps[8] && d3.semi[8] == 5.0f && liveIn (0, 8) == 2 && liveIn (0, 12) == 2 && liveIn (1, 0) == 2 && P.tracks[1].find (3)->steps[0],
               "copy step: penciled hit, pitch and both live hits copied (also to another track)");
        bool sameOffset = false; for (auto& x : d3.ev) if (x.t == 8 * TPS + 2) sameOffset = true;
        check (sameOffset, "copy step: live hits keep their timing inside the step");
        ctl.copyStep (0, 4, 0, 8);
        check (liveIn (0, 8) == 2, "copying onto a step replaces it (no doubled hits)");
        ctl.undo(); ctl.undo();
        check (liveIn (0, 8) == 0 && liveIn (1, 0) == 0, "undo removes a whole copy drag in one step");
        // Select tool copy / paste now carries live hits
        ctl.ui.hasSel = true; ctl.ui.sel = { 0, 0, 4, 5 }; ctl.copySteps();
        ctl.ui.sel = { 0, 0, 10, 10 }; ctl.pasteSteps();
        check (liveIn (0, 10) == 2 && P.tracks[0].find (3)->steps[10], "select copy / paste keeps live hits");
        ctl.ui.hasSel = false;
        P.tracks[0].get (3).ev.clear(); P.tracks[0].get (3).steps.fill (0); P.s.pat = 0;
    }

    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << std::endl;
    return fails == 0 ? 0 : 1;
}
