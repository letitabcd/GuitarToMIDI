#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
GuitarToMidiAudioProcessor::GuitarToMidiAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::mono(), true))
{
    gainParam    = apvts.getRawParameterValue ("gain");
    gateParam    = apvts.getRawParameterValue ("gate");
    clarityParam = apvts.getRawParameterValue ("clarity");
    velParam     = apvts.getRawParameterValue ("dynamics");
    retrigParam  = apvts.getRawParameterValue ("retrigger");
    bendParam    = apvts.getRawParameterValue ("bend");
    chanParam    = apvts.getRawParameterValue ("channel");
    speedParam   = apvts.getRawParameterValue ("speed");

    inputParam   = apvts.getRawParameterValue ("input");
    virtualParam = apvts.getRawParameterValue ("virtual");
    monitorParam = apvts.getRawParameterValue ("monitor");
    bypassParam  = apvts.getRawParameterValue ("bypass");
    pendingEvents.reserve (64);
    lifecycleEvents.ensureSize (2048);

}

juce::AudioProcessorValueTreeState::ParameterLayout
GuitarToMidiAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 }, "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
                { return juce::String (v, 1) + " dB"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gate", 1 }, "Gate",
        juce::NormalisableRange<float> (-80.0f, -24.0f, 0.5f), -55.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
                { return juce::String (v, 1) + " dB"; })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "clarity", 1 }, "Clarity",
        juce::NormalisableRange<float> (0.55f, 0.95f, 0.01f), 0.78f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dynamics", 1 }, "Dynamics",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "retrigger", 1 }, "Retrigger",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.55f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "bend", 1 }, "Pitch Bend",
        juce::StringArray { "Off", "+/-1", "+/-2", "+/-7", "+/-12" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "channel", 1 }, "MIDI Channel",
        [] { juce::StringArray a; for (int i = 1; i <= 16; ++i) a.add (juce::String (i)); return a; }(),
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "speed", 1 }, "Speed",
        juce::StringArray { "Fast", "Balanced", "Precise" }, 1));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "input", 1 }, "Input Channel",
        juce::StringArray { "Left / Mono", "Right", "L + R" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "virtual", 1 }, "Virtual MIDI", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "monitor", 1 }, "Monitor DI", false));

    return layout;
}

//==============================================================================
GuitarToMidiAudioProcessor::~GuitarToMidiAudioProcessor()
{
    virtualMidiOut.requestPanic();
}

void GuitarToMidiAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // A host may re-prepare without releaseResources (e.g. buffer size changes).
    pendingEvents.clear();
    engine.stop (pendingEvents);
    dispatchEvents (lifecycleEvents, 0, wasVirtualEnabled, 0);
    engine.prepare (sampleRate, samplesPerBlock);
    monoScratch.assign ((size_t) juce::jlimit (64, 1024, samplesPerBlock), 0.0f);
    wasBypassed = false;
    wasVirtualEnabled = isStandalone() || virtualParam->load() > 0.5f;
    previousInput = (int) inputParam->load();
}

void GuitarToMidiAudioProcessor::releaseResources()
{
    pendingEvents.clear();
    engine.stop (pendingEvents);
    // Save host note-offs for the next render; send the virtual ones immediately.
    dispatchEvents (lifecycleEvents, 0, wasVirtualEnabled, 0);
}

void GuitarToMidiAudioProcessor::reset()
{
    releaseResources();
}

bool GuitarToMidiAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono()
                     || in == juce::AudioChannelSet::stereo());
}

void GuitarToMidiAudioProcessor::dispatchEvents (juce::MidiBuffer& midi, int baseOffset,
                                                bool sendVirtual, int bendRange)
{
    auto send = [&] (const juce::MidiMessage& message, int offset)
    {
        midi.addEvent (message, offset);
        if (sendVirtual) virtualMidiOut.push (message);
    };
    for (const auto& event : pendingEvents)
    {
        const int channel = event.channel; // JUCE uses 1..16, just like the engine.
        const int offset = baseOffset + (int) event.sampleOffset;
        switch (event.type)
        {
            case g2m::MidiEvent::Type::NoteOn:
                if (bendRange > 0)
                {
                    // RPN 0: ask the receiving synth to use the same bend range.
                    send (juce::MidiMessage::controllerEvent (channel, 101, 0), offset);
                    send (juce::MidiMessage::controllerEvent (channel, 100, 0), offset);
                    send (juce::MidiMessage::controllerEvent (channel, 6, bendRange), offset);
                    send (juce::MidiMessage::controllerEvent (channel, 38, 0), offset);
                    send (juce::MidiMessage::controllerEvent (channel, 101, 127), offset);
                    send (juce::MidiMessage::controllerEvent (channel, 100, 127), offset);
                }
                send (juce::MidiMessage::noteOn (channel, event.note, event.velocity), offset);
                break;
            case g2m::MidiEvent::Type::NoteOff:
                send (juce::MidiMessage::noteOff (channel, event.note), offset);
                break;
            case g2m::MidiEvent::Type::PitchBend:
                send (juce::MidiMessage::pitchWheel (channel, event.bend), offset);
                break;
        }
    }
}

void GuitarToMidiAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midi)
{
    process (buffer, midi, false);
}

void GuitarToMidiAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                       juce::MidiBuffer& midi)
{
    process (buffer, midi, true);
}

void GuitarToMidiAudioProcessor::process (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;
    midi.addEvents (lifecycleEvents, 0, -1, 0);
    lifecycleEvents.clear();

    const bool bypassed = hostBypass || bypassParam->load() > 0.5f;
    const bool virtualEnabled = isStandalone() || virtualParam->load() > 0.5f;
    const int input = juce::jlimit (0, 2, (int) inputParam->load());
    const bool panic = panicRequested.exchange (false, std::memory_order_acq_rel);
    if ((bypassed && ! wasBypassed) || virtualEnabled != wasVirtualEnabled
        || input != previousInput || panic)
    {
        pendingEvents.clear();
        engine.stop (pendingEvents);
        dispatchEvents (midi, 0, wasVirtualEnabled, 0);
        if (panic)
        {
            for (int channel = 1; channel <= 16; ++channel)
            {
                const juce::MidiMessage messages[] = {
                    juce::MidiMessage::allNotesOff (channel),
                    juce::MidiMessage::allSoundOff (channel),
                    juce::MidiMessage::pitchWheel (channel, 8192) };
                for (const auto& message : messages)
                {
                    midi.addEvent (message, 0);
                    if (wasVirtualEnabled || virtualEnabled) virtualMidiOut.push (message);
                }
            }
        }
    }
    wasBypassed = bypassed;
    wasVirtualEnabled = virtualEnabled;
    previousInput = input;

    const int numInputs = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    if (! bypassed && numInputs > 0 && ! monoScratch.empty())
    {
        g2m::EngineParams ep;
        ep.inputGainDb   = gainParam->load();
        ep.gateDb        = gateParam->load();
        ep.clarityThresh = clarityParam->load();
        ep.velSens       = velParam->load();
        ep.retriggerSens = retrigParam->load();
        constexpr int bendValues[] = { 0, 1, 2, 7, 12 };
        constexpr int windowValues[] = { 1536, 2048, 3072 };
        ep.bendRange = bendValues[juce::jlimit (0, 4, (int) bendParam->load())];
        ep.midiChannel = juce::jlimit (1, 16, 1 + (int) chanParam->load());
        ep.windowSamples = windowValues[juce::jlimit (0, 2, (int) speedParam->load())];
        engine.setParams (ep);

        // Hosts can send larger blocks than advertised. Process bounded chunks
        // without resizing scratch buffers or the event vector in the callback.
        for (int start = 0; start < numSamples; )
        {
            const int count = juce::jmin ((int) monoScratch.size(), numSamples - start);
            const float* mono = buffer.getReadPointer (input == 1 && numInputs > 1 ? 1 : 0) + start;
            if (input == 2 && numInputs > 1)
            {
                const float* right = buffer.getReadPointer (1) + start;
                for (int i = 0; i < count; ++i) monoScratch[(size_t) i] = 0.5f * (mono[i] + right[i]);
                mono = monoScratch.data();
            }
            pendingEvents.clear();
            engine.process (mono, count, pendingEvents);
            dispatchEvents (midi, start, virtualEnabled, ep.bendRange);
            start += count;
        }
    }

    if (! hostBypass && monitorParam->load() < 0.5f)
        buffer.clear();
    else
        for (int channel = numInputs; channel < buffer.getNumChannels(); ++channel)
            buffer.clear (channel, 0, numSamples);
}

//==============================================================================
juce::AudioProcessorEditor* GuitarToMidiAudioProcessor::createEditor()
{
    return new GuitarToMidiAudioProcessorEditor (*this);
}

//==============================================================================
void GuitarToMidiAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GuitarToMidiAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuitarToMidiAudioProcessor();
}
