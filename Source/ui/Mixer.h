// CONTRXL 25 - mixer: a channel strip per pad (8 per bank), 2 send returns and the master,
// each with VST3 plug-in slots
#pragma once
#include "ui/Panels.h"

namespace cx::ui
{
// a button showing one plug-in slot; click = editor (or the menu when empty), right-click = menu
class PluginSlotButton : public Btn, public juce::FileDragAndDropTarget
{
public:
    bool isInterestedInFileDrag (const juce::StringArray& files) override { for (auto& f : files) if (Controller::isPluginFile (f)) return true; return false; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    PluginSlotButton (Controller& c, SlotId s, juce::String emptyText);
    void setSlot (SlotId s) { slot = s; update(); }
    void update();
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;
    SlotId slot;
private:
    Controller& ctl;
    juce::String emptyText;
};

class StripMeter : public juce::Component
{
public:
    void push (float v) { level = juce::jmax (v, level * 0.82f); if (v >= 0.985f) clipT = juce::Time::getMillisecondCounter(); repaint(); }
    void paint (juce::Graphics&) override;
private:
    float level = 0; juce::uint32 clipT = 0;
};

class ChannelStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Kind { PadStrip, ReturnStrip, MasterStrip };
    ChannelStrip (Controller& c, Dialogs& d, Kind k, int index);   // PadStrip: index = slot 0..7; ReturnStrip: bus
    void refresh();
    void tick();
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int pad() const;
private:
    Controller& ctl; Dialogs& dlg;
    Kind kind; int index;
    juce::Label name;
    std::unique_ptr<PluginSlotButton> inst;
    juce::OwnedArray<PluginSlotButton> inserts;
    Btn bFx { "Pad FX", "The built-in effects of this pad (opens the Effects tab)" },
        bMute { "M", "Mute this pad" }, bSolo { "S", "Solo this pad" },
        bArm { "R", "Arm the track that plays this pad (in this pattern) for live recording" },
        bLock { "L", "Lock the track that plays this pad: live recording won't write into it" };
    juce::Slider sendA, sendB, pan, fader;
    juce::Label lA, lB, lPan, lVal;
    StripMeter meter;
    void knob (juce::Slider& s, juce::Label& l, const juce::String& text, double lo, double hi, double def);
};

class MixerPanel : public Panel
{
public:
    MixerPanel (Controller&, Dialogs&);
    void refresh() override;
    void tick() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::OwnedArray<Btn> banks;
    juce::OwnedArray<ChannelStrip> strips;
    Btn bPlugins { "Plug-ins...", "Scan for VST3 plug-ins and see the list" };
    juce::Label title, hint;
};
} // namespace cx::ui
