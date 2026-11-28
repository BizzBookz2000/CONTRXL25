// CONTRXL 25 audio engine (real-time). The UI thread owns the Project and hands the
// engine an immutable Snapshot whenever something changes; the audio thread only reads it.
#pragma once
#include "dsp.h"
#include <mutex>

namespace cx
{
struct RSlot
{
    SamplePtr buf;
    std::vector<std::pair<double, double>> segs;
    bool ownSegs = false;
    double kk = 1.0;
    float vol = 1, pitch = 0;
    int ft = FT_OFF; float fc = 2000; float pan = 0;
};

struct RPad
{
    bool has = false, muted = false;
    float vol = 1, pitch = 0;
    int mg = 0, lg = 0;
    FxParams fx;
    bool fxOn = false;
    RSlot main;
    std::vector<RSlot> layers;
    float pan = 0;
    bool solo = false;
    int numChops() const { return has ? (int) main.segs.size() : 0; }
};

struct Snapshot
{
    std::array<RPad, NPADS + 1> pads;   // [NPADS] = browser preview
    std::vector<Track> tracks;
    std::array<int, NPAT> plen {};
    std::array<int, NPAT> pbpm {};
    std::vector<ChainItem> chain;
    Settings s;
    bool soloAny = false, trackSoloAny = false;
    float bpmOf (int p) const { return pbpm[(size_t) p] > 0 ? (float) pbpm[(size_t) p] : s.bpm; }
};
using SnapshotPtr = std::shared_ptr<const Snapshot>;

class SnapshotBuilder
{
public:
    SnapshotPtr build (const Project& p, const SamplePtr& preview = nullptr);
    SamplePtr playBuffer (const Pad& pad);   // what the pad really plays (lo-fi / retro mode baked in)
    void clear() { cache.clear(); }
private:
    struct Entry { SamplePtr src, result; int lastUse = 0; };
    std::map<std::pair<const void*, std::string>, Entry> cache;
    int generation = 0;
    SamplePtr cached (const SamplePtr& src, const std::string& key, const std::function<SamplePtr()>& make);
    SamplePtr derive (const SamplePtr& src, const FxParams& fx, int rm);
};
int effectiveRetro (const Pad& p, const SlotSettings& s);

// UI -> audio
struct Command
{
    enum Type { None, Hit, Release, Play, Stop, Panic, Preview, RepeatStart, RepeatVel, RepeatStop, CaptureClick };
    Type type = None;
    int pad = 0, chop = -1, hold = -1;   // hold: key id for gate / note-off (0..255), -1 none
    float vel = 100, semi = 0;
    bool record = false, choke = false;
    int count = 0;                      // Play: count-in beats
};
// audio -> UI
struct EngineEvent
{
    enum Type { None, RecHit, CaptureFull, Finished };
    Type type = None;
    int pad = 0, chop = -2, pat = 0;
    float semi = 0, gain = 0;
    double pos = 0;                     // in steps
};

template <typename T, int N> class SpscQueue
{
public:
    bool push (const T& v) { const int w = wr.load (std::memory_order_relaxed), n = (w + 1) % N; if (n == rd.load (std::memory_order_acquire)) return false; buf[w] = v; wr.store (n, std::memory_order_release); return true; }
    bool pop (T& v) { const int r = rd.load (std::memory_order_relaxed); if (r == wr.load (std::memory_order_acquire)) return false; v = buf[r]; rd.store ((r + 1) % N, std::memory_order_release); return true; }
private:
    T buf[N]; std::atomic<int> wr { 0 }, rd { 0 };
};

class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);
    void setSnapshot (SnapshotPtr s);           // UI thread
    void ensureFx (int pad);                    // UI thread, before a snapshot with that pad's FX on
    bool push (const Command& c) { std::lock_guard<std::mutex> l (cmdLock); return cmds.push (c); }
    bool pop (EngineEvent& e) { return events.pop (e); }

    // audio thread: inL/inR may be null
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    // capture (sampling): from the audio input or what CONTRXL plays (resample)
    enum { CAP_IDLE, CAP_COUNT, CAP_WAIT, CAP_REC, CAP_STOPREQ, CAP_STOPPED };
    bool startCapture (bool fromInput, bool useThreshold, float thresholdDb, bool countIn, double maxSeconds);
    void requestCaptureStop();
    SamplePtr takeCapture();
    std::atomic<int> capState { CAP_IDLE };
    std::atomic<int> capLen { 0 };
    int capMax = 0;

    // state the UI reads
    std::atomic<bool> playing { false };
    std::atomic<int> curPat { 0 }, curStep { -1 }, countInLeft { 0 }, songIndex { 0 };
    std::atomic<float> outPeak[2], inPeak[2];
    std::array<std::atomic<float>, NPADS> padPeak;
    std::array<std::atomic<uint32_t>, NPADS> padHits;
    std::atomic<float> playhead { -1 };         // selected pad's newest voice, 0..1
    std::atomic<int> playheadChop { -1 };
    std::atomic<bool> recordArmed { false };
    std::atomic<float> recOffsetMs { 0 };
    std::atomic<int> outLatency { 512 };
    std::atomic<float> monitor { 0 };           // input monitoring level (0 = off)
    std::atomic<float> repeatSteps { 1.0f };    // note repeat rate in 16th steps (1 = 1/16)
    std::atomic<float> inputGain { 1.0f };
    std::atomic<uint64_t> blocks { 0 };

    // offline render of patterns (export). plan = pattern indices in order.
    static std::vector<float> renderPlan (SnapshotPtr snap, const std::vector<int>& plan, int onlyTrack,
                                          int& framesOut, const std::function<void (float)>& progress, bool trimTail = true);
private:
    static constexpr int MAXV = 48;
    struct Voice
    {
        bool active = false;
        SnapshotPtr snap;
        const RPad* pad = nullptr; const RSlot* slot = nullptr;
        int padIdx = -1, layer = -1, chop = -1, trk = -1;
        float semi = 0;
        uint32_t tid = 0; bool live = false;
        const float* src[2] { nullptr, nullptr }; int len = 0;
        double pos = 0, endPos = 0, rate = 1;
        float gain = 1, relLevel = 1, relStep = 0;
        bool releasing = false;
        int age = 0, atk = 96; float finSamples = 240;
        BQState pf[2], sf[2]; BQ sfc; bool hasSlotFilter = false;
        float panL = 1, panR = 1, panX = 0; bool stereoSrc = false, hasPan = false;
        bool vinyl = false; float crk = 0, hiss = 0; int noisePos = 0;
        int64_t startTime = 0; uint64_t order = 0;
    };
    struct Pending { double time; int trk; float semi, gain; int pad, chop; };
    struct Click { double time; bool accent; };
    struct Repeat { bool on = false; int pad = 0, chop = -1; float semi = 0, vel = 100; bool record = false, choke = false; double next = 0; };
    struct Skip { int pat, step, pad, chop; float semi; };

    double sr = SR; int maxBlock = 512;
    SnapshotPtr cur, pending;
    std::mutex snapLock, cmdLock;
    SpscQueue<Command, 1024> cmds;
    SpscQueue<EngineEvent, 1024> events;
    std::array<Voice, MAXV> voices;
    uint64_t voiceOrder = 0;
    uint32_t nextTid = 1;
    int64_t now = 0;
    std::array<std::atomic<PadFx*>, NPADS> fxPtr;
    std::array<std::unique_ptr<PadFx>, NPADS> fxOwn;
    std::array<BQ, NPADS> padFilter;
    std::vector<float> padBus;                  // NPADS * 2 * maxBlock
    std::array<bool, NPADS> padActive {};
    std::vector<float> mL, mR;
    std::vector<float> crackle, hiss;
    std::array<uint32_t, 256> held {};
    std::array<Repeat, 16> repeats;
    std::vector<Skip> skips;

    // sequencer
    bool seqPlaying = false, offline = false;
    int seqStep = 0, seqPat = 0, ci = 0, rep = 0, lastStep = -1, lastPat = 0;
    double nextStep = 0, lastStepTime = 0, countInEnd = 0, finishAt = -1, effBpm = 0;
    std::vector<int> plan; int planIdx = 0; bool planMode = false, planDone = false;
    std::array<Pending, 1024> pend; int npend = 0;
    std::array<Click, 64> clicks; int nclicks = 0;
    float limEnv = 1.0f;

    // capture
    std::vector<float> capL, capR;
    bool capInput = true, capThr = false; float capThrLin = 0.05f; double capStartAt = 1e18;
    float capPre[2][1024]; int capPreN = 0;

    void adoptSnapshot();
    void handle (const Command& c);
    float gainOf (float vel) const;
    void chokeVoice (Voice& v, float seconds);
    void releaseTid (uint32_t tid, float seconds);
    void releaseLive (float seconds);
    void killAll (float seconds);
    uint32_t startVoice (const Snapshot& s, int padIdx, int chop, float semi, float gain, int trk, bool live, uint32_t tid, int layer, bool isLink);
    void renderVoice (Voice& v, float* L, float* R, int n);
    void hit (int pad, int chop, float semi, float vel, bool choke, bool record, int hold);
    void recordHit (int pad, int chop, float semi, float gain);
    void startPlay (int countBeats);
    void stopPlay();
    void scheduleStep (int pat, int step, double t);
    void advanceSequencer (double until);
    void addClick (double t, bool accent) { if (nclicks < (int) clicks.size()) clicks[(size_t) nclicks++] = { t, accent }; }
    void runRepeats (double until, double& earliest);
    void render (int start, int n);
    void runCapture (const float* sL, const float* sR, const float* oL, const float* oR, int n);
    void emit (const EngineEvent& e) { events.push (e); }
};
} // namespace cx
