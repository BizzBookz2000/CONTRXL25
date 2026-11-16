// CONTRXL 25 - the pop-out chop window: just you and the sample
#pragma once
#include "ui/Panels.h"

namespace cx::ui
{
class ChopEditor : public Panel
{
public:
    ChopEditor (Controller&, Dialogs&);
    void refresh() override;
    void tick() override { wave.tick(); }
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void()> onClose;
private:
    WaveformView wave { ctl };
    juce::Label title, hint;
    juce::OwnedArray<Btn> eqBtns;
    Btn bEdit { "Edit chops", "ON: click adds a chop point, drag moves it, right-click removes it. OFF: click a region to play it" },
        bPlay { "Play", "Play the selected region (or the whole sample)" }, bPrev { "<", "Previous region (,)" }, bNext { ">", "Next region (.)" },
        bZoomOut { "-" }, bZoomIn { "+" }, bFit { "Fit" },
        bTrans { "Transients", "Chop at the drum hits" }, bClear { "Clear", "Remove all chop points" },
        bToPads { "Chops > pads" }, bToKeys { "Chops > keys" }, bExtract { "Extract..." }, bResetKeys { "Reset keys", "The keys play this pad's pitch again (removes chop-to-key assignments)" },
        bUndo { "Undo" }, bLoad { "Load..." }, bDone { "Done", "Close this window (Esc)" },
        bTrim { "Trim", "Trim the sample: drag the green handles, then Apply" }, bTrimOk { "Apply", "Commit the trim (" CX_MOD "+Z undoes)" },
        bTrimX { "Cancel", "Leave the sample untouched" };
    LSlider fadeIn { "Fade in", 0, 500, 1, [] (double v) { return juce::String ((int) v) + " ms"; } },
            fadeOut { "Fade out", 0, 2000, 1, [] (double v) { return juce::String ((int) v) + " ms"; } },
            gain { "Gain", -24, 12, 0.1, [] (double v) { return juce::String (v, 1) + " dB"; } };
    bool trimming() const { return ctl.trim.on && ctl.trim.pad == ctl.P.s.sel; }
};
} // namespace cx::ui
