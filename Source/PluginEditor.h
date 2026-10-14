#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginLookAndFeel.h"

//==============================================================================
class GuitarToMidiAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit GuitarToMidiAudioProcessorEditor (GuitarToMidiAudioProcessor&);
    ~GuitarToMidiAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawMeter (juce::Graphics&, const juce::String& caption, const juce::String& value,
                    float proportion, juce::Rectangle<int>, juce::Colour);

    g2m::ui::LookAndFeel lookAndFeel;
    GuitarToMidiAudioProcessor& audioProcessor;
    juce::Image heroArtwork;
    juce::TooltipWindow tooltips { this, 700 };

    juce::Label titleLabel;
    juce::Label noteLabel;        // big note name
    juce::Label freqLabel;        // Hz readout
    juce::Label centsLabel;       // cents readout
    juce::Label stateLabel;       // SILENT / LISTENING / TRACKING

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        juce::Label description;
        std::unique_ptr<Attachment> attachment;
    };

    struct Combo
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAttachment> attachment;
    };

    Knob gainKnob, gateKnob, clarityKnob, dynKnob, retrigKnob;
    Combo bendBox, channelBox, speedBox, inputBox;
    juce::ToggleButton bypassButton;
    juce::ToggleButton virtualButton { "Virtual MIDI" };
    juce::ToggleButton monitorButton { "Monitor DI" };
    juce::TextButton panicButton { "Panic" };
    juce::Label routingLabel;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ButtonAttachment> virtualAttachment, monitorAttachment;

    // telemetry snapshots
    float lastLevelDb = -120.0f, lastClarity = 0.0f, lastCents = 0.0f, lastHz = 0.0f;
    int lastNote = -1, lastState = 0;
    float displayLevel = -80.0f, displayClarity = 0.0f, displayCents = 0.0f;
    bool isBypassed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarToMidiAudioProcessorEditor)
};
