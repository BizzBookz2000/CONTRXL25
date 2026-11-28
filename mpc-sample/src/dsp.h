// CONTRXL 25 DSP - offline sample processing and the real-time pad effects rack
// (ported from the desktop app so pads sound the same).
#pragma once
#include "model.h"

namespace cx::dsp
{
struct Biquad
{
    enum Type { LP, HP, BP, PEAK, LSHELF, HSHELF };
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    static Biquad make (Type, double f0, double Q, double dB, double sr);
    void process (float* x, int n) const;
};

SamplePtr normalized (const SampleData&);
SamplePtr reversed (const SampleData&);
SamplePtr lofi (const SampleData&, int bits, int rate);
SamplePtr slice (const SampleData&, double t0, double t1);
SamplePtr retroProcess (const SampleData&, int retroIndex);
SamplePtr wsolaStretch (const SampleData&, double alpha);   // alpha > 1 = longer, same pitch
std::vector<float> transientMarkers (const SampleData&, int maxN);
SamplePtr mono (const SampleData&);
} // namespace cx::dsp

namespace cx
{
struct BQ
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    void setLP (double f, double qDb, double sr);
    void setHP (double f, double qDb, double sr);
    void setBP (double f, double q, double sr);
    void setRBJ (int type, double f, double qLinear, double sr);
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

struct DelayLine
{
    std::vector<float> buf; int w = 0, mask = 0;
    void prepare (int maxSamples) { int n = 1; while (n < maxSamples + 4) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (float d) const noexcept
    {
        d = clampv (d, 1.0f, (float) (mask - 2));
        const float rp = (float) w - d;
        const int i0 = (int) std::floor (rp); const float fr = rp - (float) i0;
        const float a = buf[(size_t) (i0 & mask)], b = buf[(size_t) ((i0 + 1) & mask)];
        return a + (b - a) * fr;
    }
};

// Freeverb (Jezar's public-domain algorithm)
class Freeverb
{
public:
    void prepare (double sr);
    void reset();
    void set (float room, float damp) { roomSize = room; damping = damp; }
    void process (float* L, float* R, int n);   // in place, wet only
private:
    struct Comb { std::vector<float> b; int i = 0; float store = 0; };
    struct AP { std::vector<float> b; int i = 0; };
    Comb cL[8], cR[8]; AP aL[4], aR[4];
    float roomSize = 0.5f, damping = 0.5f;
};

class PadFx
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* L, float* R, int n, const FxParams& fx, double bpm) noexcept;
    float lastOutputLevel = 0.0f;
    int quietSamples = 0;
private:
    double sr = SR;
    uint32_t onMask = 0, usedMask = 0;
    BQ satLP; BQState satS[2];
    BQ vinHP, vinLP; BQState vinHs[2], vinLs[2]; DelayLine vinD[2]; double vinPh[2] { 0, 0 };
    DelayLine choD[2]; double choPh[2] { 0, 0.25 };
    DelayLine flaD[2]; double flaPh = 0;
    double scaPh[3] { 0, 0, 0 }; BQ scaLP; BQState scaS;
    DelayLine delD[2]; BQ delHP, delLP; BQState delHs[2], delLs[2]; float delTimeS = -1;
    DelayLine ecoD[2]; BQ ecoHP, ecoLP; BQState ecoHs[2], ecoLs[2]; float ecoTimeS = -1; double ecoPh1 = 0, ecoPh2 = 0;
    BQ revHP; BQState revHs[2]; Freeverb reverb;
    std::vector<float> wetL, wetR;
    void resetModule (int bit);
};
} // namespace cx
