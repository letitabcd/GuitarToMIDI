/*
    Guitar To MIDI — real-time monophonic guitar pitch tracker.

    Pipeline:
      input -> gain -> DC blocker -> highpass (~70 Hz) -> lowpass (~5 kHz)
            -> ring buffer
            -> every `hop` samples: NSDF analysis (McLeod Pitch Method,
               FFT-accelerated autocorrelation) + RMS gate + onset detector
            -> note state machine (note-on / note-off / retrigger / pitch bend)

    Pure C++, no dependencies, single-producer (audio thread) usage.
*/
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <atomic>

namespace g2m
{

struct MidiEvent
{
    enum class Type : uint8_t { NoteOn, NoteOff, PitchBend };

    Type      type;
    uint8_t   channel;        // 1..16
    uint8_t   note;           // 0..127
    uint8_t   velocity;       // 1..127 (NoteOn only)
    uint16_t  bend;           // 0..16383, 8192 = centre (PitchBend only)
    uint32_t  sampleOffset;   // offset inside the process() block
};

struct EngineParams
{
    float inputGainDb   = 0.0f;    // input trim
    float gateDb        = -55.0f;  // RMS gate threshold (dBFS)
    float clarityThresh = 0.78f;   // required NSDF peak quality
    float velSens       = 0.6f;    // 0 = almost fixed velocity, 1 = wide dynamics
    float retriggerSens = 0.55f;   // same-note repick sensitivity
    int   bendRange     = 0;       // semitones, 0 = pitch bend off
    int   midiChannel   = 1;       // 1..16
    int   windowSamples = 2048;    // analysis window, referenced to 44.1 kHz
};

class PitchEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void setParams (const EngineParams& p) noexcept { params = p; }
    // reset the tracking state machine (keeps allocated tables) — realtime safe
    void resetTracking() noexcept;
    // Release the active voice before clearing history (bypass, panic, routing changes).
    void stop (std::vector<MidiEvent>& outEvents, uint32_t offset = 0);

    // Feed one block of mono samples. Generated MIDI is appended to outEvents.
    void process (const float* samples, int numSamples, std::vector<MidiEvent>& outEvents);

    // ---- telemetry (relaxed atomics, safe to poll from the UI thread) ----
    float levelDb()    const noexcept { return levelDbAtomic.load (std::memory_order_relaxed); }
    float clarity()    const noexcept { return clarityAtomic.load (std::memory_order_relaxed); }
    float pitchHz()    const noexcept { return pitchHzAtomic.load (std::memory_order_relaxed); }
    int   noteNumber() const noexcept { return noteAtomic.load (std::memory_order_relaxed); }
    float cents()      const noexcept { return centsAtomic.load (std::memory_order_relaxed); }
    int   voiceState() const noexcept { return stateAtomic.load (std::memory_order_relaxed); } // 0 silent, 1 hunting, 2 tracking

private:
    struct Peak { int tau; double val; };

    void   analyseFrame (uint32_t blockOffset, std::vector<MidiEvent>& out);
    bool   detectPitch (float& freqOut, float& clarityOut);
    void   emitNoteOn  (int note, uint8_t vel, uint32_t offset, std::vector<MidiEvent>& out);
    void   emitNoteOff (uint32_t offset, std::vector<MidiEvent>& out);
    void   pushStability (float semitones);
    bool   pitchStable (float& medianSemisOut);

    // ---- resources ----
    double sampleRate = 44100.0;
    int    maxWindow  = 0;       // largest window we ever need (allocated)
    int    curWindow  = 2048;    // active window (speed setting)
    int    hop        = 256;

    std::vector<float>          ring;      // pre-processed input history
    size_t                      ringMask   = 0;
    uint64_t                    writeCount = 0;

    std::vector<double>         fftBufRe;  // complex FFT buffer (re/im interleaved below)
    std::vector<double>         fftBufIm;
    std::vector<double>         twiddleRe; // per-stage twiddles for full length
    std::vector<double>         twiddleIm;
    std::vector<int>            bitRev;
    int                         fftSize = 0;

    std::vector<double>         nsdf;
    std::vector<double>         prefixSq;  // prefix sums of x^2 over the window
    std::vector<Peak>           peaks;

    // ---- filters / envelopes ----
    double dcX1 = 0, dcY1 = 0;                 // DC blocker state
    double hpB0 = 1, hpB1 = 0, hpB2 = 0, hpA1 = 0, hpA2 = 0; // 70 Hz biquad
    double hpX1 = 0, hpX2 = 0, hpY1 = 0, hpY2 = 0;
    double lpZ  = 0, lpK = 0.3;                // one-pole ~5 kHz

    double envFast = 0, envSlow = 0;           // attack envelopes (abs EMA)
    double peakHold = 0;                       // decaying peak of envFast
    double kFast = 0.2, kSlow = 0.001;

    // ---- state machine ----
    EngineParams params;

    bool   gateOpen     = false;
    int    releaseCnt   = 0;
    int    releaseLimit = 17;
    int    badClarityCnt = 0;
    int    devCnt       = 0;       // sustained pitch deviation counter
    int    cooldown     = 0;       // frames since last note-on (retrigger guard)

    bool   tracking     = false;
    int    curNote      = -1;
    int    activeChannel = 1;
    int    activeBendRange = 0;
    double lastSemi     = -100;    // last accepted pitch in semitones (continuity bias)
    double bendState    = 0;       // smoothed bend, in semitones
    double lastSentBend = 100;     // last emitted bend in semitones
    bool   onsetArmed = false;

    float  histSemi[3]  = { 0, 0, 0 };
    int    histCnt      = 0;

    int    hopCounter   = 0;

    // telemetry
    std::atomic<float> levelDbAtomic   { -120.0f };
    std::atomic<float> clarityAtomic   { 0.0f };
    std::atomic<float> pitchHzAtomic   { 0.0f };
    std::atomic<float> centsAtomic     { 0.0f };
    std::atomic<int>   noteAtomic      { -1 };
    std::atomic<int>   stateAtomic     { 0 };

    // constants
    static constexpr double fMinHz = 70.0;    // slightly below drop-D D2 (73.4 Hz)
    static constexpr double fMaxHz = 1350.0;  // above fret-12 high E
};

} // namespace g2m
