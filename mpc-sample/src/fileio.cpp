#include "fileio.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>

namespace cx::io
{
// ------------------------------------------------------------------ files and folders
bool readFile (const std::string& path, std::string& out, size_t maxBytes)
{
    FILE* f = fopen (path.c_str(), "rb");
    if (! f) return false;
    out.clear();
    char buf[1 << 16];
    size_t n;
    while ((n = fread (buf, 1, sizeof buf, f)) > 0)
    {
        out.append (buf, n);
        if (out.size() > maxBytes) { fclose (f); return false; }
    }
    fclose (f);
    return true;
}

bool writeFileAtomic (const std::string& path, const std::string& data)
{
    const std::string tmp = path + ".tmp";
    const int fd = ::open (tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    size_t done = 0;
    while (done < data.size())
    {
        const ssize_t w = ::write (fd, data.data() + done, data.size() - done);
        if (w <= 0) { ::close (fd); ::unlink (tmp.c_str()); return false; }
        done += (size_t) w;
    }
    ::fsync (fd);
    ::close (fd);
    if (::rename (tmp.c_str(), path.c_str()) != 0) { ::unlink (tmp.c_str()); return false; }
    sync();
    return true;
}

bool isDir (const std::string& p) { struct stat st; return stat (p.c_str(), &st) == 0 && S_ISDIR (st.st_mode); }
bool exists (const std::string& p) { struct stat st; return stat (p.c_str(), &st) == 0; }

bool mkdirs (const std::string& path)
{
    if (path.empty() || isDir (path)) return true;
    const auto parent = parentOf (path);
    if (! parent.empty() && parent != path) mkdirs (parent);
    return ::mkdir (path.c_str(), 0755) == 0 || isDir (path);
}

std::string parentOf (const std::string& p)
{
    auto s = p;
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    const auto i = s.find_last_of ('/');
    if (i == std::string::npos) return "";
    return i == 0 ? "/" : s.substr (0, i);
}
std::string fileName (const std::string& p) { const auto i = p.find_last_of ('/'); return i == std::string::npos ? p : p.substr (i + 1); }
std::string baseName (const std::string& p) { auto f = fileName (p); const auto i = f.find_last_of ('.'); return i == std::string::npos || i == 0 ? f : f.substr (0, i); }
std::string lower (std::string s) { for (auto& c : s) c = (char) tolower ((unsigned char) c); return s; }
bool hasExt (const std::string& p, const char* ext)
{
    const auto e = lower (ext);
    return p.size() > e.size() && lower (p.substr (p.size() - e.size())) == e;
}

std::vector<DirEntry> listDir (const std::string& path)
{
    std::vector<DirEntry> v;
    DIR* d = opendir (path.c_str());
    if (! d) return v;
    while (auto* e = readdir (d))
    {
        std::string n = e->d_name;
        if (n.empty() || n[0] == '.') continue;   // hidden files, macOS "._" files
        struct stat st;
        const auto full = path + "/" + n;
        if (stat (full.c_str(), &st) != 0) continue;
        v.push_back ({ n, S_ISDIR (st.st_mode), (long long) st.st_size });
    }
    closedir (d);
    std::sort (v.begin(), v.end(), [] (const DirEntry& a, const DirEntry& b)
    {
        if (a.dir != b.dir) return a.dir;
        return lower (a.name) < lower (b.name);
    });
    return v;
}

// ------------------------------------------------------------------ WAV
static uint32_t rd32 (const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

SamplePtr decodeWav (const std::string& data, std::string* err)
{
    auto fail = [err] (const char* m) -> SamplePtr { if (err) *err = m; return nullptr; };
    const auto* d = (const uint8_t*) data.data();
    const size_t n = data.size();
    if (n < 12 || memcmp (d, "RIFF", 4) != 0 || memcmp (d + 8, "WAVE", 4) != 0)
    {
        if (n >= 12 && memcmp (d, "RF64", 4) == 0) return fail ("RF64 files are not supported");
        return fail ("not a WAV file");
    }
    int fmt = 0, nch = 0, bits = 0, align = 0; uint32_t rate = 0;
    const uint8_t* pcm = nullptr; size_t pcmLen = 0;
    size_t pos = 12;
    while (pos + 8 <= n)
    {
        const uint32_t len = rd32 (d + pos + 4);
        const uint8_t* body = d + pos + 8;
        const size_t avail = n - (pos + 8);
        if (! memcmp (d + pos, "fmt ", 4) && len >= 16 && avail >= 16)
        {
            fmt = rd16 (body); nch = rd16 (body + 2); rate = rd32 (body + 4); align = rd16 (body + 12); bits = rd16 (body + 14);
            if (fmt == 0xFFFE && len >= 26 && avail >= 26) fmt = rd16 (body + 24);   // extensible: sub-format GUID
        }
        else if (! memcmp (d + pos, "data", 4))
        {
            pcm = body; pcmLen = std::min<size_t> (len, avail);
            if (len == 0xFFFFFFFF || len == 0) pcmLen = avail;
        }
        pos += 8 + (size_t) len + (len & 1);
        if (len > n) break;
    }
    if (! pcm || nch < 1 || rate < 4000 || rate > 384000) return fail ("damaged WAV file");
    if (! ((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && (bits == 32 || bits == 64))))
        return fail ("unsupported WAV format (use PCM 8/16/24/32-bit or float)");
    const int bps = bits / 8;
    if (align < bps * nch) align = bps * nch;
    const size_t frames = pcmLen / (size_t) align;
    if (frames < 1) return fail ("empty WAV file");
    if (frames > (size_t) rate * 60 * 30) return fail ("too long (max 30 minutes)");
    const int outCh = std::min (nch, 2);
    std::vector<float> L (frames), R (outCh > 1 ? frames : 0);
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < outCh; ++c)
        {
            const uint8_t* s = pcm + i * (size_t) align + (size_t) c * bps;
            float v = 0;
            if (fmt == 3) { if (bits == 32) { float f; memcpy (&f, s, 4); v = f; } else { double f; memcpy (&f, s, 8); v = (float) f; } }
            else if (bits == 8) v = ((int) s[0] - 128) / 128.0f;
            else if (bits == 16) v = (int16_t) rd16 (s) / 32768.0f;
            else if (bits == 24) v = (float) ((int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8) / 8388608.0f;
            else v = (float) ((double) (int32_t) rd32 (s) / 2147483648.0);
            if (! std::isfinite (v)) v = 0;
            (c == 0 ? L : R)[i] = clampv (v, -4.0f, 4.0f);
        }
    return makeSample (std::move (L), std::move (R), rate);
}

SamplePtr loadAudioFile (const std::string& path, std::string* err)
{
    std::string data;
    if (! readFile (path, data, 600u << 20)) { if (err) *err = "cannot read the file"; return nullptr; }
    if (! hasExt (path, ".wav") && ! hasExt (path, ".wave")) { if (err) *err = "only WAV files"; return nullptr; }
    auto s = decodeWav (data, err);
    return s ? resampledTo (s, SR) : nullptr;
}

std::string encodeWav (const SampleData& s, int bits)
{
    if (bits != 16 && bits != 24) bits = 32;
    const int nch = s.channels(), n = s.length(), bps = bits / 8;
    const uint32_t dataLen = (uint32_t) n * (uint32_t) nch * (uint32_t) bps;
    std::string o;
    o.reserve (44 + dataLen);
    auto u32 = [&o] (uint32_t v) { for (int i = 0; i < 4; ++i) o += (char) ((v >> (8 * i)) & 255); };
    auto u16 = [&o] (uint32_t v) { o += (char) (v & 255); o += (char) ((v >> 8) & 255); };
    o += "RIFF"; u32 (36 + dataLen); o += "WAVE";
    o += "fmt "; u32 (16); u16 (bits == 32 ? 3 : 1); u16 ((uint32_t) nch);
    u32 ((uint32_t) s.sampleRate); u32 ((uint32_t) s.sampleRate * (uint32_t) (nch * bps)); u16 ((uint32_t) (nch * bps)); u16 ((uint32_t) bits);
    o += "data"; u32 (dataLen);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < nch; ++c)
        {
            float x = s.ch[c][(size_t) i];
            if (bits == 32) { char b[4]; memcpy (b, &x, 4); o.append (b, 4); continue; }
            x = clampv (x, -1.0f, 1.0f);
            if (bits == 16) { const int q = (int) std::lround (x < 0 ? x * 32768.0f : x * 32767.0f); u16 ((uint32_t) (q & 0xFFFF)); }
            else
            {
                const int q = (int) std::lround (x < 0 ? x * 8388608.0f : x * 8388607.0f);
                o += (char) (q & 255); o += (char) ((q >> 8) & 255); o += (char) ((q >> 16) & 255);
            }
        }
    return o;
}

bool writeWav (const std::string& path, const SampleData& s, int bits) { return writeFileAtomic (path, encodeWav (s, bits)); }

SamplePtr resampledTo (const SamplePtr& s, double rate)
{
    if (! s || std::fabs (s->sampleRate - rate) < 0.5) return s;
    const double ratio = rate / s->sampleRate;
    const int n = s->length(), m = std::max (1, (int) std::llround (n * ratio));
    const double cutoff = std::min (1.0, ratio) * 0.95;      // anti-alias when going down
    const int taps = 16;
    auto out = std::make_shared<SampleData>();
    out->sampleRate = rate;
    for (int c = 0; c < s->channels(); ++c)
    {
        const float* src = s->ch[c].data();
        auto& dst = out->ch[c];
        dst.resize ((size_t) m);
        for (int i = 0; i < m; ++i)
        {
            const double x = i / ratio;
            const int i0 = (int) std::floor (x);
            double acc = 0, wsum = 0;
            for (int k = -taps + 1; k <= taps; ++k)
            {
                const int j = i0 + k;
                if (j < 0 || j >= n) continue;
                const double t = x - j;
                const double sx = t * cutoff;
                const double sinc = std::fabs (sx) < 1e-9 ? 1.0 : std::sin (M_PI * sx) / (M_PI * sx);
                const double win = 0.5 + 0.5 * std::cos (M_PI * t / (taps + 1));
                const double w = sinc * win;
                acc += src[j] * w; wsum += w;
            }
            dst[(size_t) i] = (float) (wsum > 1e-9 ? acc / wsum : 0.0);
        }
    }
    return out;
}

// ------------------------------------------------------------------ crc32
uint32_t crc32 (const void* data, size_t n, uint32_t crc)
{
    static uint32_t table[256];
    static bool init = false;
    if (! init)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    const auto* p = (const uint8_t*) data;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

// ------------------------------------------------------------------ inflate (RFC 1951), for zips made by other tools
namespace
{
struct Bits
{
    const uint8_t* p; size_t n, pos = 0; uint32_t buf = 0; int cnt = 0; bool err = false;
    int bit()
    {
        if (cnt == 0) { if (pos >= n) { err = true; return 0; } buf = p[pos++]; cnt = 8; }
        const int b = buf & 1; buf >>= 1; --cnt; return b;
    }
    int bits (int k) { int v = 0; for (int i = 0; i < k; ++i) v |= bit() << i; return v; }
};
struct Huff { uint16_t count[16] {}; uint16_t sym[320] {}; };
static void build (Huff& h, const uint8_t* len, int n)
{
    memset (h.count, 0, sizeof h.count);
    for (int i = 0; i < n; ++i) h.count[len[i]]++;
    h.count[0] = 0;
    uint16_t offs[16]; offs[1] = 0;
    for (int i = 1; i < 15; ++i) offs[i + 1] = (uint16_t) (offs[i] + h.count[i]);
    for (int i = 0; i < n; ++i) if (len[i]) h.sym[offs[len[i]]++] = (uint16_t) i;
}
static int decode (Bits& b, const Huff& h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len)
    {
        code |= b.bit();
        const int count = h.count[len];
        if (code - count < first) return h.sym[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
        if (b.err) return -1;
    }
    return -1;
}
}

bool inflateRaw (const uint8_t* in, size_t inLen, std::string& out, size_t expected)
{
    static const uint16_t LB[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const uint8_t LE[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const uint16_t DB[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const uint8_t DE[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    Bits b { in, inLen };
    out.clear(); out.reserve (expected);
    int last = 0;
    while (! last)
    {
        last = b.bit();
        const int type = b.bits (2);
        if (b.err) return false;
        if (type == 0)
        {
            b.cnt = 0;
            if (b.pos + 4 > inLen) return false;
            const int len = in[b.pos] | (in[b.pos + 1] << 8);
            b.pos += 4;
            if (b.pos + (size_t) len > inLen) return false;
            out.append ((const char*) in + b.pos, (size_t) len); b.pos += (size_t) len;
            continue;
        }
        Huff lit, dist;
        uint8_t lens[320];
        if (type == 1)
        {
            int i = 0;
            for (; i < 144; ++i) lens[i] = 8;
            for (; i < 256; ++i) lens[i] = 9;
            for (; i < 280; ++i) lens[i] = 7;
            for (; i < 288; ++i) lens[i] = 8;
            build (lit, lens, 288);
            for (i = 0; i < 30; ++i) lens[i] = 5;
            build (dist, lens, 30);
        }
        else if (type == 2)
        {
            const int nlen = b.bits (5) + 257, ndist = b.bits (5) + 1, ncode = b.bits (4) + 4;
            static const uint8_t ord[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            uint8_t cl[19] = {};
            for (int i = 0; i < ncode; ++i) cl[ord[i]] = (uint8_t) b.bits (3);
            Huff ch; build (ch, cl, 19);
            int i = 0;
            while (i < nlen + ndist)
            {
                int sym = decode (b, ch);
                if (sym < 0) return false;
                if (sym < 16) lens[i++] = (uint8_t) sym;
                else
                {
                    int rep = 0, val = 0;
                    if (sym == 16) { if (i == 0) return false; val = lens[i - 1]; rep = 3 + b.bits (2); }
                    else if (sym == 17) rep = 3 + b.bits (3);
                    else rep = 11 + b.bits (7);
                    if (i + rep > nlen + ndist) return false;
                    while (rep--) lens[i++] = (uint8_t) val;
                }
            }
            build (lit, lens, nlen);
            build (dist, lens + nlen, ndist);
        }
        else return false;
        for (;;)
        {
            int sym = decode (b, lit);
            if (sym < 0 || b.err) return false;
            if (sym < 256) { out += (char) sym; continue; }
            if (sym == 256) break;
            sym -= 257;
            if (sym >= 29) return false;
            const int len = LB[sym] + b.bits (LE[sym]);
            const int ds = decode (b, dist);
            if (ds < 0 || ds >= 30) return false;
            const size_t d = (size_t) (DB[ds] + b.bits (DE[ds]));
            if (d > out.size()) return false;
            const size_t from = out.size() - d;
            for (int k = 0; k < len; ++k) out += out[from + (size_t) k];
        }
        if (out.size() > expected + 1024 * 1024 && expected > 0) return false;
    }
    return true;
}

// ------------------------------------------------------------------ zip
bool ZipReader::open (const std::string& path)
{
    ents.clear();
    if (! readFile (path, data, 1500u << 20)) return false;
    const auto* d = (const uint8_t*) data.data();
    const size_t n = data.size();
    if (n < 22) return false;
    // end of central directory
    size_t eocd = std::string::npos;
    for (size_t i = n - 22; ; --i)
    {
        if (rd32 (d + i) == 0x06054b50) { eocd = i; break; }
        if (i == 0 || n - i > 70000) break;
    }
    if (eocd == std::string::npos) return false;
    const int count = rd16 (d + eocd + 10);
    size_t cd = rd32 (d + eocd + 16);
    for (int k = 0; k < count && cd + 46 <= n; ++k)
    {
        if (rd32 (d + cd) != 0x02014b50) return false;
        ZipEntry e;
        e.method = rd16 (d + cd + 10);
        e.crc = rd32 (d + cd + 16); e.csize = rd32 (d + cd + 20); e.usize = rd32 (d + cd + 24);
        const int nl = rd16 (d + cd + 28), xl = rd16 (d + cd + 30), cl = rd16 (d + cd + 32);
        e.localOffset = rd32 (d + cd + 42);
        if (cd + 46 + (size_t) nl > n) return false;
        e.name.assign ((const char*) d + cd + 46, (size_t) nl);
        ents.push_back (e);
        cd += 46 + (size_t) nl + (size_t) xl + (size_t) cl;
    }
    return ! ents.empty();
}

bool ZipReader::read (const std::string& name, std::string& out) const
{
    for (auto& e : ents)
    {
        if (e.name != name) continue;
        const auto* d = (const uint8_t*) data.data();
        const size_t lo = e.localOffset;
        if (lo + 30 > data.size() || rd32 (d + lo) != 0x04034b50) return false;
        const size_t start = lo + 30 + rd16 (d + lo + 26) + rd16 (d + lo + 28);
        if (start + e.csize > data.size()) return false;
        if (e.method == 0) { out.assign (data, start, e.csize); return true; }
        if (e.method == 8) return inflateRaw (d + start, e.csize, out, e.usize);
        return false;
    }
    return false;
}

void ZipWriter::add (const std::string& name, const std::string& data)
{
    ZipEntry e;
    e.name = name; e.crc = crc32 (data.data(), data.size());
    e.csize = e.usize = (uint32_t) data.size(); e.localOffset = (uint32_t) out.size();
    auto u32 = [this] (uint32_t v) { for (int i = 0; i < 4; ++i) out += (char) ((v >> (8 * i)) & 255); };
    auto u16 = [this] (uint32_t v) { out += (char) (v & 255); out += (char) ((v >> 8) & 255); };
    const time_t t = time (nullptr); struct tm tmv; localtime_r (&t, &tmv);
    const uint16_t dtime = (uint16_t) ((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
    const uint16_t ddate = (uint16_t) (((std::max (tmv.tm_year - 80, 0)) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
    u32 (0x04034b50); u16 (20); u16 (0x0800); u16 (0); u16 (dtime); u16 (ddate);
    u32 (e.crc); u32 (e.csize); u32 (e.usize); u16 ((uint32_t) name.size()); u16 (0);
    out += name; out += data;
    ents.push_back (e);
}

std::string ZipWriter::finish()
{
    auto u32 = [this] (uint32_t v) { for (int i = 0; i < 4; ++i) out += (char) ((v >> (8 * i)) & 255); };
    auto u16 = [this] (uint32_t v) { out += (char) (v & 255); out += (char) ((v >> 8) & 255); };
    const uint32_t cdStart = (uint32_t) out.size();
    for (auto& e : ents)
    {
        u32 (0x02014b50); u16 (20); u16 (20); u16 (0x0800); u16 (0); u16 (0); u16 (0x21);
        u32 (e.crc); u32 (e.csize); u32 (e.usize); u16 ((uint32_t) e.name.size()); u16 (0); u16 (0); u16 (0); u16 (0); u32 (0);
        u32 (e.localOffset); out += e.name;
    }
    const uint32_t cdLen = (uint32_t) out.size() - cdStart;
    u32 (0x06054b50); u16 (0); u16 (0); u16 ((uint32_t) ents.size()); u16 ((uint32_t) ents.size()); u32 (cdLen); u32 (cdStart); u16 (0);
    return std::move (out);
}
} // namespace cx::io
