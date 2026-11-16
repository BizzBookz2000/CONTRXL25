// CONTRXL 25 - main window content: lays out the panels, owns the audio device, routes keys
#pragma once
#include "ui/Panels.h"
#include "ui/ChopEditor.h"
#include "ui/WindowStyle.h"
#include "ui/MidiLearn.h"
#include "ui/Mixer.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace cx::ui
{
// connects the audio device and MIDI inputs to the engine
class AudioHost : public juce::AudioIODeviceCallback, public juce::MidiInputCallback
{
public:
    explicit AudioHost (Engine& e) : engine (e) { midiBuf.ensureSize (4096); collector.reset (48000); }
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override;
    void audioDeviceStopped() override { engine.deviceStopped(); }
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override;
    // only this input plays notes / pitch bend (empty = every input). CCs and transport always pass,
    // so a pad controller or fader box can be used next to the keyboard.
    void setNoteFilter (const juce::String& identifier) { const juce::SpinLock::ScopedLockType l (lock); noteFilter = identifier; }
    juce::String getNoteFilter() const { const juce::SpinLock::ScopedLockType l (lock); return noteFilter; }
    juce::String lastSource() const { const juce::SpinLock::ScopedLockType l (lock); return lastSrc; }
private:
    Engine& engine;
    mutable juce::SpinLock lock;
    juce::String noteFilter, lastSrc;
    juce::MidiMessageCollector collector;
    juce::MidiBuffer midiBuf;
};

class MainComponent;

// a separate window for part of the UI (sequencer, keyboard, chop editor). Keys typed in it
// still play the instrument, exactly like in the main window.
class PopOutWindow : public juce::DocumentWindow
{
public:
    struct Holder : public juce::Component, public juce::DragAndDropContainer
    {
        explicit Holder (MainComponent& o);
        void resized() override { if (content != nullptr) content->setBounds (getLocalBounds()); if (learnLayer) learnLayer->setBounds (getLocalBounds()); }
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool) override;
        void mouseDown (const juce::MouseEvent&) override;
        void paint (juce::Graphics& g) override { g.fillAll (C::plate); }
        void setContent (juce::Component* c) { content = c; if (c) addAndMakeVisible (c); resized(); }
        MainComponent& owner;
        juce::Component* content = nullptr;
        std::unique_ptr<juce::Component> owned;
        std::unique_ptr<LearnOverlay> learnLayer;   // MIDI learn layer over this window
    };
    PopOutWindow (const juce::String& title, MainComponent& owner, std::function<void()> onClose);
    void closeButtonPressed() override { if (onClose) onClose(); }
    Holder* holder = nullptr;
    std::function<void()> onClose;
};

class MainComponent : public juce::Component, public Dialogs, public juce::DragAndDropContainer,
                      private juce::ChangeListener, private juce::Timer
{
    friend class PopOutWindow;
public:
    MainComponent();
    ~MainComponent() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override { releaseQwerty(); }
    void mouseDown (const juce::MouseEvent&) override;   // clicks anywhere give the keys back to the instrument
    bool confirmQuit();
    void openFiles (const juce::Array<juce::File>& files);   // song .zip or audio

    // Dialogs
    void openAudio (bool multiple, std::function<void (juce::Array<juce::File>)> cb) override;
    void openFile (const juce::String& title, const juce::String& wildcard, std::function<void (juce::File)> cb) override;
    void saveFile (const juce::String& title, const juce::String& defaultName, const juce::String& wildcard, std::function<void (juce::File)> cb) override;
    void chooseFolder (const juce::String& title, std::function<void (juce::File)> cb) override;
    void showAudioSettings() override;
    int inputChannels() override;
    void openChopWindow() override;
    void togglePopSequencer() override;
    void togglePopKeyboard() override;
    bool isSequencerPopped() const override { return seqWin != nullptr; }
    bool isKeyboardPopped() const override { return kbWin != nullptr; }
    void togglePopSection() override;
    bool isSectionPopped() const override { return tabsWin != nullptr; }
    void showMidiSettings() override;
    void showTab (const juce::String& name) override;
    void openPluginEditor (const SlotId& s);
    void showPluginList();
    void showAutoSampler (int pad);
    void toggleMidiLearn() override;
    void setUiScale (int percent) override;
    int  getUiScale() const override { return uiScale; }
    void applyUiScale();                // call once at start-up
    int  minimumHeight() const;         // the layout needs at least this much (the window scrolls if smaller)
    static constexpr int minimumWidth = 1280;
    std::function<void()> onLayoutChange;   // set by the scrolling window shell
    void releaseQwerty();
    static void setNoFocusRecursive (juce::Component& c);

private:
    Engine engine;
    Controller ctl { engine };
    AudioHost host { engine };
    juce::AudioDeviceManager devices;
    std::unique_ptr<juce::PropertiesFile> props;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastDir;
    juce::StringArray knownMidi;
    bool wasForeground = true;

    TopBar top { ctl, *this };
    Faceplate face { ctl, *this };
    Deck deck { ctl, *this };
    struct Tabs : public juce::TabbedComponent
    {
        Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) { addAndMakeVisible (pop); }
        std::function<void()> onChange;
        Btn pop { "Pop out section", "Put this whole section (sequencer, sound, effects, layers, song) in its own window" };
        void currentTabChanged (int, const juce::String&) override { if (onChange) onChange(); }
        void resized() override { juce::TabbedComponent::resized(); pop.setBounds (getWidth() - 150, 4, 146, 26); pop.toFront (false); }
    } tabs;
    // the big count-in number over the whole window (like the web version)
    struct CountOverlay : public juce::Component
    {
        CountOverlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
        int count = 0, kind = 0;
    } overlay;
    SequencerView seq { ctl };
    struct SeqHost : public juce::Component
    {
        SeqHost();
        void resized() override;
        void paint (juce::Graphics& g) override { g.fillAll (C::plate); }
        juce::Component* seq = nullptr;
        juce::Label msg; Btn dock { "Dock the sequencer here" };
    } seqHost;
    SoundPanel sound { ctl, *this };
    EffectsPanel effects { ctl, *this };
    LayersPanel layers { ctl, *this };
    SongPanel song { ctl, *this };
    MixerPanel mixer { ctl, *this };
    juce::TooltipWindow tips { this, 600 };

    std::map<int, int> qwertyDown;   // key code -> key index
    std::unique_ptr<PopOutWindow> seqWin, kbWin, chopWin, tabsWin, midiWin;
    std::unique_ptr<LearnOverlay> learnMain;
    std::map<PluginNode*, std::unique_ptr<juce::DocumentWindow>> editorWins;
    std::unique_ptr<PopOutWindow> pluginListWin;
    std::map<juce::String, bool> ccButtonDown;      // learned CC buttons fire once per press
    void updateLearnOverlays();
    std::vector<juce::Component*> learnRoots();
    void handleMapFire (const MidiTarget& t, bool isCC, int value);
    void saveMidiPrefs();
    int uiScale = 0;
    int lastMinH = 0;
    void checkLayout() { const int m = minimumHeight(); if (m != lastMinH) { lastMinH = m; if (onLayoutChange) onLayoutChange(); } resized(); }
    ChopEditor* chopEd = nullptr;
    void closeChopWindow();
    void placeWindow (PopOutWindow& w, const juce::String& key, int defW, int defH);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshAll();
    void saveDeviceState();
    void enableNewMidiInputs();
};
} // namespace cx::ui
