// CONTRXL 25 - MPC Sample edition. (c) 2026 ILLAHTAINMENT. All rights reserved.
//   contrxl25 --card /path/to/sd/card          run on the MPC Sample (started by autorun.sh)
//   contrxl25 --sim script.txt --card DIR --out DIR   desktop simulator: scripted controls, screenshots, audio
//   contrxl25 --selftest OUT.wav               render the CONTRXL kit beat without any hardware
#include "app.h"
#include <csignal>
#include <cstdarg>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace cx
{
static double simClock = -1;
double nowSeconds()
{
    if (simClock >= 0) return simClock;
    timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
std::string strf (const char* fmt, ...)
{
    char buf[1024];
    va_list ap; va_start (ap, fmt); vsnprintf (buf, sizeof buf, fmt, ap); va_end (ap);
    return buf;
}
static FILE* logFile = nullptr;
static std::mutex logLock;
void setLogFile (const std::string& path) { logFile = fopen (path.c_str(), "w"); }
void logf (const char* fmt, ...)
{
    char buf[1024];
    va_list ap; va_start (ap, fmt); vsnprintf (buf, sizeof buf, fmt, ap); va_end (ap);
    std::lock_guard<std::mutex> l (logLock);
    fprintf (stderr, "%s\n", buf);
    if (logFile) { fprintf (logFile, "%.3f %s\n", nowSeconds(), buf); fflush (logFile); }
}

// ------------------------------------------------------------------ simulator hardware
class SimHw : public Hw
{
public:
    std::vector<uint32_t> screen = std::vector<uint32_t> ((size_t) SCREEN_W * SCREEN_H, 0);
    std::array<std::array<int, 3>, 17> pads {};
    std::map<int, int> cc;
    int input = 0; float vol = 0.64f;
    bool init (std::string&) override { return true; }
    void shutdown() override {}
    void present (const uint32_t* px, int, int, int, int) override { memcpy (screen.data(), px, screen.size() * 4); }
    bool displayBusy() override { return false; }
    void sendSurface (const uint8_t* d, int n) override
    {
        if (n == 12 && d[0] == 0xF0 && d[4] == 0x65 && d[7] < 17) pads[d[7]] = { d[8], d[9], d[10] };
        else if (n == 3 && d[0] == 0xB0) cc[d[1]] = d[2];
    }
    void poll (int, std::vector<MidiMsg>&) override {}
    bool startAudio (Engine&, std::string&) override { return true; }
    void stopAudio() override {}
    void setInput (int r) override { input = r; }
    void setMainVolume (float g) override { vol = g; }
    int latencyFrames() override { return 512; }
    std::string status() override { return "simulator"; }
    bool simulated() const override { return true; }
};

static void writePpm (const std::string& path, const std::vector<uint32_t>& px)
{
    FILE* f = fopen (path.c_str(), "wb");
    if (! f) return;
    fprintf (f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (uint32_t v : px) { const uint8_t b[3] = { (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v }; fwrite (b, 1, 3, f); }
    fclose (f);
}

static int buttonByName (const std::string& n)
{
    static const std::map<std::string, int> M = {
        { "KNOBFX", S::KNOBFX }, { "PADFX", S::PADFX }, { "SAMPLE", S::SAMPLE }, { "SEQ", S::SEQ }, { "ERASE", S::ERASE }, { "NOTEREPEAT", S::NOTEREPEAT },
        { "B1", S::B1 }, { "B2", S::B2 }, { "B3", S::B3 }, { "PADBANK", S::PADBANK }, { "SAMPLESELECT", S::SAMPLESELECT }, { "16LEVELS", S::LEVELS16 },
        { "MUTE", S::MUTE }, { "SHIFT", S::SHIFT }, { "TAPTEMPO", S::TAPTEMPO }, { "PLUS", S::PLUS }, { "MINUS", S::MINUS }, { "SAMPLEREC", S::SAMPLEREC },
        { "LOOP", S::LOOP }, { "CHOP", S::CHOP }, { "SEQREC", S::SEQREC }, { "STOP", S::STOP }, { "PLAY", S::PLAY }, { "PUSH", S::PUSH } };
    auto it = M.find (n);
    return it == M.end() ? -1 : it->second;
}

static int runSim (const std::string& script, const std::string& card, const std::string& outDir)
{
    simClock = 100.0;
    setLogFile (outDir + "/sim.log");
    SimHw hw; Engine eng; eng.prepare (SR, 128);
    App app (hw, eng, card);
    app.start();
    Canvas cv;
    std::vector<float> audioOut;
    std::vector<float> L (128), R (128), iL (128), iR (128);
    double phase = 0; long long frame = 0;
    bool inputTone = false;
    auto advance = [&] (double ms)
    {
        const int blocks = (int) std::lround (ms / 1000.0 * SR / 128);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                // a test input: a plucked note every half second when the tone is on
                const double t = (frame + i) / SR;
                const double e = std::exp (-std::fmod (t, 0.5) / 0.08);
                iL[(size_t) i] = iR[(size_t) i] = inputTone ? (float) (0.4 * e * std::sin (phase)) : 0.0f;
                phase += 6.2831853 * 330 / SR;
            }
            frame += 128;
            eng.process (iL.data(), iR.data(), L.data(), R.data(), 128);
            for (int i = 0; i < 128; ++i) { audioOut.push_back (L[(size_t) i] * hw.vol); audioOut.push_back (R[(size_t) i] * hw.vol); }
            simClock += 128 / SR;
            if (b % 8 == 0) app.update();
        }
        while (app.busy()) usleep (2000);   // background jobs run in real time
        app.update();
    };
    std::ifstream in (script);
    std::string line;
    int lineNo = 0;
    while (std::getline (in, line) && app.running())
    {
        ++lineNo;
        std::istringstream ss (line);
        std::string cmd; ss >> cmd;
        if (cmd.empty() || cmd[0] == '#') continue;
        if (cmd == "wait") { double ms; ss >> ms; advance (ms); }
        else if (cmd == "pad") { int p, v; ss >> p >> v; app.simPad (p - 1, v); advance (1); }
        else if (cmd == "tap") { int p, v; ss >> p >> v; app.simPad (p - 1, v); advance (60); app.simPad (p - 1, 0); advance (1); }
        else if (cmd == "btn")
        {
            std::string n, how = "tap"; ss >> n >> how;
            const int b = buttonByName (n);
            if (b < 0) { fprintf (stderr, "line %d: unknown button %s\n", lineNo, n.c_str()); continue; }
            if (how == "down" || how == "tap") app.simButton (b, true);
            if (how == "tap") advance (40);
            if (how == "up" || how == "tap") app.simButton (b, false);
            advance (1);
        }
        else if (cmd == "cc") { int c, v; ss >> c >> v; app.simCC (c, v); advance (1); }
        else if (cmd == "knob") { int k, d; ss >> k >> d; const int cc = S::CC_K1 + k - 1; app.simCC (cc, 64); for (int i = 1; i <= std::abs (d); ++i) app.simCC (cc, clampv (64 + (d > 0 ? i : -i), 0, 127)); advance (1); }
        else if (cmd == "wheel") { int d; ss >> d; app.simCC (S::CC_WHEEL, d >= 0 ? d : 128 + d); advance (1); }
        else if (cmd == "note") { int n, v; ss >> n >> v; MidiMsg m; m.src = 1; m.b[0] = v ? 0x90 : 0x80; m.b[1] = (uint8_t) n; m.b[2] = (uint8_t) v; m.n = 3; app.onMidi (m); advance (1); }
        else if (cmd == "input") { std::string v; ss >> v; inputTone = v == "on"; }
        else if (cmd == "shot")
        {
            std::string name; ss >> name;
            app.draw (cv);
            hw.present (cv.px.data(), 0, 0, SCREEN_W, SCREEN_H);
            writePpm (outDir + "/" + name + ".ppm", hw.screen);
            FILE* f = fopen ((outDir + "/" + name + ".leds").c_str(), "w");
            if (f)
            {
                for (int i = 0; i < 17; ++i) fprintf (f, "pad %d %d %d %d\n", i, hw.pads[(size_t) i][0], hw.pads[(size_t) i][1], hw.pads[(size_t) i][2]);
                for (auto& kv : hw.cc) fprintf (f, "cc %d %d\n", kv.first, kv.second);
                fclose (f);
            }
        }
        else fprintf (stderr, "line %d: unknown command %s\n", lineNo, cmd.c_str());
    }
    app.lightsOff();
    app.saveOnExit();
    SampleData s; s.sampleRate = SR;
    s.ch[0].resize (audioOut.size() / 2); s.ch[1].resize (audioOut.size() / 2);
    for (size_t i = 0; i < audioOut.size() / 2; ++i) { s.ch[0][i] = audioOut[i * 2]; s.ch[1][i] = audioOut[i * 2 + 1]; }
    io::writeWav (outDir + "/sim-audio.wav", s, 16);
    fprintf (stderr, "sim done: %.1f s of audio, app %s\n", audioOut.size() / 2 / SR, app.running() ? "running" : "exited");
    return 0;
}

static int selfTest (const std::string& outWav)
{
    // no hardware: build the kit, render the demo beat offline (checks the engine on the target CPU)
    const std::string tmp = "/tmp/contrxl25-selftest";
    io::mkdirs (tmp);
    SimHw hw; Engine eng; eng.prepare (SR, 128);
    App app (hw, eng, tmp);
    app.start();
    SnapshotBuilder b;
    auto snap = b.build (app.project());
    int frames = 0;
    const double t0 = nowSeconds();
    auto inter = Engine::renderPlan (snap, { 0, 0 }, -1, frames, nullptr);
    const double dt = nowSeconds() - t0;
    SampleData s; s.sampleRate = SR; s.ch[0].resize ((size_t) frames); s.ch[1].resize ((size_t) frames);
    float pk = 0;
    for (int i = 0; i < frames; ++i) { s.ch[0][(size_t) i] = inter[(size_t) i * 2]; s.ch[1][(size_t) i] = inter[(size_t) i * 2 + 1]; pk = std::max (pk, std::fabs (inter[(size_t) i * 2])); }
    io::writeWav (outWav, s, 16);
    // song file round trip
    std::string err, msg;
    const bool saved = saveSong (app.project(), tmp + "/selftest.zip", err);
    Project q; const bool loaded = loadSong (tmp + "/selftest.zip", q, msg);
    printf ("selftest: %.2f s rendered in %.2f s (%.0fx real time), peak %.2f, song save %s, load %s (%s)\n",
            frames / SR, dt, frames / SR / std::max (1e-6, dt), pk, saved ? "ok" : err.c_str(), loaded ? "ok" : "FAILED", msg.c_str());
    return (pk > 0.05f && saved && loaded) ? 0 : 1;
}

static int bench()
{
    // worst case: every kit pad with saturation, chorus, delay, echo and reverb, 12 tracks hitting every 1/16
    const std::string tmp = "/tmp/contrxl25-bench";
    io::mkdirs (tmp);
    SimHw hw; Engine eng; eng.prepare (SR, 128);
    App app (hw, eng, tmp);
    app.start();
    Project& P = app.project();
    for (int i = 0; i < 12; ++i)
    {
        auto& f = P.pads[(size_t) i].fx;
        f.satOn = f.choOn = f.delOn = f.ecoOn = f.revOn = true; f.type = FT_LP; f.cut = 3000; f.rm = 1 + i % 7;
        const int ti = P.trackFor (i, true);
        auto& d = P.tracks[(size_t) ti].get (0);
        d.ev.clear();
        for (int s = 0; s < 16; ++s) d.ev.push_back ({ s * TPS + (i % 3) * 4, 0, 0.8f, -2 });
    }
    SnapshotBuilder b;
    auto snap = b.build (P);
    for (int i = 0; i < 12; ++i) eng.ensureFx (i);
    eng.setSnapshot (snap);
    Command c; c.type = Command::Play; eng.push (c);
    std::vector<float> L (128), R (128);
    const int blocks = (int) (10 * SR / 128);
    double worst = 0; const double t0 = nowSeconds();
    for (int k = 0; k < blocks; ++k)
    {
        const double a = nowSeconds();
        eng.process (nullptr, nullptr, L.data(), R.data(), 128);
        if (k > 20) worst = std::max (worst, nowSeconds() - a);
    }
    const double dt = nowSeconds() - t0;
    printf ("bench: 10 s of the heaviest case in %.2f s = %.0f%% of one core, worst block %.2f ms (budget 2.90 ms)\n", dt, dt / 10 * 100, worst * 1000);
    return 0;
}

static volatile sig_atomic_t stopSignal = 0;
static void onSignal (int) { stopSignal = 1; }

static int runDevice (const std::string& card)
{
    io::mkdirs (card + "/CONTRXL25");
    setLogFile (card + "/CONTRXL25/contrxl25.log");
    logf ("CONTRXL 25 MPC Sample edition %s - (c) ILLAHTAINMENT", APP_VERSION);
    signal (SIGTERM, onSignal); signal (SIGINT, onSignal); signal (SIGHUP, onSignal); signal (SIGPIPE, SIG_IGN);
    auto hw = makeMpcHw();
    std::string err;
    if (! hw->init (err)) { logf ("hardware: %s", err.c_str()); return 2; }
    Engine eng; eng.prepare (SR, 128);
    App app (*hw, eng, card);
    if (! hw->startAudio (eng, err)) logf ("audio: %s (running without sound)", err.c_str());
    app.start();
    Canvas cv, last;
    last.clear (0x123456);
    std::vector<MidiMsg> msgs;
    double lastDraw = 0;
    while (app.running() && ! stopSignal)
    {
        msgs.clear();
        hw->poll (6, msgs);
        for (auto& m : msgs) app.onMidi (m);
        app.update();
        const double t = nowSeconds();
        if (t - lastDraw >= 1.0 / 30 && ! hw->displayBusy())
        {
            lastDraw = t;
            app.draw (cv);
            int x0 = SCREEN_W, y0 = SCREEN_H, x1 = 0, y1 = 0;
            for (int y = 0; y < SCREEN_H; ++y)
            {
                const uint32_t* a = cv.px.data() + y * SCREEN_W; const uint32_t* b = last.px.data() + y * SCREEN_W;
                if (memcmp (a, b, SCREEN_W * 4) == 0) continue;
                int l = 0; while (a[l] == b[l]) ++l;
                int r = SCREEN_W - 1; while (a[r] == b[r]) --r;
                x0 = std::min (x0, l); x1 = std::max (x1, r + 1); y0 = std::min (y0, y); y1 = y + 1;
            }
            if (x1 > x0) { hw->present (cv.px.data(), x0, y0, x1, y1); last.px = cv.px; }
        }
    }
    logf ("exiting");
    app.lightsOff();
    for (int i = 0; i < 20; ++i) { msgs.clear(); hw->poll (2, msgs); }   // let the LED messages out
    app.saveOnExit();
    hw->stopAudio();
    hw->shutdown();
    return 0;
}
} // namespace cx

int main (int argc, char** argv)
{
    std::string card = ".", sim, out = ".", selftest;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--card") card = next();
        else if (a == "--sim") sim = next();
        else if (a == "--out") out = next();
        else if (a == "--selftest") selftest = next();
        else if (a == "--bench") return cx::bench();
        else if (a == "--version") { printf ("CONTRXL 25 MPC Sample edition %s\n", cx::APP_VERSION); return 0; }
    }
    if (! selftest.empty()) return cx::selfTest (selftest);
    if (! sim.empty()) return cx::runSim (sim, card, out);
    return cx::runDevice (card);
}
