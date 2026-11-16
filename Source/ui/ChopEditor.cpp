#include "ChopEditor.h"

namespace cx::ui
{
ChopEditor::ChopEditor (Controller& c, Dialogs& d) : Panel (c, d)
{
    wave.manual = true;
    wave.onLoadRequest = [this] { const int gi = ctl.P.s.sel; dlg.openAudio (true, [this, gi] (juce::Array<juce::File> f) { ctl.loadFiles (f, gi); }); };
    title.setFont (mono (15.0f, true)); title.setColour (juce::Label::textColourId, C::accent);
    hint.setFont (mono (11.0f)); hint.setColour (juce::Label::textColourId, C::mute);
    bEdit.onClick = [this] { wave.manual = ! wave.manual; refresh(); };
    bPlay.onClick = [this] { ctl.previewChop (ctl.selPad().markers.empty() ? -1 : ctl.selPad().sel); };
    bPrev.onClick = [this] { ctl.stepRegion (-1); };
    bNext.onClick = [this] { ctl.stepRegion (1); };
    bZoomOut.onClick = [this] { wave.zoomBy (1.0f / 1.5f); };
    bZoomIn.onClick = [this] { wave.zoomBy (1.5f); };
    bFit.onClick = [this] { wave.resetZoom(); };
    for (int n : { 2, 4, 8, 16, 25, 32, 0 })   // 0 = "as many as the keys" (shown for other keyboard sizes)
        addAndMakeVisible (eqBtns.add (new Btn (juce::String (n), "Cut into " + juce::String (n) + " equal chops", [this, n] { ctl.equalChops (n > 0 ? n : ctl.P.s.keys); })));
    bTrans.onClick = [this] { ctl.transientChops(); };
    bClear.onClick = [this] { ctl.setMarkers ({}); ctl.status ("CHOPS CLEARED"); };
    bResetKeys.onClick = [this] { ctl.resetKeys(); };
    addAndMakeVisible (bResetKeys);
    bToPads.onClick = [this] { ctl.chopsToPads(); };
    bToKeys.onClick = [this] { ctl.chopsToKeys(); };
    bExtract.onClick = [this] { showExtractBox (ctl, bExtract); };
    bUndo.onClick = [this] { ctl.undo(); };
    bLoad.onClick = [this] { wave.onLoadRequest(); };
    bDone.onClick = [this] { if (onClose) onClose(); };
    bTrim.onClick = [this] { if (trimming()) ctl.trimCancel(); else ctl.trimOpen(); };
    bTrimOk.onClick = [this] { ctl.trimApply(); };
    bTrimX.onClick = [this] { ctl.trimCancel(); };
    bTrimOk.setOnColour (C::ok);
    auto trimChanged = [this] (double) { ctl.trimSet (ctl.trim.a, ctl.trim.b, (float) fadeIn.slider.getValue(), (float) fadeOut.slider.getValue(), (float) gain.slider.getValue()); };
    fadeIn.onChange = trimChanged; fadeOut.onChange = trimChanged; gain.onChange = trimChanged;
    show (bTrim, bTrimOk, bTrimX, fadeIn, fadeOut, gain);
    show (wave, title, hint, bEdit, bPlay, bPrev, bNext, bZoomOut, bZoomIn, bFit, bTrans, bClear, bToPads, bToKeys, bExtract, bUndo, bLoad, bDone);
    refresh();
}

void ChopEditor::refresh()
{
    const auto& p = ctl.selPad();
    title.setText (padLabel (ctl.P.s.sel) + (p.name.isNotEmpty() ? "  " + p.name : juce::String ("  (empty pad)"))
                   + (p.buf ? "   -   " + juce::String (p.numChops()) + (p.numChops() == 1 ? " region" : " regions") : juce::String()),
                   juce::dontSendNotification);
    bEdit.setOn (wave.manual);
    const bool tr = trimming();
    bTrim.setOn (tr); bTrimOk.setVisible (tr); bTrimX.setVisible (tr);
    fadeIn.setVisible (tr); fadeOut.setVisible (tr); gain.setVisible (tr);
    if (tr) { fadeIn.set (ctl.trim.fi); fadeOut.set (ctl.trim.fo); gain.set (ctl.trim.g); }
    hint.setText (tr ? "TRIM: drag the green handles to set the start and end, set fades and gain, then Apply. Everything you play uses the trimmed sound while you trim." : wave.manual ? "Click to add a chop point, drag a point to move it, right-click (or Alt-click) a point to remove it. "
                                "Keys , and . step through the regions, Space plays the sequencer, " CX_MOD "+Z undoes."
                              : "Click a region to hear it. Turn on Edit chops to place chop points. Double-click to load another sample.",
                  juce::dontSendNotification);
    {
        static const int EQ[] = { 2, 4, 8, 16, 25, 32 };
        const int keys = ctl.P.s.keys;
        for (int i = 0; i < eqBtns.size(); ++i)
        {
            if (i < 6) { eqBtns[i]->setVisible (EQ[i] <= keys); continue; }
            // "chop to" grows with your keyboard: 37, 49, 61... equal chops, one per key
            eqBtns[i]->setVisible (keys != 25 && keys != 32);
            eqBtns[i]->setButtonText (juce::String (keys));
            eqBtns[i]->setTooltip ("Cut the sample into " + juce::String (keys) + " equal chops - one for every key");
        }
    }
    wave.refresh();
    resized();
}

void ChopEditor::paint (juce::Graphics& g) { g.fillAll (C::plate); }

void ChopEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (28);
    const bool tr = trimming();
    layoutRow (top, { { &title, 0 }, { &bTrim, 60 }, { tr ? &bTrimOk : nullptr, tr ? 64 : 1 }, { tr ? &bTrimX : nullptr, tr ? 70 : 1 },
                      { &bLoad, 70 }, { &bUndo, 60 }, { &bDone, 70 } }, 6);
    r.removeFromTop (8);
    auto tools = r.removeFromTop (28);
    {
        auto t = tools;
        for (auto* b : { &bEdit }) { b->setBounds (t.removeFromLeft (96)); t.removeFromLeft (8); }
        for (auto* b : { &bPlay }) { b->setBounds (t.removeFromLeft (56)); t.removeFromLeft (3); }
        bPrev.setBounds (t.removeFromLeft (30)); t.removeFromLeft (3);
        bNext.setBounds (t.removeFromLeft (30)); t.removeFromLeft (10);
        for (auto* b : eqBtns) if (b->isVisible()) { b->setBounds (t.removeFromLeft (34)); t.removeFromLeft (3); }
        t.removeFromLeft (7);
        Btn* rest[] = { &bTrans, &bClear, &bToPads, &bToKeys, &bExtract, &bResetKeys };
        const int bw = juce::jlimit (70, 100, (t.getWidth() - 140) / 6 - 3);
        for (auto* b : rest) { b->setBounds (t.removeFromLeft (bw)); t.removeFromLeft (3); }
        auto z = t.removeFromRight (130);
        layoutRow (z, { { &bZoomOut, 0 }, { &bZoomIn, 0 }, { &bFit, 0 } }, 3);
    }
    r.removeFromTop (8);
    hint.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (6);
    if (tr)
    {
        layoutRow (r.removeFromBottom (24), { { &fadeIn, 0 }, { &fadeOut, 0 }, { &gain, 0 } }, 14);
        r.removeFromBottom (6);
    }
    wave.setBounds (r);
}
} // namespace cx::ui
