// CONTRXL 25 - application controller (message thread).
// Owns the Project, undo/redo, clipboard and UI state, turns user actions into
// model edits, and publishes snapshots to the audio engine. UI components call
// these methods and repaint when the controller broadcasts a change.
#pragma once
#include "engine/Engine.h"
#include "io/SongIO.h"
#include "app/PluginManager.h"
#include "io/Library.h"
#include <set>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace cx
{
struct StepSel { int l0 = 0, l1 = 0, s0 = 0, s1 = 0; };

// what a MIDI control is linked to (MIDI learn)
struct MidiTarget
{
    juce::String kind;   // "pad", "key", "knob", "bank", "btn", "slider"
    int n = 0;           // pad slot / key / knob / bank
    juce::String id;     // btn / slider id
    juce::String label;  // shown in the list
    bool operator== (const MidiTarget& o) const { return kind == o.kind && n == o.n && id == o.id; }
};

struct UIState
{
    int bank = 0, page = 0, lane = 0, chainSel = -1;
    int layerSel = -1, stemSel = -1;
    bool rec = false, qwerty = true, keysVisible = true;
    bool selTool = false; bool hasSel = false; StepSel sel;
    int exportBits = 24, exportBars = 1;
    bool bakePitch = false;
    float stretchPct = 100; bool keepPitch = true; float sampleBpm = 90;
    bool smpThr = true, smpCount = false; float smpThrDb = -30;
    int manualLearn = 0;   // 0 off, 1 = next pad hit receives the extract chop, 2 = next key
    int extractChop = 0;
    juce::String libCrate;   // crate selected in the library ("" = all crates)
    bool recAllArmed = false; // live recording: every armed track records every hit (layering), not only the pad's track
};

class Controller : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    explicit Controller (Engine& engine);
    ~Controller() override;

    Project P;
    UIState ui;
    TrimState trim;
    dsp::StemStore stems;
    SnapshotBuilder builder;
    Engine& engine;

    // ---- hooks for the UI
    std::function<void (int keyIdx, bool on)> onKeyLight;
    std::function<void (float)> onBend, onMod;
    std::function<void()> onMidiActivity;
    // a learned button / knob / slider / bank control arrived (UI resolves the id and acts)
    std::function<void (const MidiTarget&, bool isCC, int value)> onMapFire;
    std::function<void()> onMidiMapChanged;
    juce::String statusText = "READY", exportStatus;
    bool unsaved = false;
    bool busy = false; float busyProgress = 0; juce::String busyLabel;

    void status (const juce::String& s);
    void changed();                       // publish + broadcast
    void publish();                       // rebuild snapshot for the engine
    juce::String contextLine() const;
    SamplePtr displayBuffer (std::vector<float>* markers = nullptr);   // what the waveform shows (selected pad, processed)

    // ---- undo / redo
    void pushUndo (const juce::String& key = {});
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // ---- pads
    Pad& selPad() { return P.pads[(size_t) P.s.sel]; }
    void selectPad (int gi);
    void setBank (int b);
    void hitPad (int gi, float vel, bool held = false);   // held: a releasePad follows (VST instrument note length)
    void releasePad (int gi);
    void setPitch (int gi, float v, bool quiet = false);
    void copyPad(); void pastePad(); void clearPad(); void clearAllPads(); void clearKeys();
    void togglePadMute (int gi);
    void movePad (int from, int to, bool copy);
    void setPadVol (float v);

    // ---- keys
    void noteOn (int keyIdx, float vel, int tag);
    void noteOff (int keyIdx, int tag);
    void setOct (int o);
    void setMode (int m);
    void setKeyboard (int n);
    void setRoot (int r);
    void toggle (bool& flag, const juce::String& name);

    // ---- samples
    void loadFiles (const juce::Array<juce::File>& files, int startPad);
    void addLayers (const juce::Array<juce::File>& files);
    void startSampling (bool fromInput);
    void stopSampling (bool cancel);
    bool isSampling() const { return engine.captureState() != Engine::CAP_IDLE; }
    bool capturingInput() const { return captureFromInput; }
    void savePadWav (const juce::File& f);
    void reversePad();
    void normalizePad();
    void applyStretch();
    void matchBpm();

    // ---- chops
    void setMarkers (std::vector<float> m);
    void equalChops (int n);
    void transientChops();
    void chopsToPads();
    void chopsToKeys();
    void selectChop (int c);
    void previewChop (int c);   // -1 = whole sample
    void stepRegion (int dir);  // , and . keys
    void extractChop (int chop, int targetPad);
    void assignChopKey (int chop, int key);

    // ---- trim (non-destructive preview, then commit)
    void trimOpen(); void trimApply(); void trimCancel();
    void trimSet (float a, float b, float fi, float fo, float g);

    // ---- pad fx
    void editFx (const std::function<void (FxParams&)>& fn, const juce::String& undoKey = "fx");
    void setMuteGroup (int g); void setLinkGroup (int g);

    // ---- layers
    void editSlot (int layer, const std::function<void (SlotSettings&)>& fn, const juce::String& undoKey);
    void removeLayer (int i);
    void swapLayerWithMain (int i);

    // ---- stems
    void runSeparate();
    void setStem (int k, float v);   // k = -1 resets all
    void soloStem (int k);
    void applyStems();
    void stemsToPads();
    bool stemsReady() const;

    // ---- sequencer
    void setPattern (int p);
    void setPage (int p);
    void setPatternLength (int len);
    void setPatternBpm (int bpm);
    void setStep (int lane, int step, bool on);
    bool stepOn (int lane, int step) const;
    bool stepHasEv (int lane, int step) const;
    void selectLane (int l, bool audition);
    void setTrackPad (int l, int pad); void setTrackChop (int l, int chop);
    void toggleTrackMute (int l); void toggleTrackArm (int l); void setTrackVol (int l, float v);
    void toggleTrackSolo (int l); void toggleTrackLock (int l);
    int  trackForPad (int gi) const;    // the track that plays this pad in the current pattern (-1 = none)
    void armPadTrack (int gi);          // mixer: arm (or disarm) that track, finding an empty one if needed
    void addTrack(); void deleteTrack(); void duplicateTrack(); void doubleTracks(); void clearTrack();
    void copyPattern(); void pastePattern(); void duplicatePattern(); void clearPattern();
    void copySteps(); void pasteSteps(); void deleteSteps(); void transposeSteps (int n);
    void chainAdd(); void chainRemove(); void chainMove (int dir); void chainRep (int d); void chainSetPat (int i, int pat);
    void toggleSongMode();

    // ---- transport
    void play(); void stop(); void toggleRec();
    void setBpm (float v); void tapTempo();
    void setMasterVol (float v);
    void setLA (const std::function<void (LASettings&)>& fn);

    // ---- songs / export (run with a progress window)
    void saveSong (const juce::File& f);
    void loadSong (const juce::File& f);
    void newSong();
    std::vector<int> buildPlan() const;
    std::vector<int> activeTracks (const std::vector<int>& plan) const;
    void exportSongWav (const juce::File& f);
    void exportStems (const juce::File& folder);
    void exportTempoMap (const juce::File& f);
    void exportChop (const juce::File& f);
    void exportAllChops (const juce::File& folder);

    // ---- library
    Library library;                                         // the sample library folder (crates = sub-folders)
    void addToLibrary (const juce::String& crate);           // selected pad -> crate ("" = top of the library)
    void addChopsToLibrary (const juce::String& crate);
    void libToPad (int index, int pad);
    void libToPads (const std::vector<int>& indices);       // fill pads from the selected one
    void libRemove (int index);
    void libPreview (int index);
    void libImport (const juce::Array<juce::File>& files, const juce::String& crate);
    void libAddCrate (const juce::String& path);
    void libRenameCrate (const juce::String& path, const juce::String& newName);
    void libDeleteCrate (const juce::String& path);          // its samples move up to the parent crate
    void libSave();
    void libRefresh (bool quiet = false);
    void libSetRoot (const juce::File& dir);
    void libSaveCrate (const std::vector<int>& indices, const juce::File& folder);
    void libBackup (const juce::File& zip);
    void libRestore (const juce::File& zip);
    void bounceSong (bool toLibrary);                         // render the song onto a pad or into the library

    // ---- more editing (web parity)
    void resetKeys();
    void layerFromPad (int gi);
    void selectTrackSteps();
    void moveSteps (int dl, int ds, bool copy);
    void chainClear();

    // ---- MIDI learn
    std::map<juce::String, MidiTarget> midiMap;               // "n60" / "c21" -> target
    bool midiLearn = false;
    std::optional<MidiTarget> learnTarget;                    // armed control waiting for MIDI
    juce::String lastMidi = "none yet";
    void setMidiLearn (bool on);
    void armLearn (const MidiTarget& t);
    void removeMidiLink (const juce::String& sig);
    void clearMidiLinks();
    void pushMidiMap();                                       // tables -> engine
    juce::var midiMapToVar() const;
    void midiMapFromVar (const juce::var& v);
    static juce::String sigName (const juce::String& sig);   // "Note 60" / "CC 21"
    juce::String midiSigFor (const MidiTarget& t) const;      // "" if not linked

    // ---- VST plug-ins + mixer
    PluginManager plugins;
    std::function<void (PluginNode*)> onPluginClosing;          // UI closes the editor before a plug-in goes away
    std::function<void (const SlotId&)> onOpenEditor;           // UI hooks (set by the main window)
    std::function<void (int pad)> onAutoSampleDialog;
    std::function<void()> onShowPluginList, onShowMixer;
    PluginSlot& slotRef (const SlotId& s);
    PluginNode* pluginNode (const SlotId& s) { return engine.pluginAt (s); }
    bool setPlugin (const SlotId& s, const juce::PluginDescription& d);
    juce::StringArray recentPlugins;                            // identifier strings, newest first (saved in settings)
    // a .vst3 (or, on macOS, a .component) dropped on a slot / pad: added to the plug-in list, then loaded
    bool setPluginFromFile (const SlotId& s, const juce::String& path);
    static bool isPluginFile (const juce::String& path)
    {
       #if JUCE_MAC
        if (path.endsWithIgnoreCase (".component")) return true;   // Audio Unit bundle
       #endif
        return path.endsWithIgnoreCase (".vst3");
    }
    void removePlugin (const SlotId& s);
    void toggleBypass (const SlotId& s);
    void pullPluginStates();                                    // live plug-in states -> model (before save / copy)
    PluginSetPtr clonePlugins (double sampleRate, int block);  // for offline renders
    juce::String slotStatus (const SlotId& s) const;            // "", "missing", "failed"
    void setPadPan (int gi, float v);
    void setPadSend (int gi, int k, float v);
    void setPadVolAt (int gi, float v);
    void toggleSolo (int gi);
    void setReturnLevel (int k, float v);
    void setInstRoot (int gi, int note);
    struct AutoSample
    {
        int pad = 0;                  // the instrument pad
        bool toPads = false;          // false: one sample, one chop per key (keys mode); true: one note per pad
        int startNote = 48, count = 25, step = 1, target = 1, vel = 100;
        double noteSec = 1.5, tailSec = 2.0;
    };
    void autoSample (const AutoSample& a);
    void retryMissingPlugins() { failedIids.clear(); changed(); }
    void releaseAllPlugins();                                   // at exit, after the audio device is closed

    // ---- MIDI transport CC (debounced) -> action
    void transportAction (int which);   // 1 play, 2 stop, 3 rec, 4 loop, 5 metro, 6 tap

private:
    std::vector<Project> undoStack, redoStack;
    juce::String lastUndoKey; juce::uint32 lastUndoTime = 0;
    std::vector<SnapshotPtr> releasePool;
    std::optional<Pad> padClip;
    struct PatClip { int len, bpm; std::vector<std::optional<PatternData>> pd; };
    std::optional<PatClip> patClip;
    struct StepCell { uint8_t on = 0; float semi = 0; int16_t chop = -2; };
    std::vector<std::vector<StepCell>> stepClip;
    std::vector<double> taps;
    std::map<int, juce::uint32> ccLast; std::map<int, bool> ccDown;
    int captureTarget = 0; bool captureFromInput = true;
    SamplePtr previewSample;
    std::map<int, PluginNode*> freshNodes;   // created by setPlugin, waiting for syncPlugins
    std::set<int> failedIids;
    void syncPlugins();
    void freshPluginCopies (Pad& p);
    bool ccPress (int cc, int value);
    std::unique_ptr<juce::Thread> stemThread;

    void timerCallback() override;
    void handleEvent (const EngineEvent& e);
public:
    void applyRecHit (const EngineEvent& e);   // public for the tests
private:
    void finishCapture();
    SamplePtr prepBuf (SamplePtr b) const;
    int segCount (const Pad& p) const { return p.numChops(); }
    PatternData* curData (int lane);
};
} // namespace cx
