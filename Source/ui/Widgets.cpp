#include "Widgets.h"

namespace cx::ui
{
void layoutRow (juce::Rectangle<int> r, std::initializer_list<std::pair<juce::Component*, int>> items, int gap)
{
    int fixed = 0, flex = 0;
    for (auto& it : items) { if (it.second > 0) fixed += it.second; else ++flex; }
    const int total = r.getWidth() - gap * ((int) items.size() - 1);
    const int share = flex > 0 ? juce::jmax (10, (total - fixed) / flex) : 0;
    for (auto& it : items)
    {
        const int w = it.second > 0 ? it.second : share;
        if (it.first) it.first->setBounds (r.removeFromLeft (w));
        else r.removeFromLeft (w);
        r.removeFromLeft (gap);
    }
}

// ------------------------------------------------------------------ LSlider
LSlider::LSlider (const juce::String& name, double lo, double hi, double step, std::function<juce::String (double)> fmt)
    : format (std::move (fmt))
{
    label.setText (name.toUpperCase(), juce::dontSendNotification);
    label.setFont (mono (11.0f));
    label.setColour (juce::Label::textColourId, C::mute);
    value.setFont (mono (11.0f, true));
    value.setJustificationType (juce::Justification::centredRight);
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (lo, hi, step);
    slider.setWantsKeyboardFocus (false);
    slider.setScrollWheelEnabled (false);   // the mouse wheel scrolls the page instead
    slider.onValueChange = [this]
    {
        value.setText (format ? format (slider.getValue()) : juce::String (slider.getValue(), 2), juce::dontSendNotification);
        if (onChange) onChange (slider.getValue());
    };
    addAndMakeVisible (label); addAndMakeVisible (slider); addAndMakeVisible (value);
}

void LSlider::set (double v)
{
    slider.setValue (v, juce::dontSendNotification);
    value.setText (format ? format (v) : juce::String (v, 2), juce::dontSendNotification);
}

void LSlider::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromLeft (labelWidth));
    value.setBounds (r.removeFromRight (valueWidth));
    slider.setBounds (r.reduced (4, 0));
}

// ------------------------------------------------------------------ PitchKnob
void PitchKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float labelH = 30.0f;
    auto kr = r.withTrimmedBottom (labelH);
    const float s = juce::jmin (kr.getWidth(), kr.getHeight()) - 4.0f;
    const auto c = kr.getCentre();
    const float rad = s * 0.5f;
    const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);
    const float av = juce::degreesToRadians (value / PR * 135.0f);
    juce::Path track; track.addCentredArc (c.x, c.y, rad * 0.85f, rad * 0.85f, 0, a0, a1, true);
    g.setColour (C::line); g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (std::abs (value) > 0.01f)
    {
        juce::Path arc; arc.addCentredArc (c.x, c.y, rad * 0.85f, rad * 0.85f, 0, 0, av, true);
        g.setColour (C::accent); g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (isActive ? C::plate2.brighter (0.2f) : C::plate2);
    g.fillEllipse (c.x - rad * 0.62f, c.y - rad * 0.62f, rad * 1.24f, rad * 1.24f);
    g.setColour (isActive ? C::accent : C::line2);
    g.drawEllipse (c.x - rad * 0.62f, c.y - rad * 0.62f, rad * 1.24f, rad * 1.24f, 1.2f);
    juce::Line<float> ind (c.x, c.y - rad * 0.55f, c.x, c.y - rad * 0.25f);
    ind.applyTransform (juce::AffineTransform::rotation (av, c.x, c.y));
    g.setColour (C::fg); g.drawLine (ind, 2.2f);
    auto lr = r.removeFromBottom (labelH);
    g.setFont (mono (10.5f, true)); g.setColour (isActive ? C::accent : C::mute);
    g.drawText (name, lr.removeFromTop (14), juce::Justification::centred);
    g.setFont (mono (11.0f)); g.setColour (C::fg);
    g.drawText (fmtSt (value), lr, juce::Justification::centred);
}

void PitchKnob::mouseDown (const juce::MouseEvent&) { dragStart = value; if (onGrab) onGrab(); }

void PitchKnob::mouseDrag (const juce::MouseEvent& e)
{
    const bool fine = e.mods.isShiftDown();
    float v = dragStart + (float) -e.getDistanceFromDragStartY() * (fine ? 0.02f : 0.3f);
    if (! fine) v = std::round (v);
    v = juce::jlimit (-PR, PR, v);
    if (onChange) onChange (v);
}

void PitchKnob::mouseDoubleClick (const juce::MouseEvent&) { if (onChange) onChange (0); }

void PitchKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (onGrab) onGrab();
    const float step = e.mods.isShiftDown() ? 0.1f : 1.0f;
    if (onChange) onChange (juce::jlimit (-PR, PR, value + (w.deltaY > 0 ? step : -step)));
}

// ------------------------------------------------------------------ Wheel
void Wheel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    auto body = r.withTrimmedBottom (16);
    g.setColour (C::well); g.fillRoundedRectangle (body, 6.0f);
    g.setColour (C::line); g.drawRoundedRectangle (body, 6.0f, 1.0f);
    for (int i = 1; i < 8; ++i)
    {
        const float y = body.getY() + body.getHeight() * (float) i / 8.0f;
        g.setColour (C::line.withAlpha (0.6f)); g.drawHorizontalLine ((int) y, body.getX() + 4, body.getRight() - 4);
    }
    const float ty = body.getCentreY() - value * (body.getHeight() - 18) * 0.5f;
    g.setColour (C::accent); g.fillRoundedRectangle (body.getX() + 3, ty - 7, body.getWidth() - 6, 14, 4);
    g.setColour (C::mute); g.setFont (mono (10.0f, true));
    g.drawText (name.toUpperCase(), r.removeFromBottom (14), juce::Justification::centred);
}

void Wheel::mouseDown (const juce::MouseEvent& e) { stopTimer(); setValue (at (e)); if (onChange) onChange (value); }
void Wheel::mouseDrag (const juce::MouseEvent& e) { setValue (at (e)); if (onChange) onChange (value); }
void Wheel::mouseUp (const juce::MouseEvent&) { if (springs) startTimerHz (60); }
void Wheel::timerCallback()
{
    const float v = value * 0.72f;
    if (std::abs (v) < 0.01f) { setValue (0); stopTimer(); }
    else setValue (v);
    if (onChange) onChange (value);
}

// ------------------------------------------------------------------ PadButton
void PadButton::tick()
{
    const auto h = ctl.engine.padHits[(size_t) padIndex()].load();
    if (h != lastHits) { lastHits = h; flashLevel = 1.0f; }
    if (flashLevel > 0.0f) { flashLevel = juce::jmax (0.0f, flashLevel - 0.12f); repaint(); }
}

void PadButton::paint (juce::Graphics& g)
{
    const int gi = padIndex();
    const Pad& p = ctl.P.pads[(size_t) gi];
    const bool sel = gi == ctl.P.s.sel;
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const auto col = pal (gi % 16);
    g.setColour (C::plate2.interpolatedWith (col, p.buf ? 0.10f + flashLevel * 0.45f : flashLevel * 0.2f));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (sel ? C::accent : (p.buf ? col.withAlpha (0.6f) : C::line));
    g.drawRoundedRectangle (r, 9.0f, sel ? 2.2f : 1.4f);
    auto in = getLocalBounds().reduced (10, 8);
    g.setFont (mono (15.0f, true)); g.setColour (sel ? C::accent : C::fg);
    g.drawText (padLabel (gi), in.removeFromTop (18), juce::Justification::topLeft);
    const bool vst = ! p.inst.empty();
    g.setFont (mono (11.0f)); g.setColour (p.buf || vst ? C::fg : C::mute);
    g.drawFittedText (p.buf ? p.name : vst ? "VST: " + p.inst.name : juce::String ("empty"), in.removeFromTop (30), juce::Justification::topLeft, 2);
    juce::String tags;
    if (p.buf && ! p.markers.empty()) tags << p.numChops() << " chops  ";
    if (! p.layers.empty()) tags << "+" << (int) p.layers.size() << " lay  ";
    if (p.lg) tags << "L" << p.lg << " ";
    if (p.mg) tags << "M" << p.mg << " ";
    if (p.fx.anyOn()) tags << "FX ";
    if (p.fx.rm > 0) tags << retroName (p.fx.rm) << " ";
    if (vst && p.buf) tags << "+VST ";
    if (! p.vfx[0].empty() || ! p.vfx[1].empty()) tags << "INS ";
    g.setFont (mono (9.5f)); g.setColour (C::mute);
    g.drawText (tags, in.removeFromBottom (12), juce::Justification::bottomLeft);
    if (p.muted)
    {
        g.setColour (C::rec); g.setFont (mono (11.0f, true));
        g.drawText ("MUTE", getLocalBounds().reduced (10, 8), juce::Justification::topRight);
    }
}

void PadButton::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    if (e.mods.isPopupMenu()) { ctl.selectPad (padIndex()); showMenu(); return; }
    const float vel = 55.0f + 72.0f * juce::jlimit (0.0f, 1.0f, (float) e.y / (float) getHeight());
    held = padIndex();
    ctl.hitPad (held, vel, true);
}

void PadButton::mouseUp (const juce::MouseEvent&)
{
    if (held >= 0) { ctl.releasePad (held); held = -1; }
}

void PadButton::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 12 && ctl.P.pads[(size_t) padIndex()].buf)
        if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            dnd->startDragging ("pad:" + juce::String (padIndex()), this);
        }
}

void PadButton::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-click an empty pad (or Shift+double-click any pad) to load a sample onto it
    if ((! ctl.P.pads[(size_t) padIndex()].buf || e.mods.isShiftDown()) && onLoadRequest) onLoadRequest (padIndex());
}

void PadButton::itemDropped (const SourceDetails& d)
{
    const auto desc = d.description.toString();
    if (desc.startsWith ("lib:"))    // a sample dragged out of the library list
    {
        const int idx = ctl.library.find (desc.fromFirstOccurrenceOf ("lib:", false, false).getIntValue());
        if (idx >= 0) ctl.libToPad (idx, padIndex());
        return;
    }
    if (desc.startsWith ("chop:"))   // a chop dragged out of the sample window
    {
        ctl.extractChop (desc.fromFirstOccurrenceOf ("chop:", false, false).getIntValue(), padIndex());
        return;
    }
    const int from = d.description.toString().fromFirstOccurrenceOf ("pad:", false, false).getIntValue();
    ctl.movePad (from, padIndex(), juce::ModifierKeys::currentModifiers.isCtrlDown() || juce::ModifierKeys::currentModifiers.isAltDown() || juce::ModifierKeys::currentModifiers.isCommandDown());
}

void PadButton::filesDropped (const juce::StringArray& files, int, int)
{
    // a .vst3 dropped on a pad becomes its instrument
    for (auto& f : files)
        if (Controller::isPluginFile (f)) { ctl.selectPad (padIndex()); if (ctl.setPluginFromFile ({ SlotId::Inst, padIndex(), 0 }, f) && ctl.onOpenEditor) ctl.onOpenEditor ({ SlotId::Inst, padIndex(), 0 }); return; }
    juce::Array<juce::File> fs;
    for (auto& f : files) fs.add (juce::File (f));
    ctl.loadFiles (fs, padIndex());
}

void PadButton::showMenu()
{
    const int gi = padIndex();
    const Pad& p = ctl.P.pads[(size_t) gi];
    juce::PopupMenu m;
    m.addSectionHeader (padLabel (gi) + (p.name.isNotEmpty() ? " - " + p.name : juce::String()));
    m.addItem (1, "Load sample...");
    m.addItem (2, "Copy pad", p.buf != nullptr || ! p.inst.empty());
    m.addItem (3, "Paste pad");
    m.addItem (4, "Clear pad", p.buf != nullptr || ! p.inst.empty());
    m.addItem (5, p.muted ? "Unmute pad" : "Mute pad");
    m.addItem (6, "Save .wav...", p.buf != nullptr);
    m.addItem (7, "Add to library", p.buf != nullptr);
    m.addSeparator();
    m.addItem (8, p.inst.empty() ? "VST instrument..." : "VST instrument: " + p.inst.name + "...");
    m.addItem (9, "Auto-sample the instrument...", ! p.inst.empty());
    m.addItem (10, "Mixer");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, gi] (int r)
    {
        switch (r)
        {
            case 8: showPluginMenu (ctl, { SlotId::Inst, gi, 0 }, this); break;
            case 9: if (ctl.onAutoSampleDialog) ctl.onAutoSampleDialog (gi); break;
            case 10: if (ctl.onShowMixer) ctl.onShowMixer(); break;
            case 1: if (onLoadRequest) onLoadRequest (gi); break;
            case 2: ctl.copyPad(); break;
            case 3: ctl.pastePad(); break;
            case 4: ctl.clearPad(); break;
            case 5: ctl.togglePadMute (gi); break;
            case 6: if (onSaveRequest) onSaveRequest (gi); break;
            case 7: ctl.addToLibrary (ctl.ui.libCrate); break;
            default: break;
        }
    });
}

// ------------------------------------------------------------------ plug-in slot menu
void showPluginMenu (Controller& ctl, const SlotId& s, juce::Component* target)
{
    const auto slot = ctl.slotRef (s);
    const bool wantInst = s.kind == SlotId::Inst;
    const auto list = ctl.plugins.list (wantInst);
    juce::PopupMenu m, choose;
    // group by maker
    juce::String maker; juce::PopupMenu sub;
    auto flush = [&] { if (maker.isNotEmpty() || sub.getNumItems() > 0) choose.addSubMenu (maker.isEmpty() ? juce::String ("(unknown maker)") : maker, sub); sub = {}; };
    for (int i = 0; i < list.size(); ++i)
    {
        if (i == 0 || list[i].manufacturerName != maker) { if (i > 0) flush(); maker = list[i].manufacturerName; }
        sub.addItem (1000 + i, list[i].name, true, ! slot.empty() && slot.name == list[i].name);
    }
    if (list.size() > 0) flush();
    m.addSectionHeader (s.label() + (slot.empty() ? juce::String() : " - " + slot.name));
    if (! slot.empty())
    {
        const auto st = ctl.slotStatus (s);
        m.addItem (1, "Open editor", st.isEmpty());
        m.addItem (2, "Bypass", true, slot.bypass);
        m.addItem (3, "Remove");
        if (wantInst) m.addItem (4, "Auto-sample...", st.isEmpty());
        if (st == "missing") m.addItem (5, "Plug-in not found - scan / try again");
        m.addSeparator();
    }
    if (list.isEmpty()) m.addItem (6, wantInst ? "No VST3 instruments yet - scan for plug-ins..." : "No VST3 effects yet - scan for plug-ins...");
    else
    {
        m.addItem (8, slot.empty() ? (wantInst ? "Search instruments..." : "Search effects...") : "Search and replace...");
        m.addSubMenu (slot.empty() ? (wantInst ? "Choose instrument (by maker)" : "Choose effect (by maker)") : "Replace with (by maker)", choose);
    }
    if (wantInst)
    {
        juce::PopupMenu roots;
        static const char* N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int cur = ctl.P.pads[(size_t) s.a].instRoot;
        for (int note = 24; note <= 96; note += 12) roots.addItem (2000 + note, juce::String (N[note % 12]) + juce::String (note / 12 - 1) + " (" + juce::String (note) + ")", true, note == cur);
        m.addSubMenu ("Pad / root key plays note", roots);
    }
    m.addSeparator();
    m.addItem (7, "Scan for plug-ins / plug-in list...");
    juce::Component::SafePointer<juce::Component> tp (target);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [&ctl, s, list, tp] (int r)
    {
        if (r == 8) { if (tp) showPluginBrowser (ctl, s, tp.getComponent()); }
        else if (r == 1) { if (ctl.onOpenEditor) ctl.onOpenEditor (s); }
        else if (r == 2) ctl.toggleBypass (s);
        else if (r == 3) ctl.removePlugin (s);
        else if (r == 4) { if (ctl.onAutoSampleDialog) ctl.onAutoSampleDialog (s.a); }
        else if (r == 5) ctl.retryMissingPlugins();
        else if (r == 6 || r == 7) { if (ctl.onShowPluginList) ctl.onShowPluginList(); }
        else if (r >= 2000) ctl.setInstRoot (s.a, r - 2000);
        else if (r >= 1000 && r - 1000 < list.size())
        {
            if (ctl.setPlugin (s, list[r - 1000]) && ctl.onOpenEditor) ctl.onOpenEditor (s);
        }
    });
}

// ------------------------------------------------------------------ meters
void Meter::push (float l, float r)
{
    const auto now = juce::Time::getMillisecondCounter();
    L = juce::jmax (l, L * 0.86f); R = juce::jmax (r, R * 0.86f);
    const float p = juce::jmax (l, r);
    if (p >= pk || now - pkT > 1500) { pk = p; pkT = now; }
    if (p >= 0.985f) clipT = now;
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto txt = r.removeFromRight (44);
    auto pct = [] (float x) { return x <= 0 ? 0.0f : juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (x) + 60.0f) / 60.0f); };
    auto bar = [&] (juce::Rectangle<int> b, float v)
    {
        g.setColour (C::well); g.fillRoundedRectangle (b.toFloat(), 2.5f);
        g.setColour (C::line); g.drawRoundedRectangle (b.toFloat(), 2.5f, 1.0f);
        const int w = (int) (b.getWidth() * pct (v));
        juce::ColourGradient cg (juce::Colour (0xff3ecf6a), (float) b.getX(), 0, juce::Colour (0xffe5484d), (float) b.getRight(), 0, false);
        cg.addColour (0.66, juce::Colour (0xff3ecf6a)); cg.addColour (0.84, C::accent);
        g.setGradientFill (cg);
        g.fillRect (b.withWidth (w).reduced (1));
    };
    const int h = juce::jmax (4, (r.getHeight() - 4) / 2);
    bar (r.removeFromTop (h), L); r.removeFromTop (3); bar (r.removeFromTop (h), R);
    const bool clip = juce::Time::getMillisecondCounter() - clipT < 1200;
    g.setColour (clip ? juce::Colour (0xffe5484d) : C::mute); g.setFont (mono (10.5f));
    g.drawText (pk > 0.0005f ? juce::String (20.0f * std::log10 (pk), 1) : juce::String ("-inf"), txt, juce::Justification::centredRight);
}

void VuMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xffe9dcae)); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (0xff7a6b3a)); g.drawRoundedRectangle (r, 6.0f, 1.0f);
    const juce::Point<float> c (r.getCentreX(), r.getBottom() - 8);
    const float len = r.getHeight() * 0.72f;
    g.setFont (mono (8.0f)); g.setColour (juce::Colour (0xff2b2618));
    for (int db : { 0, -2, -4, -6, -10, -15, -20 })
    {
        const float a = juce::jmap ((float) db, -20.0f, 0.0f, -50.0f, 50.0f);
        const float rad = juce::degreesToRadians (a);
        const juce::Point<float> p1 (c.x + std::sin (rad) * len, c.y - std::cos (rad) * len);
        const juce::Point<float> p0 (c.x + std::sin (rad) * (len - 6), c.y - std::cos (rad) * (len - 6));
        g.drawLine ({ p0, p1 }, 1.0f);
        g.drawText (juce::String (db), juce::Rectangle<float> (p1.x - 10, p1.y - 11, 20, 9), juce::Justification::centred);
    }
    g.drawText ("GAIN REDUCTION", r.withTrimmedBottom (14).removeFromBottom (12), juce::Justification::centred);
    const float a = juce::degreesToRadians (juce::jmap (juce::jlimit (-20.0f, 0.0f, gr), -20.0f, 0.0f, -50.0f, 50.0f));
    g.setColour (juce::Colour (0xff111111));
    g.drawLine (c.x, c.y, c.x + std::sin (a) * len, c.y - std::cos (a) * len, 1.8f);
    g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7, 7);
}

void Card::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (C::plate); g.fillRoundedRectangle (r, 8.0f);
    g.setColour (C::line); g.drawRoundedRectangle (r, 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (C::accent); g.setFont (mono (11.5f, true));
        g.drawText (title.toUpperCase(), getLocalBounds().reduced (10, 6).removeFromTop (16), juce::Justification::centredLeft);
    }
}
} // namespace cx::ui
