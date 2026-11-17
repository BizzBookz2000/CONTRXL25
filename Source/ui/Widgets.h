// CONTRXL 25 - small custom controls: buttons, knobs, wheels, pads, meters
#pragma once
#include "ui/LookAndFeel.h"
#include "app/Controller.h"

namespace cx::ui
{
// a TextButton that looks "lit" when on. Text can have a second line ("PLAY\nRestart").
class Btn : public juce::TextButton
{
public:
    Btn (const juce::String& text = {}, const juce::String& tip = {}, std::function<void()> click = {})
    {
        setButtonText (text);
        setTooltip (tip);
        onClick = std::move (click);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        // stable name for MIDI learn (the visible text may change later, e.g. "Song mode: on")
        getProperties().set ("learnText", juce::StringArray::fromLines (text)[0].trim());
    }
    void setOn (bool on) { setToggleState (on, juce::dontSendNotification); }
    void setOnColour (juce::Colour c) { getProperties().set ("onColour", (juce::int64) c.getARGB()); repaint(); }
    // hardware style (like the web transport): lit = dark tint + coloured outline and text
    void setHw (bool hw) { getProperties().set ("hw", hw); }
};

// label + slider + value readout in one row
class LSlider : public juce::Component
{
public:
    LSlider (const juce::String& name, double lo, double hi, double step, std::function<juce::String (double)> fmt = {});
    juce::Slider slider;
    juce::Label label, value;
    std::function<void (double)> onChange;
    std::function<juce::String (double)> format;
    void set (double v);
    void resized() override;
    int labelWidth = 74, valueWidth = 64;
};

// the 8 pitch knobs (pseudocode 4): drag 0.3 st/px, Shift = fine, wheel +/-1, double-click = 0
class PitchKnob : public juce::Component
{
public:
    std::function<void (float)> onChange;
    std::function<void()> onGrab;
    void setValue (float v, const juce::String& padName, bool active) { value = v; name = padName; isActive = active; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    float value = 0, dragStart = 0; juce::String name; bool isActive = false;
};

// pitch wheel (springs back) and mod wheel
class Wheel : public juce::Component, private juce::Timer
{
public:
    Wheel (const juce::String& n, bool spring) : name (n), springs (spring) {}
    std::function<void (float)> onChange;
    void setValue (float v) { value = juce::jlimit (-1.0f, 1.0f, v); repaint(); }
    float getValue() const { return value; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::String name; bool springs; float value = 0;
    float at (const juce::MouseEvent& e) const { return juce::jlimit (-1.0f, 1.0f, -((float) e.y / (float) getHeight() * 2.0f - 1.0f)); }
    void timerCallback() override;
};

// one of the 8 pads in the current bank
class PadButton : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget
{
public:
    PadButton (Controller& c, int slot) : ctl (c), slotIndex (slot) {}
    int padIndex() const { return ctl.ui.bank * 8 + slotIndex; }
    int slot() const { return slotIndex; }
    void flash() { flashLevel = 1.0f; repaint(); }
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const auto s = d.description.toString();
        return s.startsWith ("pad:") || s.startsWith ("chop:") || s.startsWith ("lib:");
    }
    void itemDropped (const SourceDetails& d) override;
    std::function<void (int pad)> onLoadRequest, onSaveRequest;
private:
    Controller& ctl; int slotIndex; float flashLevel = 0; juce::uint32 lastHits = 0; bool dragging = false; int held = -1;
    void showMenu();
};

// the menu of a plug-in slot: choose / replace a VST, open its editor, bypass, remove
void showPluginMenu (Controller& ctl, const SlotId& s, juce::Component* target);
// searchable list of your VST3 plug-ins; picking one loads it into the slot (PluginBrowser.cpp)
void showPluginBrowser (Controller& ctl, const SlotId& s, juce::Component* target);

// stereo peak meter (top bar)
class Meter : public juce::Component
{
public:
    void push (float l, float r);
    void paint (juce::Graphics&) override;
private:
    float L = 0, R = 0, pk = 0; juce::uint32 pkT = 0, clipT = 0;
};

// sample level meter: the audio input (or, while resampling, what CONTRXL 25 plays), left / right, -60..0 dB,
// with a peak hold, a CLIP light that stays on until you click / tap the meter, and the threshold level marked
class SampleMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SampleMeter (Controller& c, bool compactStyle = false);
    void tick();                         // ~30 times a second from the panel that shows it
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;   // threshold on: drag sets the level (the blue line)
private:
    Controller& ctl;
    bool compact;
    juce::Rectangle<int> barsArea;
    void setThresholdAt (int x);
    float lvl[2] {}, hold[2] { 0, 0 };
    juce::uint32 holdT[2] {};
    bool clip = false, fromOut = false, noInput = false;
};

// LA-2A style gain reduction needle
class VuMeter : public juce::Component
{
public:
    void setGr (float db) { gr = gr * 0.7f + db * 0.3f; repaint(); }
    void paint (juce::Graphics&) override;
private:
    float gr = 0;
};

// a section card with a title
class Card : public juce::Component
{
public:
    explicit Card (const juce::String& t = {}) : title (t) {}
    juce::String title;
    void paint (juce::Graphics& g) override;
    // the inner area in the PARENT's coordinates (controls are siblings of the card)
    juce::Rectangle<int> content() const { return getBounds().reduced (10).withTrimmedTop (title.isEmpty() ? 0 : 20); }
};

// simple row layout helper: lay children left to right with fixed widths (0 = share the rest)
void layoutRow (juce::Rectangle<int> r, std::initializer_list<std::pair<juce::Component*, int>> items, int gap = 6);
} // namespace cx::ui
