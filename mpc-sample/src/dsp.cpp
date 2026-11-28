#include "dsp.h"
#include <random>

namespace cx::dsp
{
static constexpr double TWO_PI = 6.283185307179586;

static std::shared_ptr<SampleData> sameShape (const SampleData& s, int length = -1)
{
    auto o = std::make_shared<SampleData>();
    o->sampleRate = s.sampleRate;
    const int n = std::max (1, length < 0 ? s.length() : length);
    for (int c = 0; c < s.channels(); ++c) o->ch[c].assign ((size_t) n, 0.0f);
    return o;
}

SamplePtr normalized (const SampleData& s)
{
    auto o = std::make_shared<SampleData> (s);
    const float pk = s.peak();
    if (pk > 1.0e-6f) for (int c = 0; c < o->channels(); ++c) for (auto& v : o->ch[c]) v *= 0.98f / pk;
    return o;
}

SamplePtr reversed (const SampleData& s)
{
    auto o = std::make_shared<SampleData> (s);
    for (int c = 0; c < o->channels(); ++c) std::reverse (o->ch[c].begin(), o->ch[c].end());
    return o;
}

SamplePtr mono (const SampleData& s)
{
    if (s.channels() == 1) return std::make_shared<SampleData> (s);
    auto o = std::make_shared<SampleData>();
    o->sampleRate = s.sampleRate;
    o->ch[0].resize ((size_t) s.length());
    for (int i = 0; i < s.length(); ++i) o->ch[0][(size_t) i] = 0.5f * (s.ch[0][(size_t) i] + s.ch[1][(size_t) i]);
    return o;
}

SamplePtr lofi (const SampleData& s, int bits, int rate)
{
    auto out = sameShape (s);
    const double q = std::pow (2.0, bits - 1), inc = rate / s.sampleRate;
    for (int c = 0; c < s.channels(); ++c)
    {
        const float* src = s.ch[c].data(); float* d = out->ch[c].data();
        double ph = 1.0; float v = 0.0f;
        for (int i = 0; i < s.length(); ++i) { ph += inc; if (ph >= 1.0) { ph -= 1.0; v = (float) (std::round (src[i] * q) / q); } d[i] = v; }
    }
    return out;
}

SamplePtr slice (const SampleData& s, double t0, double t1)
{
    const int a = clampv ((int) std::floor (t0 * s.sampleRate), 0, s.length());
    const int b = clampv ((int) std::ceil (t1 * s.sampleRate), a + 1, std::max (a + 1, s.length()));
    const int n = std::max (1, std::min (b, s.length()) - a);
    auto o = std::make_shared<SampleData>();
    o->sampleRate = s.sampleRate;
    for (int c = 0; c < s.channels(); ++c) o->ch[c].assign (s.ch[c].begin() + a, s.ch[c].begin() + std::min (s.length(), a + n));
    if (o->ch[0].empty()) o->ch[0].assign (1, 0.0f);
    if (s.channels() > 1 && o->ch[1].size() != o->ch[0].size()) o->ch[1].resize (o->ch[0].size());
    return o;
}

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
        default:
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
        case 1: p.bits = 16; p.lp = 17500; p.sat = 1.5; p.satMix = .5; p.eq = { { Biquad::PEAK, 250, .8, 2.2 }, { Biquad::HSHELF, 9000, .7, -1.5 } }; p.round = .12; p.noise = -90; break;
        case 2: p.bits = 12; p.hold = 40000; p.lp = 13500; p.lpN = 2; p.hp = 30; p.sat = 1.3; p.satMix = .5; p.eq = { { Biquad::PEAK, 200, .9, 3.5 } }; p.round = .7; p.noise = -68; break;
        case 3: p.bits = 16; p.lp = 16500; p.sat = 1.6; p.satMix = .5; p.eq = { { Biquad::LSHELF, 90, .7, 2.5 } }; p.round = .35; p.noise = -88; break;
        case 4: p.bits = 12; p.hold = 26040; p.lp = 11500; p.lpN = 2; p.hp = 25; p.sat = 1.2; p.satMix = .35; p.eq = { { Biquad::PEAK, 120, .8, 1.5 } }; p.round = 0; p.noise = -62; break;
        case 5: p.bits = 9; p.hold = 16000; p.lp = 7500; p.sat = 1.4; p.satMix = .4; p.round = 1.4; p.noise = -54; p.dirt = .5; p.wob = .0012; break;
        case 6: p.bits = 14; p.hold = 32000; p.lp = 14000; p.sat = 2.2; p.satMix = .7; p.hasComp = true; p.comp = { -20, 4, 6, 180 }; p.smear = 14; p.round = .4; p.noise = -76; p.eq = { { Biquad::PEAK, 180, .8, 1.5 } }; break;
        case 7: p.bits = 13; p.hold = 36000; p.lp = 15000; p.hp = 40; p.sat = 1.7; p.satMix = .5; p.hasComp = true; p.comp = { -18, 6, 3, 110 }; p.wob = .0008; p.dirt = .3; p.noise = -70; p.eq = { { Biquad::PEAK, 1500, .9, 3 }, { Biquad::LSHELF, 70, .7, -2 } }; break;
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
    const int N = s.length(), nc = std::max (1, s.channels());
    uint32_t seed = 12345;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (double) seed / 4294967296.0 * 2.0 - 1.0; };
    std::mt19937 rng (777);
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    std::vector<std::vector<float>> ch ((size_t) nc);
    double rmsIn = 0;
    for (int c = 0; c < nc; ++c)
    {
        ch[(size_t) c] = s.ch[c];
        for (float v : s.ch[c]) rmsIn += (double) v * v;
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
        if (P.hold > 0 && P.hold < sr) { const double st = P.hold / sr; double ph = 1; float h = 0; for (int i = 0; i < N; ++i) { ph += st; if (ph >= 1) { ph -= 1; h = x[(size_t) i]; } x[(size_t) i] = h; } }
        if (P.bits > 0 && P.bits < 16) { const double q = std::pow (2.0, P.bits - 1); for (auto& v : x) v = (float) (std::round (v * q) / q); }
        if (P.sat > 0) { const double k = P.sat, t = std::tanh (k), m = P.satMix; for (auto& v : x) v = (float) (v * (1 - m) + std::tanh (k * v) / t * m); }
        for (auto& e : P.eq) Biquad::make (e.t, e.f, e.q, e.g, sr).process (x.data(), N);
        if (P.hp > 0) Biquad::make (Biquad::HP, P.hp, .707, 0, sr).process (x.data(), N);
        if (P.lp > 0) { auto f = Biquad::make (Biquad::LP, P.lp, .707, 0, sr); f.process (x.data(), N); if (P.lpN == 2) f.process (x.data(), N); }
        if (P.round > 0) { const double al = 1 - std::exp (-1 / (P.round * .001 * sr)); double y = 0; for (auto& v : x) { y += al * (v - y); v = (float) y; } }
    }
    if (P.hasComp)
    {
        const auto& C = P.comp;
        const double at = std::exp (-1 / (C.att * .001 * sr)), rl = std::exp (-1 / (C.rel * .001 * sr));
        double env = 0;
        for (int i = 0; i < N; ++i)
        {
            double pk = 0;
            for (int c = 0; c < nc; ++c) pk = std::max (pk, (double) std::fabs (ch[(size_t) c][(size_t) i]));
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
            for (auto& v : a) { if (uni (rng) < rate) tail = (uni (rng) * .5 + .1) * P.dirt * (uni (rng) < .5 ? -1 : 1); v += (float) tail; tail *= .6; }
        }
    }
    double rmsOut = 0; float pk = 0;
    for (auto& a : ch) for (auto v : a) { rmsOut += (double) v * v; pk = std::max (pk, std::fabs (v)); }
    rmsOut = std::sqrt (rmsOut / ((double) N * nc) + 1e-12);
    double g = clampv (rmsIn / rmsOut, 0.5, 2.0);
    if (pk * g > .98) g = .98 / pk;
    auto out = std::make_shared<SampleData>();
    out->sampleRate = sr;
    for (int c = 0; c < nc; ++c) { out->ch[c].resize ((size_t) N); for (int i = 0; i < N; ++i) out->ch[c][(size_t) i] = (float) (ch[(size_t) c][(size_t) i] * g); }
    return out;
}

SamplePtr wsolaStretch (const SampleData& s, double alpha)
{
    const double sr = s.sampleRate;
    const int C = s.channels(), n = s.length();
    int N = (int) std::round (sr * .05 / 2) * 2; N = clampv (N, 512, 4096);
    const int Hs = N >> 1, tol = N >> 2;
    const double Ha = Hs / alpha;
    const int outLen = std::max (1, (int) std::round (n * alpha)), pad = n + N * 2 + tol * 2;
    std::vector<std::vector<float>> inCh ((size_t) C, std::vector<float> ((size_t) pad, 0.0f));
    std::vector<float> ref ((size_t) pad, 0.0f);
    for (int c = 0; c < C; ++c)
    {
        std::copy (s.ch[c].begin(), s.ch[c].end(), inCh[(size_t) c].begin());
        for (int i = 0; i < n; ++i) ref[(size_t) i] += s.ch[c][(size_t) i] / (float) C;
    }
    std::vector<std::vector<float>> out ((size_t) C, std::vector<float> ((size_t) (outLen + N * 2), 0.0f));
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (.5 - .5 * std::cos (TWO_PI * i / N));
    const int frames = (int) std::ceil ((double) outLen / Hs) + 1;
    int prev = 0;
    for (int k = 0; k < frames; ++k)
    {
        const int nom = std::min (n, (int) std::round (k * Ha));
        int pos = nom;
        if (k > 0)
        {
            const int tgt = prev + Hs, lo = std::max (0, nom - tol), hi = std::min (n, nom + tol);
            double best = -1.0e300;
            for (int cand = lo; cand <= hi; cand += 2)
            {
                double sum = 0;
                for (int i = 0; i < Hs; i += 4) { const size_t a = (size_t) (cand + i), b = (size_t) (tgt + i); if (a < ref.size() && b < ref.size()) sum += (double) ref[a] * ref[b]; }
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
                if (oi < oc.size() && ii < ic.size()) oc[oi] += ((k == 0 && i < Hs) ? 1.0f : win[(size_t) i]) * ic[ii];
            }
        }
        prev = pos;
    }
    auto res = std::make_shared<SampleData>();
    res->sampleRate = sr;
    for (int c = 0; c < C; ++c) res->ch[c].assign (out[(size_t) c].begin(), out[(size_t) c].begin() + outLen);
    return res;
}

std::vector<float> transientMarkers (const SampleData& s, int maxN)
{
    std::vector<float> res;
    const float* d = s.ch[0].data();
    const int hop = 512, n = s.length() / hop;
    if (n < 8) return res;
    std::vector<float> env ((size_t) n), o ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        double sum = 0;
        for (int j = 0; j < hop; ++j) { double x = d[i * hop + j]; if (s.channels() > 1) x = 0.5 * (x + s.ch[1][(size_t) (i * hop + j)]); sum += x * x; }
        env[(size_t) i] = (float) std::sqrt (sum / hop);
    }
    float mx = 0;
    for (int i = 1; i < n; ++i) { o[(size_t) i] = std::max (0.0f, env[(size_t) i] - env[(size_t) i - 1]); mx = std::max (mx, o[(size_t) i]); }
    if (mx <= 0) return res;
    const int gap = std::max (2, (int) std::ceil (.06 * s.sampleRate / hop));
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
    for (int i : pick) res.push_back (clampv ((float) ((i - 1) * hop) / (float) s.length(), .001f, .999f));
    return res;
}
} // namespace cx::dsp

namespace cx
{
static constexpr double TWO_PI_D = 6.283185307179586;
// sin (2 pi ph) for a phase in [0, 1) - LFOs only (error < 0.001)
static inline float lfoSin (double ph) noexcept
{
    float x = (float) (ph - std::floor (ph)) * 2.0f - 1.0f;          // -1..1 = -pi..pi
    float y = 4.0f * x * (1.0f - std::fabs (x));                      // parabola
    y = 0.225f * (y * std::fabs (y) - y) + y;                          // refinement
    return -y;
}
// tanh, Pade 3/2, exact at the ends (saturation shaping)
static inline float ftanh (float x) noexcept
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void BQ::setRBJ (int type, double f0, double Q, double sr)
{
    f0 = clampv (f0, 10.0, sr * 0.45);
    Q = std::max (0.05, Q);
    const double w = TWO_PI_D * f0 / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * Q);
    double nb0, nb1, nb2;
    if (type == 0) { nb0 = (1 - c) / 2; nb1 = 1 - c; nb2 = nb0; }
    else if (type == 1) { nb0 = (1 + c) / 2; nb1 = -(1 + c); nb2 = nb0; }
    else { nb0 = al; nb1 = 0; nb2 = -al; }
    const double a0 = 1 + al, na1 = -2 * c, na2 = 1 - al;
    b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0); a1 = (float) (na1 / a0); a2 = (float) (na2 / a0);
}
void BQ::setLP (double f, double qDb, double sr) { setRBJ (0, f, std::pow (10.0, qDb / 20.0), sr); }
void BQ::setHP (double f, double qDb, double sr) { setRBJ (1, f, std::pow (10.0, qDb / 20.0), sr); }
void BQ::setBP (double f, double q, double sr) { setRBJ (2, f, q, sr); }

// ------------------------------------------------------------------ Freeverb
static const int CT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int AT[4] = { 556, 441, 341, 225 };
void Freeverb::prepare (double sr)
{
    const double k = sr / 44100.0;
    for (int i = 0; i < 8; ++i) { cL[i].b.assign ((size_t) std::max (1, (int) (CT[i] * k)), 0.0f); cR[i].b.assign ((size_t) std::max (1, (int) ((CT[i] + 23) * k)), 0.0f); }
    for (int i = 0; i < 4; ++i) { aL[i].b.assign ((size_t) std::max (1, (int) (AT[i] * k)), 0.0f); aR[i].b.assign ((size_t) std::max (1, (int) ((AT[i] + 23) * k)), 0.0f); }
    reset();
}
void Freeverb::reset()
{
    for (int i = 0; i < 8; ++i) { std::fill (cL[i].b.begin(), cL[i].b.end(), 0.0f); std::fill (cR[i].b.begin(), cR[i].b.end(), 0.0f); cL[i].store = cR[i].store = 0; cL[i].i = cR[i].i = 0; }
    for (int i = 0; i < 4; ++i) { std::fill (aL[i].b.begin(), aL[i].b.end(), 0.0f); std::fill (aR[i].b.begin(), aR[i].b.end(), 0.0f); aL[i].i = aR[i].i = 0; }
}
void Freeverb::process (float* L, float* R, int n)
{
    const float fb = roomSize * 0.28f + 0.7f, d1 = damping * 0.4f, d2 = 1.0f - d1;
    const float gain = 0.015f, wet = 3.0f * 0.5f;   // wet level 1, width 1
    auto comb = [fb, d1, d2] (Comb& c, float in)
    {
        const float out = c.b[(size_t) c.i];
        c.store = out * d2 + c.store * d1;
        c.b[(size_t) c.i] = in + c.store * fb;
        if (++c.i >= (int) c.b.size()) c.i = 0;
        return out;
    };
    auto ap = [] (AP& a, float in)
    {
        const float bo = a.b[(size_t) a.i];
        a.b[(size_t) a.i] = in + bo * 0.5f;
        if (++a.i >= (int) a.b.size()) a.i = 0;
        return bo - in;
    };
    for (int s = 0; s < n; ++s)
    {
        const float in = (L[s] + R[s]) * gain;
        float ol = 0, orr = 0;
        for (int i = 0; i < 8; ++i) { ol += comb (cL[i], in); orr += comb (cR[i], in); }
        for (int i = 0; i < 4; ++i) { ol = ap (aL[i], ol); orr = ap (aR[i], orr); }
        L[s] = ol * wet; R[s] = orr * wet;
    }
}

// ------------------------------------------------------------------ pad effects rack
enum { M_SAT = 1, M_VIN = 2, M_CHO = 4, M_FLA = 8, M_SCA = 16, M_DEL = 32, M_ECO = 64, M_REV = 128 };

void PadFx::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    vinD[0].prepare ((int) (sr * 0.05)); vinD[1].prepare ((int) (sr * 0.05));
    choD[0].prepare ((int) (sr * 0.1));  choD[1].prepare ((int) (sr * 0.1));
    flaD[0].prepare ((int) (sr * 0.05)); flaD[1].prepare ((int) (sr * 0.05));
    delD[0].prepare ((int) (sr * 2.5));  delD[1].prepare ((int) (sr * 2.5));
    ecoD[0].prepare ((int) (sr * 2.0));  ecoD[1].prepare ((int) (sr * 2.0));
    reverb.prepare (sr);
    wetL.assign ((size_t) std::max (16, maxBlock), 0.0f); wetR.assign ((size_t) std::max (16, maxBlock), 0.0f);
    satLP.setLP (9500, 0.5, sr); scaLP.setLP (600, 1.0, sr); delHP.setHP (120, 1.0, sr); ecoHP.setHP (180, 1.0, sr); revHP.setHP (160, 1.0, sr);
    reset();
}

void PadFx::reset() { usedMask = 0xFF; for (int b = 0; b < 8; ++b) resetModule (1 << b); onMask = 0; quietSamples = 0; lastOutputLevel = 0; }

void PadFx::resetModule (int bit)
{
    if (! (usedMask & (uint32_t) bit)) return;   // still silent since prepare / the last reset
    usedMask &= ~(uint32_t) bit;
    switch (bit)
    {
        case M_SAT: satS[0].reset(); satS[1].reset(); break;
        case M_VIN: for (int c = 0; c < 2; ++c) { vinHs[c].reset(); vinLs[c].reset(); vinD[c].reset(); } vinPh[0] = vinPh[1] = 0; break;
        case M_CHO: choD[0].reset(); choD[1].reset(); choPh[0] = 0; choPh[1] = 0.25; break;
        case M_FLA: flaD[0].reset(); flaD[1].reset(); flaPh = 0; break;
        case M_SCA: scaPh[0] = scaPh[1] = scaPh[2] = 0; scaS.reset(); break;
        case M_DEL: for (int c = 0; c < 2; ++c) { delD[c].reset(); delHs[c].reset(); delLs[c].reset(); } delTimeS = -1; break;
        case M_ECO: for (int c = 0; c < 2; ++c) { ecoD[c].reset(); ecoHs[c].reset(); ecoLs[c].reset(); } ecoTimeS = -1; ecoPh1 = ecoPh2 = 0; break;
        case M_REV: reverb.reset(); revHs[0].reset(); revHs[1].reset(); break;
        default: break;
    }
}

void PadFx::process (float* L, float* R, int n, const FxParams& fx, double bpm) noexcept
{
    float* ch[2] = { L, R };
    const uint32_t mask = (fx.satOn ? M_SAT : 0) | (fx.vinOn ? M_VIN : 0) | (fx.choOn ? M_CHO : 0) | (fx.flaOn ? M_FLA : 0)
                        | (fx.scaOn ? M_SCA : 0) | (fx.delOn ? M_DEL : 0) | (fx.ecoOn ? M_ECO : 0) | (fx.revOn ? M_REV : 0);
    const uint32_t newlyOn = mask & ~onMask;
    for (int b = 0; b < 8; ++b) if (newlyOn & (1u << b)) resetModule (1 << b);
    onMask = mask;
    usedMask |= mask;
    if (bpm <= 0) bpm = 84;
    const float fsr = (float) sr;
    if (fx.satOn)
    {
        const float d = fx.satDrive, k = 1 + d * 11, m = fx.satMix, tk = std::tanh (k);
        const float pg = 1 + d * 2.5f, post = 0.5f / (std::tanh (k * std::min (1.0f, 0.5f * pg)) / tk);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                const float w = satS[c].run (satLP, ftanh (k * x * pg) / tk * post);
                ch[c][i] = x * (1 - m) + w * m;
            }
    }
    if (fx.vinOn)
    {
        const float t = fx.vinTone, w = fx.vinWow;
        vinLP.setLP (16000.0 * std::pow (3200.0 / 16000.0, (double) t), 1.0, sr);
        vinHP.setHP (20.0 + t * 150.0, 1.0, sr);
        const double inc1 = 0.55 / sr, inc2 = 6.3 / sr;
        for (int i = 0; i < n; ++i)
        {
            const float dt = 0.015f + (float) (lfoSin (vinPh[0]) * w * 0.0035 + lfoSin (vinPh[1]) * w * 0.0006);
            vinPh[0] += inc1; if (vinPh[0] >= 1) vinPh[0] -= 1;
            vinPh[1] += inc2; if (vinPh[1] >= 1) vinPh[1] -= 1;
            for (int c = 0; c < 2; ++c) { const float x = vinLs[c].run (vinLP, vinHs[c].run (vinHP, ch[c][i])); vinD[c].push (x); ch[c][i] = vinD[c].read (dt * fsr); }
        }
    }
    if (fx.choOn)
    {
        const float r = fx.choRate, dp = fx.choDepth * 0.006f, mix = fx.choMix;
        const double inc0 = r / sr, inc1 = r * 1.17 / sr;
        for (int i = 0; i < n; ++i)
        {
            const float d0 = (0.018f + lfoSin (choPh[0]) * dp) * fsr;
            const float d1 = (0.027f + lfoSin (choPh[1]) * dp) * fsr;
            choPh[0] += inc0; if (choPh[0] >= 1) choPh[0] -= 1;
            choPh[1] += inc1; if (choPh[1] >= 1) choPh[1] -= 1;
            for (int c = 0; c < 2; ++c) { choD[c].push (ch[c][i]); ch[c][i] += 0.7f * (choD[c].read (d0) + choD[c].read (d1)) * mix; }
        }
    }
    if (fx.flaOn)
    {
        const float dp = fx.flaDepth * 0.0025f, fb = std::min (0.92f, fx.flaFb * 0.85f), mix = fx.flaMix;
        const double inc = fx.flaRate / sr;
        for (int i = 0; i < n; ++i)
        {
            const float d = (0.0055f + lfoSin (flaPh) * dp) * fsr;
            flaPh += inc; if (flaPh >= 1) flaPh -= 1;
            for (int c = 0; c < 2; ++c) { const float y = flaD[c].read (d); flaD[c].push (ch[c][i] + y * fb); ch[c][i] += y * mix; }
        }
    }
    if (fx.scaOn)
    {
        static const float W[3] = { 0.5f, 0.3f, 0.2f }, Dv[3] = { 1, 3, 5 }, DIV[4] = { 1, 2, 4, 8 };
        const float div = DIV[clampv ((int) std::round (fx.scaDiv), 1, 4) - 1];
        const double r = bpm / 60.0 * div;
        const float t = -1.01f + fx.scaAmt * 1.5f, m = fx.scaMix;
        for (int i = 0; i < n; ++i)
        {
            float sum = 0;
            for (int k = 0; k < 3; ++k) { sum += W[k] * (scaPh[k] < 0.5 ? 1.0f : -1.0f); scaPh[k] += r / 2.0 / Dv[k] / sr; if (scaPh[k] >= 1) scaPh[k] -= 1; }
            const float g = scaS.run (scaLP, clampv ((sum - t) / 0.08f + 0.5f, 0.0f, 1.0f));
            for (int c = 0; c < 2; ++c) { const float x = ch[c][i]; ch[c][i] = x * (1 - m) + x * g * m; }
        }
    }
    if (fx.delOn)
    {
        static const float BT[5] = { 1.0f, 0.75f, 0.5f, 0.375f, 0.25f };
        const float beat = (float) (60.0 / bpm);
        const float target = std::min (2.4f, BT[clampv ((int) std::round (fx.delTime), 1, 5) - 1] * beat);
        if (delTimeS < 0) delTimeS = target;
        const float sm = 1.0f - std::exp (-1.0f / (0.03f * fsr));
        const float fb = std::min (0.95f, fx.delFb), mix = fx.delMix;
        delLP.setLP (800.0 * std::pow (10000.0 / 800.0, (double) fx.delTone), 0.5, sr);
        for (int i = 0; i < n; ++i)
        {
            delTimeS += (target - delTimeS) * sm;
            for (int c = 0; c < 2; ++c)
            {
                const float y = delD[c].read (delTimeS * fsr);
                const float l = delLs[c].run (delLP, delHs[c].run (delHP, y));
                delD[c].push (ch[c][i] + l * fb);
                ch[c][i] += l * mix;
            }
        }
    }
    if (fx.ecoOn)
    {
        const float target = fx.ecoTime / 1000.0f, w = fx.ecoWob;
        if (ecoTimeS < 0) ecoTimeS = target;
        const float sm = 1.0f - std::exp (-1.0f / (0.05f * fsr));
        const float fb = std::min (0.93f, fx.ecoFb), mix = fx.ecoMix;
        const float k = 2.2f, tk = std::tanh (k), sg = tk / k;
        ecoLP.setLP (1200.0 * std::pow (8000.0 / 1200.0, (double) fx.ecoTone), 0.6, sr);
        for (int i = 0; i < n; ++i)
        {
            ecoTimeS += (target - ecoTimeS) * sm;
            const double tri = 4.0 * std::fabs (ecoPh2 - 0.5) - 1.0;
            const float dt = ecoTimeS + (float) (lfoSin (ecoPh1) * w * 0.004 + tri * w * 0.0007);
            ecoPh1 += 0.6 / sr; if (ecoPh1 >= 1) ecoPh1 -= 1;
            ecoPh2 += 7.3 / sr; if (ecoPh2 >= 1) ecoPh2 -= 1;
            for (int c = 0; c < 2; ++c)
            {
                const float y = ecoD[c].read (dt * fsr);
                const float l = ecoLs[c].run (ecoLP, ecoHs[c].run (ecoHP, y));
                const float s = ftanh (k * l) / tk * sg;
                ecoD[c].push (ch[c][i] + s * fb);
                ch[c][i] += s * mix;
            }
        }
    }
    if (fx.revOn && n <= (int) wetL.size())
    {
        reverb.set (0.5f + fx.revSize * 0.48f, 0.15f + fx.revDamp * 0.75f);
        for (int i = 0; i < n; ++i) { wetL[(size_t) i] = revHs[0].run (revHP, L[i]); wetR[(size_t) i] = revHs[1].run (revHP, R[i]); }
        reverb.process (wetL.data(), wetR.data(), n);
        const float mix = fx.revMix * 0.9f;
        for (int i = 0; i < n; ++i) { L[i] += wetL[(size_t) i] * mix; R[i] += wetR[(size_t) i] * mix; }
    }
    float pk = 0;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::fabs (L[i]), std::fabs (R[i])));
    lastOutputLevel = pk;
}
} // namespace cx
