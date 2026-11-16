#include "Library.h"
#include "io/SongIO.h"
#include <set>

namespace cx
{
static juce::String legalPath (const juce::String& path)
{
    juce::StringArray parts;
    for (auto& p : juce::StringArray::fromTokens (path.replaceCharacter ('\\', '/'), "/", ""))
    {
        const auto t = juce::File::createLegalFileName (p.trim());
        if (t.isNotEmpty() && t != "." && t != "..") parts.add (t);
    }
    return parts.joinIntoString ("/");
}

juce::File Library::defaultRoot()
{
   #if CX_MOBILE
    return userFilesFolder().getChildFile ("Library");   // iPad: Files app > On My iPad > CONTRXL 25 > Library
   #endif
    return appDataDir().getChildFile ("Library");
}

bool Library::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;wave;aif;aiff;flac;ogg;mp3;m4a;wma");
}

bool Library::inCrate (const juce::String& itemCrate, const juce::String& crate)
{
    return crate.isEmpty() || itemCrate == crate || itemCrate.startsWith (crate + "/");
}

juce::String Library::relDir (const juce::File& f) const
{
    const auto dir = f.getParentDirectory();
    if (dir == root) return {};
    auto r = dir.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    return r == "." ? juce::String() : r;
}

void Library::setRoot (const juce::File& dir)
{
    root = dir;
    const bool fresh = ! root.exists();
    root.createDirectory();
    migrateOldLayout();
    if (fresh)
        for (auto* c : { "Drums", "Bass", "Vocals", "Loops", "FX" }) root.getChildFile (c).createDirectory();
    // keep unsaved items (e.g. when switching folders after a save failed), drop the rest
    items.erase (std::remove_if (items.begin(), items.end(), [] (const Item& it) { return ! it.dirty; }), items.end());
    crates.clear();
    scan();
}

bool Library::scan()
{
    if (! root.isDirectory()) return false;
    bool changed = false;
    juce::StringArray found;
    std::set<juce::String> seen;
    std::set<juce::String> known;
    for (auto& it : items) if (it.file != juce::File()) known.insert (it.file.getFullPathName());
    for (const auto& e : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
    {
        const auto f = e.getFile();
        if (f.getFileName().startsWithChar ('.') || f.getFileName().startsWithChar ('~')) continue;
        if (e.isDirectory()) { found.add (f.getRelativePathFrom (root).replaceCharacter ('\\', '/')); continue; }
        if (! isAudioFile (f)) continue;
        const auto path = f.getFullPathName();
        seen.insert (path);
        if (known.count (path)) continue;
        Item it;
        it.id = nextId++; it.name = f.getFileNameWithoutExtension(); it.crate = relDir (f); it.file = f; it.bytes = e.getFileSize();
        items.push_back (std::move (it));
        changed = true;
    }
    const auto before = items.size();
    items.erase (std::remove_if (items.begin(), items.end(), [&seen] (const Item& it)
    {
        return ! it.dirty && it.file != juce::File() && seen.count (it.file.getFullPathName()) == 0;
    }), items.end());
    changed |= items.size() != before;
    for (auto& it : items) if (it.crate.isNotEmpty() && ! found.contains (it.crate)) found.add (it.crate);   // unsaved items' crates
    found.sortNatural();
    if (found != crates) { crates = found; changed = true; }
    return changed;
}

int Library::unsavedCount() const
{
    int n = 0;
    for (auto& it : items) if (it.dirty) ++n;
    return n;
}

int Library::save (juce::String* error)
{
    int n = 0;
    root.createDirectory();
    for (auto& c : crates) dirFor (c).createDirectory();
    for (auto& it : items)
    {
        const auto dir = dirFor (it.crate);
        if (it.dirty || it.file == juce::File() || ! it.file.existsAsFile())
        {
            if (! it.buf) continue;
            dir.createDirectory();
            auto f = dir.getNonexistentChildFile (juce::File::createLegalFileName (it.name.isNotEmpty() ? it.name : juce::String ("sample")), ".wav", false);
            if (io::writeWav (f, it.buf->audio, it.buf->sampleRate, 24))
            {
                it.file = f; it.dirty = false; it.bytes = f.getSize(); ++n;
            }
            else if (error) *error = "could not write " + f.getFullPathName();
        }
        else if (relDir (it.file) != it.crate)
        {
            dir.createDirectory();
            auto f = dir.getNonexistentChildFile (it.file.getFileNameWithoutExtension(), it.file.getFileExtension(), false);
            if (it.file.moveFileTo (f)) it.file = f;
        }
    }
    return n;
}

SamplePtr Library::load (int index)
{
    if (index < 0 || index >= (int) items.size()) return nullptr;
    auto& it = items[(size_t) index];
    if (! it.buf && it.file.existsAsFile()) it.buf = io::decodeFile (it.file);
    return it.buf;
}

int Library::add (const juce::String& name, const juce::String& crate, SamplePtr buf)
{
    Item it;
    it.id = nextId++; it.name = name; it.crate = legalPath (crate); it.buf = std::move (buf); it.dirty = true;
    items.push_back (std::move (it));
    if (items.back().crate.isNotEmpty() && ! crates.contains (items.back().crate)) { crates.add (items.back().crate); crates.sortNatural(); }
    return items.back().id;
}

int Library::find (int id) const
{
    for (int i = 0; i < (int) items.size(); ++i) if (items[(size_t) i].id == id) return i;
    return -1;
}

bool Library::remove (int index)
{
    if (index < 0 || index >= (int) items.size()) return false;
    auto& it = items[(size_t) index];
    if (it.file.existsAsFile() && ! it.file.moveToTrash()) return false;
    items.erase (items.begin() + index);
    return true;
}

bool Library::addCrate (const juce::String& path)
{
    const auto p = legalPath (path);
    if (p.isEmpty()) return false;
    const bool ok = dirFor (p).createDirectory().wasOk();
    scan();
    return ok;
}

bool Library::renameCrate (const juce::String& path, const juce::String& newLastName)
{
    const auto nn = juce::File::createLegalFileName (newLastName.trim());
    if (path.isEmpty() || nn.isEmpty()) return false;
    const auto parent = path.upToLastOccurrenceOf ("/", false, false);
    const auto newPath = path.containsChar ('/') ? parent + "/" + nn : nn;
    if (newPath == path) return true;
    const auto src = dirFor (path), dst = dirFor (newPath);
    if (dst.exists()) return false;
    if (src.isDirectory() && ! src.moveFileTo (dst)) return false;
    for (auto& it : items)
        if (it.crate == path || it.crate.startsWith (path + "/"))
        {
            const auto rest = it.crate.substring (path.length());
            it.crate = newPath + rest;
            if (it.file != juce::File()) it.file = dirFor (it.crate).getChildFile (it.file.getFileName());
        }
    scan();
    return true;
}

bool Library::deleteCrate (const juce::String& path)
{
    if (path.isEmpty()) return false;
    const auto parent = path.containsChar ('/') ? path.upToLastOccurrenceOf ("/", false, false) : juce::String();
    const auto src = dirFor (path), dst = dirFor (parent);
    if (src.isDirectory())
    {
        // everything inside (samples and sub-crates) moves up one level
        for (const auto& e : juce::RangedDirectoryIterator (src, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto f = e.getFile();
            auto target = dst.getChildFile (f.getFileName());
            if (target.exists()) target = dst.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false);
            f.moveFileTo (target);
        }
        if (! src.deleteRecursively()) return false;
    }
    for (auto& it : items)
        if (it.dirty && (it.crate == path || it.crate.startsWith (path + "/")))
        {
            const auto rest = it.crate.substring (path.length());   // "" or "/Sub"
            it.crate = parent.isEmpty() ? rest.trimCharactersAtStart ("/") : parent + rest;
        }
    // files moved on disk: forget their old paths and read them again
    items.erase (std::remove_if (items.begin(), items.end(), [] (const Item& i) { return ! i.dirty; }), items.end());
    crates.clear();
    scan();
    return true;
}

int Library::importFiles (const juce::Array<juce::File>& files, const juce::String& crate)
{
    const auto dest = dirFor (legalPath (crate));
    dest.createDirectory();
    int n = 0;
    for (auto& f : files)
    {
        if (f.isDirectory())
        {
            // a folder comes in as a crate (with its sub-folders as sub-crates)
            const auto d = dest.getChildFile (f.getFileName());
            for (const auto& e : juce::RangedDirectoryIterator (f, true, "*", juce::File::findFiles))
            {
                const auto src = e.getFile();
                if (! isAudioFile (src)) continue;
                const auto target = d.getChildFile (src.getRelativePathFrom (f));
                if (target.existsAsFile() && target.getSize() == src.getSize()) continue;
                target.getParentDirectory().createDirectory();
                if (src.copyFileTo (target)) ++n;
            }
            continue;
        }
        if (! isAudioFile (f)) continue;
        auto target = dest.getChildFile (f.getFileName());
        if (target.existsAsFile() && target.getSize() == f.getSize()) continue;   // already there
        if (target.exists()) target = dest.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false);
        if (f.copyFileTo (target)) ++n;
    }
    scan();
    return n;
}

bool Library::backupTo (const juce::File& zip)
{
    save();
    juce::ZipFile::Builder zb;
    int n = 0;
    for (const auto& e : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
    {
        const auto f = e.getFile();
        if (! isAudioFile (f)) continue;
        zb.addFile (f, 0, f.getRelativePathFrom (root).replaceCharacter ('\\', '/'));
        ++n;
    }
    if (n == 0) return false;
    juce::TemporaryFile tmp (zip);
    bool ok = false;
    { juce::FileOutputStream os (tmp.getFile()); ok = os.openedOk() && zb.writeToStream (os, nullptr); }
    return ok && tmp.overwriteTargetFileWithTemporary();
}

int Library::restoreFolderZip (const juce::File& zip)
{
    const auto before = items.size();
    juce::ZipFile zf (zip);
    if (zf.getNumEntries() == 0) return 0;
    zf.uncompressTo (root, false);   // never overwrites what you already have
    migrateOldLayout();               // backups from older versions
    scan();
    return (int) items.size() - (int) before;
}

// versions before 0.74 kept <root>/library.json + <root>/crates/<crate>/001_name.wav: move them into plain folders
void Library::migrateOldLayout()
{
    const auto json = root.getChildFile ("library.json");
    if (! json.existsAsFile()) return;
    const auto j = juce::JSON::parse (json);
    if (auto* cr = j.getProperty ("crates", juce::var()).getArray())
        for (auto& c : *cr) { const auto p = legalPath (c.toString()); if (p.isNotEmpty()) dirFor (p).createDirectory(); }
    if (auto* a = j.getProperty ("items", juce::var()).getArray())
        for (auto& o : *a)
        {
            const auto f = root.getChildFile (o.getProperty ("wav", "").toString());
            if (! f.existsAsFile()) continue;
            const auto dir = dirFor (legalPath (o.getProperty ("crate", "").toString()));
            dir.createDirectory();
            const auto name = juce::File::createLegalFileName (o.getProperty ("name", f.getFileNameWithoutExtension()).toString());
            f.moveFileTo (dir.getNonexistentChildFile (name.isNotEmpty() ? name : juce::String ("sample"), f.getFileExtension(), false));
        }
    json.moveFileTo (root.getNonexistentChildFile ("library_before_0.74", ".json", false));
    // remove the old, now empty folders
    for (auto* old : { "crates", "unsorted" })
    {
        const auto d = root.getChildFile (old);
        if (! d.isDirectory()) continue;
        bool hasFiles = false;
        for (const auto& e : juce::RangedDirectoryIterator (d, true, "*", juce::File::findFiles)) { juce::ignoreUnused (e); hasFiles = true; break; }
        if (! hasFiles) d.deleteRecursively();
    }
}
} // namespace cx
