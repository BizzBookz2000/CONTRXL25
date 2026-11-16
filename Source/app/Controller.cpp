#include "Controller.h"
#include "dsp/Offline.h"
#include "dsp/Stems.h"

namespace cx
{
// ------------------------------------------------------------------ background job with a progress window
namespace
{
    class Job : public juce::ThreadWithProgressWindow
    {
    public:
        using Work = std::function<void (Job&)>;
        Job (const juce::String& title, Work w, std::function<void (bool cancelled)> d)
            : juce::ThreadWithProgressWindow (title, true, true), work (std::move (w)), done (std::move (d)) {}
        void run() override { work (*this); }
        void threadComplete (bool cancelled) override
        {
            if (done) done (cancelled);
            juce::MessageManager::callAsync ([this] { delete this; });
        }
        void progress (float p) { setProgress (juce::jlimit (0.0, 1.0, (double) p)); }
        bool cancelled() { return threadShouldExit(); }
    private:
        Work work;
        std::function<void (bool)> done;
    };

    void runJob (const juce::String& title, Job::Work w, std::function<void (bool)> done)
    {
        auto* j = new Job (title, std::move (w), std::move (done));
        j->launchThread();
    }

    juce::String noteName (int n)
    {
        static const char* N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (N[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    }
}

Controller::Controller (Engine& e) : engine (e)
{
    publish();
    startTimerHz (30);
}

Controller::~Controller()
{
    stopTimer();
    if (stemThread) stemThread->stopThread (4000);
}

void Controller::status (const juce::String& s)
{
    statusText = s;
    sendChangeMessage();
}

void Controller::changed()
{
    publish();
    sendChangeMessage();
}

void Controller::publish()
{
    auto snap = builder.build (P, stems, trim, previewSample);
    for (int i = 0; i < NPADS; ++i) if (snap->pads[(size_t) i].fxOn) engine.ensureFx (i);
    engine.appOct.store (P.s.oct);
    engine.uiBank.store (ui.bank);
    syncPlugins();
    engine.setSnapshot (snap);
    releasePool.push_back (snap);
    // free snapshots nobody (engine, voices) uses any more - here, on the message thread
    for (size_t i = 0; i + 1 < releasePool.size();)
        if (releasePool[i].use_count() <= 1) releasePool.erase (releasePool.begin() + (long) i); else ++i;
}

juce::String Controller::contextLine() const
{
    const Pad& p = P.pads[(size_t) P.s.sel];
    return padLabel (P.s.sel) + (p.name.isNotEmpty() ? " - " + p.name : juce::String (" - empty")) + " - "
         + (p.keyMode == MODE_KEYS ? "KEYS" : "CHOP") + " - PAT " + juce::String (P.s.pat + 1) + " - " + juce::String (P.s.keys) + "-KEY";
}

SamplePtr Controller::displayBuffer (std::vector<float>* markers)
{
    Pad& p = selPad();
    if (markers) *markers = p.markers;
    if (! p.buf) return nullptr;
    return builder.playBuffer (p, stems);
}

// ------------------------------------------------------------------ undo
void Controller::pushUndo (const juce::String& key)
{
    const auto t = juce::Time::getMillisecondCounter();
    if (key.isNotEmpty() && key == lastUndoKey && t - lastUndoTime < 1200) { lastUndoTime = t; return; }
    lastUndoKey = key; lastUndoTime = t;
    if (key.isEmpty()) pullPluginStates();   // structural edits may remove a plug-in: keep its sound for undo
    undoStack.push_back (P);
    if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void Controller::undo()
{
    if (undoStack.empty()) { status ("NOTHING TO UNDO"); return; }
    const float mv = P.s.mvol;
    redoStack.push_back (P);
    P = std::move (undoStack.back()); undoStack.pop_back();
    P.s.mvol = mv; lastUndoKey = {};
    if (trim.on && trim.pad != P.s.sel) trim.on = false;
    ui.bank = P.s.sel >> 3;
    changed(); status ("UNDO");
}

void Controller::redo()
{
    if (redoStack.empty()) { status ("NOTHING TO REDO"); return; }
    const float mv = P.s.mvol;
    undoStack.push_back (P);
    P = std::move (redoStack.back()); redoStack.pop_back();
    P.s.mvol = mv; lastUndoKey = {};
    ui.bank = P.s.sel >> 3;
    changed(); status ("REDO");
}

// ------------------------------------------------------------------ pads
void Controller::selectPad (int gi)
{
    gi = juce::jlimit (0, NPADS - 1, gi);
    if (gi == P.s.sel && (gi >> 3) == ui.bank) return;
    if (trim.on && trim.pad != gi) trim.on = false;
    P.s.sel = gi; ui.bank = gi >> 3; ui.layerSel = -1; ui.stemSel = -1;
    changed();
}

void Controller::setBank (int b)
{
    ui.bank = juce::jlimit (0, 7, b);
    selectPad (ui.bank * 8 + (P.s.sel & 7));
    sendChangeMessage();
}

void Controller::hitPad (int gi, float vel, bool held)
{
    if (ui.manualLearn == 1) { ui.manualLearn = 0; extractChop (ui.extractChop, gi); return; }
    selectPad (gi);
    Command c; c.type = Command::PadHit; c.pad = gi; c.vel = vel; c.chop = -1; c.tag = held ? 1 : 0;
    engine.push (c);
}

void Controller::releasePad (int gi)
{
    Command c; c.type = Command::PadRelease; c.pad = gi;
    engine.push (c);
}

void Controller::setPitch (int gi, float v, bool quiet)
{
    pushUndo ("pitch" + juce::String (gi));
    P.pads[(size_t) gi].pitch = std::round (juce::jlimit (-PR, PR, v) * 100.0f) / 100.0f;
    publish();
    if (! quiet) statusText = padLabel (gi) + " PITCH " + fmtSt (P.pads[(size_t) gi].pitch) + " st";
    sendChangeMessage();
}

void Controller::copyPad()
{
    if (! selPad().buf && selPad().inst.empty()) { status ("THIS PAD IS EMPTY"); return; }
    pullPluginStates();
    padClip = selPad();
    status ("COPIED " + padLabel (P.s.sel) + " - " + selPad().name + " - SELECT A PAD AND PASTE");
}

void Controller::pastePad()
{
    if (! padClip) { status ("COPY A PAD FIRST"); return; }
    pushUndo();
    selPad() = *padClip;
    freshPluginCopies (selPad());
    changed(); status ("PASTED INTO " + padLabel (P.s.sel) + " - " + padClip->name);
}

void Controller::clearPad()
{
    if (! selPad().buf && selPad().inst.empty()) return;
    pushUndo();
    selPad() = Pad();
    if (trim.pad == P.s.sel) trim.on = false;
    changed(); status (padLabel (P.s.sel) + " CLEARED");
}

void Controller::clearAllPads()
{
    pushUndo();
    for (auto& p : P.pads) p = Pad();
    trim.on = false;
    changed(); status ("ALL PADS CLEARED - " CX_MOD_UP "+Z UNDOES");
}

void Controller::clearKeys()
{
    pushUndo();
    for (auto& p : P.pads) { p.hasKmap = true; p.kmap.clear(); }
    P.s.keyPad.fill (0); P.s.keyPads = false;
    changed(); status ("ALL CHOP-TO-KEY AND PAD-TO-KEY ASSIGNMENTS REMOVED - " CX_MOD_UP "+Z UNDOES");
}

void Controller::togglePadMute (int gi)
{
    pushUndo();
    auto& p = P.pads[(size_t) gi];
    p.muted = ! p.muted;
    changed(); status (padLabel (gi) + (p.muted ? " MUTED" : " UNMUTED"));
}

void Controller::movePad (int from, int to, bool copy)
{
    if (from == to || from < 0 || to < 0 || from >= NPADS || to >= NPADS) return;
    pushUndo();
    if (copy) { P.pads[(size_t) to] = P.pads[(size_t) from]; freshPluginCopies (P.pads[(size_t) to]); }
    else
    {
        std::swap (P.pads[(size_t) to], P.pads[(size_t) from]);
        for (auto& k : P.s.keyPad)   // keys keep playing the same sounds
            k = k == from + 1 ? to + 1 : k == to + 1 ? from + 1 : k;
    }
    P.s.sel = to; ui.bank = to >> 3;
    changed(); status ((copy ? "COPIED " : "MOVED ") + padLabel (from) + " TO " + padLabel (to));
}

void Controller::setPadVol (float v)
{
    pushUndo ("padvol" + juce::String (P.s.sel));
    selPad().vol = juce::jlimit (0.0f, 1.5f, v);
    changed();
}

// ------------------------------------------------------------------ keys
void Controller::noteOn (int idx, float vel, int tag)
{
    if (ui.padToKey >= 0)
    {
        const int gi = ui.padToKey; ui.padToKey = -1;
        setPadOnKey (idx, gi);
        return;
    }
    if (ui.manualLearn == 2)
    {
        ui.manualLearn = 0;
        if (idx >= 0 && idx < P.s.keys) assignChopKey (ui.extractChop, idx);
        return;
    }
    Command c; c.type = Command::NoteOn; c.note = idx; c.vel = vel; c.tag = tag;
    engine.push (c);
}

void Controller::noteOff (int idx, int tag)
{
    Command c; c.type = Command::NoteOff; c.note = idx; c.tag = tag;
    engine.push (c);
}

void Controller::setOct (int o)
{
    P.s.oct = juce::jlimit (-3, 3, o);
    engine.appOct.store (P.s.oct);
    changed(); status ("OCTAVE " + juce::String (P.s.oct >= 0 ? "+" : "") + juce::String (P.s.oct));
}

void Controller::padsToKeys()
{
    std::array<int, 128> map {};
    int n = 0;
    for (int gi = 0; gi < NPADS && n < P.s.keys; ++gi)
        if (P.pads[(size_t) gi].hasSound()) map[(size_t) n++] = gi + 1;
    if (n == 0) { status ("NO PADS WITH A SOUND YET - LOAD OR SAMPLE SOMETHING FIRST"); return; }
    pushUndo();
    P.s.keyPad = map; P.s.keyPads = true;
    changed();
    int total = 0; for (auto& pd : P.pads) if (pd.hasSound()) ++total;
    status (juce::String (n) + " PAD" + (n > 1 ? "S" : "") + " ON KEYS 1-" + juce::String (n)
            + (total > n ? " (" + juce::String (total - n) + " MORE THAN THE KEYBOARD HAS KEYS)" : juce::String())
            + " - DRAG A PAD ONTO A KEY TO CHANGE ONE");
}

void Controller::setPadOnKey (int key, int gi)
{
    if (key < 0 || key >= P.s.keys) return;
    pushUndo ("padkey");
    if (! P.s.keyPads)
    {
        // switching to pads on keys with one dragged pad: start from an empty keyboard
        P.s.keyPad.fill (0);
        P.s.keyPads = true;
    }
    P.s.keyPad[(size_t) key] = gi >= 0 && gi < NPADS ? gi + 1 : 0;
    changed();
    const int nn = P.keyBase() + key;
    const auto keyName = juce::MidiMessage::getMidiNoteName (nn, true, true, 3);
    status (gi >= 0 ? "KEY " + keyName + " PLAYS " + padLabel (gi) + " - KEYS NOW PLAY PADS" : "KEY " + keyName + " CLEARED");
}

void Controller::setKeysPlayPads (bool on)
{
    if (on == P.s.keyPads) return;
    bool any = false; for (auto v : P.s.keyPad) any |= v != 0;
    if (on && ! any) { padsToKeys(); return; }
    pushUndo ("mode");
    P.s.keyPads = on;
    changed();
    status (on ? "KEYS PLAY PADS - EACH KEY PLAYS ITS PAD" : padLabel (P.s.sel) + " IS BACK ON THE KEYS");
}

void Controller::setMode (int m)
{
    pushUndo ("mode");
    P.s.keyPads = false;   // Keys / Chop are the selected pad's modes
    selPad().keyMode = m;
    P.s.mode = m;
    changed(); status (padLabel (P.s.sel) + (m == MODE_KEYS ? " KEYS MODE - THE SAMPLE IS PITCHED ACROSS THE KEYS" : " CHOP MODE - KEYS PLAY THE CHOPS"));
}

void Controller::setKeyboard (int n)
{
    P.s.keys = validKeys (n);
    changed(); status (juce::String (P.s.keys) + " KEYS - UP TO " + juce::String (P.s.keys) + " CHOPS ON THE KEYS");
}

void Controller::setRoot (int r)
{
    P.s.root = juce::jlimit (24, 96, r);
    changed(); status ("ROOT " + noteName (P.s.root) + " PLAYS THE SAMPLE AT ITS OWN PITCH");
}

void Controller::toggle (bool& flag, const juce::String& name)
{
    flag = ! flag;
    changed(); status (name + (flag ? " ON" : " OFF"));
}

// ------------------------------------------------------------------ samples
SamplePtr Controller::prepBuf (SamplePtr b) const
{
    if (! b || ! P.s.autoNorm) return b;
    return dsp::normalized (*b);
}

void Controller::loadFiles (const juce::Array<juce::File>& files, int gi)
{
    if (files.isEmpty()) return;
    pushUndo();
    int at = gi, ok = 0;
    for (auto& f : files)
    {
        if (at > 63) break;
        juce::String err;
        auto b = io::decodeFile (f, &err);
        if (! b) { statusText = "COULD NOT READ " + f.getFileName() + " (" + err + ")"; continue; }
        Pad& p = P.pads[(size_t) at];
        p.buf = prepBuf (b); p.name = f.getFileNameWithoutExtension(); p.markers.clear(); p.sel = -1;
        p.keyMode = MODE_KEYS; p.hasKmap = false; p.kmap.clear();   // a new sample starts in Keys mode
        statusText = "LOADED " + padLabel (at) + " - " + p.name;
        ++at; ++ok;
    }
    if (ok == 0) { undoStack.pop_back(); sendChangeMessage(); return; }
    trim.on = false;
    P.s.sel = gi; ui.bank = gi >> 3;
    changed();
    if (ok > 1) status ("LOADED " + juce::String (ok) + " FILES FROM " + padLabel (gi));
}

void Controller::addLayers (const juce::Array<juce::File>& files)
{
    Pad& p = selPad();
    if (! p.buf) { status ("LOAD A SAMPLE ON THIS PAD FIRST - LAYERS PLAY ALONGSIDE IT"); return; }
    int room = MAXLAY - (int) p.layers.size();
    if (room <= 0) { status ("A PAD HOLDS " + juce::String (MAXLAY) + " LAYERS"); return; }
    pushUndo();
    int n = 0;
    for (auto& f : files)
    {
        if (room-- <= 0) break;
        auto b = io::decodeFile (f);
        if (! b) continue;
        Layer L; L.buf = prepBuf (b); L.name = f.getFileNameWithoutExtension();
        p.layers.push_back (std::move (L)); ++n;
    }
    changed(); status (juce::String (n) + " LAYER" + (n == 1 ? "" : "S") + " ADDED TO " + padLabel (P.s.sel) + " - THEY PLAY TOGETHER");
}

int Controller::padForNewSample() const
{
    for (int k = 0; k < NPADS; ++k)
    {
        const int gi = (P.s.sel + k) % NPADS;
        if (! P.pads[(size_t) gi].hasSound()) return gi;
    }
    return -1;
}

void Controller::startSampling (bool fromInput)
{
    if (isSampling()) { stopSampling (false); return; }
    // never record over a sample: use the next empty pad (and show it)
    const int target = padForNewSample();
    if (target < 0) { status ("ALL 64 PADS ARE IN USE - CLEAR A PAD TO SAMPLE INTO IT"); return; }
    const bool moved = target != P.s.sel;
    // sampling: show the pad it records into. Resampling: keep the selected pad, you may be playing it
    if (moved && fromInput) selectPad (target);
    captureTarget = target; captureFromInput = fromInput;
    if (! engine.startCapture (fromInput, ui.smpThr, ui.smpThrDb, ui.smpCount, 60.0)) { status ("SAMPLER BUSY"); return; }
    status ((fromInput ? "SAMPLING INTO " : "RESAMPLING INTO ") + padLabel (target)
            + (moved ? " (THE NEXT EMPTY PAD)" : "") + (fromInput ? " - PRESS STOP WHEN DONE" : " - PLAY SOMETHING"));
}

void Controller::stopSampling (bool cancel)
{
    engine.requestCaptureStop();
    if (cancel) captureTarget = -1;
}

void Controller::finishCapture()
{
    const int st = engine.captureState();
    auto b = engine.takeCapture();
    juce::ignoreUnused (st);
    if (captureTarget < 0) { status ("SAMPLING CANCELLED"); return; }
    if (! b) { status ("NOTHING RECORDED"); return; }
    pushUndo();
    Pad& p = P.pads[(size_t) captureTarget];
    p.buf = prepBuf (b);
    p.name = (captureFromInput ? "Sample " : "Resample ") + juce::Time::getCurrentTime().toString (false, true, true, true).replace (":", ".");
    p.markers.clear(); p.sel = -1;
    p.keyMode = MODE_KEYS; p.hasKmap = false; p.kmap.clear();
    unsaved = true;
    changed();
    status ("RECORDED " + juce::String (b->duration(), 1) + " s INTO " + padLabel (captureTarget) + " - SAVE THE SONG OR SAVE .WAV TO KEEP IT");
}

void Controller::savePadWav (const juce::File& f)
{
    Pad& p = selPad();
    if (! p.buf) { status ("SELECT A PAD WITH A SAMPLE FIRST"); return; }
    SamplePtr b = p.buf;
    if (ui.bakePitch && std::abs (p.pitch) > 0.001f) b = dsp::resampleStretch (*b, 1.0 / std::pow (2.0, p.pitch / 12.0));
    juce::AudioBuffer<float> st (2, b->length());
    st.copyFrom (0, 0, b->audio, 0, 0, b->length());
    st.copyFrom (1, 0, b->audio, juce::jmin (1, b->channels() - 1), 0, b->length());
    if (io::writeWav (f, st, b->sampleRate, ui.exportBits)) { unsaved = false; status ("SAVED " + f.getFileName()); }
    else status ("COULD NOT SAVE " + f.getFileName());
}

void Controller::reversePad()
{
    Pad& p = selPad();
    if (! p.buf) { status ("THIS PAD IS EMPTY"); return; }
    pushUndo(); trim.on = false;
    p.buf = dsp::reversed (*p.buf);
    for (auto& m : p.markers) m = 1.0f - m;
    std::sort (p.markers.begin(), p.markers.end());
    p.sel = -1;
    changed(); status (padLabel (P.s.sel) + " REVERSED - " CX_MOD_UP "+Z UNDOES");
}

void Controller::normalizePad()
{
    Pad& p = selPad();
    if (! p.buf) { status ("THIS PAD IS EMPTY"); return; }
    pushUndo();
    p.buf = dsp::normalized (*p.buf);
    changed(); status (padLabel (P.s.sel) + " NORMALIZED TO -1 dB");
}

void Controller::applyStretch()
{
    Pad& p = selPad();
    if (! p.buf) { status ("LOAD OR RECORD A SAMPLE FIRST"); return; }
    const double alpha = ui.stretchPct / 100.0;
    if (std::abs (alpha - 1.0) < 0.005) { status ("LENGTH IS ALREADY 100% - MOVE THE SLIDER OR USE MATCH BPM"); return; }
    const bool keep = ui.keepPitch;
    const int gi = P.s.sel;
    auto src = p.buf;
    const double old = src->duration();
    auto result = std::make_shared<SamplePtr>();
    runJob (keep ? "Stretching (pitch stays put)..." : "Resampling...",
            [src, alpha, keep, result] (Job& j)
            {
                *result = keep ? dsp::wsolaStretch (*src, alpha, [&j] (float f) { j.progress (f); })
                               : dsp::resampleStretch (*src, alpha);
            },
            [this, gi, result, old, keep] (bool cancelled)
            {
                if (cancelled || ! *result) { status ("STRETCH CANCELLED"); return; }
                pushUndo(); trim.on = false;
                P.pads[(size_t) gi].buf = *result;
                ui.stretchPct = 100;
                changed();
                status (juce::String (keep ? "STRETCHED " : "RESAMPLED ") + juce::String (old, 2) + " s -> " + juce::String ((*result)->duration(), 2) + " s - " CX_MOD_UP "+Z UNDOES");
            });
}

void Controller::matchBpm()
{
    if (ui.sampleBpm < 20) { status ("SET THE SAMPLE'S BPM FIRST"); return; }
    ui.stretchPct = (float) (ui.sampleBpm / P.bpmOf (P.s.pat) * 100.0);
    applyStretch();
}

// ------------------------------------------------------------------ chops
void Controller::setMarkers (std::vector<float> m)
{
    Pad& p = selPad();
    if (! p.buf) return;
    pushUndo();
    std::sort (m.begin(), m.end());
    m.erase (std::unique (m.begin(), m.end(), [] (float a, float b) { return std::abs (a - b) < 1.0e-5f; }), m.end());
    m.erase (std::remove_if (m.begin(), m.end(), [] (float x) { return x <= 0.0005f || x >= 0.9995f; }), m.end());
    p.markers = m; p.sel = -1; p.hasKmap = false; p.kmap.clear();
    changed();
}

void Controller::equalChops (int n)
{
    if (! selPad().buf) { status ("LOAD A SAMPLE FIRST"); return; }
    std::vector<float> m;
    for (int k = 1; k < n; ++k) m.push_back ((float) k / (float) n);
    setMarkers (m);
    status (juce::String (n) + " EQUAL CHOPS");
}

void Controller::transientChops()
{
    if (! selPad().buf) { status ("LOAD A SAMPLE FIRST"); return; }
    auto m = dsp::transientMarkers (*selPad().buf, 16);
    if (m.empty()) { status ("NO CLEAR TRANSIENTS FOUND"); return; }
    setMarkers (m);
    status (juce::String ((int) m.size() + 1) + " CHOPS AT THE TRANSIENTS");
}

void Controller::chopsToPads()
{
    Pad& p = selPad();
    if (! p.buf || p.markers.empty()) { status ("CHOP THE SAMPLE FIRST"); return; }
    const auto sg = segments (p.buf.get(), p.markers);
    const int n = (int) sg.size(), first = P.s.sel + 1;
    if (first + n > 64) { status ("NEED " + juce::String (n) + " PADS AFTER " + padLabel (P.s.sel) + ": ONLY " + juce::String (juce::jmax (0, 64 - first)) + " LEFT"); return; }
    pushUndo();
    const Pad src = p;
    for (int i = 0; i < n; ++i)
    {
        Pad np;
        np.buf = dsp::slice (*src.buf, sg[(size_t) i].first, sg[(size_t) i].second);
        np.name = src.name.substring (0, 14) + " " + juce::String (i + 1).paddedLeft ('0', 2);
        np.fx = src.fx; np.mg = src.mg;
        P.pads[(size_t) (first + i)] = np;
    }
    changed(); status (juce::String (n) + " CHOPS ON " + padLabel (first) + "-" + padLabel (first + n - 1) + " - " CX_MOD_UP "+Z UNDOES");
}

void Controller::chopsToKeys()
{
    Pad& p = selPad();
    if (! p.buf) { status ("LOAD A SAMPLE FIRST"); return; }
    if (p.markers.empty()) { status ("CHOP THE SAMPLE FIRST"); return; }
    pushUndo();
    p.hasKmap = false; p.kmap.clear();
    const int n = p.numChops();
    p.keyMode = MODE_CHOP;     // only this pad - other pads keep their own mode
    changed();
    status (n > P.s.keys ? juce::String (n) + " CHOPS - ONLY THE FIRST " + juce::String (P.s.keys) + " FIT ON THE KEYS"
                         : "CHOPS 1-" + juce::String (n) + " ARE ON THE KEYS - THE COLORED KEYS PLAY THEM");
}

void Controller::selectChop (int c)
{
    selPad().sel = c;
    sendChangeMessage();
}

void Controller::previewChop (int c)
{
    if (! selPad().buf) return;
    Command cmd; cmd.type = Command::Preview; cmd.pad = P.s.sel; cmd.chop = c;
    engine.push (cmd);
}

void Controller::stepRegion (int dir)
{
    auto& p = selPad();
    if (! p.buf) return;
    const int n = p.numChops();
    const int c = p.sel < 0 ? (dir > 0 ? 0 : n - 1) : juce::jlimit (0, n - 1, p.sel + dir);
    selectChop (c);
    previewChop (n > 1 ? c : -1);
}

void Controller::extractChop (int chop, int to)
{
    Pad& p = selPad();
    if (! p.buf) { status ("SELECT A CHOP FIRST"); return; }
    const auto sg = segments (p.buf.get(), p.markers);
    if (chop < 0 || chop >= (int) sg.size()) { status ("SELECT A CHOP FIRST"); return; }
    if (to == P.s.sel) { status ("PICK A DIFFERENT PAD - THE SAMPLE IS ON THIS ONE"); return; }
    pushUndo();
    Pad np;
    np.buf = dsp::slice (*p.buf, sg[(size_t) chop].first, sg[(size_t) chop].second);
    np.name = p.name.substring (0, 14) + " " + juce::String (chop + 1).paddedLeft ('0', 2);
    np.fx = p.fx; np.mg = p.mg;
    P.pads[(size_t) to] = np;
    changed(); status ("CHOP " + juce::String (chop + 1) + " -> " + padLabel (to) + " - " CX_MOD_UP "+Z UNDOES");
}

void Controller::assignChopKey (int chop, int key)
{
    Pad& p = selPad();
    const int n = p.numChops();
    if (chop < 0 || chop >= n) { status ("SELECT A CHOP FIRST"); return; }
    pushUndo();
    if (! p.hasKmap) { p.kmap.clear(); for (int i = 0; i < juce::jmin (n, P.s.keys); ++i) p.kmap[i] = i; }
    p.hasKmap = true;
    p.kmap[key] = chop;
    p.keyMode = MODE_CHOP;
    changed(); status ("CHOP " + juce::String (chop + 1) + " -> KEY " + juce::String (key + 1) + " - " CX_MOD_UP "+Z UNDOES");
}

// ------------------------------------------------------------------ trim
void Controller::trimOpen()
{
    if (! selPad().buf) { status ("LOAD A SAMPLE FIRST"); return; }
    trim = TrimState(); trim.on = true; trim.pad = P.s.sel;
    changed(); status ("TRIM - DRAG THE HANDLES, THEN APPLY. EVERYTHING YOU PLAY USES THE TRIMMED SOUND");
}

void Controller::trimSet (float a, float b, float fi, float fo, float g)
{
    if (! trim.on) return;
    trim.a = juce::jlimit (0.0f, 1.0f, a); trim.b = juce::jlimit (trim.a + 0.001f, 1.0f, b);
    trim.fi = juce::jmax (0.0f, fi); trim.fo = juce::jmax (0.0f, fo); trim.g = g;
    publish();
    if (auto* pb = selPad().buf.get())
        statusText = "TRIM " + juce::String (trim.a * pb->duration(), 3) + " s -> " + juce::String (trim.b * pb->duration(), 3)
                   + " s - KEEPS " + juce::String ((trim.b - trim.a) * pb->duration(), 3) + " s";
    sendChangeMessage();
}

void Controller::trimCancel()
{
    trim.on = false;
    changed(); status ("TRIM CANCELLED - THE ORIGINAL IS UNTOUCHED");
}

void Controller::trimApply()
{
    Pad& p = selPad();
    if (! trim.on || ! p.buf) return;
    if (trim.b - trim.a < 0.001f) { status ("TRIM RANGE IS TOO SHORT"); return; }
    if (! trim.changed()) { trimCancel(); return; }
    pushUndo();
    const int n = p.buf->length();
    const int s0 = (int) std::floor (trim.a * n), s1 = juce::jmin (n, (int) std::ceil (trim.b * n));
    const float a = (float) s0 / n, b = (float) s1 / n;
    std::vector<float> nm;
    for (float m : p.markers) if (m > a + 0.001f && m < b - 0.001f) nm.push_back ((m - a) / (b - a));
    auto sl = dsp::slice (*p.buf, (double) s0 / p.buf->sampleRate, (double) s1 / p.buf->sampleRate);
    p.buf = dsp::trimShape (*sl, trim.fi, trim.fo, trim.g);
    p.markers = nm; p.sel = -1;
    trim.on = false;
    changed(); status ("TRIM COMMITTED - " + juce::String (p.buf->duration(), 3) + " s - " CX_MOD_UP "+Z UNDOES");
}

// ------------------------------------------------------------------ pad fx
void Controller::editFx (const std::function<void (FxParams&)>& fn, const juce::String& key)
{
    pushUndo (key + juce::String (P.s.sel));
    fn (selPad().fx);
    changed();
}

void Controller::setMuteGroup (int g)
{
    pushUndo(); selPad().mg = g;
    changed(); status (g ? padLabel (P.s.sel) + " IN MUTE GROUP " + juce::String (g) + " - PADS IN A GROUP CUT EACH OTHER OFF" : padLabel (P.s.sel) + " MUTE GROUP OFF");
}

void Controller::setLinkGroup (int g)
{
    pushUndo(); selPad().lg = g;
    changed(); status (g ? padLabel (P.s.sel) + " IN LINK GROUP " + juce::String (g) + " - PADS IN THE SAME GROUP SOUND TOGETHER" : padLabel (P.s.sel) + " LINK GROUP OFF");
}

// ------------------------------------------------------------------ layers
void Controller::editSlot (int layer, const std::function<void (SlotSettings&)>& fn, const juce::String& key)
{
    Pad& p = selPad();
    if (layer >= (int) p.layers.size()) return;
    pushUndo (key + juce::String (P.s.sel) + "_" + juce::String (layer));
    fn (layer < 0 ? p.m : p.layers[(size_t) layer].s);
    changed();
}

void Controller::removeLayer (int i)
{
    Pad& p = selPad();
    if (i < 0 || i >= (int) p.layers.size()) return;
    pushUndo();
    p.layers.erase (p.layers.begin() + i);
    if (ui.layerSel >= (int) p.layers.size()) ui.layerSel = -1;
    changed(); status ("LAYER REMOVED");
}

void Controller::swapLayerWithMain (int i)
{
    Pad& p = selPad();
    if (i < 0 || i >= (int) p.layers.size() || ! p.buf) return;
    pushUndo(); trim.on = false;
    auto& L = p.layers[(size_t) i];
    std::swap (p.buf, L.buf); std::swap (p.name, L.name); std::swap (p.markers, L.markers); std::swap (p.m, L.s);
    p.sel = -1; p.hasKmap = false; p.kmap.clear();
    changed(); status ("LAYER " + juce::String (i + 1) + " IS NOW THE MAIN SAMPLE - CHOP, TRIM, STRETCH WORK ON IT. SWAP AGAIN TO PUT IT BACK");
}

// ------------------------------------------------------------------ stems
bool Controller::stemsReady() const
{
    const Pad& p = P.pads[(size_t) P.s.sel];
    const SampleData* b = ui.stemSel < 0 ? p.buf.get() : (ui.stemSel < (int) p.layers.size() ? p.layers[(size_t) ui.stemSel].buf.get() : nullptr);
    return b != nullptr && stems.has (b);
}

void Controller::runSeparate()
{
    Pad& p = selPad();
    SamplePtr src = ui.stemSel < 0 ? p.buf : (ui.stemSel < (int) p.layers.size() ? p.layers[(size_t) ui.stemSel].buf : nullptr);
    if (! src) { status ("LOAD A SAMPLE FIRST"); return; }
    if (stems.has (src.get())) { status ("THIS SAMPLE IS ALREADY SEPARATED"); return; }
    auto result = std::make_shared<std::array<SamplePtr, 4>>();
    runJob ("Separating stems (vocals / bass / instruments / drums)...",
            [src, result] (Job& j) { *result = dsp::separateStems (*src, [&j] (float f) { j.progress (f); }, [&j] { return j.cancelled(); }); },
            [this, src, result] (bool cancelled)
            {
                if (cancelled || ! (*result)[0]) { status ("STEM SEPARATION CANCELLED"); return; }
                stems.put (src, *result);
                changed();
                status ("STEMS READY - MIX THEM, APPLY TO THE SAMPLE OR SEND THEM TO PADS");
            });
}

void Controller::setStem (int k, float v)
{
    editSlot (ui.stemSel, [k, v] (SlotSettings& s)
    {
        if (k < 0) s.st = StemMix();
        else s.st.lv[(size_t) k] = juce::jlimit (0.0f, 1.0f, v);
    }, "stm" + juce::String (k));
}

void Controller::soloStem (int k)
{
    editSlot (ui.stemSel, [k] (SlotSettings& s) { for (int i = 0; i < 4; ++i) s.st.lv[(size_t) i] = i == k ? 1.0f : 0.0f; }, "stmsolo");
    status (juce::String ("SOLO ") + dsp::stemName (k));
}

void Controller::applyStems()
{
    Pad& p = selPad();
    const int i = ui.stemSel;
    SamplePtr src = i < 0 ? p.buf : p.layers[(size_t) i].buf;
    auto* st = stems.get (src.get());
    if (! st) return;
    const StemMix mix = i < 0 ? p.m.st : p.layers[(size_t) i].s.st;
    if (mix.isUnity()) { status ("MIX IS UNCHANGED"); return; }
    pushUndo();
    auto nb = dsp::mixDown (*st, mix);
    if (i < 0) { p.buf = nb; p.m.st = StemMix(); } else { p.layers[(size_t) i].buf = nb; p.layers[(size_t) i].s.st = StemMix(); }
    changed(); status ("STEM MIX APPLIED TO THE SAMPLE - " CX_MOD_UP "+Z UNDOES");
}

void Controller::stemsToPads()
{
    Pad& p = selPad();
    const int i = ui.stemSel;
    SamplePtr src = i < 0 ? p.buf : p.layers[(size_t) i].buf;
    const juce::String nm = i < 0 ? p.name : p.layers[(size_t) i].name;
    auto* st = stems.get (src.get());
    if (! st) return;
    std::vector<int> tg;
    for (int k = 1; k < 64 && tg.size() < 4; ++k) { const int g = (P.s.sel + k) % 64; if (! P.pads[(size_t) g].buf) tg.push_back (g); }
    for (int k = 1; k < 64 && tg.size() < 4; ++k) { const int g = (P.s.sel + k) % 64; if (std::find (tg.begin(), tg.end(), g) == tg.end()) tg.push_back (g); }
    pushUndo();
    juce::String where;
    for (int j = 0; j < 4; ++j)
    {
        Pad np; np.buf = (*st)[(size_t) j];
        np.name = (nm.isEmpty() ? juce::String ("sample") : nm).substring (0, 12) + " - " + juce::String (dsp::stemName (j)).toLowerCase();
        np.fx = p.fx; np.mg = p.mg;
        P.pads[(size_t) tg[(size_t) j]] = np;
        where << (j ? ", " : "") << padLabel (tg[(size_t) j]);
    }
    changed(); status ("STEMS -> " + where + " - " CX_MOD_UP "+Z UNDOES");
}

// ------------------------------------------------------------------ sequencer
PatternData* Controller::curData (int lane)
{
    if (lane < 0 || lane >= (int) P.tracks.size()) return nullptr;
    return &P.tracks[(size_t) lane].get (P.s.pat);
}

void Controller::setPattern (int p)
{
    P.s.pat = juce::jlimit (0, NPAT - 1, p);
    ui.page = 0; ui.hasSel = false;
    changed();
    status ("PATTERN " + juce::String (P.s.pat + 1) + " - " + juce::String (P.plen[(size_t) P.s.pat] / 16) + " BAR" + (P.plen[(size_t) P.s.pat] > 16 ? "S" : "")
            + (P.pbpm[(size_t) P.s.pat] ? " - " + juce::String (P.pbpm[(size_t) P.s.pat]) + " BPM" : juce::String()));
}

void Controller::setPage (int p)
{
    ui.page = juce::jlimit (0, P.plen[(size_t) P.s.pat] / 16 - 1, p);
    sendChangeMessage();
}

void Controller::setPatternLength (int len)
{
    pushUndo();
    P.plen[(size_t) P.s.pat] = len;
    ui.page = juce::jmin (ui.page, len / 16 - 1);
    changed(); status ("PATTERN " + juce::String (P.s.pat + 1) + " IS " + juce::String (len / 16) + " BAR" + (len > 16 ? "S" : ""));
}

void Controller::setPatternBpm (int bpm)
{
    pushUndo ("pbpm");
    P.pbpm[(size_t) P.s.pat] = bpm <= 0 ? 0 : juce::jlimit (40, 240, bpm);
    changed();
    status (P.pbpm[(size_t) P.s.pat] ? "PATTERN " + juce::String (P.s.pat + 1) + " PLAYS AT " + juce::String (P.pbpm[(size_t) P.s.pat]) + " BPM"
                                     : "PATTERN " + juce::String (P.s.pat + 1) + " FOLLOWS THE PROJECT TEMPO");
}

bool Controller::stepOn (int lane, int step) const
{
    if (lane < 0 || lane >= (int) P.tracks.size()) return false;
    auto* d = P.tracks[(size_t) lane].find (P.s.pat);
    return d != nullptr && d->steps[(size_t) step] != 0;
}

bool Controller::stepHasEv (int lane, int step) const
{
    if (lane < 0 || lane >= (int) P.tracks.size()) return false;
    auto* d = P.tracks[(size_t) lane].find (P.s.pat);
    if (d == nullptr) return false;
    for (auto& e : d->ev) if (e.t / TPS == step) return true;
    return false;
}

void Controller::setStep (int lane, int step, bool on)
{
    if (step >= P.plen[(size_t) P.s.pat]) return;
    auto* d = curData (lane);
    if (! d || (d->steps[(size_t) step] != 0) == on) return;
    pushUndo ("paint");
    d->steps[(size_t) step] = on ? 1 : 0;
    if (on) { d->semi[(size_t) step] = 0; d->chopAt[(size_t) step] = -2; }
    changed();
}

void Controller::selectLane (int l, bool audition)
{
    ui.lane = juce::jlimit (0, (int) P.tracks.size() - 1, l);
    if (audition)
    {
        const auto& t = P.tracks[(size_t) ui.lane];
        Command c; c.type = Command::PadHit; c.pad = t.padIn (P.s.pat); c.chop = t.chopIn (P.s.pat); c.vel = 100; c.record = false;
        engine.push (c);
    }
    sendChangeMessage();
}

// The pad box edits what the track plays in the CURRENT pattern. A track with notes only in this
// pattern (or none) simply changes its own pad; otherwise only this pattern gets the new pad.
void Controller::setTrackPad (int l, int pad)
{
    pushUndo();
    auto& t = P.tracks[(size_t) l];
    const int pat = P.s.pat;
    bool otherData = false;
    for (int p = 0; p < NPAT; ++p) if (p != pat && t.hasDataIn (p) && t.padIn (p) == t.pad) otherData = true;
    if (! otherData) { t.pad = pad; t.chop = -1; if (auto* d = t.pd[(size_t) pat] ? &*t.pd[(size_t) pat] : nullptr) { d->pad = -1; d->chop = -2; } }
    else { auto& d = t.get (pat); d.pad = pad == t.pad ? -1 : pad; d.chop = -1; }
    changed();
}
void Controller::setTrackChop (int l, int chop)
{
    pushUndo();
    auto& t = P.tracks[(size_t) l];
    if (auto* d = t.pd[(size_t) P.s.pat] ? &*t.pd[(size_t) P.s.pat] : nullptr; d != nullptr && d->pad >= 0) d->chop = chop;
    else t.chop = chop;
    changed();
}
void Controller::toggleTrackSolo (int l)
{
    auto& t = P.tracks[(size_t) juce::jlimit (0, (int) P.tracks.size() - 1, l)];
    t.solo = ! t.solo;
    changed(); status ("TRACK " + juce::String (l + 1) + (t.solo ? " SOLO" : " SOLO OFF"));
}
void Controller::toggleTrackLock (int l)
{
    auto& t = P.tracks[(size_t) juce::jlimit (0, (int) P.tracks.size() - 1, l)];
    t.lock = ! t.lock;
    changed(); status ("TRACK " + juce::String (l + 1) + (t.lock ? " LOCKED - LIVE RECORDING WON'T WRITE INTO IT" : " UNLOCKED"));
}
int Controller::trackForPad (int gi) const
{
    const int pat = P.s.pat;
    for (int i = 0; i < (int) P.tracks.size(); ++i) if (P.tracks[(size_t) i].hasDataIn (pat) && P.tracks[(size_t) i].padIn (pat) == gi) return i;
    for (int i = 0; i < (int) P.tracks.size(); ++i) if (P.tracks[(size_t) i].padIn (pat) == gi) return i;
    return -1;
}
void Controller::armPadTrack (int gi)
{
    int l = trackForPad (gi);
    if (l < 0)
    {
        // no track plays this pad yet: give it the first track that is empty in this pattern
        for (int i = 0; i < (int) P.tracks.size(); ++i) if (! P.tracks[(size_t) i].hasDataIn (P.s.pat) && ! P.tracks[(size_t) i].lock) { l = i; break; }
        if (l < 0) { status ("NO EMPTY TRACK IN THIS PATTERN - ADD A TRACK FIRST"); return; }
        pushUndo();
        auto& t = P.tracks[(size_t) l];
        if (! t.hasAnyData()) { t.pad = gi; t.chop = -1; } else { auto& d = t.get (P.s.pat); d.pad = gi; d.chop = -1; }
    }
    ui.lane = l;
    toggleTrackArm (l);
}
void Controller::toggleTrackMute (int l) { pushUndo(); P.tracks[(size_t) l].mute = ! P.tracks[(size_t) l].mute; changed(); }
void Controller::setTrackVol (int l, float v) { pushUndo ("tvol" + juce::String (l)); P.tracks[(size_t) l].vol = juce::jlimit (0.0f, 1.0f, v); changed(); }

void Controller::toggleTrackArm (int l)
{
    auto& t = P.tracks[(size_t) l];
    t.arm = ! t.arm;
    changed();
    juce::String a;
    for (int i = 0; i < (int) P.tracks.size(); ++i) if (P.tracks[(size_t) i].arm) a << (a.isEmpty() ? "" : ", ") << (i + 1);
    status (a.isNotEmpty() ? "ARMED FOR RECORDING: TRACK " + a : "NO TRACKS ARMED");
}

void Controller::addTrack()
{
    if ((int) P.tracks.size() >= MAXT) { status ("64 TRACKS IS THE MAXIMUM"); return; }
    pushUndo();
    Track t; t.pad = juce::jmin (63, (int) P.tracks.size());
    P.tracks.push_back (t);
    ui.lane = (int) P.tracks.size() - 1;
    changed(); status ("TRACK " + juce::String (ui.lane + 1) + " ADDED");
}

void Controller::deleteTrack()
{
    if (P.tracks.size() <= 1) return;
    pushUndo();
    P.tracks.erase (P.tracks.begin() + ui.lane);
    ui.lane = juce::jmin (ui.lane, (int) P.tracks.size() - 1);
    changed(); status ("TRACK DELETED");
}

void Controller::duplicateTrack()
{
    if ((int) P.tracks.size() >= MAXT) return;
    pushUndo();
    Track t = P.tracks[(size_t) ui.lane]; t.arm = false;
    P.tracks.insert (P.tracks.begin() + ui.lane + 1, t);
    ++ui.lane;
    changed(); status ("TRACK DUPLICATED");
}

void Controller::doubleTracks()
{
    const int n = (int) P.tracks.size();
    if (n * 2 > MAXT) { status ("64 TRACKS IS THE MAXIMUM"); return; }
    pushUndo();
    for (int i = 0; i < n; ++i) { Track t; t.pad = juce::jmin (63, n + i); P.tracks.push_back (t); }
    changed(); status (juce::String (P.tracks.size()) + " TRACKS");
}

void Controller::clearTrack()
{
    pushUndo();
    P.tracks[(size_t) ui.lane].pd[(size_t) P.s.pat].reset();
    changed();
}

void Controller::copyPattern()
{
    PatClip c; c.len = P.plen[(size_t) P.s.pat]; c.bpm = P.pbpm[(size_t) P.s.pat];
    for (auto& t : P.tracks) c.pd.push_back (t.pd[(size_t) P.s.pat]);
    patClip = c;
    status ("COPIED PATTERN " + juce::String (P.s.pat + 1) + " - SELECT ANOTHER PATTERN AND PASTE");
}

void Controller::pastePattern()
{
    if (! patClip) { status ("COPY A PATTERN FIRST"); return; }
    pushUndo();
    P.plen[(size_t) P.s.pat] = patClip->len; P.pbpm[(size_t) P.s.pat] = patClip->bpm;
    for (size_t i = 0; i < P.tracks.size(); ++i) P.tracks[i].pd[(size_t) P.s.pat] = i < patClip->pd.size() ? patClip->pd[i] : std::nullopt;
    changed(); status ("PASTED INTO PATTERN " + juce::String (P.s.pat + 1));
}

void Controller::duplicatePattern()
{
    int target = -1;
    for (int k = 1; k < NPAT; ++k)
    {
        const int p = (P.s.pat + k) % NPAT;
        bool empty = true;
        for (auto& t : P.tracks) if (auto* d = t.find (p)) if (d->hasData()) { empty = false; break; }
        if (empty) { target = p; break; }
    }
    if (target < 0) { status ("NO EMPTY PATTERN LEFT"); return; }
    pushUndo();
    P.plen[(size_t) target] = P.plen[(size_t) P.s.pat]; P.pbpm[(size_t) target] = P.pbpm[(size_t) P.s.pat];
    for (auto& t : P.tracks) t.pd[(size_t) target] = t.pd[(size_t) P.s.pat];
    const int from = P.s.pat;
    P.s.pat = target; ui.page = 0;
    changed(); status ("PATTERN " + juce::String (from + 1) + " DUPLICATED TO " + juce::String (target + 1));
}

void Controller::clearPattern()
{
    pushUndo();
    for (auto& t : P.tracks) t.pd[(size_t) P.s.pat].reset();
    ui.hasSel = false;
    changed(); status ("PATTERN " + juce::String (P.s.pat + 1) + " CLEARED");
}

void Controller::copySteps()
{
    if (! ui.hasSel) return;
    stepClip.clear();
    for (int l = ui.sel.l0; l <= ui.sel.l1; ++l)
    {
        std::vector<StepCell> row;
        auto* d = P.tracks[(size_t) l].find (P.s.pat);
        for (int s = ui.sel.s0; s <= ui.sel.s1; ++s) row.push_back (d ? StepCell { d->steps[(size_t) s], d->semi[(size_t) s], d->chopAt[(size_t) s] } : StepCell {});
        stepClip.push_back (row);
    }
    status ("COPIED " + juce::String ((int) stepClip.size()) + " x " + juce::String ((int) stepClip[0].size()) + " STEPS - CLICK A STEP, THEN PASTE");
}

void Controller::pasteSteps()
{
    if (stepClip.empty()) { status ("COPY SOME STEPS FIRST"); return; }
    pushUndo();
    const int l0 = ui.hasSel ? ui.sel.l0 : ui.lane, s0 = ui.hasSel ? ui.sel.s0 : ui.page * 16;
    const int len = P.plen[(size_t) P.s.pat];
    for (int r = 0; r < (int) stepClip.size(); ++r)
    {
        const int l = l0 + r;
        if (l >= (int) P.tracks.size()) break;
        auto& d = P.tracks[(size_t) l].get (P.s.pat);
        for (int c = 0; c < (int) stepClip[(size_t) r].size(); ++c)
        {
            const int s = s0 + c;
            if (s >= len) break;
            d.steps[(size_t) s] = stepClip[(size_t) r][(size_t) c].on;
            d.semi[(size_t) s] = stepClip[(size_t) r][(size_t) c].semi;
            d.chopAt[(size_t) s] = stepClip[(size_t) r][(size_t) c].chop;
        }
    }
    changed(); status ("STEPS PASTED");
}

void Controller::deleteSteps()
{
    if (! ui.hasSel) return;
    pushUndo();
    for (int l = ui.sel.l0; l <= ui.sel.l1; ++l)
    {
        auto& d = P.tracks[(size_t) l].get (P.s.pat);
        for (int s = ui.sel.s0; s <= ui.sel.s1; ++s) d.steps[(size_t) s] = 0;
        d.ev.erase (std::remove_if (d.ev.begin(), d.ev.end(), [this] (const HitEvent& e) { const int s = e.t / TPS; return s >= ui.sel.s0 && s <= ui.sel.s1; }), d.ev.end());
    }
    changed(); status ("SELECTION CLEARED");
}

void Controller::transposeSteps (int n)
{
    if (! ui.hasSel) { status ("SELECT SOME STEPS FIRST (SELECT TOOL)"); return; }
    pushUndo ("transpose");
    for (int l = ui.sel.l0; l <= ui.sel.l1; ++l)
    {
        auto& d = P.tracks[(size_t) l].get (P.s.pat);
        for (int s = ui.sel.s0; s <= ui.sel.s1; ++s) if (d.steps[(size_t) s]) d.semi[(size_t) s] = juce::jlimit (-24.0f, 24.0f, d.semi[(size_t) s] + (float) n);
        for (auto& e : d.ev) { const int s = e.t / TPS; if (s >= ui.sel.s0 && s <= ui.sel.s1) e.p = juce::jlimit (-24.0f, 24.0f, e.p + (float) n); }
    }
    changed(); status ("TRANSPOSED " + juce::String (n > 0 ? "+" : "") + juce::String (n));
}

void Controller::chainAdd()
{
    pushUndo();
    P.chain.push_back ({ P.s.pat, 1 });
    ui.chainSel = (int) P.chain.size() - 1;
    changed(); status ("PATTERN " + juce::String (P.s.pat + 1) + " ADDED TO THE SONG CHAIN");
}

void Controller::chainRemove()
{
    if (ui.chainSel < 0 || ui.chainSel >= (int) P.chain.size()) return;
    pushUndo();
    P.chain.erase (P.chain.begin() + ui.chainSel);
    ui.chainSel = juce::jmin (ui.chainSel, (int) P.chain.size() - 1);
    if (P.chain.empty()) P.s.song = false;
    changed();
}

void Controller::chainMove (int dir)
{
    const int i = ui.chainSel, j = i + dir;
    if (i < 0 || j < 0 || j >= (int) P.chain.size()) return;
    pushUndo();
    std::swap (P.chain[(size_t) i], P.chain[(size_t) j]);
    ui.chainSel = j;
    changed();
}

void Controller::chainRep (int d)
{
    if (ui.chainSel < 0 || ui.chainSel >= (int) P.chain.size()) return;
    pushUndo ("rep");
    auto& c = P.chain[(size_t) ui.chainSel];
    c.rep = juce::jlimit (1, 16, c.rep + d);
    changed();
}

void Controller::chainSetPat (int i, int pat)
{
    if (i < 0 || i >= (int) P.chain.size()) return;
    pushUndo();
    P.chain[(size_t) i].pat = pat;
    changed();
}

void Controller::toggleSongMode()
{
    if (P.chain.empty() && ! P.s.song) { status ("ADD PATTERNS TO THE CHAIN FIRST"); return; }
    P.s.song = ! P.s.song;
    changed(); status (P.s.song ? "SONG MODE - PLAY RUNS THE CHAIN" : "SONG MODE OFF - PLAY LOOPS THE CURRENT PATTERN");
}

// ------------------------------------------------------------------ transport
void Controller::play()
{
    publish();
    engine.recordArmed.store (ui.rec);
    lastPlayCmd = true; lastPlayCmdMs = juce::Time::getMillisecondCounterHiRes();
    Command c; c.type = Command::Play; c.f = (ui.rec && P.s.count) ? 4.0f : 0.0f;
    engine.push (c);
    status (c.f > 0 ? "COUNT-IN..." : ui.rec ? "RECORDING" : (P.s.song && ! P.chain.empty() ? "PLAYING SONG" : "PLAY"));
}

void Controller::stop()
{
    ui.rec = false;
    engine.recordArmed.store (false);
    lastPlayCmd = false; lastPlayCmdMs = juce::Time::getMillisecondCounterHiRes();
    Command c; c.type = Command::Stop;
    engine.push (c);
    status ("STOP");
}

bool Controller::playingNow() const
{
    // the audio thread takes a moment to start / stop: trust the command we just sent
    if (juce::Time::getMillisecondCounterHiRes() - lastPlayCmdMs < 400.0) return lastPlayCmd;
    return engine.playing.load();
}

void Controller::transportPress (const juce::String& which)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const juce::String kind = which == "rec" ? "rec" : "play";   // play, stop and playstop are one family
    if (kind == lastTransport && now - lastTransportMs < 150.0) return;   // the same press sent twice
    lastTransport = kind; lastTransportMs = now;
    if (which == "rec") { toggleRec(); if (ui.rec) recOnMs = now; return; }
    if (which == "recexit") { if (ui.rec && now - recOnMs > 400.0) toggleRec(); return; }
    if (playingNow()) stop(); else play();
}

void Controller::transportEdge (const juce::String& which, const juce::String& control, bool down)
{
    auto& b = btnSense[control];
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (down)
    {
        if (b.down && now - b.downMs < 400.0) return;   // a repeat of the same press
        b.down = true; b.downMs = now;
        transportPress (which);
        return;
    }
    if (! b.down) return;
    b.down = false;
    if (b.momentary) return;
    // released quickly = an ordinary push button: from now on only its presses count
    if (now - b.downMs < 350.0) { b.momentary = true; return; }
    // released much later, at the next push = a latching button: this "off" is the 2nd press
    transportPress (which);
}

int Controller::relativeDelta (int v)
{
    if (v >= 1 && v <= 15) return v;            // two's complement: 1.. = right, 127.. = left
    if (v >= 113 && v <= 127) return v - 128;
    if (v >= 49 && v <= 79 && v != 64) return v - 64;   // "offset 64" style: 65 = right, 63 = left
    return 0;
}

void Controller::turnPitchKnob (const juce::String& key, int k, int value, bool endless)
{
    k = juce::jlimit (0, 7, k);
    const int gi = ui.bank * 8 + k;
    auto& s = knobSense[key];
    const int d = relativeDelta (value);
    bool justFound = false;
    if (! endless)
    {
        // an endless knob repeats the same value (1, 1, 1... or 127, 127...) - an absolute knob never does
        if (! s.rel && value == s.last && d != 0)
        {
            s.rel = true; justFound = true; s.acc = (float) d * 0.5f;   // the first message was a step too
            setPitch (gi, s.before, true);   // undo the jump the first message made
            statusText = "KNOB " + juce::String (k + 1) + " IS ENDLESS (RELATIVE) - SWITCHED TO STEP MODE";
        }
        else if (s.rel && d == 0 && value != 64) s.rel = false;   // it sent a real position after all
    }
    s.last = value;
    if (endless || s.rel)
    {
        if (endless && d == 0) return;
        s.acc += (float) d * 0.5f;   // 2 clicks = 1 semitone
        const float whole = std::trunc (s.acc);
        if (whole != 0.0f)
        {
            s.acc -= whole;
            setPitch (gi, P.pads[(size_t) gi].pitch + whole, true);
        }
    }
    else
    {
        s.before = P.pads[(size_t) gi].pitch;
        setPitch (gi, std::round ((value - 63.5f) / 63.5f * PR), true);
    }
    if (! justFound) statusText = "KNOB " + juce::String (k + 1) + " -> " + padLabel (gi) + " " + fmtSt (P.pads[(size_t) gi].pitch) + " st";
}

void Controller::toggleRec()
{
    ui.rec = ! ui.rec;
    engine.recordArmed.store (ui.rec);
    bool anyArm = false;
    for (auto& t : P.tracks) anyArm |= t.arm;
    if (ui.rec && ! anyArm) status ("REC ON - ARM A TRACK (R) TO RECORD INTO IT");
    else if (ui.rec && ! engine.playing.load()) play();
    else status (ui.rec ? "RECORDING" : "REC OFF");
    sendChangeMessage();
}

void Controller::setBpm (float v)
{
    P.s.bpm = (float) juce::jlimit (40, 240, (int) std::round (v));
    changed(); status ("TEMPO " + juce::String ((int) P.s.bpm) + " BPM");
}

void Controller::tapTempo()
{
    const double n = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && n - taps.back() > 2000) taps.clear();
    taps.push_back (n);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        double sum = 0;
        for (size_t i = 1; i < taps.size(); ++i) sum += taps[i] - taps[i - 1];
        setBpm ((float) (60000.0 / (sum / (double) (taps.size() - 1))));
    }
    else status ("TAP TEMPO");
}

void Controller::setMasterVol (float v)
{
    P.s.mvol = juce::jlimit (0.0f, 1.0f, v);
    publish();
    sendChangeMessage();
}

void Controller::setLA (const std::function<void (LASettings&)>& fn)
{
    pushUndo ("la");
    fn (P.la);
    changed();
}

// ------------------------------------------------------------------ engine events
void Controller::applyRecHit (const EngineEvent& e)
{
    const int gi = e.a, chop = e.b, pat = e.c;
    std::vector<int> ok;   // armed and not locked
    bool anyArmed = false;
    for (int i = 0; i < (int) P.tracks.size(); ++i)
    {
        const auto& t = P.tracks[(size_t) i];
        if (t.arm) anyArmed = true;
        if (t.arm && ! t.lock) ok.push_back (i);
    }
    if (! anyArmed) { statusText = "NO TRACK ARMED - CLICK R ON A TRACK TO RECORD"; return; }
    if (ok.empty()) { statusText = "THE ARMED TRACKS ARE LOCKED - UNLOCK ONE (L) TO RECORD"; return; }
    // Recording goes into ARMED, UNLOCKED tracks, judged in THIS pattern only:
    //   1. EVERY armed track that plays this pad in this pattern records the hit (whatever chop / key it is,
    //      so a keyboard performance in chop mode stays on one track; two armed tracks on one pad both record)
    //   2. otherwise the selected armed track if it is still empty here, else the next armed empty track,
    //      which then plays this pad in this pattern
    // A track that already has notes for another pad in this pattern is never re-assigned.
    auto plays = [&] (int i) { return P.tracks[(size_t) i].padIn (pat) == gi; };
    auto empty = [&] (int i) { return ! P.tracks[(size_t) i].hasDataIn (pat); };
    auto isOk = [&] (int i) { return std::find (ok.begin(), ok.end(), i) != ok.end(); };
    std::vector<int> targets;
    if (ui.recAllArmed) targets = ok;   // layer mode: every armed track records (each with its own pad)
    else for (int i : ok) if (plays (i)) targets.push_back (i);
    if (targets.empty())
    {
        int l = -1;
        if (isOk (ui.lane) && empty (ui.lane)) l = ui.lane;
        if (l < 0) for (int i : ok) if (empty (i)) { l = i; break; }
        if (l < 0) { statusText = "EVERY ARMED TRACK ALREADY HOLDS ANOTHER PAD IN THIS PATTERN - ARM AN EMPTY TRACK FOR " + padLabel (gi); return; }
        targets.push_back (l);
    }
    pushUndo ("rec");
    for (int l : targets)
    {
        auto& T = P.tracks[(size_t) l];
        if (! plays (l) && ! (ui.recAllArmed && ! empty (l)))
        {
            if (! T.hasAnyData()) { T.pad = gi; T.chop = chop; for (auto& d : T.pd) if (d) { d->pad = -1; d->chop = -2; } }
            else { auto& d = T.get (pat); d.pad = gi; d.chop = chop; }   // only this pattern plays the new pad
        }
        const int trackChop = T.chopIn (pat);
        const int16_t c = (int16_t) (chop == trackChop || ! plays (l) ? -2 : chop);   // the chop / key that was played, if not the track's own
        auto& d = T.get (pat);
        const int len = P.plen[(size_t) pat];
        if (P.s.quant)
        {
            const int step = (((int) std::round (e.pos) % len) + len) % len;
            d.steps[(size_t) step] = 1; d.semi[(size_t) step] = e.f; d.chopAt[(size_t) step] = c;
        }
        else
        {
            const int TT = len * TPS;
            const int tk = (((int) std::round (e.pos * TPS) % TT) + TT) % TT;
            d.ev.erase (std::remove_if (d.ev.begin(), d.ev.end(), [tk, &e, c] (const HitEvent& x) { return x.t == tk && x.p == e.f && x.c == c; }), d.ev.end());
            d.ev.push_back ({ tk, e.f, e.g, c });
        }
    }
    juce::String where;
    for (int l : targets) where << (where.isEmpty() ? "" : ", ") << (l + 1);
    statusText = "REC -> TRACK" + juce::String (targets.size() > 1 ? "S " : " ") + where + " (" + padLabel (gi) + (chop >= 0 ? " chop " + juce::String (chop + 1) : juce::String()) + ")";
}

void Controller::doAction (const juce::String& id)
{
    if (id == "playstop" || id == "play" || id == "stop" || id == "rec") transportPress (id);
    else if (id == "loop") toggle (P.s.loop, "LOOP");
    else if (id == "metro") toggle (P.s.metro, "METRONOME");
    else if (id == "tap") tapTempo();
    else if (id == "undo") undo();
    else if (id == "redo") redo();
}

void Controller::transportAction (int which)
{
    switch (which)
    {
        case 1: transportPress ("play"); break;
        case 2: transportPress ("stop"); break;
        case 3: transportPress ("rec"); break;
        case 4: toggle (P.s.loop, "LOOP"); break;
        case 5: toggle (P.s.metro, "METRONOME"); break;
        case 6: tapTempo(); break;
        default: break;
    }
}

void Controller::handleEvent (const EngineEvent& e)
{
    switch (e.type)
    {
        case EngineEvent::RecHit: applyRecHit (e); break;
        case EngineEvent::TransportCC:
        {
            if (e.c == 1) { statusText = "MIDI CC" + juce::String (e.a) + " = " + juce::String (e.b) + " - NOT LINKED - USE MIDI LEARN TO LINK IT"; break; }
            if (ccPress (e.a, e.b))
            {
                int w = 0;
                switch (e.a) { case 94: case 115: w = 1; break; case 93: case 117: w = 2; break; case 95: case 116: w = 3; break;
                               case 86: case 113: w = 4; break; case 87: case 112: w = 5; break; case 88: case 114: w = 6; break; default: break; }
                transportAction (w);
            }
            break;
        }
        case EngineEvent::Transport:
            if (e.a == 100) { if (e.b >= 0 && e.b < NPADS) P.pads[(size_t) e.b].sel = e.c; }
            else if (e.a == 1) transportPress ("play");
            else if (e.a == 2) transportPress ("stop");
            else if (e.a == 3) transportPress ("rec");
            else if (e.a == 4) transportPress ("recexit");
            break;
        case EngineEvent::PitchKnob:    turnPitchKnob ("prof" + juce::String (e.a), e.a, e.b, false); break;
        case EngineEvent::PitchKnobRel: turnPitchKnob ("prof" + juce::String (e.a), e.a, e.b, true); break;
        case EngineEvent::ModWheel: if (onMod) onMod (e.f); break;
        case EngineEvent::Bend: if (onBend) onBend (e.f); break;
        case EngineEvent::KeyLight:
            if (onKeyLight) onKeyLight (e.a, e.b != 0);
            if (e.b != 0 && ui.padToKey >= 0) { const int gi = ui.padToKey; ui.padToKey = -1; setPadOnKey (e.a, gi); }   // a MIDI key picks the key too
            break;
        case EngineEvent::OctFollow:
            P.s.oct = e.a;
            statusText = "KEYBOARD OCTAVE " + juce::String (e.b >= 0 ? "+" : "") + juce::String (e.b) + " - APP OCTAVE FOLLOWS";
            break;
        case EngineEvent::Finished: ui.rec = false; engine.recordArmed.store (false); statusText = "STOP"; break;
        case EngineEvent::CaptureFull: statusText = "SAMPLING STOPPED AT 60 s"; break;
        case EngineEvent::MidiIn:
        {
            juce::String hex;
            for (int b : { e.a, e.b, e.c }) if (b >= 0) hex << juce::String::toHexString (b).paddedLeft ('0', 2).toUpperCase() << " ";
            const int st = e.a & 0xF0;
            juce::String what = st == 0x90 && e.c > 0 ? "Note " + juce::String (e.b) + " on" : (st == 0x80 || st == 0x90) ? "Note " + juce::String (e.b) + " off"
                              : st == 0xB0 ? "CC " + juce::String (e.b) + " = " + juce::String (e.c) : st == 0xE0 ? "Pitch bend"
                              : e.a == 0xFA ? "Start" : e.a == 0xFB ? "Continue" : e.a == 0xFC ? "Stop" : e.a == 0xF0 ? "SysEx" : juce::String();
            lastMidi = hex.trim() + (what.isNotEmpty() ? "  (" + what + ", ch " + juce::String ((e.a & 15) + 1) + ")" : juce::String());
            if (onMidiActivity) onMidiActivity();
            break;
        }
        case EngineEvent::Learn:
        {
            if (! learnTarget) break;
            const bool isCC = e.a == 1, isPC = e.a == 2;
            const auto t = *learnTarget;
            // controller-profile steps (MPK mini IV setup): pad notes and knob CCs go to the profile, not the link list
            if (t.kind == "padnote" || t.kind == "knobcc")
            {
                if (t.kind == "padnote" && (isCC || isPC)) { statusText = "THAT WAS NOT A PAD - HIT PAD " + juce::String (t.n + 1); break; }
                if (t.kind == "knobcc" && ! isCC) { statusText = "TURN KNOB " + juce::String (t.n + 1) + " (IT SHOULD SEND A CC)"; break; }
                learnTarget.reset();
                engine.learnArmed.store (false);
                if (t.kind == "padnote") { engine.padNotes[(size_t) t.n].store (e.b); engine.padChannel.store (e.ch + 1); }
                else engine.knobCC[(size_t) t.n].store (e.b);
                if (onLearned) onLearned (t, e.a, e.b, e.ch);
                break;
            }
            if ((t.kind == "knob" || t.kind == "slider") && ! isCC) { statusText = "A KNOB OR SLIDER NEEDS A CONTROL THAT SENDS A VALUE - TURN A KNOB OR MOVE A FADER"; break; }
            if (isPC && t.kind == "key") { statusText = "A KEY NEEDS A NOTE OR CC (PROGRAM CHANGES HAVE NO RELEASE)"; break; }
            // learned on this MIDI channel, so two keyboards (or a keyboard's keys and its buttons) can't collide
            const juce::String sig = (isCC ? "c" : isPC ? "p" : "n") + juce::String (e.b) + "." + juce::String (e.ch + 1);
            // one MIDI message drives one control, but a control may have several links
            // (e.g. PLAY on two different keyboards): only this message's old link is replaced
            midiMap.erase (sig);
            midiMap[sig] = t;
            learnTarget.reset();
            engine.learnArmed.store (false);
            pushMidiMap();
            statusText = t.label.toUpperCase() + " <- " + sigName (sig).toUpperCase() + " - LINKED. CLICK ANOTHER CONTROL OR TURN MIDI LEARN OFF";
            if (onMidiMapChanged) onMidiMapChanged();
            if (onLearned) onLearned (t, e.a, e.b, e.ch);
            break;
        }
        case EngineEvent::MapFire:
        {
            const juce::String base = (e.a == 1 ? "c" : e.a == 2 ? "p" : "n") + juce::String (e.b);
            auto it = midiMap.find (base + "." + juce::String (e.ch + 1));
            if (it == midiMap.end()) it = midiMap.find (base);
            if (it == midiMap.end()) break;
            const bool release = e.f > 0.5f;
            const juce::String control = base + "." + juce::String (e.ch + 1);
            if (it->second.kind == "action")
            {
                if (isTransportId (it->second.id)) { transportEdge (it->second.id, control, e.c > 0); break; }
                // other built-in actions (loop, undo...): fire once per press
                if (release) break;
                const bool press = e.a == 1 ? ccPress (1000 + e.b, e.c) : e.c > 0;
                if (press) doAction (it->second.id);
                break;
            }
            if (release && it->second.kind != "btn") break;   // only buttons care about a note being let go
            if (onMapFire) onMapFire (it->second, e.a == 1, release ? -1 : e.c);
            break;
        }
        case EngineEvent::PadSelect: selectPad (e.a); break;
        case EngineEvent::LowKey:
            statusText = "LOW KEY SET: NOTE " + juce::String (e.a) + " IS NOW " + juce::MidiMessage::getMidiNoteName (P.keyBase(), true, true, 3);
            P.s.oct = 0; engine.appOct.store (0);
            break;
        default: break;
    }
}

void Controller::timerCallback()
{
    EngineEvent e;
    int n = 0; bool recChanged = false;
    int padSel = -1; EngineEvent lastIn; bool haveIn = false;
    while (engine.pop (e) && n++ < 4096)
    {
        // a burst of pad hits / MIDI selects the last pad once (each selection rebuilds the screen)
        if (e.type == EngineEvent::PadSelect) { padSel = e.a; continue; }
        if (e.type == EngineEvent::MidiIn) { lastIn = e; haveIn = true; continue; }
        handleEvent (e);
        if (e.type == EngineEvent::RecHit || e.type == EngineEvent::PitchKnob || e.type == EngineEvent::OctFollow || e.type == EngineEvent::Transport) recChanged = true;
    }
    if (haveIn) handleEvent (lastIn);
    if (padSel >= 0) selectPad (padSel);
    if (recChanged) changed();
    if (engine.captureState() == Engine::CAP_STOPPED) finishCapture();
    engine.collectGarbage();
    for (size_t i = 0; i + 1 < releasePool.size();)
        if (releasePool[i].use_count() <= 1) releasePool.erase (releasePool.begin() + (long) i); else ++i;
}

// ------------------------------------------------------------------ songs
void Controller::saveSong (const juce::File& f)
{
    juce::String err;
    pullPluginStates();
    if (io::saveSong (P, f, err)) { unsaved = false; exportStatus = "Saved " + f.getFileName(); status ("SAVED " + f.getFileName()); }
    else { exportStatus = "Could not save: " + err; status ("COULD NOT SAVE"); }
}

void Controller::loadSong (const juce::File& f)
{
    Project np = P;
    juce::String msg;
    if (! io::loadSong (f, np, msg)) { exportStatus = "Could not load song: " + msg; status ("COULD NOT LOAD SONG"); return; }
    stop();
    pushUndo();
    // library samples carried by a (web) song go into the library folder (written on Save library / exit)
    int nLib = 0;
    for (auto& it : np.lib)
    {
        if (! it.buf) continue;
        bool have = false;
        for (auto& x : library.items) if (x.name == it.name && x.crate == it.crate) { have = true; break; }
        if (! have) { library.add (it.name, it.crate, it.buf); ++nLib; }
    }
    np.lib.clear(); np.crates.clear();
    if (nLib > 0) msg << " " << nLib << " library sample" << (nLib > 1 ? "s" : "") << " from the song added to the library.";
    P = std::move (np);
    trim.on = false; ui.bank = P.s.sel >> 3; ui.page = 0; ui.lane = 0; ui.hasSel = false; ui.chainSel = -1;
    changed();
    exportStatus = msg;
    status ("SONG LOADED");
}

void Controller::newSong()
{
    stop();
    pushUndo();
    P.reset();
    trim.on = false; ui = UIState();
    changed(); status ("NEW SONG - " CX_MOD_UP "+Z BRINGS THE OLD ONE BACK");
}

std::vector<int> Controller::buildPlan() const
{
    std::vector<int> plan;
    if (P.s.song && ! P.chain.empty())
    {
        for (auto& c : P.chain) for (int r = 0; r < c.rep; ++r) plan.push_back (c.pat);
        return plan;
    }
    for (int i = 0; i < juce::jmax (1, ui.exportBars); ++i) plan.push_back (P.s.pat);
    return plan;
}

std::vector<int> Controller::activeTracks (const std::vector<int>& plan) const
{
    std::vector<int> ids;
    bool soloAny = false;
    for (auto& t : P.tracks) soloAny |= t.solo;
    for (int i = 0; i < (int) P.tracks.size(); ++i)
    {
        const auto& t = P.tracks[(size_t) i];
        if (t.mute || (soloAny && ! t.solo)) continue;
        bool any = false;
        for (int pat : plan)
            if (auto* d = t.find (pat))
                if (d->hasData())
                {
                    const auto& p = P.pads[(size_t) t.padIn (pat)];
                    if (! p.muted && (p.buf || ! p.inst.empty())) { any = true; break; }
                }
        if (any) ids.push_back (i);
    }
    return ids;
}

void Controller::exportSongWav (const juce::File& f)
{
    if (needPluginCopies ([this, f] { exportSongWav (f); })) return;
    const auto plan = buildPlan(); const auto ids = activeTracks (plan);
    if (ids.empty()) { exportStatus = "Nothing to export yet. Load a sample, switch on some steps, then try again."; sendChangeMessage(); return; }
    stop();
    auto snap = builder.build (P, stems, TrimState());
    const int bits = ui.exportBits;
    auto ok = std::make_shared<bool> (false);
    auto plug = clonePlugins (44100, 1024);
    runJob ("Rendering song...", [snap, plan, ids, f, bits, ok, plug] (Job& j)
    {
        auto mix = Engine::renderPlan (snap, plan, ids, 44100, true, [&j] (float p) { j.progress (p); }, [&j] { return j.cancelled(); }, nullptr, plug.get());
        if (mix.getNumSamples() > 0) *ok = io::writeWav (f, mix, 44100, bits);
    }, [this, f, ok] (bool cancelled)
    {
        exportStatus = cancelled ? "Export cancelled" : *ok ? "Saved " + f.getFullPathName() : "Could not save " + f.getFileName();
        status (cancelled ? "EXPORT CANCELLED" : *ok ? "SAVED " + f.getFileName() : "EXPORT FAILED");
    });
}

void Controller::exportStems (const juce::File& folder)
{
    if (needPluginCopies ([this, folder] { exportStems (folder); })) return;
    const auto plan = buildPlan(); const auto ids = activeTracks (plan);
    if (ids.empty()) { exportStatus = "Nothing to export yet. Load a sample, switch on some steps, then try again."; sendChangeMessage(); return; }
    stop();
    auto snap = builder.build (P, stems, TrimState());
    const int bits = ui.exportBits;
    const auto nm = io::safeName (P.name);
    const Project copy = P;   // names and tempo map
    auto okCount = std::make_shared<int> (0);
    auto plug = clonePlugins (44100, 1024);
    runJob ("Rendering mix and stems...", [snap, plan, ids, folder, bits, nm, copy, okCount, plug] (Job& j)
    {
        folder.createDirectory();
        const auto stemDir = folder.getChildFile ("stems");
        stemDir.createDirectory();
        struct Part { juce::File f; juce::AudioBuffer<float> b; };
        std::vector<Part> parts;
        const float total = (float) ids.size() + 1;
        j.setStatusMessage ("Rendering mix...");
        parts.push_back ({ folder.getChildFile (nm + "_MIX.wav"),
                           Engine::renderPlan (snap, plan, ids, 44100, true, [&j, total] (float p) { j.progress (p / total); }, [&j] { return j.cancelled(); }, nullptr, plug.get()) });
        // stems: each track through its own channel (instrument, inserts, sends), without the master inserts
        PluginSet stemPlug;
        if (plug)
            for (auto& kv : plug->nodes)
                if (kv.first.kind != SlotId::Master) stemPlug.nodes[kv.first] = std::move (kv.second);
        // hand them back afterwards so they are deleted on the message thread with the job
        struct Back { PluginSet& from; PluginSet* to; ~Back() { if (to) for (auto& kv : from.nodes) if (kv.second) to->nodes[kv.first] = std::move (kv.second); } } back { stemPlug, plug.get() };
        for (size_t k = 0; k < ids.size(); ++k)
        {
            if (j.cancelled()) return;
            const auto& t = copy.tracks[(size_t) ids[k]];
            int tp = t.pad;
            for (int pat : plan) if (t.hasDataIn (pat)) { tp = t.padIn (pat); break; }
            const auto& p = copy.pads[(size_t) tp];
            j.setStatusMessage ("Rendering stem " + juce::String ((int) k + 1) + " of " + juce::String ((int) ids.size()) + " (" + (p.name.isNotEmpty() ? p.name : padLabel (tp)) + ")...");
            auto b = Engine::renderPlan (snap, plan, { ids[k] }, 44100, false,
                                         [&j, k, total] (float pp) { j.progress (((float) k + 1 + pp) / total); }, [&j] { return j.cancelled(); }, nullptr, &stemPlug);
            parts.push_back ({ stemDir.getChildFile ("T" + juce::String (ids[k] + 1).paddedLeft ('0', 2) + "_" + padLabel (tp) + "_" + io::safeName (p.name) + ".wav"), std::move (b) });
        }
        // every file the same length, all starting at bar 1 beat 1
        int N = 0; for (auto& x : parts) N = juce::jmax (N, x.b.getNumSamples());
        for (auto& x : parts)
        {
            juce::AudioBuffer<float> padded (2, N); padded.clear();
            for (int c = 0; c < 2; ++c) padded.copyFrom (c, 0, x.b, c, 0, x.b.getNumSamples());
            if (io::writeWav (x.f, padded, 44100, bits)) ++*okCount;
        }
        if (io::planTempos (copy, plan).size() > 1)
        {
            const auto mid = io::tempoMapMid (copy, plan);
            folder.getChildFile (nm + "_tempo_map.mid").replaceWithData (mid.getData(), mid.getSize());
            folder.getChildFile (nm + "_tempo_map.txt").replaceWithText (io::tempoMapTxt (copy, plan));
        }
    }, [this, folder, okCount, ids] (bool cancelled)
    {
        exportStatus = cancelled ? "Export cancelled" : "Saved " + juce::String (*okCount) + " files (" + juce::String ((int) ids.size()) + " stems + mix) to " + folder.getFullPathName();
        status (cancelled ? "EXPORT CANCELLED" : "STEMS SAVED");
    });
}

void Controller::exportTempoMap (const juce::File& f)
{
    const auto plan = buildPlan();
    const auto mid = io::tempoMapMid (P, plan);
    const bool ok = f.replaceWithData (mid.getData(), mid.getSize());
    const auto n = io::planTempos (P, plan).size();
    exportStatus = ok ? "Saved " + f.getFileName() + " - " + (n > 1 ? juce::String ((int) n) + " tempo changes" : "constant " + juce::String (P.bpmOf (plan[0])) + " BPM") : "Could not save " + f.getFileName();
    sendChangeMessage();
}

void Controller::exportChop (const juce::File& f)
{
    Pad& p = selPad();
    if (! p.buf) { exportStatus = "Select a pad with a sample first."; sendChangeMessage(); return; }
    SamplePtr b = p.buf;
    if (! p.markers.empty() && p.sel >= 0)
    {
        const auto sg = segments (b.get(), p.markers);
        if (p.sel < (int) sg.size()) b = dsp::slice (*b, sg[(size_t) p.sel].first, sg[(size_t) p.sel].second);
    }
    if (ui.bakePitch && std::abs (p.pitch) > 0.001f) b = dsp::resampleStretch (*b, 1.0 / std::pow (2.0, p.pitch / 12.0));
    juce::AudioBuffer<float> st (2, b->length());
    for (int c = 0; c < 2; ++c) st.copyFrom (c, 0, b->audio, juce::jmin (c, b->channels() - 1), 0, b->length());
    exportStatus = io::writeWav (f, st, b->sampleRate, ui.exportBits) ? "Saved " + f.getFileName() : "Could not save " + f.getFileName();
    sendChangeMessage();
}

void Controller::exportAllChops (const juce::File& folder)
{
    Pad& p = selPad();
    if (! p.buf || p.markers.empty()) { exportStatus = "Chop the sample first."; sendChangeMessage(); return; }
    folder.createDirectory();
    const auto sg = segments (p.buf.get(), p.markers);
    int ok = 0;
    for (int i = 0; i < (int) sg.size(); ++i)
    {
        SamplePtr b = dsp::slice (*p.buf, sg[(size_t) i].first, sg[(size_t) i].second);
        if (ui.bakePitch && std::abs (p.pitch) > 0.001f) b = dsp::resampleStretch (*b, 1.0 / std::pow (2.0, p.pitch / 12.0));
        juce::AudioBuffer<float> st (2, b->length());
        for (int c = 0; c < 2; ++c) st.copyFrom (c, 0, b->audio, juce::jmin (c, b->channels() - 1), 0, b->length());
        if (io::writeWav (folder.getChildFile (padLabel (P.s.sel) + "_" + io::safeName (p.name) + "_chop" + juce::String (i + 1).paddedLeft ('0', 2) + ".wav"), st, b->sampleRate, ui.exportBits)) ++ok;
    }
    exportStatus = "Saved " + juce::String (ok) + " chops to " + folder.getFullPathName();
    sendChangeMessage();
}

// ------------------------------------------------------------------ MIDI learn
bool Controller::ccPress (int cc, int value)
{
    // fire on press only; ignore repeats within 400 ms
    const auto t = juce::Time::getMillisecondCounter();
    const bool was = ccDown[cc]; const auto last = ccLast[cc];
    ccLast[cc] = t; ccDown[cc] = value >= 1;
    return value >= 1 && (! was || t - last > 400);
}

juce::String Controller::sigName (const juce::String& sig)
{
    if (sig.containsChar (','))
    {
        juce::StringArray parts;
        for (auto& x : juce::StringArray::fromTokens (sig, ",", "")) parts.add (sigName (x));
        return parts.joinIntoString (" + ");
    }
    const auto body = sig.substring (1);
    const auto ch = body.containsChar ('.') ? " ch" + body.fromFirstOccurrenceOf (".", false, false) : juce::String();
    return (sig.startsWith ("c") ? "CC " : sig.startsWith ("p") ? "Prog " : "Note ") + body.upToFirstOccurrenceOf (".", false, false) + ch;
}

juce::String Controller::midiSigFor (const MidiTarget& t) const
{
    juce::StringArray all;
    for (auto& kv : midiMap) if (kv.second == t) all.add (kv.first);
    return all.joinIntoString (",");
}

void Controller::setMidiLearn (bool on)
{
    midiLearn = on;
    learnTarget.reset();
    engine.learnArmed.store (false);
    status (on ? "MIDI LEARN - CLICK A PAD, KEY, KNOB, BUTTON OR SLIDER, THEN PRESS OR MOVE THE CONTROL ON YOUR KEYBOARD"
               : "MIDI LEARN OFF");
}

void Controller::armLearn (const MidiTarget& t)
{
    learnTarget = t;
    engine.learnArmed.store (true);
    status ("LEARNING " + t.label.toUpperCase() + " - " + ((t.kind == "knob" || t.kind == "slider") ? "TURN A KNOB OR MOVE A FADER" : "PRESS A KEY, PAD OR BUTTON") + " ON YOUR CONTROLLER");
}

void Controller::removeMidiLink (const juce::String& sig)
{
    midiMap.erase (sig);
    pushMidiMap();
    if (onMidiMapChanged) onMidiMapChanged();
    sendChangeMessage();
}

void Controller::clearMidiLinks()
{
    midiMap.clear();
    pushMidiMap();
    if (onMidiMapChanged) onMidiMapChanged();
    status ("ALL MIDI LINKS CLEARED");
}

void Controller::pushMidiMap()
{
    for (auto& m : engine.noteMap) m.store (Engine::MAP_NONE);
    for (auto& m : engine.ccMap) m.store (Engine::MAP_NONE);
    for (auto& m : engine.pcMap) m.store (Engine::MAP_NONE);
    for (auto* tbl : { &engine.noteMapCh, &engine.ccMapCh, &engine.pcMapCh }) for (auto& row : *tbl) for (auto& m : row) m.store (Engine::MAP_NONE);
    for (auto& kv : midiMap)
    {
        // "n60" = note 60 on any channel, "n60.10" = note 60 on channel 10
        const auto body = kv.first.substring (1);
        const int num = body.upToFirstOccurrenceOf (".", false, false).getIntValue();
        const int ch = body.containsChar ('.') ? body.fromFirstOccurrenceOf (".", false, false).getIntValue() : 0;
        if (num < 0 || num > 127 || ch < 0 || ch > 16) continue;
        const auto& t = kv.second;
        const int code = t.kind == "pad" ? Engine::MAP_PAD + juce::jlimit (0, 7, t.n)
                       : t.kind == "key" ? Engine::MAP_KEY + juce::jlimit (0, 127, t.n) : Engine::MAP_UI;
        const bool isPC = kv.first.startsWith ("p"), isCC = kv.first.startsWith ("c");
        if (isPC && t.kind == "key") continue;
        auto& any = isPC ? engine.pcMap : isCC ? engine.ccMap : engine.noteMap;
        auto& per = isPC ? engine.pcMapCh : isCC ? engine.ccMapCh : engine.noteMapCh;
        if (ch == 0) any[(size_t) num].store (code);
        else per[(size_t) (ch - 1)][(size_t) num].store (code);
    }
}

juce::var Controller::midiMapToVar() const
{
    auto* o = new juce::DynamicObject();
    for (auto& kv : midiMap)
    {
        auto* t = new juce::DynamicObject();
        t->setProperty ("k", kv.second.kind); t->setProperty ("n", kv.second.n);
        t->setProperty ("id", kv.second.id); t->setProperty ("label", kv.second.label);
        o->setProperty (kv.first, juce::var (t));
    }
    return juce::var (o);
}

void Controller::midiMapFromVar (const juce::var& v)
{
    midiMap.clear();
    if (auto* o = v.getDynamicObject())
        for (auto& p : o->getProperties())
        {
            MidiTarget t;
            t.kind = p.value.getProperty ("k", "").toString();
            t.n = (int) p.value.getProperty ("n", 0);
            t.id = p.value.getProperty ("id", "").toString();
            t.label = p.value.getProperty ("label", t.kind).toString();
            if (t.kind.isNotEmpty()) midiMap[p.name.toString()] = t;
        }
    pushMidiMap();
}

// ------------------------------------------------------------------ more editing
void Controller::resetKeys()
{
    Pad& p = selPad();
    if (! p.hasKmap) { status ("KEYS ARE ALREADY IN ORDER"); return; }
    pushUndo();
    p.hasKmap = false; p.kmap.clear();
    changed(); status ("KEYS BACK IN ORDER - KEY 1 PLAYS CHOP 1, KEY 2 CHOP 2 ...");
}

void Controller::layerFromPad (int gi)
{
    Pad& p = selPad();
    const Pad& src = P.pads[(size_t) gi];
    if (! p.buf) { status ("LOAD A SAMPLE ON THIS PAD FIRST - LAYERS PLAY ALONGSIDE IT"); return; }
    if (! src.buf || gi == P.s.sel) { status ("PICK ANOTHER PAD THAT HAS A SAMPLE"); return; }
    if ((int) p.layers.size() >= MAXLAY) { status ("A PAD HOLDS " + juce::String (MAXLAY) + " LAYERS"); return; }
    pushUndo();
    Layer L; L.buf = src.buf; L.name = src.name.isNotEmpty() ? src.name : padLabel (gi); L.s.pitch = src.pitch;
    p.layers.push_back (L);
    changed(); status (padLabel (gi) + " LAYERED ONTO " + padLabel (P.s.sel));
}

void Controller::selectTrackSteps()
{
    ui.selTool = true; ui.hasSel = true;
    ui.sel = { ui.lane, ui.lane, 0, P.plen[(size_t) P.s.pat] - 1 };
    status ("TRACK " + juce::String (ui.lane + 1) + " SELECTED - COPY IT, OR DRAG IT TO ANOTHER TRACK (" CX_COPYMOD_UP " = COPY)");
}

void Controller::moveSteps (int dl, int ds, bool copy)
{
    if (! ui.hasSel || (dl == 0 && ds == 0)) return;
    const int len = P.plen[(size_t) P.s.pat];
    const auto sel = ui.sel;
    struct Cell { uint8_t on; float semi; int16_t chop; std::vector<HitEvent> ev; };
    std::vector<std::vector<Cell>> rows;
    for (int l = sel.l0; l <= sel.l1; ++l)
    {
        std::vector<Cell> row;
        const auto* d = P.tracks[(size_t) l].find (P.s.pat);
        for (int s = sel.s0; s <= sel.s1; ++s)
        {
            Cell c { 0, 0.0f, -2, {} };
            if (d) { c.on = d->steps[(size_t) s]; c.semi = d->semi[(size_t) s]; c.chop = d->chopAt[(size_t) s]; for (auto& e : d->ev) if (e.t / TPS == s) c.ev.push_back (e); }
            row.push_back (c);
        }
        rows.push_back (row);
    }
    pushUndo();
    if (! copy)
        for (int l = sel.l0; l <= sel.l1; ++l)
        {
            auto& d = P.tracks[(size_t) l].get (P.s.pat);
            for (int s = sel.s0; s <= sel.s1; ++s) d.steps[(size_t) s] = 0;
            d.ev.erase (std::remove_if (d.ev.begin(), d.ev.end(), [&] (const HitEvent& e) { return e.t / TPS >= sel.s0 && e.t / TPS <= sel.s1; }), d.ev.end());
        }
    for (int r = 0; r < (int) rows.size(); ++r)
    {
        const int l = sel.l0 + r + dl;
        if (l < 0 || l >= (int) P.tracks.size()) continue;
        auto& d = P.tracks[(size_t) l].get (P.s.pat);
        for (int c = 0; c < (int) rows[(size_t) r].size(); ++c)
        {
            const int s = sel.s0 + c + ds;
            if (s < 0 || s >= len) continue;
            const auto& cell = rows[(size_t) r][(size_t) c];
            d.steps[(size_t) s] = cell.on; d.semi[(size_t) s] = cell.semi; d.chopAt[(size_t) s] = cell.chop;
            for (auto e : cell.ev) { e.t += ds * TPS; if (e.t >= 0 && e.t < len * TPS) d.ev.push_back (e); }
        }
    }
    ui.sel = { juce::jlimit (0, (int) P.tracks.size() - 1, sel.l0 + dl), juce::jlimit (0, (int) P.tracks.size() - 1, sel.l1 + dl),
               juce::jlimit (0, len - 1, sel.s0 + ds), juce::jlimit (0, len - 1, sel.s1 + ds) };
    changed(); status (copy ? "SELECTION COPIED" : "SELECTION MOVED");
}

void Controller::chainClear()
{
    if (P.chain.empty()) return;
    pushUndo();
    P.chain.clear(); ui.chainSel = -1; P.s.song = false;
    changed(); status ("CHAIN CLEARED");
}

// ------------------------------------------------------------------ library (a folder tree, see io/Library.h)
static juce::String crateLabel (const juce::String& c) { return c.isEmpty() ? juce::String ("ALL CRATES") : "\"" + c + "\""; }

void Controller::addToLibrary (const juce::String& crate)
{
    Pad& p = selPad();
    if (! p.buf) { status ("THIS PAD HAS NO SAMPLE"); return; }
    library.add (p.name.isNotEmpty() ? p.name : padLabel (P.s.sel), crate, p.buf);
    sendChangeMessage();
    status (padLabel (P.s.sel) + " ADDED TO THE LIBRARY - " + crateLabel (crate) + " (SAVE LIBRARY WRITES IT TO DISK)");
}

void Controller::libToPad (int index, int pad)
{
    if (index < 0 || index >= (int) library.items.size()) return;
    auto b = library.load (index);
    if (! b) { status ("COULD NOT READ " + library.items[(size_t) index].name.toUpperCase()); return; }
    pushUndo();
    Pad& p = P.pads[(size_t) pad];
    p.buf = prepBuf (b); p.name = library.items[(size_t) index].name; p.markers.clear(); p.sel = -1;
    p.keyMode = MODE_KEYS; p.hasKmap = false; p.kmap.clear();
    P.s.sel = pad; ui.bank = pad >> 3;
    changed(); status (p.name + " -> " + padLabel (pad));
}

void Controller::libRemove (int index)
{
    if (index < 0 || index >= (int) library.items.size()) return;
    const auto name = library.items[(size_t) index].name;
    if (library.remove (index)) status (name.toUpperCase() + " REMOVED (THE FILE IS IN THE " CX_TRASH_UP ")");
    else status ("COULD NOT REMOVE " + name.toUpperCase());
    sendChangeMessage();
}

void Controller::addChopsToLibrary (const juce::String& crate)
{
    Pad& p = selPad();
    if (! p.buf || p.markers.empty()) { status ("CHOP THE SELECTED PAD FIRST"); return; }
    const auto sg = segments (p.buf.get(), p.markers);
    const auto base = (p.name.isNotEmpty() ? p.name : juce::String ("chop")).substring (0, 18);
    const auto cr = crate.isNotEmpty() ? crate : base;
    for (int i = 0; i < (int) sg.size(); ++i)
        library.add (base + " " + juce::String (i + 1).paddedLeft ('0', 2), cr, dsp::slice (*p.buf, sg[(size_t) i].first, sg[(size_t) i].second));
    sendChangeMessage(); status (juce::String ((int) sg.size()) + " CHOPS ADDED TO THE LIBRARY - CRATE \"" + cr + "\" (SAVE LIBRARY WRITES THEM)");
}

void Controller::libToPads (const std::vector<int>& idx)
{
    if (idx.empty()) { status ("NOTHING TO LOAD"); return; }
    const int n = juce::jmin ((int) idx.size(), 64 - P.s.sel);
    pushUndo();
    int loaded = 0;
    for (int i = 0; i < n; ++i)
    {
        auto b = library.load (idx[(size_t) i]);
        if (! b) continue;
        Pad& p = P.pads[(size_t) (P.s.sel + i)];
        p.buf = prepBuf (b); p.name = library.items[(size_t) idx[(size_t) i]].name; p.markers.clear(); p.sel = -1;
        p.keyMode = MODE_KEYS; p.hasKmap = false; p.kmap.clear();
        ++loaded;
    }
    changed();
    status (juce::String (loaded) + " SAMPLE" + (loaded != 1 ? "S" : "") + " LOADED ONTO " + padLabel (P.s.sel) + "-" + padLabel (P.s.sel + n - 1)
            + ((int) idx.size() > n ? " - OUT OF PADS" : ""));
}

void Controller::libPreview (int index)
{
    auto b = library.load (index);
    if (! b) return;
    previewSample = b;
    publish();
    Command c; c.type = Command::Preview; c.pad = NPADS; c.chop = -1;
    engine.push (c);
    status ("PREVIEW: " + library.items[(size_t) index].name);
}

void Controller::libImport (const juce::Array<juce::File>& files, const juce::String& crate)
{
    const int n = library.importFiles (files, crate);
    sendChangeMessage();
    status (n > 0 ? juce::String (n) + " SAMPLE" + (n > 1 ? "S" : "") + " COPIED INTO THE LIBRARY - " + crateLabel (crate)
                  : juce::String ("NOTHING NEW TO IMPORT (ALREADY IN THE LIBRARY?)"));
}

void Controller::libAddCrate (const juce::String& path)
{
    if (library.addCrate (path)) status ("CRATE \"" + path + "\" MADE");
    else status ("COULD NOT MAKE THAT CRATE");
    sendChangeMessage();
}

void Controller::libRenameCrate (const juce::String& path, const juce::String& newName)
{
    if (library.renameCrate (path, newName)) status ("CRATE RENAMED TO \"" + newName + "\"");
    else status ("COULD NOT RENAME - A CRATE WITH THAT NAME EXISTS, OR A FILE IN IT IS IN USE");
    sendChangeMessage();
}

void Controller::libDeleteCrate (const juce::String& path)
{
    if (library.deleteCrate (path)) status ("CRATE DELETED - ITS SAMPLES MOVED UP ONE LEVEL");
    else status ("COULD NOT DELETE THAT CRATE (A FILE IN IT MAY BE IN USE)");
    sendChangeMessage();
}

void Controller::libSave()
{
    juce::String err;
    const int n = library.save (&err);
    library.scan();
    sendChangeMessage();
    status (err.isNotEmpty() ? "LIBRARY: " + err.toUpperCase() : n > 0 ? "LIBRARY SAVED - " + juce::String (n) + " NEW SAMPLE" + (n > 1 ? "S" : "") + " WRITTEN" : juce::String ("LIBRARY SAVED - NOTHING NEW"));
}

void Controller::libRefresh (bool quiet)
{
    const bool changedLib = library.scan();
    if (changedLib || ! quiet) sendChangeMessage();
    if (! quiet) status ("LIBRARY REFRESHED - " + juce::String ((int) library.items.size()) + " SAMPLES IN " + juce::String (library.crates.size()) + " CRATES");
}

void Controller::libSetRoot (const juce::File& dir)
{
    library.save();
    library.setRoot (dir);
    sendChangeMessage();
    status ("LIBRARY FOLDER: " + dir.getFullPathName().toUpperCase());
}

void Controller::libSaveCrate (const std::vector<int>& idx, const juce::File& folder)
{
    folder.createDirectory();
    int ok = 0;
    for (int i : idx)
    {
        if (i < 0 || i >= (int) library.items.size()) continue;
        const auto& it = library.items[(size_t) i];
        if (it.file.existsAsFile() && ! it.dirty)
        {
            if (it.file.copyFileTo (folder.getNonexistentChildFile (it.file.getFileNameWithoutExtension(), it.file.getFileExtension(), false))) ++ok;
            continue;
        }
        auto b = library.load (i);
        if (! b) continue;
        auto f = folder.getNonexistentChildFile (io::safeName (it.name), ".wav", false);
        if (io::writeWav (f, b->audio, b->sampleRate, ui.exportBits)) ++ok;
    }
    exportStatus = "Saved " + juce::String (ok) + " samples to " + folder.getFullPathName();
    status ("SAVED " + juce::String (ok) + " SAMPLES");
}

void Controller::libBackup (const juce::File& zip)
{
    const bool ok = library.backupTo (zip);
    exportStatus = ok ? "Library backed up to " + zip.getFullPathName() : "Could not write " + zip.getFileName() + " (or the library is empty)";
    status (ok ? "LIBRARY BACKED UP" : "BACKUP FAILED");
}

void Controller::libRestore (const juce::File& zipFile)
{
    juce::ZipFile zf (zipFile);
    if (zf.getIndexOfFileName ("project.json") >= 0)
    {
        // a song: take the library samples it carries
        Project np = P;
        juce::String msg;
        if (! io::loadSong (zipFile, np, msg)) { status ("COULD NOT READ THAT SONG"); return; }
        for (auto& it : np.lib) if (it.buf) library.add (it.name, it.crate, it.buf);
        sendChangeMessage();
        status (juce::String ((int) np.lib.size()) + " SAMPLES TAKEN FROM THE SONG (SAVE LIBRARY WRITES THEM)");
        return;
    }
    const int n = library.restoreFolderZip (zipFile);
    sendChangeMessage();
    status (n > 0 ? juce::String (n) + " SAMPLES RESTORED INTO THE LIBRARY" : "NOTHING NEW TO RESTORE");
}

void Controller::bounceSong (bool toLibrary)
{
    if (needPluginCopies ([this, toLibrary] { bounceSong (toLibrary); })) return;
    const auto plan = buildPlan(); const auto ids = activeTracks (plan);
    if (ids.empty()) { status ("NOTHING TO BOUNCE - SWITCH ON SOME STEPS FIRST"); return; }
    stop();
    auto snap = builder.build (P, stems, TrimState());
    auto result = std::make_shared<juce::AudioBuffer<float>>();
    auto plug = clonePlugins (44100, 1024);
    runJob ("Bouncing the song...", [snap, plan, ids, result, plug] (Job& j)
    {
        *result = Engine::renderPlan (snap, plan, ids, 44100, true, [&j] (float p) { j.progress (p); }, [&j] { return j.cancelled(); }, nullptr, plug.get());
    }, [this, result, toLibrary] (bool cancelled)
    {
        if (cancelled || result->getNumSamples() == 0) { status ("BOUNCE CANCELLED"); return; }
        auto b = makeSample (std::move (*result), 44100);
        const auto name = io::safeName (P.name) + " bounce " + juce::Time::getCurrentTime().toString (false, true, false, true).replace (":", ".");
        if (toLibrary)
        {
            library.add (name, "Bounces", b);
            sendChangeMessage(); status ("SONG BOUNCED INTO THE LIBRARY (CRATE \"BOUNCES\") - SAVE LIBRARY WRITES IT");
            return;
        }
        int target = -1;
        for (int k = 1; k < 64; ++k) { const int g = (P.s.sel + k) % 64; if (! P.pads[(size_t) g].buf) { target = g; break; } }
        if (target < 0) { status ("NO EMPTY PAD FOR THE BOUNCE"); return; }
        pushUndo();
        Pad& p = P.pads[(size_t) target];
        p = Pad(); p.buf = b; p.name = name;
        P.s.sel = target; ui.bank = target >> 3;
        changed(); status ("SONG BOUNCED ONTO " + padLabel (target));
    });
}

// ------------------------------------------------------------------ VST plug-ins
PluginSlot& Controller::slotRef (const SlotId& s)
{
    const int a = juce::jlimit (0, NPADS - 1, s.a);
    switch (s.kind)
    {
        case SlotId::PadFx:  return P.pads[(size_t) a].vfx[(size_t) juce::jlimit (0, PAD_VFX - 1, s.b)];
        case SlotId::Master: return P.masterVfx[(size_t) juce::jlimit (0, MASTER_VFX - 1, s.b)];
        case SlotId::Send:   return P.sendVfx[(size_t) juce::jlimit (0, NSEND - 1, s.a)][(size_t) juce::jlimit (0, SEND_VFX - 1, s.b)];
        default:             return P.pads[(size_t) a].inst;
    }
}

juce::String Controller::slotStatus (const SlotId& s) const
{
    const auto& slot = const_cast<Controller*> (this)->slotRef (s);
    if (slot.empty()) return {};
    auto* n = engine.pluginAt (s);
    if (n != nullptr && n->iid == slot.iid) return {};
    return failedIids.count (slot.iid) ? "missing" : "loading";
}

void Controller::syncPlugins()
{
    const auto all = SlotId::all();
    std::vector<SlotId> changedSlots;
    for (auto& s : all)
    {
        const auto& slot = slotRef (s);
        auto* n = engine.pluginAt (s);
        if (n != nullptr && n->iid == slot.iid) { n->bypass.store (slot.bypass); continue; }
        if (n == nullptr && slot.empty()) continue;
        if (n == nullptr && failedIids.count (slot.iid)) continue;   // missing plug-in: don't retry on every edit
        changedSlots.push_back (s);
    }
    if (changedSlots.empty() && freshNodes.empty()) return;

    std::map<int, PluginNode*> byIid;
    for (auto* n : engine.ownedNodes()) byIid[n->iid] = n;
    for (auto& s : changedSlots) engine.setSlot (s, nullptr);

    std::set<PluginNode*> used;
    for (auto& s : all)
    {
        const auto& slot = slotRef (s);
        auto* cur = engine.pluginAt (s);
        if (cur != nullptr) { used.insert (cur); continue; }   // unchanged
        if (slot.empty() || failedIids.count (slot.iid)) continue;
        PluginNode* node = nullptr;
        auto it = byIid.find (slot.iid);
        if (it != byIid.end() && used.count (it->second) == 0) node = it->second;   // same instance, maybe moved here
        if (node == nullptr)
        {
            auto fit = freshNodes.find (slot.iid);
            if (fit != freshNodes.end()) node = fit->second;
        }
        if (node == nullptr)
        {
            if (plugins.needsAsyncXml (slot.xml))
            {
                // AUv3 (iPad): open it in the background, then sync again
                if (openingIids.insert (slot.iid).second)
                {
                    const int iid = slot.iid; const bool isInst = slot.instrument; const auto state = slot.state; const auto name = slot.name;
                    statusText = "OPENING " + name.toUpperCase() + "...";
                    plugins.createAsyncXml (slot.xml, engine.getSampleRate(), 512,
                        [this, iid, isInst, state, name] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
                        {
                            openingIids.erase (iid);
                            if (inst == nullptr) { failedIids.insert (iid); status ("PLUG-IN NOT FOUND: " + name.toUpperCase() + " (" + err + ")"); changed(); return; }
                            auto nn = std::make_unique<PluginNode> (std::move (inst), iid, isInst);
                            nn->setState (state);
                            freshNodes[iid] = engine.addOwned (std::move (nn));
                            changed();
                        });
                }
                continue;
            }
            juce::String err;
            auto inst = plugins.createFromXml (slot.xml, engine.getSampleRate(), 512, err);
            if (inst == nullptr) { failedIids.insert (slot.iid); statusText = "PLUG-IN NOT FOUND: " + slot.name.toUpperCase() + " (" + err + ")"; continue; }
            auto nn = std::make_unique<PluginNode> (std::move (inst), slot.iid, slot.instrument);
            nn->setState (slot.state);
            node = engine.addOwned (std::move (nn));
        }
        node->bypass.store (slot.bypass);
        engine.setSlot (s, node);
        used.insert (node);
    }
    freshNodes.clear();
    for (auto* n : engine.ownedNodes())
        if (used.count (n) == 0 && openingIids.count (n->iid) == 0)
        {
            if (onPluginClosing) onPluginClosing (n);
            engine.retire (n);
        }
}

bool Controller::setPlugin (const SlotId& s, const juce::PluginDescription& d)
{
    status ("LOADING " + d.name.toUpperCase() + "...");
    if (plugins.needsAsync (d))
    {
        // AUv3 (iPad): opens in the background; its editor opens when it is ready
        plugins.createAsync (d, engine.getSampleRate(), 512, [this, s, d] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
        {
            installPlugin (s, d, std::move (inst), err);
            if (! slotRef (s).empty() && onOpenEditor) onOpenEditor (s);
        });
        return false;
    }
    juce::String err;
    auto inst = plugins.create (d, engine.getSampleRate(), 512, err);
    installPlugin (s, d, std::move (inst), err);
    return ! slotRef (s).empty() && slotRef (s).name == d.name;
}

void Controller::installPlugin (const SlotId& s, const juce::PluginDescription& d, std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
{
    if (inst == nullptr) { status ("COULD NOT LOAD " + d.name.toUpperCase() + ": " + err); return; }
    const int iid = newPluginIid();
    auto* node = engine.addOwned (std::make_unique<PluginNode> (std::move (inst), iid, d.isInstrument));
    freshNodes[iid] = node;
    pushUndo();
    auto& slot = slotRef (s);
    slot = PluginSlot();
    slot.iid = iid; slot.xml = PluginManager::toXml (d); slot.name = d.name; slot.instrument = d.isInstrument;
    if (s.kind == SlotId::Inst) selectPad (s.a);
    const auto id = d.createIdentifierString();
    recentPlugins.removeString (id);
    recentPlugins.insert (0, id);
    while (recentPlugins.size() > 12) recentPlugins.remove (recentPlugins.size() - 1);
    changed();
    status (d.name.toUpperCase() + " ON " + s.label().toUpperCase());
}

bool Controller::setPluginFromFile (const SlotId& s, const juce::String& path)
{
    if (! plugins.hasVst3()) { status ("THIS BUILD HAS NO PLUG-IN SUPPORT"); return false; }
    auto* fmt = plugins.formatForFile (path);
    if (fmt == nullptr) { status (juce::File (path).getFileName().toUpperCase() + " IS NOT A PLUG-IN THIS APP CAN OPEN"); return false; }
    const auto fileName = juce::File (path).getFileName().toUpperCase();
    juce::OwnedArray<juce::PluginDescription> found;
    // already scanned? use the list instead of opening it again
    for (auto& k : plugins.known.getTypes())
        if (k.fileOrIdentifier == path) found.add (new juce::PluginDescription (k));
    if (found.isEmpty())
    {
        if (plugins.isSkipped (path))
        {
            status (fileName + " FAILED A SCAN BEFORE AND IS SKIPPED - RETRY IT IN PLUG-INS... > CHOOSE & SCAN");
            return false;
        }
        status ("READING " + fileName + "...");
        const auto o = plugins.scanOne (*fmt, path, found);   // runs in the helper: a crash can't take the app down
        if (o.status != ScanOutcome::ok)
        {
            status (fileName + ": " + o.detail.toUpperCase() + (o.failed() ? " - IT WILL BE SKIPPED" : juce::String()));
            return false;
        }
    }
    const bool wantInst = s.kind == SlotId::Inst;
    for (auto* d : found)
        if (d->isInstrument == wantInst) return setPlugin (s, *d);
    status (found[0]->name.toUpperCase() + (wantInst ? " IS AN EFFECT - DROP IT ON AN INSERT SLOT" : " IS AN INSTRUMENT - DROP IT ON A PAD OR AN INSTRUMENT SLOT"));
    return false;
}

void Controller::removePlugin (const SlotId& s)
{
    if (slotRef (s).empty()) return;
    pushUndo();
    const auto name = slotRef (s).name;
    slotRef (s) = PluginSlot();
    changed();
    status (name.toUpperCase() + " REMOVED - " CX_MOD_UP "+Z BRINGS IT BACK");
}

void Controller::toggleBypass (const SlotId& s)
{
    auto& slot = slotRef (s);
    if (slot.empty()) return;
    slot.bypass = ! slot.bypass;
    if (auto* n = engine.pluginAt (s)) n->bypass.store (slot.bypass);
    changed();
    status (slot.name.toUpperCase() + (slot.bypass ? " BYPASSED" : " ON"));
}

void Controller::releaseAllPlugins()
{
    for (auto& s : SlotId::all()) engine.setSlot (s, nullptr);
    for (auto* n : engine.ownedNodes()) { if (onPluginClosing) onPluginClosing (n); engine.retire (n); }
    engine.deviceStopped();
    engine.collectGarbage();
}

void Controller::pullPluginStates()
{
    for (auto& s : SlotId::all())
    {
        auto& slot = slotRef (s);
        if (slot.empty()) continue;
        if (auto* n = engine.pluginAt (s)) if (n->iid == slot.iid) slot.state = n->getState();
    }
}

void Controller::freshPluginCopies (Pad& p)
{
    auto fresh = [this] (PluginSlot& slot)
    {
        if (slot.empty()) return;
        for (auto* n : engine.ownedNodes()) if (n->iid == slot.iid) { slot.state = n->getState(); break; }
        slot.iid = newPluginIid();
    };
    fresh (p.inst);
    for (auto& v : p.vfx) fresh (v);
}

bool Controller::needPluginCopies (std::function<void()> again, int onlyPadInstrument)
{
    // an offline render needs its own copy of every plug-in. VST3s are copied on the spot; AUv3s (iPad)
    // open asynchronously, so open them first and run the render again when all are there.
    if (sparesWanted > 0) { status ("GETTING THE PLUG-INS READY..."); return true; }
    std::vector<std::pair<int, juce::String>> want;
    for (auto& s : SlotId::all())
    {
        if (onlyPadInstrument >= 0 && ! (s.kind == SlotId::Inst && s.a == onlyPadInstrument)) continue;
        if (engine.pluginAt (s) == nullptr) continue;
        const auto& slot = slotRef (s);
        if (spareInst.count (slot.iid) || ! plugins.needsAsyncXml (slot.xml)) continue;
        want.push_back ({ slot.iid, slot.xml });
    }
    if (want.empty()) return false;
    sparesWanted = (int) want.size();
    status ("GETTING " + juce::String (sparesWanted) + " PLUG-IN" + (sparesWanted > 1 ? "S" : "") + " READY FOR THE RENDER...");
    for (auto& w : want)
    {
        const int iid = w.first;
        plugins.createAsyncXml (w.second, 48000, 1024, [this, iid, again] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String&)
        {
            if (inst != nullptr) spareInst[iid] = std::move (inst);
            if (--sparesWanted == 0 && again) again();
        });
    }
    return true;
}

PluginSetPtr Controller::clonePlugins (double sampleRate, int block)
{
    auto set = std::make_shared<PluginSet>();
    for (auto& s : SlotId::all())
    {
        auto* live = engine.pluginAt (s);
        if (live == nullptr) continue;
        const auto& slot = slotRef (s);
        juce::String err;
        std::unique_ptr<juce::AudioPluginInstance> inst;
        auto sp = spareInst.find (slot.iid);
        if (sp != spareInst.end()) { inst = std::move (sp->second); spareInst.erase (sp); }   // opened ahead (AUv3)
        else inst = plugins.createFromXml (slot.xml, sampleRate, block, err);
        if (inst == nullptr) continue;
        auto node = std::make_unique<PluginNode> (std::move (inst), slot.iid, slot.instrument);
        node->setState (live->getState());
        node->bypass.store (slot.bypass);
        node->prepare (sampleRate, block, true);
        set->nodes[s] = std::move (node);
    }
    return set->nodes.empty() ? nullptr : set;
}

// ------------------------------------------------------------------ mixer
void Controller::setPadPan (int gi, float v)
{
    pushUndo ("pan" + juce::String (gi));
    P.pads[(size_t) juce::jlimit (0, NPADS - 1, gi)].pan = juce::jlimit (-1.0f, 1.0f, v);
    changed();
}

void Controller::setPadSend (int gi, int k, float v)
{
    pushUndo ("send" + juce::String (gi) + "_" + juce::String (k));
    P.pads[(size_t) juce::jlimit (0, NPADS - 1, gi)].send[(size_t) juce::jlimit (0, NSEND - 1, k)] = juce::jlimit (0.0f, 1.0f, v);
    changed();
}

void Controller::setPadVolAt (int gi, float v)
{
    pushUndo ("padvol" + juce::String (gi));
    P.pads[(size_t) juce::jlimit (0, NPADS - 1, gi)].vol = juce::jlimit (0.0f, 1.5f, v);
    changed();
}

void Controller::toggleSolo (int gi)
{
    auto& p = P.pads[(size_t) juce::jlimit (0, NPADS - 1, gi)];
    p.solo = ! p.solo;
    changed(); status (padLabel (gi) + (p.solo ? " SOLO" : " SOLO OFF"));
}

void Controller::setReturnLevel (int k, float v)
{
    pushUndo ("ret" + juce::String (k));
    P.retLevel[(size_t) juce::jlimit (0, NSEND - 1, k)] = juce::jlimit (0.0f, 1.5f, v);
    changed();
}

void Controller::setInstRoot (int gi, int note)
{
    pushUndo ("instroot" + juce::String (gi));
    P.pads[(size_t) juce::jlimit (0, NPADS - 1, gi)].instRoot = juce::jlimit (0, 127, note);
    changed();
    status (padLabel (gi) + " PLAYS NOTE " + noteName (P.pads[(size_t) gi].instRoot) + " ON ITS INSTRUMENT");
}

// ------------------------------------------------------------------ auto-sampler: play a VST instrument note by note into samples
void Controller::autoSample (const AutoSample& a)
{
    const SlotId sid { SlotId::Inst, a.pad, 0 };
    auto* live = engine.pluginAt (sid);
    const auto& slot = slotRef (sid);
    if (live == nullptr || slot.empty()) { status ("PUT A " + pluginKind() + " INSTRUMENT ON THIS PAD FIRST"); return; }
    if ((! a.toPads && a.target == a.pad) || (a.toPads && a.pad >= a.target && a.pad < a.target + a.count))
    { status ("CHOOSE OTHER PADS FOR THE SAMPLES - THE INSTRUMENT LIVES ON " + padLabel (a.pad)); return; }
    if (needPluginCopies ([this, a] { autoSample (a); }, a.pad)) return;
    juce::String err;
    std::unique_ptr<juce::AudioPluginInstance> inst;
    auto sp = spareInst.find (slot.iid);
    if (sp != spareInst.end()) { inst = std::move (sp->second); spareInst.erase (sp); }
    else inst = plugins.createFromXml (slot.xml, 48000, 512, err);
    if (inst == nullptr) { status ("COULD NOT OPEN THE INSTRUMENT FOR SAMPLING: " + err); return; }
    auto node = std::make_shared<PluginNode> (std::move (inst), 0, true);
    node->setState (live->getState());
    node->prepare (48000, 512, true);
    std::vector<int> notes;
    for (int k = 0; k < juce::jmax (1, a.count); ++k)
    {
        const int nt = a.startNote + k * juce::jmax (1, a.step);
        if (nt > 127) break;
        notes.push_back (nt);
    }
    auto results = std::make_shared<std::vector<juce::AudioBuffer<float>>>();
    const auto plugName = slot.name;
    runJob ("Auto-sampling " + plugName + "...", [node, notes, a, results] (Job& j)
    {
        const double sr = 48000; const int B = 512;
        const float silent = std::pow (10.0f, -66.0f / 20.0f);
        float L[512], R[512];
        for (size_t k = 0; k < notes.size(); ++k)
        {
            if (j.cancelled()) return;
            j.setStatusMessage ("Note " + noteName (notes[k]) + " (" + juce::String ((int) k + 1) + " of " + juce::String ((int) notes.size()) + ")");
            node->proc->reset();
            const int holdN = (int) (a.noteSec * sr), maxTail = (int) (a.tailSec * sr), quietNeed = (int) (0.25 * sr);
            juce::AudioBuffer<float> out (2, holdN + maxTail + B);
            int pos = 0, quiet = 0, lastLoud = 0;
            node->addNote (notes[k], a.vel, 0);
            bool released = false;
            while (pos < holdN + maxTail)
            {
                if (! released && pos + B > holdN) { node->addNote (notes[k], 0, juce::jlimit (0, B - 1, holdN - pos)); released = true; }
                std::fill (L, L + B, 0.0f); std::fill (R, R + B, 0.0f);
                node->process (L, R, B);
                out.copyFrom (0, pos, L, B); out.copyFrom (1, pos, R, B);
                float pk = 0;
                for (int i = 0; i < B; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
                pos += B;
                if (pk > silent) { quiet = 0; lastLoud = pos; } else quiet += B;
                if (released && quiet >= quietNeed) break;
            }
            node->allNotesOff (0);
            std::fill (L, L + B, 0.0f); std::fill (R, R + B, 0.0f);
            node->process (L, R, B);
            const int len = juce::jmax (B, juce::jmin (pos, juce::jmax (holdN, lastLoud + (int) (0.02 * sr))));
            juce::AudioBuffer<float> trimmed (2, len);
            trimmed.copyFrom (0, 0, out, 0, 0, len); trimmed.copyFrom (1, 0, out, 1, 0, len);
            // 3 ms fade-out so the cut never clicks
            const int fo = juce::jmin (len, (int) (0.003 * sr));
            for (int i = 0; i < fo; ++i) { const float g = (float) i / (float) fo; trimmed.setSample (0, len - 1 - i, trimmed.getSample (0, len - 1 - i) * g); trimmed.setSample (1, len - 1 - i, trimmed.getSample (1, len - 1 - i) * g); }
            results->push_back (std::move (trimmed));
            j.progress ((float) (k + 1) / (float) notes.size());
        }
    }, [this, results, a, notes, plugName] (bool cancelled)
    {
        if (cancelled || results->empty()) { status ("AUTO-SAMPLE CANCELLED"); return; }
        pushUndo();
        if (a.toPads)
        {
            int placed = 0;
            for (size_t k = 0; k < results->size(); ++k)
            {
                const int gi = a.target + (int) k;
                if (gi >= NPADS) break;
                Pad np;
                np.buf = makeSample (std::move ((*results)[k]), 48000);
                np.name = plugName.substring (0, 12) + " " + noteName (notes[k]);
                P.pads[(size_t) gi] = np;
                ++placed;
            }
            P.s.sel = a.target; ui.bank = a.target >> 3;
            changed(); status ("AUTO-SAMPLED " + juce::String (placed) + " NOTES ONTO " + padLabel (a.target) + "-" + padLabel (a.target + placed - 1));
            return;
        }
        // one sample, one chop per note, each key plays its own note (like an MPC keygroup)
        int total = 0;
        for (auto& b : *results) total += b.getNumSamples();
        juce::AudioBuffer<float> all (2, total);
        std::vector<float> markers;
        int pos = 0;
        for (size_t k = 0; k < results->size(); ++k)
        {
            auto& b = (*results)[k];
            if (k > 0) markers.push_back ((float) pos / (float) total);
            all.copyFrom (0, pos, b, 0, 0, b.getNumSamples()); all.copyFrom (1, pos, b, 1, 0, b.getNumSamples());
            pos += b.getNumSamples();
        }
        const int gi = juce::jlimit (0, NPADS - 1, a.target);
        Pad np;
        np.buf = makeSample (std::move (all), 48000);
        np.name = plugName.substring (0, 14) + " multi";
        np.markers = markers;
        np.keyMode = MODE_CHOP;
        np.hasKmap = true;
        for (int k = 0; k < (int) results->size() && k < 32; ++k) np.kmap[k] = k;
        P.pads[(size_t) gi] = np;
        P.s.sel = gi; ui.bank = gi >> 3;
        changed(); status ("AUTO-SAMPLED " + juce::String ((int) results->size()) + " NOTES ONTO " + padLabel (gi) + " - EVERY KEY PLAYS ITS OWN NOTE (CHOP MODE)");
    });
}
} // namespace cx
