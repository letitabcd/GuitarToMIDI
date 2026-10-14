#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

// One producer (the audio callback), one consumer. CoreMIDI calls stay off the
// audio thread; the fixed queue does not allocate or lock in push().
class VirtualMidiOutput final : private juce::Thread
{
public:
    VirtualMidiOutput();
    ~VirtualMidiOutput() override;
    bool available() const noexcept { return output != nullptr; }
    const juce::String& name() const noexcept { return portName; }
    void push (const juce::MidiMessage& message) noexcept;
    void requestPanic() noexcept { panicRequested.store (true, std::memory_order_release); }

private:
    struct Packet { std::array<juce::uint8, 3> bytes {}; int size = 0; };
    void run() override;
    void drain();
    void panic();

    juce::String portName;
    std::unique_ptr<juce::MidiOutput> output;
    std::array<Packet, 2048> packets {};
    juce::AbstractFifo fifo { (int) packets.size() };
    std::atomic<bool> panicRequested { false };
};
