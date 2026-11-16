#include "PluginBrowser.h"

namespace cx::ui
{
void showPluginBrowser (Controller& ctl, const SlotId& s, juce::Component* target)
{
    if (target == nullptr) return;
    juce::CallOutBox::launchAsynchronously (std::make_unique<PluginBrowser> (ctl, s), target->getScreenBounds(), nullptr);
}

PluginBrowser::PluginBrowser (Controller& c, SlotId s) : ctl (c), slot (s)
{
    showInst = slot.kind == SlotId::Inst;
    search.setTextToShowWhenEmpty ("Search plug-ins: name, maker or type (synth, reverb, drum...)", C::mute);
    search.setFont (mono (13.0f));
    search.onTextChange = [this] { rebuild(); };
    search.onReturnKey = [this] { choose (juce::jmax (0, list.getSelectedRow())); };
    search.onEscapeKey = [this] { if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss(); };
    search.addKeyListener (this);
    addAndMakeVisible (search);

    bInst.onClick = [this] { showInst = true; rebuild(); };
    bFx.onClick = [this] { showInst = false; rebuild(); };
    // an instrument slot only takes instruments; insert slots only take effects
    bInst.setEnabled (slot.kind == SlotId::Inst);
    bFx.setEnabled (slot.kind != SlotId::Inst);
    bScan.onClick = [this]
    {
        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss();
        if (ctl.onShowPluginList) ctl.onShowPluginList();
    };
    bLoad.onClick = [this] { choose (list.getSelectedRow()); };
    bLoad.setOnColour (C::ok);
    for (auto* b : { &bInst, &bFx, &bScan, &bLoad }) addAndMakeVisible (b);

    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, C::well);
    list.setColour (juce::ListBox::outlineColourId, C::line);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);
    info.setFont (mono (10.5f)); info.setColour (juce::Label::textColourId, C::mute);
    addAndMakeVisible (info);
    setSize (560, 470);
    rebuild();
}

PluginBrowser::~PluginBrowser() { search.removeKeyListener (this); }

void PluginBrowser::visibilityChanged()
{
    if (isShowing())
    {
        juce::Component::SafePointer<juce::TextEditor> sp (&search);
        juce::MessageManager::callAsync ([sp] { if (sp) sp->grabKeyboardFocus(); });
    }
}

void PluginBrowser::rebuild()
{
    rows.clear(); recentCount = 0;
    bInst.setOn (showInst); bFx.setOn (! showInst);
    const auto words = juce::StringArray::fromTokens (search.getText().trim(), " ", "");
    auto matches = [&words] (const juce::PluginDescription& d)
    {
        const auto hay = d.name + " " + d.manufacturerName + " " + d.category + " " + d.descriptiveName + " " + d.pluginFormatName;
        for (auto& w : words) if (! hay.containsIgnoreCase (w)) return false;
        return true;
    };
    auto all = ctl.plugins.list (showInst);
    // recently used first, then everything else by name
    for (auto& id : ctl.recentPlugins)
        for (auto& d : all)
            if (d.createIdentifierString() == id && matches (d)) { rows.add (d); ++recentCount; break; }
    std::sort (all.begin(), all.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b) { return a.name.compareNatural (b.name) < 0; });
    for (auto& d : all)
        if (matches (d) && ! ctl.recentPlugins.contains (d.createIdentifierString())) rows.add (d);
    list.updateContent();
    list.selectRow (rows.isEmpty() ? -1 : 0);
    list.repaint();
    const int total = ctl.plugins.list (showInst).size();
    if (ctl.plugins.known.getNumTypes() > 0 && total == 0)
        info.setText ("No " + pluginKind() + (showInst ? " instruments" : " effects") + " found yet. Press Scan" + (CX_MOBILE ? juce::String (".") : juce::String (", or drop a .vst3 file on the slot.")), juce::dontSendNotification);
    else if (ctl.plugins.known.getNumTypes() == 0)
        info.setText (CX_MOBILE ? "No plug-ins found yet. Press Scan to find the AUv3 plug-ins installed on this iPad (install them from the App Store first)."
                                : "No plug-ins found yet. Press Scan to look for VST3 plug-ins on this PC (or drop a .vst3 file on a slot).", juce::dontSendNotification);
    else
        info.setText (juce::String (rows.size()) + " of " + juce::String (total) + (showInst ? " instruments" : " effects")
                      + " - Enter or double-click loads into " + slot.label() + ". Esc closes.", juce::dontSendNotification);
    bLoad.setEnabled (! rows.isEmpty());
}

void PluginBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= rows.size()) return;
    const auto& d = rows.getReference (row);
    g.fillAll (selected ? C::accent : (row % 2 ? C::plate2 : C::well));
    if (row == recentCount && recentCount > 0) { g.setColour (C::line); g.drawHorizontalLine (0, 0, (float) w); }
    g.setColour (selected ? juce::Colours::black : C::fg);
    g.setFont (mono (12.5f, true));
    g.drawText (d.name, 10, 2, w - 120, h / 2, juce::Justification::centredLeft);
    g.setColour (selected ? juce::Colours::black.withAlpha (0.7f) : C::mute);
    g.setFont (mono (10.0f));
    g.drawText (d.manufacturerName + (d.category.isNotEmpty() ? "  -  " + d.category.replace ("|", " / ") : juce::String()),
                10, h / 2, w - 120, h / 2 - 2, juce::Justification::centredLeft);
    if (row < recentCount) g.drawText ("recent", w - 110, 0, 100, h, juce::Justification::centredRight);
}

void PluginBrowser::selectedRowsChanged (int) { bLoad.setEnabled (list.getSelectedRow() >= 0); }

bool PluginBrowser::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    // arrow keys move through the list while you type in the search box
    if (k == juce::KeyPress::downKey) { list.selectRow (juce::jmin (rows.size() - 1, list.getSelectedRow() + 1)); list.scrollToEnsureRowIsOnscreen (list.getSelectedRow()); return true; }
    if (k == juce::KeyPress::upKey)   { list.selectRow (juce::jmax (0, list.getSelectedRow() - 1)); list.scrollToEnsureRowIsOnscreen (list.getSelectedRow()); return true; }
    return false;
}

void PluginBrowser::choose (int row)
{
    if (row < 0 || row >= rows.size()) return;
    const auto d = rows[row];
    auto& c = ctl; const auto s = slot;
    if (auto* cb = findParentComponentOfClass<juce::CallOutBox>()) cb->dismiss();
    // load after the box has closed (loading a big plug-in can take a moment)
    juce::MessageManager::callAsync ([&c, s, d] { if (c.setPlugin (s, d) && c.onOpenEditor) c.onOpenEditor (s); });
}

void PluginBrowser::paint (juce::Graphics& g) { g.fillAll (C::plate); }

void PluginBrowser::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (30);
    bInst.setBounds (top.removeFromLeft (110)); top.removeFromLeft (4);
    bFx.setBounds (top.removeFromLeft (90)); top.removeFromLeft (8);
    bScan.setBounds (top.removeFromRight (190));
    r.removeFromTop (8);
    search.setBounds (r.removeFromTop (30)); r.removeFromTop (8);
    auto bottom = r.removeFromBottom (30);
    bLoad.setBounds (bottom.removeFromRight (100)); bottom.removeFromRight (8);
    info.setBounds (bottom);
    r.removeFromBottom (8);
    list.setBounds (r);
}
} // namespace cx::ui
