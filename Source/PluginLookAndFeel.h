#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace g2m::ui
{
inline const juce::Colour background { 0xff141615 };
inline const juce::Colour panel      { 0xff1b1e1c };
inline const juce::Colour raised     { 0xff252824 };
inline const juce::Colour outline    { 0xff363a33 };
inline const juce::Colour text       { 0xffefece3 };
inline const juce::Colour secondary  { 0xffa6ad9f };
inline const juce::Colour muted      { 0xff788273 };
inline const juce::Colour accent     { 0xffe1b479 };
inline const juce::Colour green      { 0xffb2cfad };

juce::Font font (float size, bool bold = false);
juce::Font monoFont (float size);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool pressed,
                       int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool hovered, bool pressed) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool hovered, bool pressed) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getPopupMenuFont() override;
};
}
