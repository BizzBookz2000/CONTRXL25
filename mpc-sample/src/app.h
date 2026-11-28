// CONTRXL 25 - MPC Sample edition: the instrument (screens, controls, lights).
#pragma once
#include "engine.h"
#include "fileio.h"
#include "gfx.h"
#include "hw.h"
#include <deque>
#include <thread>

namespace cx
{
// MPC Sample control surface (measured map, see docs/HARDWARE-NOTES.md)
namespace S
{
    enum Btn
    {
        KNOBFX = 0, PADFX = 1, SAMPLE = 6, SEQ = 7, ERASE = 9, NOTEREPEAT = 11, B1 = 12, B2 = 13, B3 = 14,
        PADBANK = 35, SAMPLESELECT = 36, LEVELS16 = 40, MUTE = 43, SHIFT = 49, TAPTEMPO = 53, PLUS = 54, MINUS = 55,
        SAMPLEREC = 73, LOOP = 74, CHOP = 75, SEQREC = 80, STOP = 81, PLAY = 82, PUSH = 111
    };
    constexpr int CC_K1 = 16, CC_K2 = 17, CC_K3 = 18, CC_VOLUME = 19, CC_FADER = 20, CC_WHEEL = 100;
    extern const uint8_t PADNOTE[16];   // channel-10 note of pads 1..16
}

enum class Page { Main, PadEdit, Sample, Chop, Seq, Song, Mixer, Browser, Menu, Open, Settings, About };
enum KeyMode { KM_PADS = 0, KM_KEYS = 1, KM_CHOP = 2 };

struct Prefs
{
    int faderMode = 0;          // 0 pad level, 1 pad filter, 2 master volume
    int midiMode = 0;           // 0 keyboard (CONTRXL keys), 1 pads (note 36 = A1)
    float recOffsetMs = 0;
    int input = 1;              // 1 line, 2 mic
    int sampleSrc = 0;          // 0 input, 1 resample
    bool thrOn = false; float thrDb = -30;
    float monitor = 0;          // 0 off .. 1
    float inputGainDb = 0;
    int repeatIdx = 3;          // index into REPEAT_RATES
    std::string lastSong, browseDir;
    bool autoAudition = true;
};

struct Row   // one line of a parameter list
{
    std::string label;
    std::function<std::string()> value;
    std::function<void (int dir, bool fine)> adjust;   // nullptr = action row
    std::function<void()> action;
};

class App
{
public:
    App (Hw& hw, Engine& engine, const std::string& cardDir);
    ~App();
    void start();
    void onMidi (const MidiMsg& m);
    void update();                         // every main-loop pass
    bool wantsDraw() const;
    void draw (Canvas& c);
    bool running() const { return ! quit; }
    void lightsOff();
    void saveOnExit();

    // simulator hooks
    void simButton (int note, bool down) { button (note, down); }
    void simPad (int pad, int vel) { if (vel > 0) padDown (pad, vel); else padUp (pad); }
    void simCC (int cc, int v) { control (cc, v); }
    Project& project() { return P; }
    void setPage (Page p);
    Page page() const { return pg; }
    bool busy() const { return jobRunning.load(); }

private:
    Hw& hw; Engine& eng;
    std::string card, root, songsDir, samplesDir, recDir, exportDir;
    Project P;
    SnapshotBuilder builder;
    std::deque<SnapshotPtr> keepAlive;
    bool snapDirty = true, quit = false, splash = true;
    double splashUntil = 0, lastDraw = 0;
    std::string songPath;          // file the song was loaded from / saved to
    bool unsaved = false;
    Prefs prefs;
    std::unique_ptr<Project> undoP, redoP;

    // surface state
    Page pg = Page::Main, prevPg = Page::Main;
    int bankPage = 0;              // 0..3: pads show banks A+B, C+D, E+F, G+H
    KeyMode km = KM_PADS;
    int oct = 0, chopPage = 0;
    bool shift = false, eraseHeld = false, muteHeld = false, repeatHeld = false;
    std::array<bool, 16> padHeld {};
    std::array<double, 16> padHeldAt {};
    std::array<int, 128> ccLast; std::array<bool, 128> ccKnown {};
    float hwVolume = 0.8f;
    std::array<float, NPADS> flash {};
    float lvIn[2] {}, lvOut[2] {}; std::array<float, NPADS> lvPad {}; double lastLv = 0;
    std::array<uint32_t, NPADS> lastHits {};
    double stopTapAt = 0;
    std::vector<double> taps;

    // pages
    int listSel = 0, listTop = 0, editTab = 0;
    int seqBar = 0, seqTrackScroll = 0;
    bool stepMode = true;
    int chopSel = 0, songSel = 0, mixSel = 0;
    std::string browseDir; std::vector<io::DirEntry> browseList; int browseSel = 0;
    std::vector<std::string> openList;
    std::string message; double messageUntil = 0;
    std::string confirmText; std::function<void()> confirmYes;
    std::unique_ptr<Pad> padClip;

    // waveform cache
    const SampleData* waveSrc = nullptr; int waveW = 0; std::vector<std::pair<float, float>> wave;

    // background jobs (load/save/export) - the UI keeps running
    std::thread job; std::atomic<bool> jobRunning { false }; std::atomic<float> jobProgress { 0 };
    std::string jobTitle; std::function<void()> jobDone; std::mutex jobLock;
    void runJob (const std::string& title, std::function<std::function<void()>()> work);

    // LEDs
    std::array<std::array<uint8_t, 3>, 17> ledPad; std::array<bool, 17> ledPadKnown {};
    std::array<int, 128> ledCC; double lastLeds = 0;
    void sendLeds (bool force = false);
    void ledButton (int note, int value);
    void padColour (int i, int r, int g, int b);

    // input
    void button (int note, bool down);
    void control (int cc, int value);
    void padDown (int p, int vel);
    void padUp (int p);
    void padPressure (int p, int value);
    void externalNote (int note, int vel, bool on);
    void wheel (int delta);
    void plusMinus (int dir);
    void softKey (int k);
    void knob (int k, int delta);

    // actions
    void say (const std::string& s, double seconds = 2.5);
    void touch() { snapDirty = true; unsaved = true; }
    void pushUndo();
    void undo();
    void select (int gi);
    int sel() const { return P.s.sel; }
    Pad& selPad() { return P.pads[(size_t) P.s.sel]; }
    void play (bool fromStart);
    void stop();
    void recordButton();
    void tapTempo();
    void sampleRecord();
    void finishCapture();
    int nextEmptyPad (int from) const;
    void setPadSample (int gi, SamplePtr s, const std::string& name);
    void addRecordedHit (const EngineEvent& e);
    void stepToggle (int step, int vel);
    void stepCopy (int from, int to);
    void clearStep (int step);
    int selTrack (bool create);
    void erasePadInPattern (int gi);
    void autoChop (int mode);
    void chopsToPads();
    void trimToChop();
    void loadSample (const std::string& path, int mode);   // 0 selected pad, 1 next empty, 2 add layer
    void openSong (const std::string& path);
    void saveSong (bool asNew);
    void newSong();
    void exportAudio (int what);   // 0 pattern, 1 song, 2 stems
    void loadPrefs(); void savePrefs();
    void makeFactoryKit();
    void browseTo (const std::string& dir, const std::string& selectName = "");
    void refreshOpenList();
    void processEvents();
    void ask (const std::string& text, std::function<void()> yes) { confirmText = text; confirmYes = std::move (yes); }
    std::vector<Row> rows();
    std::vector<Row> padRows (int tab);
    std::vector<Row> menuRows();
    std::vector<Row> settingsRows();
    std::string pageTitle() const;
    std::array<std::string, 3> softLabels();
    std::array<std::pair<std::string, std::string>, 3> knobLabels();
    void rebuildSnapshot();

    // drawing
    void drawChrome (Canvas& c);
    void drawMain (Canvas& c);
    void drawPadGrid (Canvas& c, int x, int y, int w, int h);
    void drawSeq (Canvas& c);
    void drawSong (Canvas& c);
    void drawSample (Canvas& c);
    void drawChop (Canvas& c);
    void drawMixer (Canvas& c);
    void drawList (Canvas& c, const std::vector<Row>& r, int x, int y, int w, int h);
    void drawBrowser (Canvas& c);
    void drawAbout (Canvas& c);
    void drawSplash (Canvas& c);
    void drawWave (Canvas& c, const SampleData* s, int x, int y, int w, int h, Col col, const std::vector<float>* markers, int selChop);
    void drawMeter (Canvas& c, int x, int y, int w, int h, float v, bool vertical);
    std::string padName (int gi) const;
};
} // namespace cx
