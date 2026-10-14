#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PitchEngine.h"
#include "VirtualMidiOutput.h"

//==============================================================================
class GuitarToMidiAudioProcessor : public juce::AudioProcessor
{
public:
    GuitarToMidiAudioProcessor();
    ~GuitarToMidiAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorParameter* getBypassParameter() const override
        { return apvts.getParameter ("bypass"); }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "Parameters",
                                                createParameterLayout() };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // engine telemetry for the editor
    g2m::PitchEngine& getEngine() { return engine; }
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    const juce::String& getMidiPortName() const { return virtualMidiOut.name(); }
    bool virtualMidiAvailable() const { return virtualMidiOut.available(); }
    void requestPanic() noexcept { panicRequested.store (true, std::memory_order_release); }

private:
    g2m::PitchEngine engine;
    std::vector<g2m::MidiEvent> pendingEvents;
    std::vector<float> monoScratch;
    bool wasBypassed = false;
    bool wasVirtualEnabled = false;
    int previousInput = 0;
    std::atomic<bool> panicRequested { false };
    juce::MidiBuffer lifecycleEvents;

    // standalone only: a virtual CoreMIDI source any app can listen to
    VirtualMidiOutput virtualMidiOut;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool hostBypass);
    void dispatchEvents (juce::MidiBuffer&, int baseOffset, bool sendVirtual, int bendRange);

    std::atomic<float>* gainParam   = nullptr;
    std::atomic<float>* gateParam   = nullptr;
    std::atomic<float>* clarityParam = nullptr;
    std::atomic<float>* velParam    = nullptr;
    std::atomic<float>* retrigParam = nullptr;
    std::atomic<float>* bendParam   = nullptr;   // choice index
    std::atomic<float>* chanParam   = nullptr;   // choice index
    std::atomic<float>* speedParam  = nullptr;   // choice index
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* virtualParam = nullptr;
    std::atomic<float>* monitorParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarToMidiAudioProcessor)
};
