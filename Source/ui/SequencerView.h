// CONTRXL 25 - sequencer: patterns, pages, tracks (step grid), chain / song mode
#pragma once
#include "ui/Widgets.h"

namespace cx::ui
{
void showTimingBox (Controller& ctl, juce::Component& anchor);   // Rec timing... (tap test)
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (Controller& c) : ctl (c) {}
    static constexpr int ROW = 26;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick();
    int preferredHeight() const { return (int) ctl.P.tracks.size() * ROW + 4; }
private:
    Controller& ctl;
    int lastNow = -2;
    enum { NONE, PAINT, VOL, SELECT, MOVE } mode = NONE;
    int moveL = 0, moveS = 0, moveDl = 0, moveDs = 0;
    bool paintOn = true; int selL = 0, selS = 0;
    juce::Rectangle<int> stepRect (int lane, int i) const;
    int stepsX() const { return 136 + 70 + 12; }   // lane, M, S, R, L, volume
    bool hit (juce::Point<int> p, int& lane, int& step) const;
};

class SequencerView : public juce::Component
{
public:
    explicit SequencerView (Controller& c);
    void refresh();
    void tick() { grid.tick(); }
    std::function<void()> onPop;      // pop out / dock
    bool popped = false;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    Controller& ctl;
    juce::OwnedArray<Btn> patBtns, pageBtns, chainBtns;
    juce::ComboBox lenBox, bpmBox, padBox, chopBox;
    Btn bCopyPat { "Copy", "Copy this pattern (all tracks, length and tempo)" }, bPastePat { "Paste", "Paste into this pattern" },
        bDupPat { "Duplicate", "Copy this pattern into the next empty one and switch to it" }, bClearPat { "Clear", "Clear every track in this pattern" },
        bSelTool { "Select", "Select tool: drag over steps, then copy / paste / delete / transpose" },
        bSelCopy { "Copy", "Copy the selected steps" }, bSelPaste { "Paste", "Paste at the selection or the start of the page" },
        bSelDel { "Delete", "Clear the selected steps" }, bTrDn12 { "-12" }, bTrDn { "-1" }, bTrUp { "+1" }, bTrUp12 { "+12" },
        bArm { "Arm", "Arm this track for live recording" }, bAdd { "+ Track" }, bDel { "Delete" }, bDup { "Dup" },
        bDouble { "x2 Tracks", "Double the number of tracks" }, bClrTrk { "Clear", "Clear this track in this pattern" },
        bChainAdd { "+ Add", "Add the current pattern to the chain" }, bChainDel { "Remove" }, bChainL { "<" }, bChainR { ">" },
        bRepDn { "Rep -" }, bRepUp { "Rep +" }, bSong { "Song mode" },
        bPop { "Pop out", "Put the sequencer in its own window (e.g. on a second screen)" },
        bMute { "Mute", "Mute / unmute the selected track" },
        bSolo { "Solo", "Solo the selected track (only soloed tracks play)" },
        bLock { "Lock", "Lock the selected track: live recording never writes into it" },
        bRecAll { "Rec all armed", "OFF: a hit records on the armed track that plays that pad (or the next empty armed track). ON: every armed track records every hit, each with its own sound (layering)" },
        bTiming { "Rec timing...", "If your recorded hits land a step late or early: tap along with the click once and the app corrects it" },
        bSelTrk { "Select track", "Select every step of the selected track (then copy, move, transpose...)" },
        bChainClr { "Clear chain", "Remove every step of the song chain" };
    juce::Label lPat, lLen, lBpm, lPage, lTrack, lPad, lChop, lChain, lTracks;
    StepGrid grid { ctl };
    juce::Viewport view;
    void buildChain();
};
} // namespace cx::ui
