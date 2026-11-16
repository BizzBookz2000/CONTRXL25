#include "SongIO.h"

namespace cx::io
{
// ------------------------------------------------------------------ plug-in slots in song files
juce::var pluginSlotToVar (const PluginSlot& s)
{
    if (s.empty()) return {};
    auto* o = new juce::DynamicObject();
    o->setProperty ("desc", s.xml); o->setProperty ("name", s.name); o->setProperty ("state", s.state);
    o->setProperty ("bypass", s.bypass); o->setProperty ("instrument", s.instrument);
    return juce::var (o);
}

PluginSlot pluginSlotFromVar (const juce::var& v)
{
    PluginSlot s;
    if (! v.isObject() || v.getProperty ("desc", "").toString().isEmpty()) return s;
    s.iid = newPluginIid();
    s.xml = v.getProperty ("desc", "").toString();
    s.name = v.getProperty ("name", "").toString();
    s.state = v.getProperty ("state", "").toString();
    s.bypass = (bool) v.getProperty ("bypass", false);
    s.instrument = (bool) v.getProperty ("instrument", false);
    return s;
}

juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager fm;
    static bool init = false;
    if (! init) { fm.registerBasicFormats(); init = true; }
    return fm;
}

juce::String audioWildcard()
{
    return "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.wma";
}

static SamplePtr readAll (juce::AudioFormatReader* r, juce::String* err)
{
    std::unique_ptr<juce::AudioFormatReader> reader (r);
    if (reader == nullptr) { if (err) *err = "unsupported or damaged audio file"; return nullptr; }
    const auto len = reader->lengthInSamples;
    if (len <= 0 || len > (juce::int64) 48000 * 60 * 30) { if (err) *err = "empty or too long (max 30 minutes)"; return nullptr; }
    const int ch = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> b (ch, (int) len);
    reader->read (&b, 0, (int) len, 0, true, ch > 1);
    return makeSample (std::move (b), reader->sampleRate);
}

SamplePtr decodeFile (const juce::File& f, juce::String* err)
{
    return readAll (formats().createReaderFor (f), err);
}

SamplePtr decodeMemory (const void* data, size_t size, juce::String* err)
{
    auto stream = std::make_unique<juce::MemoryInputStream> (data, size, true);
    return readAll (formats().createReaderFor (std::move (stream)), err);
}

// ------------------------------------------------------------------ WAV
juce::MemoryBlock encodeWav (const juce::AudioBuffer<float>& b, double sampleRate, int bits)
{
    if (bits != 16 && bits != 24) bits = 32;
    const int nch = b.getNumChannels(), n = b.getNumSamples(), bytes = bits / 8;
    const juce::uint32 dataLen = (juce::uint32) n * (juce::uint32) nch * (juce::uint32) bytes;
    juce::MemoryOutputStream o;
    auto u32 = [&o] (juce::uint32 v) { o.writeInt ((int) v); };
    auto u16 = [&o] (int v) { o.writeShort ((short) v); };
    o.write ("RIFF", 4); u32 (36 + dataLen); o.write ("WAVE", 4);
    o.write ("fmt ", 4); u32 (16); u16 (bits == 32 ? 3 : 1); u16 (nch);
    u32 ((juce::uint32) sampleRate); u32 ((juce::uint32) sampleRate * (juce::uint32) (nch * bytes)); u16 (nch * bytes); u16 (bits);
    o.write ("data", 4); u32 (dataLen);
    o.preallocate (44 + dataLen);
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < nch; ++c)
        {
            float x = b.getSample (c, i);
            if (bits == 32) { o.writeFloat (x); continue; }
            x = juce::jlimit (-1.0f, 1.0f, x);
            if (bits == 16) o.writeShort ((short) std::round (x < 0 ? x * 32768.0f : x * 32767.0f));
            else
            {
                const int q = (int) std::round (x < 0 ? x * 8388608.0f : x * 8388607.0f);
                const char tri[3] = { (char) (q & 255), (char) ((q >> 8) & 255), (char) ((q >> 16) & 255) };
                o.write (tri, 3);
            }
        }
    return o.getMemoryBlock();
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sampleRate, int bits)
{
    const auto mb = encodeWav (b, sampleRate, bits);
    return f.replaceWithData (mb.getData(), mb.getSize());
}

juce::String safeName (const juce::String& s)
{
    juce::String o;
    for (auto c : (s.isEmpty() ? juce::String ("sample") : s))
        o << ((juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '.' || c == '_') && c < 128 ? juce::String::charToString (c) : juce::String ("_"));
    while (o.contains ("__")) o = o.replace ("__", "_");
    o = o.trimCharactersAtStart ("_").trimCharactersAtEnd ("_").substring (0, 40);
    return o.isEmpty() ? "sample" : o;
}

static juce::MemoryBlock wavOf (const SampleData& s) { return encodeWav (s.audio, s.sampleRate, 32); }

// ------------------------------------------------------------------ slot settings <-> json
static juce::String rmToString (int rm) { return rm < 0 ? juce::String() : rm == 0 ? juce::String ("off") : juce::String (retroKey (rm)); }
static int rmFromString (const juce::String& s) { if (s.isEmpty()) return -1; if (s == "off") return 0; const int i = retroIndex (s); return i > 0 ? i : -1; }

static juce::var stemVar (const StemMix& st)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("v", st.lv[0]); o->setProperty ("b", st.lv[1]); o->setProperty ("i", st.lv[2]); o->setProperty ("d", st.lv[3]);
    return juce::var (o);
}
static StemMix stemFrom (const juce::var& v)
{
    StemMix st;
    if (! v.isObject()) return st;
    const char* k[4] = { "v", "b", "i", "d" };
    for (int i = 0; i < 4; ++i) if (v.hasProperty (k[i])) st.lv[(size_t) i] = (float) (double) v.getProperty (k[i], 1.0);
    return st;
}
static juce::var floats (const std::vector<float>& v) { juce::Array<juce::var> a; for (auto x : v) a.add (x); return a; }
static std::vector<float> floatsFrom (const juce::var& v)
{
    std::vector<float> o;
    if (auto* a = v.getArray()) for (auto& x : *a) o.push_back ((float) (double) x);
    std::sort (o.begin(), o.end());
    return o;
}

// ------------------------------------------------------------------ save
bool saveSong (const Project& p, const juce::File& zipFile, juce::String& error)
{
    std::vector<std::pair<juce::String, juce::MemoryBlock>> audio;
    auto add = [&audio] (const juce::String& name, juce::MemoryBlock data) { audio.emplace_back (name, std::move (data)); };
    auto* pj = new juce::DynamicObject();
    juce::var root (pj);
    pj->setProperty ("app", "CONTRXL 25"); pj->setProperty ("v", 2);
    pj->setProperty ("name", p.name); pj->setProperty ("bpm", p.s.bpm); pj->setProperty ("kb", p.s.keys);
    pj->setProperty ("loop", p.s.loop); pj->setProperty ("metro", p.s.metro); pj->setProperty ("quant", p.s.quant);
    pj->setProperty ("count", p.s.count); pj->setProperty ("vel", p.s.vel); pj->setProperty ("choke", p.s.choke);
    {
        // pads on keys (the web version reads the same: keyPad[i] = pad index or -1)
        juce::Array<juce::var> kp; int last = -1;
        for (int k = 0; k < 128; ++k) if (p.s.keyPad[(size_t) k] != 0) last = k;
        for (int k = 0; k <= last; ++k) kp.add (p.s.keyPad[(size_t) k] - 1);
        pj->setProperty ("keyPads", p.s.keyPads);
        if (last >= 0) pj->setProperty ("keyPad", kp);
    }
    pj->setProperty ("root", p.s.root); pj->setProperty ("gate", p.s.gate); pj->setProperty ("mode", p.pads[(size_t) p.s.sel].keyMode == MODE_CHOP ? "chop" : "keys");
    pj->setProperty ("sel", p.s.sel); pj->setProperty ("autoNorm", p.s.autoNorm); pj->setProperty ("song", p.s.song); pj->setProperty ("pat", p.s.pat);
    { juce::Array<juce::var> a, b; for (int i = 0; i < NPAT; ++i) { a.add (p.plen[(size_t) i]); b.add (p.pbpm[(size_t) i]); } pj->setProperty ("plen", a); pj->setProperty ("pbpm", b); }
    { juce::Array<juce::var> a; for (auto& c : p.chain) { auto* o = new juce::DynamicObject(); o->setProperty ("pat", c.pat); o->setProperty ("rep", c.rep); a.add (juce::var (o)); } pj->setProperty ("chain", a); }
    { auto* o = new juce::DynamicObject(); o->setProperty ("on", p.la.on); o->setProperty ("pr", p.la.pr); o->setProperty ("gain", p.la.gain); o->setProperty ("limit", p.la.limit); pj->setProperty ("la", juce::var (o)); }

    juce::Array<juce::var> padsArr;
    for (int i = 0; i < NPADS; ++i)
    {
        const Pad& pd = p.pads[(size_t) i];
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", pd.name); o->setProperty ("pitch", pd.pitch); o->setProperty ("markers", floats (pd.markers));
        o->setProperty ("fx", pd.fx.toVar()); o->setProperty ("mg", pd.mg); o->setProperty ("muted", pd.muted);
        o->setProperty ("vol", pd.vol); o->setProperty ("lg", pd.lg);
        o->setProperty ("mode", pd.keyMode == MODE_CHOP ? "chop" : "keys");   // desktop: per-pad mode (the web ignores it)
        // desktop: mixer + VST plug-ins (the web ignores these)
        if (std::abs (pd.pan) > 0.0001f) o->setProperty ("pan", pd.pan);
        if (pd.solo) o->setProperty ("solo", true);
        if (pd.send[0] > 0 || pd.send[1] > 0) { juce::Array<juce::var> a; for (float v : pd.send) a.add (v); o->setProperty ("send", a); }
        if (! pd.inst.empty()) { o->setProperty ("vstInst", pluginSlotToVar (pd.inst)); o->setProperty ("instRoot", pd.instRoot); }
        {
            juce::Array<juce::var> a; bool any = false;
            for (auto& s : pd.vfx) { a.add (pluginSlotToVar (s)); any |= ! s.empty(); }
            if (any) o->setProperty ("vstFx", a);
        }
        if (pd.hasKmap)
        {
            auto* km = new juce::DynamicObject();
            for (auto& kv : pd.kmap) km->setProperty (juce::String (kv.first), kv.second);
            o->setProperty ("kmap", juce::var (km));
        }
        else o->setProperty ("kmap", juce::var());
        {
            auto* m = new juce::DynamicObject();
            m->setProperty ("vol", pd.m.vol); m->setProperty ("pitch", pd.m.pitch); m->setProperty ("ft", filterToString (pd.m.ft));
            m->setProperty ("fc", pd.m.fc); m->setProperty ("pan", pd.m.pan);
            if (pd.m.rm >= 0) m->setProperty ("rm", rmToString (pd.m.rm));
            if (! pd.m.st.isUnity()) m->setProperty ("st", stemVar (pd.m.st));
            o->setProperty ("m", juce::var (m));
        }
        o->setProperty ("wav", juce::var());
        if (pd.buf)
        {
            const auto w = "samples/" + padLabel (i) + "_" + safeName (pd.name) + ".wav";
            o->setProperty ("wav", w);
            add (w, wavOf (*pd.buf));
        }
        juce::Array<juce::var> lays;
        for (int j = 0; j < (int) pd.layers.size(); ++j)
        {
            const Layer& L = pd.layers[(size_t) j];
            if (! L.buf) continue;
            const auto w = "samples/" + padLabel (i) + "_layer" + juce::String (j + 1) + "_" + safeName (L.name) + ".wav";
            add (w, wavOf (*L.buf));
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("name", L.name); lo->setProperty ("vol", L.s.vol); lo->setProperty ("pitch", L.s.pitch);
            lo->setProperty ("ft", filterToString (L.s.ft)); lo->setProperty ("fc", L.s.fc); lo->setProperty ("pan", L.s.pan);
            lo->setProperty ("rm", rmToString (L.s.rm)); lo->setProperty ("markers", floats (L.markers)); lo->setProperty ("wav", w);
            if (! L.s.st.isUnity()) lo->setProperty ("st", stemVar (L.s.st));
            lays.add (juce::var (lo));
        }
        o->setProperty ("layers", lays);
        padsArr.add (juce::var (o));
    }
    pj->setProperty ("pads", padsArr);

    juce::Array<juce::var> libArr;
    for (int i = 0; i < (int) p.lib.size(); ++i)
    {
        const auto& it = p.lib[(size_t) i];
        if (! it.buf) continue;
        const auto w = "library/" + juce::String (i + 1).paddedLeft ('0', 3) + "_" + safeName (it.name) + ".wav";
        add (w, wavOf (*it.buf));
        auto* o = new juce::DynamicObject(); o->setProperty ("name", it.name); o->setProperty ("wav", w); o->setProperty ("crate", it.crate);
        libArr.add (juce::var (o));
    }
    pj->setProperty ("lib", libArr);
    {
        juce::Array<juce::var> m; bool any = false;
        for (auto& s : p.masterVfx) { m.add (pluginSlotToVar (s)); any |= ! s.empty(); }
        juce::Array<juce::var> sends;
        for (int k = 0; k < NSEND; ++k)
        {
            auto* so = new juce::DynamicObject();
            juce::Array<juce::var> a;
            for (auto& s : p.sendVfx[(size_t) k]) { a.add (pluginSlotToVar (s)); any |= ! s.empty(); }
            so->setProperty ("fx", a); so->setProperty ("level", p.retLevel[(size_t) k]);
            sends.add (juce::var (so));
        }
        pj->setProperty ("vstMaster", m);
        pj->setProperty ("returns", sends);
        juce::ignoreUnused (any);
    }
    { juce::Array<juce::var> a; for (auto& c : p.crates) a.add (c); pj->setProperty ("crates", a); }

    juce::Array<juce::var> trk;
    for (auto& t : p.tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("pad", t.pad); o->setProperty ("chop", t.chop); o->setProperty ("mute", t.mute); o->setProperty ("vol", t.vol);
        if (t.solo) o->setProperty ("solo", true);     // desktop only
        if (t.lock) o->setProperty ("lock", true);
        auto* pd = new juce::DynamicObject();
        for (int i = 0; i < NPAT; ++i)
        {
            const auto* d = t.find (i);
            if (d == nullptr || ! d->hasData()) continue;
            const int len = p.plen[(size_t) i];
            juce::Array<juce::var> st, se, ev;
            for (int k = 0; k < len; ++k) { st.add ((int) d->steps[(size_t) k]); se.add (d->semi[(size_t) k]); }
            for (auto& e : d->ev) { auto* eo = new juce::DynamicObject(); eo->setProperty ("t", e.t); eo->setProperty ("p", e.p); eo->setProperty ("v", e.v); if (e.c >= -1) eo->setProperty ("c", e.c); ev.add (juce::var (eo)); }
            auto* x = new juce::DynamicObject(); x->setProperty ("steps", st); x->setProperty ("semi", se); x->setProperty ("ev", ev);
            if (d->pad >= 0) x->setProperty ("pad", d->pad);        // desktop: this pattern plays another pad
            {
                juce::Array<juce::var> ch; bool any = false;
                for (int k = 0; k < len; ++k) { ch.add ((int) d->chopAt[(size_t) k]); any |= d->chopAt[(size_t) k] >= -1; }
                if (any) x->setProperty ("chops", ch);              // desktop: chop per step (keys in chop mode)
            }
            if (d->chop >= -1) x->setProperty ("chop", d->chop);
            pd->setProperty (juce::String (i), juce::var (x));
        }
        o->setProperty ("pd", juce::var (pd));
        trk.add (juce::var (o));
    }
    pj->setProperty ("tracks", trk);

    const auto json = juce::JSON::toString (root, true);
    // one stored (uncompressed) zip: project.json first, then the audio. Stored entries keep it loadable by the web version.
    juce::ZipFile::Builder zb;
    const auto t = juce::Time::getCurrentTime();
    zb.addEntry (new juce::MemoryInputStream (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 0, "project.json", t);
    for (auto& e : audio) zb.addEntry (new juce::MemoryInputStream (std::move (e.second)), 0, e.first, t);
    juce::TemporaryFile out (zipFile);
    {
        juce::FileOutputStream os (out.getFile());
        if (! os.openedOk() || ! zb.writeToStream (os, nullptr)) { error = "could not write the zip"; return false; }
    }
    if (! out.overwriteTargetFileWithTemporary()) { error = "cannot replace " + zipFile.getFullPathName(); return false; }
    return true;
}

// ------------------------------------------------------------------ load
bool loadSong (const juce::File& zipFile, Project& P, juce::String& message)
{
    juce::ZipFile zf (zipFile);
    const int pjIndex = zf.getIndexOfFileName ("project.json");
    if (pjIndex < 0) { message = "this is not a CONTRXL 25 song"; return false; }
    auto readEntry = [&zf] (const juce::String& name, juce::MemoryBlock& mb)
    {
        const int idx = zf.getIndexOfFileName (name);
        if (idx < 0) return false;
        std::unique_ptr<juce::InputStream> in (zf.createStreamForEntry (idx));
        if (! in) return false;
        mb.reset(); in->readIntoMemoryBlock (mb);
        return true;
    };
    juce::MemoryBlock jm;
    if (! readEntry ("project.json", jm)) { message = "damaged song file"; return false; }
    const auto pj = juce::JSON::parse (jm.toString());
    const auto app = pj.getProperty ("app", "").toString();
    if (app != "CONTRXL 25" && app != "KONTRXL 25") { message = "this is not a CONTRXL 25 song"; return false; }

    auto dec = [&] (const juce::var& wav) -> SamplePtr
    {
        const auto name = wav.toString();
        if (name.isEmpty()) return nullptr;
        juce::MemoryBlock mb;
        if (! readEntry (name, mb)) return nullptr;
        return decodeMemory (mb.getData(), mb.getSize());
    };

    Project np;
    np.s.mvol = P.s.mvol;
    np.lib.clear(); np.crates.clear();        // samples a (web) song carries for the library; the controller adds them to the library folder
    int nSamples = 0;
    if (auto* pads = pj.getProperty ("pads", juce::var()).getArray())
        for (int i = 0; i < NPADS && i < pads->size(); ++i)
        {
            const auto& o = pads->getReference (i);
            Pad& pd = np.pads[(size_t) i];
            pd.buf = dec (o.getProperty ("wav", juce::var()));
            if (pd.buf) ++nSamples;
            pd.name = o.getProperty ("name", "").toString();
            pd.pitch = (float) (double) o.getProperty ("pitch", 0.0);
            pd.markers = floatsFrom (o.getProperty ("markers", juce::var()));
            pd.fx = FxParams::fromVar (o.getProperty ("fx", juce::var()));
            pd.mg = (int) o.getProperty ("mg", 0); pd.lg = (int) o.getProperty ("lg", 0);
            pd.muted = (bool) o.getProperty ("muted", false);
            pd.vol = (float) (double) o.getProperty ("vol", 1.0);
            pd.pan = (float) juce::jlimit (-1.0, 1.0, (double) o.getProperty ("pan", 0.0));
            pd.solo = (bool) o.getProperty ("solo", false);
            if (auto* a = o.getProperty ("send", juce::var()).getArray())
                for (int k = 0; k < NSEND && k < a->size(); ++k) pd.send[(size_t) k] = (float) juce::jlimit (0.0, 1.0, (double) a->getReference (k));
            pd.inst = pluginSlotFromVar (o.getProperty ("vstInst", juce::var()));
            pd.instRoot = juce::jlimit (0, 127, (int) o.getProperty ("instRoot", 60));
            if (auto* a = o.getProperty ("vstFx", juce::var()).getArray())
                for (int k = 0; k < PAD_VFX && k < a->size(); ++k) pd.vfx[(size_t) k] = pluginSlotFromVar (a->getReference (k));
            if (o.hasProperty ("mode")) pd.keyMode = o.getProperty ("mode", "keys").toString() == "chop" ? MODE_CHOP : MODE_KEYS;
            else pd.keyMode = -1;   // decided below from the song-wide mode (web songs)
            const auto km = o.getProperty ("kmap", juce::var());
            if (auto* kobj = km.getDynamicObject())
            {
                pd.hasKmap = true;
                for (auto& prop : kobj->getProperties()) pd.kmap[prop.name.toString().getIntValue()] = (int) prop.value;
            }
            const auto m = o.getProperty ("m", juce::var());
            if (m.isObject())
            {
                pd.m.vol = (float) (double) m.getProperty ("vol", 1.0); pd.m.pitch = (float) (double) m.getProperty ("pitch", 0.0);
                pd.m.ft = filterFromString (m.getProperty ("ft", "off").toString()); pd.m.fc = (float) (double) m.getProperty ("fc", 2000.0);
                pd.m.pan = (float) (double) m.getProperty ("pan", 0.0); pd.m.rm = rmFromString (m.getProperty ("rm", "").toString());
                pd.m.st = stemFrom (m.getProperty ("st", juce::var()));
            }
            if (auto* lays = o.getProperty ("layers", juce::var()).getArray())
                for (auto& q : *lays)
                {
                    Layer L;
                    L.buf = dec (q.getProperty ("wav", juce::var()));
                    if (! L.buf) continue;
                    L.name = q.getProperty ("name", "layer").toString();
                    L.s.vol = (float) (double) q.getProperty ("vol", 1.0); L.s.pitch = (float) (double) q.getProperty ("pitch", 0.0);
                    L.s.ft = filterFromString (q.getProperty ("ft", "off").toString()); L.s.fc = (float) (double) q.getProperty ("fc", 2000.0);
                    L.s.pan = (float) (double) q.getProperty ("pan", 0.0); L.s.rm = rmFromString (q.getProperty ("rm", "").toString());
                    L.s.st = stemFrom (q.getProperty ("st", juce::var()));
                    L.markers = floatsFrom (q.getProperty ("markers", juce::var()));
                    pd.layers.push_back (std::move (L));
                }
        }

    // library: merge, skipping duplicates (same name + length + crate)
    int nLib = 0;
    if (auto* crates = pj.getProperty ("crates", juce::var()).getArray())
        for (auto& c : *crates) if (std::find (np.crates.begin(), np.crates.end(), c.toString()) == np.crates.end()) np.crates.push_back (c.toString());
    if (auto* lib = pj.getProperty ("lib", juce::var()).getArray())
    {
        int nextId = 1; for (auto& it : np.lib) nextId = juce::jmax (nextId, it.id + 1);
        for (auto& q : *lib)
        {
            auto b = dec (q.getProperty ("wav", juce::var()));
            if (! b) continue;
            LibItem it; it.id = nextId++; it.name = q.getProperty ("name", "sample").toString(); it.crate = q.getProperty ("crate", "").toString(); it.buf = b;
            bool dup = false;
            for (auto& x : np.lib) if (x.name == it.name && x.crate == it.crate && x.buf && x.buf->length() == b->length()) { dup = true; break; }
            if (! dup) { np.lib.push_back (it); ++nLib; }
        }
    }

    np.tracks.clear();
    if (auto* trk = pj.getProperty ("tracks", juce::var()).getArray())
    {
        for (auto& q : *trk)
        {
            if ((int) np.tracks.size() >= MAXT) break;
            Track t;
            t.pad = juce::jlimit (0, NPADS - 1, (int) q.getProperty ("pad", 0));
            t.chop = (int) q.getProperty ("chop", -1);
            t.mute = (bool) q.getProperty ("mute", false);
            t.vol = (float) (double) q.getProperty ("vol", 1.0);
            t.solo = (bool) q.getProperty ("solo", false);
            t.lock = (bool) q.getProperty ("lock", false);
            if (auto* pdo = q.getProperty ("pd", juce::var()).getDynamicObject())
                for (auto& prop : pdo->getProperties())
                {
                    const int pi = prop.name.toString().getIntValue();
                    if (pi < 0 || pi >= NPAT) continue;
                    auto& d = t.get (pi);
                    if (prop.value.hasProperty ("pad")) d.pad = juce::jlimit (-1, NPADS - 1, (int) prop.value.getProperty ("pad", -1));
                    if (prop.value.hasProperty ("chop")) d.chop = juce::jmax (-2, (int) prop.value.getProperty ("chop", -2));
                    if (auto* st = prop.value.getProperty ("steps", juce::var()).getArray())
                        for (int k = 0; k < st->size() && k < MAXSTEP; ++k) d.steps[(size_t) k] = (int) st->getReference (k) ? 1 : 0;
                    if (auto* se = prop.value.getProperty ("semi", juce::var()).getArray())
                        for (int k = 0; k < se->size() && k < MAXSTEP; ++k) d.semi[(size_t) k] = (float) (double) se->getReference (k);
                    if (auto* ev = prop.value.getProperty ("ev", juce::var()).getArray())
                        for (auto& e : *ev)
                            d.ev.push_back ({ (int) e.getProperty ("t", 0), (float) (double) e.getProperty ("p", 0.0), (float) (double) e.getProperty ("v", 0.8), (int) e.getProperty ("c", -2) });
                    if (auto* ch = prop.value.getProperty ("chops", juce::var()).getArray())
                        for (int k = 0; k < ch->size() && k < MAXSTEP; ++k) d.chopAt[(size_t) k] = (int16_t) juce::jlimit (-2, 4096, (int) ch->getReference (k));
                }
            np.tracks.push_back (std::move (t));
        }
    }
    if (np.tracks.empty())
        for (int i = 0; i < 16; ++i) { Track t; t.pad = i; np.tracks.push_back (t); }

    if (auto* a = pj.getProperty ("plen", juce::var()).getArray())
        for (int i = 0; i < NPAT && i < a->size(); ++i) { const int v = (int) a->getReference (i); if (v == 16 || v == 32 || v == 64 || v == 128) np.plen[(size_t) i] = v; }
    if (auto* a = pj.getProperty ("pbpm", juce::var()).getArray())
        for (int i = 0; i < NPAT && i < a->size(); ++i) { const int v = (int) std::round ((double) a->getReference (i)); if (v >= 40 && v <= 240) np.pbpm[(size_t) i] = v; }
    if (auto* a = pj.getProperty ("chain", juce::var()).getArray())
        for (auto& c : *a)
        {
            const int pat = (int) c.getProperty ("pat", -1);
            if (pat >= 0 && pat < NPAT) np.chain.push_back ({ pat, juce::jlimit (1, 16, (int) c.getProperty ("rep", 1)) });
        }

    np.s.song = (bool) pj.getProperty ("song", false) && ! np.chain.empty();
    np.s.keys = P.s.keys;   // the keyboard you play on stays (it's hardware, not part of the song)
    np.s.loop = (bool) pj.getProperty ("loop", true);
    np.s.metro = (bool) pj.getProperty ("metro", false);
    np.s.quant = (bool) pj.getProperty ("quant", false);
    np.s.count = (bool) pj.getProperty ("count", true);
    np.s.vel = (bool) pj.getProperty ("vel", true);
    np.s.choke = (bool) pj.getProperty ("choke", true);
    np.s.gate = (bool) pj.getProperty ("gate", false);
    np.s.keyPad.fill (0);
    if (auto* kp = pj.getProperty ("keyPad", {}).getArray())
        for (int k = 0; k < juce::jmin (128, kp->size()); ++k)
        {
            const int gi = (int) kp->getReference (k);
            np.s.keyPad[(size_t) k] = gi >= 0 && gi < NPADS ? gi + 1 : 0;
        }
    np.s.keyPads = (bool) pj.getProperty ("keyPads", false);
    np.s.autoNorm = (bool) pj.getProperty ("autoNorm", true);
    np.s.root = (int) pj.getProperty ("root", 60);
    np.s.mode = pj.getProperty ("mode", "keys").toString() == "chop" ? MODE_CHOP : MODE_KEYS;
    np.s.bpm = (float) juce::jlimit (40, 240, (int) std::round ((double) pj.getProperty ("bpm", 84.0)));
    np.s.sel = juce::jlimit (0, NPADS - 1, (int) pj.getProperty ("sel", 0));
    np.s.pat = juce::jlimit (0, NPAT - 1, (int) pj.getProperty ("pat", 0));
    const auto la = pj.getProperty ("la", juce::var());
    if (la.isObject())
    {
        np.la.on = (bool) la.getProperty ("on", true);
        np.la.pr = (float) juce::jlimit (0.0, 100.0, (double) la.getProperty ("pr", 0.0));
        np.la.gain = (float) juce::jlimit (0.0, 100.0, (double) la.getProperty ("gain", 50.0));
        np.la.limit = (bool) la.getProperty ("limit", false);
    }
    np.name = pj.getProperty ("name", "SESSION_01").toString();
    if (auto* a = pj.getProperty ("vstMaster", juce::var()).getArray())
        for (int k = 0; k < MASTER_VFX && k < a->size(); ++k) np.masterVfx[(size_t) k] = pluginSlotFromVar (a->getReference (k));
    if (auto* a = pj.getProperty ("returns", juce::var()).getArray())
        for (int r = 0; r < NSEND && r < a->size(); ++r)
        {
            const auto& so = a->getReference (r);
            np.retLevel[(size_t) r] = (float) juce::jlimit (0.0, 1.5, (double) so.getProperty ("level", 1.0));
            if (auto* f = so.getProperty ("fx", juce::var()).getArray())
                for (int k = 0; k < SEND_VFX && k < f->size(); ++k) np.sendVfx[(size_t) r][(size_t) k] = pluginSlotFromVar (f->getReference (k));
        }
    // web songs have one mode for the whole song: give it to the selected pad and to pads with their own key map
    for (int i = 0; i < NPADS; ++i)
    {
        auto& pd = np.pads[(size_t) i];
        if (pd.keyMode < 0) pd.keyMode = (np.s.mode == MODE_CHOP && (i == np.s.sel || pd.hasKmap)) ? MODE_CHOP : MODE_KEYS;
    }
    P = std::move (np);
    message = "Loaded song \"" + P.name + "\" - " + juce::String (nSamples) + " samples, " + juce::String (nLib) + " new in the library";
    return true;
}

// ------------------------------------------------------------------ library on disk
bool saveLibrary (const juce::File& dir, const std::vector<LibItem>& lib, const std::vector<juce::String>& crates)
{
    if (! dir.createDirectory()) return false;
    const auto cratesDir = dir.getChildFile ("crates");
    cratesDir.deleteRecursively();
    cratesDir.createDirectory();
    auto* root = new juce::DynamicObject();
    juce::var rv (root);
    { juce::Array<juce::var> a; for (auto& c : crates) a.add (c); root->setProperty ("crates", a); }
    juce::Array<juce::var> items;
    int n = 0;
    for (auto& it : lib)
    {
        if (! it.buf) continue;
        const auto crate = it.crate.isNotEmpty() ? safeName (it.crate) : juce::String ("_");
        const auto rel = "crates/" + crate + "/" + juce::String (++n).paddedLeft ('0', 3) + "_" + safeName (it.name) + ".wav";
        auto f = dir.getChildFile (rel);
        f.getParentDirectory().createDirectory();
        const auto mb = encodeWav (it.buf->audio, it.buf->sampleRate, 32);
        if (! f.replaceWithData (mb.getData(), mb.getSize())) continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", it.name); o->setProperty ("crate", it.crate); o->setProperty ("wav", rel);
        items.add (juce::var (o));
    }
    root->setProperty ("items", items);
    return dir.getChildFile ("library.json").replaceWithText (juce::JSON::toString (rv));
}

void loadLibrary (const juce::File& dir, std::vector<LibItem>& lib, std::vector<juce::String>& crates)
{
    const auto j = juce::JSON::parse (dir.getChildFile ("library.json"));
    if (! j.isObject()) return;
    if (auto* a = j.getProperty ("crates", juce::var()).getArray())
        for (auto& c : *a) if (std::find (crates.begin(), crates.end(), c.toString()) == crates.end()) crates.push_back (c.toString());
    int id = 1; for (auto& it : lib) id = juce::jmax (id, it.id + 1);
    if (auto* a = j.getProperty ("items", juce::var()).getArray())
        for (auto& o : *a)
        {
            auto b = decodeFile (dir.getChildFile (o.getProperty ("wav", "").toString()));
            if (! b) continue;
            lib.push_back ({ id++, o.getProperty ("name", "sample").toString(), o.getProperty ("crate", "").toString(), b });
        }
}

// ------------------------------------------------------------------ tempo map
std::vector<TempoChange> planTempos (const Project& p, const std::vector<int>& plan)
{
    std::vector<TempoChange> out; int step = 0;
    for (int pat : plan)
    {
        const float b = p.bpmOf (pat);
        if (out.empty() || out.back().bpm != b) out.push_back ({ step, b });
        step += p.plen[(size_t) pat];
    }
    return out;
}

juce::MemoryBlock tempoMapMid (const Project& p, const std::vector<int>& plan)
{
    const auto tp = planTempos (p, plan);
    const int PPQ = 480, TS = PPQ / 4;
    std::vector<juce::uint8> bytes;
    auto vl = [&bytes] (juce::uint32 n)
    {
        juce::uint8 tmp[5]; int k = 0;
        tmp[k++] = (juce::uint8) (n & 127);
        while (n >>= 7) tmp[k++] = (juce::uint8) ((n & 127) | 128);
        while (k > 0) bytes.push_back (tmp[--k]);
    };
    juce::uint32 last = 0;
    for (auto b : { 0, 0xFF, 0x58, 4, 4, 2, 24, 8 }) bytes.push_back ((juce::uint8) b);
    for (auto& x : tp)
    {
        const juce::uint32 tick = (juce::uint32) (x.step * TS), us = (juce::uint32) std::round (60000000.0 / x.bpm);
        vl (tick - last);
        for (auto b : { 0xFF, 0x51, 3 }) bytes.push_back ((juce::uint8) b);
        bytes.push_back ((juce::uint8) ((us >> 16) & 255)); bytes.push_back ((juce::uint8) ((us >> 8) & 255)); bytes.push_back ((juce::uint8) (us & 255));
        last = tick;
    }
    juce::uint32 tot = 0; for (int pat : plan) tot += (juce::uint32) p.plen[(size_t) pat];
    tot *= (juce::uint32) TS;
    vl (tot > last ? tot - last : 0);
    for (auto b : { 0xFF, 0x2F, 0 }) bytes.push_back ((juce::uint8) b);

    juce::MemoryOutputStream o;
    const juce::uint8 hdr[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, (juce::uint8) (PPQ >> 8), (juce::uint8) (PPQ & 255) };
    o.write (hdr, sizeof (hdr));
    o.write ("MTrk", 4);
    const auto L = (juce::uint32) bytes.size();
    const juce::uint8 len[] = { (juce::uint8) (L >> 24), (juce::uint8) (L >> 16), (juce::uint8) (L >> 8), (juce::uint8) L };
    o.write (len, 4);
    o.write (bytes.data(), bytes.size());
    return o.getMemoryBlock();
}

juce::String tempoMapTxt (const Project& p, const std::vector<int>& plan)
{
    juce::String s = "CONTRXL 25 tempo map\nBar 1 starts at 0:00. Every file from the same export starts together.\n\n";
    for (auto& x : planTempos (p, plan)) s << "Bar " << (x.step / 16 + 1) << ": " << x.bpm << " BPM\n";
    return s;
}
} // namespace cx::io
