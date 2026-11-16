// CONTRXL 25 - on-screen 25/32-key keyboard (mouse, QWERTY labels, MIDI lights, chop colours)
#pragma once
#include "ui/Widgets.h"

namespace cx::ui
{
class KeyboardView : public juce::Component, public juce::DragAndDropTarget
{
public:
    // drop a pad on a key = that key plays the pad; drop a chop = that key plays the chop
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        const auto s = d.description.toString();
        return s.startsWith ("pad:") || s.startsWith ("chop:");
    }
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails& d) override { const int k = keyAt (d.localPosition.toFloat()); if (k != dropKey) { dropKey = k; repaint(); } }
    void itemDragExit (const SourceDetails&) override { dropKey = -1; repaint(); }
    void itemDropped (const SourceDetails& d) override;
    explicit KeyboardView (Controller& c) : ctl (c) {}
    void light (int idx, int source, bool on);   // source: 0 mouse, 1 qwerty, 2 midi
    void clearLights() { for (auto& l : lit) l = 0; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    static const char* qwertyLabel (int i);
    int keyAtPoint (juce::Point<float> p) const { return keyAt (p); }
    juce::Rectangle<float> keyBounds (int i) const { auto L = layout(); return (i >= 0 && i < (int) L.size()) ? L[(size_t) i].r : juce::Rectangle<float>(); }
private:
    Controller& ctl;
    std::array<juce::uint8, 128> lit {};
    int mouseKey = -1, dropKey = -1;
    struct KeyRect { juce::Rectangle<float> r; bool black; };
    std::vector<KeyRect> layout() const;
    int keyAt (juce::Point<float> p) const;
    float velAt (int key, float y) const;
};
} // namespace cx::ui
