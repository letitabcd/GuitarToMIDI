#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
int failures = 0, checks = 0;
void check (bool pass, const char* message)
{
    ++checks;
    if (! pass) ++failures;
    std::printf ("[%s] %s\n", pass ? "ok" : "FAIL", message);
}
bool ok (OSStatus status, const char* message)
{
    check (status == noErr, message);
    if (status != noErr) std::printf ("  OSStatus: %d\n", (int) status);
    return status == noErr;
}
struct Capture
{
    std::mutex mutex;
    std::vector<std::array<UInt8, 3>> messages;
    void append (const MIDIPacketList* list)
    {
        std::lock_guard<std::mutex> lock (mutex);
        const auto* packet = &list->packet[0];
        for (UInt32 i = 0; i < list->numPackets; ++i)
        {
            for (int byte = 0; byte + 2 < packet->length; byte += 3)
                messages.push_back ({ packet->data[byte], packet->data[byte + 1], packet->data[byte + 2] });
            packet = MIDIPacketNext (packet);
        }
    }
    bool contains (UInt8 type, int channel, int note = 55)
    {
        std::lock_guard<std::mutex> lock (mutex);
        for (const auto& message : messages)
            if ((message[0] & 0xf0) == type && (message[0] & 0x0f) == channel - 1
                && message[1] == note) return true;
        return false;
    }
    void clear() { std::lock_guard<std::mutex> lock (mutex); messages.clear(); }
};
struct Input { bool silent = false; };
OSStatus renderInput (void* context, AudioUnitRenderActionFlags*, const AudioTimeStamp* timestamp,
                      UInt32, UInt32 frames, AudioBufferList* buffers)
{
    for (UInt32 channel = 0; channel < buffers->mNumberBuffers; ++channel)
    {
        auto* data = static_cast<float*> (buffers->mBuffers[channel].mData);
        for (UInt32 i = 0; i < frames; ++i)
            data[i] = static_cast<Input*> (context)->silent ? 0.0f
                : (float) (0.2 * std::sin (2.0 * 3.141592653589793 * 196.0
                                          * (timestamp->mSampleTime + i) / 48000.0));
    }
    return noErr;
}
OSStatus captureOutput (void* context, const AudioTimeStamp*, UInt32, const MIDIPacketList* packets)
{
    static_cast<Capture*> (context)->append (packets);
    return noErr;
}
void captureVirtual (const MIDIPacketList* packets, void* context, void*)
{
    static_cast<Capture*> (context)->append (packets);
}
AudioUnitParameterID parameter (AudioUnit unit, const char* name)
{
    UInt32 size = 0;
    AudioUnitGetPropertyInfo (unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, nullptr);
    std::vector<AudioUnitParameterID> parameters (size / sizeof (AudioUnitParameterID));
    AudioUnitGetProperty (unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, parameters.data(), &size);
    for (auto id : parameters)
    {
        AudioUnitParameterInfo info {};
        size = sizeof (info);
        if (AudioUnitGetProperty (unit, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &size) != noErr) continue;
        char text[256] {};
        if (info.cfNameString != nullptr)
            CFStringGetCString (info.cfNameString, text, sizeof (text), kCFStringEncodingUTF8);
        else
            std::strncpy (text, info.name, sizeof (text) - 1);
        if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) && info.cfNameString != nullptr) CFRelease (info.cfNameString);
        if (std::strcmp (text, name) == 0) return id;
    }
    check (false, name);
    return 0xffffffff;
}
}

int main (int argc, char** argv)
{
    if (argc != 2) { std::fprintf (stderr, "Usage: au_integration_tests /path/to/Guitar To MIDI.component\n"); return 1; }
    std::vector<MIDIEndpointRef> existing;
    for (ItemCount i = 0; i < MIDIGetNumberOfSources(); ++i) existing.push_back (MIDIGetSource (i));
    auto url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) argv[1], (CFIndex) std::strlen (argv[1]), true);
    auto bundle = CFBundleCreate (nullptr, url);
    CFRelease (url);
    if (! bundle || ! CFBundleLoadExecutable (bundle)) { check (false, "AU bundle loads"); return 1; }
    auto factory = CFBundleGetFunctionPointerForName (bundle, CFSTR("Guitar_To_MIDIAUFactory"));
    if (! factory) { check (false, "AU factory is exported"); return 1; }
    const AudioComponentDescription description { kAudioUnitType_Effect, 'Gtmd', 'Rnyi', 0, 0 };
    auto component = AudioComponentRegister (&description, CFSTR("renyi: Guitar To MIDI Test"), 0x010000,
                                             reinterpret_cast<AudioComponentFactoryFunction> (factory));
    AudioUnit unit = nullptr;
    if (! ok (AudioComponentInstanceNew (component, &unit), "native arm64 AU instantiates")) return 1;

    AudioStreamBasicDescription format {};
    format.mSampleRate = 48000;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    format.mBytesPerPacket = format.mBytesPerFrame = sizeof (float);
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 32;
    ok (AudioUnitSetProperty (unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof (format)), "AU accepts mono DI input");
    ok (AudioUnitSetProperty (unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof (format)), "AU accepts mono output");
    Input input;
    AURenderCallbackStruct inputCallback { renderInput, &input };
    ok (AudioUnitSetProperty (unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &inputCallback, sizeof (inputCallback)), "host audio callback connects");
    Capture host, virtualMidi;
    AUMIDIOutputCallbackStruct midiCallback { captureOutput, &host };
    ok (AudioUnitSetProperty (unit, kAudioUnitProperty_MIDIOutputCallback, kAudioUnitScope_Global, 0, &midiCallback, sizeof (midiCallback)), "host MIDI output callback connects");
    if (! ok (AudioUnitInitialize (unit), "AU initializes at 48 kHz")) return 1;

    const auto channelParameter = parameter (unit, "MIDI Channel");
    const auto bypassParameter = parameter (unit, "Bypass");
    const auto virtualParameter = parameter (unit, "Virtual MIDI");
    AudioTimeStamp timestamp {};
    timestamp.mFlags = kAudioTimeStampSampleTimeValid;
    std::array<float, 256> output {};
    AudioBufferList buffers { 1, { { 1, sizeof (output), output.data() } } };
    auto render = [&] (int blocks)
    {
        for (int block = 0; block < blocks; ++block)
        {
            AudioUnitRenderActionFlags flags = 0;
            if (AudioUnitRender (unit, &flags, &timestamp, 0, 256, &buffers) != noErr) return false;
            timestamp.mSampleTime += 256;
        }
        return true;
    };
    for (int channel : { 1, 2, 16 })
    {
        AudioUnitSetParameter (unit, channelParameter, kAudioUnitScope_Global, 0, (float) (channel - 1), 0);
        AudioUnitSetParameter (unit, bypassParameter, kAudioUnitScope_Global, 0, 0, 0);
        input.silent = false;
        host.clear();
        check (render (80) && host.contains (0x90, channel), "rendered DI generates a note-on on the requested channel");
        AudioUnitSetParameter (unit, bypassParameter, kAudioUnitScope_Global, 0, 1, 0);
        check (render (1) && host.contains (0x80, channel), "AU bypass generates the matching note-off");
    }

    MIDIClientRef client = 0;
    MIDIPortRef port = 0;
    ok (MIDIClientCreate (CFSTR("Guitar To MIDI test receiver"), nullptr, nullptr, &client), "CoreMIDI test receiver starts");
    ok (MIDIInputPortCreate (client, CFSTR("Input"), captureVirtual, &virtualMidi, &port), "virtual MIDI listener starts");
    MIDIEndpointRef source = 0;
    for (ItemCount i = 0; i < MIDIGetNumberOfSources(); ++i)
    {
        auto candidate = MIDIGetSource (i);
        if (std::find (existing.begin(), existing.end(), candidate) == existing.end()) { source = candidate; break; }
    }
    check (source != 0, "AU creates a CoreMIDI source visible to other apps");
    if (source != 0)
    {
        ok (MIDIPortConnectSource (port, source, nullptr), "receiver connects to the AU virtual port");
        AudioUnitSetParameter (unit, virtualParameter, kAudioUnitScope_Global, 0, 1, 0);
        AudioUnitSetParameter (unit, bypassParameter, kAudioUnitScope_Global, 0, 0, 0);
        check (render (80), "AU renders with virtual output enabled");
        std::this_thread::sleep_for (std::chrono::milliseconds (30));
        check (virtualMidi.contains (0x90, 16), "real CoreMIDI receiver gets the generated note-on");
        AudioUnitSetParameter (unit, bypassParameter, kAudioUnitScope_Global, 0, 1, 0);
        render (1);
        std::this_thread::sleep_for (std::chrono::milliseconds (30));
        check (virtualMidi.contains (0x80, 16), "real CoreMIDI receiver gets the bypass note-off");
        MIDIPortDisconnectSource (port, source);
    }
    MIDIPortDispose (port);
    MIDIClientDispose (client);
    AudioUnitUninitialize (unit);
    AudioComponentInstanceDispose (unit);
    CFRelease (bundle);
    std::printf ("%d AU integration checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
