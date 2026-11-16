#include "MainComponent.h"

namespace cx::ui
{
// ------------------------------------------------------------------ audio host
void AudioHost::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    const double sr = d->getCurrentSampleRate();
    engine.prepare (sr, d->getCurrentBufferSizeSamples());
    engine.setOutputLatency (d->getOutputLatencyInSamples() + d->getCurrentBufferSizeSamples());
    collector.reset (sr);
}

void AudioHost::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                  const juce::AudioIODeviceCallbackContext&)
{
    midiBuf.clear();
    collector.removeNextBlockOfMessages (midiBuf, n);
    engine.process (in, numIn, out, numOut, n, midiBuf);
}

void AudioHost::handleIncomingMidiMessage (juce::MidiInput* src, const juce::MidiMessage& m)
{
    {
        const juce::SpinLock::ScopedLockType l (lock);
        if (src != nullptr && ! m.isActiveSense() && ! m.isMidiClock()) lastSrc = src->getName();
        // "notes from one input": other devices' notes are ignored, EXCEPT while learning and for
        // notes that are linked to a control (e.g. the transport buttons of a second keyboard)
        if (noteFilter.isNotEmpty() && src != nullptr && src->getIdentifier() != noteFilter
            && (m.isNoteOnOrOff() || m.isPitchWheel() || m.isAftertouch() || m.isChannelPressure()))
        {
            const bool linked = m.isNoteOnOrOff() && engine.noteMap[(size_t) (m.getNoteNumber() & 127)].load() != Engine::MAP_NONE;
            if (! engine.learnArmed.load() && ! linked) return;
        }
    }
    collector.addMessageToQueue (m);
}

// ------------------------------------------------------------------ MIDI settings window
class MidiSettings : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    MidiSettings (Controller& c, AudioHost& h, Dialogs& d, juce::AudioDeviceManager& dm, std::function<void()> save)
        : ctl (c), host (h), dlg (d), savePrefs (std::move (save)), devs (dm)
    {
        lInputs.setText ("MIDI inputs (green = listening; click to switch)", juce::dontSendNotification);
        lInputs.setFont (mono (11.5f)); lInputs.setColour (juce::Label::textColourId, C::fg);
        addAndMakeVisible (lInputs);
        title.setText ("MIDI", juce::dontSendNotification);
        title.setFont (mono (16.0f, true)); title.setColour (juce::Label::textColourId, C::accent);
        for (auto* l : { &lLast, &lSrc, &lHelp, &lFilter, &lLinks })
        {
            l->setFont (mono (11.5f)); l->setColour (juce::Label::textColourId, C::fg);
        }
        lFilter.setText ("Notes & pitch bend from", juce::dontSendNotification);
        lLinks.setText ("Learned links (MIDI -> control)", juce::dontSendNotification);
        lHelp.setText ("To link a control: press MIDI Learn (top bar), click a pad, key, pitch knob, button or slider, "
                       "then press or move the control on your controller. A control can have links from several keyboards. "
                       "Nothing under Last MIDI when you press a button? Switch on that keyboard's other port under Audio / MIDI "
                       "devices (Komplete Kontrol keyboards send their transport buttons on the port named ...DAW).",
                       juce::dontSendNotification);
        lHelp.setColour (juce::Label::textColourId, C::mute);
        lHelp.setJustificationType (juce::Justification::topLeft);
        lHelp.setMinimumHorizontalScale (1.0f);
        filter.onChange = [this]
        {
            const int i = filter.getSelectedItemIndex();
            host.setNoteFilter (i <= 0 ? juce::String() : ids[i - 1]);
            savePrefs();
        };
        filter.setWantsKeyboardFocus (false);
        bLearn.onClick = [this] { dlg.toggleMidiLearn(); };
        bLow.onClick = [this] { ctl.engine.lowKeyLearn.store (true); ctl.status ("PRESS THE LOWEST KEY ON YOUR MIDI KEYBOARD"); };
        bResetLow.onClick = [this] { ctl.engine.midiOffset.store (0); ctl.engine.hwOct.store (0); ctl.status ("LOW KEY RESET"); savePrefs(); };
        bFollow.onClick = [this] { ctl.engine.followOct.store (! ctl.engine.followOct.load()); savePrefs(); update(); };
        bTransport.onClick = [this] { ctl.engine.defaultTransportCC.store (! ctl.engine.defaultTransportCC.load()); savePrefs(); update(); };
        bRemove.onClick = [this]
        {
            const int r = list.getSelectedRow();
            if (r >= 0 && r < sigs.size()) ctl.removeMidiLink (sigs[r]);
            update();
        };
        bClear.onClick = [this]
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Clear MIDI links", "Remove every learned MIDI link?", "Clear all", "Cancel", this,
                juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) { ctl.clearMidiLinks(); update(); } }));
        };
        bDevices.onClick = [this] { dlg.showAudioSettings(); };
        bLearn.getProperties().set ("noLearn", true);
        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, C::plate2);
        list.setWantsKeyboardFocus (false);
        lEmpty.setText ("No MIDI links yet. Press MIDI Learn, click a control, then press or move the control on your controller.", juce::dontSendNotification);
        lEmpty.setFont (mono (11.5f)); lEmpty.setColour (juce::Label::textColourId, C::mute);
        lEmpty.setJustificationType (juce::Justification::centred);
        lEmpty.setInterceptsMouseClicks (false, false);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &title, &lLast, &lSrc, &lHelp, &lFilter, &filter, &bLearn, &bLow, &bResetLow, &bFollow,
                                    &bTransport, &lLinks, &list, &bRemove, &bClear, &bDevices, &lEmpty })
            addAndMakeVisible (c);
        fillInputs();
        update();
        startTimerHz (8);
    }

    void update()
    {
        bLearn.setOn (ctl.midiLearn);
        bLearn.setButtonText (ctl.midiLearn ? "Learning... (click to finish)" : "MIDI Learn");
        bFollow.setOn (ctl.engine.followOct.load());
        bTransport.setOn (ctl.engine.defaultTransportCC.load());
        sigs.clear(); rows.clear();
        for (auto& kv : ctl.midiMap)
        {
            sigs.add (kv.first);
            rows.add (Controller::sigName (kv.first).paddedRight (' ', 9) + "->  " + kv.second.label);
        }
        list.updateContent(); list.repaint();
        bRemove.setEnabled (list.getSelectedRow() >= 0);
        bClear.setEnabled (! sigs.isEmpty());
        lEmpty.setVisible (sigs.isEmpty());
    }

    void paint (juce::Graphics& g) override { g.fillAll (C::plate); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        title.setBounds (r.removeFromTop (26));
        lLast.setBounds (r.removeFromTop (20));
        lSrc.setBounds (r.removeFromTop (20));
        r.removeFromTop (6);
        lHelp.setBounds (r.removeFromTop (58));
        r.removeFromTop (6);
        auto row = r.removeFromTop (30);
        lFilter.setBounds (row.removeFromLeft (190)); filter.setBounds (row.removeFromLeft (300));
        r.removeFromTop (8);
        lInputs.setBounds (r.removeFromTop (20));
        for (int i = 0; i < inputBtns.size(); i += 2)
        {
            auto ir = r.removeFromTop (26);
            inputBtns[i]->setBounds (ir.removeFromLeft (ir.getWidth() / 2 - 4));
            if (i + 1 < inputBtns.size()) { ir.removeFromLeft (8); inputBtns[i + 1]->setBounds (ir); }
            r.removeFromTop (4);
        }
        r.removeFromTop (6);
        row = r.removeFromTop (30);
        layoutRow (row, { { &bLearn, 230 }, { &bLow, 130 }, { &bResetLow, 120 }, { nullptr, 0 } }, 8);
        r.removeFromTop (8);
        row = r.removeFromTop (30);
        layoutRow (row, { { &bFollow, 300 }, { &bTransport, 300 }, { nullptr, 0 } }, 8);
        r.removeFromTop (12);
        lLinks.setBounds (r.removeFromTop (22));
        auto bottom = r.removeFromBottom (30);
        layoutRow (bottom, { { &bRemove, 110 }, { &bClear, 90 }, { nullptr, 0 }, { &bDevices, 200 } }, 8);
        r.removeFromBottom (8);
        list.setBounds (r);
        lEmpty.setBounds (r.reduced (10));
    }

private:
    Controller& ctl; AudioHost& host; Dialogs& dlg;
    std::function<void()> savePrefs;
    juce::Label title, lLast, lSrc, lHelp, lFilter, lLinks, lEmpty, lInputs;
    juce::AudioDeviceManager& devs;
    juce::OwnedArray<Btn> inputBtns;
    juce::ComboBox filter;
    juce::StringArray ids, sigs, rows;
    juce::ListBox list { "links" };
    Btn bLearn { "MIDI Learn" },
        bLow { "Set low key", "Press this, then press the lowest key on your MIDI keyboard: it becomes the first key of CONTRXL 25" },
        bResetLow { "Reset low key", "Back to the normal key range" },
        bFollow { "Follow keyboard octave buttons", "ON: when you press your MIDI keyboard's own octave buttons, CONTRXL 25 follows" },
        bTransport { "Built-in transport CCs", "ON: the usual transport CCs work without learning (94/115 play, 95/116 rec, 93/117 stop, 86/113 loop, 87/112 metro, 88/114 tap). "
                                               "Turn OFF if they clash with your controller, and learn your own buttons instead" },
        bRemove { "Remove" }, bClear { "Clear all" }, bDevices { "Audio / MIDI devices...", "Switch MIDI inputs on and off, choose the audio device" };
    juce::String lastShown;

    void fillInputs()
    {
        filter.clear (juce::dontSendNotification);
        ids.clear();
        filter.addItem ("Every MIDI input", 1);
        const auto cur = host.getNoteFilter();
        int sel = 0, n = 1;
        for (auto& d : juce::MidiInput::getAvailableDevices())
        {
            ids.add (d.identifier);
            filter.addItem (d.name, ++n);
            if (d.identifier == cur) sel = n - 1;
        }
        if (cur.isNotEmpty() && sel == 0) { ids.add (cur); filter.addItem ("(unplugged device)", ++n); sel = n - 1; }
        filter.setSelectedItemIndex (sel, juce::dontSendNotification);
        // one on/off button per MIDI input (e.g. a keyboard's normal port and its "DAW" port)
        inputBtns.clear();
        for (auto& d : juce::MidiInput::getAvailableDevices())
        {
            auto* b = inputBtns.add (new Btn (d.name, "Switch this MIDI input on or off"));
            const auto id = d.identifier;
            b->setOnColour (C::ok);
            b->setOn (devs.isMidiInputDeviceEnabled (id));
            b->onClick = [this, id, b] { devs.setMidiInputDeviceEnabled (id, ! devs.isMidiInputDeviceEnabled (id)); b->setOn (devs.isMidiInputDeviceEnabled (id)); };
            addAndMakeVisible (b);
        }
        setSize (680, 640 + ((inputBtns.size() + 1) / 2) * 30);
        resized();
    }

    void timerCallback() override
    {
        const auto txt = "Last MIDI:  " + ctl.lastMidi;
        if (txt != lastShown) { lastShown = txt; lLast.setText (txt, juce::dontSendNotification); }
        lSrc.setText ("From:       " + (host.lastSource().isEmpty() ? juce::String ("-") : host.lastSource())
                      + "      (first key = note " + juce::String ((ctl.P.s.keys == 32 ? 53 : 48) - ctl.engine.midiOffset.load()) + ")", juce::dontSendNotification);
        if (ctl.midiMap.size() != (size_t) sigs.size() || bLearn.getToggleState() != ctl.midiLearn) update();
        static int n = 0;
        if (++n % 16 == 0 && juce::MidiInput::getAvailableDevices().size() + 1 != filter.getNumItems() && ! filter.isPopupActive()) fillInputs();
    }

    int getNumRows() override { return rows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (selected) g.fillAll (C::accent.withAlpha (0.25f));
        g.setColour (C::fg); g.setFont (mono (12.0f));
        g.drawText (rows[row], 8, 0, w - 16, h, juce::Justification::centredLeft);
    }
    void selectedRowsChanged (int) override { bRemove.setEnabled (list.getSelectedRow() >= 0); }
    void deleteKeyPressed (int) override { bRemove.onClick(); }
};

// ------------------------------------------------------------------ main component
static int qwertyCode (int i)
{
    // Z S X D C V G B H N J M Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ = ]
    static const int codes[32] = { 'Z', 'S', 'X', 'D', 'C', 'V', 'G', 'B', 'H', 'N', 'J', 'M', 'Q', '2', 'W', '3',
                                   'E', 'R', '5', 'T', '6', 'Y', '7', 'U', 'I', '9', 'O', '0', 'P', '[',
                                  #if JUCE_WINDOWS
                                   '+',   // the =/+ key (VK_OEM_PLUS)
                                  #else
                                   '=',
                                  #endif
                                   ']' };
    return codes[i];
}

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "CONTRXL25"; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    o.folderName = "CONTRXL25";
    props = std::make_unique<juce::PropertiesFile> (o);
    lastDir = juce::File (props->getValue ("lastDir", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName()));
    ctl.P.s.mvol = (float) props->getDoubleValue ("mvol", 0.85);
    ctl.P.s.keys = props->getIntValue ("keys", 25) == 32 ? 32 : 25;
    ctl.ui.qwerty = props->getBoolValue ("qwerty", true);
    {
        const auto chosen = props->getValue ("libraryRoot");
        ctl.library.setRoot (chosen.isNotEmpty() && juce::File::isAbsolutePath (chosen) ? juce::File (chosen) : Library::defaultRoot());
    }
    ctl.publish();

    // audio + MIDI
    std::unique_ptr<juce::XmlElement> saved (props->getXmlValue ("audioDevice"));
    const auto err = devices.initialise (2, 2, saved.get(), true);
    if (err.isNotEmpty()) ctl.statusText = "AUDIO: " + err;
    devices.addAudioCallback (&host);
    devices.addMidiInputDeviceCallback ({}, &host);
    knownMidi = juce::StringArray::fromTokens (props->getValue ("knownMidi"), "|", "");
    enableNewMidiInputs();
    devices.addChangeListener (this);

    addAndMakeVisible (top);
    addAndMakeVisible (face);
    addAndMakeVisible (deck);
    tabs.setTabBarDepth (34);
    tabs.setOutline (0);
    seqHost.seq = &seq;
    seqHost.addAndMakeVisible (seq);
    seqHost.dock.onClick = [this] { togglePopSequencer(); };
    seq.onPop = [this] { togglePopSequencer(); };
    tabs.addTab ("Sequencer", C::plate, &seqHost, false);
    tabs.addTab ("Mixer", C::plate, &mixer, false);
    tabs.addTab ("Sound", C::plate, &sound, false);
    tabs.addTab ("Effects", C::plate, &effects, false);
    tabs.addTab ("Layers & stems", C::plate, &layers, false);
    tabs.addTab ("Song & export", C::plate, &song, false);
    addAndMakeVisible (tabs);
    tabs.pop.onClick = [this] { togglePopSection(); };
    addAndMakeVisible (overlay);
    uiScale = props->getIntValue ("uiScale", 0);
    // switching tabs must not leave a text box holding the keyboard (QWERTY keys and Space would stop working)
    tabs.onChange = [this] { juce::Component::SafePointer<MainComponent> sp (this); juce::MessageManager::callAsync ([sp] { if (sp) sp->grabKeyboardFocus(); }); };
    addMouseListener (this, true);

    ctl.onKeyLight = [this] (int idx, bool on) { deck.kb.keys.light (idx, 2, on); };
    ctl.onBend = [this] (float v) { deck.kb.pitchWheel.setValue (v); };
    ctl.onMod = [this] (float v) { deck.kb.modWheel.setValue (v); };
    ctl.onMidiActivity = [this] { top.flashMidi(); };
    ctl.onMapFire = [this] (const MidiTarget& t, bool isCC, int v) { handleMapFire (t, isCC, v); };
    ctl.onMidiMapChanged = [this] { saveMidiPrefs(); };

    // VST3 plug-ins
    if (auto known = props->getXmlValue ("knownPlugins")) ctl.plugins.known.recreateFromXml (*known);
    ctl.plugins.known.addChangeListener (this);
    ctl.recentPlugins = juce::StringArray::fromLines (props->getValue ("recentPlugins"));
    ctl.recentPlugins.removeEmptyStrings();
    ctl.onPluginClosing = [this] (PluginNode* n) { editorWins.erase (n); };
    ctl.onOpenEditor = [this] (const SlotId& s) { openPluginEditor (s); };
    ctl.onAutoSampleDialog = [this] (int gi) { showAutoSampler (gi); };
    ctl.onShowPluginList = [this] { showPluginList(); };
    ctl.onShowMixer = [this] { showTab ("Mixer"); };

    // MIDI learn: names for the parts of the window (they make up the stable ids of learned controls)
    for (auto [comp, id, name] : std::initializer_list<std::tuple<juce::Component*, const char*, const char*>> {
            { &top, "top", "Top bar" }, { &face, "face", "Main" }, { &deck, "deck", "Deck" }, { &deck.kb, "kb", "Keyboard" },
            { &seq, "seq", "Sequencer" }, { &mixer, "mix", "Mixer" }, { &sound, "sound", "Sound" }, { &effects, "fx", "Effects" }, { &layers, "layers", "Layers" }, { &song, "song", "Song" } })
    {
        comp->setComponentID (id);
        comp->setName (name);
    }
    ctl.engine.followOct.store (props->getBoolValue ("followOct", true));
    ctl.engine.defaultTransportCC.store (props->getBoolValue ("defTransport", true));
    ctl.engine.midiOffset.store (props->getIntValue ("midiOffset", 0));
    host.setNoteFilter (props->getValue ("noteFilter"));
    ctl.ui.keysVisible = props->getBoolValue ("keysVisible", true);
    ctl.ui.recAllArmed = props->getBoolValue ("recAllArmed", false);
    ctl.midiMapFromVar (juce::JSON::parse (props->getValue ("midiMap", "{}")));
    ctl.pushMidiMap();
    ctl.addChangeListener (this);

    setNoFocusRecursive (*this);
    setWantsKeyboardFocus (true);
    refreshAll();
    startTimerHz (30);
    setSize (1500, 1000);

    // re-open the windows that were popped out last time
    const bool popSeq = props->getBoolValue ("popSeq", false), popKb = props->getBoolValue ("popKb", false), popTabs = props->getBoolValue ("popTabs", false);
    if (popSeq || popKb || popTabs)
    {
        juce::Component::SafePointer<MainComponent> sp (this);
        juce::MessageManager::callAsync ([sp, popSeq, popKb, popTabs]
        {
            if (sp == nullptr) return;
            if (popTabs && ! sp->isSectionPopped()) sp->togglePopSection();
            if (popSeq && ! sp->isSequencerPopped()) sp->togglePopSequencer();
            if (popKb && ! sp->isKeyboardPopped()) sp->togglePopKeyboard();
        });
    }
}

MainComponent::~MainComponent()
{
    stopTimer();
    editorWins.clear();          // plug-in editors go before their plug-ins
    if (pluginListWin) { props->setValue ("win_plugins", pluginListWin->getWindowStateAsString()); pluginListWin.reset(); }
    ctl.plugins.known.removeChangeListener (this);
    props->setValue ("recentPlugins", ctl.recentPlugins.joinIntoString ("\n"));
    props->setValue ("popSeq", seqWin != nullptr);
    props->setValue ("popKb", kbWin != nullptr);
    props->setValue ("popTabs", tabsWin != nullptr);
    if (tabsWin) { props->setValue ("win_tabs", tabsWin->getWindowStateAsString()); tabsWin.reset(); }
    if (seqWin) { props->setValue ("win_seq", seqWin->getWindowStateAsString()); seqWin.reset(); }
    if (kbWin)  { props->setValue ("win_kb", kbWin->getWindowStateAsString()); kbWin.reset(); }
    if (chopWin) { props->setValue ("win_chop", chopWin->getWindowStateAsString()); chopEd = nullptr; chopWin.reset(); }
    saveDeviceState();
    props->setValue ("mvol", ctl.P.s.mvol);
    props->setValue ("keys", ctl.P.s.keys);
    props->setValue ("qwerty", ctl.ui.qwerty);
    props->setValue ("lastDir", lastDir.getFullPathName());
    props->setValue ("keysVisible", ctl.ui.keysVisible);
    props->setValue ("recAllArmed", ctl.ui.recAllArmed);
    if (midiWin) { props->setValue ("win_midi", midiWin->getWindowStateAsString()); midiWin.reset(); }
    learnMain.reset();
    saveMidiPrefs();
    props->saveIfNeeded();
    ctl.library.save();          // samples added in the app are written when the app closes (or with Save library)
    props->setValue ("libraryRoot", ctl.library.getRoot().getFullPathName());
    props->saveIfNeeded();
    devices.removeMidiInputDeviceCallback ({}, &host);
    devices.removeAudioCallback (&host);
    devices.closeAudioDevice();
    ctl.releaseAllPlugins();     // plug-ins go while the plug-in formats still exist
    ctl.removeChangeListener (this);
}

void MainComponent::setNoFocusRecursive (juce::Component& c)
{
    for (auto* ch : c.getChildren())
    {
        if (dynamic_cast<juce::TextEditor*> (ch) == nullptr && dynamic_cast<juce::Label*> (ch) == nullptr)
        {
            ch->setWantsKeyboardFocus (false);
            ch->setMouseClickGrabsKeyboardFocus (false);
        }
        setNoFocusRecursive (*ch);
    }
}

void MainComponent::saveDeviceState()
{
    if (auto xml = devices.createStateXml()) props->setValue ("audioDevice", xml.get());
    props->setValue ("knownMidi", knownMidi.joinIntoString ("|"));
    props->saveIfNeeded();
}

void MainComponent::enableNewMidiInputs()
{
    // the web version listens to every MIDI input; do the same for devices we have never seen before
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (! knownMidi.contains (d.identifier))
        {
            knownMidi.add (d.identifier);
            devices.setMidiInputDeviceEnabled (d.identifier, true);
        }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* src)
{
    if (src == &devices) { saveDeviceState(); return; }
    if (src == &ctl.plugins.known)
    {
        if (auto x = ctl.plugins.known.createXml()) props->setValue ("knownPlugins", x.get());
        props->saveIfNeeded();
        ctl.retryMissingPlugins();
        return;
    }
    refreshAll();
}

void MainComponent::refreshAll()
{
    top.refresh(); face.refresh(); deck.refresh(); seq.refresh();
    sound.refresh(); effects.refresh(); layers.refresh(); song.refresh(); mixer.refresh();
    if (chopEd != nullptr) chopEd->refresh();
    if (minimumHeight() != lastMinH) checkLayout();
    setNoFocusRecursive (*this);
    tabs.pop.setButtonText (tabsWin ? "Dock section" : "Pop out section");
    for (auto* w : { seqWin.get(), kbWin.get(), chopWin.get(), tabsWin.get() }) if (w != nullptr && w->holder != nullptr) setNoFocusRecursive (*w->holder);
}

void MainComponent::timerCallback()
{
    top.tick(); face.tick(); deck.tick(); seq.tick(); sound.tick(); mixer.tick();
    if (chopEd != nullptr) chopEd->tick();
    updateLearnOverlays();
    {
        const int ci = ctl.engine.countInLeft.load(), kind = ctl.engine.countInKind.load();
        if (ci != overlay.count || kind != overlay.kind) { overlay.count = ci; overlay.kind = kind; overlay.repaint(); }
    }
    static int n = 0;
    if (++n % 60 == 0) enableNewMidiInputs();   // MIDI hot-plug, every 2 s
    // the library folder: pick up files you copied in with Explorer (every 4 s while the library is
    // on screen, and when you come back to the app)
    {
        const bool fg = juce::Process::isForegroundProcess();
        if ((fg && ! wasForeground) || (n % 120 == 0 && song.isShowing())) ctl.libRefresh (true);
        wasForeground = fg;
    }
    // keep the sequencer view on the playing pattern in song mode
    if (ctl.engine.playing.load() && ctl.P.s.song && ctl.engine.curPat.load() != ctl.P.s.pat)
    {
        ctl.P.s.pat = ctl.engine.curPat.load(); ctl.ui.page = 0;
        seq.refresh(); face.refresh();
    }
    if (ctl.engine.playing.load())
    {
        const int step = ctl.engine.curStep.load();
        if (step >= 0 && ctl.engine.curPat.load() == ctl.P.s.pat && step / 16 != ctl.ui.page && ctl.P.plen[(size_t) ctl.P.s.pat] > 16)
        {
            ctl.ui.page = step / 16;   // follow the playhead across pages
            seq.refresh();
        }
    }
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (C::bg); }

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<juce::TextEditor*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c) != nullptr) return;
    if (! hasKeyboardFocus (false)) grabKeyboardFocus();
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    top.setBounds (r.removeFromTop (44));
    r.removeFromTop (6);
    face.setBounds (r.removeFromTop (juce::jlimit (230, 300, getHeight() / 4)));
    r.removeFromTop (8);
    if (tabsWin != nullptr) { deck.setBounds (r); }   // section popped out: the deck gets the rest
    else
    {
        deck.setBounds (r.removeFromTop (ctl.ui.keysVisible && kbWin == nullptr ? 296 : 200));
        r.removeFromTop (8);
        tabs.setBounds (r);
    }
    if (learnMain) { learnMain->setBounds (getLocalBounds()); learnMain->toFront (false); }
    overlay.setBounds (getLocalBounds());
    overlay.toFront (false);
}

// ------------------------------------------------------------------ keys
void MainComponent::releaseQwerty()
{
    for (auto& kv : qwertyDown) { ctl.noteOff (kv.second, 1); deck.kb.keys.light (kv.second, 1, false); }
    qwertyDown.clear();
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) != nullptr) return false;
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    if (mods.isCommandDown())
    {
        const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter() ? k.getTextCharacter() : (juce::juce_wchar) code);
        if (c == 'z' || code == 'Z') { if (mods.isShiftDown()) ctl.redo(); else ctl.undo(); return true; }
        if (c == 'y' || code == 'Y') { ctl.redo(); return true; }
        if (c == 'c' || code == 'C') { if (ctl.ui.selTool && ctl.ui.hasSel) ctl.copySteps(); else ctl.copyPad(); return true; }
        if (c == 'v' || code == 'V') { if (ctl.ui.selTool) ctl.pasteSteps(); else ctl.pastePad(); return true; }
        if (c == 's' || code == 'S') { saveFile ("Save song", io::safeName (ctl.P.name) + ".zip", "*.zip", [this] (juce::File f) { ctl.saveSong (f); }); return true; }
        if (c == 'o' || code == 'O') { openFile ("Load CONTRXL 25 song", "*.zip", [this] (juce::File f) { ctl.loadSong (f); }); return true; }
        return false;
    }
    if (code == juce::KeyPress::spaceKey) { if (ctl.engine.playing.load()) ctl.stop(); else ctl.play(); return true; }
    if (code == juce::KeyPress::leftKey) { ctl.setOct (ctl.P.s.oct - 1); return true; }
    if (code == juce::KeyPress::rightKey) { ctl.setOct (ctl.P.s.oct + 1); return true; }
    if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && ctl.ui.hasSel) { ctl.deleteSteps(); return true; }
    if (code == juce::KeyPress::escapeKey && ctl.midiLearn)
    {
        if (ctl.learnTarget) { ctl.learnTarget.reset(); ctl.engine.learnArmed.store (false); ctl.status ("MIDI LEARN - CLICK ANOTHER CONTROL, OR ESC AGAIN TO FINISH"); }
        else toggleMidiLearn();
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        if (chopWin != nullptr && chopWin->isActiveWindow()) { closeChopWindow(); return true; }
        ctl.ui.manualLearn = 0; ctl.status ("READY"); return true;
    }
    const bool kb32 = ctl.P.s.keys == 32 && ctl.ui.qwerty;
    const auto ch = k.getTextCharacter();
    if (! kb32 && ch == '[') { ctl.setBank ((ctl.ui.bank + 7) % 8); return true; }
    if (! kb32 && ch == ']') { ctl.setBank ((ctl.ui.bank + 1) % 8); return true; }
    if (ch == ',' || ch == '<') { ctl.stepRegion (-1); return true; }
    if (ch == '.' || ch == '>') { ctl.stepRegion (1); return true; }
    if (mods.isShiftDown())
    {
        static const char* shifted = "!@#$%^&*";   // Shift+1..8 on a US layout
        for (int i = 0; i < 8; ++i)
            if (ch == (juce::juce_wchar) shifted[i] || (code == '1' + i)) { ctl.hitPad (ctl.ui.bank * 8 + i, 100); return true; }
    }
    // the note keys themselves are handled in keyStateChanged (needs key-up)
    for (int i = 0; i < 32; ++i) if (code == qwertyCode (i) || juce::CharacterFunctions::toUpperCase (ch) == qwertyCode (i)) return ctl.ui.qwerty;
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    if (dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) != nullptr) return false;
    if (! ctl.ui.qwerty) { if (! qwertyDown.empty()) releaseQwerty(); return false; }
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    bool used = false;
    for (int i = 0; i < ctl.P.s.keys; ++i)
    {
        const int code = qwertyCode (i);
        const bool down = juce::KeyPress::isKeyCurrentlyDown (code) && ! mods.isCommandDown() && ! mods.isShiftDown() && ! mods.isAltDown();
        const bool was = qwertyDown.count (code) > 0;
        if (down && ! was)
        {
            if (i >= 29 && ctl.P.s.keys != 32) continue;
            qwertyDown[code] = i;
            deck.kb.keys.light (i, 1, true);
            ctl.noteOn (i, 100, 1);
            used = true;
        }
        else if (! down && was)
        {
            const int idx = qwertyDown[code];
            qwertyDown.erase (code);
            deck.kb.keys.light (idx, 1, false);
            ctl.noteOff (idx, 1);
            used = true;
        }
    }
    return used;
}

// ------------------------------------------------------------------ dialogs
void MainComponent::openAudio (bool multiple, std::function<void (juce::Array<juce::File>)> cb)
{
    chooser = std::make_unique<juce::FileChooser> ("Load audio", lastDir, io::audioWildcard());
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
               | (multiple ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
    chooser->launchAsync (flags, [this, cb] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();
        if (files.isEmpty()) return;
        lastDir = files[0].getParentDirectory();
        cb (files);
    });
}

void MainComponent::openFile (const juce::String& title, const juce::String& wildcard, std::function<void (juce::File)> cb)
{
    chooser = std::make_unique<juce::FileChooser> (title, lastDir, wildcard);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, cb] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        lastDir = f.getParentDirectory();
        cb (f);
    });
}

void MainComponent::saveFile (const juce::String& title, const juce::String& name, const juce::String& wildcard, std::function<void (juce::File)> cb)
{
    chooser = std::make_unique<juce::FileChooser> (title, lastDir.getChildFile (name), wildcard);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, cb, wildcard] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto ext = wildcard.fromLastOccurrenceOf ("*", false, false);   // ".wav"
        if (ext.isNotEmpty() && ! f.hasFileExtension (ext)) f = f.withFileExtension (ext);
        lastDir = f.getParentDirectory();
        cb (f);
    });
}

void MainComponent::chooseFolder (const juce::String& title, std::function<void (juce::File)> cb)
{
    chooser = std::make_unique<juce::FileChooser> (title, lastDir);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, cb] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        lastDir = f;
        cb (f);
    });
}

void MainComponent::showAudioSettings()
{
    auto sel = std::make_unique<juce::AudioDeviceSelectorComponent> (devices, 0, 2, 0, 2, true, false, true, false);
    sel->setSize (560, 520);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (sel.release());
    o.dialogTitle = "Audio / MIDI settings";
    o.dialogBackgroundColour = C::plate;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

int MainComponent::inputChannels()
{
    if (auto* d = devices.getCurrentAudioDevice()) return d->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

void MainComponent::openFiles (const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> audio;
    for (auto& f : files)
    {
        if (f.hasFileExtension (".zip")) { ctl.loadSong (f); return; }
        audio.add (f);
    }
    if (! audio.isEmpty()) ctl.loadFiles (audio, ctl.P.s.sel);
}

// ------------------------------------------------------------------ pop-out windows
PopOutWindow::Holder::Holder (MainComponent& o) : owner (o)
{
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);
}

bool PopOutWindow::Holder::keyPressed (const juce::KeyPress& k) { return owner.keyPressed (k); }
bool PopOutWindow::Holder::keyStateChanged (bool d) { return owner.keyStateChanged (d); }

void PopOutWindow::Holder::mouseDown (const juce::MouseEvent& e)
{
    for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<juce::TextEditor*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c) != nullptr) return;
    if (! hasKeyboardFocus (false)) grabKeyboardFocus();
}

PopOutWindow::PopOutWindow (const juce::String& title, MainComponent& owner, std::function<void()> close)
    : juce::DocumentWindow (title, C::plate, juce::DocumentWindow::closeButton | juce::DocumentWindow::maximiseButton | juce::DocumentWindow::minimiseButton),
      onClose (std::move (close))
{
    setUsingNativeTitleBar (true);
    holder = new Holder (owner);
    setContentOwned (holder, false);
    setResizable (true, false);
}

MainComponent::SeqHost::SeqHost()
{
    msg.setText ("The sequencer is in its own window.", juce::dontSendNotification);
    msg.setFont (mono (14.0f)); msg.setColour (juce::Label::textColourId, C::mute);
    msg.setJustificationType (juce::Justification::centred);
    addChildComponent (msg); addChildComponent (dock);
}

void MainComponent::SeqHost::resized()
{
    const bool docked = seq != nullptr && seq->getParentComponent() == this;
    msg.setVisible (! docked); dock.setVisible (! docked);
    if (docked) { seq->setBounds (getLocalBounds()); return; }
    auto r = getLocalBounds().withSizeKeepingCentre (420, 80);
    msg.setBounds (r.removeFromTop (30));
    dock.setBounds (r.withSizeKeepingCentre (240, 30));
}

void MainComponent::placeWindow (PopOutWindow& w, const juce::String& key, int defW, int defH)
{
    const auto saved = props->getValue (key);
    if (saved.isEmpty() || ! w.restoreWindowStateFromString (saved))
    {
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            defW = juce::jmin (defW, d->userArea.getWidth() - 60);
            defH = juce::jmin (defH, d->userArea.getHeight() - 80);
        }
        w.setSize (defW, defH);
        if (auto* top = getTopLevelComponent())
            w.setTopLeftPosition (top->getScreenX() + 40, top->getScreenY() + 60);
        else w.centreWithSize (defW, defH);
    }
    w.setVisible (true);
    makeTitleBarDark (w);
    w.toFront (true);
    if (w.holder) { setNoFocusRecursive (*w.holder); w.holder->grabKeyboardFocus(); }
}

void MainComponent::togglePopSequencer()
{
    if (seqWin != nullptr)
    {
        props->setValue ("win_seq", seqWin->getWindowStateAsString());
        seqWin->holder->removeChildComponent (&seq);
        seqWin.reset();
        seqHost.addAndMakeVisible (seq);
        seq.popped = false;
        seqHost.resized();
        grabKeyboardFocus();
    }
    else
    {
        seqHost.removeChildComponent (&seq);
        seqWin = std::make_unique<PopOutWindow> ("CONTRXL 25 - Sequencer", *this, [this] { juce::MessageManager::callAsync ([this] { if (seqWin) togglePopSequencer(); }); });
        seq.popped = true;
        seqWin->holder->setContent (&seq);
        seqHost.resized();
        tabs.setCurrentTabIndex (0);
        placeWindow (*seqWin, "win_seq", 1320, 720);
    }
    seq.refresh();
    ctl.status (seqWin ? "SEQUENCER POPPED OUT - CLOSE ITS WINDOW (OR PRESS DOCK) TO PUT IT BACK" : "SEQUENCER DOCKED");
}

void MainComponent::togglePopKeyboard()
{
    if (kbWin != nullptr)
    {
        props->setValue ("win_kb", kbWin->getWindowStateAsString());
        kbWin->holder->removeChildComponent (&deck.kb);
        kbWin.reset();
        deck.kb.drawBackground = false;
        deck.addAndMakeVisible (deck.kb);
        ctl.ui.keysVisible = true;
        grabKeyboardFocus();
    }
    else
    {
        deck.removeChildComponent (&deck.kb);
        kbWin = std::make_unique<PopOutWindow> ("CONTRXL 25 - Keyboard", *this, [this] { juce::MessageManager::callAsync ([this] { if (kbWin) togglePopKeyboard(); }); });
        deck.kb.drawBackground = true;
        deck.kb.setVisible (true);
        kbWin->holder->setContent (&deck.kb);
        placeWindow (*kbWin, "win_kb", 1180, 250);
    }
    deck.refresh();
    deck.resized();
    checkLayout();
    ctl.status (kbWin ? "KEYBOARD POPPED OUT - CLOSE ITS WINDOW (OR PRESS DOCK) TO PUT IT BACK" : "KEYBOARD DOCKED");
}

void MainComponent::openChopWindow()
{
    if (chopWin != nullptr) { chopWin->toFront (true); return; }
    if (! ctl.selPad().buf) ctl.status ("LOAD A SAMPLE ON THIS PAD - DOUBLE-CLICK THE CHOP WINDOW TO LOAD ONE");
    chopWin = std::make_unique<PopOutWindow> ("CONTRXL 25 - Chop", *this, [this] { juce::MessageManager::callAsync ([this] { closeChopWindow(); }); });
    auto ed = std::make_unique<ChopEditor> (ctl, *this);
    chopEd = ed.get();
    chopEd->setComponentID ("chop");
    chopEd->setName ("Chop window");
    chopEd->onClose = [this] { juce::MessageManager::callAsync ([this] { closeChopWindow(); }); };
    chopWin->holder->setContent (chopEd);
    chopWin->holder->owned = std::move (ed);
    const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                    ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea : juce::Rectangle<int> (0, 0, 1600, 900);
    placeWindow (*chopWin, "win_chop", juce::jmin (1400, area.getWidth() - 80), juce::jmin (620, area.getHeight() - 120));
    ctl.status ("CHOP WINDOW - CLICK TO ADD CHOP POINTS, DRAG TO MOVE, RIGHT-CLICK TO REMOVE. ESC CLOSES");
}

void MainComponent::togglePopSection()
{
    if (tabsWin != nullptr)
    {
        props->setValue ("win_tabs", tabsWin->getWindowStateAsString());
        tabsWin->holder->removeChildComponent (&tabs);
        tabsWin.reset();
        addAndMakeVisible (tabs);
        grabKeyboardFocus();
    }
    else
    {
        removeChildComponent (&tabs);
        tabsWin = std::make_unique<PopOutWindow> ("CONTRXL 25 - Sequencer & sound", *this, [this] { juce::MessageManager::callAsync ([this] { if (tabsWin) togglePopSection(); }); });
        tabsWin->holder->setContent (&tabs);
        placeWindow (*tabsWin, "win_tabs", 1400, 520);
    }
    tabs.pop.setButtonText (tabsWin ? "Dock section" : "Pop out section");
    checkLayout();
    ctl.status (tabsWin ? "SECTION POPPED OUT - CLOSE ITS WINDOW (OR PRESS DOCK SECTION) TO PUT IT BACK" : "SECTION DOCKED");
}

int MainComponent::minimumHeight() const
{
    const int faceH = 230, deckH = ctl.ui.keysVisible && kbWin == nullptr ? 296 : 200;
    return 8 + 44 + 6 + faceH + 8 + deckH + (tabsWin != nullptr ? 0 : 8 + 330) + 8;
}

void MainComponent::applyUiScale()
{
    float f = uiScale > 0 ? uiScale / 100.0f : 1.0f;
    if (uiScale == 0)
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            // fit: the full layout is about 1500 x 1000 (logical pixels, Windows scaling already included)
            const auto a = d->userArea;
            f = juce::jlimit (0.6f, 1.0f, juce::jmin ((float) (a.getHeight() - 50) / 1000.0f, (float) (a.getWidth() - 20) / 1400.0f));
        }
    juce::Desktop::getInstance().setGlobalScaleFactor (f);
}

void MainComponent::setUiScale (int percent)
{
    uiScale = percent;
    props->setValue ("uiScale", uiScale);
    props->saveIfNeeded();
    applyUiScale();
    ctl.status ("SIZE " + (uiScale == 0 ? juce::String ("FIT SCREEN") : juce::String (uiScale) + "%"));
}

void MainComponent::CountOverlay::paint (juce::Graphics& g)
{
    if (count <= 0) return;
    const auto txt = juce::String (count);
    const float size = juce::jmin (200.0f, getWidth() * 0.26f);
    auto r = juce::Rectangle<float> (0, getHeight() * 0.38f - size * 0.6f, (float) getWidth(), size * 1.05f);
    g.setFont (mono (size, true));
    // soft dark shadow and amber glow, then the number
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    for (int d = 6; d >= 2; d -= 2) g.drawText (txt, r.translated ((float) d, (float) d), juce::Justification::centred);
    g.setColour (C::accent.withAlpha (0.25f));
    for (int d : { -3, 3 }) { g.drawText (txt, r.translated ((float) d, 0), juce::Justification::centred); g.drawText (txt, r.translated (0, (float) d), juce::Justification::centred); }
    g.setColour (C::accent.withAlpha (0.92f));
    g.drawText (txt, r, juce::Justification::centred);
    g.setColour (juce::Colours::white);
    g.setFont (mono (15.0f, true));
    const juce::String label = kind == 1 ? "R E C O R D I N G" : kind == 2 ? "S A M P L I N G" : "C O U N T - I N";
    g.drawText (label, r.withY (r.getBottom()).withHeight (24), juce::Justification::centred);
}

void MainComponent::closeChopWindow()
{
    if (chopWin == nullptr) return;
    props->setValue ("win_chop", chopWin->getWindowStateAsString());
    chopEd = nullptr;
    chopWin.reset();
    grabKeyboardFocus();
}

// ------------------------------------------------------------------ MIDI learn
std::vector<juce::Component*> MainComponent::learnRoots()
{
    std::vector<juce::Component*> r { this };
    for (auto* w : { seqWin.get(), kbWin.get(), chopWin.get(), tabsWin.get() })
        if (w != nullptr && w->holder != nullptr) r.push_back (w->holder);
    return r;
}

void MainComponent::updateLearnOverlays()
{
    const bool on = ctl.midiLearn;
    if (on && learnMain == nullptr)
    {
        learnMain = std::make_unique<LearnOverlay> (ctl, *this);
        addAndMakeVisible (*learnMain);
        resized();
    }
    else if (! on && learnMain != nullptr) learnMain.reset();
    for (auto* w : { seqWin.get(), kbWin.get(), chopWin.get(), tabsWin.get() })
    {
        if (w == nullptr || w->holder == nullptr) continue;
        auto& h = *w->holder;
        if (on && h.learnLayer == nullptr) { h.learnLayer = std::make_unique<LearnOverlay> (ctl, h); h.addAndMakeVisible (*h.learnLayer); }
        else if (! on && h.learnLayer != nullptr) h.learnLayer.reset();
        if (h.learnLayer != nullptr)
        {
            h.learnLayer->setBounds (h.getLocalBounds());
            if (h.getChildren().getLast() != h.learnLayer.get()) h.learnLayer->toFront (false);
        }
    }
}

void MainComponent::toggleMidiLearn()
{
    ctl.setMidiLearn (! ctl.midiLearn);
    updateLearnOverlays();
    top.refresh();
    grabKeyboardFocus();
}

void MainComponent::handleMapFire (const MidiTarget& t, bool isCC, int value)
{
    const juce::String key = t.kind + ":" + t.id + ":" + juce::String (t.n);
    // buttons: a note fires on note-on; a CC fires once when it goes up (>= 64), like pressing a button
    auto pressed = [&]
    {
        if (! isCC) return value > 0;
        const bool down = value >= 64, was = ccButtonDown[key];
        ccButtonDown[key] = down;
        return down && ! was;
    };
    if (t.kind == "bank") { if (pressed()) ctl.setBank (juce::jlimit (0, 7, t.n)); return; }
    if (t.kind == "knob")
    {
        if (! isCC) return;
        const int gi = ctl.ui.bank * 8 + juce::jlimit (0, 7, t.n);
        ctl.setPitch (gi, std::round ((value - 63.5f) / 63.5f * PR), true);
        ctl.status ("MIDI -> " + padLabel (gi) + " PITCH " + fmtSt (ctl.P.pads[(size_t) gi].pitch) + " ST");
        return;
    }
    juce::Component* c = nullptr;
    for (auto* root : learnRoots()) if ((c = findLearnable (root, t.id)) != nullptr) break;
    if (c == nullptr) return;   // e.g. the chop window is closed
    if (t.kind == "slider")
    {
        juce::Slider* s = dynamic_cast<juce::Slider*> (c);
        if (auto* ls = dynamic_cast<LSlider*> (c)) s = &ls->slider;
        if (s == nullptr || ! s->isEnabled()) return;
        const double v = isCC ? value / 127.0 : (value > 0 ? 1.0 : 0.0);
        s->setValue (s->proportionOfLengthToValue (v), juce::sendNotificationSync);
        return;
    }
    if (t.kind == "btn")
        if (auto* b = dynamic_cast<juce::Button*> (c))
            if (pressed() && b->isEnabled() && b->onClick) b->onClick();
}

void MainComponent::saveMidiPrefs()
{
    if (props == nullptr) return;
    props->setValue ("midiMap", juce::JSON::toString (ctl.midiMapToVar(), true));
    props->setValue ("followOct", ctl.engine.followOct.load());
    props->setValue ("defTransport", ctl.engine.defaultTransportCC.load());
    props->setValue ("midiOffset", ctl.engine.midiOffset.load());
    props->setValue ("noteFilter", host.getNoteFilter());
    props->saveIfNeeded();
}

void MainComponent::showMidiSettings()
{
    if (midiWin != nullptr) { midiWin->toFront (true); return; }
    midiWin = std::make_unique<PopOutWindow> ("CONTRXL 25 - MIDI", *this, [this] { juce::MessageManager::callAsync ([this]
    {
        if (midiWin) { props->setValue ("win_midi", midiWin->getWindowStateAsString()); midiWin.reset(); grabKeyboardFocus(); }
    }); });
    auto panel = std::make_unique<MidiSettings> (ctl, host, *this, devices, [this] { saveMidiPrefs(); });
    midiWin->holder->setContent (panel.get());
    midiWin->holder->owned = std::move (panel);
    placeWindow (*midiWin, "win_midi", 700, 620);
}

// ------------------------------------------------------------------ VST3 plug-ins
namespace
{
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        PluginEditorWindow (const juce::String& title, juce::AudioProcessorEditor* ed, std::function<void()> onClose)
            : juce::DocumentWindow (title, juce::Colours::black, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
              close (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (ed, true);
            setResizable (ed->isResizable(), false);
        }
        void closeButtonPressed() override { if (close) close(); }
    private:
        std::function<void()> close;
    };

    // the plug-in list with a big "scan" button on top
    class PluginListHost : public juce::Component
    {
    public:
        PluginListHost (Controller& c, juce::PropertiesFile* props, const juce::File& crashFile) : ctl (c)
        {
            list = std::make_unique<juce::PluginListComponent> (c.plugins.formats, c.plugins.known, crashFile, props, true);
            addAndMakeVisible (*list);
            info.setText ("VST3 plug-ins (instruments and effects). Scan finds plug-ins in the usual VST3 folder "
                          "(C:\\Program Files\\Common Files\\VST3); add other folders when the scan asks. "
                          "A plug-in that crashes the scan is skipped next time.", juce::dontSendNotification);
            info.setFont (mono (11.0f)); info.setColour (juce::Label::textColourId, C::mute);
            info.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (info);
            scan.onClick = [this] { if (auto* f = ctl.plugins.formats.getFormat (0)) list->scanFor (*f); };
            addAndMakeVisible (scan);
            setSize (760, 560);
        }
        void paint (juce::Graphics& g) override { g.fillAll (C::plate); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            auto top = r.removeFromTop (40);
            scan.setBounds (top.removeFromRight (180).reduced (0, 4));
            info.setBounds (top);
            r.removeFromTop (6);
            list->setBounds (r);
        }
    private:
        Controller& ctl;
        std::unique_ptr<juce::PluginListComponent> list;
        juce::Label info;
        Btn scan { "Scan for VST3 plug-ins", "Look for new or updated VST3 plug-ins" };
    };

    // auto-sampler settings
    class AutoSampleBox : public juce::Component
    {
    public:
        AutoSampleBox (Controller& c, int instPad) : ctl (c), pad (instPad)
        {
            static const char* N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            title.setText ("Auto-sample " + ctl.P.pads[(size_t) pad].inst.name + " (" + padLabel (pad) + ")", juce::dontSendNotification);
            title.setFont (mono (13.0f, true)); title.setColour (juce::Label::textColourId, C::accent);
            mode.addItem ("Keys: one sample, every key gets its own note (chop mode)", 1);
            mode.addItem ("Pads: one note per pad (drum kits, one-shots)", 2);
            mode.setSelectedId (1, juce::dontSendNotification);
            for (int n = 0; n < 128; ++n) start.addItem (juce::String (N[n % 12]) + juce::String (n / 12 - 1) + "  (" + juce::String (n) + ")", n + 1);
            for (int k = 1; k <= 64; ++k) count.addItem (juce::String (k) + (k == 1 ? " note" : " notes"), k);
            for (int k = 1; k <= 12; ++k) step.addItem ("every " + juce::String (k) + (k == 1 ? " semitone" : " semitones"), k);
            for (int i = 0; i < NPADS; ++i) target.addItem (padLabel (i) + " - " + (ctl.P.pads[(size_t) i].buf ? ctl.P.pads[(size_t) i].name : juce::String ("empty")), i + 1);
            for (auto* cb : { &mode, &start, &count, &step, &target }) { cb->setWantsKeyboardFocus (false); addAndMakeVisible (cb); }
            auto setupS = [this] (juce::Slider& s, double lo, double hi, double v, const juce::String& suffix)
            {
                s.setRange (lo, hi, lo < 1 ? 0.1 : 1); s.setValue (v);
                s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22); s.setTextValueSuffix (suffix);
                s.setSliderStyle (juce::Slider::LinearHorizontal); s.setWantsKeyboardFocus (false);
                addAndMakeVisible (s);
            };
            setupS (noteLen, 0.1, 10, 1.5, " s"); setupS (tail, 0.1, 10, 2.0, " s"); setupS (vel, 1, 127, 100, "");
            for (auto [l, t] : { std::pair<juce::Label*, const char*> { &lMode, "Make" }, { &lStart, "First note" }, { &lCount, "How many" },
                                 { &lStep, "Step" }, { &lTarget, "Put it on" }, { &lLen, "Hold each note" }, { &lTail, "Max release tail" }, { &lVel, "Velocity" } })
            {
                l->setText (t, juce::dontSendNotification); l->setFont (mono (11.5f)); l->setColour (juce::Label::textColourId, C::mute);
                addAndMakeVisible (l);
            }
            mode.onChange = [this] { defaults(); };
            go.onClick = [this]
            {
                Controller::AutoSample a;
                a.pad = pad; a.toPads = mode.getSelectedId() == 2;
                a.startNote = start.getSelectedId() - 1; a.count = count.getSelectedId(); a.step = step.getSelectedId();
                a.target = target.getSelectedId() - 1; a.vel = (int) vel.getValue();
                a.noteSec = noteLen.getValue(); a.tailSec = tail.getValue();
                auto& c2 = ctl;
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0);
                juce::MessageManager::callAsync ([&c2, a] { c2.autoSample (a); });
            };
            cancel.onClick = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); };
            addAndMakeVisible (title); addAndMakeVisible (go); addAndMakeVisible (cancel);
            go.setOnColour (C::ok); go.setOn (true);
            defaults();
            setSize (560, 380);
        }
        void defaults()
        {
            const bool pads = mode.getSelectedId() == 2;
            const auto& P = ctl.P;
            int startNote = pads ? 36 : juce::jlimit (0, 127, P.pads[(size_t) pad].instRoot + P.keyBase() - P.s.root);
            start.setSelectedId (startNote + 1, juce::dontSendNotification);
            count.setSelectedId (pads ? 8 : P.s.keys, juce::dontSendNotification);
            step.setSelectedId (1, juce::dontSendNotification);
            step.setEnabled (pads);
            // the next empty pad (or pads) after the instrument
            int tgt = (pad + 1) % NPADS;
            for (int k = 1; k < NPADS; ++k) { const int g = (pad + k) % NPADS; if (! P.pads[(size_t) g].buf && P.pads[(size_t) g].inst.empty()) { tgt = g; break; } }
            target.setSelectedId (tgt + 1, juce::dontSendNotification);
        }
        void paint (juce::Graphics& g) override { g.fillAll (C::plate); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (14);
            title.setBounds (r.removeFromTop (26)); r.removeFromTop (8);
            auto row = [&] (juce::Label& l, juce::Component& c) { auto x = r.removeFromTop (28); l.setBounds (x.removeFromLeft (140)); c.setBounds (x); r.removeFromTop (6); };
            row (lMode, mode); row (lStart, start); row (lCount, count); row (lStep, step); row (lTarget, target);
            row (lLen, noteLen); row (lTail, tail); row (lVel, vel);
            auto b = r.removeFromBottom (30);
            cancel.setBounds (b.removeFromRight (100)); b.removeFromRight (8); go.setBounds (b.removeFromRight (160));
        }
    private:
        Controller& ctl; int pad;
        juce::Label title, lMode, lStart, lCount, lStep, lTarget, lLen, lTail, lVel;
        juce::ComboBox mode, start, count, step, target;
        juce::Slider noteLen, tail, vel;
        Btn go { "Start sampling" }, cancel { "Cancel" };
    };
}

void MainComponent::showTab (const juce::String& name)
{
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (tabs.getTabNames()[i] == name) { tabs.setCurrentTabIndex (i); break; }
    if (tabsWin) tabsWin->toFront (true);
}

void MainComponent::openPluginEditor (const SlotId& s)
{
    auto* node = ctl.pluginNode (s);
    if (node == nullptr || node->proc == nullptr) { ctl.status ("THE PLUG-IN IS NOT LOADED (" + ctl.slotRef (s).name.toUpperCase() + ")"); return; }
    auto it = editorWins.find (node);
    if (it != editorWins.end()) { it->second->setVisible (true); it->second->toFront (true); return; }
    juce::AudioProcessorEditor* ed = node->proc->hasEditor() ? node->proc->createEditorIfNeeded() : nullptr;
    if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor (*node->proc);
    juce::Component::SafePointer<MainComponent> sp (this);
    auto w = std::make_unique<PluginEditorWindow> (ctl.slotRef (s).name + " - " + s.label(), ed, [sp, node]
    {
        juce::MessageManager::callAsync ([sp, node] { if (sp) sp->editorWins.erase (node); });
    });
    if (auto* top = getTopLevelComponent()) w->setTopLeftPosition (top->getScreenX() + 120 + 30 * (int) editorWins.size(), top->getScreenY() + 120 + 30 * (int) editorWins.size());
    w->setVisible (true);
    makeTitleBarDark (*w);
    w->toFront (true);
    editorWins[node] = std::move (w);
}

void MainComponent::showPluginList()
{
    if (pluginListWin) { pluginListWin->toFront (true); return; }
    if (! ctl.plugins.hasVst3()) { ctl.status ("THIS BUILD HAS NO VST3 SUPPORT"); return; }
    pluginListWin = std::make_unique<PopOutWindow> ("CONTRXL 25 - Plug-ins (VST3)", *this, [this] { juce::MessageManager::callAsync ([this]
    {
        if (pluginListWin) { props->setValue ("win_plugins", pluginListWin->getWindowStateAsString()); pluginListWin.reset(); grabKeyboardFocus(); }
    }); });
    auto crash = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("CONTRXL25").getChildFile ("plugin_scan_crash.txt");
    auto host = std::make_unique<PluginListHost> (ctl, props.get(), crash);
    pluginListWin->holder->setContent (host.get());
    pluginListWin->holder->owned = std::move (host);
    placeWindow (*pluginListWin, "win_plugins", 780, 600);
}

void MainComponent::showAutoSampler (int pad)
{
    if (ctl.slotRef ({ SlotId::Inst, pad, 0 }).empty()) { ctl.status ("PUT A VST INSTRUMENT ON " + padLabel (pad) + " FIRST"); return; }
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new AutoSampleBox (ctl, pad));
    o.dialogTitle = "Auto-sample";
    o.dialogBackgroundColour = C::plate;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    if (auto* dw = o.launchAsync()) makeTitleBarDark (*dw);
}

bool MainComponent::confirmQuit()
{
    return ! ctl.unsaved;
}
} // namespace cx::ui
