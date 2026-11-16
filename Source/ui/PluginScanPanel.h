// CONTRXL 25 - "Choose & scan": list every plug-in file / Audio Unit on this computer, tick the ones
// you want and scan just those. Each plug-in is opened in the scanner helper, so one that crashes or
// hangs is marked "Failed" and skipped, and the rest carry on.
#pragma once
#include "ui/Widgets.h"

namespace cx::ui
{
class PluginScanPanel : public juce::Component,
                        private juce::TableListBoxModel,
                        private juce::AsyncUpdater
{
public:
    PluginScanPanel (Controller& c, juce::PropertiesFile* props);
    ~PluginScanPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void findPlugins();                              // fill the list (does not open any plug-in)
    void addFileAndScan (const juce::String& path);  // a single .vst3 / .component chosen by hand

private:
    struct Row
    {
        enum State { notScanned, queued, scanning, ok, failed, skipped };
        juce::String format, id, name;
        State state = notScanned;
        juce::String detail;
        bool tick = false;
    };

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void sortOrderChanged (int col, bool forwards) override;
    juce::String getCellTooltip (int row, int col) override;

    void handleAsyncUpdate() override;               // refresh the table + labels from the scan thread
    Row stateFromList (Row r) const;                 // OK / skipped / not scanned, from the known list
    void tickWhere (std::function<bool (const Row&)> pred);
    void scanTicked();
    void stopScan();
    void setSkipped (bool skip);
    void chooseFile();
    void updateButtons();
    int addOrFindRow (const juce::String& format, const juce::String& id);

    class ScanThread;
    Controller& ctl;
    juce::PropertiesFile* props;
    juce::CriticalSection lock;                      // guards rows (scan thread writes states)
    std::vector<Row> rows;
    juce::String progress;
    std::unique_ptr<ScanThread> scanner;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TableListBox table { "plugins", this };
    juce::Label help, progressLabel;
    juce::ComboBox formatBox, timeoutBox;
    Btn find { "Find plug-ins", "List the plug-ins installed on this computer (nothing is opened yet)" },
        addFile { "Add a plug-in file...", "Pick one .vst3 (or, on a Mac, .component) and scan only that one" },
        tickNew { "Tick new", "Tick every plug-in that was never scanned" },
        tickFailed { "Tick failed", "Tick the plug-ins that failed or are skipped, to try them again" },
        tickAll { "Tick all" }, tickNone { "Untick all" },
        scanBtn { "Scan ticked", "Open the ticked plug-ins one by one in the safe scanner" },
        stopBtn { "Stop" },
        skipBtn { "Skip ticked", "Never scan or list the ticked plug-ins" },
        unskipBtn { "Un-skip ticked", "Allow the ticked plug-ins to be scanned again" };
};
} // namespace cx::ui
