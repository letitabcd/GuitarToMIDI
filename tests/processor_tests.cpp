#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
int failures = 0, checks = 0;
void check (bool pass, const char* name)
{
    ++checks;
    if (! pass) ++failures;
    std::printf ("[%s] %s\n", pass ? "ok" : "FAIL", name);
}
void set (GuitarToMidiAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.apvts.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}
std::vector<juce::MidiMessage> tone (GuitarToMidiAudioProcessor& processor,
                                   int blockSize = 256, bool rightOnly = false,
                                   bool invertedRight = false)
{
    std::vector<juce::MidiMessage> result;
    juce::MidiBuffer midi;
    midi.ensureSize (8192);
    juce::AudioBuffer<float> audio (processor.getTotalNumInputChannels(), blockSize);
    for (int start = 0; start < 24000; start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float sample = 0.2f * std::sin ((float) (2.0 * 3.141592653589793 * 196.0 * (start + i) / 48000.0));
            audio.setSample (0, i, rightOnly ? 0.0f : sample);
            if (audio.getNumChannels() > 1)
                audio.setSample (1, i, invertedRight ? -sample : sample);
        }
        processor.processBlock (audio, midi);
        for (const auto metadata : midi)
        {
            check (metadata.samplePosition >= 0 && metadata.samplePosition < blockSize,
                   "MIDI timestamp belongs to the current block");
            result.push_back (metadata.getMessage());
        }
    }
    return result;
}
int firstOnChannel (const std::vector<juce::MidiMessage>& messages)
{
    for (const auto& message : messages)
        if (message.isNoteOn()) return message.getChannel();
    return 0;
}
bool hasOff (const juce::MidiBuffer& midi, int channel)
{
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOff() && metadata.getMessage().getChannel() == channel)
            return true;
    return false;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialise;
    GuitarToMidiAudioProcessor processor;
    juce::AudioBuffer<float> audio (1, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (8192);

    for (int channel = 1; channel <= 16; ++channel)
    {
        set (processor, "channel", (float) (channel - 1));
        processor.prepareToPlay (48000, 256);
        check (firstOnChannel (tone (processor)) == channel, "encoded MIDI channel matches the selected channel");
        set (processor, "bypass", 1);
        audio.clear();
        processor.processBlock (audio, midi);
        check (hasOff (midi, channel), "bypass releases the active note on its original channel");
        set (processor, "bypass", 0);
    }

    set (processor, "channel", 0);
    processor.prepareToPlay (48000, 256);
    tone (processor);
    set (processor, "channel", 8);
    audio.clear();
    processor.processBlock (audio, midi);
    check (hasOff (midi, 1), "changing channel releases the old channel");
    check (firstOnChannel (tone (processor)) == 9, "new notes use the new channel");
    processor.processBlockBypassed (audio, midi);
    check (hasOff (midi, 9), "host bypass also releases the active note");

    processor.prepareToPlay (48000, 256);
    tone (processor);
    processor.releaseResources();
    processor.prepareToPlay (48000, 256);
    processor.processBlock (audio, midi);
    check (hasOff (midi, 9), "device restart retains a note-off for the host");
    tone (processor);
    processor.prepareToPlay (48000, 128);
    processor.processBlock (audio, midi);
    check (hasOff (midi, 9), "re-prepare without releaseResources retains a note-off");

    processor.prepareToPlay (48000, 64);
    check (firstOnChannel (tone (processor, 8192)) == 9, "oversized blocks are processed without losing MIDI");
    processor.requestPanic();
    audio.clear();
    processor.processBlock (audio, midi);
    check (hasOff (midi, 9), "panic releases the tracked note");
    bool allSoundOff = false;
    for (const auto metadata : midi)
        allSoundOff |= metadata.getMessage().isAllSoundOff();
    check (allSoundOff, "panic sends all-sound-off messages");

    set (processor, "bend", 4); // +/-12
    processor.prepareToPlay (48000, 256);
    const auto bent = tone (processor);
    bool rpnRange = false;
    for (const auto& message : bent)
        rpnRange |= message.isController() && message.getControllerNumber() == 6
                    && message.getControllerValue() == 12 && message.getChannel() == 9;
    check (rpnRange, "pitch bend sensitivity is announced to the synth with RPN");
    set (processor, "bend", 0);
    processor.processBlock (audio, midi);
    check (hasOff (midi, 9), "disabling bend releases the previously bent voice");

    juce::AudioBuffer<float> empty (1, 0);
    processor.processBlock (empty, midi);
    check (midi.isEmpty(), "empty audio blocks do not emit invalid timestamps");

    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    check (processor.setBusesLayout (layout), "stereo layout is supported");
    processor.prepareToPlay (48000, 256);
    check (firstOnChannel (tone (processor, 256, false, true)) == 9,
           "default left input does not cancel an inverted stereo channel");
    set (processor, "input", 1);
    processor.prepareToPlay (48000, 256);
    check (firstOnChannel (tone (processor, 256, true)) == 9, "right-only DI input is detected when selected");

    juce::AudioBuffer<float> stereo (2, 256);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 256; ++i) stereo.setSample (channel, i, 0.123f);
    processor.processBlock (stereo, midi);
    check (stereo.getMagnitude (0, 256) < 0.000001f, "DI monitoring is muted by default");
    set (processor, "monitor", 1);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 256; ++i) stereo.setSample (channel, i, 0.123f);
    processor.processBlock (stereo, midi);
    check (std::abs (stereo.getSample (0, 128) - 0.123f) < 0.000001f,
           "DI monitoring preserves the input when enabled");

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    set (processor, "channel", 2);
    processor.setStateInformation (state.getData(), (int) state.getSize());
    check ((int) processor.apvts.getRawParameterValue ("channel")->load() == 8,
           "host state restores parameters");

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    int choices = 0;
    bool boundsValid = true;
    for (auto* child : editor->getChildren())
    {
        if (! child->isVisible()) continue; // Hidden tooltip overlays have no layout bounds.
        boundsValid &= ! child->getBounds().isEmpty() && editor->getLocalBounds().contains (child->getBounds());
        if (auto* box = dynamic_cast<juce::ComboBox*> (child))
        {
            ++choices;
            check (box->getNumItems() > 0 && box->getSelectedId() > 0, "parameter menu has selectable items");
        }
    }
    check (choices == 4 && boundsValid, "editor controls all fit within the plugin window");

    // Opt-in visual export of the actual JUCE editor, for checking native fonts,
    // embedded artwork, control spacing and Retina rendering without opening a DAW.
    if (const auto* previewDirectory = std::getenv ("G2M_UI_PREVIEW_DIR"))
    {
        const juce::File directory (juce::String::fromUTF8 (previewDirectory));
        directory.createDirectory();
        auto capture = [&] (const char* filename, float scale)
        {
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
            const auto destination = directory.getChildFile (filename);
            destination.deleteFile();
            auto output = destination.createOutputStream();
            if (output != nullptr)
            {
                juce::PNGImageFormat png;
                check (png.writeImageToStream (image, *output), "native editor preview exported");
            }
            else check (false, "native editor preview output opens");
        };
        editor.reset();
        set (processor, "channel", 0);
        set (processor, "input", 0);
        set (processor, "monitor", 0);
        processor.prepareToPlay (48000, 256);
        tone (processor);
        editor.reset (processor.createEditor());
        capture ("ui-tracking.png", 1.0f);
        capture ("ui-tracking-retina.png", 2.0f);
        editor.reset();
        processor.reset();
        editor.reset (processor.createEditor());
        capture ("ui-idle.png", 1.0f);
    }
    std::printf ("%d processor checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
