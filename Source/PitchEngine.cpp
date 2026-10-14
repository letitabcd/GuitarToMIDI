#include "PitchEngine.h"

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cassert>

namespace g2m
{

static inline double dbToLin (double db) noexcept  { return std::pow (10.0, db / 20.0); }
static inline float  dbToLinF (float db) noexcept  { return (float) std::pow (10.0, (double) db / 20.0); }

//==============================================================================
void PitchEngine::prepare (double sr, int /*maxBlockSize*/)
{
    sampleRate = sr;

    // window lengths are defined at 44.1 kHz; scale to the real rate
    const double scale = sr / 44100.0;
    maxWindow = (int) std::clamp (std::llround (3072.0 * scale / 64.0) * 64.0, 1024.0, 8192.0);
    hop       = (int) std::clamp (std::llround (256.0 * scale), 64LL, 1024LL);

    size_t ringSize = 4;
    while (ringSize < (size_t) (2 * maxWindow)) ringSize <<= 1;
    ring.assign (ringSize, 0.0f);
    ringMask   = ringSize - 1;
    writeCount = 0;

    fftSize = 4;
    while (fftSize < 2 * maxWindow) fftSize <<= 1;
    fftBufRe.assign ((size_t) fftSize, 0.0);
    fftBufIm.assign ((size_t) fftSize, 0.0);
    bitRev.assign ((size_t) fftSize, 0);
    {
        int lg = 0; while ((1 << lg) < fftSize) ++lg;
        for (int i = 0; i < fftSize; ++i)
            bitRev[(size_t) i] = (bitRev[(size_t) i >> 1] >> 1) | ((i & 1) << (lg - 1));
        twiddleRe.assign ((size_t) fftSize / 2, 0.0);
        twiddleIm.assign ((size_t) fftSize / 2, 0.0);
        for (int i = 0; i < fftSize / 2; ++i)
        {
            const double ang = -2.0 * M_PI * (double) i / (double) fftSize;
            twiddleRe[(size_t) i] = std::cos (ang);
            twiddleIm[(size_t) i] = std::sin (ang);
        }
    }

    nsdf.assign    ((size_t) maxWindow + 2, 0.0);
    prefixSq.assign((size_t) maxWindow + 2, 0.0);
    peaks.reserve ((size_t) maxWindow / 2);

    // 70 Hz highpass biquad (RBJ, Q = 0.707)
    {
        const double f0 = 70.0, Q = 0.707, w0 = 2.0 * M_PI * f0 / sr;
        const double alpha = std::sin (w0) / (2.0 * Q), cw = std::cos (w0);
        const double a0 = 1.0 + alpha;
        hpB0 = (1.0 + cw) / 2.0 / a0;
        hpB1 = -(1.0 + cw) / a0;
        hpB2 = (1.0 + cw) / 2.0 / a0;
        hpA1 = -2.0 * cw / a0;
        hpA2 = (1.0 - alpha) / a0;
    }
    // one-pole lowpass ~5 kHz
    {
        const double wc = 2.0 * M_PI * 5000.0 / sr;
        lpK = wc / (wc + 1.0);
    }

    // envelope detector coefficients
    kFast = 1.0 - std::exp (-1.0 / (0.003 * sr));
    kSlow = 1.0 - std::exp (-1.0 / (0.150 * sr));

    releaseLimit = std::max (4, (int) std::lround (0.10 * sr / hop));

    // reset state
    dcX1 = dcY1 = 0; hpX1 = hpX2 = hpY1 = hpY2 = 0; lpZ = 0;
    envFast = envSlow = peakHold = 0;
    gateOpen = false;
    releaseCnt = badClarityCnt = devCnt = cooldown = 0;
    tracking = false; curNote = -1; lastSemi = -100;
    activeChannel = 1; activeBendRange = 0; onsetArmed = false;
    bendState = 0; lastSentBend = 100;
    histCnt = 0; hopCounter = 0;
    levelDbAtomic = -120.0f; clarityAtomic = 0; pitchHzAtomic = 0;
    noteAtomic = -1; stateAtomic = 0; centsAtomic = 0;
}

//==============================================================================
void PitchEngine::resetTracking() noexcept
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writeCount = 0;
    dcX1 = dcY1 = 0; hpX1 = hpX2 = hpY1 = hpY2 = 0; lpZ = 0;
    envFast = envSlow = peakHold = 0;
    gateOpen = false;
    releaseCnt = badClarityCnt = devCnt = cooldown = 0;
    tracking = false; curNote = -1; lastSemi = -100;
    onsetArmed = false;
    bendState = 0; lastSentBend = 100;
    histCnt = 0; hopCounter = 0;
    levelDbAtomic = -120.0f; clarityAtomic = 0; pitchHzAtomic = 0;
    noteAtomic = -1; stateAtomic = 0; centsAtomic = 0;
}

void PitchEngine::stop (std::vector<MidiEvent>& out, uint32_t offset)
{
    emitNoteOff (offset, out);
    resetTracking();
}

//==============================================================================
void PitchEngine::process (const float* in, int numSamples, std::vector<MidiEvent>& out)
{
    if (numSamples <= 0 || in == nullptr || ring.empty()) return;
    if (tracking && (params.midiChannel != activeChannel || params.bendRange != activeBendRange))
        stop (out);
    const float gainLin = dbToLinF (params.inputGainDb);

    for (int i = 0; i < numSamples; ++i)
    {
        double s = std::isfinite (in[i]) ? (double) in[i] * gainLin : 0.0;

        // DC blocker
        const double y = s - dcX1 + 0.995 * dcY1;
        dcX1 = s; dcY1 = y;

        // 70 Hz highpass biquad
        const double hp = hpB0 * y + hpB1 * hpX1 + hpB2 * hpX2 - hpA1 * hpY1 - hpA2 * hpY2;
        hpX2 = hpX1; hpX1 = y; hpY2 = hpY1; hpY1 = hp;

        // gentle lowpass (kills pick squeak / hiss before correlation)
        lpZ += lpK * (hp - lpZ);

        ring[writeCount & ringMask] = (float) lpZ;
        ++writeCount;

        const double a = std::abs (lpZ);
        envFast += kFast * (a - envFast);
        envSlow += kSlow * (a - envSlow);

        if (++hopCounter >= hop)
        {
            hopCounter = 0;
            analyseFrame ((uint32_t) i, out);
        }
    }
}

//==============================================================================
bool PitchEngine::detectPitch (float& freqOut, float& clarityOut)
{
    const int W = curWindow;
    const int N = fftSize;

    // copy window (zero-padded to fftSize) and power spectrum in one pass
    for (int i = 0; i < W; ++i)
    {
        const float v = ring[(writeCount - (uint64_t) W + (uint64_t) i) & ringMask];
        fftBufRe[(size_t) i] = v;
        fftBufIm[(size_t) i] = 0.0;
    }
    std::fill (fftBufRe.begin() + W, fftBufRe.end(), 0.0);
    std::fill (fftBufIm.begin() + W, fftBufIm.end(), 0.0);

    // ---- FFT (iterative radix-2) ----
    for (int i = 0; i < N; ++i)
        if (i < bitRev[(size_t) i])
        {
            std::swap (fftBufRe[(size_t) i], fftBufRe[(size_t) bitRev[(size_t) i]]);
            std::swap (fftBufIm[(size_t) i], fftBufIm[(size_t) bitRev[(size_t) i]]);
        }
    for (int len = 2; len <= N; len <<= 1)
    {
        const int half = len >> 1, stride = N / len;
        for (int base = 0; base < N; base += len)
            for (int j = 0; j < half; ++j)
            {
                const auto& wr = twiddleRe[(size_t) (j * stride)];
                const auto& wi = twiddleIm[(size_t) (j * stride)];
                const double vr = fftBufRe[(size_t) (base + j + half)] * wr - fftBufIm[(size_t) (base + j + half)] * wi;
                const double vi = fftBufRe[(size_t) (base + j + half)] * wi + fftBufIm[(size_t) (base + j + half)] * wr;
                const double ur = fftBufRe[(size_t) (base + j)], ui = fftBufIm[(size_t) (base + j)];
                fftBufRe[(size_t) (base + j)]        = ur + vr;
                fftBufIm[(size_t) (base + j)]        = ui + vi;
                fftBufRe[(size_t) (base + j + half)] = ur - vr;
                fftBufIm[(size_t) (base + j + half)] = ui - vi;
            }
    }

    // power spectrum, then inverse FFT -> circular autocorrelation is linear
    // because the signal block is zero-padded to 2x the window.
    for (int i = 0; i < N; ++i)
    {
        const double r = fftBufRe[(size_t) i], im = fftBufIm[(size_t) i];
        fftBufRe[(size_t) i] = r * r + im * im;
        fftBufIm[(size_t) i] = 0.0;
    }
    for (int i = 0; i < N; ++i)
        if (i < bitRev[(size_t) i])
        {
            std::swap (fftBufRe[(size_t) i], fftBufRe[(size_t) bitRev[(size_t) i]]);
            std::swap (fftBufIm[(size_t) i], fftBufIm[(size_t) bitRev[(size_t) i]]);
        }
    for (int len = 2; len <= N; len <<= 1)
    {
        const int half = len >> 1, stride = N / len;
        for (int base = 0; base < N; base += len)
            for (int j = 0; j < half; ++j)
            {
                const double wr = twiddleRe[(size_t) (j * stride)], wi = -twiddleIm[(size_t) (j * stride)];
                const double vr = fftBufRe[(size_t) (base + j + half)] * wr - fftBufIm[(size_t) (base + j + half)] * wi;
                const double vi = fftBufRe[(size_t) (base + j + half)] * wi + fftBufIm[(size_t) (base + j + half)] * wr;
                const double ur = fftBufRe[(size_t) (base + j)], ui = fftBufIm[(size_t) (base + j)];
                fftBufRe[(size_t) (base + j)]        = ur + vr;
                fftBufIm[(size_t) (base + j)]        = ui + vi;
                fftBufRe[(size_t) (base + j + half)] = ur - vr;
                fftBufIm[(size_t) (base + j + half)] = ui - vi;
            }
    }
    const double invN = 1.0 / (double) N;
    // acf[t] = fftBufRe[t] * invN

    // prefix sums of x^2 for the MPM normaliser
    prefixSq[0] = 0.0;
    for (int i = 0; i < W; ++i)
    {
        const double v = ring[(writeCount - (uint64_t) W + (uint64_t) i) & ringMask];
        prefixSq[(size_t) i + 1] = prefixSq[(size_t) i] + v * v;
    }
    const double totalPower = prefixSq[(size_t) W];

    const int tmin = std::max (2,    (int) std::floor (sampleRate / fMaxHz));
    const int tmax = std::min (W - 2, (int) std::ceil  (sampleRate / fMinHz));

    peaks.clear();
    double maxVal = -1.0;

    nsdf[(size_t) (tmin - 1)] = 0.0;   // clean the left boundary before peak tests
    for (int t = tmin; t <= tmax; ++t)
    {
        const double m = prefixSq[(size_t) (W - t)] + (totalPower - prefixSq[(size_t) t]);
        nsdf[(size_t) t] = (m <= 1e-12) ? 0.0
                                        : 2.0 * fftBufRe[(size_t) t] * invN / m;
    }
    for (int t = tmin + 1; t < tmax; ++t)
    {
        const double v = nsdf[(size_t) t];
        if (v > 0.0 && v >= nsdf[(size_t) t - 1] && v > nsdf[(size_t) t + 1])
        {
            peaks.push_back ({ t, v });
            maxVal = std::max (maxVal, v);
        }
    }

    if (peaks.empty() || maxVal < 0.25)
    {
        clarityOut = (float) std::max (0.0, maxVal);
        freqOut = 0.0f;
        return false;
    }

    // MPM: first peak within k of the global maximum
    Peak chosen = peaks.front();
    for (const auto& p : peaks)
        if (p.val >= 0.90 * maxVal) { chosen = p; break; }

    // continuity bias: a recent pitch pulls us toward a nearby candidate
    if (lastSemi > -50)
    {
        for (const auto& p : peaks)
        {
            const double semi = 69.0 + 12.0 * std::log2 ((sampleRate / (double) p.tau) / 440.0);
            // A true octave-up also has a perfect peak at twice its period.
            // Only prefer the previous note when its peak is materially better;
            // otherwise continuity would permanently hold the lower octave.
            if (std::abs (semi - lastSemi) < 0.6 && p.val > chosen.val + 0.03
                && p.val >= 0.82 * maxVal)
            {
                chosen = p;
                break;
            }
        }
    }

    // parabolic refinement
    const int t = chosen.tau;
    double refined = chosen.val, tauStar = (double) t;
    if (t > tmin && t < tmax)
    {
        const double ym = nsdf[(size_t) t - 1], y0 = nsdf[(size_t) t], yp = nsdf[(size_t) t + 1];
        const double denom = ym - 2.0 * y0 + yp;
        if (std::abs (denom) > 1e-9)
        {
            const double d = 0.5 * (ym - yp) / denom;
            if (std::abs (d) <= 0.5)
            {
                tauStar = (double) t + d;
                refined = y0 - 0.25 * (ym - yp) * d;
            }
        }
    }

    freqOut    = (float) (sampleRate / tauStar);
    clarityOut = (float) std::clamp (refined, 0.0, 1.0);
    return true;
}

//==============================================================================
void PitchEngine::pushStability (float semitones)
{
    if (histCnt < 3) ++histCnt;
    histSemi[0] = histSemi[1];
    histSemi[1] = histSemi[2];
    histSemi[2] = semitones;
}

bool PitchEngine::pitchStable (float& medianOut)
{
    if (histCnt < 3) return false;
    float a = histSemi[0], b = histSemi[1], c = histSemi[2];
    const float lo = std::min ({ a, b, c }), hi = std::max ({ a, b, c });
    if (hi - lo > 0.45f) return false;
    // median
    if (a > b) std::swap (a, b);
    if (b > c) std::swap (b, c);
    if (a > b) std::swap (a, b);
    medianOut = b;
    return true;
}

//==============================================================================
void PitchEngine::emitNoteOn (int note, uint8_t vel, uint32_t offset, std::vector<MidiEvent>& out)
{
    if (params.bendRange > 0)
        out.push_back ({ MidiEvent::Type::PitchBend, (uint8_t) params.midiChannel,
                         0, 0, 8192, offset });
    out.push_back ({ MidiEvent::Type::NoteOn, (uint8_t) params.midiChannel,
                     (uint8_t) note, vel, 0, offset });
    tracking  = true;
    curNote   = note;
    activeChannel = params.midiChannel;
    activeBendRange = params.bendRange;
    cooldown  = 0;
    devCnt    = 0;
    badClarityCnt = 0;
    bendState     = 0;
    lastSentBend  = 100;
}

void PitchEngine::emitNoteOff (uint32_t offset, std::vector<MidiEvent>& out)
{
    if (! tracking) return;
    out.push_back ({ MidiEvent::Type::NoteOff, (uint8_t) activeChannel,
                     (uint8_t) curNote, 0, 0, offset });
    if (activeBendRange > 0)
        out.push_back ({ MidiEvent::Type::PitchBend, (uint8_t) activeChannel,
                         0, 0, 8192, offset });
    tracking = false;
    curNote  = -1;
}

//==============================================================================
void PitchEngine::analyseFrame (uint32_t blockOffset, std::vector<MidiEvent>& out)
{
    // active window from the speed parameter, clamped to what we allocated
    const double scale = sampleRate / 44100.0;
    curWindow = (int) std::clamp (std::llround (params.windowSamples * scale / 64.0) * 64.0,
                                  1024.0, (double) maxWindow);

    // ---- RMS gate (with 3 dB hysteresis) ----
    double sumSq = 0.0;
    for (int i = curWindow - hop; i < curWindow; ++i)   // RMS over the newest hop only
    {
        const double v = ring[(writeCount - 1 - (uint64_t) (curWindow - 1 - i)) & ringMask];
        sumSq += v * v;
    }
    const double rms = std::sqrt (sumSq / (double) hop);
    const double rmsDb = 20.0 * std::log10 (rms + 1e-12);
    levelDbAtomic.store ((float) rmsDb, std::memory_order_relaxed);

    const double gateLin = dbToLin (params.gateDb);
    if (! gateOpen)
        gateOpen = rms > gateLin;
    else if (rms < gateLin * 0.7)          // ~3 dB below -> close
        gateOpen = false;

    // decaying peak envelope, used for velocity
    peakHold = std::max (envFast, peakHold * 0.82);

    ++cooldown;
    if (cooldown > 100000) cooldown = 100000;

    if (! gateOpen)
    {
        histCnt = 0;
        clarityAtomic.store (0.0f, std::memory_order_relaxed);
        pitchHzAtomic.store (0.0f, std::memory_order_relaxed);
        lastSemi = -100;
        if (tracking)
        {
            if (++releaseCnt >= releaseLimit)
            {
                emitNoteOff (blockOffset, out);
                releaseCnt = 0;
                stateAtomic.store (0, std::memory_order_relaxed);
                noteAtomic.store (-1, std::memory_order_relaxed);
            }
            else
                stateAtomic.store (2, std::memory_order_relaxed); // holding through a dip
        }
        else
            stateAtomic.store (0, std::memory_order_relaxed);
        return;
    }

    releaseCnt = 0;

    // ---- pitch analysis ----
    float freq = 0, clarity = 0;
    const bool havePitch = detectPitch (freq, clarity);
    clarityAtomic.store (clarity, std::memory_order_relaxed);
    pitchHzAtomic.store (freq, std::memory_order_relaxed);

    const float semi = havePitch ? (float) (69.0 + 12.0 * std::log2 ((double) freq / 440.0)) : -100.0f;
    const bool goodClarity = havePitch && clarity >= params.clarityThresh;

    if (goodClarity)
    {
        pushStability (semi);
        lastSemi = semi;
        badClarityCnt = 0;
    }
    else
    {
        ++badClarityCnt;
        if (badClarityCnt >= 2) histCnt = 0;
    }

    // ---------------- state machine ----------------
    if (! tracking)
    {
        stateAtomic.store (1, std::memory_order_relaxed);
        float median = 0;
        if (goodClarity && pitchStable (median))
        {
            // velocity from the decaying peak envelope
            const double peakDb = 20.0 * std::log10 (peakHold + 1e-12);
            const double loDb = std::max ((double) params.gateDb + 14.0, -46.0);
            const double hiDb = -2.0;
            double norm = (peakDb - loDb) / (hiDb - loDb);
            norm = std::clamp (norm, 0.0, 1.0);
            const double spread = 0.2 + 1.6 * (double) params.velSens;
            int vel = (int) std::lround (74.0 + (norm - 0.5) * 106.0 * spread);
            vel = std::clamp (vel, 12, 127);

            emitNoteOn ((int) std::lround (median), (uint8_t) vel, blockOffset, out);
            centsAtomic.store ((median - std::round (median)) * 100.0f, std::memory_order_relaxed);
            noteAtomic.store (curNote, std::memory_order_relaxed);
            stateAtomic.store (2, std::memory_order_relaxed);
        }
        return;
    }

    // ---- tracking ----
    stateAtomic.store (2, std::memory_order_relaxed);

    // note lost (severe clarity collapse)?
    if (! havePitch || clarity < params.clarityThresh - 0.20f)
    {
        if (badClarityCnt >= 5)
        {
            emitNoteOff (blockOffset, out);
            histCnt = 0;
            stateAtomic.store (1, std::memory_order_relaxed);
            noteAtomic.store (-1, std::memory_order_relaxed);
            return;
        }
    }

    if (! goodClarity) return;

    // pitch deviation -> retrigger or pitch bend
    const double dev = (double) semi - (double) curNote;

    if (std::abs (dev) >= 0.7)
    {
        ++devCnt;
        if (devCnt >= 2)
        {
            const int newNote = (int) std::lround (semi);
            const bool wantBend = params.bendRange > 0
                                  && std::abs (dev) < (params.bendRange - 0.25);
            if (! wantBend)
            {
                emitNoteOff (blockOffset, out);
                emitNoteOn (newNote, 76, blockOffset, out);
                noteAtomic.store (curNote, std::memory_order_relaxed);
                centsAtomic.store ((semi - std::round (semi)) * 100.0f, std::memory_order_relaxed);
            }
            devCnt = 0;
        }
    }
    else
        devCnt = 0;

    if (tracking && params.bendRange > 0)
    {
        const double target = std::clamp ((double) semi - (double) curNote,
                                          - (double) params.bendRange, (double) params.bendRange);
        bendState += 0.4 * (target - bendState);
        if (std::abs (bendState - lastSentBend) > 0.02)
        {
            const double normBend = std::clamp (bendState / (double) params.bendRange, -1.0, 1.0);
            const uint16_t bend14 = (uint16_t) std::lround (8192.0 + normBend * 8191.0);
            out.push_back ({ MidiEvent::Type::PitchBend, (uint8_t) params.midiChannel,
                             0, 0, bend14, blockOffset });
            lastSentBend = bendState;
        }
    }

    // same-note repick (retrigger) for articulation
    const double ratio = envFast / (envSlow + 1e-9);
    const double rtThresh = 1.35 + (1.0 - (double) params.retriggerSens) * 1.65;
    if (ratio < 1.15) onsetArmed = true;
    if (tracking && onsetArmed && cooldown * hop > (int) (0.07 * sampleRate))
    {
        if (ratio > rtThresh)
        {
            onsetArmed = false;
            const double peakDb = 20.0 * std::log10 (peakHold + 1e-12);
            const double loDb = std::max ((double) params.gateDb + 14.0, -46.0);
            double norm = (peakDb - loDb) / (-2.0 - loDb);
            norm = std::clamp (norm, 0.0, 1.0);
            const double spread = 0.2 + 1.6 * (double) params.velSens;
            int vel = (int) std::lround (74.0 + (norm - 0.5) * 106.0 * spread);
            vel = std::clamp (vel, 12, 127);

            const int note = curNote;
            emitNoteOff (blockOffset, out);
            emitNoteOn (note, (uint8_t) vel, blockOffset, out);
            noteAtomic.store (note, std::memory_order_relaxed);
        }
    }

    if (curNote >= 0)
        centsAtomic.store ((float) (((double) semi - (double) curNote) * 100.0),
                           std::memory_order_relaxed);
}

} // namespace g2m
