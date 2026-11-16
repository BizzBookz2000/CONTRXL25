#include "ui/PluginScanPanel.h"

namespace cx::ui
{
namespace { enum Col { cTick = 1, cFormat, cName, cStatus, cWhere }; }

// ---------------------------------------------------------------- background scanning
class PluginScanPanel::ScanThread : public juce::Thread
{
public:
    ScanThread (PluginScanPanel& p, std::vector<int> todo, int timeoutMs)
        : juce::Thread ("CONTRXL25 plug-in scan"), owner (p), rowsToScan (std::move (todo)), timeout (timeoutMs) {}
    void run() override
    {
        int done = 0, good = 0, bad = 0;
        for (int r : rowsToScan)
        {
            if (threadShouldExit()) break;
            juce::String format, id, name;
            {
                const juce::ScopedLock sl (owner.lock);
                if (r < 0 || r >= (int) owner.rows.size()) continue;
                auto& row = owner.rows[(size_t) r];
                row.state = Row::scanning; row.detail = "Scanning...";
                format = row.format; id = row.id; name = row.name;
                owner.progress = "Scanning " + juce::String (done + 1) + " of " + juce::String ((int) rowsToScan.size()) + ": " + name;
            }
            owner.triggerAsyncUpdate();
            ScanOutcome o;
            juce::OwnedArray<juce::PluginDescription> found;
            if (auto* fmt = owner.ctl.plugins.formatNamed (format))
                o = owner.ctl.plugins.scanOne (*fmt, id, found, timeout, [this] { return threadShouldExit(); });
            else { o.status = ScanOutcome::nothingFound; o.detail = "Failed - unknown format"; }
            {
                const juce::ScopedLock sl (owner.lock);
                auto& row = owner.rows[(size_t) r];
                row.detail = o.detail;
                row.tick = false;
                if (o.status == ScanOutcome::ok)            { row.state = Row::ok; ++good; }
                else if (o.status == ScanOutcome::stopped)  { row.state = Row::notScanned; row.detail = "Not scanned (stopped)"; }
                else if (o.status == ScanOutcome::noHelper) { row.state = Row::failed; ++bad; }
                else                                        { row.state = Row::skipped; ++bad; }
            }
            ++done;
            owner.triggerAsyncUpdate();
        }
        {
            const juce::ScopedLock sl (owner.lock);
            for (auto& row : owner.rows) if (row.state == Row::queued) { row.state = Row::notScanned; row.detail = "Not scanned"; }
            owner.progress = (threadShouldExit() ? "Stopped. " : "Done. ") + juce::String (good) + " OK, " + juce::String (bad)
                           + " failed (failed plug-ins are skipped from now on - tick them and press Scan to retry).";
        }
        owner.triggerAsyncUpdate();
    }
private:
    PluginScanPanel& owner;
    std::vector<int> rowsToScan;
    int timeout;
};

// ---------------------------------------------------------------- panel
PluginScanPanel::PluginScanPanel (Controller& c, juce::PropertiesFile* p) : ctl (c), props (p)
{
    help.setText ("1. Find plug-ins (nothing is opened yet)   2. tick the ones you want (click a row)   3. Scan ticked.  "
                  "Failed plug-ins are skipped from then on; tick them and scan again to retry, or add one file by hand.",
                  juce::dontSendNotification);
    help.setFont (mono (11.0f)); help.setColour (juce::Label::textColourId, C::mute);
    help.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (help);

    formatBox.addItem ("All formats", 1);
    int id = 2;
    for (auto* f : ctl.plugins.allFormats()) formatBox.addItem (f->getName(), id++);
    formatBox.setSelectedId (1, juce::dontSendNotification);
    formatBox.onChange = [this] { findPlugins(); };
    for (int s : { 15, 30, 60, 120, 300 }) timeoutBox.addItem ("Give up after " + juce::String (s) + " s", s);
    timeoutBox.setSelectedId (props != nullptr ? props->getIntValue ("pluginScanTimeout", 60) : 60, juce::dontSendNotification);
    if (timeoutBox.getSelectedId() == 0) timeoutBox.setSelectedId (60, juce::dontSendNotification);
    timeoutBox.onChange = [this] { if (props) props->setValue ("pluginScanTimeout", timeoutBox.getSelectedId()); };
    timeoutBox.setTooltip ("How long one plug-in may take to open before it counts as failed");
    for (auto* cb : { &formatBox, &timeoutBox }) { cb->setWantsKeyboardFocus (false); addAndMakeVisible (cb); }

    find.onClick = [this] { findPlugins(); };
    addFile.onClick = [this] { chooseFile(); };
    tickNew.onClick = [this] { tickWhere ([] (const Row& r) { return r.state == Row::notScanned; }); };
    tickFailed.onClick = [this] { tickWhere ([] (const Row& r) { return r.state == Row::failed || r.state == Row::skipped; }); };
    tickAll.onClick = [this] { tickWhere ([] (const Row&) { return true; }); };
    tickNone.onClick = [this] { tickWhere ([] (const Row&) { return false; }); };
    scanBtn.onClick = [this] { scanTicked(); };
    stopBtn.onClick = [this] { stopScan(); };
    skipBtn.onClick = [this] { setSkipped (true); };
    unskipBtn.onClick = [this] { setSkipped (false); };
    scanBtn.setOnColour (C::ok); scanBtn.setOn (true);
    for (auto* b : { &find, &addFile, &tickNew, &tickFailed, &tickAll, &tickNone, &scanBtn, &stopBtn, &skipBtn, &unskipBtn })
        addAndMakeVisible (b);

    progressLabel.setFont (mono (11.5f, true)); progressLabel.setColour (juce::Label::textColourId, C::accent);
    addAndMakeVisible (progressLabel);

    auto& h = table.getHeader();
    h.addColumn ("", cTick, 28, 28, 28, juce::TableHeaderComponent::notResizableOrSortable);
    h.addColumn ("Format", cFormat, 80, 60, 120);
    h.addColumn ("Plug-in", cName, 220, 100, 600);
    h.addColumn ("Status", cStatus, 300, 120, 900);
    h.addColumn ("Where", cWhere, 300, 100, 2000);
    table.setColour (juce::ListBox::backgroundColourId, C::well);
    table.setColour (juce::ListBox::outlineColourId, C::line);
    table.setOutlineThickness (1);
    table.setRowHeight (22);
    table.setMultipleSelectionEnabled (false);
    table.setWantsKeyboardFocus (false);
    addAndMakeVisible (table);
    updateButtons();
    setSize (900, 600);
    findPlugins();
}

PluginScanPanel::~PluginScanPanel()
{
    cancelPendingUpdate();
    if (scanner) scanner->stopThread (15000);
}

void PluginScanPanel::paint (juce::Graphics& g) { g.fillAll (C::plate); }

void PluginScanPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    help.setBounds (r.removeFromTop (34));
    r.removeFromTop (4);
    auto row1 = r.removeFromTop (28);
    find.setBounds (row1.removeFromLeft (120)); row1.removeFromLeft (6);
    formatBox.setBounds (row1.removeFromLeft (130)); row1.removeFromLeft (6);
    addFile.setBounds (row1.removeFromLeft (170)); row1.removeFromLeft (6);
    timeoutBox.setBounds (row1.removeFromRight (170));
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (28);
    for (auto* b : { &tickNew, &tickFailed, &tickAll, &tickNone }) { b->setBounds (row2.removeFromLeft (92)); row2.removeFromLeft (4); }
    row2.removeFromLeft (8);
    scanBtn.setBounds (row2.removeFromLeft (110)); row2.removeFromLeft (4);
    stopBtn.setBounds (row2.removeFromLeft (64)); row2.removeFromLeft (12);
    unskipBtn.setBounds (row2.removeFromRight (110)); row2.removeFromRight (4);
    skipBtn.setBounds (row2.removeFromRight (96));
    r.removeFromTop (6);
    progressLabel.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);
    table.setBounds (r);
}

PluginScanPanel::Row PluginScanPanel::stateFromList (Row r) const
{
    if (ctl.plugins.isSkipped (r.id)) { r.state = Row::skipped; r.detail = "Skipped - failed a scan before (tick + Scan to retry)"; return r; }
    juce::StringArray names;
    bool inst = false;
    for (auto& k : ctl.plugins.known.getTypes())
        if (k.fileOrIdentifier == r.id) { names.add (k.name); inst = inst || k.isInstrument; }
    if (! names.isEmpty())
    {
        r.state = Row::ok;
        r.detail = "OK - " + names.joinIntoString (", ") + (inst ? "  [instrument]" : "  [effect]");
        if (r.name.isEmpty()) r.name = names[0];
    }
    else { r.state = Row::notScanned; r.detail = "Not scanned"; }
    return r;
}

void PluginScanPanel::findPlugins()
{
    if (scanner && scanner->isThreadRunning()) return;
    juce::MouseCursor::showWaitCursor();
    std::vector<Row> found;
    const int sel = formatBox.getSelectedId();
    int fi = 2;
    for (auto* f : ctl.plugins.allFormats())
    {
        const bool use = sel == 1 || sel == fi;
        ++fi;
        if (! use) continue;
        auto paths = f->getDefaultLocationsToSearch();
        if (props != nullptr)   // folders added in JUCE's scan dialog are remembered under this key
        {
            const juce::FileSearchPath extra (props->getValue ("lastPluginScanPath_" + f->getName()));
            for (int i = 0; i < extra.getNumPaths(); ++i) paths.addIfNotAlreadyThere (extra[i]);
        }
        // lists files / identifiers only - no plug-in code runs here
        for (auto& id : f->searchPathsForPlugins (paths, true, true))
        {
            Row r; r.format = f->getName(); r.id = id;
            r.name = f->getNameOfPluginFromIdentifier (id);
            if (r.name.isEmpty() || juce::File::isAbsolutePath (r.name))   // VST3 gives the path back: show the file name
                r.name = juce::File::isAbsolutePath (id) ? juce::File (id).getFileNameWithoutExtension() : id;
            found.push_back (stateFromList (r));
        }
    }
    // previously skipped plug-ins that live somewhere else
    for (auto& id : ctl.plugins.known.getBlacklistedFiles())
    {
        if (std::any_of (found.begin(), found.end(), [&] (const Row& r) { return r.id == id; })) continue;
        if (auto* f = ctl.plugins.formatForFile (id))
        {
            if (sel != 1 && formatBox.getText() != f->getName()) continue;
            Row r; r.format = f->getName(); r.id = id;
            r.name = juce::File::isAbsolutePath (id) ? juce::File (id).getFileNameWithoutExtension() : f->getNameOfPluginFromIdentifier (id);
            found.push_back (stateFromList (r));
        }
    }
    std::sort (found.begin(), found.end(), [] (const Row& a, const Row& b) { return a.name.compareNatural (b.name) < 0; });
    int nNew = 0, nOk = 0, nSkip = 0;
    for (auto& r : found) { nNew += r.state == Row::notScanned; nOk += r.state == Row::ok; nSkip += r.state == Row::skipped; }
    {
        const juce::ScopedLock sl (lock);
        rows = std::move (found);
        progress = juce::String ((int) rows.size()) + " plug-ins found: " + juce::String (nOk) + " OK, " + juce::String (nNew)
                 + " not scanned yet, " + juce::String (nSkip) + " skipped. Tick the ones to scan.";
    }
    juce::MouseCursor::hideWaitCursor();
    handleAsyncUpdate();
}

int PluginScanPanel::addOrFindRow (const juce::String& format, const juce::String& id)
{
    const juce::ScopedLock sl (lock);
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].id == id) return (int) i;
    Row r; r.format = format; r.id = id;
    r.name = juce::File::isAbsolutePath (id) ? juce::File (id).getFileNameWithoutExtension() : id;
    rows.insert (rows.begin(), stateFromList (r));
    return 0;
}

void PluginScanPanel::addFileAndScan (const juce::String& path)
{
    auto* f = ctl.plugins.formatForFile (path);
    if (f == nullptr)
    {
        const juce::ScopedLock sl (lock);
        progress = juce::File (path).getFileName() + " is not a plug-in this app can open (" CX_PLUGIN_KINDS ").";
        triggerAsyncUpdate();
        return;
    }
    const int r = addOrFindRow (f->getName(), path);
    {
        const juce::ScopedLock sl (lock);
        for (auto& row : rows) row.tick = false;
        rows[(size_t) r].tick = true;
    }
    table.updateContent();
    table.scrollToEnsureRowIsOnscreen (r);
    scanTicked();
}

void PluginScanPanel::chooseFile()
{
   #if JUCE_MAC
    const juce::String patterns = "*.vst3;*.component";
   #else
    const juce::String patterns = "*.vst3";
   #endif
    auto start = juce::File (props != nullptr ? props->getValue ("pluginAddFileDir") : juce::String());
    if (! start.isDirectory())
    {
        const auto defaults = ctl.plugins.allFormats().isEmpty() ? juce::FileSearchPath() : ctl.plugins.allFormats()[0]->getDefaultLocationsToSearch();
        start = defaults.getNumPaths() > 0 ? defaults[0] : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }
    chooser = std::make_unique<juce::FileChooser> ("Choose a plug-in to scan", start, patterns);
    // plug-ins are often "bundles" (folders that look like files), so folders can be picked too
    const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::canSelectDirectories;
    juce::Component::SafePointer<PluginScanPanel> sp (this);
    chooser->launchAsync (flags, [sp] (const juce::FileChooser& fc)
    {
        if (sp == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        if (sp->props) sp->props->setValue ("pluginAddFileDir", f.getParentDirectory().getFullPathName());
        sp->addFileAndScan (f.getFullPathName());
    });
}

void PluginScanPanel::tickWhere (std::function<bool (const Row&)> pred)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& r : rows) r.tick = pred (r);
    }
    handleAsyncUpdate();
}

void PluginScanPanel::scanTicked()
{
    if (scanner && scanner->isThreadRunning()) return;
    std::vector<int> todo;
    {
        const juce::ScopedLock sl (lock);
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].tick) { todo.push_back ((int) i); rows[i].state = Row::queued; rows[i].detail = "Waiting..."; }
        if (todo.empty()) progress = "Tick at least one plug-in first (or use Tick new).";
    }
    if (! todo.empty())
    {
        if (! PluginScanner::helperAvailable())
        {
            const juce::ScopedLock sl (lock);
            progress = "Warning: the scanner helper is missing, so plug-ins are opened inside the app (unsafe). Rebuild to fix.";
        }
        scanner = std::make_unique<ScanThread> (*this, std::move (todo), timeoutBox.getSelectedId() * 1000);
        scanner->startThread();
    }
    handleAsyncUpdate();
}

void PluginScanPanel::stopScan()
{
    if (scanner) scanner->signalThreadShouldExit();
}

void PluginScanPanel::setSkipped (bool skip)
{
    if (scanner && scanner->isThreadRunning()) return;
    int n = 0;
    {
        const juce::ScopedLock sl (lock);
        for (auto& r : rows)
        {
            if (! r.tick) continue;
            if (skip) ctl.plugins.known.addToBlacklist (r.id); else ctl.plugins.known.removeFromBlacklist (r.id);
            r = stateFromList (r);
            r.tick = false;
            ++n;
        }
        progress = n == 0 ? juce::String ("Tick some plug-ins first.")
                          : juce::String (n) + (skip ? " plug-ins will be skipped." : " plug-ins can be scanned again.");
    }
    handleAsyncUpdate();
}

void PluginScanPanel::updateButtons()
{
    const bool busy = scanner && scanner->isThreadRunning();
    for (auto* b : { &find, &addFile, &tickNew, &tickFailed, &tickAll, &tickNone, &scanBtn, &skipBtn, &unskipBtn })
        b->setEnabled (! busy);
    formatBox.setEnabled (! busy);
    stopBtn.setEnabled (busy);
}

void PluginScanPanel::handleAsyncUpdate()
{
    {
        const juce::ScopedLock sl (lock);
        progressLabel.setText (progress, juce::dontSendNotification);
    }
    table.updateContent();
    table.repaint();
    updateButtons();
}

// ---------------------------------------------------------------- table
int PluginScanPanel::getNumRows()
{
    const juce::ScopedLock sl (lock);
    return (int) rows.size();
}

void PluginScanPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? C::plate2.brighter (0.1f) : (row % 2 ? C::well : C::plate.darker (0.2f)));
}

void PluginScanPanel::paintCell (juce::Graphics& g, int rowNum, int col, int w, int h, bool)
{
    Row r;
    {
        const juce::ScopedLock sl (lock);
        if (rowNum < 0 || rowNum >= (int) rows.size()) return;
        r = rows[(size_t) rowNum];
    }
    if (col == cTick)
    {
        auto b = juce::Rectangle<float> (0, 0, 14, 14).withCentre ({ w * 0.5f, h * 0.5f });
        g.setColour (r.tick ? C::accent : C::line2);
        if (r.tick) g.fillRoundedRectangle (b, 3.0f); else g.drawRoundedRectangle (b, 3.0f, 1.2f);
        if (r.tick) { g.setColour (juce::Colours::black); g.drawText (juce::String::charToString (0x2713), b, juce::Justification::centred); }
        return;
    }
    juce::String text; juce::Colour colour = C::fg;
    switch (col)
    {
        case cFormat: text = r.format; colour = C::mute; break;
        case cName:   text = r.name; break;
        case cWhere:  text = r.id; colour = C::mute; break;
        case cStatus:
            text = r.detail;
            colour = r.state == Row::ok ? C::ok : (r.state == Row::failed || r.state == Row::skipped) ? C::rec
                   : (r.state == Row::scanning || r.state == Row::queued) ? C::accent : C::mute;
            break;
        default: break;
    }
    g.setColour (colour);
    g.setFont (mono (11.5f, col == cName));
    g.drawText (text, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
}

void PluginScanPanel::cellClicked (int row, int, const juce::MouseEvent&)
{
    if (scanner && scanner->isThreadRunning()) return;
    {
        const juce::ScopedLock sl (lock);
        if (row < 0 || row >= (int) rows.size()) return;
        rows[(size_t) row].tick = ! rows[(size_t) row].tick;   // a click anywhere on the row ticks it
    }
    table.repaintRow (row);
}

void PluginScanPanel::sortOrderChanged (int col, bool forwards)
{
    if (scanner && scanner->isThreadRunning()) return;
    {
        const juce::ScopedLock sl (lock);
        std::stable_sort (rows.begin(), rows.end(), [col, forwards] (const Row& a, const Row& b)
        {
            int c = 0;
            if (col == cFormat) c = a.format.compare (b.format);
            else if (col == cStatus) c = (int) a.state - (int) b.state;
            else if (col == cWhere) c = a.id.compareNatural (b.id);
            else c = a.name.compareNatural (b.name);
            return forwards ? c < 0 : c > 0;
        });
    }
    table.updateContent(); table.repaint();
}

juce::String PluginScanPanel::getCellTooltip (int row, int)
{
    const juce::ScopedLock sl (lock);
    if (row < 0 || row >= (int) rows.size()) return {};
    return rows[(size_t) row].name + "\n" + rows[(size_t) row].detail + "\n" + rows[(size_t) row].id;
}
} // namespace cx::ui
