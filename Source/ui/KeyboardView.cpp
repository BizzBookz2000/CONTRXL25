#include "KeyboardView.h"

namespace cx::ui
{
static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

const char* KeyboardView::qwertyLabel (int i)
{
    static const char* Q[32] = { "Z", "S", "X", "D", "C", "V", "G", "B", "H", "N", "J", "M", "Q", "2", "W", "3",
                                 "E", "R", "5", "T", "6", "Y", "7", "U", "I", "9", "O", "0", "P", "[", "=", "]" };
    return (i >= 0 && i < 32) ? Q[i] : "";
}

std::vector<KeyboardView::KeyRect> KeyboardView::layout() const
{
    const int NK = ctl.P.s.keys, shift = ctl.P.keyShift();
    int whites = 0;
    for (int i = 0; i < NK; ++i) if (! isBlack ((shift + i) % 12)) ++whites;
    const auto b = getLocalBounds().toFloat();
    const float ww = b.getWidth() / (float) whites, bw = ww * 0.58f, bh = b.getHeight() * 0.62f;
    std::vector<KeyRect> out ((size_t) NK);
    int w = 0;
    for (int i = 0; i < NK; ++i)
    {
        if (isBlack ((shift + i) % 12)) out[(size_t) i] = { { b.getX() + w * ww - bw * 0.5f, b.getY(), bw, bh }, true };
        else { out[(size_t) i] = { { b.getX() + w * ww, b.getY(), ww, b.getHeight() }, false }; ++w; }
    }
    return out;
}

int KeyboardView::keyAt (juce::Point<float> p) const
{
    const auto L = layout();
    for (int i = 0; i < (int) L.size(); ++i) if (L[(size_t) i].black && L[(size_t) i].r.contains (p)) return i;
    for (int i = 0; i < (int) L.size(); ++i) if (! L[(size_t) i].black && L[(size_t) i].r.contains (p)) return i;
    return -1;
}

float KeyboardView::velAt (int key, float y) const
{
    const auto L = layout();
    if (key < 0 || key >= (int) L.size()) return 100;
    const auto& r = L[(size_t) key].r;
    return 55.0f + 72.0f * juce::jlimit (0.0f, 1.0f, (y - r.getY()) / r.getHeight());
}

void KeyboardView::light (int idx, int source, bool on)
{
    if (idx < 0 || idx >= 64) return;
    const juce::uint8 bit = (juce::uint8) (1 << source);
    lit[(size_t) idx] = on ? (juce::uint8) (lit[(size_t) idx] | bit) : (juce::uint8) (lit[(size_t) idx] & ~bit);
    repaint();
}

void KeyboardView::paint (juce::Graphics& g)
{
    static const char* NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto L = layout();
    const Pad& p = ctl.selPad();
    const bool chopMode = p.keyMode == MODE_CHOP;
    const int n = p.numChops();
    const int KB = ctl.P.keyBase();
    auto drawKey = [&] (int i)
    {
        const auto& k = L[(size_t) i];
        const int nn = KB + i + 12 * ctl.P.s.oct, pc = ((nn % 12) + 12) % 12;
        const int kc = chopMode ? p.chopForKey (i) : -1;
        const bool mapped = chopMode && kc >= 0 && kc < n;
        const bool down = lit[(size_t) i] != 0;
        auto r = k.r.reduced (k.black ? 0.0f : 1.0f, 0.0f);
        juce::Colour fill = k.black ? juce::Colour (0xff15161a) : juce::Colour (0xffe9e9ee);
        if (down) fill = C::accent;
        if (chopMode && ! mapped && ! down) fill = fill.withAlpha (0.5f);
        g.setColour (fill);
        g.fillRoundedRectangle (r.withTrimmedTop (-4.0f), 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.withTrimmedTop (-4.0f), 4.0f, 1.0f);
        if (mapped) { g.setColour (pal (kc)); g.fillRect (r.removeFromBottom (8.0f).reduced (2.0f, 0.0f)); }
        auto tr = k.r.reduced (2.0f, 4.0f);
        g.setFont (mono (9.0f));
        g.setColour (k.black ? C::mute : juce::Colour (0xff6b6d78));
        if (ctl.ui.qwerty) g.drawText (qwertyLabel (i), tr.removeFromTop (12), juce::Justification::centred);
        g.setFont (mono (k.black ? 9.0f : 10.0f, true));
        g.setColour (k.black ? C::fg : juce::Colour (0xff222222));
        juce::String label = mapped ? juce::String (kc + 1) : juce::String (NAMES[pc]) + ((pc == 0 || i == 0) ? juce::String (nn / 12 - 1) : juce::String());
        if (! k.black || mapped) g.drawText (label, tr.removeFromBottom (k.black ? 12.0f : 22.0f), juce::Justification::centred);
    };
    for (int i = 0; i < (int) L.size(); ++i) if (! L[(size_t) i].black) drawKey (i);
    for (int i = 0; i < (int) L.size(); ++i) if (L[(size_t) i].black) drawKey (i);
}

void KeyboardView::mouseDown (const juce::MouseEvent& e)
{
    mouseKey = keyAt (e.position);
    if (mouseKey < 0) return;
    light (mouseKey, 0, true);
    ctl.noteOn (mouseKey, velAt (mouseKey, e.position.y), 0);
}

void KeyboardView::mouseDrag (const juce::MouseEvent& e)
{
    const int k = keyAt (e.position);
    if (k < 0 || k == mouseKey) return;
    if (mouseKey >= 0) { ctl.noteOff (mouseKey, 0); light (mouseKey, 0, false); }
    mouseKey = k;
    light (k, 0, true);
    ctl.noteOn (k, velAt (k, e.position.y), 0);
}

void KeyboardView::mouseUp (const juce::MouseEvent&)
{
    if (mouseKey >= 0) { ctl.noteOff (mouseKey, 0); light (mouseKey, 0, false); }
    mouseKey = -1;
}
} // namespace cx::ui
