#include "Effects.h"
#include <cmath>

namespace cx
{
static constexpr double PI_D = juce::MathConstants<double>::pi;
static constexpr double TWO_PI_D = juce::MathConstants<double>::twoPi;

// ------------------------------------------------------------------ biquad coefficient helpers
void BQ::setRBJ (int type, double f0, double Q, double sr)
{
    f0 = juce::jlimit (10.0, sr * 0.45, f0);
    Q = std::max (0.05, Q);
    const double w = TWO_PI_D * f0 / sr, c = std::cos (w), s = std::sin (w), al = s / (2 * Q);
    double nb0, nb1, nb2, a0, na1, na2;
    if (type == 0)      { nb0 = (1 - c) / 2; nb1 = 1 - c;    nb2 = nb0; }
    else if (type == 1) { nb0 = (1 + c) / 2; nb1 = -(1 + c); nb2 = nb0; }
    else                { nb0 = al;          nb1 = 0;        nb2 = -al; }
    a0 = 1 + al; na1 = -2 * c; na2 = 1 - al;
    b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
    a1 = (float) (na1 / a0); a2 = (float) (na2 / a0);
}
void BQ::setLP (double f, double qDb, double sr) { setRBJ (0, f, std::pow (10.0, qDb / 20.0), sr); }
void BQ::setHP (double f, double qDb, double sr) { setRBJ (1, f, std::pow (10.0, qDb / 20.0), sr); }
void BQ::setBP (double f, double q, double sr)   { setRBJ (2, f, q, sr); }

// ------------------------------------------------------------------ module bits
enum { M_SAT = 1, M_VIN = 2, M_CHO = 4, M_FLA = 8, M_SCA = 16, M_DEL = 32, M_ECO = 64, M_REV = 128 };

void PadFx::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    vinD[0].prepare ((int) (sr * 0.05)); vinD[1].prepare ((int) (sr * 0.05));
    choD[0].prepare ((int) (sr * 0.1));  choD[1].prepare ((int) (sr * 0.1));
    flaD[0].prepare ((int) (sr * 0.05)); flaD[1].prepare ((int) (sr * 0.05));
    delD[0].prepare ((int) (sr * 2.5));  delD[1].prepare ((int) (sr * 2.5));
    ecoD[0].prepare ((int) (sr * 2.0));  ecoD[1].prepare ((int) (sr * 2.0));
    reverb.setSampleRate (sr);
    wetL.assign ((size_t) juce::jmax (16, maxBlock), 0.0f);
    wetR.assign ((size_t) juce::jmax (16, maxBlock), 0.0f);
    satLP.setLP (9500, 0.5, sr);
    scaLP.setLP (600, 1.0, sr);
    delHP.setHP (120, 1.0, sr);
    ecoHP.setHP (180, 1.0, sr);
    revHP.setHP (160, 1.0, sr);
    reset();
}

void PadFx::reset()
{
    for (int b = 0; b < 8; ++b) resetModule (1 << b);
    onMask = 0; quietSamples = 0; lastOutputLevel = 0;
}

void PadFx::resetModule (int bit)
{
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
    const juce::uint32 mask = (fx.satOn ? M_SAT : 0) | (fx.vinOn ? M_VIN : 0) | (fx.choOn ? M_CHO : 0) | (fx.flaOn ? M_FLA : 0)
                            | (fx.scaOn ? M_SCA : 0) | (fx.delOn ? M_DEL : 0) | (fx.ecoOn ? M_ECO : 0) | (fx.revOn ? M_REV : 0);
    const juce::uint32 newlyOn = mask & ~onMask;
    for (int b = 0; b < 8; ++b) if (newlyOn & (1u << b)) resetModule (1 << b);
    onMask = mask;
    if (bpm <= 0) bpm = 84;
    const float fsr = (float) sr;

    // ---------------- saturation: tanh waveshaper, drive, mix
    if (fx.satOn)
    {
        const float d = fx.satDrive, k = 1 + d * 11, m = fx.satMix, tk = std::tanh (k);
        const float pg = 1 + d * 2.5f, post = 0.5f / (std::tanh (k * std::min (1.0f, 0.5f * pg)) / tk);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                const float w = satS[c].run (satLP, std::tanh (k * x * pg) / tk * post);
                ch[c][i] = x * (1 - m) + w * m;
            }
    }
    // ---------------- vinyl: age tone + wow (the crackle/hiss is added per voice by the engine)
    if (fx.vinOn)
    {
        const float t = fx.vinTone, w = fx.vinWow;
        vinLP.setLP (16000.0 * std::pow (3200.0 / 16000.0, (double) t), 1.0, sr);
        vinHP.setHP (20.0 + t * 150.0, 1.0, sr);
        const double inc1 = 0.55 / sr, inc2 = 6.3 / sr;
        for (int i = 0; i < n; ++i)
        {
            const float dt = 0.015f + (float) (std::sin (TWO_PI_D * vinPh[0]) * w * 0.0035 + std::sin (TWO_PI_D * vinPh[1]) * w * 0.0006);
            vinPh[0] += inc1; if (vinPh[0] >= 1) vinPh[0] -= 1;
            vinPh[1] += inc2; if (vinPh[1] >= 1) vinPh[1] -= 1;
            for (int c = 0; c < 2; ++c)
            {
                const float x = vinLs[c].run (vinLP, vinHs[c].run (vinHP, ch[c][i]));
                vinD[c].push (x);
                ch[c][i] = vinD[c].read (dt * fsr);
            }
        }
    }
    // ---------------- chorus: two modulated delays (18 / 27 ms)
    if (fx.choOn)
    {
        const float r = fx.choRate, dp = fx.choDepth * 0.006f, mix = fx.choMix;
        const double inc0 = r / sr, inc1 = r * 1.17 / sr;
        for (int i = 0; i < n; ++i)
        {
            const float d0 = (0.018f + (float) std::sin (TWO_PI_D * choPh[0]) * dp) * fsr;
            const float d1 = (0.027f + (float) std::sin (TWO_PI_D * choPh[1]) * dp) * fsr;
            choPh[0] += inc0; if (choPh[0] >= 1) choPh[0] -= 1;
            choPh[1] += inc1; if (choPh[1] >= 1) choPh[1] -= 1;
            for (int c = 0; c < 2; ++c)
            {
                choD[c].push (ch[c][i]);
                const float wet = 0.7f * (choD[c].read (d0) + choD[c].read (d1));
                ch[c][i] += wet * mix;
            }
        }
    }
    // ---------------- flanger: short modulated delay with feedback
    if (fx.flaOn)
    {
        const float dp = fx.flaDepth * 0.0025f, fb = std::min (0.92f, fx.flaFb * 0.85f), mix = fx.flaMix;
        const double inc = fx.flaRate / sr;
        for (int i = 0; i < n; ++i)
        {
            const float d = (0.0055f + (float) std::sin (TWO_PI_D * flaPh) * dp) * fsr;
            flaPh += inc; if (flaPh >= 1) flaPh -= 1;
            for (int c = 0; c < 2; ++c)
            {
                const float y = flaD[c].read (d);
                flaD[c].push (ch[c][i] + y * fb);
                ch[c][i] += y * mix;
            }
        }
    }
    // ---------------- scatter: tempo-synced square LFOs drive a gate
    if (fx.scaOn)
    {
        static const float W[3] = { 0.5f, 0.3f, 0.2f };
        static const float Dv[3] = { 1, 3, 5 };
        static const float DIV[4] = { 1, 2, 4, 8 };
        const float div = DIV[juce::jlimit (1, 4, (int) std::round (fx.scaDiv)) - 1];
        const double r = bpm / 60.0 * div;
        const float t = -1.01f + fx.scaAmt * 1.5f, m = fx.scaMix;
        for (int i = 0; i < n; ++i)
        {
            float sum = 0;
            for (int k = 0; k < 3; ++k)
            {
                sum += W[k] * (scaPh[k] < 0.5 ? 1.0f : -1.0f);
                scaPh[k] += r / 2.0 / Dv[k] / sr; if (scaPh[k] >= 1) scaPh[k] -= 1;
            }
            const float shaped = juce::jlimit (0.0f, 1.0f, (sum - t) / 0.08f + 0.5f);
            const float g = scaS.run (scaLP, shaped);
            for (int c = 0; c < 2; ++c) { const float x = ch[c][i]; ch[c][i] = x * (1 - m) + x * g * m; }
        }
    }
    // ---------------- delay: tempo-synced, feedback through a tone filter
    if (fx.delOn)
    {
        static const float BT[5] = { 1.0f, 0.75f, 0.5f, 0.375f, 0.25f };
        const float beat = (float) (60.0 / bpm);
        const float target = std::min (2.4f, BT[juce::jlimit (1, 5, (int) std::round (fx.delTime)) - 1] * beat);
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
    // ---------------- echo: tape-style, darker and dirtier repeats with wobble
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
            const double tri = 4.0 * std::abs (ecoPh2 - 0.5) - 1.0;
            const float dt = ecoTimeS + (float) (std::sin (TWO_PI_D * ecoPh1) * w * 0.004 + tri * w * 0.0007);
            ecoPh1 += 0.6 / sr; if (ecoPh1 >= 1) ecoPh1 -= 1;
            ecoPh2 += 7.3 / sr; if (ecoPh2 >= 1) ecoPh2 -= 1;
            for (int c = 0; c < 2; ++c)
            {
                const float y = ecoD[c].read (dt * fsr);
                const float l = ecoLs[c].run (ecoLP, ecoHs[c].run (ecoHP, y));
                const float s = std::tanh (k * l) / tk * sg;
                ecoD[c].push (ch[c][i] + s * fb);
                ch[c][i] += s * mix;
            }
        }
    }
    // ---------------- reverb (Freeverb-style; the web version uses a generated impulse response)
    if (fx.revOn && n <= (int) wetL.size())
    {
        juce::Reverb::Parameters p;
        p.roomSize = 0.5f + fx.revSize * 0.48f;
        p.damping = 0.15f + fx.revDamp * 0.75f;
        p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.width = 1.0f; p.freezeMode = 0.0f;
        reverb.setParameters (p);
        for (int i = 0; i < n; ++i) { wetL[(size_t) i] = revHs[0].run (revHP, L[i]); wetR[(size_t) i] = revHs[1].run (revHP, R[i]); }
        reverb.processStereo (wetL.data(), wetR.data(), n);
        const float mix = fx.revMix * 0.9f;
        for (int i = 0; i < n; ++i) { L[i] += wetL[(size_t) i] * mix; R[i] += wetR[(size_t) i] * mix; }
    }

    float pk = 0;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
    lastOutputLevel = pk;
}
} // namespace cx
