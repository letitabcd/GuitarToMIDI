/*
    dsp_tests — offline verification of the Guitar To MIDI pitch engine.

    Signals are synthesised with Karplus-Strong (physically modelled plucked
    string), fed through the engine in audio-sized blocks, and the emitted
    MIDI stream is checked: detected notes, onset latency, false positives.
*/
#include "../Source/PitchEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;

static void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("  [FAIL] %s\n", what.c_str()); }
    else        std::printf ("  [ ok ] %s\n", what.c_str());
}

//==============================================================================
// Karplus-Strong plucked string
static std::vector<float> ksPluck (double freq, double sr, double seconds,
                                   float amp, uint32_t seed, float damping = 0.996f)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    const int N = std::max (2, (int) std::lround (sr / freq));
    std::vector<float> buf (N);
    float prev = 0.0f;
    for (auto& v : buf) { v = 0.5f * (noise (rng) + prev); prev = v; } // soften attack spectrum

    std::vector<float> out;
    out.reserve ((size_t) (seconds * sr));
    int idx = 0;
    for (int n = 0; n < (int) (seconds * sr); ++n)
    {
        const float v = 0.5f * (buf[(size_t) idx] + buf[(size_t) ((idx + 1) % N)]) * damping;
        buf[(size_t) idx] = v;
        idx = (idx + 1) % N;
        out.push_back (v);
    }
    float peak = 1e-9f;
    for (float v : out) peak = std::max (peak, std::abs (v));
    for (float& v : out) v *= amp / peak;
    return out;
}

struct Collected
{
    struct On { int note; uint8_t vel; int64_t samplePos; };
    std::vector<On> ons;
    std::vector<int> offs;
    std::vector<int> bends;
    int64_t samplesFed = 0;
};

static Collected runEngine (g2m::PitchEngine& engine,
                            const std::vector<float>& signal,
                            int blockSize = 256)
{
    Collected c;
    std::vector<g2m::MidiEvent> ev;
    for (size_t pos = 0; pos < signal.size(); pos += (size_t) blockSize)
    {
        const int n = (int) std::min ((size_t) blockSize, signal.size() - pos);
        ev.clear();
        engine.process (signal.data() + pos, n, ev);
        for (const auto& e : ev)
        {
            if (e.type == g2m::MidiEvent::Type::NoteOn)
                c.ons.push_back ({ e.note, e.velocity, (int64_t) pos + e.sampleOffset });
            else if (e.type == g2m::MidiEvent::Type::NoteOff)
                c.offs.push_back (e.note);
            else
                c.bends.push_back (e.bend);
        }
    }
    c.samplesFed = (int64_t) signal.size();
    return c;
}

static const char* noteName (int m)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[m % 12];
}

//==============================================================================
int main()
{
    const double sr = 44100.0;
    const int blockSize = 256;

    std::printf ("==== Test 1: single plucks across the fretboard ====\n");
    {
        const int notes[] = { 40, 45, 50, 55, 59, 64, 52, 57, 62, 67, 71, 76 }; // E2..E6-ish
        for (int note : notes)
        {
            g2m::PitchEngine engine;
            engine.prepare (sr, blockSize);
            g2m::EngineParams p;
            engine.setParams (p);

            const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            auto sig = ksPluck (f, sr, 1.2, 0.20f, (uint32_t) (note * 7919 + 13));
            // trailing silence so the note releases
            sig.insert (sig.end(), (size_t) (0.6 * sr), 0.0f);

            Collected c = runEngine (engine, sig, blockSize);

            char buf[128];
            std::snprintf (buf, sizeof (buf), "note %d (%s%d, %.1f Hz): %zu note-ons",
                           note, noteName (note), note / 12 - 1, f, c.ons.size());
            std::printf ("%s\n", buf);

            std::snprintf (buf, sizeof (buf), "%s%d detected",
                           noteName (note), note / 12 - 1);
            check (! c.ons.empty() && c.ons.front().note == note, buf);

            if (! c.ons.empty())
            {
                const double ms = (double) c.ons.front().samplePos / sr * 1000.0;
                std::snprintf (buf, sizeof (buf), "%s%d onset latency %.0f ms < 140 ms",
                               noteName (note), note / 12 - 1, ms);
                check (ms < 140.0, buf);
            }
            std::snprintf (buf, sizeof (buf), "%s%d note-off received", noteName (note), note / 12 - 1);
            check (! c.offs.empty(), buf);

            // no spurious extra notes during a clean single pluck
            std::snprintf (buf, sizeof (buf), "%s%d no spurious retrigger (<=2 ons)",
                           noteName (note), note / 12 - 1);
            check (c.ons.size() <= 2, buf);
        }
    }

    std::printf ("\n==== Test 2: melody with gaps (re-picking) ====\n");
    {
        g2m::PitchEngine engine;
        engine.prepare (sr, blockSize);
        g2m::EngineParams p;
        engine.setParams (p);

        const int melody[] = { 52, 55, 59, 55, 62, 59, 64 }; // E-G-B-G-D-B-E-ish line
        std::vector<float> sig;
        for (int note : melody)
        {
            const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            auto s = ksPluck (f, sr, 0.9, 0.2f, (uint32_t) (note * 104729 + 7));
            sig.insert (sig.end(), s.begin(), s.end());
            sig.insert (sig.end(), (size_t) (0.25 * sr), 0.0f);
        }

        Collected c = runEngine (engine, sig, blockSize);
        std::vector<int> got;
        for (auto& on : c.ons) got.push_back (on.note);

        std::string seq, want;
        for (size_t i = 0; i < got.size(); ++i)
            seq += std::to_string (got[i]) + " ";
        for (int n : melody) want += std::to_string (n) + " ";
        std::printf ("  detected: %s\n  expected: %s\n", seq.c_str(), want.c_str());
        check (got.size() == 7, "all 7 melody notes detected as note-ons");
        if (got.size() == 7)
            for (size_t i = 0; i < 7; ++i)
                check (got[i] == melody[i], "melody note " + std::to_string (i) + " correct");
    }

    std::printf ("\n==== Test 3: noise immunity ====\n");
    {
        g2m::PitchEngine engine;
        engine.prepare (sr, blockSize);
        g2m::EngineParams p;
        p.gateDb = -55.0f;
        engine.setParams (p);

        std::mt19937 rng (99);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        std::vector<float> sig ((size_t) (2.0 * sr));
        for (auto& v : sig) v = noise (rng) * 0.018f; // ~ -35 dBFS noise, above gate

        Collected c = runEngine (engine, sig, blockSize);
        std::printf ("  -35 dBFS white noise, 2 s: %zu note-ons, %zu bends\n",
                     c.ons.size(), c.bends.size());
        check (c.ons.empty(), "no false note-ons from noise");
    }

    std::printf ("\n==== Test 4: silence produces nothing ====\n");
    {
        g2m::PitchEngine engine;
        engine.prepare (sr, blockSize);
        std::vector<float> sig ((size_t) sr, 0.0f);
        Collected c = runEngine (engine, sig, blockSize);
        check (c.ons.empty() && c.offs.empty(), "silent input -> no events");
    }

    std::printf ("\n==== Test 5: velocity responds to picking strength ====\n");
    {
        int softVel = -1, hardVel = -1;
        for (int hard = 0; hard < 2; ++hard)
        {
            g2m::PitchEngine engine;
            engine.prepare (sr, blockSize);
            g2m::EngineParams p;
            p.velSens = 0.8f;
            engine.setParams (p);
            const double f = 440.0 * std::pow (2.0, (55 - 69) / 12.0);
            auto sig = ksPluck (f, sr, 1.0, hard ? 0.8f : 0.05f, 1234);
            Collected c = runEngine (engine, sig, blockSize);
            if (! c.ons.empty())
                (hard ? hardVel : softVel) = c.ons.front().vel;
        }
        std::printf ("  soft -> %d, hard -> %d\n", softVel, hardVel);
        check (softVel > 0 && hardVel > softVel + 15, "hard pluck velocity > soft pluck + 15");
    }

    std::printf ("\n==== Test 6: pitch bend (±2 semitones) ====\n");
    {
        g2m::PitchEngine engine;
        engine.prepare (sr, blockSize);
        g2m::EngineParams p;
        p.bendRange = 2;
        engine.setParams (p);

        // G3 then slide up ~1 semitone by re-synthesising at rising pitch via crossfade
        const double f0 = 440.0 * std::pow (2.0, (55 - 69) / 12.0);
        auto a = ksPluck (f0, sr, 1.0, 0.2f, 42);
        const double f1 = 440.0 * std::pow (2.0, (56.0 - 69) / 12.0);
        auto b = ksPluck (f1, sr, 1.0, 0.2f, 42);
        std::vector<float> sig;
        // 0.35 s of f0, then a long crossfade into f1 (a "slide")
        const size_t n0 = (size_t) (0.35 * sr), xf = (size_t) (0.4 * sr);
        sig.insert (sig.end(), a.begin(), a.begin() + n0);
        for (size_t i = 0; i < xf; ++i)
            sig.push_back (a[n0 + i] * (1.f - i / (float) xf)
                           + b[i] * (i / (float) xf));
        Collected c = runEngine (engine, sig, blockSize);
        std::printf ("  note-ons: %zu, bend events: %zu\n", c.ons.size(), c.bends.size());
        check (c.ons.size() >= 1, "initial note detected");
        check (! c.bends.empty(), "bend events emitted during slide");
    }

    std::printf ("\n==== Test 7: 48 kHz sample rate ====\n");
    {
        g2m::PitchEngine engine;
        engine.prepare (48000.0, blockSize);
        g2m::EngineParams p;
        engine.setParams (p);
        const double f = 440.0 * std::pow (2.0, (50 - 69) / 12.0); // D3
        auto sig = ksPluck (f, 48000.0, 1.2, 0.2f, 777);
        sig.insert (sig.end(), (size_t) (0.6 * 48000.0), 0.0f);
        Collected c = runEngine (engine, sig, blockSize);
        check (! c.ons.empty() && c.ons.front().note == 50, "D3 detected at 48 kHz");
    }

    std::printf ("\n=============================================\n");
    std::printf ("%d checks, %d failures => %s\n", checks, failures,
                 failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
