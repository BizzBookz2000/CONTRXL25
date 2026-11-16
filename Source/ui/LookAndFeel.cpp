#include "LookAndFeel.h"

namespace cx::ui
{
juce::Colour pal (int i)
{
    static const juce::uint32 P[16] = { 0xffd2150f, 0xffd65c10, 0xffd4a010, 0xffb4d00e, 0xff6fd010, 0xff12d010, 0xff10d05c, 0xff10d09c,
                                        0xff10c4d6, 0xff1478d8, 0xff1436e0, 0xff3c10dc, 0xff8010dc, 0xffd010d8, 0xffd8108c, 0xffd4103c };
    return juce::Colour (P[((i % 16) + 16) % 16]);
}

juce::Font mono (float size, bool bold)
{
   #if JUCE_WINDOWS
    const juce::String name = "Consolas";
   #else
    const juce::String name = juce::Font::getDefaultMonospacedFontName();
   #endif
    return juce::Font (juce::FontOptions (name, size, bold ? juce::Font::bold : juce::Font::plain));
}

CxLookAndFeel::CxLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, C::bg);
    setColour (juce::TextButton::buttonColourId, C::plate2);
    setColour (juce::TextButton::buttonOnColourId, C::accent);
    setColour (juce::TextButton::textColourOffId, C::fg);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Label::textColourId, C::fg);
    setColour (juce::ComboBox::backgroundColourId, C::plate2);
    setColour (juce::ComboBox::outlineColourId, C::line);
    setColour (juce::ComboBox::textColourId, C::fg);
    setColour (juce::ComboBox::arrowColourId, C::mute);
    setColour (juce::PopupMenu::backgroundColourId, C::plate);
    setColour (juce::PopupMenu::textColourId, C::fg);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, C::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextEditor::backgroundColourId, C::well);
    setColour (juce::TextEditor::textColourId, C::fg);
    setColour (juce::TextEditor::outlineColourId, C::line);
    setColour (juce::TextEditor::focusedOutlineColourId, C::accent);
    setColour (juce::TextEditor::highlightColourId, C::accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, C::accent);
    setColour (juce::Slider::trackColourId, C::accent);
    setColour (juce::Slider::backgroundColourId, C::well);
    setColour (juce::Slider::thumbColourId, C::fg);
    setColour (juce::Slider::textBoxTextColourId, C::fg);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, C::well);
    setColour (juce::ListBox::backgroundColourId, C::well);
    setColour (juce::ScrollBar::thumbColourId, C::line2);
    setColour (juce::TabbedComponent::backgroundColourId, C::plate);
    setColour (juce::TabbedComponent::outlineColourId, C::line);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, C::line);
    setColour (juce::AlertWindow::backgroundColourId, C::plate);
    setColour (juce::AlertWindow::textColourId, C::fg);
    setColour (juce::AlertWindow::outlineColourId, C::accent);
    setColour (juce::ProgressBar::foregroundColourId, C::accent);
    setColour (juce::ProgressBar::backgroundColourId, C::well);
    setColour (juce::ToggleButton::textColourId, C::fg);
    setColour (juce::ToggleButton::tickColourId, C::accent);
    setColour (juce::DocumentWindow::textColourId, C::fg);
    setDefaultSansSerifTypeface (nullptr);
}

juce::Font CxLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return mono (juce::jlimit (10.0f, 13.0f, h * 0.42f), true);
}

void CxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto custom = b.getProperties()["onColour"];
    const juce::Colour onCol = custom.isVoid() ? C::accent : juce::Colour ((juce::uint32) (juce::int64) custom);
    const bool hw = (bool) b.getProperties()["hw"];
    juce::Colour fill = on ? (hw ? C::plate2.interpolatedWith (onCol, 0.16f) : onCol) : C::plate2;
    if (! b.isEnabled()) fill = fill.withAlpha (0.35f);
    if (down) r = r.translated (0, 1);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? onCol : (over ? C::line2 : C::line));
    g.drawRoundedRectangle (r, 5.0f, on && hw ? 1.6f : 1.0f);
}

void CxLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const bool on = b.getToggleState();
    const bool hw = (bool) b.getProperties()["hw"];
    auto custom = b.getProperties()["onColour"];
    const juce::Colour onCol = custom.isVoid() ? C::accent : juce::Colour ((juce::uint32) (juce::int64) custom);
    auto col = on ? (hw ? onCol : juce::Colours::black) : C::fg;
    if (! b.isEnabled()) col = col.withAlpha (0.35f);
    g.setColour (col);
    auto r = b.getLocalBounds().reduced (4, 2);
    if (down) r = r.translated (0, 1);
    const auto lines = juce::StringArray::fromLines (b.getButtonText());
    if (lines.size() == 2)
    {
        auto top = r.removeFromTop (r.getHeight() / 2 + 2);
        g.drawFittedText (lines[0], top, juce::Justification::centredBottom, 1);
        g.setFont (mono (9.5f));
        g.setColour (on ? (hw ? onCol.withAlpha (0.75f) : juce::Colours::black.withAlpha (0.7f)) : C::mute);
        g.drawFittedText (lines[1], r, juce::Justification::centredTop, 1);
    }
    else g.drawFittedText (b.getButtonText(), r, juce::Justification::centred, 2);
}

void CxLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                      juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    const bool horiz = style == juce::Slider::LinearHorizontal;
    auto track = horiz ? juce::Rectangle<float> ((float) x, (float) y + h * 0.5f - 2.5f, (float) w, 5.0f)
                       : juce::Rectangle<float> ((float) x + w * 0.5f - 2.5f, (float) y, 5.0f, (float) h);
    g.setColour (C::well); g.fillRoundedRectangle (track, 2.5f);
    g.setColour (C::line); g.drawRoundedRectangle (track, 2.5f, 1.0f);
    auto fill = track;
    if (horiz) fill.setRight (pos); else fill.setTop (pos);
    g.setColour (s.isEnabled() ? C::accent : C::line2);
    g.fillRoundedRectangle (fill, 2.5f);
    g.setColour (s.isEnabled() ? C::fg : C::mute);
    if (horiz) g.fillEllipse (pos - 5.5f, (float) y + h * 0.5f - 5.5f, 11.0f, 11.0f);
    else g.fillEllipse ((float) x + w * 0.5f - 5.5f, pos - 5.5f, 11.0f, 11.0f);
}

juce::Font CxLookAndFeel::getLabelFont (juce::Label& l) { return mono (juce::jmin (12.0f, l.getFont().getHeight())); }
juce::Font CxLookAndFeel::getComboBoxFont (juce::ComboBox&) { return mono (12.0f); }
juce::Font CxLookAndFeel::getPopupMenuFont() { return mono (13.0f); }

void CxLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (C::plate2); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? C::accent : C::line); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path p;
    const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (C::mute); g.fillPath (p);
}

void CxLookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    const bool front = b.isFrontTab();
    g.setColour (front ? C::accent : (over ? C::line : C::plate2));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (front ? juce::Colours::black : C::fg);
    g.setFont (mono (12.0f, true));
    g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, 1);
}

int CxLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& b, int)
{
    return juce::GlyphArrangement::getStringWidthInt (mono (12.0f, true), b.getButtonText().toUpperCase()) + 40;
}
} // namespace cx::ui
