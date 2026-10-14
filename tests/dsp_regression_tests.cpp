#include "../Source/PitchEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>

static bool measureAllocations = false;
static int allocations = 0;
void* operator new (std::size_t size)
{
    if (measureAllocations) ++allocations;
    if (auto* result = std::malloc (size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void operator delete (void* ptr) noexcept { std::free (ptr); }
void* operator new[] (std::size_t size) { return ::operator new (size); }
void operator delete[] (void* ptr) noexcept { ::operator delete (ptr); }

namespace
{
constexpr double pi = 3.141592653589793;
int failures = 0, checks = 0;
void check (bool pass, const char* name)
{
    ++checks;
    if (! pass) ++failures;
    std::printf ("[%s] %s\n", pass ? "ok" : "FAIL", name);
}
double hz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
std::vector<float> sine (double note, double sr, double seconds, float amplitude = 0.2f)
{
    std::vector<float> signal ((size_t) (sr * seconds));
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = amplitude * (float) std::sin (2 * pi * hz (note) * (double) i / sr);
    return signal;
}
std::vector<g2m::MidiEvent> feed (g2m::PitchEngine& engine, const std::vector<float>& signal, int block = 127)
{
    std::vector<g2m::MidiEvent> events, all;
    events.reserve (128);
    for (size_t position = 0; position < signal.size(); position += (size_t) block)
    {
        const int count = (int) std::min ((size_t) block, signal.size() - position);
        events.clear();
        engine.process (signal.data() + position, count, events);
        for (auto event : events)
        {
            if (event.sampleOffset >= (uint32_t) count) ++failures;
            event.sampleOffset += (uint32_t) position;
            all.push_back (event);
        }
    }
    return all;
}
std::vector<int> notes (const std::vector<g2m::MidiEvent>& events)
{
    std::vector<int> result;
    for (const auto& event : events)
        if (event.type == g2m::MidiEvent::Type::NoteOn) result.push_back (event.note);
    return result;
}
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int window : { 1536, 2048, 3072 })
            for (int note : { 38, 40, 55, 76, 88 })
            {
                g2m::PitchEngine engine;
                engine.prepare (sr, 127);
                g2m::EngineParams params;
                params.windowSamples = window;
                engine.setParams (params);
                const auto events = feed (engine, sine (note, sr, 0.3));
                const auto got = notes (events);
                char label[160];
                std::snprintf (label, sizeof (label), "%.0f Hz / window %d / MIDI %d: correct note, no sustain retrigger", sr, window, note);
                check (got.size() == 1 && got.front() == note, label);
            }

    constexpr double sr = 48000;
    g2m::PitchEngine engine;
    engine.prepare (sr, 256);
    g2m::EngineParams params;
    engine.setParams (params);
    feed (engine, sine (55, sr, 0.3));
    params.midiChannel = 16;
    engine.setParams (params);
    const auto changed = feed (engine, sine (57, sr, 0.3));
    bool releasedOldChannel = false, playedNewChannel = false;
    for (const auto& event : changed)
    {
        releasedOldChannel |= event.type == g2m::MidiEvent::Type::NoteOff && event.channel == 1 && event.note == 55;
        playedNewChannel |= event.type == g2m::MidiEvent::Type::NoteOn && event.channel == 16 && event.note == 57;
    }
    check (releasedOldChannel && playedNewChannel, "channel switch releases the original channel before the new note");
    std::vector<g2m::MidiEvent> stopped;
    stopped.reserve (128);
    engine.stop (stopped);
    check (! stopped.empty() && stopped.front().type == g2m::MidiEvent::Type::NoteOff,
           "stop emits the active note-off");
    check (notes (feed (engine, std::vector<float> (9600, 0))).empty(), "reset cannot redetect stale ring-buffer audio");

    engine.prepare (sr, 127);
    params = {};
    engine.setParams (params);
    auto repick = sine (55, sr, 0.7, 0.025f);
    for (size_t i = 16800; i < repick.size(); ++i) repick[i] *= 16.0f;
    check (notes (feed (engine, repick)).size() == 2, "same-note repick produces exactly one new articulation");

    engine.prepare (sr, 127);
    auto octave = sine (52, sr, 0.3);
    auto up = sine (64, sr, 0.3);
    octave.insert (octave.end(), up.begin(), up.end());
    check (notes (feed (engine, octave)) == std::vector<int> ({ 52, 64 }), "legato octave change does not stick to the subharmonic");

    engine.prepare (sr, 127);
    params.bendRange = 2;
    engine.setParams (params);
    auto slide = sine (55, sr, 0.8);
    double phase = 0;
    for (size_t i = 0; i < slide.size(); ++i)
    {
        const double semis = std::clamp (((double) i / sr - 0.3) / 0.2, 0.0, 1.0);
        phase += 2 * pi * hz (55 + semis) / sr;
        slide[i] = 0.2f * (float) std::sin (phase);
    }
    const auto bent = feed (engine, slide);
    int maximumBend = 8192;
    for (const auto& event : bent)
        if (event.type == g2m::MidiEvent::Type::PitchBend) maximumBend = std::max (maximumBend, (int) event.bend);
    check (notes (bent) == std::vector<int> ({ 55 }) && maximumBend > 12000 && maximumBend < 12600,
           "one-semitone slide holds the note and sends the expected +/-2 bend");

    engine.prepare (sr, 127);
    params = {};
    engine.setParams (params);
    auto invalid = sine (55, sr, 0.3);
    invalid[5] = std::numeric_limits<float>::quiet_NaN();
    invalid[75] = std::numeric_limits<float>::infinity();
    check (notes (feed (engine, invalid)) == std::vector<int> ({ 55 }), "NaN/Inf input cannot poison the tracking filters");

    engine.prepare (96000, 256);
    std::mt19937 random (42);
    std::uniform_real_distribution<float> noise (-0.2f, 0.2f);
    std::vector<float> block (256);
    std::vector<g2m::MidiEvent> events;
    events.reserve (128);
    allocations = 0;
    std::vector<double> elapsed;
    elapsed.reserve (1000);
    for (int i = 0; i < 1000; ++i)
    {
        for (auto& sample : block) sample = noise (random);
        params.windowSamples = i % 2 == 0 ? 1536 : 3072;
        engine.setParams (params);
        events.clear();
        const auto start = std::chrono::steady_clock::now();
        measureAllocations = true;
        engine.process (block.data(), 256, events);
        measureAllocations = false;
        elapsed.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count());
    }
    check (allocations == 0, "audio processing and speed changes allocate no heap memory");
    std::sort (elapsed.begin(), elapsed.end());
    std::printf ("96 kHz / 256 samples: p99 CPU %.3f ms, maximum %.3f ms, block budget 2.667 ms\n", elapsed[989], elapsed.back());
    std::printf ("%d regression checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
