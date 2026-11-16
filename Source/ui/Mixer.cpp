#include "Mixer.h"

namespace cx::ui
{
// ------------------------------------------------------------------ plug-in slot button
PluginSlotButton::PluginSlotButton (Controller& c, SlotId s, juce::String empty)
    : Btn (empty, "Click: open the plug-in (empty: search your plug-ins). Right-click: plug-in menu. You can also drop a .vst3 file here"), slot (s), ctl (c), emptyText (std::move (empty))
{
    getProperties().set ("noLearn", true);
}

void PluginSlotButton::update()
{
    const auto& sl = ctl.slotRef (slot);
    setButtonText (sl.empty() ? emptyText : sl.name);
    repaint();
}

void PluginSlotButton::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto& sl = ctl.slotRef (slot);
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto st = sl.empty() ? juce::String() : ctl.slotStatus (slot);
    const juce::Colour base = sl.empty() ? C::well : (st == "missing" ? C::rec.withAlpha (0.35f) : (sl.bypass ? C::plate2 : C::accent.withAlpha (0.22f)));
    g.setColour (over ? base.brighter (0.15f) : base); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (sl.empty() ? C::line : (sl.bypass ? C::mute : C::accent)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (sl.empty() ? C::mute : (sl.bypass ? C::mute : C::fg));
    g.setFont (mono (10.0f, ! sl.empty()));
    auto txt = sl.empty() ? emptyText : (st == "missing" ? "? " + sl.name : sl.name);
    g.drawFittedText (txt, getLocalBounds().reduced (5, 1), juce::Justification::centred, 1, 0.8f);
}

void PluginSlotButton::mouseDown (const juce::MouseEvent& e)
{
    const auto& sl = ctl.slotRef (slot);
    if (e.mods.isPopupMenu() || ctl.slotStatus (slot).isNotEmpty()) showPluginMenu (ctl, slot, this);
    else if (sl.empty()) showPluginBrowser (ctl, slot, this);   // empty slot: search and pick a plug-in
    else if (ctl.onOpenEditor) ctl.onOpenEditor (slot);
}

void PluginSlotButton::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (Controller::isPluginFile (f)) { if (ctl.setPluginFromFile (slot, f) && ctl.onOpenEditor) ctl.onOpenEditor (slot); return; }
}

// ------------------------------------------------------------------ meter
void StripMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::well); g.fillRoundedRectangle (r, 2.0f);
    const float db = level > 0 ? 20.0f * std::log10 (level) : -100.0f;
    const float f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    auto bar = r.reduced (1.5f);
    bar = bar.removeFromBottom (bar.getHeight() * f);
    juce::ColourGradient cg (juce::Colour (0xff3ecf6a), 0, r.getBottom(), juce::Colour (0xffe5484d), 0, r.getY(), false);
    cg.addColour (0.7, juce::Colour (0xff3ecf6a)); cg.addColour (0.88, C::accent);
    g.setGradientFill (cg); g.fillRect (bar);
    if (juce::Time::getMillisecondCounter() - clipT < 1200) { g.setColour (juce::Colour (0xffe5484d)); g.fillRect (r.removeFromTop (4)); }
}

// ------------------------------------------------------------------ channel strip
ChannelStrip::ChannelStrip (Controller& c, Dialogs& d, Kind k, int idx) : ctl (c), dlg (d), kind (k), index (idx)
{
    name.setFont (mono (11.5f, true));
    name.setJustificationType (juce::Justification::centred);
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);
    const juce::String sid = kind == PadStrip ? "S" + juce::String (index + 1) : kind == ReturnStrip ? juce::String ("Ret ") + (char) ('A' + index) : juce::String ("Master");
    if (kind == PadStrip)
    {
        inst = std::make_unique<PluginSlotButton> (ctl, SlotId { SlotId::Inst, 0, 0 }, "+ instrument");
        addAndMakeVisible (*inst);
        for (int k2 = 0; k2 < PAD_VFX; ++k2) addAndMakeVisible (inserts.add (new PluginSlotButton (ctl, { SlotId::PadFx, 0, k2 }, "+ insert")));
        bFx.onClick = [this] { ctl.selectPad (pad()); dlg.showTab ("Effects"); };
        bMute.onClick = [this] { ctl.togglePadMute (pad()); };
        bSolo.onClick = [this] { ctl.toggleSolo (pad()); };
        bArm.onClick = [this] { ctl.armPadTrack (pad()); };
        bLock.onClick = [this]
        {
            const int l = ctl.trackForPad (pad());
            if (l < 0) ctl.status ("NO TRACK PLAYS " + padLabel (pad()) + " IN THIS PATTERN YET - PRESS R FIRST");
            else ctl.toggleTrackLock (l);
        };
        bMute.setOnColour (C::accent); bSolo.setOnColour (C::ok); bArm.setOnColour (C::rec); bLock.setOnColour (C::blue);
        for (auto* b : { &bFx, &bMute, &bSolo, &bArm, &bLock }) addAndMakeVisible (b);
        knob (sendA, lA, "A", 0, 1, 0); knob (sendB, lB, "B", 0, 1, 0); knob (pan, lPan, "PAN", -1, 1, 0);
        sendA.setTooltip ("Send to return A"); sendB.setTooltip ("Send to return B"); pan.setTooltip ("Pan (double-click = centre)");
        sendA.onValueChange = [this] { ctl.setPadSend (pad(), 0, (float) sendA.getValue()); };
        sendB.onValueChange = [this] { ctl.setPadSend (pad(), 1, (float) sendB.getValue()); };
        pan.onValueChange = [this] { ctl.setPadPan (pad(), (float) pan.getValue()); };
        sendA.getProperties().set ("learnName", sid + " send A");
        sendB.getProperties().set ("learnName", sid + " send B");
        pan.getProperties().set ("learnName", sid + " pan");
    }
    else if (kind == ReturnStrip)
        for (int k2 = 0; k2 < SEND_VFX; ++k2) addAndMakeVisible (inserts.add (new PluginSlotButton (ctl, { SlotId::Send, index, k2 }, "+ insert")));
    else
        for (int k2 = 0; k2 < MASTER_VFX; ++k2) addAndMakeVisible (inserts.add (new PluginSlotButton (ctl, { SlotId::Master, 0, k2 }, "+ insert")));

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setRange (0, kind == MasterStrip ? 1.0 : 1.5, 0.01);
    fader.setDoubleClickReturnValue (true, kind == MasterStrip ? 0.85 : 1.0);
    fader.setScrollWheelEnabled (false);
    fader.setWantsKeyboardFocus (false);
    fader.getProperties().set ("learnName", sid + " level");
    fader.onValueChange = [this]
    {
        const float v = (float) fader.getValue();
        if (kind == PadStrip) ctl.setPadVolAt (pad(), v);
        else if (kind == ReturnStrip) ctl.setReturnLevel (index, v);
        else ctl.setMasterVol (v);
        lVal.setText (juce::String ((int) std::round (v * 100)) + "%", juce::dontSendNotification);
    };
    lVal.setFont (mono (10.0f)); lVal.setJustificationType (juce::Justification::centred);
    lVal.setColour (juce::Label::textColourId, C::mute);
    addAndMakeVisible (fader); addAndMakeVisible (meter); addAndMakeVisible (lVal);
    refresh();
}

void ChannelStrip::knob (juce::Slider& s, juce::Label& l, const juce::String& text, double lo, double hi, double def)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (lo, hi, 0.01);
    s.setDoubleClickReturnValue (true, def);
    s.setScrollWheelEnabled (false);
    s.setWantsKeyboardFocus (false);
    s.setColour (juce::Slider::rotarySliderFillColourId, C::accent);
    s.setPopupDisplayEnabled (true, false, nullptr);
    l.setText (text, juce::dontSendNotification);
    l.setFont (mono (9.0f)); l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, C::mute);
    l.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (s); addAndMakeVisible (l);
}

int ChannelStrip::pad() const { return ctl.ui.bank * 8 + index; }

void ChannelStrip::refresh()
{
    if (kind == PadStrip)
    {
        const int gi = pad();
        const Pad& p = ctl.P.pads[(size_t) gi];
        name.setText (padLabel (gi), juce::dontSendNotification);
        name.setColour (juce::Label::textColourId, gi == ctl.P.s.sel ? C::accent : C::fg);
        setTooltip (padLabel (gi) + (p.name.isNotEmpty() ? " - " + p.name : juce::String()));
        inst->setSlot ({ SlotId::Inst, gi, 0 });
        for (int k = 0; k < inserts.size(); ++k) inserts[k]->setSlot ({ SlotId::PadFx, gi, k });
        bFx.setOn (p.fx.anyOn() || p.fx.type != FT_OFF || p.fx.lofi || p.fx.rm > 0);
        bMute.setOn (p.muted); bSolo.setOn (p.solo);
        const int l = ctl.trackForPad (gi);
        bArm.setOn (l >= 0 && ctl.P.tracks[(size_t) l].arm);
        bLock.setOn (l >= 0 && ctl.P.tracks[(size_t) l].lock);
        bArm.setTooltip (l >= 0 ? "Arm track " + juce::String (l + 1) + " (plays " + padLabel (gi) + ") for live recording" : "Arm a track for " + padLabel (gi) + " (an empty track is picked)");
        sendA.setValue (p.send[0], juce::dontSendNotification);
        sendB.setValue (p.send[1], juce::dontSendNotification);
        pan.setValue (p.pan, juce::dontSendNotification);
        fader.setValue (p.vol, juce::dontSendNotification);
        lVal.setText (juce::String ((int) std::round (p.vol * 100)) + "%", juce::dontSendNotification);
    }
    else if (kind == ReturnStrip)
    {
        name.setText (juce::String ("RETURN ") + (char) ('A' + index), juce::dontSendNotification);
        name.setColour (juce::Label::textColourId, C::blue);
        for (auto* b : inserts) b->update();
        fader.setValue (ctl.P.retLevel[(size_t) index], juce::dontSendNotification);
        lVal.setText (juce::String ((int) std::round (ctl.P.retLevel[(size_t) index] * 100)) + "%", juce::dontSendNotification);
    }
    else
    {
        name.setText ("MASTER", juce::dontSendNotification);
        name.setColour (juce::Label::textColourId, C::accent);
        for (auto* b : inserts) b->update();
        fader.setValue (ctl.P.s.mvol, juce::dontSendNotification);
        lVal.setText (juce::String ((int) std::round (ctl.P.s.mvol * 100)) + "%", juce::dontSendNotification);
    }
    repaint();
}

void ChannelStrip::tick()
{
    if (kind == PadStrip) meter.push (ctl.engine.padPeak[(size_t) pad()].exchange (0.0f));
    else if (kind == ReturnStrip) meter.push (ctl.engine.retPeak[(size_t) index].exchange (0.0f));
    else meter.push (ctl.engine.masterPeak.exchange (0.0f));
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool sel = kind == PadStrip && pad() == ctl.P.s.sel;
    g.setColour (kind == MasterStrip ? C::plate2.brighter (0.04f) : C::plate2); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (sel ? C::accent : C::line); g.drawRoundedRectangle (r, 6.0f, sel ? 1.6f : 1.0f);
}

void ChannelStrip::mouseDown (const juce::MouseEvent&)
{
    if (kind == PadStrip) ctl.selectPad (pad());
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (5);
    name.setBounds (r.removeFromTop (18)); r.removeFromTop (2);
    if (inst) { inst->setBounds (r.removeFromTop (22)); r.removeFromTop (3); }
    for (auto* b : inserts) { b->setBounds (r.removeFromTop (20)); r.removeFromTop (2); }
    if (kind == PadStrip)
    {
        r.removeFromTop (1);
        bFx.setBounds (r.removeFromTop (20)); r.removeFromTop (3);
        auto kn = r.removeFromTop (44);
        const int w = kn.getWidth() / 3;
        for (auto [s, l] : { std::pair<juce::Slider*, juce::Label*> { &sendA, &lA }, { &sendB, &lB }, { &pan, &lPan } })
        {
            auto c = kn.removeFromLeft (w);
            l->setBounds (c.removeFromBottom (11));
            s->setBounds (c.withSizeKeepingCentre (juce::jmin (c.getWidth(), c.getHeight()), juce::jmin (c.getWidth(), c.getHeight())));
        }
        r.removeFromTop (3);
        auto ms = r.removeFromTop (20);
        const int bw = (ms.getWidth() - 6) / 4;
        for (auto* b : { &bMute, &bSolo, &bArm, &bLock }) { b->setBounds (ms.removeFromLeft (bw)); ms.removeFromLeft (2); }
        r.removeFromTop (3);
    }
    else r.removeFromTop (4);
    lVal.setBounds (r.removeFromBottom (14));
    auto fz = r;
    meter.setBounds (fz.removeFromRight (10).reduced (0, 4));
    fz.removeFromRight (2);
    fader.setBounds (fz);
}

// ------------------------------------------------------------------ mixer panel
MixerPanel::MixerPanel (Controller& c, Dialogs& d) : Panel (c, d)
{
    for (int b = 0; b < 8; ++b)
    {
        auto* btn = banks.add (new Btn (juce::String::charToString ((juce::juce_wchar) ('A' + b)), "Show the pads of bank " + juce::String::charToString ((juce::juce_wchar) ('A' + b)),
                                        [this, b] { ctl.setBank (b); }));
        btn->getProperties().set ("bank", b);
        addAndMakeVisible (btn);
    }
    for (int i = 0; i < 8; ++i) addAndMakeVisible (strips.add (new ChannelStrip (ctl, dlg, ChannelStrip::PadStrip, i)));
    for (int k = 0; k < NSEND; ++k) addAndMakeVisible (strips.add (new ChannelStrip (ctl, dlg, ChannelStrip::ReturnStrip, k)));
    addAndMakeVisible (strips.add (new ChannelStrip (ctl, dlg, ChannelStrip::MasterStrip, 0)));
    title.setFont (mono (12.0f, true)); title.setColour (juce::Label::textColourId, C::accent);
    hint.setFont (mono (10.5f)); hint.setColour (juce::Label::textColourId, C::mute);
    hint.setText ("Click a slot to open its plug-in, right-click for the menu. Pad strips: instrument > pad FX > inserts > pan > sends.", juce::dontSendNotification);
    bPlugins.onClick = [this] { if (ctl.onShowPluginList) ctl.onShowPluginList(); };
    show (title, hint, bPlugins);
}

void MixerPanel::refresh()
{
    title.setText ("MIXER - BANK " + juce::String::charToString ((juce::juce_wchar) ('A' + ctl.ui.bank)), juce::dontSendNotification);
    for (int b = 0; b < banks.size(); ++b) banks[b]->setOn (b == ctl.ui.bank);
    for (auto* s : strips) s->refresh();
}

void MixerPanel::tick()
{
    if (! isShowing()) { for (auto& p : ctl.engine.padPeak) p.store (0); return; }
    for (auto* s : strips) s->tick();
}

void MixerPanel::paint (juce::Graphics& g) { g.fillAll (C::plate); }

void MixerPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (26);
    title.setBounds (top.removeFromLeft (170));
    for (auto* b : banks) { b->setBounds (top.removeFromLeft (30)); top.removeFromLeft (3); }
    top.removeFromLeft (8);
    bPlugins.setBounds (top.removeFromRight (110));
    hint.setBounds (top);
    r.removeFromTop (6);
    const int gap = 4, n = strips.size();
    const int sepGap = 12;
    const int w = (r.getWidth() - gap * (n - 1) - sepGap * 2) / n;
    for (int i = 0; i < n; ++i)
    {
        if (i == 8 || i == 10) r.removeFromLeft (sepGap);
        strips[i]->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (gap);
    }
}
} // namespace cx::ui
