// CONTRXL 25 - offline (non real-time) sample processing.
// These run on the message thread or a background thread, never on the audio thread.
// Each one returns a NEW SampleData; the input is never modified.
#pragma once
#include "model/Model.h"
#include <functional>

namespace cx::dsp
{
using Progress = std::function<void (float)>;   // 0..1

void      normalizeInPlace (juce::AudioBuffer<float>& b);            // peak -> 0.98
SamplePtr normalized (const SampleData& s);
SamplePtr reversed (const SampleData& s);
SamplePtr lofi (const SampleData& s, int bits, int rate);             // sample&hold + bit crush
SamplePtr retroProcess (const SampleData& s, int retroIndex);         // sampler emulations (6.3)
SamplePtr wsolaStretch (const SampleData& s, double alpha, const Progress& p = {});  // keep pitch
SamplePtr resampleStretch (const SampleData& s, double alpha);        // tape-style (pitch follows)
SamplePtr slice (const SampleData& s, double t0, double t1);          // seconds
SamplePtr trimShape (const SampleData& s, float fadeInMs, float fadeOutMs, float gainDb);
SamplePtr gainScaled (const SampleData& s, float gain);
SamplePtr mixDown (const std::array<SamplePtr, 4>& stems, const StemMix& mix);
std::vector<float> transientMarkers (const SampleData& s, int maxN = 16);

// RBJ biquad (same maths as the web version's rbj()/biq())
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    enum Type { LP, HP, BP, PEAK, LSHELF, HSHELF };
    static Biquad make (Type t, double f0, double Q, double dB, double sr);
    void process (float* x, int n) const;   // in place, fresh state
};
} // namespace cx::dsp
