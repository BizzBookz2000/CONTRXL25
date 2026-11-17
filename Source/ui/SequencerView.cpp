#include "SequencerView.h"

namespace cx::ui
{
namespace
{
    // "Rec timing...": tap along with the metronome so recorded hits land where you played them
    class TimingBox : public juce::Component, private juce::Timer, private juce::ChangeListener
    {
    public:
        explicit TimingBox (Controller& c) : ctl (c)
        {
            title.setText ("Recording timing", juce::dontSendNotification);
            title.setFont (mono (13.0f, true)); title.setColour (juce::Label::textColourId, C::accent);
            help.setText ("Recorded hits landing a step late (or on the step before)? Sound and MIDI can reach you later than "
                          "the computer knows - Bluetooth headphones especially. Press Start, then tap any pad or key exactly on "
                          "each click, " + juce::String (Controller::calibTaps) + " times. The app measures how late your taps arrive "
                          "and corrects every recording from then on (saved for this computer).", juce::dontSendNotification);
            help.setFont (mono (11.0f)); help.setColour (juce::Label::textColourId, C::fg);
            help.setJustificationType (juce::Justification::topLeft);
            progress.setFont (mono (12.0f, true)); progress.setColour (juce::Label::textColourId, C::accent);
            offset.labelWidth = 92; offset.valueWidth = 70;
            offset.onChange = [this] (double v) { ctl.setRecOffset ((float) v); };
            bStart.onClick = [this] { if (ctl.timingTestRunning()) ctl.stopTimingTest (false); else ctl.startTimingTest(); update(); };
            bReset.onClick = [this] { ctl.setRecOffset (0); ctl.status ("RECORD TIMING RESET TO 0 MS"); update(); };
            for (auto* c2 : { (juce::Component*) &title, (juce::Component*) &help, (juce::Component*) &progress,
                              (juce::Component*) &offset, (juce::Component*) &bStart, (juce::Component*) &bReset })
                addAndMakeVisible (c2);
            ctl.addChangeListener (this);
            update();
            startTimerHz (10);
            setSize (470, 230);
        }
        ~TimingBox() override
        {
            ctl.removeChangeListener (this);   // a running test goes on (clicking a pad closes this box); the status line counts the taps
        }
        void update()
        {
            const bool run = ctl.timingTestRunning();
            bStart.setButtonText (run ? "Stop test" : "Start tap test");
            offset.set (ctl.recOffset());
            const float o = ctl.recOffset();
            progress.setText (run ? "Tap on every click: " + juce::String (ctl.timingTestCount()) + " / " + juce::String (Controller::calibTaps)
                                  : (std::abs (o) < 0.5f ? juce::String ("No correction (0 ms)")
                                                         : "Hits are moved " + juce::String (std::abs (o), 0) + " ms " + (o > 0 ? "earlier" : "later")),
                              juce::dontSendNotification);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            title.setBounds (r.removeFromTop (22)); r.removeFromTop (2);
            help.setBounds (r.removeFromTop (92)); r.removeFromTop (4);
            progress.setBounds (r.removeFromTop (22)); r.removeFromTop (4);
            offset.setBounds (r.removeFromTop (24)); r.removeFromTop (8);
            layoutRow (r.removeFromTop (28), { { &bStart, 0 }, { &bReset, 120 } }, 6);
        }
    private:
        void timerCallback() override { update(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { update(); }
        Controller& ctl;
        juce::Label title, help, progress;
        LSlider offset { "Correction", -300, 300, 1, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " ms"; } };
        Btn bStart { "Start tap test" }, bReset { "Reset", "No correction (0 ms)" };
    };
}

void showTimingBox (Controller& ctl, juce::Component& anchor)
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<TimingBox> (ctl), anchor.getScreenBounds(), nullptr);
}

// ------------------------------------------------------------------ StepGrid
juce::Rectangle<int> StepGrid::stepRect (int lane, int i) const
{
    const int x0 = stepsX(), w = getWidth() - x0 - 4;
    const int gap = 10, cell = (w - gap * 3) / 16;
    const int x = x0 + i * cell + (i / 4) * gap;
    return { x + 1, lane * ROW + 2, cell - 3, ROW - 4 };
}

bool StepGrid::hit (juce::Point<int> p, int& lane, int& step) const
{
    lane = p.y / ROW;
    step = -1;
    if (lane < 0 || lane >= (int) ctl.P.tracks.size()) return false;
    for (int i = 0; i < 16; ++i)
        if (stepRect (lane, i).expanded (2, 2).contains (p)) { step = i; return true; }
    return false;
}

void StepGrid::paint (juce::Graphics& g)
{
    const int len = ctl.P.plen[(size_t) ctl.P.s.pat];
    const bool showNow = ctl.engine.playing.load() && ctl.engine.curPat.load() == ctl.P.s.pat;
    const int nowStep = showNow ? ctl.engine.curStep.load() : -1;
    for (int l = 0; l < (int) ctl.P.tracks.size(); ++l)
    {
        const auto& T = ctl.P.tracks[(size_t) l];
        const int y = l * ROW;
        const bool selLane = l == ctl.ui.lane;
        if (selLane) { g.setColour (C::plate2); g.fillRect (0, y, getWidth(), ROW); }
        const auto col = pal (l);
        // lane button
        juce::Rectangle<float> lb (2.0f, (float) y + 2, 30.0f, (float) ROW - 4);
        g.setColour (selLane ? col : C::plate2); g.fillRoundedRectangle (lb, 4.0f);
        g.setColour (col); g.drawRoundedRectangle (lb, 4.0f, 1.0f);
        g.setColour (selLane ? juce::Colours::black : C::fg); g.setFont (mono (11.0f, true));
        g.drawText (juce::String (l + 1), lb, juce::Justification::centred);
        // M (mute) / S (solo) / R (arm) / L (lock)
        auto small = [&] (float x, const char* txt, bool on, juce::Colour onCol, juce::Colour onText)
        {
            juce::Rectangle<float> b (x, (float) y + 3, 22.0f, (float) ROW - 6);
            g.setColour (on ? onCol : C::plate2); g.fillRoundedRectangle (b, 3.0f);
            g.setColour (C::line); g.drawRoundedRectangle (b, 3.0f, 1.0f);
            g.setColour (on ? onText : C::mute); g.setFont (mono (10.0f, true)); g.drawText (txt, b, juce::Justification::centred);
        };
        small (36.0f, "M", T.mute, C::accent, juce::Colours::black);
        small (60.0f, "S", T.solo, C::ok, juce::Colours::black);
        small (84.0f, "R", T.arm, C::rec, juce::Colours::white);
        small (108.0f, "L", T.lock, C::blue, juce::Colours::white);
        // volume bar
        juce::Rectangle<float> vb (136.0f, (float) y + ROW / 2.0f - 3, 70.0f, 6.0f);
        g.setColour (C::well); g.fillRoundedRectangle (vb, 3.0f);
        g.setColour (C::accent.withAlpha (T.mute ? 0.3f : 0.8f)); g.fillRoundedRectangle (vb.withWidth (70.0f * T.vol), 3.0f);
        // pad name tooltip-ish (tiny)
        const int tpad = T.padIn (ctl.P.s.pat), tchop = T.chopIn (ctl.P.s.pat);
        const auto& pd = ctl.P.pads[(size_t) tpad];
        g.setColour (C::mute); g.setFont (mono (8.5f));
        g.drawText (padLabel (tpad) + (tchop >= 0 ? "." + juce::String (tchop + 1) : juce::String()) + (pd.buf || ! pd.inst.empty() ? "" : " -")
                    + (T.lock ? "  locked" : ""),
                    juce::Rectangle<int> (136, y, 80, 9), juce::Justification::centredLeft);
        // steps
        for (int i = 0; i < 16; ++i)
        {
            const int s = ctl.ui.page * 16 + i;
            auto r = stepRect (l, i).toFloat();
            if (s >= len) { g.setColour (C::well.withAlpha (0.5f)); g.fillRoundedRectangle (r, 3.0f); continue; }
            const bool on = ctl.stepOn (l, s), ev = ctl.stepHasEv (l, s);
            const bool inSel = ctl.ui.hasSel && l >= ctl.ui.sel.l0 && l <= ctl.ui.sel.l1 && s >= ctl.ui.sel.s0 && s <= ctl.ui.sel.s1;
            g.setColour (on ? col : (i % 4 == 0 ? C::plate2.brighter (0.08f) : C::plate2));
            if (T.mute && on) g.setColour (col.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 3.0f);
            if (ev) { g.setColour (on ? juce::Colours::white : col); g.fillEllipse (r.getCentreX() - 3, r.getCentreY() - 3, 6, 6); }
            if (on)
                if (auto* d = T.find (ctl.P.s.pat))
                {
                    if (d->chopAt[(size_t) s] >= 0)
                    {
                        g.setColour (juce::Colours::black); g.setFont (mono (8.5f, true));
                        g.drawText ("c" + juce::String (d->chopAt[(size_t) s] + 1), r, juce::Justification::centred);
                    }
                    else if (std::abs (d->semi[(size_t) s]) > 0.01f)
                    {
                        g.setColour (juce::Colours::black); g.setFont (mono (8.5f, true));
                        g.drawText (fmtSt (d->semi[(size_t) s]), r, juce::Justification::centred);
                    }
                }
            if (s == nowStep) { g.setColour (C::fg.withAlpha (0.85f)); g.drawRoundedRectangle (r.expanded (1.0f), 3.0f, 2.0f); }
            if (inSel) { g.setColour (C::blue); g.drawRoundedRectangle (r, 3.0f, 1.6f); }
            // where a dragged selection will land
            const auto& q = ctl.ui.sel;
            if (mode == MOVE && (moveDl || moveDs) && l - moveDl >= q.l0 && l - moveDl <= q.l1 && s - moveDs >= q.s0 && s - moveDs <= q.s1)
            { g.setColour (C::ok.withAlpha (0.4f)); g.fillRoundedRectangle (r, 3.0f); }
        }
    }
}

void StepGrid::tick()
{
    const int n = ctl.engine.playing.load() ? ctl.engine.curStep.load() + 1000 * ctl.engine.curPat.load() : -1;
    if (n != lastNow) { lastNow = n; repaint(); }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int lane = e.y / ROW;
    if (lane < 0 || lane >= (int) ctl.P.tracks.size()) return;
    mode = NONE;
    if (e.x < 34) { ctl.selectLane (lane, true); return; }
    if (e.x < 58) { ctl.toggleTrackMute (lane); return; }
    if (e.x < 82) { ctl.toggleTrackSolo (lane); return; }
    if (e.x < 106) { ctl.toggleTrackArm (lane); return; }
    if (e.x < 132) { ctl.toggleTrackLock (lane); return; }
    if (e.x < stepsX() - 4) { mode = VOL; ctl.setTrackVol (lane, juce::jlimit (0.0f, 1.0f, (float) (e.x - 136) / 70.0f)); selL = lane; return; }
    int l, i;
    if (! hit (e.getPosition(), l, i)) return;
    const int s = ctl.ui.page * 16 + i;
    if (e.mods.isPopupMenu())
    {
        // right click (Mac: also Ctrl+click): clear this step - penciled and live hits. Drag to clear more.
        mode = ERASE; lastL = l; lastS = s;
        undoMade = ctl.clearStep (l, s, true);
        return;
    }
   #if JUCE_MAC
    const bool copyMod = e.mods.isAltDown();                            // Option (Ctrl+click is a right click on a Mac)
   #else
    const bool copyMod = e.mods.isAltDown() || e.mods.isCtrlDown();      // Ctrl or Alt
   #endif
    if (copyMod && ! ctl.ui.selTool)
    {
        // Option (Mac) / Ctrl or Alt (Windows) + drag: copy this step onto every step you drag over
        if (! ctl.stepHasAnything (l, s)) { ctl.status ("NOTHING ON THIS STEP TO COPY - " CX_COPYMOD_UP "-DRAG FROM A STEP THAT HAS A HIT"); return; }
        mode = STAMP; stampL = l; stampS = s; lastL = l; lastS = s; undoMade = false;
        ctl.status ("COPYING STEP " + juce::String (s + 1) + " - DRAG ONTO THE STEPS THAT SHOULD GET IT");
        return;
    }
    if (ctl.ui.selTool)
    {
        const auto& q = ctl.ui.sel;
        if (ctl.ui.hasSel && l >= q.l0 && l <= q.l1 && s >= q.s0 && s <= q.s1)
        {
            // drag the selection to move it (hold Ctrl or Alt to copy)
            mode = MOVE; moveL = l; moveS = s; moveDl = moveDs = 0;
            return;
        }
        mode = SELECT; selL = l; selS = s;
        ctl.ui.hasSel = true; ctl.ui.sel = { l, l, s, s };
        repaint();
        return;
    }
    if (ctl.ui.lane != l) ctl.selectLane (l, false);
    mode = PAINT; paintOn = ! ctl.stepOn (l, s);
    ctl.setStep (l, s, paintOn);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (mode == VOL) { ctl.setTrackVol (selL, juce::jlimit (0.0f, 1.0f, (float) (e.x - 136) / 70.0f)); return; }
    int l, i;
    if (! hit (e.getPosition(), l, i)) return;
    const int s = ctl.ui.page * 16 + i;
    if (mode == PAINT) ctl.setStep (l, s, paintOn);
    else if ((mode == ERASE || mode == STAMP) && (l != lastL || s != lastS))
    {
        // one undo step for the whole drag: made by the first step that actually changes
        lastL = l; lastS = s;
        if (mode == ERASE) { if (ctl.clearStep (l, s, ! undoMade)) undoMade = true; }
        else if (l != stampL || s != stampS) { ctl.copyStep (stampL, stampS, l, s, ! undoMade); undoMade = true; }
    }
    else if (mode == MOVE)
    {
        moveDl = l - moveL; moveDs = s - moveS;
        repaint();
    }
    else if (mode == SELECT)
    {
        ctl.ui.sel = { juce::jmin (selL, l), juce::jmax (selL, l), juce::jmin (selS, s), juce::jmax (selS, s) };
        repaint();
    }
}

void StepGrid::mouseUp (const juce::MouseEvent& e)
{
    if (mode == SELECT) ctl.sendChangeMessage();
    if (mode == MOVE && (moveDl != 0 || moveDs != 0))
    {
        const bool copy = e.mods.isCtrlDown() || e.mods.isAltDown() || e.mods.isCommandDown();
        ctl.moveSteps (moveDl, moveDs, copy);
        moveDl = moveDs = 0;
        repaint();
    }
    mode = NONE;
}

// ------------------------------------------------------------------ SequencerView
static void styleLabel (juce::Label& l, const juce::String& t)
{
    l.setText (t, juce::dontSendNotification);
    l.setFont (mono (11.0f, true));
    l.setColour (juce::Label::textColourId, C::mute);
}

SequencerView::SequencerView (Controller& c) : ctl (c)
{
    for (int i = 0; i < NPAT; ++i)
    {
        auto* b = patBtns.add (new Btn (juce::String (i + 1), "Pattern " + juce::String (i + 1), [this, i] { ctl.setPattern (i); }));
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 8; ++i)
    {
        auto* b = pageBtns.add (new Btn ("Bar " + juce::String (i + 1), {}, [this, i] { ctl.setPage (i); }));
        addAndMakeVisible (b);
    }
    styleLabel (lPat, "PATTERN"); styleLabel (lLen, "LENGTH"); styleLabel (lBpm, "TEMPO"); styleLabel (lPage, "PAGE");
    styleLabel (lTrack, "TRACK"); styleLabel (lPad, "PAD"); styleLabel (lChop, "CHOP"); styleLabel (lChain, "SONG CHAIN");
    styleLabel (lTracks, "");
    for (auto* l : { &lPat, &lLen, &lBpm, &lPage, &lTrack, &lPad, &lChop, &lChain, &lTracks }) addAndMakeVisible (l);

    lenBox.addItem ("1 bar (16)", 16); lenBox.addItem ("2 bars (32)", 32); lenBox.addItem ("4 bars (64)", 64); lenBox.addItem ("8 bars (128)", 128);
    lenBox.onChange = [this] { if (lenBox.getSelectedId() != ctl.P.plen[(size_t) ctl.P.s.pat]) ctl.setPatternLength (lenBox.getSelectedId()); };
    bpmBox.setEditableText (true);
    bpmBox.addItem ("Project tempo", 1);
    for (int b = 60; b <= 180; b += 5) bpmBox.addItem (juce::String (b) + " BPM", b);
    bpmBox.setTooltip ("Per-pattern tempo. Type any value 40-240, or pick Project tempo to follow the main BPM");
    bpmBox.onChange = [this]
    {
        const int id = bpmBox.getSelectedId();
        int v = id == 1 ? 0 : id > 1 ? id : bpmBox.getText().getIntValue();
        if (v != 0 && (v < 40 || v > 240)) v = 0;
        if (v != ctl.P.pbpm[(size_t) ctl.P.s.pat]) ctl.setPatternBpm (v);
    };
    for (int i = 0; i < NPADS; ++i) padBox.addItem (padLabel (i), i + 1);
    padBox.onChange = [this] { const int p = padBox.getSelectedId() - 1; if (p >= 0 && p != ctl.P.tracks[(size_t) ctl.ui.lane].padIn (ctl.P.s.pat)) ctl.setTrackPad (ctl.ui.lane, p); };
    chopBox.onChange = [this] { const int c2 = chopBox.getSelectedId() - 2; if (chopBox.getSelectedId() > 0 && c2 != ctl.P.tracks[(size_t) ctl.ui.lane].chopIn (ctl.P.s.pat)) ctl.setTrackChop (ctl.ui.lane, c2); };
    for (auto* cb : { &lenBox, &bpmBox, &padBox, &chopBox }) { cb->setWantsKeyboardFocus (false); addAndMakeVisible (cb); }

    bCopyPat.onClick = [this] { ctl.copyPattern(); }; bPastePat.onClick = [this] { ctl.pastePattern(); };
    bDupPat.onClick = [this] { ctl.duplicatePattern(); }; bClearPat.onClick = [this] { ctl.clearPattern(); };
    bSelTool.onClick = [this] { ctl.ui.selTool = ! ctl.ui.selTool; if (! ctl.ui.selTool) ctl.ui.hasSel = false; ctl.status (ctl.ui.selTool ? "SELECT TOOL - DRAG OVER STEPS" : "PAINT TOOL"); };
    bSelCopy.onClick = [this] { ctl.copySteps(); }; bSelPaste.onClick = [this] { ctl.pasteSteps(); }; bSelDel.onClick = [this] { ctl.deleteSteps(); };
    bTrDn12.onClick = [this] { ctl.transposeSteps (-12); }; bTrDn.onClick = [this] { ctl.transposeSteps (-1); };
    bTrUp.onClick = [this] { ctl.transposeSteps (1); }; bTrUp12.onClick = [this] { ctl.transposeSteps (12); };
    bArm.onClick = [this] { ctl.toggleTrackArm (ctl.ui.lane); };
    bAdd.onClick = [this] { ctl.addTrack(); }; bDel.onClick = [this] { ctl.deleteTrack(); }; bDup.onClick = [this] { ctl.duplicateTrack(); };
    bDouble.onClick = [this] { ctl.doubleTracks(); }; bClrTrk.onClick = [this] { ctl.clearTrack(); };
    bChainAdd.onClick = [this] { ctl.chainAdd(); }; bChainDel.onClick = [this] { ctl.chainRemove(); };
    bChainL.onClick = [this] { ctl.chainMove (-1); }; bChainR.onClick = [this] { ctl.chainMove (1); };
    bRepDn.onClick = [this] { ctl.chainRep (-1); }; bRepUp.onClick = [this] { ctl.chainRep (1); };
    bSong.onClick = [this] { ctl.toggleSongMode(); };
    bPop.onClick = [this] { if (onPop) onPop(); };
    bMute.onClick = [this] { ctl.toggleTrackMute (ctl.ui.lane); };
    bSolo.onClick = [this] { ctl.toggleTrackSolo (ctl.ui.lane); };
    bLock.onClick = [this] { ctl.toggleTrackLock (ctl.ui.lane); };
    bSolo.setOnColour (C::ok); bLock.setOnColour (C::blue);
    bRecAll.onClick = [this] { ctl.ui.recAllArmed = ! ctl.ui.recAllArmed; ctl.status (ctl.ui.recAllArmed ? "REC ALL ARMED: EVERY ARMED TRACK RECORDS EVERY HIT" : "REC: EACH PAD GOES TO ITS OWN ARMED TRACK"); };
    bRecAll.setOnColour (C::rec);
    for (auto* b : { &bSolo, &bLock, &bRecAll }) addAndMakeVisible (b);
    bTiming.onClick = [this] { showTimingBox (ctl, bTiming); };
    addAndMakeVisible (bTiming);
    bSelTrk.onClick = [this] { ctl.selectTrackSteps(); };
    bChainClr.onClick = [this] { ctl.chainClear(); };
    for (auto* b : { &bMute, &bSelTrk, &bChainClr }) addAndMakeVisible (b);
    for (auto* b : { &bCopyPat, &bPastePat, &bDupPat, &bClearPat, &bSelTool, &bSelCopy, &bSelPaste, &bSelDel, &bTrDn12, &bTrDn, &bTrUp, &bTrUp12,
                     &bArm, &bAdd, &bDel, &bDup, &bDouble, &bClrTrk, &bChainAdd, &bChainDel, &bChainL, &bChainR, &bRepDn, &bRepUp, &bSong, &bPop })
        addAndMakeVisible (b);
    bArm.setOnColour (C::rec);

    view.setViewedComponent (&grid, false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);
    refresh();
}

void SequencerView::buildChain()
{
    chainBtns.clear();
    for (int i = 0; i < (int) ctl.P.chain.size(); ++i)
    {
        const auto& c = ctl.P.chain[(size_t) i];
        auto* b = chainBtns.add (new Btn ("P" + juce::String (c.pat + 1) + (c.rep > 1 ? " x" + juce::String (c.rep) : juce::String()),
                                          "Chain step " + juce::String (i + 1) + ": click to select, right-click to set the pattern"));
        b->setOn (i == ctl.ui.chainSel);
        b->onClick = [this, i]
        {
            if (juce::ModifierKeys::currentModifiers.isPopupMenu())
            {
                juce::PopupMenu m;
                for (int p = 0; p < NPAT; ++p) m.addItem (p + 1, "Pattern " + juce::String (p + 1));
                m.showMenuAsync ({}, [this, i] (int r) { if (r > 0) ctl.chainSetPat (i, r - 1); });
                return;
            }
            ctl.ui.chainSel = i; ctl.sendChangeMessage();
        };
        addAndMakeVisible (b);
    }
    resized();
}

void SequencerView::refresh()
{
    const int pat = ctl.P.s.pat;
    const int playingPat = ctl.engine.playing.load() ? ctl.engine.curPat.load() : -1;
    for (int i = 0; i < NPAT; ++i)
    {
        bool has = false;
        for (auto& t : ctl.P.tracks) if (auto* d = t.find (i)) if (d->hasData()) { has = true; break; }
        auto* b = patBtns[i];
        b->setOn (i == pat);
        b->setButtonText (juce::String (i + 1) + (has ? "\n*" : "\n ") + (i == playingPat ? ">" : ""));
    }
    const int len = ctl.P.plen[(size_t) pat];
    lenBox.setSelectedId (len, juce::dontSendNotification);
    if (ctl.P.pbpm[(size_t) pat] > 0) bpmBox.setText (juce::String (ctl.P.pbpm[(size_t) pat]) + " BPM", juce::dontSendNotification);
    else bpmBox.setSelectedId (1, juce::dontSendNotification);
    for (int i = 0; i < 8; ++i) { pageBtns[i]->setVisible (i < len / 16); pageBtns[i]->setOn (i == ctl.ui.page); }

    const auto& T = ctl.P.tracks[(size_t) juce::jlimit (0, (int) ctl.P.tracks.size() - 1, ctl.ui.lane)];
    lTrack.setText ("TRACK " + juce::String (ctl.ui.lane + 1), juce::dontSendNotification);
    const int tpad = T.padIn (ctl.P.s.pat), tchop = T.chopIn (ctl.P.s.pat);
    padBox.setSelectedId (tpad + 1, juce::dontSendNotification);
    chopBox.clear (juce::dontSendNotification);
    chopBox.addItem ("Whole sample", 1);
    for (int c = 0; c < ctl.P.pads[(size_t) tpad].numChops(); ++c) if (ctl.P.pads[(size_t) tpad].numChops() > 1) chopBox.addItem ("Chop " + juce::String (c + 1), c + 2);
    chopBox.setSelectedId (tchop + 2, juce::dontSendNotification);
    bArm.setOn (T.arm); bMute.setOn (T.mute); bSolo.setOn (T.solo); bLock.setOn (T.lock); bRecAll.setOn (ctl.ui.recAllArmed);
    bSelTool.setOn (ctl.ui.selTool);
    for (auto* b : { &bSelCopy, &bSelPaste, &bSelDel, &bTrDn12, &bTrDn, &bTrUp, &bTrUp12 }) b->setEnabled (ctl.ui.selTool);
    bSong.setOn (ctl.P.s.song);
    bPop.setButtonText (popped ? "Dock" : "Pop out");
    bSong.setButtonText (ctl.P.s.song ? "Song mode: on" : "Song mode: off");
    lTracks.setText (juce::String ((int) ctl.P.tracks.size()) + " tracks - click a number to select & hear it, M mute, S solo, R arm, L lock, drag the bar for volume. Click a step: add / remove a hit. Right-click: clear the step (also live hits, the dots). " CX_COPYMOD "-drag a step onto other steps to copy it", juce::dontSendNotification);
    buildChain();
    grid.setSize (juce::jmax (400, view.getWidth() - view.getScrollBarThickness()), grid.preferredHeight());
    grid.repaint();
}

void SequencerView::paint (juce::Graphics& g)
{
    g.fillAll (C::plate);
}

void SequencerView::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto row1 = r.removeFromTop (34);
    lPat.setBounds (row1.removeFromLeft (64));
    const int pw = juce::jmin (40, (row1.getWidth() - 560) / 16);
    for (auto* b : patBtns) { b->setBounds (row1.removeFromLeft (pw - 3)); row1.removeFromLeft (3); }
    row1.removeFromLeft (8);
    layoutRow (row1, { { &lLen, 52 }, { &lenBox, 112 }, { &lBpm, 46 }, { &bpmBox, 118 }, { &bCopyPat, 0 }, { &bPastePat, 0 }, { &bDupPat, 0 }, { &bClearPat, 0 } }, 4);
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (28);
    lPage.setBounds (row2.removeFromLeft (44));
    for (auto* b : pageBtns) if (b->isVisible()) { b->setBounds (row2.removeFromLeft (58)); row2.removeFromLeft (3); }
    row2.removeFromLeft (10);
    layoutRow (row2, { { &bSelTool, 70 }, { &bSelCopy, 56 }, { &bSelPaste, 56 }, { &bSelDel, 62 }, { &bTrDn12, 40 }, { &bTrDn, 34 }, { &bTrUp, 34 }, { &bTrUp12, 40 }, { &bSelTrk, 92 }, { nullptr, 0 }, { &bTiming, 104 }, { &bRecAll, 112 }, { &bPop, 80 } }, 3);
    r.removeFromTop (6);
    auto row3 = r.removeFromTop (28);
    layoutRow (row3, { { &lTrack, 76 }, { &lPad, 30 }, { &padBox, 70 }, { &lChop, 36 }, { &chopBox, 120 }, { &bArm, 46 }, { &bLock, 50 }, { &bMute, 48 }, { &bSolo, 46 }, { &bAdd, 70 },
                       { &bDel, 64 }, { &bDup, 46 }, { &bDouble, 86 }, { &bClrTrk, 56 }, { &lTracks, 0 } }, 4);
    r.removeFromTop (6);
    auto chainRow = r.removeFromBottom (30);
    r.removeFromBottom (6);
    view.setBounds (r);
    grid.setSize (juce::jmax (400, r.getWidth() - view.getScrollBarThickness()), grid.preferredHeight());
    lChain.setBounds (chainRow.removeFromLeft (92));
    layoutRow (chainRow.removeFromRight (540), { { &bChainClr, 94 }, { &bChainAdd, 58 }, { &bChainDel, 66 }, { &bChainL, 28 }, { &bChainR, 28 }, { &bRepDn, 54 }, { &bRepUp, 54 }, { &bSong, 0 } }, 3);
    for (auto* b : chainBtns) { if (chainRow.getWidth() < 50) { b->setBounds ({}); continue; } b->setBounds (chainRow.removeFromLeft (54)); chainRow.removeFromLeft (3); }
}
} // namespace cx::ui
