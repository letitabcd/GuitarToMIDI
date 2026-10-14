#include "PluginLookAndFeel.h"
#include <cmath>

namespace g2m::ui
{
juce::Font font (float size, bool bold)
{
    return juce::Font (juce::FontOptions ("Avenir Next", size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont (float size)
{
    return juce::Font (juce::FontOptions ("Menlo", size, juce::Font::plain));
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, background);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::focusedOutlineColourId, accent);
    setColour (juce::ComboBox::arrowColourId, secondary);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, accent);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, panel);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
    setColour (juce::TextEditor::outlineColourId, accent.withAlpha (0.4f));
    setColour (juce::TextButton::textColourOffId, accent);
    setColour (juce::TextButton::textColourOnId, accent);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                    float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);
    const float ringRadius = radius - 3.0f;
    const bool hovered = slider.isMouseOverOrDragging();

    for (int tick = 0; tick <= 20; ++tick)
    {
        const float t = (float) tick / 20.0f;
        const float a = startAngle + t * (endAngle - startAngle);
        const float inner = radius + 2.0f, outer = radius + (tick % 5 == 0 ? 6.0f : 4.0f);
        g.setColour (t <= position ? accent.withAlpha (0.45f) : outline);
        g.drawLine (centre.x + std::sin (a) * inner, centre.y - std::cos (a) * inner,
                    centre.x + std::sin (a) * outer, centre.y - std::cos (a) * outer, 1.0f);
    }

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0, startAngle, endAngle, true);
    g.setColour (outline);
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (position > 0.0f)
    {
        juce::Path active;
        active.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0, startAngle, angle, true);
        g.setColour (accent.withAlpha (hovered ? 1.0f : 0.85f));
        g.strokePath (active, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float faceRadius = radius - 9.0f;
    const juce::Rectangle<float> face (centre.x - faceRadius, centre.y - faceRadius, faceRadius * 2, faceRadius * 2);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (face.translated (0, 3.0f).expanded (1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (hovered ? 0xff51574b : 0xff44493e), face.getTopLeft(),
                                           juce::Colour (0xff20251f), face.getBottomRight(), false));
    g.fillEllipse (face);
    g.setColour (juce::Colour (0xff697060).withAlpha (0.45f));
    g.drawEllipse (face.reduced (0.5f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.drawEllipse (face.reduced (3.0f), 1.0f);
    juce::Path pointer;
    pointer.startNewSubPath (centre.x + std::sin (angle) * faceRadius * 0.52f,
                            centre.y - std::cos (angle) * faceRadius * 0.52f);
    pointer.lineTo (centre.x + std::sin (angle) * faceRadius * 0.83f,
                    centre.y - std::cos (angle) * faceRadius * 0.83f);
    g.setColour (accent.brighter (0.1f));
    g.strokePath (pointer, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (monoFont (14.0f));
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setBorderSize (juce::BorderSize<int> (0));
    label->setJustificationType (juce::Justification::centred);
    return label;
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool pressed,
                               int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1, (float) height - 1);
    g.setColour (pressed ? raised : background);
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (box.hasKeyboardFocus (true) || box.isMouseOverOrDragging() ? accent.withAlpha (0.7f) : outline);
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
    const float arrowX = (float) width - 19.0f, arrowY = (float) height * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (arrowX - 3.0f, arrowY - 1.5f);
    arrow.lineTo (arrowX, arrowY + 1.5f);
    arrow.lineTo (arrowX + 3.0f, arrowY - 1.5f);
    g.setColour (secondary);
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (14.5f); }
void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (11, 0, box.getWidth() - 36, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool hovered, bool pressed)
{
    const bool on = button.getToggleState();
    const bool bypass = button.getComponentID() == "bypass";
    const auto colour = bypass ? juce::Colour (0xffd8967a) : accent;
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (on ? colour.withAlpha (pressed ? 0.20f : 0.10f) : (hovered ? raised : background));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (on ? colour.withAlpha (0.4f) : outline);
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
    const auto track = juce::Rectangle<float> (bounds.getRight() - 40.0f, bounds.getCentreY() - 7.0f, 28, 14);
    g.setColour (on ? colour.withAlpha (0.45f) : outline);
    g.fillRoundedRectangle (track, 7.0f);
    g.setColour (on ? colour : secondary);
    g.fillEllipse ((on ? track.getRight() - 12.0f : track.getX() + 2.0f), track.getY() + 2.0f, 10, 10);
    g.setFont (font (14.0f, true));
    g.setColour (on ? text : secondary);
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (12).withTrimmedRight (44), juce::Justification::centredLeft);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                       bool hovered, bool pressed)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (accent.withAlpha (pressed ? 0.22f : (hovered ? 0.13f : 0.06f)));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (accent.withAlpha (hovered ? 0.7f : 0.35f));
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font (14.0f, true); }
juce::Font LookAndFeel::getPopupMenuFont() { return font (13.0f); }
}
