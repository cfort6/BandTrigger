#pragma once

// TriggerEngine: the drum-hit detector at the heart of BandTrigger.
//
// It has no JUCE dependency so it can be unit-tested on its own
// (see Tests/EngineTest.cpp).
//
// Signal flow per sample:
//   mono input -> 4th-order band-pass (LR4 high-pass + LR4 low-pass)
//              -> rectify -> envelope follower (fast attack, slower release)
//              -> onset detector (threshold + hysteresis + retrigger lockout)
//              -> velocity measured over a short window after the onset
//              -> note-on / note-off scheduled `lookahead` samples after the
//                 transient, so that when the host compensates for the
//                 reported latency the MIDI lands exactly on the hit.
//
// Low bands detect late because a narrow low filter needs time to ring up
// (its group delay, ~8 ms at 60 Hz). Part of the lookahead is used to cancel
// that delay, so set lookahead >= the filter delay for tight timing.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bt
{

constexpr double pi = 3.14159265358979323846;

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline float process (float xIn) noexcept
    {
        const double x = xIn;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void reset() noexcept { z1 = z2 = 0; }

    // RBJ cookbook filters
    void setLowPass (double fs, double f, double q) noexcept
    {
        const double w0 = 2.0 * bt::pi * f / fs, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = ((1.0 - c) * 0.5) / a0;
        b1 = (1.0 - c) / a0;
        b2 = b0;
        a1 = (-2.0 * c) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void setHighPass (double fs, double f, double q) noexcept
    {
        const double w0 = 2.0 * bt::pi * f / fs, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = ((1.0 + c) * 0.5) / a0;
        b1 = -(1.0 + c) / a0;
        b2 = b0;
        a1 = (-2.0 * c) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    // Magnitude response at frequency f (used by the GUI to draw the curve)
    double magnitudeAt (double fs, double f) const noexcept
    {
        const double w = 2.0 * bt::pi * f / fs;
        const double c1 = std::cos (w), s1 = std::sin (w);
        const double c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    double phaseAt (double fs, double f) const noexcept
    {
        const double w = 2.0 * bt::pi * f / fs;
        const double c1 = std::cos (w), s1 = std::sin (w);
        const double c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::atan2 (ni, nr) - std::atan2 (di, dr);
    }
};

//==============================================================================
// Band-pass made from two Butterworth high-passes and two Butterworth
// low-passes (Linkwitz-Riley 24 dB/oct slopes on each side).
struct BandPass
{
    std::array<Biquad, 4> stages;

    void set (double fs, double lowHz, double highHz) noexcept
    {
        const double nyq = fs * 0.5;
        lowHz  = std::clamp (lowHz, 5.0, nyq * 0.9);
        highHz = std::clamp (highHz, lowHz * 1.01, nyq * 0.95);
        constexpr double q = 0.70710678118654752;
        stages[0].setHighPass (fs, lowHz, q);
        stages[1].setHighPass (fs, lowHz, q);
        stages[2].setLowPass (fs, highHz, q);
        stages[3].setLowPass (fs, highHz, q);
    }

    inline float process (float x) noexcept
    {
        for (auto& s : stages)
            x = s.process (x);
        return x;
    }

    void reset() noexcept
    {
        for (auto& s : stages)
            s.reset();
    }

    double magnitudeAt (double fs, double f) const noexcept
    {
        double m = 1.0;
        for (auto& s : stages)
            m *= s.magnitudeAt (fs, f);
        return m;
    }

    double phaseAt (double fs, double f) const noexcept
    {
        double p = 0.0;
        for (auto& s : stages)
            p += s.phaseAt (fs, f);
        return p;
    }

    // Group delay in seconds at frequency f: how long energy at f takes to get
    // through the filter. Used to pull detected hits back onto the transient.
    double groupDelayAt (double fs, double f) const noexcept
    {
        const double df = f * 0.001;
        double d = phaseAt (fs, f + df) - phaseAt (fs, f - df);
        while (d > bt::pi)  d -= 2.0 * bt::pi;
        while (d < -bt::pi) d += 2.0 * bt::pi;
        return -d / (2.0 * bt::pi * 2.0 * df);
    }
};

inline void bandEdges (double centreHz, double widthOct, double& lowHz, double& highHz) noexcept
{
    const double half = std::pow (2.0, widthOct * 0.5);
    lowHz  = centreHz / half;
    highHz = centreHz * half;
}

inline float gainToDb (float g) noexcept { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }

//==============================================================================
struct NoteEvent
{
    int64_t time = 0;   // absolute sample time
    int note = 36;
    int channel = 10;   // 1..16
    int velocity = 0;   // 0 = note-off
};

//==============================================================================
class TriggerEngine
{
public:
    static constexpr float hysteresisDb    = 3.0f;   // must fall this far below threshold to re-arm
    static constexpr float velocityRangeDb = 30.0f;  // dB above threshold that maps to full velocity
    static constexpr double maxVelocityWindowMs = 5.0;
    static constexpr double maxNoteLengthMs     = 30.0;
    static constexpr double maxBacktrackMs      = 4.0;
    static constexpr float  riseDb              = 20.0f;
    static constexpr double delayCompFactor     = 0.33;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        attackCoeff  = 1.0f - (float) std::exp (-1.0 / (0.0003 * fs)); // 0.3 ms
        releaseCoeff = 1.0f - (float) std::exp (-1.0 / (0.050 * fs));  // 50 ms
        filterDirty = true;
        reset();
    }

    void reset()
    {
        band.reset();
        env = 0.0f;
        armed = true;
        samplesSinceOnset = 1 << 30;
        pendingActive = false;
        numEvents = 0;
        history.fill (-120.0f);
    }

    //--- settings (call once per block before processing) ----------------------
    void setBand (double centreHz, double widthOct)
    {
        if (filterDirty || std::abs (centreHz - centre) > 1.0e-6 || std::abs (widthOct - width) > 1.0e-6)
        {
            centre = centreHz;
            width  = widthOct;
            double lo, hi;
            bandEdges (centre, width, lo, hi);
            band.set (fs, lo, hi);
            // The start of a hit's rise emerges from the filter before its
            // group delay (which measures the centre of the energy), so only
            // part of it is cancelled (the onset back-tracking below
            // recovers the rest). 0.33 was tuned on synthetic drums
            // with 60 Hz, 1.5 kHz and 10 kHz bands (see Tests/EngineTest.cpp).
            filterDelaySamples = (int64_t) std::lround (delayCompFactor * band.groupDelayAt (fs, centre) * fs);
            filterDirty = false;
        }
    }

    void setThresholdDb (float db) noexcept         { thresholdDb = db; }
    void setRetriggerMs (float ms) noexcept         { retriggerSamples = (int64_t) (ms * 0.001 * fs); }
    void setSensitivity (float zeroToOne) noexcept  { sensitivity = std::clamp (zeroToOne, 0.0f, 1.0f); }
    void setLookaheadSamples (int n) noexcept       { lookahead = std::max (0, n); }
    void setNote (int noteNumber, int midiChannel) noexcept { note = noteNumber; channel = midiChannel; }
    void setDetectionEnabled (bool e) noexcept      { enabled = e; }

    //--- processing ---------------------------------------------------------------
    // Feed one mono sample taken at absolute sample time t. Returns the
    // band-passed sample (used for "solo band" monitoring).
    inline float processSample (float x, int64_t t) noexcept
    {
        const float y = band.process (x);
        const float r = std::abs (y);
        env += (r > env ? attackCoeff : releaseCoeff) * (r - env);
        envDb = gainToDb (env);
        history[(size_t) (t & historyMask)] = envDb;

        ++samplesSinceOnset;

        if (! armed && envDb < thresholdDb - hysteresisDb)
            armed = true;

        if (enabled && armed && envDb > thresholdDb && samplesSinceOnset >= retriggerSamples)
        {
            armed = false;
            samplesSinceOnset = 0;
            pendingActive = true;
            pendingOnset = t;
            pendingPeakDb = envDb;
        }

        if (pendingActive)
        {
            pendingPeakDb = std::max (pendingPeakDb, envDb);
            if (t - pendingOnset >= velocityWindowSamples())
                finaliseHit (t);
        }

        return y;
    }

    // Calls fn(const NoteEvent&) for every scheduled event with time < endTime,
    // removing them from the queue.
    template <typename Fn>
    void popEventsBefore (int64_t endTime, Fn&& fn)
    {
        int i = 0;
        while (i < numEvents)
        {
            if (events[(size_t) i].time < endTime)
            {
                fn (events[(size_t) i]);
                events[(size_t) i] = events[(size_t) --numEvents];
            }
            else
            {
                ++i;
            }
        }
    }

    float getEnvelopeDb() const noexcept { return envDb; }
    int   getLastVelocity() const noexcept { return lastVelocity; }
    int   getHitCount() const noexcept { return hitCount; }
    double getFilterDelayMs() const noexcept { return (double) filterDelaySamples * 1000.0 / fs; }
    const BandPass& getBandPass() const noexcept { return band; }

    // Velocity mapping: sensitivity 0 = every hit at 127, 1 = full dynamics.
    static int velocityFor (float peakDb, float thresholdDb, float sensitivity) noexcept
    {
        const float norm = std::clamp ((peakDb - thresholdDb) / velocityRangeDb, 0.0f, 1.0f);
        const float v = 127.0f * ((1.0f - sensitivity) + sensitivity * norm);
        return std::clamp ((int) std::lround (v), 1, 127);
    }

private:
    // How much of the lookahead is spent cancelling the band-pass filter's own
    // delay (large for low bands: ~8 ms at 60 Hz, well under 1 ms for hats).
    int64_t delayCompensation() const noexcept
    {
        return std::min<int64_t> (filterDelaySamples, lookahead);
    }

    int64_t velocityWindowSamples() const noexcept
    {
        // Measure the peak for up to 5 ms using whatever lookahead is left
        // after delay compensation, otherwise the note would be late.
        return std::min<int64_t> (lookahead - delayCompensation(),
                                  (int64_t) (maxVelocityWindowMs * 0.001 * fs));
    }

    // The threshold crossing happens later for soft hits than for loud ones
    // (a soft hit takes longer to climb to the threshold). Once we know the
    // peak, walk back through the envelope history to where the hit started
    // rising (peak - riseDb), which is level-independent.
    int64_t refinedOnset() const noexcept
    {
        const int64_t maxBack = std::min<int64_t> ((int64_t) (maxBacktrackMs * 0.001 * fs), historyMask);
        const float startLevel = pendingPeakDb - riseDb;
        int64_t t = pendingOnset;
        while (pendingOnset - t < maxBack && history[(size_t) ((t - 1) & historyMask)] > startLevel)
            --t;
        return t;
    }

    void finaliseHit (int64_t now) noexcept
    {
        pendingActive = false;
        const int vel = velocityFor (pendingPeakDb, thresholdDb, sensitivity);
        lastVelocity = vel;
        ++hitCount;

        const int64_t onTime = std::max (refinedOnset() + lookahead - delayCompensation(), now);
        // Keep the note shorter than the retrigger time so a note-off never
        // lands on top of the next note-on for the same pitch.
        const int64_t len = std::max<int64_t> (1, std::min<int64_t> ((int64_t) (maxNoteLengthMs * 0.001 * fs),
                                                                      (retriggerSamples * 8) / 10));
        push ({ onTime, note, channel, vel });
        push ({ onTime + len, note, channel, 0 });
    }

    void push (const NoteEvent& e) noexcept
    {
        if (numEvents < (int) events.size())
            events[(size_t) numEvents++] = e;
    }

    double fs = 44100.0;
    BandPass band;
    bool filterDirty = true;
    double centre = 60.0, width = 1.2;
    int64_t filterDelaySamples = 0;

    float attackCoeff = 0.1f, releaseCoeff = 0.001f;
    float env = 0.0f, envDb = -120.0f;

    float thresholdDb = -18.0f, sensitivity = 0.7f;
    int64_t retriggerSamples = 2205;
    int lookahead = 0;
    int note = 36, channel = 10;
    bool enabled = true;

    bool armed = true;
    int64_t samplesSinceOnset = 1 << 30;

    bool pendingActive = false;
    int64_t pendingOnset = 0;
    float pendingPeakDb = -120.0f;
    int lastVelocity = 0;
    int hitCount = 0;

    static constexpr int64_t historyMask = 4095;
    std::array<float, 4096> history {};

    std::array<NoteEvent, 256> events {};
    int numEvents = 0;
};

//==============================================================================
// Simple fixed-size delay used to delay the audio by the lookahead amount.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        buffer.assign ((size_t) maxDelaySamples + 1, 0.0f);
        writePos = 0;
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    inline float process (float x, int delay) noexcept
    {
        const int size = (int) buffer.size();
        buffer[(size_t) writePos] = x;
        int readPos = writePos - std::min (delay, size - 1);
        if (readPos < 0)
            readPos += size;
        const float out = buffer[(size_t) readPos];
        if (++writePos == size)
            writePos = 0;
        return out;
    }

private:
    std::vector<float> buffer;
    int writePos = 0;
};

} // namespace bt
