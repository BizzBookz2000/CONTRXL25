// Files: WAV, zip (song files), folders.
#pragma once
#include "common.h"

namespace cx::io
{
bool readFile (const std::string& path, std::string& out, size_t maxBytes = 1u << 30);
bool writeFileAtomic (const std::string& path, const std::string& data);   // write to .tmp, fsync, rename
bool mkdirs (const std::string& path);
bool isDir (const std::string& path);
bool exists (const std::string& path);
std::string parentOf (const std::string& path);
std::string fileName (const std::string& path);
std::string baseName (const std::string& path);   // without extension
std::string lower (std::string s);
bool hasExt (const std::string& path, const char* ext);   // ext with dot, case-insensitive
struct DirEntry { std::string name; bool dir = false; long long size = 0; };
std::vector<DirEntry> listDir (const std::string& path);  // sorted: folders first, then names (case-insensitive)

// WAV: PCM 8/16/24/32, float 32/64, WAVE_FORMAT_EXTENSIBLE. Any rate, mono or stereo (more channels: first two).
SamplePtr decodeWav (const std::string& data, std::string* err = nullptr);
SamplePtr loadAudioFile (const std::string& path, std::string* err = nullptr);
std::string encodeWav (const SampleData& s, int bits);     // 16, 24 or 32 (float)
bool writeWav (const std::string& path, const SampleData& s, int bits);
SamplePtr resampledTo (const SamplePtr& s, double rate);   // windowed-sinc; returns s when the rate already matches

// zip
struct ZipEntry { std::string name; uint32_t crc = 0, csize = 0, usize = 0; uint16_t method = 0; uint32_t localOffset = 0; };
class ZipReader
{
public:
    bool open (const std::string& path);
    const std::vector<ZipEntry>& entries() const { return ents; }
    bool read (const std::string& name, std::string& out) const;
private:
    std::string data;
    std::vector<ZipEntry> ents;
};
class ZipWriter   // stored (uncompressed) entries, like the desktop app writes
{
public:
    void add (const std::string& name, const std::string& data);
    std::string finish();
private:
    std::string out;
    std::vector<ZipEntry> ents;
};
uint32_t crc32 (const void* data, size_t n, uint32_t crc = 0);
bool inflateRaw (const uint8_t* in, size_t inLen, std::string& out, size_t expected);
} // namespace cx::io
