// Stand-alone test for TriggerEngine (no JUCE needed).
// Builds a synthetic drum loop (kick, snare, hi-hat at known times) and checks
// that a kick-band engine and a snare-band engine each fire on the right hits.
//
//   g++ -std=c++17 -O2 -I../Source EngineTest.cpp -o EngineTest && ./EngineTest

#include "TriggerEngine.h"

#include <cstdio>
#include <random>
#include <vector>

static constexpr double fs = 44100.0;

struct Hit { double timeSec; float gain; };

static void addKick (std::vector<float>& buf, double t0, float gain)
{
    double phase = 0;
    const int start = (int) (t0 * fs);
    for (int i = 0; i < (int) (0.4 * fs) && start + i < (int) buf.size(); ++i)
    {
        const double t = i / fs;
        const double f = 50.0 + 70.0 * std::exp (-t * 40.0); // 120 Hz -> 50 Hz sweep
        phase += 2.0 * bt::pi * f / fs;
        buf[(size_t) (start + i)] += gain * (float) (std::sin (phase) * std::exp (-t * 9.0));
    }
}

static void addSnare (std::vector<float>& buf, double t0, float gain, std::mt19937& rng)
{
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    const int start = (int) (t0 * fs);
    for (int i = 0; i < (int) (0.25 * fs) && start + i < (int) buf.size(); ++i)
    {
        const double t = i / fs;
        const float body = (float) (std::sin (2.0 * bt::pi * 190.0 * t) * std::exp (-t * 25.0));
        const float rattle = noise (rng) * (float) std::exp (-t * 18.0);
        buf[(size_t) (start + i)] += gain * (0.6f * body + 0.5f * rattle);
    }
}

static void addHat (std::vector<float>& buf, double t0, float gain, std::mt19937& rng)
{
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    bt::Biquad hp;
    hp.setHighPass (fs, 7000.0, 0.7071);
    const int start = (int) (t0 * fs);
    for (int i = 0; i < (int) (0.06 * fs) && start + i < (int) buf.size(); ++i)
    {
        const double t = i / fs;
        buf[(size_t) (start + i)] += gain * hp.process (noise (rng)) * (float) std::exp (-t * 70.0);
    }
}

static int runEngine (const char* name, const std::vector<float>& audio, double centre, double width,
                      float thresholdDb, int lookahead, const std::vector<Hit>& expected)
{
    bt::TriggerEngine eng;
    eng.prepare (fs);
    eng.setBand (centre, width);
    eng.setThresholdDb (thresholdDb);
    eng.setRetriggerMs (50.0f);
    eng.setSensitivity (0.7f);
    eng.setLookaheadSamples (lookahead);
    eng.setNote (36, 10);

    std::vector<bt::NoteEvent> got;
    const int block = 512;
    for (int64_t pos = 0; pos < (int64_t) audio.size(); pos += block)
    {
        const int n = (int) std::min<int64_t> (block, (int64_t) audio.size() - pos);
        for (int i = 0; i < n; ++i)
            eng.processSample (audio[(size_t) (pos + i)], pos + i);
        eng.popEventsBefore (pos + n, [&] (const bt::NoteEvent& e) {
            if (e.velocity > 0)
            {
                if (e.time < pos) std::printf ("  !! event scheduled in the past\n");
                got.push_back (e);
            }
        });
    }

    std::printf ("%s: filter delay %.1f ms. Expected %zu hits, got %zu\n", name, eng.getFilterDelayMs(),
                 expected.size(), got.size());
    int failures = 0;
    if (got.size() != expected.size())
        ++failures;

    for (size_t i = 0; i < std::min (got.size(), expected.size()); ++i)
    {
        // The note should land `lookahead` samples after the true transient,
        // which is exactly where the host puts it after latency compensation.
        const double errMs = ((double) (got[i].time - lookahead) / fs - expected[i].timeSec) * 1000.0;
        std::printf ("  hit %zu: t=%.3fs  timing error %+.2f ms  vel %d\n", i,
                     (double) (got[i].time - lookahead) / fs, errMs, got[i].velocity);
        if (std::abs (errMs) > 3.0)
            ++failures;
    }
    return failures;
}

int main()
{
    std::mt19937 rng (1234);
    std::vector<float> audio ((size_t) (4.0 * fs), 0.0f);

    // 120 BPM: kick on 1 and 3 (plus a ghost), snare on 2 and 4, hats on 8ths.
    std::vector<Hit> kicks  { { 0.0, 0.9f }, { 1.0, 0.9f }, { 1.75, 0.45f }, { 2.0, 0.9f }, { 3.0, 0.7f } };
    std::vector<Hit> snares { { 0.5, 0.8f }, { 1.5, 0.8f }, { 2.5, 0.8f }, { 3.5, 0.5f } };

    for (auto& k : kicks)  addKick (audio, k.timeSec, k.gain);
    for (auto& s : snares) addSnare (audio, s.timeSec, s.gain, rng);
    for (int i = 0; i < 16; ++i) addHat (audio, i * 0.25, 0.3f, rng);

    const int lookahead = (int) (0.015 * fs); // 15 ms (the plugin default)
    int failures = 0;
    failures += runEngine ("Kick band (60 Hz, 1.2 oct, -18 dB)", audio, 60.0, 1.2, -18.0f, lookahead, kicks);
    failures += runEngine ("Snare band (1.5 kHz, 1 oct, -30 dB)", audio, 1500.0, 1.0, -30.0f, lookahead, snares);

    std::vector<Hit> hats;
    for (int i = 0; i < 16; ++i) hats.push_back ({ i * 0.25, 0.3f });
    failures += runEngine ("Hat band (10 kHz, 1.5 oct, -30 dB)", audio, 10000.0, 1.5, -30.0f, lookahead, hats);

    // Velocity mapping sanity checks
    if (bt::TriggerEngine::velocityFor (-18.0f, -18.0f, 0.0f) != 127) ++failures;
    if (bt::TriggerEngine::velocityFor (12.0f, -18.0f, 1.0f) != 127) ++failures;
    if (bt::TriggerEngine::velocityFor (-18.0f, -18.0f, 1.0f) != 1) ++failures;

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
