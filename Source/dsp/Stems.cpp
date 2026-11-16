#include "Stems.h"
#include <juce_dsp/juce_dsp.h>

namespace cx::dsp
{
static const char* const SKEYS[4]  = { "v", "b", "i", "d" };
static const char* const SNAMES[4] = { "Vocals", "Bass", "Instruments", "Drums" };
const char* stemKey (int i)  { return SKEYS[juce::jlimit (0, 3, i)]; }
const char* stemName (int i) { return SNAMES[juce::jlimit (0, 3, i)]; }

std::array<SamplePtr, 4> separateStems (const SampleData& buf, const Progress& prog, const std::function<bool()>& cancel)
{
    using C = juce::dsp::Complex<float>;
    constexpr int ORDER = 11, N = 1 << ORDER, H = 512, B = N / 2 + 1;
    constexpr int HT = 8, HF = 5; constexpr float PM = 0.5f;
    const double sr = buf.sampleRate;
    const int len = buf.length(), nc = buf.channels();
    const float* L = buf.audio.getReadPointer (0);
    const float* R = nc > 1 ? buf.audio.getReadPointer (1) : L;

    juce::dsp::FFT fft (ORDER);
    std::vector<float> win (N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    const int frames = (int) std::ceil ((double) (len + N) / H) + 1, total = (frames - 1) * H + N;

    std::vector<C> in ((size_t) N), spec ((size_t) N);
    auto frameFFT = [&] (int t)
    {
        const int s = t * H - N + H;
        for (int i = 0; i < N; ++i)
        {
            const int k = s + i;
            const bool ok = k >= 0 && k < len;
            in[(size_t) i] = C (ok ? L[k] * win[(size_t) i] : 0.0f, ok ? R[k] * win[(size_t) i] : 0.0f);
        }
        fft.perform (in.data(), spec.data(), false);
    };
    // split the packed L + jR spectrum into the two real spectra
    auto unpack = [&] (int k, float& lr, float& li, float& rr, float& ri)
    {
        const int nk = (N - k) & (N - 1);
        const C a = spec[(size_t) k], b = spec[(size_t) nk];
        lr = (a.real() + b.real()) / 2; li = (a.imag() - b.imag()) / 2;
        rr = (a.imag() + b.imag()) / 2; ri = -(a.real() - b.real()) / 2;
    };

    // pass 1: mid magnitude spectrogram
    std::vector<std::vector<float>> M ((size_t) frames, std::vector<float> ((size_t) B));
    for (int t = 0; t < frames; ++t)
    {
        frameFFT (t);
        for (int k = 0; k < B; ++k)
        {
            float lr, li, rr, ri; unpack (k, lr, li, rr, ri);
            const float mr = (lr + rr) / 2, mi = (li + ri) / 2;
            M[(size_t) t][(size_t) k] = std::sqrt (mr * mr + mi * mi);
        }
        if (t % 64 == 0)
        {
            if (prog) prog ((float) t / (float) frames * 0.4f);
            if (cancel && cancel()) return {};
        }
    }
    // stereo test
    double diff = 0, tot = 0;
    for (int i = 0; i < len; i += 7) { diff += std::abs (L[i] - R[i]); tot += std::abs (L[i]) + std::abs (R[i]); }
    const bool stereo = nc > 1 && diff / (tot + 1e-9) > .04;

    std::vector<float> wb ((size_t) B), bw ((size_t) B);
    for (int k = 0; k < B; ++k)
    {
        const double f = k * sr / N;
        wb[(size_t) k] = (float) (f < 110 ? 1 : f > 240 ? 0 : .5 + .5 * std::cos (juce::MathConstants<double>::pi * (f - 110) / 130));
        bw[(size_t) k] = (float) (f < 200 ? .15 : f < 320 ? .15 + .85 * (f - 200) / 120 : f < 5200 ? 1 : f < 9000 ? 1 - .7 * (f - 5200) / 3800 : .3);
    }

    std::vector<std::vector<float>> outL (4, std::vector<float> ((size_t) total, 0.0f)), outR = outL;
    std::vector<float> tmp (2 * std::max (HT, HF) + 1);
    auto med = [&] (int n)
    {
        std::sort (tmp.begin(), tmp.begin() + n);
        return tmp[(size_t) (n >> 1)];
    };
    std::vector<std::vector<C>> Z (4, std::vector<C> ((size_t) N)), Zo = Z;
    std::vector<float> Hm ((size_t) B), Pm ((size_t) B);

    for (int t = 0; t < frames; ++t)
    {
        for (int k = 0; k < B; ++k)
        {
            int n = 0;
            for (int d = -HT; d <= HT; ++d) { const int tt = t + d; if (tt >= 0 && tt < frames) tmp[(size_t) n++] = M[(size_t) tt][(size_t) k]; }
            Hm[(size_t) k] = med (n);
            n = 0;
            for (int d = -HF; d <= HF; ++d) { const int kk = k + d; if (kk >= 0 && kk < B) tmp[(size_t) n++] = M[(size_t) t][(size_t) kk]; }
            Pm[(size_t) k] = med (n);
        }
        frameFFT (t);
        for (auto& z : Z) std::fill (z.begin(), z.end(), C (0, 0));
        for (int k = 0; k < B; ++k)
        {
            const int nk = (N - k) & (N - 1);
            float lr, li, rr, ri; unpack (k, lr, li, rr, ri);
            const float h2 = Hm[(size_t) k] * Hm[(size_t) k], p2 = Pm[(size_t) k] * Pm[(size_t) k] * PM, mh = h2 / (h2 + p2 + 1e-12f);
            float c;
            if (stereo)
            {
                const float dr = lr - rr, di = li - ri, dn = std::sqrt (dr * dr + di * di);
                const float sn = std::sqrt (lr * lr + li * li) + std::sqrt (rr * rr + ri * ri);
                c = std::pow (std::max (0.0f, 1 - dn / (sn + 1e-9f)), 1.4f);
            }
            else c = 0.55f;
            const float v = c * bw[(size_t) k], w = wb[(size_t) k];
            const float m[4] = { (1 - w) * mh * v, w * mh, (1 - w) * mh * (1 - v), 1 - mh };
            for (int s = 0; s < 4; ++s)
            {
                Z[(size_t) s][(size_t) k] = spec[(size_t) k] * m[s];
                if (k > 0 && k < N / 2) Z[(size_t) s][(size_t) nk] = spec[(size_t) nk] * m[s];
            }
        }
        const int s0 = t * H - N + H;
        for (int s = 0; s < 4; ++s)
        {
            fft.perform (Z[(size_t) s].data(), Zo[(size_t) s].data(), true);
            auto& oL = outL[(size_t) s]; auto& oR = outR[(size_t) s];
            for (int i = 0; i < N; ++i)
            {
                const int k = s0 + i + N;
                if (k >= 0 && k < total)
                {
                    oL[(size_t) k] += Zo[(size_t) s][(size_t) i].real() * win[(size_t) i] / 1.5f;
                    oR[(size_t) k] += Zo[(size_t) s][(size_t) i].imag() * win[(size_t) i] / 1.5f;
                }
            }
        }
        if (t % 24 == 0)
        {
            if (prog) prog (0.4f + (float) t / (float) frames * 0.6f);
            if (cancel && cancel()) return {};
        }
    }

    std::array<SamplePtr, 4> res;
    const int och = nc > 1 ? 2 : 1;
    for (int s = 0; s < 4; ++s)
    {
        juce::AudioBuffer<float> b (och, juce::jmax (1, len));
        for (int i = 0; i < len; ++i)
        {
            b.setSample (0, i, outL[(size_t) s][(size_t) (i + N)]);
            if (och > 1) b.setSample (1, i, outR[(size_t) s][(size_t) (i + N)]);
        }
        res[(size_t) s] = makeSample (std::move (b), sr);
    }
    return res;
}
} // namespace cx::dsp
