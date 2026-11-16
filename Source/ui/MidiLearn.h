// CONTRXL 25 - MIDI learn: which on-screen controls can be linked, and the click-to-learn overlay
#pragma once
#include "ui/Widgets.h"

namespace cx::ui
{
// Stable id of a learnable control ("seq/btn:Copy#1", "fx/slider:Mix#3", ...). Empty if not learnable.
juce::String learnId (juce::Component* c);
// What the control under a click is, for MIDI learn. Returns false if it can't be linked.
bool learnTargetFor (juce::Component* c, juce::Point<int> screenPos, MidiTarget& out);
// Find the control with this id under root (or nullptr).
juce::Component* findLearnable (juce::Component* root, const juce::String& id);
// Every learnable control under root (for drawing the overlay).
void collectLearnables (juce::Component* root, juce::Array<juce::Component*>& out);

// Transparent layer over a window while MIDI learn is on: shows which controls are linked,
// and clicking a control arms it so the next note / CC from your controller links to it.
class LearnOverlay : public juce::Component, private juce::Timer
{
public:
    LearnOverlay (Controller& c, juce::Component& rootToCover) : ctl (c), root (rootToCover) { startTimerHz (4); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    Controller& ctl;
    juce::Component& root;
    bool blink = false;
    void timerCallback() override { if (isVisible()) { blink = ! blink; repaint(); } }
};
} // namespace cx::ui
