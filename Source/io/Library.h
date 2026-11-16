// CONTRXL 25 - the sample library is a folder on disk.
//   * Every folder (at any depth) is a crate: "Soul Samples/Artist/Kicks".
//   * Audio files you copy into those folders with Explorer show up after a refresh (automatic
//     every few seconds while the library is on screen, or the Refresh button).
//   * Samples added from inside the app (pad > library, chops, bounces, song imports) are kept in
//     memory until you press Save (or close the app), then written as .wav into their crate folder.
//   * Audio is loaded when it is needed (preview, load onto a pad), so a big library starts fast.
#pragma once
#include "model/Model.h"

namespace cx
{
class Library
{
public:
    struct Item
    {
        int id = 0;
        juce::String name, crate;      // crate = folder path relative to the root, "/" separated ("" = the root)
        juce::File file;               // where it lives on disk (empty until saved)
        SamplePtr buf;                 // loaded on demand
        juce::int64 bytes = 0;
        bool dirty = false;            // not written to disk yet
        double seconds() const { return buf ? buf->duration() : -1.0; }
    };

    std::vector<Item> items;
    juce::StringArray crates;          // every folder, sorted ("Drums", "Soul Samples", "Soul Samples/Artist", ...)

    static juce::File defaultRoot();
    const juce::File& getRoot() const { return root; }
    void setRoot (const juce::File& dir);              // creates it (with a few starter crates if it is new) and scans

    // re-read the folder tree: new files appear, deleted files disappear. Unsaved items are kept.
    // Returns true if anything changed.
    bool scan();
    // write unsaved items into their crate folders (and move items whose crate changed). Returns the number written.
    int save (juce::String* error = nullptr);
    int unsavedCount() const;

    SamplePtr load (int index);                        // decodes the file the first time
    int add (const juce::String& name, const juce::String& crate, SamplePtr buf);   // unsaved until save()
    bool remove (int index);                           // the file goes to the recycle bin
    int find (int id) const;

    bool addCrate (const juce::String& path);
    bool renameCrate (const juce::String& path, const juce::String& newLastName);
    bool deleteCrate (const juce::String& path);       // its samples move up to the parent crate
    int  importFiles (const juce::Array<juce::File>& files, const juce::String& crate);   // copies into the crate folder
    bool backupTo (const juce::File& zip);             // saves first, then zips the whole folder tree
    int  restoreFolderZip (const juce::File& zip);     // unzips into the root (keeps existing files), then scans

    static bool isAudioFile (const juce::File& f);
    static bool inCrate (const juce::String& itemCrate, const juce::String& crate);   // crate itself or one of its sub-crates
    static juce::String crateName (const juce::String& path) { return path.fromLastOccurrenceOf ("/", false, false); }
    static int crateDepth (const juce::String& path) { return path.isEmpty() ? 0 : path.retainCharacters ("/").length(); }

private:
    juce::File root;
    int nextId = 1;
    juce::File dirFor (const juce::String& crate) const { return crate.isEmpty() ? root : root.getChildFile (crate); }
    juce::String relDir (const juce::File& f) const;   // crate path of a file's folder
    void migrateOldLayout();
};
} // namespace cx
