#include "Offline.h"
#include <cmath>
#include <random>

namespace cx::dsp
{
using juce::AudioBuffer;
static constexpr double TWO_PI = juce::MathConstants<double>::twoPi;

static AudioBuffer<float> sameShape (const SampleData& s, int length = -1)
{
    AudioBuffer<float> b (juce::jmax (1, s.channels()), juce::jmax (1, length < 0 ? s.length() : length));
    b.clear();
    return b;
}

// ------------------------------------------------------------------ basics
void normalizeInPlace (AudioBuffer<float>& b)
{
    float pk = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) pk = std::max (pk, b.getMagnitude (c, 0, b.getNumSamples()));
    if (pk > 1.0e-6f) b.applyGain (0.98f / pk);
}

SamplePtr normalized (const SampleData& s)
{
    AudioBuffer<float> b (s.audio);
    normalizeInPlace (b);
    return makeSample (std::move (b), s.sampleRate);
}

SamplePtr reversed (const SampleData& s)
{
    AudioBuffer<float> b (s.audio);
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto* d = b.getWritePointer (c);
        std::reverse (d, d + b.getNumSamples());
    }
    return makeSample (std::move (b), s.sampleRate);
}

SamplePtr gainScaled (const SampleData& s, float gain)
{
    AudioBuffer<float> b (s.audio);
    b.applyGain (gain);
    return makeSample (std::move (b), s.sampleRate);
}

SamplePtr lofi (const SampleData& s, int bits, int rate)
{
    auto out = sameShape (s);
    const double q = std::pow (2.0, bits - 1), inc = rate / s.sampleRate;
    for (int c = 0; c < s.channels(); ++c)
    {
        const float* src = s.audio.getReadPointer (c);
        float* d = out.getWritePointer (c);
        double ph = 1.0; float v = 0.0f;
        for (int i = 0; i < s.length(); ++i)
        {
            ph += inc;
            if (ph >= 1.0) { ph -= 1.0; v = (float) (std::round (src[i] * q) / q); }
            d[i] = v;
        }
    }
    return makeSample (std::move (out), s.sampleRate);
}

SamplePtr slice (const SampleData& s, double t0, double t1)
{
    const int a = juce::jlimit (0, s.length(), (int) std::floor (t0 * s.sampleRate));
    const int b = juce::jlimit (a + 1, juce::jmax (a + 1, s.length()), (int) std::ceil (t1 * s.sampleRate));
    const int n = juce::jmax (1, juce::jmin (b, s.length()) - a);
    AudioBuffer<float> out (s.channels(), n);
    for (int c = 0; c < s.channels(); ++c) out.copyFrom (c, 0, s.audio, c, a, n);
    return makeSample (std::move (out), s.sampleRate);
}

SamplePtr trimShape (const SampleData& s, float fiMs, float foMs, float gDb)
{
    AudioBuffer<float> b (s.audio);
    const int n = b.getNumSamples();
    const float g = std::pow (10.0f, gDb / 20.0f);
    const int fi = juce::jmin (n, (int) (fiMs * 0.001 * s.sampleRate));
    const int fo = juce::jmin (n, (int) (foMs * 0.001 * s.sampleRate));
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        float* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            float k = g;
            if (i < fi)      k *= 0.5f - 0.5f * std::cos ((float) juce::MathConstants<float>::pi * (float) i / (float) fi);
            if (i >= n - fo) k *= 0.5f - 0.5f * std::cos ((float) juce::MathConstants<float>::pi * (float) (n - 1 - i) / (float) fo);
            d[i] = juce::jlimit (-1.0f, 1.0f, d[i] * k);
        }
    }
    return makeSample (std::move (b), s.sampleRate);
}

SamplePtr mixDown (const std::array<SamplePtr, 4>& stems, const StemMix& mix)
{
    if (! stems[0]) return nullptr;
    auto out = sameShape (*stems[0]);
    for (int k = 0; k < 4; ++k)
    {
        if (! stems[(size_t) k] || mix.lv[(size_t) k] == 0.0f) continue;
        for (int c = 0; c < out.getNumChannels(); ++c)
            out.addFrom (c, 0, stems[(size_t) k]->audio, juce::jmin (c, stems[(size_t) k]->channels() - 1), 0,
                         juce::jmin (out.getNumSamples(), stems[(size_t) k]->length()), mix.lv[(size_t) k]);
    }
    return makeSample (std::move (out), stems[0]->sampleRate);
}

// ------------------------------------------------------------------ biquad
Biquad Biquad::make (Type type, double f0, double Q, double dB, double sr)
{
    f0 = std::min (f0, sr * 0.45);
    const double A = std::pow (10.0, dB / 40.0), w = TWO_PI * f0 / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * Q);
    double b0, b1, b2, a0, a1, a2;
    switch (type)
    {
        case LP:   b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
        case HP:   b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
        case BP:   b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
        case PEAK: b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; break;
        case LSHELF:
        {
            const double sq = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) - (A - 1) * c + sq); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sq);
            a0 = (A + 1) + (A - 1) * c + sq; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sq; break;
        }
        default: // HSHELF
        {
            const double sq = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
            a0 = (A + 1) - (A - 1) * c + sq; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sq; break;
        }
    }
    Biquad q;
    q.b0 = (float) (b0 / a0); q.b1 = (float) (b1 / a0); q.b2 = (float) (b2 / a0); q.a1 = (float) (a1 / a0); q.a2 = (float) (a2 / a0);
    return q;
}

void Biquad::process (float* x, int n) const
{
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double v = x[i], y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = v; y2 = y1; y1 = y; x[i] = (float) y;
    }
}

// ------------------------------------------------------------------ retro modes (pseudocode 6.3)
namespace
{
    struct EqBand { Biquad::Type t; double f, q, g; };
    struct Comp { double thr, ratio, att, rel; };
    struct RetroProfile
    {
        int bits = 0; double hold = 0, lp = 0; int lpN = 1; double hp = 0;
        double sat = 0, satMix = 1;
        std::vector<EqBand> eq;
        double round = 0, noise = -120, dirt = 0, wob = 0;
        bool hasComp = false; Comp comp {};
        int smear = 0;
    };

    RetroProfile profile (int idx)
    {
        RetroProfile p;
        switch (idx)
        {
            case 1: // ASR-10
                p.bits = 16; p.lp = 17500; p.sat = 1.5; p.satMix = .5;
                p.eq = { { Biquad::PEAK, 250, .8, 2.2 }, { Biquad::HSHELF, 9000, .7, -1.5 } };
                p.round = .12; p.noise = -90; break;
            case 2: // MPC60
                p.bits = 12; p.hold = 40000; p.lp = 13500; p.lpN = 2; p.hp = 30; p.sat = 1.3; p.satMix = .5;
                p.eq = { { Biquad::PEAK, 200, .9, 3.5 } }; p.round = .7; p.noise = -68; break;
            case 3: // MPC3000
                p.bits = 16; p.lp = 16500; p.sat = 1.6; p.satMix = .5;
                p.eq = { { Biquad::LSHELF, 90, .7, 2.5 } }; p.round = .35; p.noise = -88; break;
            case 4: // SP-1200
                p.bits = 12; p.hold = 26040; p.lp = 11500; p.lpN = 2; p.hp = 25; p.sat = 1.2; p.satMix = .35;
                p.eq = { { Biquad::PEAK, 120, .8, 1.5 } }; p.round = 0; p.noise = -62; break;
            case 5: // SP-202
                p.bits = 9; p.hold = 16000; p.lp = 7500; p.sat = 1.4; p.satMix = .4; p.round = 1.4;
                p.noise = -54; p.dirt = .5; p.wob = .0012; break;
            case 6: // SP-303
                p.bits = 14; p.hold = 32000; p.lp = 14000; p.sat = 2.2; p.satMix = .7;
                p.hasComp = true; p.comp = { -20, 4, 6, 180 }; p.smear = 14; p.round = .4; p.noise = -76;
                p.eq = { { Biquad::PEAK, 180, .8, 1.5 } }; break;
            case 7: // SP-404
                p.bits = 13; p.hold = 36000; p.lp = 15000; p.hp = 40; p.sat = 1.7; p.satMix = .5;
                p.hasComp = true; p.comp = { -18, 6, 3, 110 }; p.wob = .0008; p.dirt = .3; p.noise = -70;
                p.eq = { { Biquad::PEAK, 1500, .9, 3 }, { Biquad::LSHELF, 70, .7, -2 } }; break;
            default: break;
        }
        return p;
    }
}

SamplePtr retroProcess (const SampleData& s, int idx)
{
    if (idx <= 0 || idx >= NRETRO) return nullptr;
    const auto P = profile (idx);
    const double sr = s.sampleRate;
    const int N = s.length(), nc = juce::jmax (1, s.channels());
    uint32_t seed = 12345;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (double) seed / 4294967296.0 * 2.0 - 1.0; };
    std::mt19937 rng (777);
    std::uniform_real_distribution<double> uni (0.0, 1.0);

    std::vector<std::vector<float>> ch ((size_t) nc);
    double rmsIn = 0;
    for (int c = 0; c < nc; ++c)
    {
        const float* src = s.audio.getReadPointer (juce::jmin (c, s.channels() - 1));
        ch[(size_t) c].assign (src, src + N);
        for (int i = 0; i < N; ++i) rmsIn += (double) src[i] * src[i];
    }
    rmsIn = std::sqrt (rmsIn / ((double) N * nc) + 1e-12);

    for (int c = 0; c < nc; ++c)
    {
        auto& x = ch[(size_t) c];
        if (P.wob > 0)
        {
            std::vector<float> y ((size_t) N);
            const double d = P.wob * sr, f1 = .7 + .15 * c, f2 = 5.5;
            for (int i = 0; i < N; ++i)
            {
                const double t = i / sr, p = i + d * (std::sin (TWO_PI * f1 * t) + .25 * std::sin (TWO_PI * f2 * t));
                const int i0 = (int) std::floor (p); const double fr = p - i0;
                const double s0 = (i0 < 0 || i0 >= N) ? 0 : x[(size_t) i0];
                const double s1 = (i0 + 1 < 0 || i0 + 1 >= N) ? 0 : x[(size_t) i0 + 1];
                y[(size_t) i] = (float) (s0 + (s1 - s0) * fr);
            }
            x.swap (y);
        }
        if (P.hold > 0 && P.hold < sr)
        {
            const double st = P.hold / sr; double ph = 1; float h = 0;
            for (int i = 0; i < N; ++i) { ph += st; if (ph >= 1) { ph -= 1; h = x[(size_t) i]; } x[(size_t) i] = h; }
        }
        if (P.bits > 0 && P.bits < 16)
        {
            const double q = std::pow (2.0, P.bits - 1);
            for (auto& v : x) v = (float) (std::round (v * q) / q);
        }
        if (P.sat > 0)
        {
            const double k = P.sat, t = std::tanh (k), m = P.satMix;
            for (auto& v : x) v = (float) (v * (1 - m) + std::tanh (k * v) / t * m);
        }
        for (auto& e : P.eq) Biquad::make (e.t, e.f, e.q, e.g, sr).process (x.data(), N);
        if (P.hp > 0) Biquad::make (Biquad::HP, P.hp, .707, 0, sr).process (x.data(), N);
        if (P.lp > 0)
        {
            auto f = Biquad::make (Biquad::LP, P.lp, .707, 0, sr);
            f.process (x.data(), N);
            if (P.lpN == 2) f.process (x.data(), N);
        }
        if (P.round > 0)
        {
            const double al = 1 - std::exp (-1 / (P.round * .001 * sr)); double y = 0;
            for (auto& v : x) { y += al * (v - y); v = (float) y; }
        }
    }
    if (P.hasComp)
    {
        const auto& C = P.comp;
        const double at = std::exp (-1 / (C.att * .001 * sr)), rl = std::exp (-1 / (C.rel * .001 * sr));
        double env = 0;
        for (int i = 0; i < N; ++i)
        {
            double pk = 0;
            for (int c = 0; c < nc; ++c) pk = std::max (pk, (double) std::abs (ch[(size_t) c][(size_t) i]));
            env = pk > env ? at * env + (1 - at) * pk : rl * env + (1 - rl) * pk;
            const double db = 20 * std::log10 (env + 1e-9), over = db - C.thr, gr = over > 0 ? -over * (1 - 1 / C.ratio) : 0;
            const float g = (float) std::pow (10.0, gr / 20);
            for (int c = 0; c < nc; ++c) ch[(size_t) c][(size_t) i] *= g;
        }
    }
    if (P.smear > 0 && nc > 1)
    {
        auto& r = ch[1]; const std::vector<float> o (r);
        for (int i = 0; i < N; ++i) { const float v = i >= P.smear ? o[(size_t) (i - P.smear)] : 0.0f; r[(size_t) i] = o[(size_t) i] * .6f + v * .4f; }
    }
    const double nz = std::pow (10.0, P.noise / 20);
    for (auto& a : ch)
    {
        for (auto& v : a) v += (float) (rnd() * nz);
        if (P.dirt > 0)
        {
            const double rate = P.dirt * 8 / sr; double tail = 0;
            for (auto& v : a)
            {
                if (uni (rng) < rate) tail = (uni (rng) * .5 + .1) * P.dirt * (uni (rng) < .5 ? -1 : 1);
                v += (float) tail; tail *= .6;
            }
        }
    }
    double rmsOut = 0; float pk = 0;
    for (auto& a : ch) for (auto v : a) { rmsOut += (double) v * v; pk = std::max (pk, std::abs (v)); }
    rmsOut = std::sqrt (rmsOut / ((double) N * nc) + 1e-12);
    double g = juce::jlimit (0.5, 2.0, rmsIn / rmsOut);
    if (pk * g > .98) g = .98 / pk;

    AudioBuffer<float> out (nc, juce::jmax (1, N));
    for (int c = 0; c < nc; ++c)
    {
        float* d = out.getWritePointer (c);
        for (int i = 0; i < N; ++i) d[i] = (float) (ch[(size_t) c][(size_t) i] * g);
    }
    return makeSample (std::move (out), sr);
}

// ------------------------------------------------------------------ time stretch (pseudocode 6.2)
SamplePtr wsolaStretch (const SampleData& s, double alpha, const Progress& prog)
{
    const double sr = s.sampleRate;
    const int C = s.channels(), n = s.length();
    int N = (int) std::round (sr * .05 / 2) * 2; N = juce::jlimit (512, 4096, N);
    const int Hs = N >> 1, tol = N >> 2;
    const double Ha = Hs / alpha;
    const int outLen = juce::jmax (1, (int) std::round (n * alpha)), pad = n + N * 2 + tol * 2;

    std::vector<std::vector<float>> inCh ((size_t) C, std::vector<float> ((size_t) pad, 0.0f));
    std::vector<float> ref ((size_t) pad, 0.0f);
    for (int c = 0; c < C; ++c)
    {
        const float* src = s.audio.getReadPointer (c);
        std::copy (src, src + n, inCh[(size_t) c].begin());
        for (int i = 0; i < n; ++i) ref[(size_t) i] += src[i] / (float) C;
    }
    std::vector<std::vector<float>> out ((size_t) C, std::vector<float> ((size_t) (outLen + N * 2), 0.0f));
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (.5 - .5 * std::cos (TWO_PI * i / N));

    const int frames = (int) std::ceil ((double) outLen / Hs) + 1;
    int prev = 0;
    for (int k = 0; k < frames; ++k)
    {
        const int nom = juce::jmin (n, (int) std::round (k * Ha));
        int pos = nom;
        if (k > 0)
        {
            const int tgt = prev + Hs, lo = juce::jmax (0, nom - tol), hi = juce::jmin (n, nom + tol);
            double best = -1.0e300;
            for (int cand = lo; cand <= hi; cand += 2)
            {
                double sum = 0;
                for (int i = 0; i < Hs; i += 4)
                {
                    const size_t a = (size_t) (cand + i), b = (size_t) (tgt + i);
                    if (a < ref.size() && b < ref.size()) sum += (double) ref[a] * ref[b];
                }
                if (sum > best) { best = sum; pos = cand; }
            }
        }
        else pos = 0;
        const int o = k * Hs;
        for (int c = 0; c < C; ++c)
        {
            auto& oc = out[(size_t) c]; auto& ic = inCh[(size_t) c];
            for (int i = 0; i < N; ++i)
            {
                const size_t oi = (size_t) (o + i), ii = (size_t) (pos + i);
                if (oi < oc.size() && ii < ic.size())
                    oc[oi] += ((k == 0 && i < Hs) ? 1.0f : win[(size_t) i]) * ic[ii];
            }
        }
        prev = pos;
        if (prog && k % 48 == 0) prog ((float) k / (float) frames);
    }
    AudioBuffer<float> b (C, outLen);
    for (int c = 0; c < C; ++c) b.copyFrom (c, 0, out[(size_t) c].data(), outLen);
    return makeSample (std::move (b), sr);
}

SamplePtr resampleStretch (const SampleData& s, double alpha)
{
    const int n = s.length(), m = juce::jmax (1, (int) std::round (n * alpha));
    AudioBuffer<float> b (s.channels(), m);
    for (int c = 0; c < s.channels(); ++c)
    {
        const float* src = s.audio.getReadPointer (c);
        float* d = b.getWritePointer (c);
        for (int i = 0; i < m; ++i)
        {
            const double x = i / alpha; const int j = (int) std::floor (x); const double f = x - j;
            const float a = j < n ? src[j] : 0.0f, bb = src[juce::jmin (n - 1, j + 1)];
            d[i] = (float) (a * (1 - f) + bb * f);
        }
    }
    return makeSample (std::move (b), s.sampleRate);
}

// ------------------------------------------------------------------ transient detection (5.3)
std::vector<float> transientMarkers (const SampleData& s, int maxN)
{
    std::vector<float> res;
    const float* d = s.audio.getReadPointer (0);
    const int hop = 512, n = s.length() / hop;
    if (n < 8) return res;
    std::vector<float> env ((size_t) n), o ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        double sum = 0;
        for (int j = 0; j < hop; ++j) { const double x = d[i * hop + j]; sum += x * x; }
        env[(size_t) i] = (float) std::sqrt (sum / hop);
    }
    float mx = 0;
    for (int i = 1; i < n; ++i) { o[(size_t) i] = std::max (0.0f, env[(size_t) i] - env[(size_t) i - 1]); mx = std::max (mx, o[(size_t) i]); }
    if (mx <= 0) return res;
    const int gap = juce::jmax (2, (int) std::ceil (.06 * s.sampleRate / hop));
    std::vector<int> cand;
    for (int i = 2; i < n - 1; ++i)
        if (o[(size_t) i] > mx * .3f && o[(size_t) i] >= o[(size_t) i - 1] && o[(size_t) i] >= o[(size_t) i + 1]) cand.push_back (i);
    std::sort (cand.begin(), cand.end(), [&] (int a, int b) { return o[(size_t) a] > o[(size_t) b]; });
    std::vector<int> pick;
    for (int i : cand)
    {
        if ((int) pick.size() >= maxN - 1) break;
        if (i * hop < s.sampleRate * .03) continue;
        bool ok = true;
        for (int j : pick) if (std::abs (j - i) < gap) { ok = false; break; }
        if (ok) pick.push_back (i);
    }
    std::sort (pick.begin(), pick.end());
    for (int i : pick) res.push_back (juce::jlimit (.001f, .999f, (float) ((i - 1) * hop) / (float) s.length()));
    return res;
}

} // namespace cx::dsp
