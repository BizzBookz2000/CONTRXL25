// CONTRXL 25 - dark hardware look (same palette as the web version's CSS variables)
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace cx::ui
{
namespace C
{
    const juce::Colour bg      { 0xff0d0d10 };
    const juce::Colour plate   { 0xff16171c };
    const juce::Colour plate2  { 0xff1d1f26 };
    const juce::Colour well    { 0xff08080b };
    const juce::Colour line    { 0xff2a2c35 };
    const juce::Colour line2   { 0xff555a6b };
    const juce::Colour fg      { 0xffe8e8ee };
    const juce::Colour mute    { 0xff8b8d99 };
    const juce::Colour accent  { 0xfff5a31a };
    const juce::Colour ok      { 0xff2ecf8a };
    const juce::Colour rec     { 0xffe5322d };
    const juce::Colour blue    { 0xff4aa3ff };
}
// 16-colour pad / chop palette
juce::Colour pal (int i);

juce::Font mono (float size, bool bold = false);

class CxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CxLookAndFeel();
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int depth) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override {}
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
};
} // namespace cx::ui
