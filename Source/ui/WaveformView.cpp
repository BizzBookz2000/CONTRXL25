#include "WaveformView.h"

namespace cx::ui
{
float WaveformView::xToFrac (float x) const
{
    const auto a = waveArea();
    return juce::jlimit (0.0f, 1.0f, start + (x - (float) a.getX()) / (float) a.getWidth() / zoom);
}

float WaveformView::fracToX (float f) const
{
    const auto a = waveArea();
    return (float) a.getX() + (f - start) * zoom * (float) a.getWidth();
}

void WaveformView::refresh()
{
    auto b = ctl.displayBuffer (&markers);
    if (b != buf) { buf = b; peaksFor = nullptr; }
    repaint();
}

void WaveformView::tick()
{
    float ph = ctl.engine.playhead.load();
    if (ph >= 0 && ctl.trim.on && ctl.trim.pad == ctl.P.s.sel && ctl.trim.changed())
        ph = ctl.trim.a + ph * (ctl.trim.b - ctl.trim.a);   // the engine plays the trimmed slice
    if (std::abs (ph - playhead) > 1.0e-5f) { playhead = ph; repaint(); }
}

void WaveformView::zoomBy (float factor, float anchor)
{
    const float f = start + anchor / zoom;
    zoom = juce::jlimit (1.0f, 400.0f, zoom * factor);
    start = juce::jlimit (0.0f, 1.0f - 1.0f / zoom, f - anchor / zoom);
    repaint();
}

void WaveformView::buildPeaks()
{
    const auto a = waveArea();
    peaks.assign ((size_t) juce::jmax (1, a.getWidth()), { 0.0f, 0.0f });
    peaksFor = buf.get(); peaksZoom = zoom; peaksStart = start; peaksW = a.getWidth();
    if (! buf) return;
    const int n = buf->length(), nc = buf->channels();
    for (int x = 0; x < a.getWidth(); ++x)
    {
        const int i0 = (int) ((start + (float) x / (float) a.getWidth() / zoom) * (float) n);
        const int i1 = juce::jmax (i0 + 1, (int) ((start + (float) (x + 1) / (float) a.getWidth() / zoom) * (float) n));
        float mn = 0, mx = 0;
        const int stepI = juce::jmax (1, (i1 - i0) / 64);
        for (int c = 0; c < nc; ++c)
        {
            const float* d = buf->audio.getReadPointer (c);
            for (int i = juce::jmax (0, i0); i < juce::jmin (n, i1); i += stepI) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
        }
        peaks[(size_t) x] = { mn, mx };
    }
}

int WaveformView::regionAt (float f) const
{
    if (markers.empty()) return -1;
    int ci = 0;
    while (ci < (int) markers.size() && f >= markers[(size_t) ci]) ++ci;
    return ci;
}

void WaveformView::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    g.setColour (C::well); g.fillRoundedRectangle (full, 6.0f);
    g.setColour (C::line); g.drawRoundedRectangle (full.reduced (0.5f), 6.0f, 1.0f);
    const auto a = waveArea();
    if (! buf)
    {
        g.setColour (C::mute); g.setFont (mono (12.0f));
        const auto& sp = ctl.selPad();
        if (! sp.inst.empty())
        {
            g.setColour (C::accent);
            g.drawText ("VST instrument: " + sp.inst.name, a.withTrimmedBottom (a.getHeight() / 2 + 3).withTrimmedTop (a.getHeight() / 2 - 30), juce::Justification::centredBottom);
            g.setColour (C::mute);
            g.drawText ("Play it from the pad, the keys or the sequencer. Right-click the pad > Auto-sample to turn it into samples.", a.withTrimmedTop (a.getHeight() / 2 + 5).withHeight (20), juce::Justification::centredTop);
        }
        else g.drawText ("Double-click to load a sample, or drop an audio file here", a, juce::Justification::centred);
        return;
    }
    if (peaksFor != buf.get() || peaksZoom != zoom || peaksStart != start || peaksW != a.getWidth()) buildPeaks();
    const Pad& p = ctl.selPad();

    // chop regions
    std::vector<float> b { 0.0f }; b.insert (b.end(), markers.begin(), markers.end()); b.push_back (1.0f);
    if (! markers.empty())
        for (int i = 0; i + 1 < (int) b.size(); ++i)
        {
            const float x0 = juce::jmax ((float) a.getX(), fracToX (b[(size_t) i])), x1 = juce::jmin ((float) a.getRight(), fracToX (b[(size_t) i + 1]));
            if (x1 <= x0) continue;
            g.setColour (pal (i).withAlpha (i == p.sel ? 0.28f : 0.11f));
            g.fillRect (juce::Rectangle<float> (x0, (float) a.getY(), x1 - x0, (float) a.getHeight()));
            g.setColour (pal (i)); g.setFont (mono (10.0f, true));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (x0 + 3, (float) a.getY() + 2, 30, 12), juce::Justification::topLeft);
        }

    // waveform
    const float mid = (float) a.getCentreY(), hh = (float) a.getHeight() * 0.48f;
    g.setColour (C::accent.withAlpha (0.9f));
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const auto& pk = peaks[(size_t) x];
        g.drawVerticalLine (a.getX() + x, mid - pk.second * hh, mid - pk.first * hh + 1.0f);
    }
    g.setColour (C::line); g.drawHorizontalLine ((int) mid, (float) a.getX(), (float) a.getRight());

    // markers
    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const float x = fracToX (markers[(size_t) i]);
        if (x < a.getX() || x > a.getRight()) continue;
        g.setColour (i == dragMarker ? C::fg : pal (i + 1));
        g.drawVerticalLine ((int) x, (float) a.getY(), (float) a.getBottom());
        if (manual) g.fillRect (x - 3, (float) a.getY(), 6.0f, 6.0f);
    }

    // trim overlay
    if (ctl.trim.on && ctl.trim.pad == ctl.P.s.sel)
    {
        const float xa = fracToX (ctl.trim.a), xb = fracToX (ctl.trim.b);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> ((float) a.getX(), (float) a.getY(), juce::jmax (0.0f, xa - a.getX()), (float) a.getHeight()));
        g.fillRect (juce::Rectangle<float> (xb, (float) a.getY(), juce::jmax (0.0f, (float) a.getRight() - xb), (float) a.getHeight()));
        g.setColour (C::ok);
        g.fillRect (xa - 1.5f, (float) a.getY(), 3.0f, (float) a.getHeight());
        g.fillRect (xb - 1.5f, (float) a.getY(), 3.0f, (float) a.getHeight());
        g.fillRect (xa - 6.0f, (float) a.getY(), 12.0f, 10.0f);
        g.fillRect (xb - 6.0f, (float) a.getY(), 12.0f, 10.0f);
        if (buf)
        {
            const float d = (float) buf->duration();
            const float fiX = fracToX (ctl.trim.a + ctl.trim.fi / 1000.0f / d), foX = fracToX (ctl.trim.b - ctl.trim.fo / 1000.0f / d);
            juce::Path pth;
            pth.startNewSubPath (xa, (float) a.getBottom()); pth.lineTo (fiX, (float) a.getY());
            pth.lineTo (foX, (float) a.getY()); pth.lineTo (xb, (float) a.getBottom());
            g.setColour (C::ok.withAlpha (0.7f)); g.strokePath (pth, juce::PathStrokeType (1.2f));
        }
    }

    // playhead
    if (playhead >= 0)
    {
        const float x = fracToX (playhead);
        if (x >= a.getX() && x <= a.getRight()) { g.setColour (C::fg); g.drawVerticalLine ((int) x, (float) a.getY(), (float) a.getBottom()); }
    }

    // footer
    auto foot = getLocalBounds().reduced (6, 0).removeFromBottom (14);
    g.setColour (C::mute); g.setFont (mono (10.0f));
    juce::String info = juce::String (buf->duration(), 3) + " s - " + juce::String ((int) buf->sampleRate) + " Hz - "
                      + (buf->channels() > 1 ? "stereo" : "mono") + " - " + juce::String (p.numChops()) + (p.numChops() == 1 ? " region" : " regions");
    if (zoom > 1.01f) info << " - zoom x" << juce::String (zoom, 1);
    if (manual) info << " - MANUAL CHOP: click adds, drag moves, right-click removes";
    else info << " - click a region to play it, double-click to load, wheel zooms";
    g.drawText (info, foot, juce::Justification::centredLeft);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (! buf) return;
    const float f = xToFrac ((float) e.x);
    const auto a = waveArea();
    drag = D_NONE; dragMarker = -1;
    if (e.mods.isMiddleButtonDown() || (e.mods.isShiftDown() && zoom > 1.01f)) { drag = D_SCROLL; dragStartView = start; return; }

    if (ctl.trim.on && ctl.trim.pad == ctl.P.s.sel)
    {
        const float xa = fracToX (ctl.trim.a), xb = fracToX (ctl.trim.b);
        if (std::abs ((float) e.x - xa) < 9) { drag = D_TRIM_A; return; }
        if (std::abs ((float) e.x - xb) < 9) { drag = D_TRIM_B; return; }
        drag = std::abs ((float) e.x - xa) < std::abs ((float) e.x - xb) ? D_TRIM_A : D_TRIM_B;
        mouseDrag (e);
        return;
    }
    if (manual)
    {
        for (int i = 0; i < (int) markers.size(); ++i)
            if (std::abs (fracToX (markers[(size_t) i]) - (float) e.x) < 6)
            {
                if (e.mods.isPopupMenu() || e.mods.isAltDown())
                {
                    auto m = markers; m.erase (m.begin() + i);
                    ctl.setMarkers (m); ctl.status ("CHOP POINT REMOVED");
                    return;
                }
                drag = D_MARKER; dragMarker = i; ctl.pushUndo(); return;
            }
        if (e.mods.isPopupMenu() || e.mods.isAltDown()) return;
        if (e.y >= a.getY() && e.y <= a.getBottom())
        {
            auto m = markers; m.push_back (f);
            ctl.setMarkers (m);
            ctl.status ("CHOP POINT ADDED - " + juce::String ((int) ctl.selPad().markers.size() + 1) + " REGIONS");
        }
        return;
    }
    const int r = regionAt (f);
    ctl.selectChop (r);
    ctl.previewChop (r);
    if (! markers.empty() && r >= 0) { drag = D_REGION; dragRegion = r; }   // drag the region onto a pad to copy it there
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    const float f = xToFrac ((float) e.x);
    if (drag == D_SCROLL)
    {
        const float d = (float) e.getDistanceFromDragStartX() / (float) waveArea().getWidth() / zoom;
        start = juce::jlimit (0.0f, 1.0f - 1.0f / zoom, dragStartView - d);
        repaint();
    }
    else if (drag == D_TRIM_A) ctl.trimSet (juce::jmin (f, ctl.trim.b - 0.001f), ctl.trim.b, ctl.trim.fi, ctl.trim.fo, ctl.trim.g);
    else if (drag == D_TRIM_B) ctl.trimSet (ctl.trim.a, juce::jmax (f, ctl.trim.a + 0.001f), ctl.trim.fi, ctl.trim.fo, ctl.trim.g);
    else if (drag == D_REGION && e.getDistanceFromDragStart() > 10)
    {
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            juce::Image img (juce::Image::ARGB, 90, 26, true);
            {
                juce::Graphics g (img);
                g.setColour (C::accent.withAlpha (0.85f)); g.fillRoundedRectangle (0, 0, 90, 26, 5.0f);
                g.setColour (juce::Colours::black); g.setFont (mono (12.0f, true));
                g.drawText ("CHOP " + juce::String (dragRegion + 1), 0, 0, 90, 26, juce::Justification::centred);
            }
            dc->startDragging ("chop:" + juce::String (dragRegion), this, juce::ScaledImage (img));
            ctl.status ("DROP CHOP " + juce::String (dragRegion + 1) + " ON A PAD TO COPY IT THERE, OR ON A KEY TO PLAY IT FROM THAT KEY");
        }
        drag = D_NONE;
    }
    else if (drag == D_MARKER && dragMarker >= 0)
    {
        auto& pm = ctl.selPad().markers;
        if (dragMarker < (int) pm.size())
        {
            const float lo = dragMarker > 0 ? pm[(size_t) dragMarker - 1] + 0.001f : 0.001f;
            const float hi = dragMarker + 1 < (int) pm.size() ? pm[(size_t) dragMarker + 1] - 0.001f : 0.999f;
            pm[(size_t) dragMarker] = juce::jlimit (lo, hi, f);
            markers = pm;
            repaint();
        }
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (drag == D_MARKER) { ctl.selPad().hasKmap = false; ctl.selPad().kmap.clear(); ctl.changed(); }
    drag = D_NONE; dragMarker = -1;
    repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! buf) return;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = (std::abs (w.deltaX) > 0 ? w.deltaX : w.deltaY) * 0.2f / zoom;
        start = juce::jlimit (0.0f, 1.0f - 1.0f / zoom, start - d);
        repaint();
        return;
    }
    const auto a = waveArea();
    zoomBy (w.deltaY > 0 ? 1.25f : 0.8f, juce::jlimit (0.0f, 1.0f, (float) (e.x - a.getX()) / (float) a.getWidth()));
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
{
    // like the web version: double-click the sample window to load a sample (not while placing chop points)
    if (! manual && onLoadRequest) onLoadRequest();
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> fs;
    for (auto& f : files) fs.add (juce::File (f));
    ctl.loadFiles (fs, ctl.P.s.sel);
}
} // namespace cx::ui
