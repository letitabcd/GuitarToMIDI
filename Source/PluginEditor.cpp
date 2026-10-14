#include "PluginEditor.h"
#include "GuitarToMidiAssets.h"
#include <cmath>

namespace
{
constexpr int editorWidth = 900;
constexpr int editorHeight = 652;
const juce::Rectangle<int> heroBounds { 24, 76, 852, 190 };
const juce::Rectangle<int> routingBounds { 24, 486, 852, 122 };

void caption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> bounds)
{
    auto type = g2m::ui::font (11.5f, true);
    type.setExtraKerningFactor (0.12f);
    g.setFont (type);
    g.setColour (g2m::ui::secondary);
    g.drawText (text, bounds, juce::Justification::centredLeft);
}
}

GuitarToMidiAudioProcessorEditor::GuitarToMidiAudioProcessorEditor (GuitarToMidiAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    heroArtwork = juce::ImageCache::getFromMemory (GuitarToMidiAssets::harmonic_strings_png,
                                                   GuitarToMidiAssets::harmonic_strings_pngSize);
    auto& apvts = p.apvts;
    auto setupLabel = [this] (juce::Label& label, juce::Font font, juce::Colour colour,
                              juce::Justification alignment = juce::Justification::centredLeft)
    {
        label.setFont (font);
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (alignment);
        label.setBorderSize (juce::BorderSize<int> (0));
        addAndMakeVisible (label);
    };

    setupLabel (titleLabel, g2m::ui::font (23.0f, true), g2m::ui::text);
    titleLabel.setText ("Guitar To MIDI", juce::dontSendNotification);
    setupLabel (noteLabel, g2m::ui::font (88.0f, true), g2m::ui::text);
    noteLabel.setMinimumHorizontalScale (1.0f);
    setupLabel (freqLabel, g2m::ui::monoFont (12.0f), g2m::ui::secondary);
    setupLabel (centsLabel, g2m::ui::monoFont (12.0f), g2m::ui::secondary, juce::Justification::centred);
    setupLabel (stateLabel, g2m::ui::font (12.0f, true), g2m::ui::secondary, juce::Justification::centred);

    auto setupKnob = [&] (Knob& knob, const juce::String& id, const juce::String& name,
                          const juce::String& description)
    {
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                         juce::MathConstants<float>::pi * 2.75f, true);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 108, 22);
        knob.slider.setLookAndFeel (&lookAndFeel);
        knob.slider.setDoubleClickReturnValue (true, apvts.getParameter (id)->convertFrom0to1 (
                                                       apvts.getParameter (id)->getDefaultValue()));
        knob.slider.setTitle (name);
        addAndMakeVisible (knob.slider);
        setupLabel (knob.label, g2m::ui::font (14.5f, true), g2m::ui::text, juce::Justification::centred);
        knob.label.setText (name, juce::dontSendNotification);
        setupLabel (knob.description, g2m::ui::font (12.0f), g2m::ui::secondary, juce::Justification::centred);
        knob.description.setText (description, juce::dontSendNotification);
        knob.attachment = std::make_unique<Attachment> (apvts, id, knob.slider);
    };
    setupKnob (gainKnob, "gain", "Input gain", "Signal trim");
    setupKnob (gateKnob, "gate", "Gate", "Noise floor");
    setupKnob (clarityKnob, "clarity", "Clarity", "Pitch certainty");
    setupKnob (dynKnob, "dynamics", "Dynamics", "Velocity response");
    setupKnob (retrigKnob, "retrigger", "Retrigger", "Pick articulation");

    auto setupCombo = [&] (Combo& combo, const juce::String& id, const juce::String& name)
    {
        addAndMakeVisible (combo.box);
        combo.box.setTitle (name);
        setupLabel (combo.label, g2m::ui::font (11.5f, true), g2m::ui::secondary);
        combo.label.setText (name, juce::dontSendNotification);
        if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
            combo.box.addItemList (parameter->choices, 1);
        combo.attachment = std::make_unique<ComboAttachment> (apvts, id, combo.box);
    };
    setupCombo (inputBox, "input", "INPUT CHANNEL");
    setupCombo (bendBox, "bend", "PITCH BEND");
    setupCombo (channelBox, "channel", "MIDI CHANNEL");
    setupCombo (speedBox, "speed", "ANALYSIS SPEED");

    bypassButton.setButtonText ("Bypass");
    bypassButton.setComponentID ("bypass");
    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, "bypass", bypassButton);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (virtualButton);
    if (p.isStandalone())
    {
        virtualButton.setToggleState (true, juce::dontSendNotification);
        virtualButton.setEnabled (false);
    }
    else
        virtualAttachment = std::make_unique<ButtonAttachment> (apvts, "virtual", virtualButton);
    addAndMakeVisible (monitorButton);
    monitorAttachment = std::make_unique<ButtonAttachment> (apvts, "monitor", monitorButton);
    addAndMakeVisible (panicButton);
    panicButton.setButtonText ("Release notes");
    panicButton.onClick = [this] { audioProcessor.requestPanic(); };
    setupLabel (routingLabel, g2m::ui::font (11.0f), g2m::ui::secondary);

    gainKnob.slider.setTooltip ("Input trim for detection. Aim for -18 to -6 dBFS on strong plucks. Double-click to reset.");
    gateKnob.slider.setTooltip ("Set above the DI noise floor. Raising this also shortens note sustain. Double-click to reset.");
    clarityKnob.slider.setTooltip ("Higher values need a clearer, more stable pitch. Double-click to reset.");
    dynKnob.slider.setTooltip ("How strongly MIDI velocity follows your picking dynamics. Double-click to reset.");
    retrigKnob.slider.setTooltip ("Higher values detect repeated picking more easily. Double-click to reset.");
    bendBox.box.setTooltip ("Match the synth's pitch-bend range if it does not accept MIDI RPN messages.");
    inputBox.box.setTooltip ("Choose the channel carrying your guitar DI. Mono input always uses its only channel.");
    channelBox.box.setTooltip ("MIDI channel used for generated notes.");
    speedBox.box.setTooltip ("Fast uses a shorter detection window. Balanced is the default for guitar.");
    virtualButton.setTooltip ("Enable for Logic: receive this virtual MIDI port on a software instrument track.");
    monitorButton.setTooltip ("Hear the original DI through this plugin's audio output.");
    bypassButton.setTooltip ("Stop conversion and release the current MIDI note.");
    panicButton.setTooltip ("Panic: release held MIDI notes and centre pitch bend.");

    setSize (editorWidth, editorHeight);
    timerCallback();
    displayLevel = juce::jlimit (-80.0f, 0.0f, lastLevelDb);
    displayClarity = lastClarity;
    displayCents = juce::jlimit (-50.0f, 50.0f, lastCents);
    startTimerHz (30);
}

GuitarToMidiAudioProcessorEditor::~GuitarToMidiAudioProcessorEditor()
{
    stopTimer();
    for (auto* knob : { &gainKnob, &gateKnob, &clarityKnob, &dynKnob, &retrigKnob })
        knob->slider.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void GuitarToMidiAudioProcessorEditor::timerCallback()
{
    auto& engine = audioProcessor.getEngine();
    lastLevelDb = engine.levelDb();
    lastClarity = engine.clarity();
    lastHz = engine.pitchHz();
    lastNote = engine.noteNumber();
    lastCents = engine.cents();
    lastState = engine.voiceState();
    isBypassed = bypassButton.getToggleState();
    displayLevel += 0.28f * (juce::jlimit (-80.0f, 0.0f, lastLevelDb) - displayLevel);
    displayClarity += 0.24f * (lastClarity - displayClarity);
    displayCents += 0.3f * ((lastState == 2 ? juce::jlimit (-50.0f, 50.0f, lastCents) : 0.0f) - displayCents);

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (lastNote >= 0 && lastNote <= 127 && lastState == 2 && ! isBypassed)
    {
        noteLabel.setText (juce::String (names[lastNote % 12]) + juce::String (lastNote / 12 - 1), juce::dontSendNotification);
        noteLabel.setColour (juce::Label::textColourId, g2m::ui::text);
        freqLabel.setText (juce::String (lastHz, 1) + " Hz", juce::dontSendNotification);
        const int roundedCents = (int) std::lround (lastCents);
        centsLabel.setText ((roundedCents > 0 ? "+" : "") + juce::String (roundedCents) + " cents", juce::dontSendNotification);
        stateLabel.setText ("TRACKING", juce::dontSendNotification);
        stateLabel.setColour (juce::Label::textColourId, g2m::ui::green);
    }
    else
    {
        noteLabel.setText (juce::String::fromUTF8 ("—"), juce::dontSendNotification);
        noteLabel.setColour (juce::Label::textColourId, g2m::ui::secondary.withAlpha (0.65f));
        freqLabel.setText (isBypassed ? "CONVERSION PAUSED" : "PLAY A SINGLE NOTE", juce::dontSendNotification);
        centsLabel.setText ("PITCH ALIGNMENT", juce::dontSendNotification);
        stateLabel.setText (isBypassed ? "BYPASSED" : (lastState == 1 ? "LISTENING" : "READY"), juce::dontSendNotification);
        stateLabel.setColour (juce::Label::textColourId, lastState == 1 && ! isBypassed ? g2m::ui::accent : g2m::ui::secondary);
    }
    const bool virtualEnabled = audioProcessor.isStandalone() || virtualButton.getToggleState();
    routingLabel.setText (virtualEnabled
        ? (audioProcessor.virtualMidiAvailable() ? "VIRTUAL MIDI  /  " + audioProcessor.getMidiPortName()
                                                : "Virtual MIDI unavailable. Use host MIDI routing.")
        : "HOST MIDI OUTPUT  /  Enable Virtual MIDI to connect with Logic", juce::dontSendNotification);
    repaint();
}

void GuitarToMidiAudioProcessorEditor::drawMeter (juce::Graphics& g, const juce::String& label,
                                                  const juce::String& value, float proportion,
                                                  juce::Rectangle<int> area, juce::Colour colour)
{
    caption (g, label, area.withHeight (14));
    g.setFont (g2m::ui::monoFont (10.5f));
    g.setColour (g2m::ui::text);
    g.drawText (value, area.withHeight (14), juce::Justification::centredRight);
    const int segments = 32;
    const float step = (float) area.getWidth() / (float) segments;
    for (int index = 0; index < segments; ++index)
    {
        const bool lit = (float) index / (float) segments < proportion;
        g.setColour (lit ? colour : juce::Colour (0xff343a30));
        g.fillRoundedRectangle ((float) area.getX() + (float) index * step, (float) area.getY() + 21.0f,
                                step - 2.0f, 4.0f, 1.0f);
    }
}

void GuitarToMidiAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace g2m::ui;
    g.fillAll (background);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22261f), 0, 0, background, 0, 220, false));
    g.fillRect (getLocalBounds().removeFromTop (220));

    // A small native mark: six strings forming one harmonic pulse.
    g.setColour (accent);
    const float lengths[] = { 10, 19, 29, 23, 14, 8 };
    for (int string = 0; string < 6; ++string)
        g.fillRoundedRectangle (29.0f + (float) string * 4.8f, 38.0f - lengths[string] * 0.5f,
                                1.8f, lengths[string], 0.9f);
    caption (g, "REALTIME GUITAR INTERFACE", { 76, 47, 300, 14 });
    caption (g, juce::String::fromUTF8 ("DI  →  MIDI"), { 640, 27, 95, 20 });

    const auto hero = heroBounds.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.fillRoundedRectangle (hero.translated (0, 4), 13);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (hero, 13.0f);
        g.reduceClipRegion (clip);
        g.setColour (panel);
        g.fillRect (hero);
        if (heroArtwork.isValid())
            g.drawImage (heroArtwork, hero, juce::RectanglePlacement::fillDestination);
        juce::ColourGradient shade (juce::Colour (0xff191d19), hero.getX(), hero.getY(),
                                   juce::Colour (0xff191d19).withAlpha (0.18f), hero.getRight(), hero.getY(), false);
        shade.addColour (0.5, juce::Colour (0xff191d19).withAlpha (0.88f));
        g.setGradientFill (shade);
        g.fillRect (hero);
    }
    g.setColour (outline.withAlpha (0.7f));
    g.drawRoundedRectangle (hero.reduced (0.5f), 13, 1);
    caption (g, "DETECTED NOTE", { 48, 95, 210, 18 });
    caption (g, "TUNING", { 342, 123, 218, 16 });

    const auto statusColour = isBypassed ? secondary : (lastState == 2 ? green : (lastState == 1 ? accent : secondary));
    g.setColour (statusColour.withAlpha (0.08f));
    g.fillRoundedRectangle (699, 92, 154, 26, 13);
    g.setColour (statusColour.withAlpha (0.22f));
    g.drawRoundedRectangle (699.5f, 92.5f, 153, 25, 13, 1);
    g.setColour (statusColour);
    g.fillEllipse (713, 102, 5, 5);

    // Tuner is driven by pitch telemetry; idle remains a quiet centre mark.
    const float tunerX = 342, tunerWidth = 218, tunerY = 162;
    g.setColour (outline);
    g.drawHorizontalLine ((int) tunerY, tunerX, tunerX + tunerWidth);
    for (int tick = 0; tick <= 10; ++tick)
    {
        const float x = tunerX + (float) tick * tunerWidth / 10.0f;
        const float height = tick == 5 ? 15.0f : (tick % 5 == 0 ? 11.0f : 6.0f);
        g.setColour (tick == 5 ? accent.withAlpha (0.6f) : outline.brighter (0.1f));
        g.drawVerticalLine ((int) x, tunerY - height * 0.5f, tunerY + height * 0.5f);
    }
    if (lastState == 2 && ! isBypassed)
    {
        const float x = tunerX + (displayCents + 50.0f) / 100.0f * tunerWidth;
        g.setColour (accent.withAlpha (0.12f));
        g.fillEllipse (x - 8, tunerY - 8, 16, 16);
        g.setColour (std::abs (lastCents) < 8 ? green : accent);
        g.fillRoundedRectangle (x - 2, tunerY - 8, 4, 16, 2);
    }
    g.setFont (monoFont (9.0f));
    g.setColour (muted);
    g.drawText ("-50", 342, 178, 35, 12, juce::Justification::centredLeft);
    g.drawText ("0", 436, 178, 30, 12, juce::Justification::centred);
    g.drawText ("+50", 525, 178, 35, 12, juce::Justification::centredRight);

    drawMeter (g, "INPUT LEVEL", lastLevelDb < -79 ? "-- dB" : juce::String (lastLevelDb, 1) + " dB",
               juce::jlimit (0.0f, 1.0f, (displayLevel + 60) / 60), { 617, 169, 236, 26 }, accent);
    drawMeter (g, "PITCH CONFIDENCE", juce::String ((int) std::lround (lastClarity * 100)) + "%",
               displayClarity, { 617, 217, 236, 26 }, green);

    caption (g, "TRACKING", { 24, 285, 140, 18 });
    g.setFont (font (11.0f));
    g.setColour (muted);
    g.drawText ("Shape the response to your playing", 500, 285, 376, 18, juce::Justification::centredRight);
    for (int separator = 1; separator < 5; ++separator)
    {
        g.setColour (outline.withAlpha (0.45f));
        g.drawVerticalLine (24 + separator * 170, 330, 455);
    }

    g.setColour (panel);
    g.fillRoundedRectangle (routingBounds.toFloat(), 11.0f);
    g.setColour (outline.withAlpha (0.65f));
    g.drawRoundedRectangle (routingBounds.toFloat().reduced (0.5f), 11.0f, 1.0f);
    g.setColour (outline.withAlpha (0.55f));
    g.drawHorizontalLine (558, 40, 860);
    g.setFont (font (10.5f));
    g.setColour (muted);
    g.drawText ("MONOPHONIC  /  LIVE", 680, 623, 196, 16, juce::Justification::centredRight);
    g.setColour (virtualButton.getToggleState() || audioProcessor.isStandalone() ? accent : muted);
    g.fillEllipse (27, 627, 5, 5);
}

void GuitarToMidiAudioProcessorEditor::resized()
{
    titleLabel.setBounds (76, 18, 360, 31);
    noteLabel.setBounds (44, 114, 280, 111);
    freqLabel.setBounds (48, 231, 270, 18);
    centsLabel.setBounds (342, 205, 218, 20);
    stateLabel.setBounds (721, 94, 123, 22);
    bypassButton.setBounds (750, 22, 126, 34);
    int column = 0;
    for (auto* knob : { &gainKnob, &gateKnob, &clarityKnob, &dynKnob, &retrigKnob })
    {
        const int x = 24 + column++ * 170;
        knob->label.setBounds (x + 5, 313, 160, 18);
        knob->slider.setBounds (x + 5, 333, 160, 114);
        knob->description.setBounds (x + 5, 453, 160, 17);
    }
    column = 0;
    for (auto* combo : { &inputBox, &bendBox, &channelBox, &speedBox })
    {
        const int x = 40 + column++ * 207;
        combo->label.setBounds (x, 497, 191, 16);
        combo->box.setBounds (x, 519, 191, 30);
    }
    virtualButton.setBounds (40, 569, 154, 28);
    monitorButton.setBounds (206, 569, 154, 28);
    panicButton.setBounds (726, 569, 134, 28);
    routingLabel.setBounds (41, 620, 614, 21);
}
