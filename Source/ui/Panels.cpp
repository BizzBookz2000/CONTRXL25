#include "Panels.h"
#include "dsp/Stems.h"

namespace cx::ui
{
static juce::String hzText (double hz) { return hz >= 1000 ? juce::String (hz / 1000.0, 2) + " kHz" : juce::String ((int) std::round (hz)) + " Hz"; }
static double cutFromPos (double v) { return 20.0 * std::pow (1000.0, v / 1000.0); }
static double posFromCut (double hz) { return std::log (std::max (20.0, hz) / 20.0) / std::log (1000.0) * 1000.0; }
static juce::String pct (double v) { return juce::String ((int) std::round (v * 100)) + "%"; }

static void caption (juce::Label& l, const juce::String& t)
{
    l.setText (t.toUpperCase(), juce::dontSendNotification);
    l.setFont (mono (11.0f));
    l.setColour (juce::Label::textColourId, C::mute);
}

static void noFocus (juce::ComboBox& c) { c.setWantsKeyboardFocus (false); }

// sub-crates are indented in lists (non-breaking spaces: menus trim normal leading spaces)
static juce::String crateIndent (const juce::String& path)
{
    const int d = Library::crateDepth (path);
    return d == 0 ? juce::String() : juce::String::repeatedString (juce::String::charToString ((juce::juce_wchar) 0x00A0), d * 4) + "- ";
}

// ================================================================== TopBar
TopBar::TopBar (Controller& c, Dialogs& d) : Panel (c, d)
{
    bSettings.onClick = [this] { dlg.showAudioSettings(); };
    bUndo.onClick = [this] { ctl.undo(); };
    bRedo.onClick = [this] { ctl.redo(); };
    bQwerty.onClick = [this] { ctl.ui.qwerty = ! ctl.ui.qwerty; ctl.status (ctl.ui.qwerty ? "QWERTY KEYS ON" : "QWERTY KEYS OFF"); };
    bPanic.onClick = [this] { Command cmd; cmd.type = Command::Panic; ctl.engine.push (cmd); ctl.status ("ALL SOUND STOPPED"); };
    noFocus (kbBox);
    kbBox.onChange = [this]
    {
        const int id = kbBox.getSelectedId();
        if (id == 999) { refresh(); dlg.askCustomKeys(); return; }   // "Your own keyboard..."
        // 125 = Akai MPK mini IV, 225 = Akai MPK mini 3: 25 keys + their pads and knobs (+ the IV's transport)
        const int profile = id == 125 ? 1 : id == 225 ? 2 : 0;
        const int keys = profile != 0 ? 25 : id;
        if (keys != ctl.P.s.keys) ctl.setKeyboard (keys);
        if (profile != dlg.controllerProfile()) dlg.setControllerProfile (profile);
    };
    caption (lKb, "Keyboard"); caption (lMaster, "Master"); caption (lSize, "Size");
    sizeBox.addItem ("Fit", 1);
    for (int p : { 60, 70, 80, 90, 100, 110, 125 }) sizeBox.addItem (juce::String (p) + "%", p);
    sizeBox.setTooltip ("Make the whole app smaller or bigger. Fit = fit your screen automatically");
    sizeBox.onChange = [this] { const int id = sizeBox.getSelectedId(); dlg.setUiScale (id == 1 ? 0 : id); };
    noFocus (sizeBox);
    master.setSliderStyle (juce::Slider::LinearHorizontal);
    master.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    master.setRange (0, 1, 0.01); master.setWantsKeyboardFocus (false); master.setScrollWheelEnabled (false);
    master.onValueChange = [this] { ctl.setMasterVol ((float) master.getValue()); };
    bLearn.onClick = [this] { dlg.toggleMidiLearn(); };
    bMidi.onClick = [this] { dlg.showMidiSettings(); };
    bLearn.setOnColour (C::ok);
    for (auto* b : { &bLearn, &bMidi }) b->getProperties().set ("noLearn", true);
    master.setComponentID ("master");
    master.setTooltip ("Master volume");
    kbBox.setTooltip ("Keyboard size: 25 or 32 keys, or \"Your own keyboard...\" (25-88 keys). \"25 keys IV\" = Akai MPK mini IV, \"25 keys 3\" = Akai MPK mini 3: "
                      "their pads play the pads and their knobs turn the pitch knobs (the IV's transport runs CONTRXL 25 too) - a quick setup links them");
    show (bSettings, bUndo, bRedo, bQwerty, bPanic, kbBox, master, lMaster, meter, sizeBox, lSize, bLearn, bMidi);
}

void TopBar::refresh()
{
    {
        const int keys = ctl.P.s.keys, prof = dlg.controllerProfile();
        const int sel = prof == 1 && keys == 25 ? 125 : prof == 2 && keys == 25 ? 225 : keys;
        if (kbBox.getNumItems() == 0 || kbKeys != keys)
        {
            kbKeys = keys;
            kbBox.clear (juce::dontSendNotification);
            kbBox.addItem ("25 keys", 25); kbBox.addItem ("32 keys", 32);
            kbBox.addItem ("25 keys IV", 125); kbBox.addItem ("25 keys 3", 225);
            if (keys != 25 && keys != 32) kbBox.addItem (juce::String (keys) + " keys", keys);
            kbBox.addSeparator();
            kbBox.addItem ("Your own keyboard...", 999);
        }
        kbBox.setSelectedId (sel, juce::dontSendNotification);
    }
    sizeBox.setSelectedId (dlg.getUiScale() == 0 ? 1 : dlg.getUiScale(), juce::dontSendNotification);
    master.setValue (ctl.P.s.mvol, juce::dontSendNotification);
    bUndo.setEnabled (ctl.canUndo()); bRedo.setEnabled (ctl.canRedo());
    bQwerty.setOn (ctl.ui.qwerty);
    bLearn.setOn (ctl.midiLearn);
    bLearn.setButtonText (ctl.midiLearn ? "Learning..." : "MIDI Learn");
}

void TopBar::tick()
{
    meter.push (ctl.engine.meterL.exchange (0.0f), ctl.engine.meterR.exchange (0.0f));
    repaint (getLocalBounds().removeFromLeft (420));
}

void TopBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (4, 6);
    auto brand = r.removeFromLeft (150);
    g.setColour (C::accent); g.fillRoundedRectangle (brand.removeFromLeft (118).toFloat(), 5.0f);
    g.setColour (juce::Colours::black); g.setFont (mono (19.0f, true));
    g.drawText ("CONTRXL", juce::Rectangle<int> (r.getX() - 150, r.getY(), 118, r.getHeight()), juce::Justification::centred);
    g.setColour (C::fg); g.drawText ("25", brand, juce::Justification::centred);
    auto tag = r.removeFromLeft (showTag ? 230 : 0);
    if (showTag)
    {
        g.setColour (C::plate2); g.fillRoundedRectangle (tag.toFloat().reduced (0, 4), 5.0f);
        g.setColour (C::mute); g.setFont (mono (10.5f, true));
        g.drawText ("KEYBOARD SAMPLING WORKSTATION", tag, juce::Justification::centred);
    }
    // MIDI activity LED
    const bool on = juce::Time::getMillisecondCounter() - ledT < 120;
    auto led = juce::Rectangle<float> ((float) tag.getRight() + 14, (float) getHeight() * 0.5f - 4, 8, 8);
    g.setColour (on ? C::ok : juce::Colour (0xff2c313c)); g.fillEllipse (led);
    g.setColour (C::mute); g.setFont (mono (9.5f)); g.drawText ("MIDI", led.translated (12, -4).withWidth (40).withHeight (16).toNearestInt(), juce::Justification::centredLeft);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (4, 6);
    showTag = getWidth() >= 1640;
    r.removeFromLeft (150 + (showTag ? 230 : 0) + 64);
    const bool wide = getWidth() >= 1400;
    layoutRow (r, { { &bSettings, 108 }, { &kbBox, 104 }, { &lSize, 34 }, { &sizeBox, 66 }, { nullptr, 0 }, { &lMaster, 50 }, { &master, wide ? 100 : 80 }, { &meter, wide ? 140 : 76 },
                    { &bPanic, 56 }, { &bUndo, 52 }, { &bRedo, 52 }, { &bQwerty, 70 }, { &bLearn, 92 }, { &bMidi, 58 } }, 6);
}

// ================================================================== Faceplate
Faceplate::Faceplate (Controller& c, Dialogs& d) : Panel (c, d)
{
    bLoop.onClick = [this] { ctl.toggle (ctl.P.s.loop, "LOOP"); };
    bMetro.onClick = [this] { ctl.toggle (ctl.P.s.metro, "METRONOME"); };
    bTempo.onClick = [this] { ctl.tapTempo(); };
    bPlay.onClick = [this] { ctl.play(); };
    bRec.onClick = [this] { ctl.toggleRec(); };
    bStop.onClick = [this] { ctl.stop(); };
    // from a MIDI controller these toggle (press Play again = stop, Stop again = play, Rec again = rec off)
    bPlay.getProperties().set ("transport", "play"); bStop.getProperties().set ("transport", "stop"); bRec.getProperties().set ("transport", "rec");
    for (auto* b : { &bLoop, &bMetro, &bTempo, &bPlay, &bRec, &bStop }) b->setHw (true);
    bRec.setOnColour (C::rec);
    bPlay.setOnColour (C::ok);
    bCount.onClick = [this] { ctl.toggle (ctl.P.s.count, "COUNT-IN"); };
    bQuant.onClick = [this] { ctl.toggle (ctl.P.s.quant, "QUANTIZE"); };
    bpm.setSliderStyle (juce::Slider::IncDecButtons);
    bpm.setRange (40, 240, 1);
    bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 26);
    bpm.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    bpm.onValueChange = [this] { if ((int) bpm.getValue() != (int) ctl.P.s.bpm) ctl.setBpm ((float) bpm.getValue()); };
    caption (lBpm, "BPM");
    show (bLoop, bMetro, bTempo, bPlay, bRec, bStop, bCount, bQuant, bpm, lBpm);

    for (int n = 0; n < 8; ++n)
    {
        auto& k = knobs[(size_t) n];
        k.getProperties().set ("knob", n);
        k.onGrab = [this, n] { ctl.selectPad (ctl.ui.bank * 8 + n); };
        k.onChange = [this, n] (float v) { ctl.setPitch (ctl.ui.bank * 8 + n, v); };
        addAndMakeVisible (k);
    }

    show (wave);
    padTitle.setFont (mono (12.0f, true));
    padTitle.setColour (juce::Label::textColourId, C::accent);
    show (padTitle);
    bPrev.onClick = [this] { stepRegion (-1); };
    bNext.onClick = [this] { stepRegion (1); };
    bPlaySmp.onClick = [this] { ctl.previewChop (ctl.selPad().markers.empty() ? -1 : ctl.selPad().sel); };
    bTrim.onClick = [this] { if (ctl.trim.on) ctl.trimCancel(); else ctl.trimOpen(); };
    bTrimOk.onClick = [this] { ctl.trimApply(); };
    bTrimX.onClick = [this] { ctl.trimCancel(); };
    bZoomIn.onClick = [this] { wave.zoomBy (1.5f); };
    bZoomOut.onClick = [this] { wave.zoomBy (1.0f / 1.5f); };
    bFit.onClick = [this] { wave.resetZoom(); };
    bChopWin.onClick = [this] { dlg.openChopWindow(); };
    wave.onLoadRequest = [this] { const int gi = ctl.P.s.sel; dlg.openAudio (true, [this, gi] (juce::Array<juce::File> f) { ctl.loadFiles (f, gi); }); };
    bTrimOk.setOnColour (C::ok);
    show (bPrev, bNext, bPlaySmp, bTrim, bTrimOk, bTrimX, bZoomIn, bZoomOut, bFit, bChopWin);
    auto trimChanged = [this] (double) { ctl.trimSet (ctl.trim.a, ctl.trim.b, (float) fadeIn.slider.getValue(), (float) fadeOut.slider.getValue(), (float) gain.slider.getValue()); };
    fadeIn.onChange = trimChanged; fadeOut.onChange = trimChanged; gain.onChange = trimChanged;
    fadeIn.labelWidth = fadeOut.labelWidth = gain.labelWidth = 66;
    show (fadeIn, fadeOut, gain);

    bLoad.onClick = [this] { const int gi = ctl.P.s.sel; dlg.openAudio (true, [this, gi] (juce::Array<juce::File> f) { ctl.loadFiles (f, gi); }); };
    bAddLay.onClick = [this] { dlg.openAudio (true, [this] (juce::Array<juce::File> f) { ctl.addLayers (f); }); };
    bSample.onClick = [this]
    {
        if (! ctl.isSampling() && dlg.inputChannels() == 0) { ctl.status ("NO AUDIO INPUT - TURN ONE ON IN AUDIO / MIDI SETTINGS"); return; }
        ctl.startSampling (true);
    };
    bResample.onClick = [this] { ctl.startSampling (false); };
    bSample.setOnColour (C::rec); bResample.setOnColour (C::rec);
    bSaveWav.onClick = [this]
    {
        auto& p = ctl.selPad();
        if (! p.buf) { ctl.status ("SELECT A PAD WITH A SAMPLE FIRST"); return; }
        dlg.saveFile ("Save pad as WAV", padLabel (ctl.P.s.sel) + "_" + io::safeName (p.name) + ".wav", "*.wav", [this] (juce::File f) { ctl.savePadWav (f); });
    };
    for (int n : { 2, 4, 8, 16, 25, 32, 0 })   // 0 = "as many as the keys" (shown for other keyboard sizes)
    {
        auto* b = eqBtns.add (new Btn (juce::String (n), "Cut the sample into " + juce::String (n) + " equal chops", [this, n] { ctl.equalChops (n > 0 ? n : ctl.P.s.keys); }));
        addAndMakeVisible (b);
    }
    bTrans.onClick = [this] { ctl.transientChops(); };
    bClearChops.onClick = [this] { ctl.setMarkers ({}); ctl.status ("CHOPS CLEARED"); };
    bManual.onClick = [this]
    {
        wave.manual = ! wave.manual;
        ctl.status (wave.manual ? "MANUAL CHOP - CLICK THE WAVEFORM TO ADD CHOP POINTS, DRAG TO MOVE, RIGHT-CLICK TO REMOVE" : "MANUAL CHOP OFF");
    };
    bToPads.onClick = [this] { ctl.chopsToPads(); };
    bToKeys.onClick = [this] { ctl.chopsToKeys(); };
    bExtract.onClick = [this] { openExtract(); };
    bCopy.onClick = [this] { ctl.copyPad(); };
    bPaste.onClick = [this] { ctl.pastePad(); };
    bClear.onClick = [this] { ctl.clearPad(); };
    bToLib.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Add to the library (top level, no crate)");
        const auto& cr = ctl.library.crates;
        for (int i = 0; i < cr.size(); ++i)
            m.addItem (i + 2, crateIndent (cr[i]) + Library::crateName (cr[i]));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bToLib), [this, cr] (int r)
        {
            if (r == 1) ctl.addToLibrary ({});
            else if (r > 1) ctl.addToLibrary (cr[r - 2]);
        });
    };
    show (bLoad, bAddLay, bSample, bResample, bSaveWav, bTrans, bClearChops, bManual, bToPads, bToKeys, bExtract, bCopy, bPaste, bClear, bToLib);
    show (smpMeter);
}

void Faceplate::stepRegion (int dir) { ctl.stepRegion (dir); }

namespace
{
    class ExtractBox : public juce::Component
    {
    public:
        explicit ExtractBox (Controller& c) : ctl (c)
        {
            auto& p = ctl.selPad();
            const int n = p.numChops();
            for (int i = 0; i < n; ++i) chop.addItem ("Chop " + juce::String (i + 1), i + 1);
            chop.setSelectedId ((p.sel >= 0 && p.sel < n ? p.sel : 0) + 1);
            int next = (ctl.P.s.sel + 1) % 64;
            for (int i = 1; i < 64; ++i) { const int g = (ctl.P.s.sel + i) % 64; if (! ctl.P.pads[(size_t) g].buf) { next = g; break; } }
            fillPads (next);
            for (int i = 0; i < ctl.P.s.keys; ++i) key.addItem ("Key " + juce::String (i + 1), i + 1);
            key.setSelectedId (1);
            title.setText ("Extract from " + padLabel (ctl.P.s.sel) + " - " + p.name, juce::dontSendNotification);
            title.setFont (mono (12.0f, true)); title.setColour (juce::Label::textColourId, C::accent);
            bPad.onClick = [this] { ctl.extractChop (chop.getSelectedId() - 1, pad.getSelectedId() - 1); int nx = pad.getSelectedId() % 64; fillPads (nx); };
            bKey.onClick = [this] { ctl.assignChopKey (chop.getSelectedId() - 1, key.getSelectedId() - 1); key.setSelectedId (juce::jmin (ctl.P.s.keys, key.getSelectedId() + 1)); };
            bPlay.onClick = [this] { ctl.previewChop (chop.getSelectedId() - 1); };
            bLearnPad.onClick = [this] { ctl.ui.extractChop = chop.getSelectedId() - 1; ctl.ui.manualLearn = 1; ctl.status ("HIT THE PAD THAT SHOULD GET CHOP " + juce::String (ctl.ui.extractChop + 1)); };
            bLearnKey.onClick = [this] { ctl.ui.extractChop = chop.getSelectedId() - 1; ctl.ui.manualLearn = 2; ctl.status ("PLAY THE KEY THAT SHOULD PLAY CHOP " + juce::String (ctl.ui.extractChop + 1)); };
            for (auto* c2 : { &chop, &pad, &key }) addAndMakeVisible (c2);
            bReset.onClick = [this] { ctl.resetKeys(); };
            for (auto* b : { &bPad, &bKey, &bPlay, &bLearnPad, &bLearnKey, &bReset }) addAndMakeVisible (b);
            addAndMakeVisible (title);
            setSize (440, 150);
        }
        void fillPads (int sel)
        {
            pad.clear (juce::dontSendNotification);
            for (int i = 0; i < 64; ++i) pad.addItem (padLabel (i) + (ctl.P.pads[(size_t) i].name.isNotEmpty() ? " - " + ctl.P.pads[(size_t) i].name : juce::String (" - empty")), i + 1);
            pad.setSelectedId (sel + 1, juce::dontSendNotification);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            title.setBounds (r.removeFromTop (22)); r.removeFromTop (4);
            layoutRow (r.removeFromTop (28), { { &chop, 110 }, { &bPlay, 70 }, { &pad, 0 } }, 6); r.removeFromTop (6);
            layoutRow (r.removeFromTop (28), { { &bPad, 0 }, { &key, 100 }, { &bKey, 0 } }, 6); r.removeFromTop (6);
            layoutRow (r.removeFromTop (28), { { &bLearnPad, 0 }, { &bLearnKey, 0 }, { &bReset, 0 } }, 6);
        }
        Controller& ctl;
        juce::ComboBox chop, pad, key; juce::Label title;
        Btn bPad { "Extract to pad" }, bKey { "Assign to key" }, bPlay { "Play" }, bLearnPad { "Pick a pad..." }, bLearnKey { "Pick a key..." },
            bReset { "Reset keys", "Back to normal: the keys play this pad's pitch again (removes the key assignments)" };
    };
}

void showExtractBox (Controller& ctl, juce::Component& anchor)
{
    auto& p = ctl.selPad();
    if (! p.buf || p.markers.empty()) { ctl.status ("CHOP THE SAMPLE FIRST - THEN EXTRACT PIECES OF IT"); return; }
    juce::CallOutBox::launchAsynchronously (std::make_unique<ExtractBox> (ctl), anchor.getScreenBounds(), nullptr);
}

void Faceplate::openExtract() { showExtractBox (ctl, bExtract); }

void Faceplate::refresh()
{
    bLoop.setOn (ctl.P.s.loop); bMetro.setOn (ctl.P.s.metro); bCount.setOn (ctl.P.s.count); bQuant.setOn (ctl.P.s.quant);
    bRec.setOn (ctl.ui.rec);
    bpm.setValue (ctl.P.s.bpm, juce::dontSendNotification);
    for (int n = 0; n < 8; ++n)
    {
        const int gi = ctl.ui.bank * 8 + n;
        knobs[(size_t) n].setValue (ctl.P.pads[(size_t) gi].pitch, padLabel (gi), gi == ctl.P.s.sel);
    }
    const auto& p = ctl.selPad();
    padTitle.setText (padLabel (ctl.P.s.sel) + (p.name.isNotEmpty() ? " - " + p.name : juce::String (" - empty"))
                      + (p.buf ? "  (" + (p.buf->peak() > 1e-6f ? juce::String (20 * std::log10 (p.buf->peak()), 1) + " dB peak)" : juce::String ("silent)")) : juce::String()),
                      juce::dontSendNotification);
    const bool trimming = ctl.trim.on && ctl.trim.pad == ctl.P.s.sel;
    bTrim.setOn (trimming); bTrimOk.setVisible (trimming); bTrimX.setVisible (trimming);
    fadeIn.setVisible (trimming); fadeOut.setVisible (trimming); gain.setVisible (trimming);
    if (trimming) { fadeIn.set (ctl.trim.fi); fadeOut.set (ctl.trim.fo); gain.set (ctl.trim.g); }
    bManual.setOn (wave.manual);
    {
        static const int EQ[] = { 2, 4, 8, 16, 25, 32 };
        const int keys = ctl.P.s.keys;
        for (int i = 0; i < eqBtns.size(); ++i)
        {
            if (i < 6) { eqBtns[i]->setVisible (EQ[i] <= keys); continue; }
            // "chop to" grows with your keyboard: 37, 49, 61... equal chops, one per key
            eqBtns[i]->setVisible (keys != 25 && keys != 32);
            eqBtns[i]->setButtonText (juce::String (keys));
            eqBtns[i]->setTooltip ("Cut the sample into " + juce::String (keys) + " equal chops - one for every key");
        }
    }
    wave.refresh();
    resized();
    repaint (displayArea);
}

void Faceplate::tick()
{
    wave.tick();
    smpMeter.tick();
    // PLAY: unlit when stopped, green while playing, red while playing + recording
    const bool playing = ctl.engine.playing.load();
    bPlay.setOnColour (ctl.ui.rec ? C::rec : C::ok);
    bPlay.setOn (playing);
    const int st = ctl.engine.captureState();
    const bool smp = st != Engine::CAP_IDLE;
    juce::String label;
    if (smp)
    {
        if (st == Engine::CAP_COUNT) label = "Count-in...";
        else if (st == Engine::CAP_WAIT) label = "Waiting...";
        else label = "Stop " + juce::String (ctl.engine.captureSeconds(), 1) + "s";
    }
    const bool in = ctl.capturingInput();
    bSample.setButtonText (smp && in ? label : "Sample");
    bResample.setButtonText (smp && ! in ? label : "Resample");
    bSample.setOn (smp && in); bResample.setOn (smp && ! in);
    repaint (displayArea);
}

void Faceplate::paint (juce::Graphics& g)
{
    g.setColour (C::plate); g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    g.setColour (C::line); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);
    // display
    auto d = displayArea.toFloat();
    // red OLED, like the web version
    const juce::Colour oledText (0xffff3b30), oledDim (0xffc4443b);
    g.setColour (juce::Colour (0xff2a0606)); g.fillRoundedRectangle (d, 6.0f);
    g.setColour (juce::Colour (0xff4d1010)); g.drawRoundedRectangle (d, 6.0f, 1.0f);
    auto r = displayArea.reduced (10, 6);
    const int ci = ctl.engine.countInLeft.load();
    g.setColour (oledDim); g.setFont (mono (11.0f));
    g.drawText (ctl.contextLine().toUpperCase(), r.removeFromTop (16), juce::Justification::centredLeft);
    g.setFont (mono (14.0f, true));
    g.setColour (oledText);
    g.drawFittedText (ctl.statusText, r.removeFromTop (34), juce::Justification::topLeft, 2);
    auto bottom = r.removeFromBottom (18);
    g.setFont (mono (12.0f, true)); g.setColour (oledDim);
    const bool playing = ctl.engine.playing.load();
    const int pat = playing ? ctl.engine.curPat.load() : ctl.P.s.pat;
    g.drawText (juce::String ((int) std::round (ctl.engine.playing.load() ? ctl.engine.curBpm.load() : ctl.P.bpmOf (ctl.P.s.pat))) + " BPM", bottom, juce::Justification::centredLeft);
    g.drawText (juce::String (playing ? "> " : "") + "PAT " + juce::String (pat + 1) + (playing && ctl.engine.curStep.load() >= 0 ? "  STEP " + juce::String (ctl.engine.curStep.load() + 1) : juce::String()),
                bottom, juce::Justification::centred);
    g.drawText (ctl.ui.rec ? "REC" : "", bottom, juce::Justification::centredRight);
    juce::ignoreUnused (ci);   // the big count-in number is drawn by the overlay over the whole window
}

void Faceplate::resized()
{
    auto r = getLocalBounds().reduced (12);
    // transport column
    auto tr = r.removeFromLeft (234);
    {
        auto grid = tr.removeFromTop (124);
        const int w = (grid.getWidth() - 12) / 3, h = (grid.getHeight() - 6) / 2;
        Btn* bs[6] = { &bLoop, &bMetro, &bTempo, &bPlay, &bRec, &bStop };
        for (int i = 0; i < 6; ++i) bs[i]->setBounds (grid.getX() + (i % 3) * (w + 6), grid.getY() + (i / 3) * (h + 6), w, h);
        tr.removeFromTop (8);
        layoutRow (tr.removeFromTop (28), { { &lBpm, 34 }, { &bpm, 0 } }, 4);
        tr.removeFromTop (6);
        layoutRow (tr.removeFromTop (26), { { &bCount, 0 }, { &bQuant, 0 } }, 6);
    }
    r.removeFromLeft (14);
    // display + knobs
    auto mid = r.removeFromLeft (juce::jmax (360, (int) (r.getWidth() * 0.40)));
    displayArea = mid.removeFromTop (96);
    mid.removeFromTop (8);
    const int kw = mid.getWidth() / 8;
    for (int n = 0; n < 8; ++n) knobs[(size_t) n].setBounds (mid.getX() + n * kw, mid.getY(), kw, juce::jmin (96, mid.getHeight()));
    r.removeFromLeft (14);
    // sample window
    auto head = r.removeFromTop (26);
    layoutRow (head, { { &padTitle, 0 }, { &bChopWin, 98 }, { &bPrev, 30 }, { &bNext, 30 }, { &bPlaySmp, 54 }, { &bZoomOut, 28 }, { &bZoomIn, 28 }, { &bFit, 40 },
                       { &bTrim, 54 }, { bTrimOk.isVisible() ? &bTrimOk : nullptr, bTrimOk.isVisible() ? 60 : 1 }, { bTrimX.isVisible() ? &bTrimX : nullptr, bTrimX.isVisible() ? 64 : 1 } }, 4);
    r.removeFromTop (4);
    auto tools2 = r.removeFromBottom (26); r.removeFromBottom (4);
    auto tools1 = r.removeFromBottom (26); r.removeFromBottom (4);
    smpMeter.setBounds (r.removeFromBottom (14)); r.removeFromBottom (4);
    if (fadeIn.isVisible())
    {
        auto tr2 = r.removeFromBottom (22); r.removeFromBottom (2);
        layoutRow (tr2, { { &fadeIn, 0 }, { &fadeOut, 0 }, { &gain, 0 } }, 10);
    }
    wave.setBounds (r);
    {
        std::vector<std::pair<juce::Component*, int>> items { { &bLoad, 0 }, { &bAddLay, 0 }, { &bSample, 0 }, { &bResample, 0 }, { &bSaveWav, 0 } };
        auto t = tools1;
        const int w = (t.getWidth() - 4 * 4) / 5;
        for (auto& it : items) { it.first->setBounds (t.removeFromLeft (w)); t.removeFromLeft (4); }
    }
    {
        auto t = tools2;
        int visibleEq = 0; for (auto* b : eqBtns) if (b->isVisible()) ++visibleEq;
        for (auto* b : eqBtns) if (b->isVisible()) { b->setBounds (t.removeFromLeft (30)); t.removeFromLeft (3); }
        t.removeFromLeft (4);
        Btn* rest[] = { &bTrans, &bClearChops, &bManual, &bToPads, &bToKeys, &bExtract, &bCopy, &bPaste, &bClear, &bToLib };
        const int n = 10, w = (t.getWidth() - (n - 1) * 3) / n;
        for (auto* b : rest) { b->setBounds (t.removeFromLeft (w)); t.removeFromLeft (3); }
    }
}

// ================================================================== KeyboardPanel
KeyboardPanel::KeyboardPanel (Controller& c, Dialogs& d) : Panel (c, d)
{
    bOctDn.onClick = [this] { ctl.setOct (ctl.P.s.oct - 1); };
    bOctUp.onClick = [this] { ctl.setOct (ctl.P.s.oct + 1); };
    bPop.onClick = [this] { dlg.togglePopKeyboard(); };
    lOct.setFont (mono (14.0f, true)); lOct.setJustificationType (juce::Justification::centred);
    lOct.setColour (juce::Label::textColourId, C::accent);
    pitchWheel.onChange = [this] (float v) { ctl.engine.bend.store (v); };
    modWheel.setValue (-1);
    show (bOctDn, bOctUp, bPop, lOct, keys, pitchWheel, modWheel);
}

void KeyboardPanel::refresh()
{
    lOct.setText ("OCT " + juce::String (ctl.P.s.oct >= 0 ? "+" : "") + juce::String (ctl.P.s.oct), juce::dontSendNotification);
    bPop.setButtonText (dlg.isKeyboardPopped() ? "Dock" : "Pop out");
    bPop.setTooltip (dlg.isKeyboardPopped() ? "Put the keyboard back in the main window" : "Put the keyboard in its own window");
    keys.repaint();
}

void KeyboardPanel::paint (juce::Graphics& g)
{
    if (drawBackground) g.fillAll (C::plate);
}

void KeyboardPanel::resized()
{
    auto r = getLocalBounds();
    if (drawBackground) r = r.reduced (12);
    auto left = r.removeFromLeft (150);
    {
        layoutRow (left.removeFromTop (26), { { &bOctDn, 0 }, { &bOctUp, 0 } }, 4);
        left.removeFromTop (2);
        auto row = left.removeFromTop (20);
        bPop.setBounds (row.removeFromRight (64).withTrimmedTop (1));
        lOct.setBounds (row);
        left.removeFromTop (4);
        layoutRow (left, { { &pitchWheel, 0 }, { &modWheel, 0 } }, 10);
    }
    r.removeFromLeft (12);
    keys.setBounds (r);
}

// ================================================================== Deck
Deck::Deck (Controller& c, Dialogs& d) : Panel (c, d)
{
    for (int b = 0; b < 8; ++b)
    {
        auto* btn = banks.add (new Btn (juce::String ("BANK ") + (char) ('A' + b), {}, [this, b] { ctl.setBank (b); }));
        btn->getProperties().set ("bank", b);
        addAndMakeVisible (btn);
    }
    for (int i = 0; i < 8; ++i)
    {
        auto* p = pads.add (new PadButton (ctl, i));
        p->onLoadRequest = [this] (int gi) { dlg.openAudio (true, [this, gi] (juce::Array<juce::File> f) { ctl.loadFiles (f, gi); }); };
        p->onSaveRequest = [this] (int gi)
        {
            ctl.selectPad (gi);
            dlg.saveFile ("Save pad as WAV", padLabel (gi) + "_" + io::safeName (ctl.P.pads[(size_t) gi].name) + ".wav", "*.wav", [this] (juce::File f) { ctl.savePadWav (f); });
        };
        addAndMakeVisible (p);
    }
    bKeys.onClick = [this] { ctl.setMode (MODE_KEYS); };
    bChop.onClick = [this] { ctl.setMode (MODE_CHOP); };
    bPadKeys.onClick = [this] { ctl.setKeysPlayPads (! ctl.P.s.keyPads); };
    bPadsToKeys.onClick = [this] { ctl.padsToKeys(); };
    bKeys.setTooltip ("Keys mode for the selected pad: the sample is pitched across the keys");
    bChop.setTooltip ("Chop mode for the selected pad: each key plays one chop");
    bGate.onClick = [this] { ctl.toggle (ctl.P.s.gate, "GATE"); };
    bVel.onClick = [this] { ctl.toggle (ctl.P.s.vel, "VELOCITY"); };
    bChoke.onClick = [this] { ctl.toggle (ctl.P.s.choke, "CHOKE"); };
    bClrPads.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Clear all pads?", "Remove the samples from all 64 pads? (Ctrl+Z undoes)", "Clear", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) ctl.clearAllPads(); }));
    };
    bClrKeys.onClick = [this] { ctl.clearKeys(); };
    bHideKeys.onClick = [this]
    {
        if (dlg.isKeyboardPopped()) { dlg.togglePopKeyboard(); return; }   // "Dock keyboard"
        ctl.ui.keysVisible = ! ctl.ui.keysVisible; ctl.sendChangeMessage();
        if (auto* p = getParentComponent()) p->resized();
    };
    static const char* N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int n = 36; n <= 84; ++n) rootBox.addItem (juce::String (N[n % 12]) + juce::String (n / 12 - 1), n);
    rootBox.setTooltip ("The key that plays the sample at its original pitch");
    rootBox.onChange = [this] { if (rootBox.getSelectedId() != ctl.P.s.root) ctl.setRoot (rootBox.getSelectedId()); };
    noFocus (rootBox);
    caption (lRoot, "Root");
    lDocked.setFont (mono (12.0f)); lDocked.setColour (juce::Label::textColourId, C::mute);
    lDocked.setJustificationType (juce::Justification::centred);
    lDocked.setText ("The keyboard is in its own window. QWERTY and MIDI keep working.", juce::dontSendNotification);
    show (bKeys, bChop, bGate, bVel, bChoke, bClrPads, bClrKeys, bHideKeys, rootBox, lRoot, lDocked, kb);
    show (bPadKeys, bPadsToKeys);
}

void Deck::refresh()
{
    for (int b = 0; b < 8; ++b) banks[b]->setOn (b == ctl.ui.bank);
    for (auto* p : pads) p->repaint();
    bKeys.setOn (! ctl.P.s.keyPads && ctl.selPad().keyMode == MODE_KEYS); bChop.setOn (! ctl.P.s.keyPads && ctl.selPad().keyMode == MODE_CHOP);
    bPadKeys.setOn (ctl.P.s.keyPads);
    bKeys.setButtonText (padLabel (ctl.P.s.sel) + " KEYS\nPitched");
    bChop.setButtonText (padLabel (ctl.P.s.sel) + " CHOP\nChops on keys");
    bGate.setOn (ctl.P.s.gate); bVel.setOn (ctl.P.s.vel); bChoke.setOn (ctl.P.s.choke);
    rootBox.setSelectedId (ctl.P.s.root, juce::dontSendNotification);
    const bool popped = dlg.isKeyboardPopped();
    bHideKeys.setButtonText (popped ? "Dock keyboard" : ctl.ui.keysVisible ? "Hide keyboard" : "Show keyboard");
    if (keyboardDocked()) kb.setVisible (ctl.ui.keysVisible);
    lDocked.setVisible (popped);
    kb.refresh();
}

void Deck::tick() { for (auto* p : pads) p->tick(); }

void Deck::paint (juce::Graphics& g)
{
    g.setColour (C::plate); g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    g.setColour (C::line); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);
}

void Deck::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto right = r.removeFromRight (236);
    {
        auto rr = right;
        layoutRow (rr.removeFromTop (44), { { &bKeys, 0 }, { &bChop, 0 }, { &bPadKeys, 0 } }, 4); rr.removeFromTop (6);
        layoutRow (rr.removeFromTop (26), { { &bGate, 0 }, { &bVel, 0 }, { &bChoke, 0 } }, 6); rr.removeFromTop (6);
        layoutRow (rr.removeFromTop (26), { { &lRoot, 44 }, { &rootBox, 0 } }, 6); rr.removeFromTop (6);
        layoutRow (rr.removeFromTop (26), { { &bClrPads, 0 }, { &bClrKeys, 0 } }, 6); rr.removeFromTop (6);
        layoutRow (rr.removeFromTop (26), { { &bPadsToKeys, 0 }, { &bHideKeys, 0 } }, 6);
    }
    r.removeFromRight (12);
    auto bankRow = r.removeFromTop (28);
    const int bw = (bankRow.getWidth() - 7 * 6) / 8;
    for (auto* b : banks) { b->setBounds (bankRow.removeFromLeft (bw)); bankRow.removeFromLeft (6); }
    r.removeFromTop (8);
    const bool showKb = keyboardDocked() && ctl.ui.keysVisible;
    auto padRow = r.removeFromTop (showKb ? 92 : juce::jmax (92, r.getHeight() - (dlg.isKeyboardPopped() ? 22 : 0)));
    const int pw = (padRow.getWidth() - 7 * 8) / 8;
    for (auto* p : pads) { p->setBounds (padRow.removeFromLeft (pw)); padRow.removeFromLeft (8); }
    if (dlg.isKeyboardPopped()) { lDocked.setBounds (r.withTrimmedTop (4)); return; }
    if (! showKb) return;
    r.removeFromTop (10);
    kb.setBounds (r);
}

// ================================================================== SoundPanel
SoundPanel::SoundPanel (Controller& c, Dialogs& d) : Panel (c, d)
{
    show (cFx, cLa, cSmp, cStr);
    auto filt = [this] (int t)
    {
        ctl.editFx ([t] (FxParams& f) { f.type = t; if (t == FT_HP && f.cut > 8000) f.cut = 120; }, "ft");
        ctl.status (t == FT_OFF ? "FILTER OFF" : juce::String (t == FT_LP ? "LOW-PASS" : t == FT_BP ? "BAND-PASS" : "HIGH-PASS") + " FILTER ON " + padLabel (ctl.P.s.sel));
    };
    fOff.onClick = [filt] { filt (FT_OFF); }; fLp.onClick = [filt] { filt (FT_LP); };
    fBp.onClick = [filt] { filt (FT_BP); }; fHp.onClick = [filt] { filt (FT_HP); };
    cut.format = [] (double v) { return hzText (cutFromPos (v)); };
    cut.onChange = [this] (double v) { ctl.editFx ([v] (FxParams& f) { f.cut = (float) cutFromPos (v); }, "cut"); };
    res.format = [] (double v) { FxParams f; f.res = (float) v / 100.0f; return "Q " + juce::String (f.qValue(), 1); };
    res.onChange = [this] (double v) { ctl.editFx ([v] (FxParams& f) { f.res = (float) v / 100.0f; }, "res"); };
    bLofi.onClick = [this] { ctl.editFx ([] (FxParams& f) { f.lofi = ! f.lofi; }, "lofi"); ctl.status (ctl.selPad().fx.lofi ? "LO-FI ON - 12-BIT / 26 kHz IS THE CLASSIC SAMPLER CRUNCH" : "LO-FI OFF"); };
    for (int b : { 4, 6, 8, 10, 12, 16 }) bits.addItem (juce::String (b) + " bit", b);
    for (int r : { 6000, 8000, 11025, 13000, 16000, 22050, 26040, 32000 }) rate.addItem (juce::String (r / 1000.0, 1) + " kHz", r);
    bits.onChange = [this] { const int v = bits.getSelectedId(); ctl.editFx ([v] (FxParams& f) { f.bits = v; }, "bits"); };
    rate.onChange = [this] { const int v = rate.getSelectedId(); ctl.editFx ([v] (FxParams& f) { f.rate = v; }, "rate"); };
    for (int i = 0; i < NRETRO; ++i) retro.addItem (retroName (i), i + 1);
    retro.onChange = [this]
    {
        const int v = retro.getSelectedId() - 1;
        if (v != ctl.selPad().fx.rm)
        {
            ctl.status ("RENDERING " + juce::String (retroName (v)) + "...");
            ctl.editFx ([v] (FxParams& f) { f.rm = v; }, "rm");
            ctl.status (v ? juce::String (retroName (v)) + " MODE ON " + padLabel (ctl.P.s.sel) : "RETRO MODE OFF");
        }
    };
    mg.addItem ("Off", 1); lg.addItem ("Off", 1);
    for (int i = 1; i <= 8; ++i) { mg.addItem (juce::String (i), i + 1); lg.addItem (juce::String (i), i + 1); }
    mg.onChange = [this] { if (mg.getSelectedId() - 1 != ctl.selPad().mg) ctl.setMuteGroup (mg.getSelectedId() - 1); };
    lg.onChange = [this] { if (lg.getSelectedId() - 1 != ctl.selPad().lg) ctl.setLinkGroup (lg.getSelectedId() - 1); };
    padVol.onChange = [this] (double v) { ctl.setPadVol ((float) v); };
    bRev.onClick = [this] { ctl.reversePad(); };
    bNorm.onClick = [this] { ctl.normalizePad(); };
    bAutoNorm.onClick = [this] { ctl.toggle (ctl.P.s.autoNorm, "AUTO-NORMALIZE"); };
    caption (lBits, "Bits"); caption (lRate, "Rate"); caption (lRetro, "Retro mode"); caption (lMg, "Mute grp"); caption (lLg, "Link grp");
    retroDesc.setFont (mono (11.0f)); retroDesc.setColour (juce::Label::textColourId, C::mute);
    retroDesc.setJustificationType (juce::Justification::topLeft);
    for (auto* cb : { &bits, &rate, &retro, &mg, &lg }) noFocus (*cb);
    show (fOff, fLp, fBp, fHp, cut, res, bLofi, bits, rate, retro, mg, lg, lBits, lRate, lRetro, lMg, lLg, retroDesc, padVol, bRev, bNorm, bAutoNorm);

    laOn.onClick = [this] { ctl.setLA ([] (LASettings& s) { s.on = ! s.on; }); };
    laComp.onClick = [this] { ctl.setLA ([] (LASettings& s) { s.limit = false; }); };
    laLim.onClick = [this] { ctl.setLA ([] (LASettings& s) { s.limit = true; }); };
    laPr.onChange = [this] (double v) { ctl.setLA ([v] (LASettings& s) { s.pr = (float) v; }); };
    laGain.onChange = [this] (double v) { ctl.setLA ([v] (LASettings& s) { s.gain = (float) v; }); };
    laGr.setFont (mono (12.0f, true)); laGr.setColour (juce::Label::textColourId, C::accent);
    show (laOn, laComp, laLim, laPr, laGain, laGr, vu);

    sThr.onClick = [this] { ctl.ui.smpThr = ! ctl.ui.smpThr; ctl.status (ctl.ui.smpThr ? "SAMPLING WAITS FOR SOUND ABOVE THE LEVEL" : "SAMPLING STARTS AT ONCE"); };
    sCount.onClick = [this] { ctl.ui.smpCount = ! ctl.ui.smpCount; ctl.sendChangeMessage(); };
    thrDb.onChange = [this] (double v) { ctl.ui.smpThrDb = (float) v; };
    smpInfo.setFont (mono (11.0f)); smpInfo.setColour (juce::Label::textColourId, C::mute);
    smpInfo.setJustificationType (juce::Justification::topLeft);
    show (sThr, sCount, thrDb, smpInfo, smpMeter);

    tKeep.onClick = [this] { ctl.ui.keepPitch = ! ctl.ui.keepPitch; ctl.sendChangeMessage(); };
    tMatch.onClick = [this] { ctl.matchBpm(); };
    tApply.onClick = [this] { ctl.applyStretch(); };
    tHalf.onClick = [this] { ctl.ui.stretchPct = 200; ctl.applyStretch(); };
    tDouble.onClick = [this] { ctl.ui.stretchPct = 50; ctl.applyStretch(); };
    show (tHalf, tDouble);
    strPct.onChange = [this] (double v) { ctl.ui.stretchPct = (float) v; };
    smpBpm.onChange = [this] (double v) { ctl.ui.sampleBpm = (float) v; };
    strInfo.setFont (mono (11.0f)); strInfo.setColour (juce::Label::textColourId, C::mute);
    strInfo.setJustificationType (juce::Justification::topLeft);
    show (tKeep, tMatch, tApply, strPct, smpBpm, strInfo);
}

void SoundPanel::refresh()
{
    const auto& p = ctl.selPad();
    const auto& fx = p.fx;
    cFx.title = "Pad FX - " + padLabel (ctl.P.s.sel) + (p.name.isNotEmpty() ? " - " + p.name : juce::String());
    cFx.repaint();
    fOff.setOn (fx.type == FT_OFF); fLp.setOn (fx.type == FT_LP); fBp.setOn (fx.type == FT_BP); fHp.setOn (fx.type == FT_HP);
    cut.set (posFromCut (fx.cut)); res.set (fx.res * 100.0);
    cut.setEnabled (fx.type != FT_OFF); res.setEnabled (fx.type != FT_OFF);
    bLofi.setOn (fx.lofi); bLofi.setButtonText (fx.lofi ? "Lo-Fi: on" : "Lo-Fi: off");
    bits.setSelectedId (fx.bits, juce::dontSendNotification); rate.setSelectedId (fx.rate, juce::dontSendNotification);
    bits.setEnabled (fx.lofi); rate.setEnabled (fx.lofi);
    retro.setSelectedId (fx.rm + 1, juce::dontSendNotification);
    retroDesc.setText (cx::retroDesc (fx.rm), juce::dontSendNotification);
    mg.setSelectedId (p.mg + 1, juce::dontSendNotification); lg.setSelectedId (p.lg + 1, juce::dontSendNotification);
    padVol.set (p.vol);
    bAutoNorm.setOn (ctl.P.s.autoNorm);
    laOn.setOn (ctl.P.la.on); laComp.setOn (! ctl.P.la.limit); laLim.setOn (ctl.P.la.limit);
    laPr.set (ctl.P.la.pr); laGain.set (ctl.P.la.gain);
    sThr.setOn (ctl.ui.smpThr); sCount.setOn (ctl.ui.smpCount); thrDb.set (ctl.ui.smpThrDb);
    tKeep.setOn (ctl.ui.keepPitch); strPct.set (ctl.ui.stretchPct); smpBpm.set (ctl.ui.sampleBpm);
    strInfo.setText (p.buf ? "Now " + juce::String (p.buf->duration(), 2) + " s. Match BPM stretches from the sample BPM to "
                             + juce::String ((int) ctl.P.bpmOf (ctl.P.s.pat)) + " BPM (this pattern)." : juce::String ("Load a sample first."), juce::dontSendNotification);
}

void SoundPanel::tick()
{
    if (smpMeter.isShowing()) smpMeter.tick();
    vu.setGr (ctl.engine.laGr.load());
    laGr.setText (juce::String (ctl.engine.laGr.load(), 1) + " dB", juce::dontSendNotification);
    const float lv = ctl.engine.captureLevel();
    smpInfo.setText (ctl.isSampling() ? "Input level " + (lv > 1e-5f ? juce::String (20 * std::log10 (lv), 1) : juce::String ("-inf")) + " dB - "
                                         + juce::String (ctl.engine.captureSeconds(), 1) + " s recorded"
                                      : "Press Sample (input) or Resample (what you hear) on the sample window. Max 60 s.",
                     juce::dontSendNotification);
}

void SoundPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    const int w = (r.getWidth() - 3 * 8) / 4;
    auto place = [&] (Card& c, int width) { c.setBounds (r.removeFromLeft (width)); r.removeFromLeft (8); return c.content(); };
    {
        auto a = place (cFx, (int) (w * 1.5));
        layoutRow (a.removeFromTop (26), { { &fOff, 0 }, { &fLp, 0 }, { &fBp, 0 }, { &fHp, 0 } }, 4); a.removeFromTop (6);
        cut.setBounds (a.removeFromTop (22)); a.removeFromTop (4);
        res.setBounds (a.removeFromTop (22)); a.removeFromTop (6);
        layoutRow (a.removeFromTop (26), { { &bLofi, 96 }, { &lBits, 40 }, { &bits, 0 }, { &lRate, 40 }, { &rate, 0 } }, 4); a.removeFromTop (6);
        layoutRow (a.removeFromTop (26), { { &lRetro, 90 }, { &retro, 0 }, { &lMg, 70 }, { &mg, 56 }, { &lLg, 70 }, { &lg, 56 } }, 4); a.removeFromTop (2);
        retroDesc.setBounds (a.removeFromTop (30)); a.removeFromTop (2);
        padVol.setBounds (a.removeFromTop (22)); a.removeFromTop (6);
        layoutRow (a.removeFromTop (26), { { &bRev, 0 }, { &bNorm, 0 }, { &bAutoNorm, 0 } }, 4);
    }
    {
        auto a = place (cLa, (int) (w * 0.95));
        layoutRow (a.removeFromTop (26), { { &laOn, 50 }, { &laComp, 0 }, { &laLim, 0 } }, 4); a.removeFromTop (6);
        auto vrow = a.removeFromTop (juce::jmin (110, a.getHeight() - 60));
        vu.setBounds (vrow.withSizeKeepingCentre (juce::jmin (vrow.getWidth(), 190), vrow.getHeight()));
        a.removeFromTop (4);
        laPr.setBounds (a.removeFromTop (22)); a.removeFromTop (4);
        laGain.setBounds (a.removeFromTop (22)); a.removeFromTop (4);
        laGr.setBounds (a.removeFromTop (20));
    }
    {
        auto a = place (cSmp, (int) (w * 0.75));
        layoutRow (a.removeFromTop (26), { { &sThr, 0 }, { &sCount, 0 } }, 4); a.removeFromTop (6);
        thrDb.setBounds (a.removeFromTop (22)); a.removeFromTop (6);
        smpMeter.setBounds (a.removeFromTop (34)); a.removeFromTop (6);
        smpInfo.setBounds (a);
    }
    {
        auto a = cStr.getLocalBounds();
        cStr.setBounds (r);
        a = cStr.content();
        strPct.setBounds (a.removeFromTop (22)); a.removeFromTop (6);
        smpBpm.setBounds (a.removeFromTop (22)); a.removeFromTop (6);
        layoutRow (a.removeFromTop (26), { { &tKeep, 0 }, { &tMatch, 0 } }, 4); a.removeFromTop (6);
        layoutRow (a.removeFromTop (26), { { &tApply, 0 }, { &tHalf, 0 }, { &tDouble, 0 } }, 4); a.removeFromTop (6);
        strInfo.setBounds (a);
    }
}

// ================================================================== EffectsPanel
EffectsPanel::EffectsPanel (Controller& c, Dialogs& d) : Panel (c, d)
{
    auto P01 = [] (double v) { return pct (v); };
    struct Def { const char* name; bool FxParams::* on; std::vector<std::tuple<const char*, float FxParams::*, double, double, double, std::function<juce::String (double)>>> ctl; };
    const std::vector<Def> defs = {
        { "Tape saturation", &FxParams::satOn, { { "Drive", &FxParams::satDrive, 0, 1, .01, P01 }, { "Mix", &FxParams::satMix, 0, 1, .01, P01 } } },
        { "Vinyl sim", &FxParams::vinOn, { { "Crackle", &FxParams::vinCrackle, 0, 1, .01, P01 }, { "Hiss", &FxParams::vinHiss, 0, 1, .01, P01 },
                                           { "Wow", &FxParams::vinWow, 0, 1, .01, P01 }, { "Age", &FxParams::vinTone, 0, 1, .01, P01 } } },
        { "Chorus", &FxParams::choOn, { { "Rate", &FxParams::choRate, .1, 5, .01, [] (double v) { return juce::String (v, 2) + " Hz"; } },
                                        { "Depth", &FxParams::choDepth, 0, 1, .01, P01 }, { "Mix", &FxParams::choMix, 0, 1, .01, P01 } } },
        { "Flanger", &FxParams::flaOn, { { "Rate", &FxParams::flaRate, .05, 3, .01, [] (double v) { return juce::String (v, 2) + " Hz"; } },
                                         { "Depth", &FxParams::flaDepth, 0, 1, .01, P01 }, { "Feedback", &FxParams::flaFb, 0, .95, .01, P01 }, { "Mix", &FxParams::flaMix, 0, 1, .01, P01 } } },
        { "Scatter", &FxParams::scaOn, { { "Rate", &FxParams::scaDiv, 1, 4, 1, [] (double v) { static const char* R[4] = { "1/4", "1/8", "1/16", "1/32" }; return juce::String (R[juce::jlimit (0, 3, (int) v - 1)]); } },
                                         { "Scatter", &FxParams::scaAmt, 0, 1, .01, P01 }, { "Mix", &FxParams::scaMix, 0, 1, .01, P01 } } },
        { "Delay", &FxParams::delOn, { { "Time", &FxParams::delTime, 1, 5, 1, [] (double v) { static const char* T[5] = { "1/4", "1/8 dot", "1/8", "1/16 dot", "1/16" }; return juce::String (T[juce::jlimit (0, 4, (int) v - 1)]); } },
                                       { "Feedback", &FxParams::delFb, 0, .95, .01, P01 }, { "Tone", &FxParams::delTone, 0, 1, .01, P01 }, { "Mix", &FxParams::delMix, 0, 1, .01, P01 } } },
        { "Echo", &FxParams::ecoOn, { { "Time", &FxParams::ecoTime, 60, 1200, 1, [] (double v) { return juce::String ((int) v) + " ms"; } },
                                      { "Repeats", &FxParams::ecoFb, 0, .93, .01, P01 }, { "Tone", &FxParams::ecoTone, 0, 1, .01, P01 },
                                      { "Wobble", &FxParams::ecoWob, 0, 1, .01, P01 }, { "Mix", &FxParams::ecoMix, 0, 1, .01, P01 } } },
        { "Reverb", &FxParams::revOn, { { "Size", &FxParams::revSize, 0, 1, .01, P01 }, { "Damping", &FxParams::revDamp, 0, 1, .01, P01 }, { "Mix", &FxParams::revMix, 0, 1, .01, P01 } } } };
    for (auto& def : defs)
    {
        Module m;
        m.on = def.on;
        m.card = std::make_unique<Card> (def.name);
        m.toggle = std::make_unique<Btn> ("Off");
        auto onPtr = def.on;
        m.toggle->onClick = [this, onPtr, name = juce::String (def.name)]
        {
            ctl.editFx ([onPtr] (FxParams& f) { f.*onPtr = ! (f.*onPtr); }, "fxon");
            ctl.status (name.toUpperCase() + ((ctl.selPad().fx.*onPtr) ? " ON " : " OFF ") + padLabel (ctl.P.s.sel));
        };
        addAndMakeVisible (*m.card);
        addAndMakeVisible (*m.toggle);
        for (auto& t : def.ctl)
        {
            Ctl cc;
            cc.key = std::get<0> (t); cc.m = std::get<1> (t);
            cc.s = std::make_unique<LSlider> (std::get<0> (t), std::get<2> (t), std::get<3> (t), std::get<4> (t), std::get<5> (t));
            cc.s->labelWidth = 70; cc.s->valueWidth = 60;
            auto mp = cc.m; auto key = cc.key;
            cc.s->onChange = [this, mp, key] (double v) { ctl.editFx ([mp, v] (FxParams& f) { f.*mp = (float) v; }, "fx" + key); };
            addAndMakeVisible (*cc.s);
            m.ctls.push_back (std::move (cc));
        }
        mods.push_back (std::move (m));
    }
    hint.setFont (mono (11.0f)); hint.setColour (juce::Label::textColourId, C::mute);
    hint.setText ("Effects act on the selected pad. Chain order: saturation > vinyl > chorus > flanger > scatter > delay > echo > reverb. Delay and scatter follow each pattern's tempo.", juce::dontSendNotification);
    addAndMakeVisible (hint);
}

void EffectsPanel::refresh()
{
    const auto& fx = ctl.selPad().fx;
    for (auto& m : mods)
    {
        const bool on = fx.*(m.on);
        m.toggle->setOn (on); m.toggle->setButtonText (on ? "On" : "Off");
        for (auto& c : m.ctls) { c.s->set (fx.*(c.m)); c.s->setAlpha (on ? 1.0f : 0.55f); }
    }
}

void EffectsPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    hint.setBounds (r.removeFromTop (18)); r.removeFromTop (4);
    const int cols = 4, rowsN = 2;
    const int cw = (r.getWidth() - (cols - 1) * 8) / cols, ch = (r.getHeight() - (rowsN - 1) * 8) / rowsN;
    for (int i = 0; i < (int) mods.size(); ++i)
    {
        auto& m = mods[(size_t) i];
        const juce::Rectangle<int> cr (r.getX() + (i % cols) * (cw + 8), r.getY() + (i / cols) * (ch + 8), cw, ch);
        m.card->setBounds (cr);
        auto a = m.card->content();
        m.toggle->setBounds (cr.getRight() - 64, cr.getY() + 6, 54, 22);
        const int rh = juce::jlimit (16, 22, (a.getHeight() - 2 * ((int) m.ctls.size() - 1)) / juce::jmax (1, (int) m.ctls.size()));
        for (auto& c : m.ctls) { c.s->setBounds (a.removeFromTop (rh)); a.removeFromTop (2); }
    }
}

// ================================================================== LayersPanel
LayersPanel::Row::Row (Controller& c, int layerIndex) : ctl (c), li (layerIndex)
{
    name.setFont (mono (12.0f, true)); name.setColour (juce::Label::textColourId, li < 0 ? C::accent : C::fg);
    ft.addItem ("No filter", 1); ft.addItem ("Lo-pass", 2); ft.addItem ("Band-pass", 3); ft.addItem ("Hi-pass", 4);
    rm.addItem ("Retro: pad", 1); rm.addItem ("Retro: off", 2);
    for (int i = 1; i < NRETRO; ++i) rm.addItem (juce::String ("Retro: ") + retroName (i), i + 2);
    noFocus (ft); noFocus (rm);
    vol.labelWidth = pitch.labelWidth = fc.labelWidth = pan.labelWidth = 48;
    vol.valueWidth = pitch.valueWidth = fc.valueWidth = pan.valueWidth = 62;
    fc.format = [] (double v) { return hzText (cutFromPos (v)); };
    vol.onChange = [this] (double v) { ctl.editSlot (li, [v] (SlotSettings& s) { s.vol = (float) v; }, "lvol"); };
    pitch.onChange = [this] (double v) { ctl.editSlot (li, [v] (SlotSettings& s) { s.pitch = (float) v; }, "lpitch"); };
    fc.onChange = [this] (double v) { ctl.editSlot (li, [v] (SlotSettings& s) { s.fc = (float) cutFromPos (v); }, "lfc"); };
    pan.onChange = [this] (double v) { ctl.editSlot (li, [v] (SlotSettings& s) { s.pan = (float) v; }, "lpan"); };
    ft.onChange = [this] { const int v = ft.getSelectedId() - 1; ctl.editSlot (li, [v] (SlotSettings& s) { s.ft = v; }, "lft"); };
    rm.onChange = [this] { const int v = rm.getSelectedId() - 2; ctl.editSlot (li, [v] (SlotSettings& s) { s.rm = v; }, "lrm"); };
    swap.onClick = [this] { ctl.swapLayerWithMain (li); };
    del.onClick = [this] { ctl.removeLayer (li); };
    for (auto* x : std::initializer_list<juce::Component*> { &name, &vol, &pitch, &fc, &pan, &ft, &rm, &swap, &del }) addAndMakeVisible (x);
    swap.setVisible (li >= 0); del.setVisible (li >= 0);
    update();
}

void LayersPanel::Row::update()
{
    const auto& p = ctl.selPad();
    if (li >= (int) p.layers.size()) return;
    const SlotSettings& s = li < 0 ? p.m : p.layers[(size_t) li].s;
    name.setText (li < 0 ? "MAIN - " + p.name : juce::String (li + 1) + " - " + p.layers[(size_t) li].name, juce::dontSendNotification);
    vol.set (s.vol); pitch.set (s.pitch); fc.set (posFromCut (s.fc)); pan.set (s.pan);
    ft.setSelectedId (s.ft + 1, juce::dontSendNotification);
    rm.setSelectedId (s.rm + 2, juce::dontSendNotification);
    fc.setEnabled (s.ft != FT_OFF);
}

void LayersPanel::Row::paint (juce::Graphics& g)
{
    g.setColour (C::plate2); g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
}

void LayersPanel::Row::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto top = r.removeFromTop (22);
    layoutRow (top, { { &name, 0 }, { &ft, 104 }, { &rm, 140 }, { &swap, 52 }, { &del, 70 } }, 4);
    r.removeFromTop (2);
    layoutRow (r.removeFromTop (22), { { &vol, 0 }, { &pitch, 0 }, { &fc, 0 }, { &pan, 0 } }, 8);
}

LayersPanel::LayersPanel (Controller& c, Dialogs& d) : Panel (c, d)
{
    show (cLayers, cStems, bAdd);
    bAdd.onClick = [this] { dlg.openAudio (true, [this] (juce::Array<juce::File> f) { ctl.addLayers (f); }); };
    bFromPad.onClick = [this]
    {
        juce::PopupMenu m;
        for (int i = 0; i < NPADS; ++i)
            if (i != ctl.P.s.sel && ctl.P.pads[(size_t) i].buf) m.addItem (i + 1, padLabel (i) + " - " + ctl.P.pads[(size_t) i].name);
        if (m.getNumItems() == 0) { ctl.status ("NO OTHER PAD HAS A SAMPLE YET"); return; }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bFromPad), [this] (int r) { if (r > 0) ctl.layerFromPad (r - 1); });
    };
    addAndMakeVisible (bFromPad);
    caption (lSlot, "Sample");
    stemInfo.setFont (mono (11.0f)); stemInfo.setColour (juce::Label::textColourId, C::mute);
    stemInfo.setJustificationType (juce::Justification::topLeft);
    stemSlot.onChange = [this] { ctl.ui.stemSel = stemSlot.getSelectedId() - 2; ctl.sendChangeMessage(); };
    noFocus (stemSlot);
    bSep.onClick = [this] { ctl.runSeparate(); };
    bApply.onClick = [this] { ctl.applyStems(); };
    bToPads.onClick = [this] { ctl.stemsToPads(); };
    bReset.onClick = [this] { ctl.setStem (-1, 1); };
    show (lSlot, stemSlot, stemInfo, bSep, bApply, bToPads, bReset);
    for (int k = 0; k < 4; ++k)
    {
        stemS[(size_t) k] = std::make_unique<LSlider> (dsp::stemName (k), 0, 1, 0.01, [] (double v) { return pct (v); });
        stemS[(size_t) k]->labelWidth = 100;
        stemS[(size_t) k]->onChange = [this, k] (double v) { ctl.setStem (k, (float) v); };
        solo[(size_t) k].setButtonText ("Solo");
        solo[(size_t) k].onClick = [this, k] { ctl.soloStem (k); };
        addAndMakeVisible (*stemS[(size_t) k]); addAndMakeVisible (solo[(size_t) k]);
    }
}

void LayersPanel::refresh()
{
    const auto& p = ctl.selPad();
    const int count = (int) p.layers.size() + (p.buf ? 1 : 0);
    if (builtFor != ctl.P.s.sel || builtCount != count)
    {
        rows.clear();
        if (p.buf)
        {
            addAndMakeVisible (rows.add (new Row (ctl, -1)));
            for (int i = 0; i < (int) p.layers.size(); ++i) addAndMakeVisible (rows.add (new Row (ctl, i)));
        }
        builtFor = ctl.P.s.sel; builtCount = count;
        resized();
    }
    for (auto* r : rows) r->update();
    cLayers.title = "Layers - " + padLabel (ctl.P.s.sel) + " (" + juce::String ((int) p.layers.size()) + " of " + juce::String (MAXLAY) + ", all play together)";
    cLayers.repaint();

    stemSlot.clear (juce::dontSendNotification);
    if (p.buf) stemSlot.addItem ((p.name.isNotEmpty() ? p.name : juce::String ("Main sample")) + " (main)", 1);
    for (int i = 0; i < (int) p.layers.size(); ++i) stemSlot.addItem (p.layers[(size_t) i].name.isNotEmpty() ? p.layers[(size_t) i].name : "Layer " + juce::String (i + 1), i + 2);
    if (ctl.ui.stemSel >= (int) p.layers.size()) ctl.ui.stemSel = -1;
    stemSlot.setSelectedId (ctl.ui.stemSel + 2, juce::dontSendNotification);
    const bool ready = ctl.stemsReady();
    bSep.setEnabled (p.buf != nullptr && ! ready);
    bSep.setButtonText (ready ? "Separated" : "Separate stems");
    const StemMix& mix = ctl.ui.stemSel < 0 ? p.m.st : p.layers[(size_t) ctl.ui.stemSel].s.st;
    for (int k = 0; k < 4; ++k)
    {
        stemS[(size_t) k]->set (mix.lv[(size_t) k]);
        stemS[(size_t) k]->setEnabled (ready); solo[(size_t) k].setEnabled (ready);
        bool isSolo = true;
        for (int o = 0; o < 4; ++o) isSolo &= mix.lv[(size_t) o] == (o == k ? 1.0f : 0.0f);
        solo[(size_t) k].setOn (ready && isSolo);
    }
    bApply.setEnabled (ready); bToPads.setEnabled (ready); bReset.setEnabled (ready);
    stemInfo.setText ("Splits the sample into vocals, bass, instruments and drums with spectral masks (harmonic/percussive + stereo centre). "
                      "The four parts add back to the original. Mix them here (you hear it on the pad), bake the mix into the sample, or send each stem to its own pad.",
                      juce::dontSendNotification);
}

void LayersPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto left = r.removeFromLeft ((int) (r.getWidth() * 0.6));
    r.removeFromLeft (8);
    cLayers.setBounds (left);
    auto a = cLayers.content();
    bAdd.setBounds (left.getRight() - 110, left.getY() + 6, 100, 22);
    bFromPad.setBounds (left.getRight() - 220, left.getY() + 6, 104, 22);
    for (auto* row : rows) { row->setBounds (a.removeFromTop (54)); a.removeFromTop (4); }
    cStems.setBounds (r);
    auto s = cStems.content();
    layoutRow (s.removeFromTop (26), { { &lSlot, 64 }, { &stemSlot, 0 }, { &bSep, 130 } }, 6); s.removeFromTop (8);
    for (int k = 0; k < 4; ++k)
    {
        layoutRow (s.removeFromTop (24), { { stemS[(size_t) k].get(), 0 }, { &solo[(size_t) k], 54 } }, 6);
        s.removeFromTop (4);
    }
    s.removeFromTop (4);
    layoutRow (s.removeFromTop (26), { { &bApply, 0 }, { &bToPads, 0 }, { &bReset, 0 } }, 6); s.removeFromTop (8);
    stemInfo.setBounds (s);
}

// ================================================================== SongPanel
SongPanel::SongPanel (Controller& c, Dialogs& d) : Panel (c, d)
{
    show (cSong, cExport, cLib);
    caption (lName, "Song name");
    songName.setFont (mono (13.0f));
    songName.onTextChange = [this] { ctl.P.name = songName.getText(); };
    songName.onReturnKey = [this] { songName.giveAwayKeyboardFocus(); };
    songName.onEscapeKey = [this] { songName.giveAwayKeyboardFocus(); };
    bNew.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "New song", "Start a new song? (Ctrl+Z brings this one back)", "New song", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) ctl.newSong(); }));
    };
    bLoad.onClick = [this] { dlg.openFile ("Load CONTRXL 25 song", "*.zip", [this] (juce::File f) { ctl.loadSong (f); }); };
    bSave.onClick = [this] { dlg.saveFile ("Save song", io::safeName (ctl.P.name) + ".zip", "*.zip", [this] (juce::File f) { ctl.saveSong (f); }); };
    caption (lBits, "Bit depth"); caption (lBars, "Bars (no song)");
    bitsBox.addItem ("16-bit", 16); bitsBox.addItem ("24-bit", 24); bitsBox.addItem ("32-bit float", 32);
    bitsBox.onChange = [this] { ctl.ui.exportBits = bitsBox.getSelectedId(); };
    for (int b : { 1, 2, 4, 8, 16 }) barsBox.addItem ("x" + juce::String (b), b);
    barsBox.setTooltip ("How many times the current pattern is rendered when song mode is off");
    barsBox.onChange = [this] { ctl.ui.exportBars = barsBox.getSelectedId(); };
    noFocus (bitsBox); noFocus (barsBox);
    bBake.onClick = [this] { ctl.ui.bakePitch = ! ctl.ui.bakePitch; ctl.sendChangeMessage(); };
    auto plan = [this]
    {
        const auto p = ctl.buildPlan();
        return juce::String (ctl.P.bpmOf (p[0]), 0) + "bpm" + (io::planTempos (ctl.P, p).size() > 1 ? "_tempo-map" : "");
    };
    bSongWav.onClick = [this, plan] { dlg.saveFile ("Export song", io::safeName (ctl.P.name) + "_" + plan() + ".wav", "*.wav", [this] (juce::File f) { ctl.exportSongWav (f); }); };
    bStems.onClick = [this] { dlg.chooseFolder ("Choose a folder for the stems", [this] (juce::File f) { ctl.exportStems (f.getChildFile (io::safeName (ctl.P.name) + "_stems")); }); };
    bTmap.onClick = [this] { dlg.saveFile ("Export tempo map", io::safeName (ctl.P.name) + "_tempo_map.mid", "*.mid", [this] (juce::File f) { ctl.exportTempoMap (f); }); };
    bChop.onClick = [this]
    {
        auto& p = ctl.selPad();
        if (! p.buf) { ctl.exportStatus = "Select a pad with a sample first."; refresh(); return; }
        dlg.saveFile ("Export chop", padLabel (ctl.P.s.sel) + "_" + io::safeName (p.name) + (p.sel >= 0 && ! p.markers.empty() ? "_chop" + juce::String (p.sel + 1).paddedLeft ('0', 2) : juce::String()) + ".wav",
                      "*.wav", [this] (juce::File f) { ctl.exportChop (f); });
    };
    bChops.onClick = [this] { dlg.chooseFolder ("Choose a folder for the chops", [this] (juce::File f) { ctl.exportAllChops (f); }); };
    status.setFont (mono (11.5f)); status.setColour (juce::Label::textColourId, C::ok);
    status.setJustificationType (juce::Justification::topLeft);
    show (lName, songName, bNew, bLoad, bSave, lBits, lBars, bitsBox, barsBox, bBake, bSongWav, bStems, bTmap, bChop, bChops, status);

    caption (lCrate, "Crate");
    crateBox.onChange = [this]
    {
        const int id = crateBox.getSelectedId();
        ctl.ui.libCrate = id >= 2 && id - 2 < cratePaths.size() ? cratePaths[id - 2] : juce::String();
        refresh();
    };
    crateBox.setTooltip ("Crates are the folders of your library; sub-folders are sub-crates. A crate shows its sub-crates' samples too");
    noFocus (crateBox);
    libList.setRowHeight (24);
    libList.setColour (juce::ListBox::outlineColourId, C::line);
    libList.setOutlineThickness (1);
    bLibToPad.onClick = [this] { const int r = libList.getSelectedRow(); if (r >= 0 && r < (int) libRows.size()) ctl.libToPad (libRows[(size_t) r], ctl.P.s.sel); };
    bLibAdd.onClick = [this] { ctl.addToLibrary (currentCrate()); };
    bLibFolder.onClick = [this]
    {
        auto d = currentCrate().isEmpty() ? ctl.library.getRoot() : ctl.library.getRoot().getChildFile (currentCrate());
        d.createDirectory();
       #if CX_MOBILE
        ctl.status ("OPEN THE FILES APP > ON MY IPAD > CONTRXL 25 > LIBRARY" + (currentCrate().isEmpty() ? juce::String() : " > " + currentCrate().toUpperCase().replace ("/", " > ")));
       #else
        d.startAsProcess();
       #endif
    };
    bCrate.onClick = [this]
    {
        const auto parent = currentCrate();
        auto* w = new juce::AlertWindow ("New crate", parent.isEmpty() ? juce::String ("Name of the new crate:") : "Name of the new crate inside \"" + parent + "\":",
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", "");
        w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, parent] (int r)
        {
            const auto n = w->getTextEditorContents ("name").trim();
            if (r != 1 || n.isEmpty()) return;
            const auto path = parent.isEmpty() ? n : parent + "/" + n;
            ctl.libAddCrate (path);
            ctl.ui.libCrate = path;
            refresh();
        }), true);
    };
    bLibRefresh.onClick = [this] { ctl.libRefresh (false); };
    bLibSave.onClick = [this] { ctl.libSave(); };
    bPadLib.onClick = [this] { ctl.addToLibrary (currentCrate()); };
    search.setTextToShowWhenEmpty ("Search the library...", C::mute);
    search.setFont (mono (12.0f));
    search.onTextChange = [this] { refresh(); };
    search.onReturnKey = [this] { search.giveAwayKeyboardFocus(); };
    search.onEscapeKey = [this] { search.setText ({}); search.giveAwayKeyboardFocus(); };
    sortBox.addItem ("Newest", 1); sortBox.addItem ("Name", 2); sortBox.addItem ("Length", 3); sortBox.addItem ("Folder", 4);
    sortBox.setSelectedId (4, juce::dontSendNotification);
    sortBox.setTooltip ("Sort the library");
    sortBox.onChange = [this] { refresh(); };
    noFocus (sortBox);
    libList.setMultipleSelectionEnabled (true);
    bLibPlay.onClick = [this] { const auto s = selectedLib(); if (! s.empty()) ctl.libPreview (s[0]); };
    bLibPads.onClick = [this] { const auto s = selectedLib(); if (! s.empty()) ctl.libToPads (s); };
    bLibChops.onClick = [this] { ctl.addChopsToLibrary (currentCrate()); };
    bLibMore.onClick = [this] { moreMenu(); };
    bLibDel.onClick = [this]
    {
        auto s = selectedLib();
        if (s.empty()) return;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Remove from the library",
            "Remove " + juce::String ((int) s.size()) + " sample" + (s.size() > 1 ? "s" : "") + "? The files go to the Recycle Bin.", "Remove", "Cancel", this,
            juce::ModalCallbackFunction::create ([this, s] (int r) mutable
            {
                if (r != 1) return;
                std::sort (s.rbegin(), s.rend());
                for (int i : s) ctl.libRemove (i);
            }));
    };
    bBounce.onClick = [this] { ctl.bounceSong (false); };
    bBounceLib.onClick = [this] { ctl.bounceSong (true); };
    show (bLibRefresh, bLibSave, bPadLib);
    show (lCrate, crateBox, libList, bLibToPad, bLibDel, bLibAdd, bLibFolder, bCrate, search, sortBox, bLibPlay, bLibPads, bLibChops, bLibMore, bBounce, bBounceLib);
}

std::vector<int> SongPanel::selectedLib() const
{
    std::vector<int> out;
    const auto rows = libList.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i] >= 0 && rows[i] < (int) libRows.size()) out.push_back (libRows[(size_t) rows[i]]);
    return out;
}

void SongPanel::moreMenu()
{
    const auto crate = currentCrate();
    juce::PopupMenu m;
    m.addItem (1, "Import audio files..." + (crate.isNotEmpty() ? " (into " + crate + ")" : juce::String()));
    m.addItem (2, juce::String (CX_MOBILE ? "Import several samples..." : "Import a folder (sub-folders become sub-crates)...") + (crate.isNotEmpty() ? " (into " + crate + ")" : juce::String()));
    m.addSeparator();
    m.addItem (3, "Rename crate...", crate.isNotEmpty());
    m.addItem (4, "Delete crate (its samples move up one level)", crate.isNotEmpty());
    m.addItem (5, "Save crate / selection as .wav files...");
    m.addSeparator();
    m.addItem (6, "Back up the library (.zip)...");
    m.addItem (7, "Restore / import a library or song .zip...");
    m.addSeparator();
   #if CX_MOBILE
    m.addItem (8, "Library folder: Files app > On My iPad > CONTRXL 25 > Library");
    m.addItem (9, "Use another folder as the library...", false);
   #else
    m.addItem (8, "Library folder: " + ctl.library.getRoot().getFullPathName());
    m.addItem (9, "Use another folder as the library...");
   #endif
    m.addItem (10, "Back to the default library folder", ctl.library.getRoot() != Library::defaultRoot());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bLibMore), [this, crate] (int r)
    {
        if (r == 1) dlg.openAudio (true, [this, crate] (juce::Array<juce::File> f) { ctl.libImport (f, crate); });
        else if (r == 2)
        {
           #if CX_MOBILE   // iPad: pick the files (select all in a folder)
            dlg.openAudio (true, [this, crate] (juce::Array<juce::File> f) { ctl.libImport (f, crate); });
           #else
            dlg.chooseFolder ("Import a folder of samples", [this, crate] (juce::File f) { ctl.libImport ({ f }, crate); });
           #endif
        }
        else if (r == 3)
        {
            const auto last = Library::crateName (crate);
            auto* w = new juce::AlertWindow ("Rename crate", "New name for \"" + crate + "\":", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", last);
            w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, crate, last] (int res)
            {
                const auto n = w->getTextEditorContents ("name").trim();
                if (res != 1 || n.isEmpty() || n == last) return;
                ctl.libRenameCrate (crate, n);
                ctl.ui.libCrate = crate.containsChar ('/') ? crate.upToLastOccurrenceOf ("/", true, false) + n : n;
                refresh();
            }), true);
        }
        else if (r == 4)
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete crate",
                "Delete the crate \"" + crate + "\"? Its samples and sub-crates move up one level - nothing is thrown away.",
                "Delete", "Cancel", this, juce::ModalCallbackFunction::create ([this, crate] (int res)
                {
                    if (res != 1) return;
                    ctl.libDeleteCrate (crate);
                    ctl.ui.libCrate = crate.containsChar ('/') ? crate.upToLastOccurrenceOf ("/", false, false) : juce::String();
                    refresh();
                }));
        else if (r == 5)
        {
            auto sel = selectedLib();
            if (sel.size() < 2) sel = libRows;   // nothing (or one) selected: everything shown
            dlg.chooseFolder ("Save the samples into a folder", [this, sel] (juce::File f) { ctl.libSaveCrate (sel, f); });
        }
        else if (r == 6) dlg.saveFile ("Back up the library", "CONTRXL25_library_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + ".zip", "*.zip", [this] (juce::File f) { ctl.libBackup (f); });
        else if (r == 7) dlg.openFile ("Restore a library backup (or take the samples out of a song)", "*.zip", [this] (juce::File f) { ctl.libRestore (f); });
        else if (r == 8)
        {
           #if CX_MOBILE
            ctl.status ("OPEN THE FILES APP > ON MY IPAD > CONTRXL 25 > LIBRARY - COPY SAMPLES IN THERE, THEN PRESS REFRESH");
           #else
            ctl.library.getRoot().startAsProcess();
           #endif
        }
        else if (r == 9) dlg.chooseFolder ("Choose the folder to use as the library (its sub-folders become crates)", [this] (juce::File f)
        {
            ctl.ui.libCrate = {};
            ctl.libSetRoot (f);
        });
        else if (r == 10) { ctl.ui.libCrate = {}; ctl.libSetRoot (Library::defaultRoot()); }
    });
}

void SongPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) libRows.size() || libRows[(size_t) row] >= (int) ctl.library.items.size()) return;
    const auto& it = ctl.library.items[(size_t) libRows[(size_t) row]];
    g.setColour (selected ? C::accent : (row % 2 ? C::plate2 : C::well)); g.fillRect (0, 0, w, h);
    g.setColour (selected ? juce::Colours::black : (it.dirty ? C::accent : C::fg)); g.setFont (mono (12.0f));
    const int right = juce::jmin (w / 2, 280);
    g.drawText ((it.dirty ? "* " : "") + it.name, 8, 0, w - right - 12, h, juce::Justification::centredLeft);
    g.setColour (selected ? juce::Colours::black : C::mute); g.setFont (mono (10.5f));
    const auto len = it.buf ? juce::String (it.buf->duration(), 2) + " s"
                   : it.bytes > 0 ? (it.bytes > 1000000 ? juce::String (it.bytes / 1048576.0, 1) + " MB" : juce::String ((int) (it.bytes / 1024)) + " KB") : juce::String();
    // show the crate path relative to the crate you are looking at
    auto where = it.crate;
    const auto cur = currentCrate();
    if (cur.isNotEmpty() && where.startsWith (cur)) where = where.substring (cur.length()).trimCharactersAtStart ("/");
    g.drawFittedText (where + (where.isNotEmpty() ? "   " : "") + len, w - right, 0, right - 8, h, juce::Justification::centredRight, 1, 0.75f);
}

void SongPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) libRows.size()) ctl.libToPad (libRows[(size_t) row], ctl.P.s.sel);
}

juce::var SongPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty() || rows[0] >= (int) libRows.size()) return {};
    return "lib:" + juce::String (ctl.library.items[(size_t) libRows[(size_t) rows[0]]].id);
}

bool SongPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) { const juce::File x (f); if (x.isDirectory() || Library::isAudioFile (x)) return true; }
    return false;
}

void SongPanel::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> fs;
    for (auto& f : files) fs.add (juce::File (f));
    ctl.libImport (fs, currentCrate());
}

void SongPanel::refresh()
{
    if (! songName.hasKeyboardFocus (true)) songName.setText (ctl.P.name, juce::dontSendNotification);
    bitsBox.setSelectedId (ctl.ui.exportBits, juce::dontSendNotification);
    barsBox.setSelectedId (ctl.ui.exportBars, juce::dontSendNotification);
    barsBox.setEnabled (! (ctl.P.s.song && ! ctl.P.chain.empty()));
    bBake.setOn (ctl.ui.bakePitch);
    const auto plan = ctl.buildPlan();
    status.setText ((ctl.exportStatus.isNotEmpty() ? ctl.exportStatus + "\n\n" : juce::String())
                    + "Plan: " + (ctl.P.s.song && ! ctl.P.chain.empty() ? "song chain, " : "pattern " + juce::String (ctl.P.s.pat + 1) + ", ")
                    + juce::String ((int) plan.size()) + " pattern(s), " + juce::String ((int) ctl.activeTracks (plan).size()) + " active track(s). "
                    + "Stems are one file per track, all the same length, all starting at bar 1."
                    + (ctl.unsaved ? "\n\nYou have a new recording that is not saved yet." : ""),
                    juce::dontSendNotification);

    // crates: the folder tree, indented
    const auto& lib = ctl.library;
    if (ctl.ui.libCrate.isNotEmpty() && ! lib.crates.contains (ctl.ui.libCrate)) ctl.ui.libCrate = {};
    cratePaths = lib.crates;
    crateBox.clear (juce::dontSendNotification);
    crateBox.addItem ("All crates", 1);
    int sel = 1;
    for (int i = 0; i < cratePaths.size(); ++i)
    {
        crateBox.addItem (crateIndent (cratePaths[i]) + Library::crateName (cratePaths[i]), i + 2);
        if (cratePaths[i] == ctl.ui.libCrate) sel = i + 2;
    }
    crateBox.setSelectedId (sel, juce::dontSendNotification);
    bCrate.setButtonText (ctl.ui.libCrate.isEmpty() ? "+ Crate" : "+ Sub-crate");
    bPadLib.setTooltip ("Add the selected pad's sample to " + (ctl.ui.libCrate.isEmpty() ? juce::String ("the library (no crate)") : "the crate \"" + ctl.ui.libCrate + "\""));
    const int unsaved = lib.unsavedCount();
    bLibSave.setButtonText (unsaved > 0 ? "Save (" + juce::String (unsaved) + ")" : juce::String ("Save"));
    bLibSave.setOn (unsaved > 0);

    libRows.clear();
    const auto q = search.getText().trim();
    const auto cur = ctl.ui.libCrate;
    for (int i = 0; i < (int) lib.items.size(); ++i)
    {
        const auto& it = lib.items[(size_t) i];
        if (! Library::inCrate (it.crate, cur)) continue;
        if (q.isNotEmpty() && ! it.name.containsIgnoreCase (q) && ! it.crate.containsIgnoreCase (q)) continue;
        libRows.push_back (i);
    }
    const int sort = sortBox.getSelectedId();
    const auto& items = lib.items;
    if (sort == 1) std::stable_sort (libRows.begin(), libRows.end(), [&items] (int a, int b)
    {
        auto t = [&items] (int i) { const auto& x = items[(size_t) i]; return x.dirty ? juce::Time::getCurrentTime() : x.file.getLastModificationTime(); };
        return t (a) > t (b);
    });
    else if (sort == 2) std::stable_sort (libRows.begin(), libRows.end(), [&items] (int a, int b) { return items[(size_t) a].name.compareNatural (items[(size_t) b].name) < 0; });
    else if (sort == 3) std::stable_sort (libRows.begin(), libRows.end(), [&items] (int a, int b)
    {
        auto len = [&items] (int i) { const auto& x = items[(size_t) i]; return x.buf ? (double) x.buf->length() * 4.0 : (double) x.bytes; };
        return len (a) < len (b);
    });
    else std::stable_sort (libRows.begin(), libRows.end(), [&items] (int a, int b)
    {
        const int c = items[(size_t) a].crate.compareNatural (items[(size_t) b].crate);
        return c != 0 ? c < 0 : items[(size_t) a].name.compareNatural (items[(size_t) b].name) < 0;
    });
    libList.updateContent(); libList.repaint();
    cLib.title = "Library - " + juce::String ((int) libRows.size()) + (cur.isEmpty() ? " samples" : " in \"" + cur + "\"")
               + (unsaved > 0 ? " - " + juce::String (unsaved) + " not saved yet" : juce::String()) + "  (drop files or folders here)";
    cLib.repaint();
}

void SongPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto col1 = r.removeFromLeft ((int) (r.getWidth() * 0.27)); r.removeFromLeft (8);
    auto col2 = r.removeFromLeft ((int) (r.getWidth() * 0.45)); r.removeFromLeft (8);
    cSong.setBounds (col1);
    {
        auto a = cSong.content();
        lName.setBounds (a.removeFromTop (18));
        songName.setBounds (a.removeFromTop (28)); a.removeFromTop (8);
        layoutRow (a.removeFromTop (28), { { &bNew, 0 }, { &bLoad, 0 }, { &bSave, 0 } }, 4);
    }
    cExport.setBounds (col2);
    {
        auto a = cExport.content();
        layoutRow (a.removeFromTop (26), { { &lBits, 80 }, { &bitsBox, 0 }, { &lBars, 118 }, { &barsBox, 70 } }, 4); a.removeFromTop (6);
        layoutRow (a.removeFromTop (28), { { &bSongWav, 0 }, { &bStems, 0 }, { &bTmap, 0 } }, 4); a.removeFromTop (6);
        layoutRow (a.removeFromTop (28), { { &bChop, 0 }, { &bChops, 0 }, { &bBake, 0 } }, 4); a.removeFromTop (6);
        layoutRow (a.removeFromTop (28), { { &bBounce, 0 }, { &bBounceLib, 0 }, { &bPadLib, 0 } }, 4); a.removeFromTop (8);
        status.setBounds (a);
    }
    cLib.setBounds (r);
    {
        auto a = cLib.content();
        layoutRow (a.removeFromTop (26), { { &lCrate, 50 }, { &crateBox, 0 }, { &bCrate, 92 }, { &bLibMore, 66 } }, 4); a.removeFromTop (4);
        layoutRow (a.removeFromTop (24), { { &search, 0 }, { &sortBox, 84 }, { &bLibRefresh, 70 }, { &bLibSave, 72 } }, 4); a.removeFromTop (4);
        auto btns2 = a.removeFromBottom (26); a.removeFromBottom (4);
        auto btns = a.removeFromBottom (26); a.removeFromBottom (4);
        layoutRow (btns, { { &bLibPlay, 0 }, { &bLibToPad, 0 }, { &bLibPads, 0 }, { &bLibDel, 0 } }, 4);
        layoutRow (btns2, { { &bLibAdd, 0 }, { &bLibChops, 0 }, { &bLibFolder, 0 } }, 4);
        libList.setBounds (a);
    }
}
} // namespace cx::ui
