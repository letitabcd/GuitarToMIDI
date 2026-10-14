#include "VirtualMidiOutput.h"

VirtualMidiOutput::VirtualMidiOutput() : Thread ("Guitar To MIDI output")
{
    const auto devices = juce::MidiInput::getAvailableDevices();
    for (int index = 1; ; ++index)
    {
        portName = "Guitar To MIDI";
        if (index > 1) portName += " (" + juce::String (index) + ")";
        bool taken = false;
        for (const auto& device : devices)
            if (device.name == portName) taken = true;
        if (! taken) break;
    }
    output = juce::MidiOutput::createNewDevice (portName);
    if (output != nullptr) startThread();
}

VirtualMidiOutput::~VirtualMidiOutput()
{
    signalThreadShouldExit();
    notify();
    // The worker owns all sends, including final note-offs before the port closes.
    stopThread (-1);
}

void VirtualMidiOutput::push (const juce::MidiMessage& message) noexcept
{
    if (output == nullptr || message.getRawDataSize() > 3) return;
    int start1, count1, start2, count2;
    fifo.prepareToWrite (1, start1, count1, start2, count2);
    if (count1 + count2 == 0)
    {
        requestPanic(); // Never leave a voice held if the queue loses a note-off.
        return;
    }
    auto& packet = packets[(size_t) (count1 > 0 ? start1 : start2)];
    packet.size = message.getRawDataSize();
    std::copy_n (message.getRawData(), packet.size, packet.bytes.begin());
    fifo.finishedWrite (1);
}

void VirtualMidiOutput::drain()
{
    int start1, count1, start2, count2;
    fifo.prepareToRead (fifo.getNumReady(), start1, count1, start2, count2);
    auto send = [this] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& packet = packets[(size_t) (start + i)];
            output->sendMessageNow (juce::MidiMessage (packet.bytes.data(), packet.size));
        }
    };
    send (start1, count1);
    send (start2, count2);
    fifo.finishedRead (count1 + count2);
}

void VirtualMidiOutput::panic()
{
    for (int channel = 1; channel <= 16; ++channel)
    {
        output->sendMessageNow (juce::MidiMessage::allNotesOff (channel));
        output->sendMessageNow (juce::MidiMessage::allSoundOff (channel));
        output->sendMessageNow (juce::MidiMessage::pitchWheel (channel, 8192));
    }
}

void VirtualMidiOutput::run()
{
    while (! threadShouldExit())
    {
        drain();
        if (panicRequested.exchange (false, std::memory_order_acq_rel)) panic();
        wait (1);
    }
    drain();
    panic();
}
