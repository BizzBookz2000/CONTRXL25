// CONTRXL 25 - MPC Sample edition. (c) ILLAHTAINMENT. All rights reserved.
// Shared basics: sample buffers and small helpers.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cx
{
constexpr double SR = 44100.0;           // the MPC Sample's codec runs at 44.1 kHz on firmware 1.4
constexpr const char* APP_VERSION = "1.0.0";

template <typename T> inline T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Immutable audio, shared by reference count. Editing makes a new one.
struct SampleData
{
    std::vector<float> ch[2];            // ch[1] empty for mono
    double sampleRate = SR;
    int channels() const { return ch[1].empty() ? 1 : 2; }
    int length() const { return (int) ch[0].size(); }
    double duration() const { return length() / sampleRate; }
    const float* data (int c) const { return ch[c < channels() ? c : 0].data(); }
    float peak() const
    {
        float p = 0;
        for (int c = 0; c < channels(); ++c) for (float v : ch[c]) p = std::max (p, std::fabs (v));
        return p;
    }
};
using SamplePtr = std::shared_ptr<const SampleData>;

inline SamplePtr makeSample (std::vector<float> l, std::vector<float> r, double sr)
{
    auto s = std::make_shared<SampleData>();
    s->ch[0] = std::move (l);
    if (! r.empty() && r.size() == s->ch[0].size()) s->ch[1] = std::move (r);
    s->sampleRate = sr;
    return s;
}

double nowSeconds();                     // monotonic
std::string strf (const char* fmt, ...);
void logf (const char* fmt, ...);        // to the log file and stderr
void setLogFile (const std::string& path);
} // namespace cx
