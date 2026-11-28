// The MPC Sample (AC50) hardware, firmware 1.4: DRM screen, ALSA audio (I2S / AK4558),
// raw MIDI for the control surface (UART3) and external MIDI (DIN, USB host, USB-C).
// The stock app (ac50.service) is stopped by the multiboot session before this runs.
#include "hw.h"
#include "engine.h"
#include <condition_variable>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <sound/asound.h>
extern "C" {
#include <tinyalsa/asoundlib.h>
}

namespace cx
{
namespace
{
int xioctl (int fd, unsigned long req, void* arg) { int r; do { r = ioctl (fd, req, arg); } while (r == -1 && (errno == EINTR || errno == EAGAIN)); return r; }

// ------------------------------------------------------------------ screen (DRM dumb buffer + DIRTYFB: the st7796s SPI panel only updates when told)
struct Display
{
    int fd = -1;
    uint32_t connId = 0, crtcId = 0, fbId = 0, handle = 0, pitch = 0, w = 0, h = 0;
    uint8_t* map = nullptr; size_t size = 0;
    drm_mode_modeinfo mode {};
    drm_mode_crtc saved {}; bool haveSaved = false;
    bool portrait = false;

    bool open (std::string& err)
    {
        for (int k = 0; k < 4 && fd < 0; ++k)
        {
            const std::string path = "/dev/dri/card" + std::to_string (k);
            const int f = ::open (path.c_str(), O_RDWR | O_CLOEXEC);
            if (f < 0) continue;
            if (tryCard (f)) { fd = f; logf ("display: %s %ux%u", path.c_str(), w, h); break; }
            ::close (f);
        }
        if (fd < 0) { err = "no screen found (/dev/dri)"; return false; }
        return true;
    }

    bool tryCard (int f)
    {
        xioctl (f, DRM_IOCTL_SET_MASTER, nullptr);
        drm_mode_card_res res {};
        if (xioctl (f, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0 || res.count_connectors == 0 || res.count_crtcs == 0) return false;
        std::vector<uint32_t> conns (res.count_connectors), crtcs (res.count_crtcs), encs (res.count_encoders ? res.count_encoders : 1), fbs (res.count_fbs ? res.count_fbs : 1);
        res.connector_id_ptr = (uint64_t) (uintptr_t) conns.data(); res.crtc_id_ptr = (uint64_t) (uintptr_t) crtcs.data();
        res.encoder_id_ptr = (uint64_t) (uintptr_t) encs.data(); res.fb_id_ptr = (uint64_t) (uintptr_t) fbs.data();
        if (xioctl (f, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) return false;
        for (uint32_t c : conns)
        {
            drm_mode_get_connector gc {}; gc.connector_id = c;
            if (xioctl (f, DRM_IOCTL_MODE_GETCONNECTOR, &gc) != 0 || gc.count_modes == 0) continue;
            std::vector<drm_mode_modeinfo> modes (gc.count_modes);
            std::vector<uint32_t> cencs (gc.count_encoders ? gc.count_encoders : 1);
            std::vector<uint32_t> props (gc.count_props ? gc.count_props : 1); std::vector<uint64_t> pvals (gc.count_props ? gc.count_props : 1);
            gc.modes_ptr = (uint64_t) (uintptr_t) modes.data(); gc.encoders_ptr = (uint64_t) (uintptr_t) cencs.data();
            gc.props_ptr = (uint64_t) (uintptr_t) props.data(); gc.prop_values_ptr = (uint64_t) (uintptr_t) pvals.data();
            if (xioctl (f, DRM_IOCTL_MODE_GETCONNECTOR, &gc) != 0) continue;
            if (gc.connection != 1 /* connected */ && gc.connection != 3 /* unknown */) continue;
            mode = modes[0];
            for (auto& m : modes) if (m.type & DRM_MODE_TYPE_PREFERRED) { mode = m; break; }
            // the encoder's CRTC (or the first one it can use)
            uint32_t enc = gc.encoder_id ? gc.encoder_id : (gc.count_encoders ? cencs[0] : 0);
            uint32_t crtc = 0;
            if (enc)
            {
                drm_mode_get_encoder ge {}; ge.encoder_id = enc;
                if (xioctl (f, DRM_IOCTL_MODE_GETENCODER, &ge) == 0)
                {
                    crtc = ge.crtc_id;
                    if (! crtc) for (uint32_t i = 0; i < res.count_crtcs; ++i) if (ge.possible_crtcs & (1u << i)) { crtc = crtcs[i]; break; }
                }
            }
            if (! crtc) crtc = crtcs[0];
            connId = c; crtcId = crtc; w = mode.hdisplay; h = mode.vdisplay;
            portrait = h > w;
            saved.crtc_id = crtc;
            haveSaved = xioctl (f, DRM_IOCTL_MODE_GETCRTC, &saved) == 0;
            drm_mode_create_dumb cd {}; cd.width = w; cd.height = h; cd.bpp = 32;
            if (xioctl (f, DRM_IOCTL_MODE_CREATE_DUMB, &cd) != 0) return false;
            handle = cd.handle; pitch = cd.pitch; size = (size_t) cd.size;
            drm_mode_fb_cmd fb {}; fb.width = w; fb.height = h; fb.pitch = pitch; fb.bpp = 32; fb.depth = 24; fb.handle = handle;
            if (xioctl (f, DRM_IOCTL_MODE_ADDFB, &fb) != 0) return false;
            fbId = fb.fb_id;
            drm_mode_map_dumb md {}; md.handle = handle;
            if (xioctl (f, DRM_IOCTL_MODE_MAP_DUMB, &md) != 0) return false;
            map = (uint8_t*) mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, f, (off_t) md.offset);
            if (map == MAP_FAILED) { map = nullptr; return false; }
            memset (map, 0, size);
            drm_mode_crtc sc {}; sc.crtc_id = crtcId; sc.fb_id = fbId; sc.set_connectors_ptr = (uint64_t) (uintptr_t) &connId; sc.count_connectors = 1;
            sc.mode = mode; sc.mode_valid = 1;
            if (xioctl (f, DRM_IOCTL_MODE_SETCRTC, &sc) != 0) { logf ("display: SETCRTC failed (%d)", errno); return false; }
            return true;
        }
        return false;
    }

    void flush (const uint32_t* px, int x0, int y0, int x1, int y1)
    {
        if (! map) return;
        drm_clip_rect clip {};
        if (! portrait)
        {
            // landscape (the stock st7796s driver turns the panel for us)
            const int W = std::min ((int) w, SCREEN_W), H = std::min ((int) h, SCREEN_H);
            x0 = std::max (0, x0); y0 = std::max (0, y0); x1 = std::min (W, x1); y1 = std::min (H, y1);
            if (x1 <= x0 || y1 <= y0) return;
            for (int y = y0; y < y1; ++y) memcpy (map + (size_t) y * pitch + (size_t) x0 * 4, px + (size_t) y * SCREEN_W + x0, (size_t) (x1 - x0) * 4);
            clip = { (unsigned short) x0, (unsigned short) y0, (unsigned short) x1, (unsigned short) y1 };
        }
        else
        {
            // a portrait mode (240x320): turn our landscape frame by 90 degrees
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                {
                    const int px2 = SCREEN_H - 1 - y, py2 = x;
                    if (px2 < (int) w && py2 < (int) h) *(uint32_t*) (map + (size_t) py2 * pitch + (size_t) px2 * 4) = px[(size_t) y * SCREEN_W + x];
                }
            clip = { (unsigned short) (SCREEN_H - y1), (unsigned short) x0, (unsigned short) (SCREEN_H - y0), (unsigned short) x1 };
        }
        drm_mode_fb_dirty_cmd d {}; d.fb_id = fbId; d.num_clips = 1; d.clips_ptr = (uint64_t) (uintptr_t) &clip;
        xioctl (fd, DRM_IOCTL_MODE_DIRTYFB, &d);
    }

    void close()
    {
        if (fd < 0) return;
        if (map) { memset (map, 0, size); drm_clip_rect clip { 0, 0, (unsigned short) w, (unsigned short) h }; drm_mode_fb_dirty_cmd d {}; d.fb_id = fbId; d.num_clips = 1; d.clips_ptr = (uint64_t) (uintptr_t) &clip; xioctl (fd, DRM_IOCTL_MODE_DIRTYFB, &d); }
        if (haveSaved && saved.fb_id)
        {
            drm_mode_crtc sc = saved; sc.set_connectors_ptr = (uint64_t) (uintptr_t) &connId; sc.count_connectors = 1;
            xioctl (fd, DRM_IOCTL_MODE_SETCRTC, &sc);
        }
        if (fbId) { uint32_t id = fbId; xioctl (fd, DRM_IOCTL_MODE_RMFB, &id); }
        if (map) munmap (map, size);
        if (handle) { drm_mode_destroy_dumb dd {}; dd.handle = handle; xioctl (fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd); }
        xioctl (fd, DRM_IOCTL_DROP_MASTER, nullptr);
        ::close (fd); fd = -1;
    }
};

// ------------------------------------------------------------------ raw MIDI ports
struct Port
{
    int fd = -1, card = -1, dev = 0;
    bool surface = false;
    std::string name;
    uint8_t st = 0, d[2] {}; int need = 0, have = 0; bool sysex = false;
    std::string out;            // pending output (surface)
};

int cardOfLink (const char* path)
{
    char buf[256];
    const ssize_t n = readlink (path, buf, sizeof buf - 1);
    if (n <= 0) return -1;
    buf[n] = 0;
    const char* p = strstr (buf, "controlC");
    if (p) return atoi (p + 8);
    p = strstr (buf, "card");
    return p ? atoi (p + 4) : -1;
}

std::string rawmidiName (int fd)
{
    snd_rawmidi_info info {};
    info.stream = SNDRV_RAWMIDI_STREAM_INPUT;
    if (ioctl (fd, SNDRV_RAWMIDI_IOCTL_INFO, &info) == 0) return std::string ((const char*) info.name) + " / " + (const char*) info.subname;
    return "";
}
} // namespace

class MpcHw : public Hw
{
public:
    ~MpcHw() override { shutdown(); }

    bool init (std::string& err) override
    {
        if (! disp.open (err)) return false;
        dispThread = std::thread ([this] { displayLoop(); });
        surfaceCard = cardOfLink ("/dev/snd/by-path/platform-fe670000.serial");
        rescanMidi();
        if (! haveSurface()) logf ("control surface not found yet (by-path card %d)", surfaceCard);
        i2sCard = cardOfLink ("/proc/asound/I2S");
        if (i2sCard < 0) i2sCard = 0;
        mix = mixer_open ((unsigned) i2sCard);
        return true;
    }

    void shutdown() override
    {
        stopAudio();
        if (dispThread.joinable()) { { std::lock_guard<std::mutex> l (dm); dquit = true; } dcv.notify_all(); dispThread.join(); }
        for (auto& p : ports) { flushOut (p, true); if (p.fd >= 0) ::close (p.fd); }
        ports.clear();
        restoreMixer();
        if (mix) { mixer_close (mix); mix = nullptr; }
        disp.close();
    }

    // ---------------- screen
    void present (const uint32_t* px, int x0, int y0, int x1, int y1) override
    {
        std::lock_guard<std::mutex> l (dm);
        if (frame.empty()) frame.assign ((size_t) SCREEN_W * SCREEN_H, 0);
        memcpy (frame.data(), px, frame.size() * 4);
        if (! dirty) { rx0 = x0; ry0 = y0; rx1 = x1; ry1 = y1; }
        else { rx0 = std::min (rx0, x0); ry0 = std::min (ry0, y0); rx1 = std::max (rx1, x1); ry1 = std::max (ry1, y1); }
        dirty = true;
        dcv.notify_all();
    }
    bool displayBusy() override { std::lock_guard<std::mutex> l (dm); return dirty || flushing; }

    void displayLoop()
    {
        std::vector<uint32_t> local ((size_t) SCREEN_W * SCREEN_H);
        for (;;)
        {
            int x0, y0, x1, y1;
            {
                std::unique_lock<std::mutex> l (dm);
                dcv.wait (l, [this] { return dirty || dquit; });
                if (dquit) return;
                memcpy (local.data(), frame.data(), local.size() * 4);
                x0 = rx0; y0 = ry0; x1 = rx1; y1 = ry1; dirty = false; flushing = true;
            }
            disp.flush (local.data(), x0, y0, x1, y1);   // ~50 ms for the whole screen over SPI
            std::lock_guard<std::mutex> l (dm);
            flushing = false;
        }
    }

    // ---------------- MIDI
    bool haveSurface() const { for (auto& p : ports) if (p.surface) return true; return false; }

    void rescanMidi()
    {
        lastScan = nowSeconds();
        DIR* d = opendir ("/dev/snd");
        if (! d) return;
        std::vector<std::pair<int, int>> found;
        while (auto* e = readdir (d))
        {
            int c, dv;
            if (sscanf (e->d_name, "midiC%dD%d", &c, &dv) == 2) found.push_back ({ c, dv });
        }
        closedir (d);
        // drop ports that went away
        for (size_t i = 0; i < ports.size();)
        {
            bool still = false;
            for (auto& f : found) if (f.first == ports[i].card && f.second == ports[i].dev) still = true;
            if (still) { ++i; continue; }
            logf ("MIDI port gone: %s", ports[i].name.c_str());
            if (ports[i].fd >= 0) ::close (ports[i].fd);
            ports.erase (ports.begin() + (long) i);
        }
        for (auto& f : found)
        {
            bool have = false;
            for (auto& p : ports) if (p.card == f.first && p.dev == f.second) have = true;
            if (have) continue;
            const std::string path = "/dev/snd/midiC" + std::to_string (f.first) + "D" + std::to_string (f.second);
            Port p; p.card = f.first; p.dev = f.second;
            p.fd = ::open (path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
            if (p.fd < 0) p.fd = ::open (path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (p.fd < 0) continue;
            p.name = rawmidiName (p.fd);
            p.surface = (surfaceCard >= 0 && p.card == surfaceCard) || p.name.find ("Control Surface") != std::string::npos;
            logf ("MIDI port %s: %s%s", path.c_str(), p.name.c_str(), p.surface ? " (control surface)" : "");
            ports.push_back (std::move (p));
        }
    }

    void sendSurface (const uint8_t* d, int n) override
    {
        for (auto& p : ports)
            if (p.surface) { if (p.out.size() < 65536) p.out.append ((const char*) d, (size_t) n); flushOut (p, false); return; }
    }

    void flushOut (Port& p, bool block)
    {
        int guard = 0;
        while (! p.out.empty() && p.fd >= 0)
        {
            const ssize_t w = ::write (p.fd, p.out.data(), p.out.size());
            if (w > 0) { p.out.erase (0, (size_t) w); continue; }
            if (w < 0 && (errno == EAGAIN || errno == EINTR) && block && ++guard < 50) { usleep (2000); continue; }
            break;
        }
    }

    void parse (Port& p, const uint8_t* buf, int n, std::vector<MidiMsg>& out)
    {
        const int src = p.surface ? 0 : 1 + (int) (&p - ports.data());
        for (int i = 0; i < n; ++i)
        {
            const uint8_t b = buf[i];
            if (b >= 0xF8) { MidiMsg m; m.src = src; m.b[0] = b; m.n = 1; out.push_back (m); continue; }
            if (b == 0xF0) { p.sysex = true; continue; }
            if (p.sysex) { if (b == 0xF7 || b >= 0x80) p.sysex = false; if (b < 0x80 || b == 0xF7) continue; }
            if (b >= 0x80)
            {
                p.st = b; p.have = 0;
                const int hi = b & 0xF0;
                p.need = (hi == 0xC0 || hi == 0xD0 || b == 0xF1 || b == 0xF3) ? 1 : (b >= 0xF4) ? 0 : 2;
                if (p.need == 0) { MidiMsg m; m.src = src; m.b[0] = b; m.n = 1; out.push_back (m); p.st = 0; }
                continue;
            }
            if (! p.st) continue;
            p.d[p.have++] = b;
            if (p.have == p.need)
            {
                MidiMsg m; m.src = src; m.b[0] = p.st; m.b[1] = p.d[0]; m.b[2] = p.need > 1 ? p.d[1] : 0; m.n = 1 + p.need;
                out.push_back (m);
                p.have = 0;
                if (p.st >= 0xF0) p.st = 0;
            }
        }
    }

    void poll (int timeoutMs, std::vector<MidiMsg>& out) override
    {
        if (nowSeconds() - lastScan > 2.0) rescanMidi();
        std::vector<pollfd> fds;
        for (auto& p : ports) fds.push_back ({ p.fd, (short) (POLLIN | (p.out.empty() ? 0 : POLLOUT)), 0 });
        if (fds.empty()) { usleep ((useconds_t) timeoutMs * 1000); return; }
        const int r = ::poll (fds.data(), (nfds_t) fds.size(), timeoutMs);
        if (r <= 0) return;
        uint8_t buf[512];
        for (size_t i = 0; i < fds.size() && i < ports.size(); ++i)
        {
            if (fds[i].revents & POLLOUT) flushOut (ports[i], false);
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { lastScan = 0; continue; }
            if (! (fds[i].revents & POLLIN)) continue;
            for (;;)
            {
                const ssize_t n = ::read (ports[i].fd, buf, sizeof buf);
                if (n <= 0) break;
                parse (ports[i], buf, (int) n, out);
            }
        }
    }

    // ---------------- audio
    bool startAudio (Engine& e, std::string& err) override
    {
        engine = &e;
        pcm_config cfg {};
        cfg.channels = 2; cfg.rate = 44100; cfg.period_size = PERIOD; cfg.period_count = PERIODS; cfg.format = PCM_FORMAT_S32_LE;
        cfg.start_threshold = PERIOD * 2; cfg.stop_threshold = PERIOD * PERIODS; cfg.silence_threshold = 0;
        out = pcm_open ((unsigned) i2sCard, 0, PCM_OUT, &cfg);
        if (! out || ! pcm_is_ready (out)) { err = std::string ("audio output: ") + (out ? pcm_get_error (out) : "open failed"); if (out) pcm_close (out); out = nullptr; return false; }
        pcm_config ic = cfg; ic.start_threshold = 1;
        in = pcm_open ((unsigned) i2sCard, 0, PCM_IN, &ic);
        if (in && ! pcm_is_ready (in)) { logf ("audio input: %s", pcm_get_error (in)); pcm_close (in); in = nullptr; }
        logf ("audio: card %d, 44.1 kHz, %d x %d frames, input %s", i2sCard, PERIOD, PERIODS, in ? "on" : "off");
        aquit = false;
        audioThread = std::thread ([this] { audioLoop(); });
        return true;
    }

    void stopAudio() override
    {
        aquit = true;
        if (audioThread.joinable()) audioThread.join();
        if (in) { pcm_close (in); in = nullptr; }
        if (out) { pcm_close (out); out = nullptr; }
    }

    void audioLoop()
    {
        sched_param sp {}; sp.sched_priority = 70;
        if (pthread_setschedparam (pthread_self(), SCHED_FIFO, &sp) != 0) logf ("audio: no real-time priority");
        std::vector<int32_t> ibuf (PERIOD * 2), obuf (PERIOD * 2, 0);
        std::vector<float> iL (PERIOD), iR (PERIOD), oL (PERIOD), oR (PERIOD);
        for (int k = 0; k < 2; ++k) if (pcm_writei (out, obuf.data(), PERIOD) < 0) pcm_prepare (out);   // prefill
        while (! aquit.load())
        {
            bool haveIn = false;
            if (in)
            {
                const int r = pcm_readi (in, ibuf.data(), PERIOD);
                if (r == PERIOD)
                {
                    haveIn = true;
                    for (int i = 0; i < PERIOD; ++i) { iL[(size_t) i] = ibuf[(size_t) i * 2] / 2147483648.0f; iR[(size_t) i] = ibuf[(size_t) i * 2 + 1] / 2147483648.0f; }
                }
                else { ++inErrors; pcm_prepare (in); }
            }
            engine->process (haveIn ? iL.data() : nullptr, haveIn ? iR.data() : nullptr, oL.data(), oR.data(), PERIOD);
            const float g = volume.load (std::memory_order_relaxed);
            for (int i = 0; i < PERIOD; ++i)
            {
                obuf[(size_t) i * 2] = (int32_t) (clampv (oL[(size_t) i] * g, -1.0f, 0.99999994f) * 2147483647.0f);
                obuf[(size_t) i * 2 + 1] = (int32_t) (clampv (oR[(size_t) i] * g, -1.0f, 0.99999994f) * 2147483647.0f);
            }
            if (pcm_writei (out, obuf.data(), PERIOD) < 0)
            {
                ++xruns;
                pcm_prepare (out);
                std::fill (obuf.begin(), obuf.end(), 0);
                if (pcm_writei (out, obuf.data(), PERIOD) < 0) pcm_prepare (out);
            }
        }
    }

    // ---------------- mixer (input routing): never line and mic together
    void setCtl (const char* name, int v)
    {
        if (! mix) return;
        mixer_ctl* c = mixer_get_ctl_by_name (mix, name);
        if (! c) { logf ("mixer: no control '%s'", name); return; }
        if (! savedCtl.count (name)) savedCtl[name] = mixer_ctl_get_value (c, 0);
        for (unsigned i = 0; i < mixer_ctl_get_num_values (c); ++i) mixer_ctl_set_value (c, i, v);
    }
    void setInput (int route) override
    {
        if (route == curRoute) return;
        if (route == 0 && savedCtl.empty()) { curRoute = 0; return; }   // never touched: leave as the system had it
        setCtl ("TRS Switch", 0); setCtl ("Builtin Microphone Switch", 0);
        if (route == 1) setCtl ("TRS Switch", 1);
        if (route == 2) setCtl ("Builtin Microphone Switch", 1);
        curRoute = route;
    }
    void restoreMixer()
    {
        if (! mix) return;
        for (auto& kv : savedCtl)
            if (mixer_ctl* c = mixer_get_ctl_by_name (mix, kv.first.c_str()))
                for (unsigned i = 0; i < mixer_ctl_get_num_values (c); ++i) mixer_ctl_set_value (c, i, kv.second);
        savedCtl.clear();
    }

    void setMainVolume (float g) override { volume.store (g); }
    int latencyFrames() override { return PERIOD * PERIODS; }
    std::string status() override
    {
        int ext = 0; for (auto& p : ports) if (! p.surface) ++ext;
        return strf ("44.1 kHz %dx%d  xruns %d  MIDI %d", PERIOD, PERIODS, xruns.load(), ext);
    }

private:
    static constexpr int PERIOD = 128, PERIODS = 4;
    Display disp;
    std::thread dispThread;
    std::mutex dm; std::condition_variable dcv;
    std::vector<uint32_t> frame; bool dirty = false, flushing = false, dquit = false; int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
    std::vector<Port> ports; int surfaceCard = -1; double lastScan = 0;
    int i2sCard = 0;
    pcm* out = nullptr; pcm* in = nullptr;
    struct mixer* mix = nullptr; std::map<std::string, int> savedCtl; int curRoute = 0;
    Engine* engine = nullptr;
    std::thread audioThread; std::atomic<bool> aquit { false };
    std::atomic<float> volume { 0.64f };
    std::atomic<int> xruns { 0 }, inErrors { 0 };
};

std::unique_ptr<Hw> makeMpcHw() { return std::make_unique<MpcHw>(); }
} // namespace cx
