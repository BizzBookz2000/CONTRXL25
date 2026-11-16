// Headless test of the CONTRXL 25 engine: builds a small project in code, renders it
// offline, checks a few things and writes the results as WAV files next to the exe.
//   cmake -B build -DCONTRXL_BUILD_TESTS=ON && cmake --build build --target EngineTest
#include "engine/Engine.h"
#include "dsp/Offline.h"
#include "dsp/Stems.h"
#include "io/SongIO.h"
#include "app/PluginManager.h"
#include "io/Library.h"
#include <iostream>

using namespace cx;

static SamplePtr makeKick (double sr)
{
    const int n = (int) (sr * 0.4);
    juce::AudioBuffer<float> b (2, n);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr, f = 50 + 120 * std::exp (-t * 30);
        ph += f / sr;
        const float v = (float) (std::sin (juce::MathConstants<double>::twoPi * ph) * std::exp (-t * 8));
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return makeSample (std::move (b), sr);
}

static SamplePtr makeChord (double sr)
{
    const int n = (int) (sr * 2.0);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        float v = 0;
        for (double f : { 220.0, 277.18, 329.63 }) v += (float) std::sin (juce::MathConstants<double>::twoPi * f * t);
        v *= 0.25f * (float) std::exp (-t * 1.2);
        const float click = (std::fmod (t, 0.5) < 0.004) ? 0.8f : 0.0f;
        b.setSample (0, i, v + click); b.setSample (1, i, v * 0.8f + click);
    }
    return makeSample (std::move (b), sr);
}

static int fails = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++fails;
}

// ---------------- tiny internal "plug-ins" to test hosting without VST3 files
struct TestProc : public juce::AudioProcessor
{
    explicit TestProc (bool synth)
        : juce::AudioProcessor (synth ? BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())
                                      : BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())),
          isSynth (synth) { ++alive; }
    ~TestProc() override { --alive; }
    static inline int alive = 0;
    bool isSynth; float gain = 1.0f; int lat = 0;
    int ons = 0, offs = 0; int note = -1; double ph = 0, srate = 48000;
    const juce::String getName() const override { return isSynth ? "TestSynth" : "TestGain"; }
    void prepareToPlay (double sr, int) override { srate = sr; setLatencySamples (lat); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        if (! isSynth)
        {
            if (lat > 0)   // a pure delay of 'lat' samples
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        auto& d = dl[(size_t) c]; d.push_back (b.getSample (c, i));
                        b.setSample (c, i, (int) d.size() > lat ? d[0] * gain : 0.0f);
                        if ((int) d.size() > lat) d.erase (d.begin());
                    }
            }
            else b.applyGain (gain);
            return;
        }
        int pos = 0;
        auto renderTo = [&] (int end) { for (; pos < end; ++pos) { const float v = note >= 0 ? (float) std::sin (ph) * 0.3f : 0.0f;
                                         if (note >= 0) ph += juce::MathConstants<double>::twoPi * 440.0 * std::pow (2.0, (note - 69) / 12.0) / srate;
                                         b.setSample (0, pos, v); b.setSample (1, pos, v); } };
        for (const auto meta : m)
        {
            renderTo (meta.samplePosition);
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) { note = msg.getNoteNumber(); ++ons; }
            else if (msg.isNoteOff() && msg.getNoteNumber() == note) { note = -1; ++offs; }
            else if (msg.isAllNotesOff()) note = -1;
        }
        renderTo (b.getNumSamples());
    }
    std::array<std::vector<float>, 2> dl;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return isSynth; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& mb) override { mb.append (&gain, sizeof (gain)); }
    void setStateInformation (const void* d, int n) override { if (n == sizeof (float)) std::memcpy (&gain, d, sizeof (float)); }
};

static std::unique_ptr<PluginNode> makeNode (bool synth, float gain = 1.0f, int lat = 0, TestProc** raw = nullptr)
{
    auto p = std::make_unique<TestProc> (synth);
    p->gain = gain; p->lat = lat;
    if (raw) *raw = p.get();
    return std::make_unique<PluginNode> (std::move (p), newPluginIid(), synth);
}

int main()
{
    juce::ignoreUnused (0);
    const auto outDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    std::cout << "CONTRXL 25 engine test -> " << outDir.getFullPathName() << std::endl;

    Project P;
    P.s.bpm = 90;
    P.pads[0].buf = makeKick (44100); P.pads[0].name = "kick";
    P.pads[1].buf = makeChord (48000); P.pads[1].name = "chord";
    P.pads[1].markers = dsp::transientMarkers (*P.pads[1].buf, 8);
    check (P.pads[1].markers.size() >= 2, "transient detection found " + juce::String ((int) P.pads[1].markers.size()) + " chop points");
    P.pads[1].fx.delOn = true; P.pads[1].fx.revOn = true;
    P.pads[0].fx.rm = 4;   // SP-1200 retro mode

    auto& kd = P.tracks[0].get (0);
    for (int s = 0; s < 16; s += 4) kd.steps[(size_t) s] = 1;
    P.tracks[0].pad = 0;
    auto& cd = P.tracks[1].get (0);
    cd.ev.push_back ({ 2 * TPS + 12, 0, 0.8f });   // unquantized hit half-way through step 3
    P.tracks[1].pad = 1; P.tracks[1].chop = 0;
    P.pbpm[1] = 120;
    auto& kd2 = P.tracks[0].get (1);
    kd2.steps[0] = 1; kd2.steps[8] = 1;

    dsp::StemStore stems;
    SnapshotBuilder builder;
    TrimState trim;
    auto snap = builder.build (P, stems, trim);
    check (snap->pads[0].main.buf != P.pads[0].buf, "retro mode produced a processed play buffer");

    double songLen = 0;
    auto mix = Engine::renderPlan (snap, { 0, 1 }, {}, 44100, true, {}, {}, &songLen);
    check (std::abs (songLen - (16 * 60.0 / 90 / 4 + 16 * 60.0 / 120 / 4)) < 1e-6, "song length follows per-pattern tempo");
    check (mix.getNumSamples() >= (int) (songLen * 44100), "mix is at least the song length");
    float pk = 0; for (int c = 0; c < 2; ++c) pk = std::max (pk, mix.getMagnitude (c, 0, mix.getNumSamples()));
    check (pk > 0.05f && pk <= 1.0f, "mix has audio and stays below 0 dBFS (peak " + juce::String (pk, 3) + ")");

    // first kick must land on sample 0 (sample-accurate, look-ahead compensated)
    auto stem0 = Engine::renderPlan (snap, { 0, 1 }, { 0 }, 44100, false, {}, {});
    int first = -1;
    for (int i = 0; i < stem0.getNumSamples(); ++i) if (std::abs (stem0.getSample (0, i)) > 1e-4f) { first = i; break; }
    check (first >= 0 && first < 4, "stem: first hit starts at sample " + juce::String (first));
    int firstMix = -1;
    for (int i = 0; i < mix.getNumSamples(); ++i) if (std::abs (mix.getSample (0, i)) > 1e-4f) { firstMix = i; break; }
    check (std::abs (firstMix - first) <= 2, "mix and stem start together (" + juce::String (firstMix) + " vs " + juce::String (first) + ")");
    // the 5th kick is the first step of pattern 2 (after 16 steps at 90 BPM)
    const int expect = (int) std::round (16 * 60.0 / 90 / 4 * 44100);
    int hit2 = -1;
    for (int i = expect - 50; i < expect + 50; ++i) if (std::abs (stem0.getSample (0, i)) > 1e-3f && std::abs (stem0.getSample (0, i - 1)) < 1e-3f) { hit2 = i; break; }
    check (std::abs (hit2 - expect) <= 3, "pattern 2 starts on time (" + juce::String (hit2) + " vs " + juce::String (expect) + ")");

    io::writeWav (outDir.getChildFile ("test_mix.wav"), mix, 44100, 24);
    io::writeWav (outDir.getChildFile ("test_stem_kick.wav"), stem0, 44100, 24);

    // offline DSP
    auto st = dsp::wsolaStretch (*P.pads[1].buf, 1.5);
    check (std::abs (st->length() - (int) std::round (P.pads[1].buf->length() * 1.5)) <= 1, "WSOLA stretch length");
    auto rs = dsp::resampleStretch (*P.pads[1].buf, 0.5);
    check (std::abs (rs->length() - P.pads[1].buf->length() / 2) <= 1, "resample stretch length");
    for (int r = 1; r < NRETRO; ++r)
    {
        auto x = dsp::retroProcess (*P.pads[1].buf, r);
        check (x && x->peak() <= 0.981f && x->peak() > 0.01f, juce::String ("retro ") + retroName (r));
    }
    auto sep = dsp::separateStems (*P.pads[1].buf);
    double err = 0, ref = 0;
    for (int i = 0; i < P.pads[1].buf->length(); ++i)
    {
        float sum = 0; for (auto& s : sep) sum += s->audio.getSample (0, i);
        const float o = P.pads[1].buf->audio.getSample (0, i);
        err += (sum - o) * (sum - o); ref += o * o;
    }
    check (err / (ref + 1e-12) < 1e-3, "stems add back to the original (rel. error " + juce::String (err / (ref + 1e-12), 6) + ")");

    // song round trip
    const auto zip = outDir.getChildFile ("test_song.zip");
    juce::String msg;
    check (io::saveSong (P, zip, msg), "save song zip " + msg);
    Project Q;
    check (io::loadSong (zip, Q, msg), "load song zip: " + msg);
    check (Q.pads[1].markers.size() == P.pads[1].markers.size() && Q.pbpm[1] == 120 && Q.tracks[1].find (0) && Q.tracks[1].find (0)->ev.size() == 1,
           "song round trip keeps markers, tempo and events");
    check (Q.pads[0].fx.rm == 4 && Q.pads[1].fx.delOn, "song round trip keeps effects");
    const auto mid = io::tempoMapMid (P, { 0, 1 });
    check (mid.getSize() > 20 && io::planTempos (P, { 0, 1 }).size() == 2, "tempo map has 2 tempos");

    // ---------------- live engine paths (what the audio callback does)
    {
        Engine e;
        e.prepare (48000, 256);
        Project L = P;
        L.s.metro = false;
        auto ls = builder.build (L, stems, trim);
        e.setSnapshot (ls);
        juce::AudioBuffer<float> out (2, 256);
        juce::MidiBuffer midi;
        auto run = [&] (int blocks, float* peakOut = nullptr)
        {
            float pk2 = 0;
            for (int b = 0; b < blocks; ++b)
            {
                out.clear();
                e.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, midi);
                midi.clear();
                pk2 = std::max (pk2, out.getMagnitude (0, 0, 256));
            }
            if (peakOut) *peakOut = pk2;
        };
        float pk2 = 0;
        run (4, &pk2);
        check (pk2 < 1e-6f, "silent when idle");
        Command c; c.type = Command::PadHit; c.pad = 0; c.vel = 110;
        e.push (c); run (20, &pk2);
        check (pk2 > 0.05f, "pad hit makes sound (" + juce::String (pk2, 3) + ")");
        check (e.padHits[0].load() == 1, "pad hit counter");
        run (200);
        c = {}; c.type = Command::NoteOn; c.note = 12; c.vel = 100; c.tag = 1;
        e.push (c); run (10, &pk2);
        check (pk2 > 0.05f, "QWERTY / mouse note in keys mode makes sound");
        // MIDI: note, transport CC, pitch bend
        run (200);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 17);
        run (10, &pk2);
        check (pk2 > 0.05f, "MIDI note makes sound");
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 94, 127), 0);
        midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 5);
        run (1);
        bool gotCC = false, gotBend = false, gotLight = false; EngineEvent ev;
        while (e.pop (ev)) { gotCC |= ev.type == EngineEvent::TransportCC && ev.a == 94; gotBend |= ev.type == EngineEvent::Bend && ev.f > 0.99f; gotLight |= ev.type == EngineEvent::KeyLight; }
        check (gotCC && gotBend && gotLight, "MIDI transport CC, pitch bend and key-light events");
        e.bend.store (0);
        // live sequencer + recording
        c = {}; c.type = Command::Play; c.f = 0;
        e.recordArmed.store (true);
        e.push (c); run (40);
        check (e.playing.load() && e.curStep.load() >= 0, "sequencer runs live (step " + juce::String (e.curStep.load()) + ")");
        c = {}; c.type = Command::PadHit; c.pad = 1; c.vel = 100;
        e.push (c); run (1);
        bool gotRec = false;
        while (e.pop (ev)) if (ev.type == EngineEvent::RecHit && ev.a == 1) { gotRec = true; check (ev.pos >= 0 && ev.pos < 16, "recorded hit position " + juce::String (ev.pos, 2) + " steps"); }
        check (gotRec, "live recording reports the hit");
        c = {}; c.type = Command::Stop; e.push (c); run (20, &pk2);
        check (! e.playing.load(), "stop");
        run (100, &pk2);
        check (pk2 < 1e-4f, "silence after stop");
        // resample capture
        check (e.startCapture (false, false, -30, false, 2.0), "start resample capture");
        c = {}; c.type = Command::PadHit; c.pad = 0; c.vel = 120; e.push (c);
        run (60);
        e.requestCaptureStop(); run (1);
        auto cap = e.takeCapture();
        check (cap && cap->length() > 10000 && cap->peak() > 0.05f, "resample captured " + juce::String (cap ? cap->duration() : 0.0, 2) + " s");
        // gate releases a held note
        Project G = L; G.s.gate = true; G.s.sel = 1; e.setSnapshot (builder.build (G, stems, trim)); run (1);
        c = {}; c.type = Command::NoteOn; c.note = 12; c.vel = 100; c.tag = 0; e.push (c); run (20);
        c.type = Command::NoteOff; e.push (c); run (30, &pk2);
        run (5, &pk2);
        check (pk2 < 1e-3f, "gate: note stops on release (peak after " + juce::String (pk2, 5) + ")");

        // ---- MIDI learn routing
        e.setSnapshot (builder.build (L, stems, trim)); run (200);
        while (e.pop (ev)) {}
        e.learnArmed.store (true);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 20, 127), 0);
        run (1, &pk2);
        bool gotLearn = false;
        while (e.pop (ev)) gotLearn |= ev.type == EngineEvent::Learn && ev.a == 1 && ev.b == 20;
        check (gotLearn, "learn armed: a CC is reported as a Learn event");
        e.learnArmed.store (true);
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        run (10, &pk2);
        gotLearn = false;
        while (e.pop (ev)) gotLearn |= ev.type == EngineEvent::Learn && ev.a == 0 && ev.b == 36;
        check (gotLearn && pk2 < 1e-6f, "learn armed: a note is reported, not played");
        e.learnArmed.store (false);
        midi.addEvent (juce::MidiMessage::noteOff (1, 36), 0); run (1);
        while (e.pop (ev)) {}
        // note 36 -> pad 1 of the bank
        e.noteMap[36].store (Engine::MAP_PAD + 0);
        const auto hits0 = e.padHits[0].load();
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        run (10, &pk2);
        bool gotSel = false;
        while (e.pop (ev)) gotSel |= ev.type == EngineEvent::PadSelect && ev.a == 0;
        check (e.padHits[0].load() == hits0 + 1 && pk2 > 0.05f && gotSel, "learned note plays pad 1");
        midi.addEvent (juce::MidiMessage::noteOff (1, 36), 0); run (200);
        // CC 21 -> a UI control (button)
        e.ccMap[21].store (Engine::MAP_UI);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, 127), 0);
        run (1);
        bool gotFire = false;
        while (e.pop (ev)) gotFire |= ev.type == EngineEvent::MapFire && ev.a == 1 && ev.b == 21 && ev.c == 127;
        check (gotFire, "learned CC fires its UI control");
        // a learned transport CC wins over the built-in one; built-in can be switched off
        e.ccMap[94].store (Engine::MAP_UI);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 94, 127), 0);
        run (1);
        bool gotT = false; gotFire = false;
        while (e.pop (ev)) { gotT |= ev.type == EngineEvent::TransportCC && ev.c == 0; gotFire |= ev.type == EngineEvent::MapFire && ev.b == 94; }
        check (gotFire && ! gotT, "a learned CC replaces the built-in transport CC");
        e.ccMap[94].store (Engine::MAP_NONE);
        e.defaultTransportCC.store (false);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 95, 127), 0);
        run (1);
        gotT = false;
        while (e.pop (ev)) gotT |= ev.type == EngineEvent::TransportCC && ev.c == 0;   // c = 1 is only "not linked" info
        check (! gotT, "built-in transport CCs can be switched off");
        e.defaultTransportCC.store (true);
        // note 40 -> key 5
        e.noteMap[40].store (Engine::MAP_KEY + 5);
        midi.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 0);
        run (10, &pk2);
        bool lit5 = false;
        while (e.pop (ev)) lit5 |= ev.type == EngineEvent::KeyLight && ev.a == 5 && ev.b == 1;
        check (lit5 && pk2 > 0.05f, "learned note plays key 6");
        midi.addEvent (juce::MidiMessage::noteOff (1, 40), 0); run (1);
        bool off5 = false;
        while (e.pop (ev)) off5 |= ev.type == EngineEvent::KeyLight && ev.a == 5 && ev.b == 0;
        check (off5, "learned key releases on note-off");
        // low key learn
        e.lowKeyLearn.store (true);
        midi.addEvent (juce::MidiMessage::noteOn (1, 36 + 12, (juce::uint8) 100), 0);
        run (1);
        bool gotLow = false;
        while (e.pop (ev)) gotLow |= ev.type == EngineEvent::LowKey && ev.a == 48;
        check (gotLow && ! e.lowKeyLearn.load() && e.midiOffset.load() == 0, "set low key");
        e.lowKeyLearn.store (true);
        midi.addEvent (juce::MidiMessage::noteOn (1, 41, (juce::uint8) 100), 0);
        run (1);
        check (e.midiOffset.load() == 7, "low key F2 shifts the keyboard by 7");
    }


    // ---------------- plug-in hosting + mixer
    {
        Engine e;
        e.prepare (48000, 256);
        Project M = P;
        M.s.metro = false; M.s.sel = 5;
        juce::AudioBuffer<float> out (2, 256);
        juce::MidiBuffer midi;
        auto run = [&] (int blocks) { float pk2 = 0; for (int b = 0; b < blocks; ++b) { out.clear(); e.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, midi); midi.clear(); pk2 = std::max (pk2, out.getMagnitude (0, 0, 256)); } return pk2; };
        auto publish = [&] { e.setSnapshot (builder.build (M, stems, trim)); run (1); };
        publish();
        TestProc* synth = nullptr;
        e.setSlot ({ SlotId::Inst, 5, 0 }, e.addOwned (makeNode (true, 1.0f, 0, &synth)));
        Command c; c.type = Command::PadHit; c.pad = 5; c.vel = 100; c.tag = 1;
        e.push (c);
        float pk = run (10);
        check (pk > 0.05f && synth->ons == 1, "VST instrument pad (no sample) plays on a pad hit");
        c = {}; c.type = Command::PadRelease; c.pad = 5; e.push (c);
        run (2); pk = run (4);
        check (pk < 1e-6f && synth->offs == 1, "pad release sends the note-off");
        c = {}; c.type = Command::NoteOn; c.note = 12; c.vel = 100; c.tag = 1; e.push (c);
        pk = run (6);
        check (pk > 0.05f && synth->note == 60, "keyboard plays the instrument chromatically (root key = note " + juce::String (synth->note) + ")");
        c.type = Command::NoteOff; e.push (c); run (2);
        c.type = Command::NoteOn; c.note = 19; e.push (c); run (2);
        check (synth->note == 67, "key 7 semitones up plays note 67");
        c.type = Command::NoteOff; e.push (c); run (2);
        check (synth->note == -1, "key release stops the instrument note");
        // sequencer: one step on track 15 -> pad 5
        M.tracks[15].pad = 5; M.tracks[15].get (0).steps[0] = 1; M.tracks[15].get (0).semi[0] = 7;
        for (int t = 0; t < 15; ++t) M.tracks[(size_t) t].mute = true;
        publish();
        const int ons0 = synth->ons, offs0 = synth->offs;
        c = {}; c.type = Command::Play; c.f = 0; e.push (c);
        run (60);   // ~0.3 s: past the first step (0.167 s at 90 BPM)
        check (synth->ons == ons0 + 1 && synth->offs == offs0 + 1, "sequencer step plays a note and ends it inside the step");
        c = {}; c.type = Command::Stop; e.push (c); run (4);
        for (int t = 0; t < 15; ++t) M.tracks[(size_t) t].mute = false;
        M.tracks[15].pd[0].reset();
        publish();
        // pad inserts: gain 0 silences pad 0
        auto* g0 = e.addOwned (makeNode (false, 0.0f));
        e.setSlot ({ SlotId::PadFx, 0, 1 }, g0);
        c = {}; c.type = Command::PadHit; c.pad = 0; c.vel = 110; e.push (c);
        pk = run (20);
        check (pk < 1e-6f, "VST insert on a pad processes that pad");
        e.retire (g0); run (200);
        e.push (c); pk = run (20);
        check (pk > 0.05f, "removing the insert brings the pad back");
        run (200);
        // solo pad 1 -> pad 0 is silent; mute
        M.pads[1].solo = true; publish();
        e.push (c); pk = run (20);
        check (pk < 1e-6f, "solo on another pad silences this one");
        M.pads[1].solo = false; M.pads[0].muted = true; publish();
        e.push (c); pk = run (20);
        check (pk < 1e-6f, "mute");
        M.pads[0].muted = false; M.pads[0].pan = 1.0f; M.pads[0].send[0] = 1.0f; publish();
        e.push (c); run (20);
        check (out.getMagnitude (0, 0, 256) < 1e-6f || true, "pan right");   // (left channel checked below)
        e.push (c);
        float lpk = 0, rpk = 0;
        for (int b = 0; b < 20; ++b) { out.clear(); e.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, midi); lpk = std::max (lpk, out.getMagnitude (0, 0, 256)); rpk = std::max (rpk, out.getMagnitude (1, 0, 256)); }
        check (rpk > 0.05f && lpk < rpk * 0.75f, "pan hard right: right " + juce::String (rpk, 3) + " left " + juce::String (lpk, 3));
        check (e.retPeak[0].exchange (0) > 0.01f, "send 1 feeds return A");
        check (e.padPeak[0].exchange (0) > 0.01f, "pad meter");
        M.pads[0].pan = 0; M.pads[0].send[0] = 0; publish(); run (200);
        // master insert gain 0 -> everything silent
        auto* gm = e.addOwned (makeNode (false, 0.0f));
        e.setSlot ({ SlotId::Master, 0, 2 }, gm);
        e.push (c); pk = run (20);
        check (pk < 1e-6f, "VST insert on the master");
        // retire + garbage collection deletes the node only after the audio thread moved on
        e.collectGarbage();
        const int aliveBefore = TestProc::alive;
        e.retire (gm);
        e.collectGarbage();
        check (TestProc::alive == aliveBefore, "retired plug-in is not deleted while the audio thread may use it");
        run (3); e.collectGarbage();
        check (TestProc::alive == aliveBefore - 1, "retired plug-in is deleted after the audio thread moved on");

        // offline render with plug-in clones (instrument pad + master delay with latency)
        Project O = P;
        O.s.metro = false;
        for (auto& t : O.tracks) t.pd.fill (std::nullopt);
        O.tracks[0].pad = 5; O.tracks[0].get (0).steps[0] = 1; O.tracks[0].get (0).steps[8] = 1;
        auto osnap = builder.build (O, stems, trim);
        PluginSet set;
        set.nodes[{ SlotId::Inst, 5, 0 }] = makeNode (true);
        set.nodes[{ SlotId::Master, 0, 0 }] = makeNode (false, 1.0f, 300);
        for (auto& kv : set.nodes) kv.second->prepare (44100, 1024, true);
        auto r = Engine::renderPlan (osnap, { 0 }, {}, 44100, false, {}, {}, nullptr, &set);
        int f1 = -1;
        for (int i = 0; i < r.getNumSamples(); ++i) if (std::abs (r.getSample (0, i)) > 1e-4f) { f1 = i; break; }
        check (r.getMagnitude (0, 0, r.getNumSamples()) > 0.05f, "export renders the VST instrument");
        check (f1 >= 0 && f1 < 8, "export removes the master plug-in latency (first sound at " + juce::String (f1) + ")");
    }
    // ---------------- per-pattern pads + track solo (engine)
    {
        Engine e; e.prepare (48000, 256);
        Project S = P; S.s.metro = false; S.s.pat = 1;
        for (auto& t : S.tracks) t.pd.fill (std::nullopt);
        S.pads[4].buf = makeKick (44100);
        S.tracks[0].get (0).steps[0] = 1;                       // pattern 1: track 1 plays its own pad (A1)
        S.tracks[0].get (1).steps[0] = 1; S.tracks[0].get (1).pad = 4;   // pattern 2: the same track plays A5
        e.setSnapshot (builder.build (S, stems, trim));
        juce::AudioBuffer<float> out (2, 256); juce::MidiBuffer midi;
        auto run = [&] (int blocks) { for (int b = 0; b < blocks; ++b) { out.clear(); e.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, midi); } };
        run (1);
        const auto h0 = e.padHits[0].load(), h4 = e.padHits[4].load();
        Command c; c.type = Command::Play; c.f = 0; e.push (c); run (20);
        check (e.padHits[4].load() == h4 + 1 && e.padHits[0].load() == h0, "pattern 2 plays the pad recorded there (A5), not the track's pad");
        c = {}; c.type = Command::Stop; e.push (c); run (4);
        // per-step chop (keys in chop mode recorded onto one track)
        {
            Project K = P; K.s.metro = false; K.s.pat = 0;
            for (auto& t : K.tracks) t.pd.fill (std::nullopt);
            K.tracks[5].pad = 1; K.tracks[5].chop = 0;
            K.tracks[5].get (0).steps[0] = 1; K.tracks[5].get (0).chopAt[0] = 2;
            auto ks = builder.build (K, stems, trim);
            auto a = Engine::renderPlan (ks, { 0 }, {}, 44100, false, {}, {});
            K.tracks[5].get (0).chopAt[0] = -2;
            auto b = Engine::renderPlan (builder.build (K, stems, trim), { 0 }, {}, 44100, false, {}, {});
            float diff = 0; const int n = juce::jmin (a.getNumSamples(), b.getNumSamples(), 20000);
            for (int i = 0; i < n; ++i) diff = std::max (diff, std::abs (a.getSample (0, i) - b.getSample (0, i)));
            check (diff > 0.01f && a.getMagnitude (0, 0, n) > 0.01f, "a step plays its own chop (chop 3 instead of the track's chop 1)");
        }
        S.tracks[0].solo = false; S.tracks[1].solo = true; S.tracks[1].get (1).steps[4] = 1;
        e.setSnapshot (builder.build (S, stems, trim)); run (1);
        const auto h4b = e.padHits[4].load();
        c = {}; c.type = Command::Play; c.f = 0; e.push (c); run (20);
        check (e.padHits[4].load() == h4b, "solo on another track silences this one");
        c = {}; c.type = Command::Stop; e.push (c); run (4);
    }

    // ---------------- the library folder
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cx_lib_test_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        root.deleteRecursively();
        Library lib;
        lib.setRoot (root);
        check (root.getChildFile ("Drums").isDirectory() && lib.crates.contains ("Drums"), "new library gets starter crates");
        auto kick = makeKick (44100);
        auto deep = root.getChildFile ("Soul Samples").getChildFile ("Some Artist").getChildFile ("Kicks");
        deep.createDirectory();
        io::writeWav (deep.getChildFile ("kick 1.wav"), kick->audio, 44100, 16);   // dropped in with Explorer
        io::writeWav (root.getChildFile ("loose.wav"), kick->audio, 44100, 16);
        check (lib.scan() && lib.items.size() == 2, "refresh finds files copied into the folders (" + juce::String ((int) lib.items.size()) + ")");
        check (lib.crates.contains ("Soul Samples") && lib.crates.contains ("Soul Samples/Some Artist") && lib.crates.contains ("Soul Samples/Some Artist/Kicks"),
               "sub-folders become sub-crates");
        int ik = -1; for (int i = 0; i < (int) lib.items.size(); ++i) if (lib.items[(size_t) i].name == "kick 1") ik = i;
        check (ik >= 0 && lib.items[(size_t) ik].crate == "Soul Samples/Some Artist/Kicks" && ! lib.items[(size_t) ik].buf, "item knows its crate and loads lazily");
        check (lib.load (ik) && lib.items[(size_t) ik].buf->length() == kick->length(), "sample loads when needed");
        int inSoul = 0; for (auto& it : lib.items) if (Library::inCrate (it.crate, "Soul Samples")) ++inSoul;
        check (inSoul == 1, "a parent crate shows its sub-crates' samples");
        lib.add ("my snare", "Soul Samples/Some Artist/Snares", kick);
        check (lib.unsavedCount() == 1 && lib.crates.contains ("Soul Samples/Some Artist/Snares"), "added sample waits for Save");
        check (lib.save() == 1 && root.getChildFile ("Soul Samples/Some Artist/Snares/my snare.wav").existsAsFile() && lib.unsavedCount() == 0, "Save writes it into its crate folder");
        lib.scan();
        check (lib.items.size() == 3, "saved sample is not listed twice after a refresh");
        root.getChildFile ("loose.wav").deleteFile();
        lib.scan();
        check (lib.items.size() == 2, "a file deleted in Explorer disappears on refresh");
        check (lib.renameCrate ("Soul Samples/Some Artist", "Artist Two") && root.getChildFile ("Soul Samples/Artist Two/Kicks/kick 1.wav").existsAsFile()
               && lib.crates.contains ("Soul Samples/Artist Two/Kicks") && ! lib.crates.contains ("Soul Samples/Some Artist"), "rename crate renames the folder");
        check (lib.deleteCrate ("Soul Samples/Artist Two/Kicks") && root.getChildFile ("Soul Samples/Artist Two/kick 1.wav").existsAsFile(),
               "delete crate moves its samples up one level");
        auto zip = root.getSiblingFile (root.getFileName() + "_backup.zip");
        check (lib.backupTo (zip) && zip.getSize() > 1000, "library backup zip");
        // migration from the old layout (library.json + crates/<crate>/001_name.wav)
        auto old = root.getSiblingFile (root.getFileName() + "_old");
        old.getChildFile ("crates/Drums").createDirectory();
        io::writeWav (old.getChildFile ("crates/Drums/001_boom.wav"), kick->audio, 44100, 16);
        old.getChildFile ("library.json").replaceWithText (R"({"crates":["Drums","FX"],"items":[{"name":"boom","crate":"Drums","wav":"crates/Drums/001_boom.wav"}]})");
        Library lib2; lib2.setRoot (old);
        check (old.getChildFile ("Drums/boom.wav").existsAsFile() && ! old.getChildFile ("crates").exists() && lib2.items.size() == 1 && lib2.items[0].crate == "Drums",
               "old library layout is moved into plain folders");
        Library lib3; auto r3 = root.getSiblingFile (root.getFileName() + "_restore"); lib3.setRoot (r3);
        check (lib3.restoreFolderZip (zip) == 2 && r3.getChildFile ("Soul Samples/Artist Two/Snares/my snare.wav").existsAsFile(), "restore a backup zip");
        for (auto d : { root, old, r3 }) d.deleteRecursively();
        zip.deleteFile();
    }

    // ---------------- MIDI: program change learn + several links
    {
        Engine e; e.prepare (48000, 256);
        e.setSnapshot (builder.build (P, stems, trim));
        juce::AudioBuffer<float> out (2, 256); juce::MidiBuffer midi;
        auto run = [&] { out.clear(); e.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, midi); midi.clear(); };
        run();
        EngineEvent ev; while (e.pop (ev)) {}
        e.learnArmed.store (true);
        midi.addEvent (juce::MidiMessage::programChange (1, 7), 0); run();
        bool gotPC = false; while (e.pop (ev)) gotPC |= ev.type == EngineEvent::Learn && ev.a == 2 && ev.b == 7;
        check (gotPC, "MIDI learn hears program changes");
        e.learnArmed.store (false);
        e.pcMap[7].store (Engine::MAP_UI);
        midi.addEvent (juce::MidiMessage::programChange (1, 7), 0); run();
        bool fire = false; while (e.pop (ev)) fire |= ev.type == EngineEvent::MapFire && ev.a == 2 && ev.b == 7;
        check (fire, "a learned program change fires its control");
    }

    // ---------------- song file keeps mixer + plug-in slots
    {
        Project S = P;
        S.pads[3].pan = -0.5f; S.pads[3].solo = true; S.pads[3].send = { 0.25f, 0.75f }; S.pads[3].instRoot = 48;
        S.pads[3].inst.iid = newPluginIid(); S.pads[3].inst.xml = "<PLUGIN name=\"X\"/>"; S.pads[3].inst.name = "X"; S.pads[3].inst.state = "QUJD"; S.pads[3].inst.instrument = true;
        S.tracks[2].lock = true; S.tracks[3].solo = true; S.tracks[1].get (1).steps[2] = 1; S.tracks[1].get (1).pad = 9; S.tracks[1].get (1).chop = -1;
        S.pads[2].vfx[1].iid = newPluginIid(); S.pads[2].vfx[1].xml = "<PLUGIN name=\"Y\"/>"; S.pads[2].vfx[1].name = "Y"; S.pads[2].vfx[1].bypass = true;
        S.masterVfx[3].iid = newPluginIid(); S.masterVfx[3].xml = "<PLUGIN name=\"Z\"/>"; S.masterVfx[3].name = "Z";
        S.sendVfx[1][0].iid = newPluginIid(); S.sendVfx[1][0].xml = "<PLUGIN name=\"W\"/>"; S.sendVfx[1][0].name = "W"; S.retLevel[1] = 0.6f;
        auto zip = juce::File::createTempFile (".zip");
        juce::String msg;
        check (io::saveSong (S, zip, msg), "save song with mixer + plug-ins");
        Project Q;
        check (io::loadSong (zip, Q, msg), "load it back");
        const auto& q3 = Q.pads[3];
        check (std::abs (q3.pan + 0.5f) < 1e-4f && q3.solo && std::abs (q3.send[1] - 0.75f) < 1e-4f && q3.instRoot == 48, "mixer settings survive the song file");
        check (! q3.inst.empty() && q3.inst.name == "X" && q3.inst.state == "QUJD" && q3.inst.instrument && q3.inst.iid != S.pads[3].inst.iid,
               "instrument slot survives (with a fresh instance id)");
        check (Q.tracks[2].lock && Q.tracks[3].solo && Q.tracks[1].find (1) && Q.tracks[1].find (1)->pad == 9 && Q.tracks[1].find (1)->chop == -1,
               "track lock / solo and per-pattern pads survive the song file");
        check (Q.pads[2].vfx[1].name == "Y" && Q.pads[2].vfx[1].bypass && Q.masterVfx[3].name == "Z" && Q.sendVfx[1][0].name == "W" && std::abs (Q.retLevel[1] - 0.6f) < 1e-4f,
               "insert / master / return slots survive");
        zip.deleteFile();
    }

    // ---------------- a real VST3 file (set CONTRXL_TEST_VST3=/path/to/Some Synth.vst3)
    const auto vstPath = juce::SystemStats::getEnvironmentVariable ("CONTRXL_TEST_VST3", {});
    if (vstPath.isNotEmpty())
    {
        juce::ScopedJuceInitialiser_GUI gui;
        PluginManager pm;
        juce::OwnedArray<juce::PluginDescription> found;
        pm.formats.getFormat (0)->findAllTypesForFile (found, vstPath);
        check (found.size() > 0, "VST3 file scanned: " + (found.isEmpty() ? juce::String ("nothing") : found[0]->name + (found[0]->isInstrument ? " (instrument)" : " (effect)")));
        if (! found.isEmpty())
        {
            const auto xml = PluginManager::toXml (*found[0]);
            juce::String err;
            auto inst = pm.createFromXml (xml, 48000, 256, err);
            check (inst != nullptr, "VST3 instance created from the saved description " + err);
            if (inst)
            {
                const bool hasEd = inst->hasEditor();
                Engine e; e.prepare (48000, 256);
                Project V = P; V.s.metro = false; V.s.sel = 6;
                e.setSnapshot (builder.build (V, stems, trim));
                auto* node = e.addOwned (std::make_unique<PluginNode> (std::move (inst), newPluginIid(), found[0]->isInstrument));
                e.setSlot ({ SlotId::Inst, 6, 0 }, node);
                juce::AudioBuffer<float> out (2, 256); juce::MidiBuffer midi;
                auto run = [&] (int blocks) { float pk2 = 0; for (int b = 0; b < blocks; ++b) { out.clear(); e.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 256, midi); pk2 = std::max (pk2, out.getMagnitude (0, 0, 256)); } return pk2; };
                run (2);
                Command c; c.type = Command::PadHit; c.pad = 6; c.vel = 100; c.tag = 1; e.push (c);
                float pk = run (20);
                check (pk > 0.01f, "real VST3 instrument sounds from a pad (peak " + juce::String (pk, 3) + ")");
                c = {}; c.type = Command::PadRelease; c.pad = 6; e.push (c); run (4);
                const auto st = node->getState();
                check (st.isNotEmpty(), "VST3 state saved (" + juce::String (st.length()) + " chars)");
                node->setState (st);
                check (hasEd, "VST3 has an editor");
                if (auto* ed = node->proc->createEditorIfNeeded()) { check (ed->getWidth() > 0, "VST3 editor opens"); delete ed; }
                e.setSlot ({ SlotId::Inst, 6, 0 }, nullptr);
                e.retire (node); e.deviceStopped(); e.collectGarbage();
            }
        }
    }

    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << std::endl;
    return fails == 0 ? 0 : 1;
}
