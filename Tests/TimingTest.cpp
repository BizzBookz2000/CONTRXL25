// Live-record timing: the player hits exactly when they HEAR a metronome click. Which step is recorded?
#include "engine/Engine.h"
#include "dsp/Stems.h"
#include <iostream>
using namespace cx;

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const int SR = 48000, BS = 256;
    for (int lat : { 0, 1024, 4800 })
    for (int extraMs : { 0, 150 })
    for (int pathMidi : { 0, 1 })
    for (int delayClicks : { 0, 1 })   // hit on beat 1 (click 0) or on beat 2 (click 1)
    {
        Engine e; e.prepare (SR, BS);
        e.setOutputLatency (lat);
        e.recOffsetMs.store ((float) extraMs);   // the player's sound arrives extraMs later than the computer reports
        Project P; P.s.metro = true; P.s.bpm = 84; for (auto& b : P.pbpm) b = 84;
        dsp::StemStore stems; TrimState trim;
        { juce::AudioBuffer<float> b (1, 2000); b.clear(); b.setSample (0, 0, 0.001f); P.pads[0].buf = makeSample (std::move (b), SR); }
        SnapshotBuilder builder;
        e.setSnapshot (builder.build (P, stems, trim));
        juce::AudioBuffer<float> out (2, BS); juce::MidiBuffer midi;
        e.recordArmed.store (true);
        Command c; c.type = Command::Play; c.f = 0; e.push (c);
        juce::int64 t = 0; std::vector<juce::int64> clicks; bool prevLoud = false;
        juce::int64 hitAt = -1; bool hitSent = false; double pos = -99;
        for (int b = 0; b < 2000 && pos < -50; ++b)
        {
            if (! hitSent && hitAt >= 0 && t + BS > hitAt)
            {
                const int off = (int) (hitAt - t);
                if (pathMidi) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), juce::jlimit (0, BS - 1, off));
                else { Command h; h.type = Command::PadHit; h.pad = 0; h.vel = 100; e.push (h); }   // UI: applied at block start
                hitSent = true;
            }
            out.clear();
            e.process (nullptr, 0, out.getArrayOfWritePointers(), 2, BS, midi);
            midi.clear();
            for (int i = 0; i < BS; ++i)
            {
                const bool loud = std::abs (out.getSample (0, i)) > 0.02f;
                if (loud && ! prevLoud && (clicks.empty() || t + i - clicks.back() > SR / 20)) clicks.push_back (t + i);
                prevLoud = loud;
            }
            t += BS;
            // the 2nd bar's click 0 (5th click) +delay: the player hears it 'lat' samples later and hits then
            if (hitAt < 0 && (int) clicks.size() == 5 + delayClicks) hitAt = clicks.back() + lat + extraMs * SR / 1000;
            EngineEvent ev;
            while (e.pop (ev)) if (ev.type == EngineEvent::RecHit) pos = ev.pos;
        }
        std::cout << "latency " << lat << " +" << extraMs << "ms unreported, corrected" << (pathMidi ? " MIDI" : " UI  ") << " hit on click " << delayClicks
                  << " -> pos " << pos << "  (expected " << (delayClicks == 0 ? "0 / 16" : "4") << ")\n";
    }
    return 0;
}
