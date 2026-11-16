// CONTRXL 25 - estimated stem separation (pseudocode 6.4). DSP, not AI.
#pragma once
#include "dsp/Offline.h"

namespace cx::dsp
{
constexpr int NSTEMS = 4;
const char* stemKey (int i);    // "v", "b", "i", "d"
const char* stemName (int i);   // "Vocals", ...

// Returns vocals, bass, instruments, drums. The four add back to the input.
// 'cancel' may be polled to abort (returns empty array).
std::array<SamplePtr, 4> separateStems (const SampleData& s, const Progress& prog = {},
                                        const std::function<bool()>& cancel = {});

// Results live here, keyed by the source sample (stems are not saved in songs).
class StemStore
{
public:
    bool has (const SampleData* s) const { return map.count (s) > 0; }
    const std::array<SamplePtr, 4>* get (const SampleData* s) const
    {
        auto it = map.find (s);
        return it == map.end() ? nullptr : &it->second.stems;
    }
    void put (const SamplePtr& src, const std::array<SamplePtr, 4>& st) { map[src.get()] = { src, st }; }
    void clear() { map.clear(); }
private:
    struct Entry { SamplePtr src; std::array<SamplePtr, 4> stems; };
    std::map<const SampleData*, Entry> map;
};
} // namespace cx::dsp
