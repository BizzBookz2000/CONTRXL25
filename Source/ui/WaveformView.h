// CONTRXL 25 - sample window: waveform, chop regions, playhead, trim handles, manual chop editing
#pragma once
#include "ui/Widgets.h"

namespace cx::ui
{
class WaveformView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (Controller& c) : ctl (c) {}
    bool manual = false;              // manual chop: click adds, drag moves, right/Alt-click removes
    std::function<void()> onLoadRequest;   // double-click = load a sample onto the selected pad
    void refresh();                   // after model changes
    void tick();                      // playhead animation
    void zoomBy (float factor, float anchor = 0.5f);
    void resetZoom() { zoom = 1; start = 0; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override { peaksFor = nullptr; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    Controller& ctl;
    SamplePtr buf; std::vector<float> markers;
    float zoom = 1, start = 0, playhead = -1;
    const SampleData* peaksFor = nullptr; float peaksZoom = 0, peaksStart = 0; int peaksW = 0;
    std::vector<std::pair<float, float>> peaks;
    int dragMarker = -1; enum { D_NONE, D_TRIM_A, D_TRIM_B, D_MARKER, D_SCROLL, D_REGION } drag = D_NONE;
    int dragRegion = -1;
    float dragStartView = 0;

    juce::Rectangle<int> waveArea() const { return getLocalBounds().reduced (2).withTrimmedBottom (14); }
    float xToFrac (float x) const;
    float fracToX (float f) const;
    void buildPeaks();
    int regionAt (float f) const;
};
} // namespace cx::ui
