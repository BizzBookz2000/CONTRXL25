// CONTRXL 25 - MPC Sample edition: parameter lists and screens.
#include "app.h"

namespace cx
{
static const char* NOTE_N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static std::string noteStr (int n) { return std::string (NOTE_N[((n % 12) + 12) % 12]) + std::to_string (n / 12 - 2); }
static std::string onoff (bool b) { return b ? "On" : "Off"; }
static std::string pct (float v) { return std::to_string ((int) std::lround (v * 100)); }

// ------------------------------------------------------------------ lists
std::vector<Row> App::rows()
{
    switch (pg)
    {
        case Page::PadEdit: return padRows (editTab);
        case Page::Menu: return menuRows();
        case Page::Settings: return settingsRows();
        default: return {};
    }
}

std::vector<Row> App::padRows (int tab)
{
    std::vector<Row> r;
    Pad& p = selPad();
    FxParams& f = p.fx;
    auto T = [this] { touch(); };
    auto fl = [&r, T] (const std::string& label, float* v, float lo, float hi, float step, std::function<std::string (float)> show)
    {
        r.push_back ({ label, [v, show] { return show (*v); }, [v, lo, hi, step, T] (int d, bool fine) { *v = clampv (*v + d * step * (fine ? 0.1f : 1.0f), lo, hi); T(); }, nullptr });
    };
    auto bl = [&r, T] (const std::string& label, bool* v)
    {
        r.push_back ({ label, [v] { return onoff (*v); }, [v, T] (int, bool) { *v = ! *v; T(); }, nullptr });
    };
    auto p100 = [] (float v) { return pct (v); };
    if (tab == 0)
    {
        r.push_back ({ "Pad", [this] { return padLabel (sel()) + "  " + (selPad().buf ? padName (sel()) : std::string ("(empty)")); }, nullptr, nullptr });
        fl ("Level", &p.vol, 0, 2, 0.01f, p100);
        fl ("Pan", &p.pan, -1, 1, 0.02f, [] (float v) { return std::abs (v) < 0.01f ? std::string ("C") : strf ("%s%d", v < 0 ? "L" : "R", (int) std::lround (std::abs (v) * 50)); });
        fl ("Tune", &p.pitch, -PR, PR, 1.0f, [] (float v) { return strf ("%+.1f st", v); });
        r.push_back ({ "Filter", [&f] { return std::string (filterName (f.type)); }, [&f, T] (int d, bool) { f.type = (f.type + d + 4) % 4; T(); }, nullptr });
        fl ("Cutoff", &f.cut, 20, 20000, 0, [] (float v) { return v >= 1000 ? strf ("%.1f kHz", v / 1000) : strf ("%d Hz", (int) v); });
        r.back().adjust = [&f, T] (int d, bool fine) { f.cut = clampv (f.cut * std::pow (2.0f, d / (fine ? 48.0f : 12.0f)), 20.0f, 20000.0f); if (f.type == FT_OFF) f.type = FT_LP; T(); };
        fl ("Resonance", &f.res, 0, 1, 0.02f, p100);
        r.push_back ({ "Retro mode", [&f] { return std::string (retroName (f.rm)); }, [&f, T] (int d, bool) { f.rm = (f.rm + d + NRETRO) % NRETRO; T(); }, nullptr });
        bl ("Lo-fi", &f.lofi);
        r.push_back ({ "Lo-fi bits", [&f] { return std::to_string (f.bits); }, [&f, T] (int d, bool) { f.bits = clampv (f.bits + d, 2, 16); T(); }, nullptr });
        r.push_back ({ "Lo-fi rate", [&f] { return strf ("%.1f kHz", f.rate / 1000.0); }, [&f, T] (int d, bool fine) { f.rate = clampv (f.rate + d * (fine ? 100 : 1000), 2000, 44100); T(); }, nullptr });
        r.push_back ({ "Mute group", [&p] { return p.mg ? std::to_string (p.mg) : std::string ("Off"); }, [&p, T] (int d, bool) { p.mg = clampv (p.mg + d, 0, 16); T(); }, nullptr });
        r.push_back ({ "Link group", [&p] { return p.lg ? std::to_string (p.lg) : std::string ("Off"); }, [&p, T] (int d, bool) { p.lg = clampv (p.lg + d, 0, 16); T(); }, nullptr });
        r.push_back ({ "Key mode", [&p] { return std::string (p.keyMode == MODE_CHOP ? "CHOP (keys play chops)" : "KEYS (pitched)"); }, [&p, T] (int, bool) { p.keyMode = 1 - p.keyMode; T(); }, nullptr });
        r.push_back ({ "Root key", [this] { return noteStr (P.s.root); }, [this, T] (int d, bool) { P.s.root = clampv (P.s.root + d, 24, 96); T(); }, nullptr });
    }
    else if (tab == 1)
    {
        bl ("Saturation", &f.satOn); fl ("  Drive", &f.satDrive, 0, 1, 0.02f, p100); fl ("  Mix", &f.satMix, 0, 1, 0.02f, p100);
        bl ("Vinyl", &f.vinOn); fl ("  Crackle", &f.vinCrackle, 0, 1, 0.02f, p100); fl ("  Hiss", &f.vinHiss, 0, 1, 0.02f, p100); fl ("  Wow", &f.vinWow, 0, 1, 0.02f, p100); fl ("  Tone", &f.vinTone, 0, 1, 0.02f, p100);
        bl ("Chorus", &f.choOn); fl ("  Rate", &f.choRate, 0.05f, 5, 0.05f, [] (float v) { return strf ("%.2f Hz", v); }); fl ("  Depth", &f.choDepth, 0, 1, 0.02f, p100); fl ("  Mix", &f.choMix, 0, 1, 0.02f, p100);
        bl ("Flanger", &f.flaOn); fl ("  Rate", &f.flaRate, 0.02f, 3, 0.02f, [] (float v) { return strf ("%.2f Hz", v); }); fl ("  Depth", &f.flaDepth, 0, 1, 0.02f, p100); fl ("  Feedback", &f.flaFb, 0, 1, 0.02f, p100); fl ("  Mix", &f.flaMix, 0, 1, 0.02f, p100);
        bl ("Scatter", &f.scaOn);
        r.push_back ({ "  Rate", [&f] { static const char* D[4] = { "1/4", "1/8", "1/16", "1/32" }; return std::string (D[clampv ((int) std::lround (f.scaDiv), 1, 4) - 1]); }, [&f, T] (int d, bool) { f.scaDiv = (float) clampv ((int) std::lround (f.scaDiv) + d, 1, 4); T(); }, nullptr });
        fl ("  Amount", &f.scaAmt, 0, 1, 0.02f, p100); fl ("  Mix", &f.scaMix, 0, 1, 0.02f, p100);
        bl ("Delay", &f.delOn);
        r.push_back ({ "  Time", [&f] { static const char* D[5] = { "1/4", "3/16", "1/8", "3/32", "1/16" }; return std::string (D[clampv ((int) std::lround (f.delTime), 1, 5) - 1]); }, [&f, T] (int d, bool) { f.delTime = (float) clampv ((int) std::lround (f.delTime) + d, 1, 5); T(); }, nullptr });
        fl ("  Feedback", &f.delFb, 0, 0.95f, 0.02f, p100); fl ("  Tone", &f.delTone, 0, 1, 0.02f, p100); fl ("  Mix", &f.delMix, 0, 1, 0.02f, p100);
        bl ("Echo (tape)", &f.ecoOn); fl ("  Time", &f.ecoTime, 40, 1500, 10, [] (float v) { return strf ("%d ms", (int) v); });
        fl ("  Feedback", &f.ecoFb, 0, 0.93f, 0.02f, p100); fl ("  Tone", &f.ecoTone, 0, 1, 0.02f, p100); fl ("  Wobble", &f.ecoWob, 0, 1, 0.02f, p100); fl ("  Mix", &f.ecoMix, 0, 1, 0.02f, p100);
        bl ("Reverb", &f.revOn); fl ("  Size", &f.revSize, 0, 1, 0.02f, p100); fl ("  Damping", &f.revDamp, 0, 1, 0.02f, p100); fl ("  Mix", &f.revMix, 0, 1, 0.02f, p100);
    }
    else
    {
        auto act = [&r] (const std::string& label, std::function<void()> a) { r.push_back ({ label, [] { return std::string ("PUSH"); }, nullptr, std::move (a) }); };
        auto needs = [this] { if (! selPad().buf) { say ("The pad is empty"); return false; } return true; };
        act ("Normalize", [this, needs] { if (! needs()) return; pushUndo(); selPad().buf = dsp::normalized (*selPad().buf); touch(); say ("Normalized"); });
        act ("Reverse", [this, needs] { if (! needs()) return; pushUndo(); auto& p = selPad(); p.buf = dsp::reversed (*p.buf); for (auto& m : p.markers) m = 1 - m; std::sort (p.markers.begin(), p.markers.end()); touch(); say ("Reversed"); });
        act ("Make mono", [this, needs] { if (! needs()) return; pushUndo(); selPad().buf = dsp::mono (*selPad().buf); touch(); say ("Mono"); });
        auto stretch = [this, needs] (double alpha, const std::string& what)
        {
            if (! needs()) return;
            const auto src = selPad().buf; const int gi = sel();
            runJob ("Time-stretching", [this, src, alpha, gi, what]() -> std::function<void()>
            {
                auto out = dsp::wsolaStretch (*src, alpha);
                return [this, out, gi, src, what] { if (P.pads[(size_t) gi].buf != src) return; pushUndo(); P.pads[(size_t) gi].buf = out; touch(); say (what); };
            });
        };
        act ("Half speed (same pitch)", [stretch] { stretch (2.0, "Twice as long, same pitch"); });
        act ("Double speed (same pitch)", [stretch] { stretch (0.5, "Half as long, same pitch"); });
        for (int bars : { 1, 2, 4 })
            act (strf ("Fit to %d bar%s at the tempo", bars, bars > 1 ? "s" : ""), [this, stretch, bars]
            {
                if (! selPad().buf) return;
                const double want = bars * 4 * 60.0 / P.s.bpm, have = selPad().buf->duration();
                if (have <= 0.01) return;
                stretch (want / have, strf ("Fits %d bar%s at %.1f BPM", bars, bars > 1 ? "s" : "", P.s.bpm));
            });
        act ("Copy pad", [this] { padClip = std::make_unique<Pad> (selPad()); say ("Copied " + padLabel (sel())); });
        act ("Paste pad", [this] { if (! padClip) { say ("Copy a pad first"); return; } pushUndo(); selPad() = *padClip; touch(); say ("Pasted on " + padLabel (sel())); });
        act ("Pad on a key (PADS on keys)", [this]
        {
            // the next free key from C3 plays this pad (CONTRXL "Put this pad on a key")
            for (int k = 0; k < 128; ++k) if (P.s.keyPad[(size_t) k] == sel() + 1) { P.s.keyPad[(size_t) k] = 0; }
            int key = -1;
            for (int k = 0; k < 61 && key < 0; ++k) if (P.s.keyPad[(size_t) k] == 0) key = k;
            if (key < 0) return;
            P.s.keyPad[(size_t) key] = sel() + 1; P.s.keyPads = true; touch();
            say (padLabel (sel()) + " is on key " + noteStr (KEYBASE + key) + " (MIDI keyboard)");
        });
        act ("Keys play pads: " + onoff (P.s.keyPads), [this] { P.s.keyPads = ! P.s.keyPads; touch(); say (P.s.keyPads ? "MIDI keys play the pads you put on them" : "MIDI keys play the selected pad"); });
        for (int i = 0; i < (int) p.layers.size(); ++i)
        {
            Layer& L = p.layers[(size_t) i];
            r.push_back ({ strf ("Layer %d vol", i + 1), [&L] { return L.name.substr (0, 14) + "  " + pct (L.s.vol); }, [&L, T] (int d, bool) { L.s.vol = clampv (L.s.vol + d * 0.02f, 0.0f, 2.0f); T(); }, nullptr });
            r.push_back ({ strf ("Layer %d tune", i + 1), [&L] { return strf ("%+.1f st", L.s.pitch); }, [&L, T] (int d, bool fine) { L.s.pitch = clampv (L.s.pitch + d * (fine ? 0.1f : 1.0f), -36.0f, 36.0f); T(); }, nullptr });
            r.push_back ({ strf ("Remove layer %d", i + 1), [] { return std::string ("PUSH"); }, nullptr, [this, i] { pushUndo(); auto& ls = selPad().layers; if (i < (int) ls.size()) ls.erase (ls.begin() + i); listSel = 0; touch(); say ("Layer removed"); } });
        }
        act ("Clear pad", [this] { ask ("Clear " + padLabel (sel()) + "?", [this] { pushUndo(); const int mg = selPad().mg; selPad() = Pad(); selPad().mg = mg; touch(); say ("Cleared (Shift+Erase = undo)"); }); });
    }
    return r;
}

std::vector<Row> App::menuRows()
{
    std::vector<Row> r;
    auto act = [&r] (const std::string& label, const std::string& hint, std::function<void()> a) { r.push_back ({ label, [hint] { return hint; }, nullptr, std::move (a) }); };
    act ("Save song", songPath.empty() ? "new file" : io::fileName (songPath), [this] { saveSong (false); });
    act ("Save as new song", "SESSION_xx.zip", [this] { saveSong (true); });
    act ("Open song", "CONTRXL25/Songs", [this] { setPage (Page::Open); });
    act ("New song", "empty pads", [this] { ask ("Start a new song?" + std::string (unsaved ? " Unsaved changes are lost." : ""), [this] { newSong(); setPage (Page::Main); }); });
    act ("Load the CONTRXL kit", "12 sounds + beat", [this] { ask ("Replace the song with the CONTRXL kit?", [this] { pushUndo(); makeFactoryKit(); setPage (Page::Main); say ("CONTRXL kit loaded on A1-B4"); }); });
    act ("Export pattern", strf ("P%02d .wav", P.s.pat + 1), [this] { exportAudio (0); });
    act ("Export song", P.chain.empty() ? "add a song chain" : strf ("%d parts .wav", (int) P.chain.size()), [this] { if (P.chain.empty()) { say ("Build the song chain first (LOOP button)"); return; } exportAudio (1); });
    act ("Export stems", "one .wav per pad", [this] { exportAudio (2); });
    act ("Settings", "", [this] { setPage (Page::Settings); });
    act ("About", "ILLAHTAINMENT", [this] { setPage (Page::About); });
    act ("Exit CONTRXL 25", "back to the menu", [this] { ask (unsaved ? "Exit? The song has unsaved changes." : "Exit CONTRXL 25?", [this] { quit = true; }); });
    return r;
}

std::vector<Row> App::settingsRows()
{
    std::vector<Row> r;
    auto S = [this] { savePrefs(); };
    r.push_back ({ "Fader", [this] { static const char* F[3] = { "Pad level", "Pad filter", "Master volume" }; return std::string (F[prefs.faderMode]); }, [this, S] (int d, bool) { prefs.faderMode = (prefs.faderMode + d + 3) % 3; S(); }, nullptr });
    r.push_back ({ "MIDI notes play", [this] { return std::string (prefs.midiMode ? "Pads (36 = A1)" : "Keyboard (selected pad)"); }, [this, S] (int, bool) { prefs.midiMode = 1 - prefs.midiMode; S(); }, nullptr });
    r.push_back ({ "Metronome", [this] { return onoff (P.s.metro); }, [this] (int, bool) { P.s.metro = ! P.s.metro; touch(); }, nullptr });
    r.push_back ({ "Count-in", [this] { return onoff (P.s.count); }, [this] (int, bool) { P.s.count = ! P.s.count; touch(); }, nullptr });
    r.push_back ({ "Quantize recording", [this] { return P.s.quant ? std::string ("1/16") : std::string ("Off (keep timing)"); }, [this] (int, bool) { P.s.quant = ! P.s.quant; touch(); }, nullptr });
    r.push_back ({ "Loop", [this] { return onoff (P.s.loop); }, [this] (int, bool) { P.s.loop = ! P.s.loop; touch(); }, nullptr });
    r.push_back ({ "Velocity", [this] { return onoff (P.s.vel); }, [this] (int, bool) { P.s.vel = ! P.s.vel; touch(); }, nullptr });
    r.push_back ({ "Choke (keys/chops)", [this] { return onoff (P.s.choke); }, [this] (int, bool) { P.s.choke = ! P.s.choke; touch(); }, nullptr });
    r.push_back ({ "Gate", [this] { return onoff (P.s.gate); }, [this] (int, bool) { P.s.gate = ! P.s.gate; touch(); }, nullptr });
    r.push_back ({ "Auto-normalize", [this] { return onoff (P.s.autoNorm); }, [this] (int, bool) { P.s.autoNorm = ! P.s.autoNorm; touch(); }, nullptr });
    r.push_back ({ "Note repeat", [this] { static const char* N[7] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" }; return std::string (N[prefs.repeatIdx]); },
                   [this, S] (int d, bool) { static const float V[7] = { 4, 2, 4.0f / 3, 1, 2.0f / 3, 0.5f, 1.0f / 3 }; prefs.repeatIdx = clampv (prefs.repeatIdx + d, 0, 6); eng.repeatSteps.store (V[prefs.repeatIdx]); S(); }, nullptr });
    r.push_back ({ "Rec timing offset", [this] { return strf ("%+d ms", (int) prefs.recOffsetMs); }, [this, S] (int d, bool fine) { prefs.recOffsetMs = clampv (prefs.recOffsetMs + d * (fine ? 1.0f : 5.0f), -100.0f, 100.0f); eng.recOffsetMs.store (prefs.recOffsetMs); S(); }, nullptr });
    r.push_back ({ "Input monitor", [this] { return prefs.monitor > 0 ? pct (prefs.monitor) : std::string ("Off"); }, [this, S] (int d, bool) { prefs.monitor = clampv (prefs.monitor + d * 0.05f, 0.0f, 1.0f); eng.monitor.store (prefs.monitor); hw.setInput (prefs.monitor > 0 || pg == Page::Sample ? prefs.input : 0); S(); }, nullptr });
    r.push_back ({ "Input gain", [this] { return strf ("%+.1f dB", prefs.inputGainDb); }, [this, S] (int d, bool fine) { prefs.inputGainDb = clampv (prefs.inputGainDb + d * (fine ? 0.25f : 1.0f), -12.0f, 24.0f); eng.inputGain.store (std::pow (10.0f, prefs.inputGainDb / 20.0f)); S(); }, nullptr });
    r.push_back ({ "Browser audition", [this] { return onoff (prefs.autoAudition); }, [this, S] (int, bool) { prefs.autoAudition = ! prefs.autoAudition; S(); }, nullptr });
    return r;
}

std::string App::pageTitle() const
{
    switch (pg)
    {
        case Page::Main: return km == KM_PADS ? "PADS" : km == KM_KEYS ? "KEYS" : "CHOP";
        case Page::PadEdit: return "PAD " + padLabel (P.s.sel);
        case Page::Sample: return "SAMPLE";
        case Page::Chop: return "CHOP " + padLabel (P.s.sel);
        case Page::Seq: return "SEQUENCER";
        case Page::Song: return "SONG";
        case Page::Mixer: return "MIXER";
        case Page::Browser: return "BROWSE";
        case Page::Menu: return "MENU";
        case Page::Open: return "OPEN SONG";
        case Page::Settings: return "SETTINGS";
        case Page::About: return "ABOUT";
    }
    return "";
}

std::array<std::string, 3> App::softLabels()
{
    if (! confirmText.empty()) return { "NO", "", "YES" };
    switch (pg)
    {
        case Page::Main: return { std::string ("VEL ") + (P.s.vel ? "ON" : "OFF"), std::string ("CHOKE ") + (P.s.choke ? "ON" : "OFF"), std::string ("GATE ") + (P.s.gate ? "ON" : "OFF") };
        case Page::PadEdit: return { "SOUND", "FX", "TOOLS" };
        case Page::Sample: return { prefs.input == 1 ? "LINE IN" : "MIC", prefs.sampleSrc ? "RESAMPLE" : "INPUT", std::string ("THRESH ") + (prefs.thrOn ? "ON" : "OFF") };
        case Page::Chop: return { shift ? "AUTO" : "AUTO CHOP", shift ? "DELETE" : "SPLIT", shift ? "TRIM" : "TO PADS" };
        case Page::Seq: return { stepMode ? "STEP" : "LIVE", strf ("BAR %d/%d", seqBar + 1, P.plen[(size_t) P.s.pat] / 16), shift ? "CLEAR PAT" : "COPY PAT" };
        case Page::Song: return { "INSERT", "DELETE", std::string ("SONG ") + (P.s.song ? "ON" : "OFF") };
        case Page::Mixer: return { selPad().muted ? "UNMUTE" : "MUTE", selPad().solo ? "UNSOLO" : "SOLO", "RESET" };
        case Page::Browser: return { std::string ("AUDITION ") + (prefs.autoAudition ? "ON" : "OFF"), "TO NEXT PAD", "+ LAYER" };
        case Page::Open: return { "BACK", "", "OPEN" };
        case Page::Menu: case Page::About: return { "BACK", "", "" };
        case Page::Settings: return { "BACK", "-", "+" };
    }
    return { "", "", "" };
}

std::array<std::pair<std::string, std::string>, 3> App::knobLabels()
{
    Pad& p = selPad();
    switch (pg)
    {
        case Page::Main:
            return { std::make_pair (std::string ("TUNE"), strf ("%+.1f", p.pitch)), std::make_pair (std::string ("LEVEL"), pct (p.vol)),
                     std::make_pair (std::string ("FILTER"), p.fx.type == FT_OFF ? std::string ("Off") : (p.fx.cut >= 1000 ? strf ("%.1fk", p.fx.cut / 1000) : strf ("%d", (int) p.fx.cut))) };
        case Page::Mixer: return { std::make_pair (std::string ("LEVEL"), pct (p.vol)), std::make_pair (std::string ("PAN"), std::abs (p.pan) < 0.01f ? std::string ("C") : strf ("%s%d", p.pan < 0 ? "L" : "R", (int) std::lround (std::abs (p.pan) * 50))), std::make_pair (std::string ("MASTER"), pct (P.s.mvol)) };
        case Page::Seq: return { std::make_pair (std::string ("LENGTH"), strf ("%d bar%s", P.plen[(size_t) P.s.pat] / 16, P.plen[(size_t) P.s.pat] > 16 ? "s" : "")), std::make_pair (std::string ("QUANTIZE"), P.s.quant ? std::string ("1/16") : std::string ("Off")), std::make_pair (std::string ("TEMPO"), strf ("%.1f", P.s.bpm)) };
        case Page::Song: return { std::make_pair (std::string ("PATTERN"), songSel < (int) P.chain.size() ? strf ("P%02d", P.chain[(size_t) songSel].pat + 1) : std::string ("-")), std::make_pair (std::string ("REPEATS"), songSel < (int) P.chain.size() ? strf ("x%d", P.chain[(size_t) songSel].rep) : std::string ("-")), std::make_pair (std::string ("TEMPO"), strf ("%.1f", P.s.bpm)) };
        case Page::Sample: return { std::make_pair (std::string ("THRESHOLD"), strf ("%.0f dB", prefs.thrDb)), std::make_pair (std::string ("GAIN"), strf ("%+.1f dB", prefs.inputGainDb)), std::make_pair (std::string ("MONITOR"), prefs.monitor > 0 ? pct (prefs.monitor) : std::string ("Off")) };
        case Page::Chop: return { std::make_pair (std::string ("START"), "coarse"), std::make_pair (std::string ("START"), "fine"), std::make_pair (std::string ("TUNE"), strf ("%+.1f", p.pitch)) };
        case Page::PadEdit:
        {
            auto r = rows();
            const std::string nm = listSel < (int) r.size() ? r[(size_t) listSel].label : "";
            std::string a = nm; while (! a.empty() && a[0] == ' ') a.erase (a.begin());
            return { std::make_pair (a.empty() ? std::string ("VALUE") : a, std::string ("adjust")), std::make_pair (std::string ("LEVEL"), pct (p.vol)), std::make_pair (std::string ("TUNE"), strf ("%+.1f", p.pitch)) };
        }
        case Page::Browser: return { std::make_pair (std::string ("SCROLL"), ""), std::make_pair (std::string (""), ""), std::make_pair (std::string (""), "") };
        default: return { std::make_pair (std::string ("SCROLL"), ""), std::make_pair (std::string ("VALUE"), ""), std::make_pair (std::string (""), "") };
    }
}

// ------------------------------------------------------------------ drawing
void App::drawMeter (Canvas& c, int x, int y, int w, int h, float v, bool vertical)
{
    c.fill (x, y, w, h, C::PANEL2);
    const float db = 20.0f * std::log10 (std::max (1e-6f, v));
    const float f = clampv ((db + 48.0f) / 48.0f, 0.0f, 1.0f);
    if (vertical)
    {
        const int hh = (int) (h * f);
        for (int yy = 0; yy < hh; ++yy) { const float t = (float) yy / h; c.fill (x, y + h - 1 - yy, w, 1, t > 0.93f ? C::RED : t > 0.8f ? C::YELLOW : C::GREEN); }
    }
    else
    {
        const int ww = (int) (w * f);
        for (int xx = 0; xx < ww; ++xx) { const float t = (float) xx / w; c.fill (x + xx, y, 1, h, t > 0.93f ? C::RED : t > 0.8f ? C::YELLOW : C::GREEN); }
    }
}

void App::drawWave (Canvas& c, const SampleData* s, int x, int y, int w, int h, Col col, const std::vector<float>* markers, int selChop)
{
    c.fill (x, y, w, h, 0x111216);
    if (! s || s->length() < 2) { c.text (x + w / 2, y + h / 2 - 6, "empty", F_SMALL, C::FAINT, CENTER); return; }
    if (waveSrc != s || waveW != w)
    {
        waveSrc = s; waveW = w; wave.assign ((size_t) w, { 0.0f, 0.0f });
        const int n = s->length();
        for (int i = 0; i < w; ++i)
        {
            const int a = (int) ((long long) i * n / w), b = std::max (a + 1, (int) ((long long) (i + 1) * n / w));
            float lo = 0, hi = 0;
            const int stepk = std::max (1, (b - a) / 256);
            for (int k = a; k < b && k < n; k += stepk)
                for (int ch = 0; ch < s->channels(); ++ch) { const float v = s->ch[ch][(size_t) k]; lo = std::min (lo, v); hi = std::max (hi, v); }
            wave[(size_t) i] = { lo, hi };
        }
    }
    if (markers && selChop >= 0)
    {
        const float a = selChop == 0 ? 0.0f : (selChop - 1 < (int) markers->size() ? (*markers)[(size_t) selChop - 1] : 1.0f);
        const float b = selChop < (int) markers->size() ? (*markers)[(size_t) selChop] : 1.0f;
        c.fill (x + (int) (a * w), y, std::max (1, (int) ((b - a) * w)), h, 0x2a2410);
    }
    const int mid = y + h / 2;
    c.hline (x, mid, w, 0x24262c);
    for (int i = 0; i < w; ++i)
    {
        const int y0 = mid - (int) (wave[(size_t) i].second * (h / 2 - 1)), y1 = mid - (int) (wave[(size_t) i].first * (h / 2 - 1));
        c.vline (x + i, std::min (y0, y1), std::max (1, std::abs (y1 - y0) + 1), col);
    }
    if (markers)
        for (size_t k = 0; k < markers->size(); ++k)
        {
            const int mx = x + (int) ((*markers)[k] * w);
            c.vline (mx, y, h, (int) k + 1 == selChop ? C::ORANGE : 0x8a8a90);
        }
}

void App::drawChrome (Canvas& c)
{
    // soft keys (the three buttons above the screen)
    const auto sl = softLabels();
    for (int k = 0; k < 3; ++k)
    {
        const int x = k * 107, w = k == 2 ? 106 : 105;
        c.fill (x, 0, w, 17, C::PANEL2);
        if (! sl[(size_t) k].empty()) c.text (x + w / 2, 2, sl[(size_t) k], F_BOLD, (pg == Page::PadEdit && k == editTab) ? C::ORANGE : C::TEXT, CENTER, w - 6);
    }
    // the red display: status line + transport
    c.fill (0, 18, SCREEN_W, 16, C::DISPLAY);
    const double t = nowSeconds();
    const bool playing = eng.playing.load();
    std::string right = strf ("P%02d", (playing ? eng.curPat.load() : P.s.pat) + 1);
    if (P.s.song && ! P.chain.empty()) right = "SONG " + right;
    right += strf ("  %.1f", P.s.bpm);
    const int rw = c.textWidth (right, F_BOLD);
    // transport symbol
    const int sx = SCREEN_W - 14;
    if (eng.recordArmed.load()) c.disc ((float) sx + 5, 26, 4.5f, C::RED);
    else if (playing) { for (int i = 0; i < 9; ++i) c.vline (sx + 1 + i, 22 + i / 2, 9 - (i / 2) * 2, C::GREEN); }
    else c.fill (sx + 1, 22, 8, 8, C::DISPTEXT);
    c.text (sx - 6, 19, right, F_BOLD, C::DISPTEXT, RIGHT);
    std::string msg = t < messageUntil ? message : pageTitle() + (jobRunning.load() ? "  " + jobTitle : "");
    if (eng.countInLeft.load() > 0) msg = strf ("Count-in %d", eng.countInLeft.load());
    c.text (5, 19, msg, F_BOLD, C::DISPTEXT, LEFT, SCREEN_W - rw - 30);
    // knobs K1-K3 below the screen
    const auto kl = knobLabels();
    for (int k = 0; k < 3; ++k)
    {
        const int x = k * 107, w = k == 2 ? 106 : 105;
        c.fill (x, 207, w, 33, C::PANEL);
        c.fill (x, 207, w, 1, C::LINE);
        c.text (x + 6, 209, "K" + std::to_string (k + 1), F_SMALL, C::ORANGE);
        c.text (x + 22, 209, kl[(size_t) k].first, F_SMALL, C::DIM, LEFT, w - 26);
        c.text (x + w / 2, 222, kl[(size_t) k].second, F_MID, C::TEXT, CENTER, w - 8);
    }
}

void App::drawPadGrid (Canvas& c, int x0, int y0, int w, int h)
{
    const int gw = (w - 9) / 4, gh = (h - 9) / 4;
    for (int p = 0; p < 16; ++p)
    {
        const int col = p % 4, row = 3 - p / 4;       // pad 1 is bottom-left, as on the MPC
        const int x = x0 + col * (gw + 3), y = y0 + row * (gh + 3);
        const int gi = bankPage * 16 + p;
        const Pad& pd = P.pads[(size_t) gi];
        const Col bc = BANKCOL[gi >> 3];
        std::string top, name;
        Col fillc = C::PANEL2;
        bool selFrame = false;
        float fl = 0;
        if (km == KM_KEYS && pg == Page::Main)
        {
            const int n = P.s.root + p + 12 * oct;
            top = noteStr (n);
            const bool black = (n % 12 == 1 || n % 12 == 3 || n % 12 == 6 || n % 12 == 8 || n % 12 == 10);
            fillc = black ? 0x15161b : 0x26282f;
            name = p == 0 ? padLabel (sel()) : "";
            if (padHeld[(size_t) p]) fl = 1;
        }
        else if (km == KM_CHOP && pg == Page::Main)
        {
            const int ci = chopPage * 16 + p;
            const bool has = selPad().buf && ci < selPad().numChops();
            top = strf ("CHOP %d", ci + 1);
            name = has ? "" : "-";
            selFrame = has && ci == chopSel;
            if (has && eng.playheadChop.load() == ci && eng.playhead.load() >= 0) fl = 0.8f;
        }
        else
        {
            top = padLabel (gi);
            name = pd.buf ? (pd.name.empty() ? std::string ("sample") : pd.name) : std::string();
            selFrame = gi == sel();
            fl = flash[(size_t) gi];
        }
        if (fl > 0) fillc = mix (fillc, bc, fl * 0.8f);
        c.round (x, y, gw, gh, 4, fillc);
        if (pd.muted && km == KM_PADS) c.text (x + gw - 4, y + 2, "M", F_SMALL, C::RED, RIGHT);
        if (! pd.layers.empty() && km == KM_PADS) c.text (x + gw - 4, y + gh - 12, "+" + std::to_string (pd.layers.size()), F_SMALL, C::DIM, RIGHT);
        c.text (x + 4, y + 2, top, F_SMALL, km == KM_PADS || pg != Page::Main ? bc : C::DIM);
        if (! name.empty()) c.text (x + 4, y + 14, name, F_SMALL, pd.buf || km != KM_PADS ? C::TEXT : C::FAINT, LEFT, gw - 7);
        if (km == KM_PADS || pg != Page::Main)
        {
            // a thin bar shows the level (and a dot when it is on the sequencer in this pattern)
            if (pd.buf) c.fill (x + 4, y + gh - 5, (int) ((gw - 8) * clampv (pd.vol / 2.0f, 0.0f, 1.0f)), 2, mix (C::PANEL2, bc, 0.7f));
            for (auto& t : P.tracks) if (t.pad == gi && t.hasDataIn (P.s.pat)) { c.disc ((float) (x + gw - 7), (float) (y + gh - 9), 2.2f, C::ORANGE); break; }
        }
        if (selFrame) c.roundFrame (x, y, gw, gh, 4, C::ORANGE, 2);
    }
}

void App::drawMain (Canvas& c)
{
    drawPadGrid (c, 3, 37, 194, 168);
    // the selected pad
    const int x = 202, w = 115;
    const Pad& pd = selPad();
    const Col bc = BANKCOL[sel() >> 3];
    c.round (x, 37, w, 168, 5, C::PANEL);
    c.text (x + 6, 40, padLabel (sel()), F_BIG, bc);
    std::string mode = km == KM_PADS ? "PADS" : km == KM_KEYS ? strf ("KEYS %+d", oct) : strf ("CHOP %d-%d", chopPage * 16 + 1, chopPage * 16 + 16);
    c.text (x + w - 6, 46, mode, F_SMALL, C::ORANGE, RIGHT);
    c.text (x + 6, 66, pd.buf ? pd.name : std::string ("empty pad"), F_BOLD, pd.buf ? C::TEXT : C::FAINT, LEFT, w - 12);
    drawWave (c, pd.buf.get(), x + 5, 84, w - 10, 40, bc, &pd.markers, -1);
    const float ph = eng.playhead.load();
    if (ph >= 0 && pd.buf) c.vline (x + 5 + (int) (ph * (w - 10)), 84, 40, C::TEXT);
    int yy = 130;
    auto line = [&] (const std::string& a, const std::string& b) { c.text (x + 6, yy, a, F_SMALL, C::DIM); c.text (x + w - 6, yy, b, F_SMALL, C::TEXT, RIGHT, w - 50); yy += 12; };
    if (pd.buf)
    {
        line ("Length", strf ("%.2f s %s", pd.buf->duration(), pd.buf->channels() > 1 ? "st" : "mono"));
        line ("Chops", std::to_string (pd.numChops()));
        if (pd.fx.rm > 0 || pd.fx.anyOn() || pd.fx.lofi)
        {
            std::string fx;
            if (pd.fx.rm > 0) fx += retroName (pd.fx.rm);
            if (pd.fx.anyOn()) fx += fx.empty() ? "FX on" : " + FX";
            if (pd.fx.lofi) fx += " lo-fi";
            line ("Sound", fx);
        }
        else line ("Layers", std::to_string (pd.layers.size()));
        line ("Groups", strf ("mute %s link %s", pd.mg ? std::to_string (pd.mg).c_str() : "-", pd.lg ? std::to_string (pd.lg).c_str() : "-"));
    }
    else
    {
        c.text (x + 6, yy, "SAMPLE SELECT: load", F_SMALL, C::DIM); yy += 12;
        c.text (x + 6, yy, "SAMPLE REC: record", F_SMALL, C::DIM); yy += 12;
    }
    c.text (x + w / 2, 191, "PUSH = MENU", F_SMALL, C::FAINT, CENTER);
}

void App::drawSeq (Canvas& c)
{
    const int pat = P.s.pat, len = P.plen[(size_t) pat];
    const bool playing = eng.playing.load();
    const int cur = playing && eng.curPat.load() == pat ? eng.curStep.load() : -1;
    // the tracks used in this pattern (and the selected pad)
    struct R { int pad; const PatternData* d; };
    std::vector<R> list;
    for (auto& t : P.tracks) if (t.hasDataIn (pat)) list.push_back ({ t.pad, t.find (pat) });
    bool selIn = false; for (auto& r : list) if (r.pad == sel()) selIn = true;
    if (! selIn) list.push_back ({ sel(), nullptr });
    std::stable_sort (list.begin(), list.end(), [] (const R& a, const R& b) { return a.pad < b.pad; });
    const int rowH = 16, top = 52, maxRows = 9;
    int selRow = 0; for (int i = 0; i < (int) list.size(); ++i) if (list[(size_t) i].pad == sel()) selRow = i;
    if (selRow < seqTrackScroll) seqTrackScroll = selRow;
    if (selRow >= seqTrackScroll + maxRows) seqTrackScroll = selRow - maxRows + 1;
    seqTrackScroll = clampv (seqTrackScroll, 0, std::max (0, (int) list.size() - maxRows));
    const int gx = 74, cw = 15;
    // header: step numbers of this bar
    c.text (4, 37, strf ("P%02d  %d/%d", pat + 1, seqBar + 1, len / 16), F_BOLD, C::ORANGE);
    for (int s = 0; s < 16; ++s)
    {
        const int st = seqBar * 16 + s;
        c.text (gx + s * cw + cw / 2, 38, std::to_string (s + 1), F_SMALL, st == cur ? C::TEXT : (s % 4 == 0 ? C::DIM : C::FAINT), CENTER);
    }
    for (int i = 0; i < maxRows && seqTrackScroll + i < (int) list.size(); ++i)
    {
        const auto& r = list[(size_t) (seqTrackScroll + i)];
        const int y = top + i * rowH;
        const bool isSel = r.pad == sel();
        const Col bc = BANKCOL[r.pad >> 3];
        if (isSel) c.fill (0, y - 1, SCREEN_W, rowH, 0x1d1a12);
        c.text (4, y + 1, padLabel (r.pad), F_BOLD, bc);
        c.text (24, y + 2, P.pads[(size_t) r.pad].name, F_SMALL, isSel ? C::TEXT : C::DIM, LEFT, gx - 28);
        for (int s = 0; s < 16; ++s)
        {
            const int st = seqBar * 16 + s;
            const int x = gx + s * cw;
            Col bg = (s / 4) % 2 ? 0x1a1b20 : 0x202228;
            if (st == cur) bg = 0x3a3c44;
            c.fill (x + 1, y, cw - 2, rowH - 3, bg);
            if (! r.d) continue;
            float v = r.d->steps[(size_t) st] ? 0.9f : 0.0f;
            int nOff = 0;
            for (auto& e : r.d->ev)
                if (e.t / TPS == st)
                {
                    v = std::max (v, e.v);
                    const int off = e.t % TPS;
                    if (off) nOff = off;
                }
            if (v > 0)
            {
                const int hh = std::max (3, (int) ((rowH - 5) * std::min (1.0f, v)));
                const int ox = nOff ? (int) ((cw - 6) * nOff / (float) TPS) : 0;   // live hits show where they really land
                c.fill (x + 2 + ox, y + (rowH - 3) - hh - 1, cw - 4 - (nOff ? 2 : 0), hh, isSel ? C::ORANGE : bc);
            }
        }
    }
    if ((int) list.size() > maxRows) c.text (SCREEN_W - 4, 37, strf ("%d/%d", seqTrackScroll + 1, (int) list.size()), F_SMALL, C::FAINT, RIGHT);
    // the whole pattern (all bars), playhead
    const int by = 198, bw = SCREEN_W - 8;
    c.fill (4, by, bw, 5, 0x1a1b20);
    c.fill (4 + bw * seqBar * 16 / len, by, bw * 16 / len, 5, 0x3b3420);
    if (cur >= 0) c.fill (4 + bw * cur / len, by, std::max (2, bw / len), 5, C::ORANGE);
    c.text (SCREEN_W - 4, 186, stepMode ? "STEP: pads = steps   SHIFT+pad: track" : "LIVE: pads play", F_SMALL, C::FAINT, RIGHT);
}

void App::drawSong (Canvas& c)
{
    c.text (6, 38, P.s.song ? "Song mode ON - PLAY plays this chain" : "Song mode OFF - PLAY plays the pattern", F_SMALL, P.s.song ? C::ORANGE : C::DIM);
    if (P.chain.empty()) { c.text (SCREEN_W / 2, 110, "INSERT (B1) adds the current pattern", F_TEXT, C::DIM, CENTER); return; }
    const int playing = eng.playing.load() && P.s.song ? eng.songIndex.load() : -1;
    const int rowH = 17, top = 54, vis = 8;
    int first = clampv (songSel - vis / 2, 0, std::max (0, (int) P.chain.size() - vis));
    double t = 0;
    for (int i = 0; i < first; ++i) t += P.chain[(size_t) i].rep * P.plen[(size_t) P.chain[(size_t) i].pat] * 15.0 / P.bpmOf (P.chain[(size_t) i].pat);
    for (int i = first; i < (int) P.chain.size() && i < first + vis; ++i)
    {
        const auto& it = P.chain[(size_t) i];
        const int y = top + (i - first) * rowH;
        if (i == songSel) c.round (4, y - 2, SCREEN_W - 8, rowH - 1, 3, 0x2a2414);
        if (i == playing) c.disc (14, (float) y + 6, 3.5f, C::GREEN);
        c.text (24, y, strf ("%2d", i + 1), F_TEXT, C::DIM);
        c.text (50, y, strf ("Pattern %02d", it.pat + 1), F_BOLD, i == songSel ? C::ORANGE : C::TEXT);
        c.text (150, y, strf ("x%d", it.rep), F_BOLD, C::TEXT);
        c.text (190, y, strf ("%d bar%s", P.plen[(size_t) it.pat] / 16 * it.rep, P.plen[(size_t) it.pat] / 16 * it.rep > 1 ? "s" : ""), F_SMALL, C::DIM);
        c.text (SCREEN_W - 10, y, strf ("%d:%04.1f", (int) (t / 60), std::fmod (t, 60.0)), F_SMALL, C::FAINT, RIGHT);
        t += it.rep * P.plen[(size_t) it.pat] * 15.0 / P.bpmOf (it.pat);
    }
}

void App::drawSample (Canvas& c)
{
    const int st = eng.capState.load();
    const bool input = prefs.sampleSrc == 0;
    const float* lv = input ? lvIn : lvOut;
    c.text (6, 38, input ? (prefs.input == 1 ? "LINE IN" : "BUILT-IN MIC") : "RESAMPLE (what CONTRXL plays)", F_BOLD, C::TEXT);
    std::string state = st == Engine::CAP_REC ? strf ("RECORDING %.1f s", eng.capLen.load() / SR) : st == Engine::CAP_WAIT ? "WAITING FOR SOUND" : st == Engine::CAP_COUNT ? "COUNT-IN" : "READY";
    c.text (SCREEN_W - 6, 38, state, F_BOLD, st == Engine::CAP_REC ? C::RED : st == Engine::CAP_IDLE ? C::DIM : C::YELLOW, RIGHT);
    // meters with the threshold line
    for (int ch = 0; ch < 2; ++ch)
    {
        const int y = 58 + ch * 18;
        c.text (6, y, ch ? "R" : "L", F_BOLD, C::DIM);
        drawMeter (c, 20, y + 1, SCREEN_W - 30, 12, lv[ch], false);
        if (prefs.thrOn && input)
        {
            const int tx = 20 + (int) ((SCREEN_W - 30) * clampv ((prefs.thrDb + 48.0f) / 48.0f, 0.0f, 1.0f));
            c.vline (tx, y - 1, 16, C::TEXT);
        }
    }
    for (int db : { -48, -36, -24, -12, -6, 0 })
    {
        const int x = 20 + (int) ((SCREEN_W - 30) * (db + 48.0f) / 48.0f);
        c.text (x, 92, std::to_string (db), F_SMALL, C::FAINT, db == 0 ? RIGHT : CENTER);
    }
    // where the take goes
    const int target = selPad().buf ? nextEmptyPad (sel()) : sel();
    c.round (6, 108, SCREEN_W - 12, 56, 5, C::PANEL);
    c.text (14, 113, "SAMPLE RECORD starts / stops (max 60 s)", F_SMALL, C::DIM);
    c.text (14, 128, target >= 0 ? "The take goes on pad " + padLabel (target) + (selPad().buf ? " (next empty pad)" : "") : std::string ("All pads are full"), F_BOLD, target >= 0 ? C::ORANGE : C::RED);
    c.text (14, 145, std::string ("Auto-normalize ") + (P.s.autoNorm ? "on" : "off") + "  -  saved in CONTRXL25/Recordings", F_SMALL, C::DIM, LEFT, SCREEN_W - 28);
    if (st == Engine::CAP_REC)
    {
        const float f = (float) eng.capLen.load() / (float) std::max (1, eng.capMax);
        c.fill (6, 172, SCREEN_W - 12, 8, 0x1a1b20);
        c.fill (6, 172, (int) ((SCREEN_W - 12) * f), 8, C::RED);
    }
    if (prefs.monitor > 0 && input) c.text (6, 188, "Monitoring the input - mind feedback with the speaker", F_SMALL, C::YELLOW);
}

void App::drawChop (Canvas& c)
{
    const Pad& pd = selPad();
    const Col bc = BANKCOL[sel() >> 3];
    c.text (6, 37, pd.buf ? pd.name : std::string ("Select a pad with a sample (SHIFT + pad)"), F_BOLD, C::TEXT, LEFT, 200);
    if (pd.buf) c.text (SCREEN_W - 6, 37, strf ("chop %d / %d", chopSel + 1, pd.numChops()), F_BOLD, C::ORANGE, RIGHT);
    drawWave (c, pd.buf.get(), 4, 56, SCREEN_W - 8, 104, bc, &pd.markers, chopSel);
    const float ph = eng.playhead.load();
    if (ph >= 0 && pd.buf) c.vline (4 + (int) (ph * (SCREEN_W - 8)), 56, 104, C::TEXT);
    if (pd.buf)
    {
        const auto segs = segments (pd.buf.get(), pd.markers);
        if (chopSel < (int) segs.size())
            c.text (6, 166, strf ("%.3f - %.3f s   (%.0f ms)", segs[(size_t) chopSel].first, segs[(size_t) chopSel].second, (segs[(size_t) chopSel].second - segs[(size_t) chopSel].first) * 1000), F_SMALL, C::DIM);
        // chop numbers under the waveform
        for (int i = 0; i < (int) segs.size() && i < 32; ++i)
        {
            const int x = 4 + (int) ((segs[(size_t) i].first + segs[(size_t) i].second) * 0.5 / pd.buf->duration() * (SCREEN_W - 8));
            if ((int) segs.size() <= 16) c.text (x, 145, std::to_string (i + 1), F_SMALL, i == chopSel ? C::ORANGE : C::FAINT, CENTER);
        }
    }
    c.text (6, 182, "Pads play chops 1-16   PUSH plays   wheel: pick a chop", F_SMALL, C::FAINT);
    c.text (6, 194, "SHIFT: B1 auto / B2 delete / B3 trim to chop", F_SMALL, C::FAINT);
}

void App::drawMixer (Canvas& c)
{
    const int x0 = 4, w = (SCREEN_W - 8) / 16;
    for (int p = 0; p < 16; ++p)
    {
        const int gi = bankPage * 16 + p;
        const Pad& pd = P.pads[(size_t) gi];
        const Col bc = BANKCOL[gi >> 3];
        const int x = x0 + p * w;
        if (gi == sel()) c.fill (x, 36, w - 1, 169, 0x2a2414);
        c.text (x + w / 2, 38, padLabel (gi), F_SMALL, bc, CENTER);
        // level fader + meter
        const int fy = 52, fh = 110;
        c.fill (x + 3, fy, 4, fh, 0x24262c);
        drawMeter (c, x + 9, fy, 5, fh, lvPad[(size_t) gi], true);
        const int ky = fy + fh - (int) (fh * clampv (pd.vol / 2.0f, 0.0f, 1.0f));
        c.fill (x + 1, ky - 2, 8, 4, pd.buf ? C::TEXT : C::FAINT);
        // pan dot
        c.hline (x + 2, 170, w - 4, 0x34363e);
        c.disc ((float) (x + w / 2 + pd.pan * (w / 2 - 3)), 170, 2.3f, pd.buf ? C::ORANGE : C::FAINT);
        if (pd.muted) c.text (x + w / 2, 178, "M", F_SMALL, C::RED, CENTER);
        if (pd.solo) c.text (x + w / 2, 189, "S", F_SMALL, C::YELLOW, CENTER);
    }
    c.text (SCREEN_W - 6, 194, padLabel (sel()) + "  " + padName (sel()), F_SMALL, C::DIM, RIGHT, 200);
}

void App::drawList (Canvas& c, const std::vector<Row>& r, int x, int y, int w, int h)
{
    const int rowH = 17, vis = h / rowH;
    if (listSel < listTop) listTop = listSel;
    if (listSel >= listTop + vis) listTop = listSel - vis + 1;
    listTop = clampv (listTop, 0, std::max (0, (int) r.size() - vis));
    for (int i = 0; i < vis && listTop + i < (int) r.size(); ++i)
    {
        const auto& row = r[(size_t) (listTop + i)];
        const int yy = y + i * rowH;
        const bool on = listTop + i == listSel;
        if (on) c.round (x, yy, w, rowH - 1, 3, 0x2e2813);
        const bool header = ! row.label.empty() && row.label[0] != ' ' && pg == Page::PadEdit && editTab == 1;
        c.text (x + 6, yy + 2, row.label, header ? F_BOLD : F_TEXT, on ? C::ORANGE : (header ? C::TEXT : C::DIM), LEFT, w / 2);
        if (row.value) c.text (x + w - 6, yy + 2, row.value(), F_BOLD, on ? C::TEXT : C::DIM, RIGHT, w / 2 - 4);
    }
    if ((int) r.size() > vis)
    {
        const int sh = std::max (10, h * vis / (int) r.size());
        const int sy = y + (h - sh) * listTop / std::max (1, (int) r.size() - vis);
        c.fill (x + w + 2, y, 2, h, 0x24262c); c.fill (x + w + 2, sy, 2, sh, C::ORANGE);
    }
}

void App::drawBrowser (Canvas& c)
{
    std::string rel = browseDir.size() > card.size() ? browseDir.substr (card.size()) : "/";
    c.text (6, 37, rel, F_SMALL, C::ORANGE, LEFT, SCREEN_W - 12);
    const int rowH = 16, top = 52, vis = 9;
    int first = clampv (browseSel - vis / 2, 0, std::max (0, (int) browseList.size() - vis));
    if (browseList.empty()) c.text (SCREEN_W / 2, 110, "No WAV files here", F_TEXT, C::DIM, CENTER);
    for (int i = first; i < (int) browseList.size() && i < first + vis; ++i)
    {
        const auto& e = browseList[(size_t) i];
        const int y = top + (i - first) * rowH;
        const bool on = i == browseSel;
        if (on) c.round (4, y - 1, SCREEN_W - 8, rowH - 1, 3, 0x2e2813);
        const bool song = io::hasExt (e.name, ".zip");
        c.text (10, y + 1, e.dir ? "[" + e.name + "]" : e.name, e.dir ? F_BOLD : F_TEXT, on ? C::ORANGE : (e.dir ? C::TEXT : song ? C::BLUE : C::DIM), LEFT, SCREEN_W - 80);
        if (! e.dir) c.text (SCREEN_W - 10, y + 2, song ? "song" : (e.size > 1048576 ? strf ("%.1f MB", e.size / 1048576.0) : strf ("%d kB", (int) (e.size / 1024))), F_SMALL, C::FAINT, RIGHT);
    }
    c.text (6, 196, "PUSH: load on " + padLabel (sel()) + "   -: up a folder", F_SMALL, C::FAINT);
}

void App::drawAbout (Canvas& c)
{
    c.image (10, 44, LOGO_L, 150, 150);
    c.text (172, 52, "CONTRXL 25", F_BIG, C::ORANGE);
    c.text (172, 80, "MPC Sample edition", F_BOLD, C::TEXT);
    c.text (172, 96, std::string ("Version ") + APP_VERSION, F_SMALL, C::DIM);
    c.text (172, 116, "(c) 2026 ILLAHTAINMENT.", F_SMALL, C::TEXT);
    c.text (172, 128, "All rights reserved.", F_SMALL, C::TEXT);
    c.text (172, 148, "Songs open in CONTRXL 25", F_SMALL, C::DIM);
    c.text (172, 160, "for Windows, Mac and web.", F_SMALL, C::DIM);
    c.text (172, 180, hw.status(), F_SMALL, C::FAINT, LEFT, 145);
}

void App::drawSplash (Canvas& c)
{
    c.clear (0);
    c.image ((SCREEN_W - 150) / 2, 18, LOGO_L, 150, 150);
    c.text (SCREEN_W / 2, 174, "CONTRXL 25", F_HUGE, C::ORANGE, CENTER);
    c.text (SCREEN_W / 2, 214, "ILLAHTAINMENT", F_BOLD, C::TEXT, CENTER);
    if (jobRunning.load()) c.text (SCREEN_W / 2, 228, jobTitle, F_SMALL, C::DIM, CENTER);
}

void App::draw (Canvas& c)
{
    if (splash) { drawSplash (c); return; }
    c.clear (C::BG);
    switch (pg)
    {
        case Page::Main: drawMain (c); break;
        case Page::Seq: drawSeq (c); break;
        case Page::Song: drawSong (c); break;
        case Page::Sample: drawSample (c); break;
        case Page::Chop: drawChop (c); break;
        case Page::Mixer: drawMixer (c); break;
        case Page::Browser: drawBrowser (c); break;
        case Page::About: drawAbout (c); break;
        case Page::PadEdit:
        {
            const Col bc = BANKCOL[sel() >> 3];
            c.fill (0, 36, 4, 170, bc);
            drawList (c, padRows (editTab), 8, 38, SCREEN_W - 16, 166);
            break;
        }
        case Page::Menu: drawList (c, menuRows(), 8, 38, SCREEN_W - 16, 166); break;
        case Page::Settings: drawList (c, settingsRows(), 8, 38, SCREEN_W - 16, 166); break;
        case Page::Open:
        {
            if (openList.empty()) { c.text (SCREEN_W / 2, 100, "No songs in CONTRXL25/Songs yet", F_TEXT, C::DIM, CENTER); c.text (SCREEN_W / 2, 118, "Copy .zip songs from the desktop app there", F_SMALL, C::FAINT, CENTER); break; }
            std::vector<Row> r;
            for (auto& n : openList) r.push_back ({ n, nullptr, nullptr, nullptr });
            drawList (c, r, 8, 38, SCREEN_W - 16, 166);
            break;
        }
    }
    drawChrome (c);
    if (jobRunning.load())
    {
        c.round (40, 96, 240, 50, 6, C::PANEL2);
        c.roundFrame (40, 96, 240, 50, 6, C::ORANGE);
        c.text (SCREEN_W / 2, 104, jobTitle, F_BOLD, C::TEXT, CENTER, 220);
        c.fill (56, 128, 208, 6, 0x111216);
        c.fill (56, 128, (int) (208 * clampv (jobProgress.load(), 0.0f, 1.0f)), 6, C::ORANGE);
    }
    if (! confirmText.empty())
    {
        c.round (24, 80, 272, 72, 6, C::PANEL2);
        c.roundFrame (24, 80, 272, 72, 6, C::ORANGE, 2);
        c.text (SCREEN_W / 2, 92, confirmText, F_BOLD, C::TEXT, CENTER, 256);
        c.text (SCREEN_W / 2, 124, "YES: B3 or PUSH      NO: B1 or STOP", F_SMALL, C::DIM, CENTER);
    }
}
} // namespace cx
