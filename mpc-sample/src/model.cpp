#include "model.h"
#include "fileio.h"

namespace cx
{
static const char* const RKEYS[NRETRO]  = { "", "asr10", "mpc60", "mpc3000", "sp1200", "sp202", "sp303", "sp404" };
static const char* const RNAMES[NRETRO] = { "Off", "ASR-10", "MPC60", "MPC3000", "SP-1200", "SP-202", "SP-303", "SP-404" };
const char* retroKey (int i) { return (i >= 0 && i < NRETRO) ? RKEYS[i] : ""; }
const char* retroName (int i) { return (i >= 0 && i < NRETRO) ? RNAMES[i] : "Off"; }
int retroIndex (const std::string& k) { if (k.empty()) return -1; for (int i = 1; i < NRETRO; ++i) if (k == RKEYS[i]) return i; return -1; }

FilterType filterFromString (const std::string& s) { return s == "lp" ? FT_LP : s == "bp" ? FT_BP : s == "hp" ? FT_HP : FT_OFF; }
const char* filterToString (int t) { switch (t) { case FT_LP: return "lp"; case FT_BP: return "bp"; case FT_HP: return "hp"; default: return "off"; } }
const char* filterName (int t) { switch (t) { case FT_LP: return "Low-pass"; case FT_BP: return "Band-pass"; case FT_HP: return "High-pass"; default: return "Off"; } }

namespace
{
struct FxF { const char* key; float FxParams::* m; };
struct FxB { const char* key; bool FxParams::* m; };
const FxF FXF[] = {
    { "satDrive", &FxParams::satDrive }, { "satMix", &FxParams::satMix },
    { "vinCrackle", &FxParams::vinCrackle }, { "vinHiss", &FxParams::vinHiss }, { "vinWow", &FxParams::vinWow }, { "vinTone", &FxParams::vinTone },
    { "scaDiv", &FxParams::scaDiv }, { "scaAmt", &FxParams::scaAmt }, { "scaMix", &FxParams::scaMix },
    { "choRate", &FxParams::choRate }, { "choDepth", &FxParams::choDepth }, { "choMix", &FxParams::choMix },
    { "flaRate", &FxParams::flaRate }, { "flaDepth", &FxParams::flaDepth }, { "flaFb", &FxParams::flaFb }, { "flaMix", &FxParams::flaMix },
    { "delTime", &FxParams::delTime }, { "delFb", &FxParams::delFb }, { "delTone", &FxParams::delTone }, { "delMix", &FxParams::delMix },
    { "ecoTime", &FxParams::ecoTime }, { "ecoFb", &FxParams::ecoFb }, { "ecoTone", &FxParams::ecoTone }, { "ecoWob", &FxParams::ecoWob }, { "ecoMix", &FxParams::ecoMix },
    { "revSize", &FxParams::revSize }, { "revDamp", &FxParams::revDamp }, { "revMix", &FxParams::revMix } };
const FxB FXB[] = {
    { "satOn", &FxParams::satOn }, { "vinOn", &FxParams::vinOn }, { "scaOn", &FxParams::scaOn }, { "choOn", &FxParams::choOn },
    { "flaOn", &FxParams::flaOn }, { "delOn", &FxParams::delOn }, { "ecoOn", &FxParams::ecoOn }, { "revOn", &FxParams::revOn },
    { "lofi", &FxParams::lofi } };
}

Json FxParams::toJson() const
{
    Json o = Json::object();
    o.set ("type", filterToString (type)); o.set ("cut", cut); o.set ("res", res); o.set ("bits", bits); o.set ("rate", rate);
    if (rm > 0) o.set ("rm", retroKey (rm));
    for (auto& f : FXF) o.set (f.key, this->*(f.m));
    for (auto& b : FXB) o.set (b.key, this->*(b.m));
    return o;
}

FxParams FxParams::fromJson (const Json& v)
{
    FxParams f;
    if (! v.isObject()) return f;
    f.type = filterFromString (v.gets ("type", "off"));
    f.cut = (float) clampv (v.get ("cut", f.cut), 20.0, 20000.0);
    f.res = (float) clampv (v.get ("res", f.res), 0.0, 1.0);
    f.bits = clampv ((int) v.get ("bits", f.bits), 2, 16);
    f.rate = clampv ((int) v.get ("rate", f.rate), 2000, 44100);
    f.rm = std::max (0, retroIndex (v.gets ("rm", "")));
    for (auto& x : FXF) if (v.has (x.key)) f.*(x.m) = (float) v.get (x.key, 0.0);
    for (auto& x : FXB) if (v.has (x.key)) f.*(x.m) = v.getb (x.key, false);
    return f;
}

void Project::reset()
{
    for (auto& p : pads) p = Pad();
    tracks.clear();
    for (int i = 0; i < 16; ++i) { Track t; t.pad = i; tracks.push_back (t); }
    plen.fill (16); pbpm.fill (0);
    chain.clear();
    const float mv = s.mvol;
    s = Settings(); s.mvol = mv;
    name = "SESSION_01";
    keep = Json();
}

int Project::trackFor (int pad, bool create)
{
    for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].pad == pad && tracks[(size_t) i].chop == -1) return i;
    if (! create) return -1;
    // reuse an empty track that plays nothing yet
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        auto& t = tracks[(size_t) i];
        bool any = false; for (auto& d : t.pd) if (d && d->hasData()) any = true;
        if (! any && ! pads[(size_t) clampv (t.pad, 0, NPADS - 1)].hasSound() && t.chop == -1) { t = Track(); t.pad = pad; return i; }
    }
    if ((int) tracks.size() >= MAXT) return -1;
    Track t; t.pad = pad; tracks.push_back (t);
    return (int) tracks.size() - 1;
}

std::vector<std::pair<double, double>> segments (const SampleData* buf, const std::vector<float>& markers)
{
    std::vector<std::pair<double, double>> out;
    if (buf == nullptr) return out;
    const double d = buf->duration();
    double prev = 0.0;
    for (float m : markers) { out.push_back ({ prev * d, m * d }); prev = m; }
    out.push_back ({ prev * d, d });
    return out;
}

// ------------------------------------------------------------------ song files
static std::string safeName (const std::string& s0)
{
    std::string o;
    for (unsigned char c : (s0.empty() ? std::string ("sample") : s0))
        o += (isalnum (c) || c == '-' || c == '.' || c == '_') && c < 128 ? (char) c : '_';
    std::string r;
    for (char c : o) { if (c == '_' && ! r.empty() && r.back() == '_') continue; r += c; }
    while (! r.empty() && r.front() == '_') r.erase (r.begin());
    while (! r.empty() && r.back() == '_') r.pop_back();
    if (r.size() > 40) r.resize (40);
    return r.empty() ? "sample" : r;
}
static Json floats (const std::vector<float>& v) { Json a = Json::array(); for (auto x : v) a.push (x); return a; }
static std::vector<float> floatsFrom (const Json& v)
{
    std::vector<float> o;
    if (v.isArray()) for (auto& x : v.a) { const float f = (float) x.num(); if (f > 0 && f < 1) o.push_back (f); }
    std::sort (o.begin(), o.end());
    return o;
}
static std::string rmToString (int rm) { return rm < 0 ? std::string() : rm == 0 ? std::string ("off") : std::string (retroKey (rm)); }
static int rmFromString (const std::string& s) { if (s.empty()) return -1; if (s == "off") return 0; const int i = retroIndex (s); return i > 0 ? i : -1; }

bool saveSong (const Project& p, const std::string& zipPath, std::string& error)
{
    std::vector<std::pair<std::string, std::string>> audio;
    Json pj = Json::object();
    pj.set ("app", "CONTRXL 25"); pj.set ("v", 2);
    pj.set ("name", p.name); pj.set ("bpm", p.s.bpm); pj.set ("kb", p.keep.isObject() ? (int) p.keep.get ("kb", 25) : 25);
    pj.set ("loop", p.s.loop); pj.set ("metro", p.s.metro); pj.set ("quant", p.s.quant);
    pj.set ("count", p.s.count); pj.set ("vel", p.s.vel); pj.set ("choke", p.s.choke);
    {
        Json kp = Json::array(); int last = -1;
        for (int k = 0; k < 128; ++k) if (p.s.keyPad[(size_t) k] != 0) last = k;
        for (int k = 0; k <= last; ++k) kp.push (p.s.keyPad[(size_t) k] - 1);
        pj.set ("keyPads", p.s.keyPads);
        if (last >= 0) pj.set ("keyPad", kp);
    }
    pj.set ("root", p.s.root); pj.set ("gate", p.s.gate);
    pj.set ("mode", p.pads[(size_t) clampv (p.s.sel, 0, NPADS - 1)].keyMode == MODE_CHOP ? "chop" : "keys");
    pj.set ("sel", p.s.sel); pj.set ("autoNorm", p.s.autoNorm); pj.set ("song", p.s.song); pj.set ("pat", p.s.pat);
    { Json a = Json::array(), b = Json::array(); for (int i = 0; i < NPAT; ++i) { a.push (p.plen[(size_t) i]); b.push (p.pbpm[(size_t) i]); } pj.set ("plen", a); pj.set ("pbpm", b); }
    { Json a = Json::array(); for (auto& c : p.chain) { Json o = Json::object(); o.set ("pat", c.pat); o.set ("rep", c.rep); a.push (o); } pj.set ("chain", a); }

    Json padsArr = Json::array();
    for (int i = 0; i < NPADS; ++i)
    {
        const Pad& pd = p.pads[(size_t) i];
        Json o = Json::object();
        o.set ("name", pd.name); o.set ("pitch", pd.pitch); o.set ("markers", floats (pd.markers));
        o.set ("fx", pd.fx.toJson()); o.set ("mg", pd.mg); o.set ("muted", pd.muted);
        o.set ("vol", pd.vol); o.set ("lg", pd.lg);
        o.set ("mode", pd.keyMode == MODE_CHOP ? "chop" : "keys");
        if (std::fabs (pd.pan) > 0.0001f) o.set ("pan", pd.pan);
        if (pd.solo) o.set ("solo", true);
        if (pd.send[0] > 0 || pd.send[1] > 0) { Json a = Json::array(); for (float v : pd.send) a.push (v); o.set ("send", a); }
        if (pd.extra.isObject()) for (auto& kv : pd.extra.o) o.set (kv.first, kv.second);
        if (pd.hasKmap) { Json km = Json::object(); for (auto& kv : pd.kmap) km.set (std::to_string (kv.first), kv.second); o.set ("kmap", km); }
        else o.set ("kmap", Json());
        {
            Json m = Json::object();
            m.set ("vol", pd.m.vol); m.set ("pitch", pd.m.pitch); m.set ("ft", filterToString (pd.m.ft)); m.set ("fc", pd.m.fc); m.set ("pan", pd.m.pan);
            if (pd.m.rm >= 0) m.set ("rm", rmToString (pd.m.rm));
            if (pd.m.st.isObject()) m.set ("st", pd.m.st);
            o.set ("m", m);
        }
        o.set ("wav", Json());
        if (pd.buf)
        {
            const auto w = "samples/" + padLabel (i) + "_" + safeName (pd.name) + ".wav";
            o.set ("wav", w);
            audio.emplace_back (w, io::encodeWav (*pd.buf, 32));
        }
        Json lays = Json::array();
        for (int j = 0; j < (int) pd.layers.size(); ++j)
        {
            const Layer& L = pd.layers[(size_t) j];
            if (! L.buf) continue;
            const auto w = "samples/" + padLabel (i) + "_layer" + std::to_string (j + 1) + "_" + safeName (L.name) + ".wav";
            audio.emplace_back (w, io::encodeWav (*L.buf, 32));
            Json lo = Json::object();
            lo.set ("name", L.name); lo.set ("vol", L.s.vol); lo.set ("pitch", L.s.pitch); lo.set ("ft", filterToString (L.s.ft));
            lo.set ("fc", L.s.fc); lo.set ("pan", L.s.pan); lo.set ("rm", rmToString (L.s.rm)); lo.set ("markers", floats (L.markers)); lo.set ("wav", w);
            if (L.s.st.isObject()) lo.set ("st", L.s.st);
            lays.push (lo);
        }
        o.set ("layers", lays);
        padsArr.push (o);
    }
    pj.set ("pads", padsArr);
    pj.set ("lib", Json::array());

    Json trk = Json::array();
    for (auto& t : p.tracks)
    {
        Json o = Json::object();
        o.set ("pad", t.pad); o.set ("chop", t.chop); o.set ("mute", t.mute); o.set ("vol", t.vol);
        if (t.solo) o.set ("solo", true);
        if (t.lock) o.set ("lock", true);
        Json pd = Json::object();
        for (int i = 0; i < NPAT; ++i)
        {
            const auto* d = t.find (i);
            if (d == nullptr || ! d->hasData()) continue;
            const int len = p.plen[(size_t) i];
            Json st = Json::array(), se = Json::array(), ev = Json::array();
            for (int k = 0; k < len; ++k) { st.push ((int) d->steps[(size_t) k]); se.push (d->semi[(size_t) k]); }
            for (auto& e : d->ev)
            {
                Json eo = Json::object(); eo.set ("t", e.t); eo.set ("p", e.p); eo.set ("v", e.v);
                if (e.c >= -1) eo.set ("c", e.c);
                ev.push (eo);
            }
            Json x = Json::object(); x.set ("steps", st); x.set ("semi", se); x.set ("ev", ev);
            if (d->pad >= 0) x.set ("pad", d->pad);
            {
                Json ch = Json::array(); bool any = false;
                for (int k = 0; k < len; ++k) { ch.push ((int) d->chopAt[(size_t) k]); any |= d->chopAt[(size_t) k] >= -1; }
                if (any) x.set ("chops", ch);
            }
            if (d->chop >= -1) x.set ("chop", d->chop);
            pd.set (std::to_string (i), x);
        }
        o.set ("pd", pd);
        trk.push (o);
    }
    pj.set ("tracks", trk);
    // everything else the song had (LA-2A, plug-ins on the master / returns, crates...) goes back unchanged
    if (p.keep.isObject())
        for (auto& kv : p.keep.o)
            if (! pj.has (kv.first) && kv.first != "lib") pj.set (kv.first, kv.second);
    if (! pj.has ("crates")) { Json c = Json::array(); for (auto* n : { "Drums", "Bass", "Vocals", "Loops", "FX" }) c.push (n); pj.set ("crates", c); }

    io::ZipWriter zw;
    zw.add ("project.json", pj.dump (true));
    for (auto& e : audio) zw.add (e.first, e.second);
    if (! io::writeFileAtomic (zipPath, zw.finish())) { error = "could not write the song file"; return false; }
    return true;
}

bool loadSong (const std::string& zipPath, Project& P, std::string& message)
{
    io::ZipReader zr;
    if (! zr.open (zipPath)) { message = "not a song file"; return false; }
    std::string jtxt;
    if (! zr.read ("project.json", jtxt)) { message = "this is not a CONTRXL 25 song"; return false; }
    bool ok = false;
    const Json pj = Json::parse (jtxt, &ok);
    if (! ok || ! pj.isObject()) { message = "damaged song file"; return false; }
    const auto app = pj.gets ("app", "");
    if (app != "CONTRXL 25" && app != "KONTRXL 25") { message = "this is not a CONTRXL 25 song"; return false; }

    auto dec = [&] (const Json& wav) -> SamplePtr
    {
        const auto name = wav.str();
        if (name.empty()) return nullptr;
        std::string data;
        if (! zr.read (name, data)) return nullptr;
        auto s = io::decodeWav (data);
        return io::resampledTo (s, SR);
    };

    Project np;
    np.s.mvol = P.s.mvol;
    int nSamples = 0;
    const Json& pads = pj["pads"];
    if (pads.isArray())
        for (int i = 0; i < NPADS && i < (int) pads.a.size(); ++i)
        {
            const Json& o = pads.a[(size_t) i];
            Pad& pd = np.pads[(size_t) i];
            pd.buf = dec (o["wav"]);
            if (pd.buf) ++nSamples;
            pd.name = o.gets ("name", "");
            pd.pitch = (float) clampv (o.get ("pitch", 0.0), -36.0, 36.0);
            pd.markers = floatsFrom (o["markers"]);
            pd.fx = FxParams::fromJson (o["fx"]);
            pd.mg = clampv ((int) o.get ("mg", 0), 0, 16); pd.lg = clampv ((int) o.get ("lg", 0), 0, 16);
            pd.muted = o.getb ("muted", false);
            pd.vol = (float) clampv (o.get ("vol", 1.0), 0.0, 2.0);
            pd.pan = (float) clampv (o.get ("pan", 0.0), -1.0, 1.0);
            pd.solo = o.getb ("solo", false);
            if (o["send"].isArray()) for (int k = 0; k < 2 && k < (int) o["send"].a.size(); ++k) pd.send[(size_t) k] = (float) clampv (o["send"].a[(size_t) k].num(), 0.0, 1.0);
            for (auto* key : { "vstInst", "instRoot", "vstFx" }) if (o.has (key) && ! o[key].isNull()) pd.extra.set (key, o[key]);
            if (o.has ("mode")) pd.keyMode = o.gets ("mode", "keys") == "chop" ? MODE_CHOP : MODE_KEYS;
            else pd.keyMode = -1;
            if (o["kmap"].isObject())
            {
                pd.hasKmap = true;
                for (auto& kv : o["kmap"].o) pd.kmap[atoi (kv.first.c_str())] = kv.second.integer();
            }
            const Json& m = o["m"];
            if (m.isObject())
            {
                pd.m.vol = (float) m.get ("vol", 1.0); pd.m.pitch = (float) m.get ("pitch", 0.0);
                pd.m.ft = filterFromString (m.gets ("ft", "off")); pd.m.fc = (float) m.get ("fc", 2000.0);
                pd.m.pan = (float) m.get ("pan", 0.0); pd.m.rm = rmFromString (m.gets ("rm", ""));
                if (m["st"].isObject()) pd.m.st = m["st"];
            }
            if (o["layers"].isArray())
                for (auto& q : o["layers"].a)
                {
                    if ((int) pd.layers.size() >= MAXLAY) break;
                    Layer L;
                    L.buf = dec (q["wav"]);
                    if (! L.buf) continue;
                    L.name = q.gets ("name", "layer");
                    L.s.vol = (float) q.get ("vol", 1.0); L.s.pitch = (float) q.get ("pitch", 0.0);
                    L.s.ft = filterFromString (q.gets ("ft", "off")); L.s.fc = (float) q.get ("fc", 2000.0);
                    L.s.pan = (float) q.get ("pan", 0.0); L.s.rm = rmFromString (q.gets ("rm", ""));
                    if (q["st"].isObject()) L.s.st = q["st"];
                    L.markers = floatsFrom (q["markers"]);
                    pd.layers.push_back (std::move (L));
                }
        }

    np.tracks.clear();
    if (pj["tracks"].isArray())
        for (auto& q : pj["tracks"].a)
        {
            if ((int) np.tracks.size() >= MAXT) break;
            Track t;
            t.pad = clampv ((int) q.get ("pad", 0), 0, NPADS - 1);
            t.chop = (int) q.get ("chop", -1);
            t.mute = q.getb ("mute", false); t.vol = (float) q.get ("vol", 1.0);
            t.solo = q.getb ("solo", false); t.lock = q.getb ("lock", false);
            if (q["pd"].isObject())
                for (auto& kv : q["pd"].o)
                {
                    const int pi = atoi (kv.first.c_str());
                    if (pi < 0 || pi >= NPAT) continue;
                    const Json& x = kv.second;
                    auto& d = t.get (pi);
                    if (x.has ("pad")) d.pad = clampv ((int) x.get ("pad", -1), -1, NPADS - 1);
                    if (x.has ("chop")) d.chop = std::max (-2, (int) x.get ("chop", -2));
                    if (x["steps"].isArray()) for (size_t k = 0; k < x["steps"].a.size() && k < MAXSTEP; ++k) d.steps[k] = x["steps"].a[k].integer() ? 1 : 0;
                    if (x["semi"].isArray()) for (size_t k = 0; k < x["semi"].a.size() && k < MAXSTEP; ++k) d.semi[k] = (float) x["semi"].a[k].num();
                    if (x["ev"].isArray())
                        for (auto& e : x["ev"].a)
                            d.ev.push_back ({ clampv ((int) e.get ("t", 0), 0, MAXSTEP * TPS - 1), (float) e.get ("p", 0.0), (float) clampv (e.get ("v", 0.8), 0.0, 1.5), (int) e.get ("c", -2) });
                    if (x["chops"].isArray()) for (size_t k = 0; k < x["chops"].a.size() && k < MAXSTEP; ++k) d.chopAt[k] = (int16_t) clampv (x["chops"].a[k].integer(), -2, 4096);
                }
            np.tracks.push_back (std::move (t));
        }
    if (np.tracks.empty()) for (int i = 0; i < 16; ++i) { Track t; t.pad = i; np.tracks.push_back (t); }

    if (pj["plen"].isArray()) for (int i = 0; i < NPAT && i < (int) pj["plen"].a.size(); ++i) { const int v = pj["plen"].a[(size_t) i].integer(); if (v == 16 || v == 32 || v == 64 || v == 128) np.plen[(size_t) i] = v; }
    if (pj["pbpm"].isArray()) for (int i = 0; i < NPAT && i < (int) pj["pbpm"].a.size(); ++i) { const int v = pj["pbpm"].a[(size_t) i].integer(); if (v >= 40 && v <= 240) np.pbpm[(size_t) i] = v; }
    if (pj["chain"].isArray())
        for (auto& c : pj["chain"].a)
        {
            const int pat = (int) c.get ("pat", -1);
            if (pat >= 0 && pat < NPAT) np.chain.push_back ({ pat, clampv ((int) c.get ("rep", 1), 1, 16) });
        }
    np.s.song = pj.getb ("song", false) && ! np.chain.empty();
    np.s.loop = pj.getb ("loop", true); np.s.metro = pj.getb ("metro", false); np.s.quant = pj.getb ("quant", false);
    np.s.count = pj.getb ("count", true); np.s.vel = pj.getb ("vel", true); np.s.choke = pj.getb ("choke", true);
    np.s.gate = pj.getb ("gate", false);
    np.s.keyPad.fill (0);
    if (pj["keyPad"].isArray())
        for (int k = 0; k < 128 && k < (int) pj["keyPad"].a.size(); ++k)
        {
            const int gi = pj["keyPad"].a[(size_t) k].integer (-1);
            np.s.keyPad[(size_t) k] = gi >= 0 && gi < NPADS ? gi + 1 : 0;
        }
    np.s.keyPads = pj.getb ("keyPads", false);
    np.s.autoNorm = pj.getb ("autoNorm", true);
    np.s.root = clampv ((int) pj.get ("root", 60), 0, 127);
    const bool chopSong = pj.gets ("mode", "keys") == "chop";
    np.s.bpm = (float) clampv ((int) std::lround (pj.get ("bpm", 84.0)), 40, 240);
    np.s.sel = clampv ((int) pj.get ("sel", 0), 0, NPADS - 1);
    np.s.pat = clampv ((int) pj.get ("pat", 0), 0, NPAT - 1);
    np.name = pj.gets ("name", "SESSION_01");
    for (int i = 0; i < NPADS; ++i)
    {
        auto& pd = np.pads[(size_t) i];
        if (pd.keyMode < 0) pd.keyMode = (chopSong && (i == np.s.sel || pd.hasKmap)) ? MODE_CHOP : MODE_KEYS;
    }
    np.keep = Json::object();
    for (auto& kv : pj.o)
        if (kv.first == "la" || kv.first == "vstMaster" || kv.first == "returns" || kv.first == "crates" || kv.first == "kb")
            np.keep.set (kv.first, kv.second);
    P = std::move (np);
    message = "Loaded \"" + P.name + "\" - " + std::to_string (nSamples) + " samples";
    return true;
}
} // namespace cx
