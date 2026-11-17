// CONTRXL 25 - real-time audio engine.
//
// Threading:
//   * process() runs on the audio thread. It never allocates, locks (except a
//     try-lock on the snapshot pointer) or frees memory.
//   * The UI thread talks to it through a lock-free command FIFO and gets
//     events back through a second FIFO (recorded hits, MIDI CCs, ...).
//   * Project data arrives as an immutable Snapshot (see Snapshot.h).
//
// The sequencer runs inside process() and is sample-accurate (the web version
// used a 25 ms look-ahead timer; here every hit lands on the exact sample).
// The same class renders exports offline (renderPlan), so exports sound exactly
// like live playback.
#pragma once
#include "engine/Snapshot.h"
#include "engine/Effects.h"
#include "engine/Plugins.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace cx
{
struct Command
{
    enum Type : int { None, PadHit, NoteOn, NoteOff, Play, Stop, Panic, Preview, CaptureClick, PadRelease };
    Type type = None;
    int pad = 0, chop = -1, note = 0, tag = 0;
    float semi = 0, vel = 100, f = 0;
    bool record = true;
};

struct EngineEvent
{
    enum Type : int { None, RecHit, TransportCC, PitchKnob, ModWheel, Bend, KeyLight, OctFollow, Finished, CaptureFull, MidiIn, Transport,
                      Learn, MapFire, PadSelect, LowKey, PitchKnobRel, CalibHit };
    Type type = None;
    int a = 0, b = 0, c = 0;
    int ch = 0;                       // MIDI channel 0..15 (Learn, MapFire)
    float f = 0, g = 0;
    double pos = 0;
};

// simple feed-forward compressor in the style of the Web Audio DynamicsCompressor
struct Compressor
{
    float threshold = 0, knee = 0, ratio = 1, attack = 0.003f, release = 0.25f;
    float makeupDb = 0;
    bool autoMakeup = false;
    double sr = 48000;
    float envDb = 0;                  // current gain reduction (dB, <= 0)
    void reset() { envDb = 0; }
    float curve (float xDb) const;
    void process (float* L, float* R, int n) noexcept;
};

class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlockSize);
    double getSampleRate() const { return sr; }

    // ---- UI thread
    void setSnapshot (SnapshotPtr s);
    bool push (const Command& c);
    bool pop (EngineEvent& e);
    void ensureFx (int pad);                       // allocate a pad's effects rack (call before publishing)
    void setOutputLatency (int samples) { outLatency.store (samples); }

    // ---- plug-ins (UI thread). The rack holds raw pointers; nodes added with addOwned are owned by the
    //      engine and are deleted by collectGarbage() once the audio thread is done with them.
    PluginRack rack;
    PluginNode* addOwned (std::unique_ptr<PluginNode> node);      // prepares it; not yet in a slot
    void setSlot (const SlotId& s, PluginNode* node) { rack.at (s).store (node, std::memory_order_release); }
    PluginNode* pluginAt (const SlotId& s) { return rack.at (s).load (std::memory_order_acquire); }
    void retire (PluginNode* node);                                // remove from every slot, delete later
    void collectGarbage();
    void deviceStopped() { running.store (false); }
    std::vector<PluginNode*> ownedNodes();
    // mixer meters (UI reads and resets)
    std::array<std::atomic<float>, NPADS> padPeak;
    std::array<std::atomic<float>, NSEND> retPeak;
    std::atomic<float> masterPeak { 0 };
    // sample level meter: audio input and output peaks, left / right (the meter reads and resets them)
    std::array<std::atomic<float>, 2> inPeak {}, outPeak {};
    std::atomic<int> inChannels { 0 };   // how many input channels the audio device gives us
    std::atomic<int> outLatency { 0 };

    // live values the UI may set directly
    std::atomic<float> bend { 0 };                 // -1..1 (pitch wheel)
    std::atomic<int>   appOct { 0 }, hwOct { 0 }, midiOffset { 0 };
    std::atomic<bool>  followOct { true };

    // ---- MIDI learn. Per note / CC number: -1 = normal behaviour, 1000+n = pad slot n of the current bank,
    //      2000+i = keyboard key i, 1 = a UI control (the event goes to the UI thread)
    static constexpr int MAP_NONE = -1, MAP_UI = 1, MAP_PAD = 1000, MAP_KEY = 2000;
    std::array<std::atomic<int>, 128> noteMap, ccMap, pcMap;   // any channel; pcMap: program changes (buttons / pads only)
    std::array<std::array<std::atomic<int>, 128>, 16> noteMapCh, ccMapCh, pcMapCh;   // links learned on one MIDI channel (checked first)
    // ---- controller profile (e.g. Akai MPK mini IV): pads on their own channel, endless knobs -> pitch knobs
    std::atomic<int>  padChannel { 0 };            // 1..16; notes on this channel play the pads (0 = off)
    std::array<std::atomic<int>, 16> padNotes;     // note for pads 1-8 (bank A) and 9-16 (bank B = next bank); -1 = none
    std::array<std::atomic<int>, 8>  knobCC;       // CC of the 8 knobs (-1 = none)
    std::atomic<bool> knobRelative { false };      // endless knobs in "Rel" mode (1..63 = up, 65..127 = down)
    std::atomic<bool>  learnArmed { false };      // next note-on / CC is reported as a Learn event, not played
    std::atomic<bool>  lowKeyLearn { false };     // next note-on becomes the keyboard's lowest key
    std::atomic<bool>  defaultTransportCC { true };
    std::atomic<int>   uiBank { 0 };
    void injectMidiForTest (const juce::uint8* d, int sz) { handleMidi (d, sz, 0); }

    // live values the UI reads
    std::atomic<bool>  playing { false };
    std::atomic<int>   curPat { 0 }, curStep { -1 };
    std::atomic<float> curBpm { 84 };
    std::atomic<float> meterL { 0 }, meterR { 0 }, laGr { 0 };
    std::atomic<float> playhead { -1 };            // selected pad, 0..1 of the main buffer
    std::atomic<int>   countInLeft { 0 };
    std::atomic<int>   countInKind { 0 };          // 0 count-in, 1 recording, 2 sampling
    std::atomic<bool>  recordArmed { false };      // REC button
    // Recording timing: hits are moved this many ms earlier before they land on a step (sound or MIDI delay
    // the computer doesn't report, e.g. Bluetooth headphones). Set by the tap test or by hand.
    std::atomic<float> recOffsetMs { 0 };
    std::atomic<bool>  calibrating { false };      // tap test running: hits report their timing (CalibHit)
    std::array<std::atomic<juce::uint32>, NPADS> padHits;   // increments on every trigger

    // ---- audio thread
    void process (const float* const* in, int numIn, float* const* out, int numOut, int n, const juce::MidiBuffer& midi);

    // ---- sampling (audio input or resample of the master output)
    enum CaptureState { CAP_IDLE = 0, CAP_COUNT, CAP_WAIT, CAP_REC, CAP_STOPREQ, CAP_STOPPED };
    bool startCapture (bool fromInput, bool useThreshold, float thresholdDb, bool countIn, double maxSeconds);
    void requestCaptureStop();
    int  captureState() const { return capState.load(); }
    float captureLevel() const { return capLevel.load(); }
    bool captureFromInput() const { return capInput; }
    double captureSeconds() const { return capLen.load() / sr; }
    // call once captureState() == CAP_STOPPED; returns the audio (or null) and resets to idle
    SamplePtr takeCapture();

    // ---- offline render (export). plan = list of patterns in order. Mutes tracks not in 'tracks' (empty = all).
    //      plugins: clones of the live plug-ins (may be null). They are reset and re-prepared for each render.
    static juce::AudioBuffer<float> renderPlan (SnapshotPtr snap, const std::vector<int>& plan, const std::vector<int>& tracks,
                                                double sampleRate, bool limit,
                                                const std::function<void (float)>& progress, const std::function<bool()>& cancel,
                                                double* songSecondsOut = nullptr, PluginSet* plugins = nullptr);

private:
    static constexpr int MAXV = 256;
    struct Voice
    {
        bool active = false;
        SnapshotPtr snap;
        const RPad* pad = nullptr; const RSlot* slot = nullptr;
        int padIdx = -1, layer = -1, chop = -1, trk = -1;
        float semi = 0;
        juce::uint32 tid = 0; bool live = false;
        const float* src[2] { nullptr, nullptr };
        double pos = 0, endPos = 0, rate = 1;
        float gain = 1, relLevel = 1, relStep = 0;
        bool releasing = false;
        int age = 0, atk = 96; float finSamples = 240;
        BQState pf[2], sf[2]; BQ sfc; bool hasSlotFilter = false;
        float panL = 1, panR = 1, panX = 0; bool stereoSrc = false, hasPan = false;
        bool vinyl = false; float crk = 0, hiss = 0; int noisePos = 0;
        juce::int64 startTime = 0; juce::uint64 order = 0;
    };

    double sr = 48000; int maxBlock = 512;
    SnapshotPtr cur, pending;
    juce::SpinLock snapLock;

    std::array<Voice, MAXV> voices;
    juce::uint64 voiceOrder = 0;
    juce::uint32 nextTid = 1;
    juce::int64 now = 0;               // absolute sample time

    std::array<std::atomic<PadFx*>, NPADS> fxPtr;
    std::array<std::unique_ptr<PadFx>, NPADS> fxOwn;
    juce::CriticalSection fxAllocLock;
    std::array<bool, NPADS> padActive {};
    juce::AudioBuffer<float> padBus, masterBus;
    std::array<BQ, NPADS + 1> padFilter;

    Compressor la, lim;
    bool limiterOn = true;
    struct LookAhead { static constexpr int MAXLA = 512; float bufL[MAXLA] {}, bufR[MAXLA] {}; int w = 0, la = 64; } look;
    std::vector<float> crackle, hiss;

    // FIFOs
    juce::AbstractFifo cmdFifo { 1024 }, evtFifo { 2048 };
    std::array<Command, 1024> cmdBuf;
    std::array<EngineEvent, 2048> evtBuf;
    juce::SpinLock cmdWriteLock;      // several UI-side producers are possible (UI + MIDI learn)
    void emit (const EngineEvent& e);

    // sequencer
    struct Pending { double time; int trk; float semi, gain; double len; int pad, chop; };
    // plug-in instruments
    struct NoteOff { double time; int pad, note; };
    std::array<NoteOff, 2048> noteOffs; int nNoteOffs = 0;
    std::array<int, NPADS> padHeldNote {};          // note + 1 held by a pad press that will be released
    std::array<int, 128> midiPadHeld {};            // MIDI note -> pad + 1 (learned pads, for the release)
    std::array<std::array<int, 256>, 3> keyInst {}; // key held -> pad * 128 + note + 1
    int curOffset = 0, chunkLen = 0;
    float lastBendSent = 0;
    juce::AudioBuffer<float> sendBus, instBus;
    std::vector<std::unique_ptr<PluginNode>> owned;
    struct Retired { std::unique_ptr<PluginNode> node; juce::uint64 at; };
    std::vector<Retired> retiredNodes;
    juce::CriticalSection pluginLock;
    std::atomic<juce::uint64> blockCount { 0 };
    std::atomic<bool> running { false };
    PluginNode* instAt (int pad) const { return pad >= 0 && pad < NPADS ? rack.inst[(size_t) pad].load (std::memory_order_acquire) : nullptr; }
    int instNote (const RPad& p, float semi) const { return juce::jlimit (0, 127, (int) std::round ((float) p.instRoot + semi + p.pitch)); }
    void instNoteOn (int pad, float semi, int vel, double lenSamples, bool held);
    void instAllOff();
    void runStrips (int n);
    std::array<Pending, 4096> pend; int npend = 0;
    bool seqPlaying = false; double nextStep = 0; int seqStep = 0, seqPat = 0, ci = 0, rep = 0;
    int lastStep = -1, lastPat = 0; double lastStepTime = 0; double countInEnd = 0;
    std::vector<int> plan; int planIdx = 0; bool planMode = false; bool planDone = false; double planEnd = 0;
    float effBpm = 0;
    struct Click { double time; bool accent; };
    std::array<Click, 64> clicks; int nclicks = 0;
    std::array<std::array<juce::uint32, 256>, 3> held {};
    std::array<int, 128> midiHeld {};
    double finishAt = -1;

    // capture
    std::atomic<int> capState { CAP_IDLE };
    std::atomic<int> capLen { 0 };
    std::atomic<float> capLevel { 0 };
    juce::AudioBuffer<float> capBuf; int capMax = 0;
    bool capInput = true, capThr = true; float capThrLin = 0.03f; double capStartAt = 0;
    float capPre[2][1024] {}; int capPreN = 0;

    // helpers
    void adoptSnapshot();
    void handleCommand (const Command& c, int offsetInBlock);
    void handleMidi (const juce::uint8* data, int size, int offsetInBlock);
    void hitPadLive (int gi, float vel, int chop, float semi, bool record, int offset, bool held = false);
    void releasePad (int gi);
    void handleMapped (int target, bool isCC, int d1, int d2, bool on, bool off, int offset);
    std::array<int, 128> ccHeld {};
    int mapCh = 0;
    juce::uint32 lastStreamLedMs = 0;   // channel of the message being handled (for MapFire)
    void noteOn (int note, float vel, int tag, int offset);
    void noteOff (int note, int tag);
    juce::uint32 startVoice (const Snapshot& s, int padIdx, int chop, float semi, float gain, int trk, bool live,
                             juce::uint32 tid, int layer, bool isLink);
    void chokeVoice (Voice& v, float seconds);
    void releaseTid (juce::uint32 tid, float seconds);
    void releaseLive (float seconds);
    void killAll (float seconds);
    float gainOf (float vel) const;
    void recordHit (int pad, int chop, float semi, float gain, int offset);
    void startPlay (int countBeats);
    void stopPlay();
    void scheduleStep (int pat, int step, double t);
    void advanceSequencer (double untilTime);
    void addClick (double t, bool accent);
    void render (int start, int n);
    void renderVoice (Voice& v, float* L, float* R, int n);
    void runCapture (const float* const* in, int numIn, const float* L, const float* R, int n);
    void applyLA (const LASettings& s);
};
} // namespace cx
