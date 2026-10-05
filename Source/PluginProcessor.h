#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TriggerEngine.h"

#include <array>
#include <atomic>

//==============================================================================
// Eight detection bands. Each band has its own filter, detector and MIDI note,
// so one instance can trigger a whole kit into one Drum Rack.
namespace bands
{
    constexpr int count = 8;

    struct Preset
    {
        const char* name;
        float freq, width, threshold;
        int note;      // General MIDI drum map (Ableton: 36 = C1)
        bool on;
    };

    inline constexpr Preset presets[count] = {
        { "Kick",       60.0f,    1.2f, -18.0f, 36, true  },
        { "Snare",      2000.0f,  1.0f, -30.0f, 38, true  },
        { "Closed Hat", 10000.0f, 1.5f, -30.0f, 42, true  },
        { "Open Hat",   7000.0f,  1.2f, -30.0f, 46, false },
        { "Low Tom",    90.0f,    0.6f, -24.0f, 45, false },
        { "Mid Tom",    140.0f,   0.6f, -24.0f, 47, false },
        { "High Tom",   200.0f,   0.6f, -24.0f, 50, false },
        { "Crash",      5000.0f,  1.5f, -30.0f, 49, false },
    };

    // Per-band parameter IDs: "b1_freq", "b2_note", ...
    inline juce::String id (int band, const char* param) { return "b" + juce::String (band + 1) + "_" + param; }

    inline constexpr const char* on     = "on";
    inline constexpr const char* freq   = "freq";
    inline constexpr const char* width  = "width";
    inline constexpr const char* thresh = "thresh";
    inline constexpr const char* retrig = "retrig";
    inline constexpr const char* sens   = "sens";
    inline constexpr const char* note   = "note";
}

namespace ParamID
{
    inline constexpr const char* lookahead = "lookahead";
    inline constexpr const char* channel   = "channel";
    inline constexpr const char* solo      = "solo";
    inline constexpr const char* bypass    = "bypass";
}

//==============================================================================
class BandTriggerProcessor : public juce::AudioProcessor
{
public:
    BandTriggerProcessor();
    ~BandTriggerProcessor() override = default;

    //--- AudioProcessor ---------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return isInstrument; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Instrument build: tell VST3 hosts the only audio input is a sidechain
    // (aux) bus, so Ableton etc. show a sidechain selector for it.
    struct Vst3Extensions : juce::VST3ClientExtensions
    {
        bool getPluginHasMainInput() const override { return ! isInstrument; }
    };
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &vst3Extensions; }

   #if JucePlugin_IsSynth
    static constexpr bool isInstrument = true;   // sits on a MIDI track, hears the drums via sidechain
   #else
    static constexpr bool isInstrument = false;  // insert effect on the drum audio track
   #endif

    //--- shared with the editor -----------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

    // The band being edited. Also the one "Solo band" listens to.
    int getSelectedBand() const noexcept { return selectedBand.load(); }
    void setSelectedBand (int band);

    juce::String getBandName (int band) const;
    void setBandName (int band, const juce::String& name);

    float getBandValue (int band, const char* param) const;
    bool isBandOn (int band) const { return getBandValue (band, bands::on) > 0.5f; }

    // Raw input audio (mono) for the spectrum display.
    static constexpr int spectrumFifoSize = 16384;
    int readSpectrumSamples (float* dest, int maxNum);

    // Envelope history for every band: one frame every ~4 ms.
    struct EnvFrame
    {
        std::array<float, bands::count> db;
        uint8_t hits; // bit n set = band n sent a note in this frame
    };
    static constexpr int envFifoSize = 4096;
    int readEnvelopeFrames (EnvFrame* dest, int maxNum);

    std::atomic<int> hitCounters[bands::count] {};
    std::atomic<int> lastVelocities[bands::count] {};

    // "Learn from hit": arm, the next strong broadband transient is captured
    // and the editor moves the selected band to its loudest frequency.
    static constexpr int learnSize = 2048;
    void armLearn() noexcept { learnReady.store (false); learnArmed.store (true); }
    bool isLearnArmed() const noexcept { return learnArmed.load(); }
    bool takeLearnCapture (std::array<float, learnSize>& dest);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();

    void pushSpectrum (const float* data, int num);
    void pushEnvFrame (const EnvFrame& f);
    void processLearn (float mono);

    struct BandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* width = nullptr;
        std::atomic<float>* thresh = nullptr;
        std::atomic<float>* retrig = nullptr;
        std::atomic<float>* sens = nullptr;
        std::atomic<float>* note = nullptr;
    };
    std::array<BandParams, bands::count> bandParams;

    std::atomic<float>* lookaheadParam = nullptr;
    std::atomic<float>* channelParam = nullptr;
    std::atomic<float>* soloParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    Vst3Extensions vst3Extensions;

    std::array<bt::TriggerEngine, bands::count> engines;
    std::array<int, bands::count> lastSeenHits {};
    std::array<bt::DelayLine, 2> audioDelays;
    bt::DelayLine soloDelay;
    static constexpr double maxLookaheadMs = 30.0;

    std::atomic<int> selectedBand { 0 };
    std::atomic<double> currentSampleRate { 44100.0 };
    int64_t sampleClock = 0;
    int currentLatency = -1;

    // spectrum FIFO
    juce::AbstractFifo spectrumFifo { spectrumFifoSize };
    std::vector<float> spectrumData = std::vector<float> ((size_t) spectrumFifoSize);
    std::array<float, 256> spectrumScratch {};
    int spectrumScratchCount = 0;

    // envelope FIFO
    juce::AbstractFifo envFifo { envFifoSize };
    std::vector<EnvFrame> envData = std::vector<EnvFrame> ((size_t) envFifoSize);
    int envChunkSize = 176, envChunkCount = 0;
    EnvFrame envChunk {};

    // learn
    std::atomic<bool> learnArmed { false }, learnReady { false };
    std::array<float, learnSize> learnBuffer {};
    int learnPos = -1;
    float learnFast = 0.0f, learnSlow = 0.0f, learnFastAtt = 0.1f, learnFastRel = 0.01f,
          learnSlowAtt = 0.001f, learnSlowRel = 0.0001f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandTriggerProcessor)
};
