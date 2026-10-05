#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TriggerEngine.h"

#include <array>
#include <atomic>

namespace ParamID
{
    inline constexpr const char* freq        = "freq";
    inline constexpr const char* width       = "width";
    inline constexpr const char* threshold   = "threshold";
    inline constexpr const char* retrigger   = "retrigger";
    inline constexpr const char* sensitivity = "sensitivity";
    inline constexpr const char* lookahead   = "lookahead";
    inline constexpr const char* note        = "note";
    inline constexpr const char* channel     = "channel";
    inline constexpr const char* solo        = "solo";
    inline constexpr const char* bypass      = "bypass";
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

    // Instrument build: tell VST3 hosts the only input is a sidechain (aux)
    // bus, so Ableton etc. show a sidechain selector for it.
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

    // Raw input audio (mono) for the spectrum display.
    static constexpr int spectrumFifoSize = 16384;
    int readSpectrumSamples (float* dest, int maxNum);

    // Band envelope history: one point every ~4 ms.
    struct EnvPoint { float db; bool hit; };
    static constexpr int envFifoSize = 4096;
    int readEnvelopePoints (EnvPoint* dest, int maxNum);

    std::atomic<int> hitCounter { 0 };
    std::atomic<int> lastVelocity { 0 };

    // "Learn from hit": arm, the next strong broadband transient is captured
    // and the editor picks the band from its spectrum.
    static constexpr int learnSize = 2048;
    void armLearn() noexcept { learnReady.store (false); learnArmed.store (true); }
    bool isLearnArmed() const noexcept { return learnArmed.load(); }
    bool takeLearnCapture (std::array<float, learnSize>& dest);

    juce::String getInstanceName() const;
    void setInstanceName (const juce::String& name);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void pushSpectrum (const float* data, int num);
    void pushEnvPoint (EnvPoint p);
    void processLearn (float mono);

    std::atomic<float>* freqParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* retriggerParam = nullptr;
    std::atomic<float>* sensitivityParam = nullptr;
    std::atomic<float>* lookaheadParam = nullptr;
    std::atomic<float>* noteParam = nullptr;
    std::atomic<float>* channelParam = nullptr;
    std::atomic<float>* soloParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    Vst3Extensions vst3Extensions;
    static BusesProperties makeBuses();

    bt::TriggerEngine engine;
    std::array<bt::DelayLine, 2> audioDelays;
    bt::DelayLine soloDelay;
    static constexpr double maxLookaheadMs = 30.0;

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
    std::vector<EnvPoint> envData = std::vector<EnvPoint> ((size_t) envFifoSize);
    int envChunkSize = 176, envChunkCount = 0;
    float envChunkMax = -120.0f;
    bool envChunkHit = false;
    int lastSeenHits = 0;

    // learn
    std::atomic<bool> learnArmed { false }, learnReady { false };
    std::array<float, learnSize> learnBuffer {};
    int learnPos = -1;
    float learnFast = 0.0f, learnSlow = 0.0f, learnFastAtt = 0.1f, learnFastRel = 0.01f,
          learnSlowAtt = 0.001f, learnSlowRel = 0.0001f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandTriggerProcessor)
};
