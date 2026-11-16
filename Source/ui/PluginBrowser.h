// CONTRXL 25 - search your VST3 plug-ins by name, maker or type and load one into a slot
#pragma once
#include "ui/Widgets.h"

namespace cx::ui
{
class PluginBrowser : public juce::Component, private juce::ListBoxModel, private juce::KeyListener
{
public:
    PluginBrowser (Controller& c, SlotId s);
    ~PluginBrowser() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
private:
    Controller& ctl;
    SlotId slot;
    juce::TextEditor search;
    Btn bInst { "Instruments" }, bFx { "Effects" }, bScan { "Scan for VST3 plug-ins...", "Find new or updated plug-ins" }, bLoad { "Load" };
    juce::ListBox list { "plugins", this };
    juce::Label info;
    juce::Array<juce::PluginDescription> rows;
    int recentCount = 0;
    bool showInst = true;

    void rebuild();
    void choose (int row);
    int getNumRows() override { return rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { choose (row); }
    void returnKeyPressed (int row) override { choose (row); }
    void selectedRowsChanged (int) override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
};
} // namespace cx::ui
