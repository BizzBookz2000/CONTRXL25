// CONTRXL 25 - the main panels: top bar, faceplate, deck, and the bottom tabs
#pragma once
#include "ui/Widgets.h"
#include "ui/WaveformView.h"
#include "ui/KeyboardView.h"
#include "ui/SequencerView.h"

namespace cx::ui
{
// file dialogs, implemented by the main window
struct Dialogs
{
    virtual ~Dialogs() = default;
    virtual void openAudio (bool multiple, std::function<void (juce::Array<juce::File>)> cb) = 0;
    virtual void openFile (const juce::String& title, const juce::String& wildcard, std::function<void (juce::File)> cb) = 0;
    virtual void saveFile (const juce::String& title, const juce::String& defaultName, const juce::String& wildcard, std::function<void (juce::File)> cb) = 0;
    virtual void chooseFolder (const juce::String& title, std::function<void (juce::File)> cb) = 0;
    virtual void showAudioSettings() = 0;
    virtual int inputChannels() = 0;
    // pop-out windows
    virtual void openChopWindow() = 0;
    virtual void togglePopSequencer() = 0;
    virtual void togglePopKeyboard() = 0;
    virtual bool isSequencerPopped() const = 0;
    virtual bool isKeyboardPopped() const = 0;
    virtual void togglePopSection() = 0;           // the whole bottom section (sequencer, sound, effects ...)
    virtual bool isSectionPopped() const = 0;
    // UI size (percent, 0 = fit the screen automatically)
    virtual void showMidiSettings() = 0;
    virtual void showTab (const juce::String& name) = 0;
    // controller profiles: 0 none, 1 "25 keys IV" (Akai MPK mini IV), 2 "25 keys 3" (Akai MPK mini 3):
    // pads on their channel, knobs -> pitch knobs (+ transport on the IV)
    virtual void setControllerProfile (int profile) = 0;
    virtual int controllerProfile() const = 0;
    virtual void showMpkSetup() = 0;
    virtual void askCustomKeys() = 0;              // "Your own keyboard...": type how many keys it has
    virtual void toggleMidiLearn() = 0;
    virtual void setUiScale (int percent) = 0;
    virtual int  getUiScale() const = 0;
};

// small pop-up to copy a chop to another pad or assign it to a key
void showExtractBox (Controller& ctl, juce::Component& anchor);

class Panel : public juce::Component
{
public:
    Panel (Controller& c, Dialogs& d) : ctl (c), dlg (d) {}
    virtual void refresh() {}
    virtual void tick() {}
protected:
    Controller& ctl; Dialogs& dlg;
    template <typename... T> void show (T&... comps) { (addAndMakeVisible (comps), ...); }
};

// ------------------------------------------------------------------ top bar
class TopBar : public Panel
{
public:
    TopBar (Controller&, Dialogs&);
    void refresh() override; void tick() override;
    void resized() override; void paint (juce::Graphics&) override;
    void flashMidi() { ledT = juce::Time::getMillisecondCounter(); }
private:
    Btn bSettings { "Audio / MIDI", "Choose the audio output, audio input and MIDI keyboard" },
        bUndo { "Undo", "Undo (" CX_MOD "+Z)" }, bRedo { "Redo", "Redo (" CX_REDO ")" },
        bQwerty { "QWERTY", "Play the keys from your computer keyboard: the Z-M and Q-I rows" },
        bPanic { "Panic", "Stop every sound" },
        bLearn { "MIDI Learn", "Link any pad, key, knob, button or slider to your controller: click this, click a control, then press / move the control on your controller" },
        bMidi { "MIDI...", "MIDI settings: learned links, which input plays notes, low key, built-in transport CCs, MIDI monitor" };
    bool showTag = true;
    juce::ComboBox kbBox, sizeBox;
    int kbKeys = -1;
    juce::Slider master; juce::Label lMaster, lKb, lSize;
    Meter meter;
    juce::uint32 ledT = 0;
};

// ------------------------------------------------------------------ faceplate: transport, display, knobs, sample window
class Faceplate : public Panel
{
public:
    Faceplate (Controller&, Dialogs&);
    void refresh() override; void tick() override;
    void resized() override; void paint (juce::Graphics&) override;
private:
    Btn bLoop { "LOOP\nPattern" }, bMetro { "METRO\nClick" }, bTempo { "TEMPO\nTap" },
        bPlay { "PLAY\nRestart" }, bRec { "REC\nLive rec" }, bStop { "STOP\nClear" },
        bCount { "Count-in", "4 beats of click before recording starts" },
        bQuant { "Quantize", "ON: recorded hits snap to the 16th grid. OFF: they keep your timing" };
    juce::Slider bpm; juce::Label lBpm;
    std::array<PitchKnob, 8> knobs;
    WaveformView wave { ctl };
    SampleMeter smpMeter { ctl, true };   // input level, right above Sample / Resample
    // sampling threshold, next to the meter (same setting as Sound > Sampling)
    Btn bThr { "Threshold", "ON: Sample waits until the input rises above the level (the blue line on the meter - drag on the meter to move it)" };
    LSlider thrLevel { "Level", -60, 0, 1, [] (double v) { return juce::String ((int) v) + " dB"; } };
    Btn bPrev { "<", "Previous region" }, bNext { ">", "Next region" }, bPlaySmp { "Play", "Play the selected region (or the whole sample)" },
        bTrim { "Trim", "Trim the sample: drag the green handles, then Apply" }, bTrimOk { "Apply", "Commit the trim (" CX_MOD "+Z undoes)" },
        bTrimX { "Cancel", "Leave the sample untouched" }, bZoomIn { "+", "Zoom in (mouse wheel)" }, bZoomOut { "-", "Zoom out" },
        bFit { "Fit", "Show the whole sample" },
        bChopWin { "Chop window", "Pop the sample out into its own big window to chop it" },
        bLoad { "Load", "Load audio files onto this pad (several files fill the next pads)" },
        bAddLay { "+ Samples", "Add audio files as layers that play together with this pad" },
        bSample { "Sample", "Record from the audio input into this pad" },
        bResample { "Resample", "Record what CONTRXL 25 is playing into this pad" },
        bSaveWav { "Save .wav", "Save this pad's sound as a .wav file" },
        bTrans { "Transients", "Chop at the drum hits" }, bClearChops { "Clear", "Remove the chop points" },
        bManual { "Manual chop", "Click to add chop points, drag to move them, right-click to remove" },
        bToPads { "Chops > pads", "Put every chop on its own pad, starting at the next pad" },
        bToKeys { "Chops > keys", "Put the chops on the keys: C is chop 1, C# chop 2..." },
        bExtract { "Extract...", "Copy one chop to another pad, or put it on a key" },
        bCopy { "Copy pad" }, bPaste { "Paste pad" }, bClear { "Clear pad" },
        bToLib { "+ Library", "Save this sample in the library (stored in your song files)" };
    juce::OwnedArray<Btn> eqBtns;
    LSlider fadeIn { "Fade in", 0, 500, 1, [] (double v) { return juce::String ((int) v) + " ms"; } },
            fadeOut { "Fade out", 0, 2000, 1, [] (double v) { return juce::String ((int) v) + " ms"; } },
            gain { "Gain", -24, 12, 0.1, [] (double v) { return juce::String (v, 1) + " dB"; } };
    juce::Label padTitle;
    juce::Rectangle<int> displayArea;
    void stepRegion (int dir);
    void openExtract();
};

// ------------------------------------------------------------------ keyboard section (can pop out into its own window)
class KeyboardPanel : public Panel
{
public:
    KeyboardPanel (Controller&, Dialogs&);
    void refresh() override;
    void resized() override; void paint (juce::Graphics&) override;
    KeyboardView keys { ctl };
    Wheel pitchWheel { "Pitch", true }, modWheel { "Mod", false };
    bool drawBackground = false;      // true when it lives in its own window
private:
    Btn bOctDn { "Octave <" }, bOctUp { "Octave >" }, bPop { "Pop out", "Put the keyboard in its own window" };
    juce::Label lOct;
};

// ------------------------------------------------------------------ deck: banks, pads (+ the keyboard when docked)
class Deck : public Panel
{
public:
    Deck (Controller&, Dialogs&);
    void refresh() override; void tick() override;
    void resized() override; void paint (juce::Graphics&) override;
    KeyboardPanel kb { ctl, dlg };
    bool keyboardDocked() const { return kb.getParentComponent() == this; }
private:
    juce::OwnedArray<Btn> banks;
    juce::OwnedArray<PadButton> pads;
    Btn bKeys { "1 - KEYS\nPitch across keys" }, bChop { "2 - CHOP\nKeys play chops" },
        bPadKeys { "PADS\nA pad per key", "Keys play pads: each key plays a whole pad (fill it with Pads > keys, or drag a pad onto a key)" },
        bPadsToKeys { "Pads > keys", "Put your pads on the keys in order: key 1 = the first pad with a sound, key 2 the next... up to the number of keys" },
        bGate { "Gate", "ON: a note stops when you let go. OFF: the sample plays out" },
        bVel { "Vel", "Velocity changes the level" },
        bChoke { "Choke", "ON: a new hit cuts off what you are playing. OFF: hits ring together" },
        bClrPads { "Clear pads", "Remove the samples from all 64 pads (" CX_MOD "+Z undoes)" },
        bClrKeys { "Clear keys", "Remove every chop-to-key assignment" },
        bHideKeys { "Hide keyboard" };
    juce::ComboBox rootBox; juce::Label lRoot, lDocked;
};

// ------------------------------------------------------------------ bottom tabs
class SoundPanel : public Panel
{
public:
    SoundPanel (Controller&, Dialogs&);
    void refresh() override; void tick() override; void resized() override;
private:
    Card cFx { "Pad FX" }, cLa { "LA-2A leveling amplifier (master)" }, cSmp { "Sampling" }, cStr { "Time stretch" };
    Btn fOff { "Off" }, fLp { "Lo-pass" }, fBp { "Band-pass" }, fHp { "Hi-pass" }, bLofi { "Lo-Fi: off", "Crush the bit depth and sample rate of this pad" },
        bRev { "Reverse" }, bNorm { "Normalize", "Raise this sample so its loudest peak sits at -1 dB" },
        bAutoNorm { "Auto-normalize", "Normalize new samples when they are loaded or recorded" },
        laOn { "On" }, laComp { "Compress" }, laLim { "Limit" },
        sThr { "Threshold", "ON: recording waits until sound rises above the level" }, sCount { "Count-in", "4 beats of click, then sampling starts" },
        tKeep { "Keep pitch", "ON: WSOLA stretch keeps the pitch. OFF: tape-style resample (pitch follows)" },
        tMatch { "Match BPM", "Stretch the sample from its own BPM to this pattern's tempo" }, tApply { "Apply stretch" },
        tHalf { "Half speed", "Stretch to 200% (twice as long) and apply" }, tDouble { "Double speed", "Stretch to 50% (half as long) and apply" };
    LSlider cut { "Cutoff", 0, 1000, 1 }, res { "Resonance", 0, 100, 1 }, padVol { "Pad vol", 0, 1.5, 0.01, [] (double v) { return juce::String ((int) std::round (v * 100)) + "%"; } },
            laPr { "Peak red.", 0, 100, 1, [] (double v) { return juce::String ((int) v); } }, laGain { "Gain", 0, 100, 1, [] (double v) { return juce::String ((int) v); } },
            thrDb { "Level", -60, 0, 1, [] (double v) { return juce::String ((int) v) + " dB"; } },
            strPct { "Length", 25, 400, 1, [] (double v) { return juce::String ((int) v) + "%"; } },
            smpBpm { "Sample BPM", 40, 240, 0.5, [] (double v) { return juce::String (v, 1); } };
    juce::ComboBox bits, rate, retro, mg, lg;
    juce::Label lBits, lRate, lRetro, lMg, lLg, retroDesc, laGr, smpInfo, strInfo;
    SampleMeter smpMeter { ctl };
    VuMeter vu;
};

class EffectsPanel : public Panel
{
public:
    EffectsPanel (Controller&, Dialogs&);
    void refresh() override; void resized() override;
private:
    struct Ctl { juce::String key; float FxParams::* m; std::unique_ptr<LSlider> s; };
    struct Module { bool FxParams::* on; std::unique_ptr<Card> card; std::unique_ptr<Btn> toggle; std::vector<Ctl> ctls; };
    std::vector<Module> mods;
    juce::Label hint;
};

class LayersPanel : public Panel
{
public:
    LayersPanel (Controller&, Dialogs&);
    void refresh() override; void resized() override;
private:
    struct Row : public juce::Component
    {
        Row (Controller& c, int layerIndex);
        Controller& ctl; int li;
        juce::Label name; LSlider vol { "Vol", 0, 1.5, 0.01, [] (double v) { return juce::String ((int) std::round (v * 100)) + "%"; } },
            pitch { "Pitch", -36, 36, 1, [] (double v) { return fmtSt ((float) v); } }, fc { "Cutoff", 0, 1000, 1 }, pan { "Pan", -1, 1, 0.01, [] (double v) { return juce::String (v, 2); } };
        juce::ComboBox ft, rm; Btn swap { "Edit", "Swap this layer with the main sample so chops, trim and stretch work on it" }, del { "Remove" };
        void update();
        void resized() override;
        void paint (juce::Graphics&) override;
    };
    Card cLayers { "Layers (play together with the pad)" }, cStems { "Stem separation (estimated, DSP)" };
    juce::OwnedArray<Row> rows;
    Btn bAdd { "+ Samples", "Add audio files as layers" }, bFromPad { "+ From pad", "Add another pad's sample as a layer of this pad" };
    juce::ComboBox stemSlot; juce::Label lSlot, stemInfo;
    Btn bSep { "Separate stems" }, bApply { "Apply to sample" }, bToPads { "Stems > pads" }, bReset { "Reset mix" };
    std::array<std::unique_ptr<LSlider>, 4> stemS; std::array<Btn, 4> solo;
    int builtFor = -1, builtCount = -1;
};

class SongPanel : public Panel, private juce::ListBoxModel, public juce::FileDragAndDropTarget
{
public:
    SongPanel (Controller&, Dialogs&);
    void refresh() override; void resized() override;
private:
    Card cSong { "Song" }, cExport { "Export (aligned for your DAW)" }, cLib { "Library" };
    juce::TextEditor songName;
    Btn bNew { "New song" }, bLoad { "Load song..." }, bSave { "Save song..." },
        bSongWav { "Song .wav", "Render the song (chain in song mode, otherwise the current pattern)" },
        bStems { "Stems + mix", "Render every active track as its own .wav, all the same length, plus the mix" },
        bTmap { "Tempo map .mid" }, bChop { "Selected chop .wav" }, bChops { "All chops..." },
        bBake { "Bake pad pitch", "Apply the pad pitch knob to exported chops / pad .wav" },
        bLibToPad { "To pad" }, bLibDel { "Remove" }, bLibAdd { "+ From pad" },
        bLibFolder { "Open folder", "Show the library folder (kept between sessions)" }, bCrate { "+ Crate", "Make a new crate" },
        bLibPlay { "Play", "Hear the selected sample (without loading it)" },
        bLibPads { "Onto pads", "Load the selected samples onto the selected pad and the ones after it" },
        bLibChops { "+ Its chops", "Save every chop of the selected pad into the library as its own sample" },
        bLibMore { "More...", "Import files / folders, rename / delete / save a crate, back up or restore the whole library" },
        bBounce { "Bounce > pad", "Render the song (or pattern) onto the next empty pad, ready to chop or resample" },
        bBounceLib { "Bounce > library", "Render the song (or pattern) into the library (crate Bounces)" },
        bPadLib { "Pad > library", "Add the selected pad's sample to the selected crate" },
        bLibRefresh { "Refresh", "Read the library folder again (picks up files you copied in with " CX_FILEMGR ")" },
        bLibSave { "Save", "Write the samples you added in the app into their crate folders (also happens when you close the app)" };
    juce::StringArray cratePaths;
    juce::ComboBox bitsBox, barsBox, crateBox, sortBox; juce::Label lBits, lBars, lName, status, lCrate;
    juce::TextEditor search;
    std::vector<int> selectedLib() const;
    juce::String currentCrate() const { return ctl.ui.libCrate; }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
public:
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
private:
    void moreMenu();
    juce::ListBox libList { "lib", this };
    std::vector<int> libRows;
    int getNumRows() override { return (int) libRows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
};
} // namespace cx::ui
