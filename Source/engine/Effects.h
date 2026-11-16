// CONTRXL 25 - per-pad effects rack (pseudocode section 7), real-time.
// Chain order: saturation -> vinyl -> chorus -> flanger -> scatter -> delay -> echo -> reverb.
// A module only runs when its switch is on; switching it on resets its state.
#pragma once
#include "model/Model.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace cx
{
// Transposed direct form II biquad with per-channel state; coefficients can change every block.
struct BQ
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    // Web Audio style: for LP/HP the Q argument is in dB (resonance), for BP it is plain Q.
    void setLP (double f, double qDb, double sr);
    void setHP (double f, double qDb, double sr);
    void setBP (double f, double q, double sr);
    void setRBJ (int type, double f, double qLinear, double sr);   // 0 lp, 1 hp, 2 bp
};
struct BQState
{
    float z1 = 0, z2 = 0;
    inline float run (const BQ& c, float x) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

// circular delay line with fractional (linear) read
struct DelayLine
{
    std::vector<float> buf; int w = 0, mask = 0;
    void prepare (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (float delaySamples) const noexcept   // delay measured from the newest sample
    {
        const float d = juce::jlimit (1.0f, (float) (mask - 2), delaySamples);
        const float rp = (float) w - d;
        const int i0 = (int) std::floor (rp); const float fr = rp - (float) i0;
        const float a = buf[(size_t) (i0 & mask)], b = buf[(size_t) ((i0 + 1) & mask)];
        return a + (b - a) * fr;
    }
};

class PadFx
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    // processes the stereo bus in place
    void process (float* L, float* R, int n, const FxParams& fx, double bpm) noexcept;
    float lastOutputLevel = 0.0f;     // peak of the last block (for idle detection)
    int quietSamples = 0;

private:
    double sr = 48000.0;
    juce::uint32 onMask = 0;          // which modules were on last block

    // saturation
    BQ satLP; BQState satS[2];
    // vinyl
    BQ vinHP, vinLP; BQState vinHs[2], vinLs[2]; DelayLine vinD[2]; double vinPh[2] { 0, 0 };
    // chorus
    DelayLine choD[2]; double choPh[2] { 0, 0.25 };
    // flanger
    DelayLine flaD[2]; double flaPh = 0;
    // scatter
    double scaPh[3] { 0, 0, 0 }; BQ scaLP; BQState scaS;
    // delay
    DelayLine delD[2]; BQ delHP, delLP; BQState delHs[2], delLs[2]; float delTimeS = -1;
    // echo
    DelayLine ecoD[2]; BQ ecoHP, ecoLP; BQState ecoHs[2], ecoLs[2]; float ecoTimeS = -1; double ecoPh1 = 0, ecoPh2 = 0;
    // reverb
    BQ revHP; BQState revHs[2]; juce::Reverb reverb;
    std::vector<float> wetL, wetR;

    void resetModule (int bit);
};
} // namespace cx
