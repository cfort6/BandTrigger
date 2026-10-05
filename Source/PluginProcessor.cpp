#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
static juce::String formatFrequency (float hz)
{
    return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz"
                        : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
}

static juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float v) { return s * std::pow (e / s, v); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}

juce::AudioProcessorValueTreeState::ParameterLayout BandTriggerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::freq, 1 }, "Frequency", logRange (20.0f, 20000.0f), 60.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatFrequency (v); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::width, 1 }, "Width", NormalisableRange<float> (0.1f, 4.0f, 0.01f), 1.2f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " oct"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::threshold, 1 }, "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    NormalisableRange<float> retrigRange (10.0f, 500.0f, 1.0f);
    retrigRange.setSkewForCentre (80.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::retrigger, 1 }, "Retrigger", retrigRange, 50.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::sensitivity, 1 }, "Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 70.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::lookahead, 1 }, "Lookahead", NormalisableRange<float> (0.0f, 30.0f, 0.1f), 15.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; })));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { ParamID::note, 1 }, "MIDI Note", 0, 127, 36,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
            // Ableton / Drum Rack naming: note 36 = C1
            return MidiMessage::getMidiNoteName (v, true, true, 3) + " " + String (v);
        })));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ParamID::channel, 1 }, "MIDI Channel", 1, 16, 10));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::solo, 1 }, "Solo Band", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::bypass, 1 }, "Bypass", false));
    return layout;
}

//==============================================================================
juce::AudioProcessor::BusesProperties BandTriggerProcessor::makeBuses()
{
    if (isInstrument)
        return BusesProperties()
            .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

BandTriggerProcessor::BandTriggerProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "BandTrigger", createLayout())
{
    freqParam        = apvts.getRawParameterValue (ParamID::freq);
    widthParam       = apvts.getRawParameterValue (ParamID::width);
    thresholdParam   = apvts.getRawParameterValue (ParamID::threshold);
    retriggerParam   = apvts.getRawParameterValue (ParamID::retrigger);
    sensitivityParam = apvts.getRawParameterValue (ParamID::sensitivity);
    lookaheadParam   = apvts.getRawParameterValue (ParamID::lookahead);
    noteParam        = apvts.getRawParameterValue (ParamID::note);
    channelParam     = apvts.getRawParameterValue (ParamID::channel);
    soloParam        = apvts.getRawParameterValue (ParamID::solo);
    bypassParam      = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamID::bypass));

    if (! apvts.state.hasProperty ("name"))
        apvts.state.setProperty ("name", "Kick", nullptr);
}

juce::AudioProcessorParameter* BandTriggerProcessor::getBypassParameter() const { return bypassParam; }

bool BandTriggerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (isInstrument) // sidechain may be mono, stereo or switched off
        return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();

    return in == out;
}

void BandTriggerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    engine.prepare (sampleRate);

    const int maxDelay = (int) std::ceil (maxLookaheadMs * 0.001 * sampleRate) + 1;
    for (auto& d : audioDelays)
        d.prepare (maxDelay);
    soloDelay.prepare (maxDelay);

    currentLatency = -1;
    sampleClock = 0;

    envChunkSize = juce::jmax (1, juce::roundToInt (sampleRate * 0.004));
    envChunkCount = 0;
    envChunkMax = -120.0f;
    envChunkHit = false;
    lastSeenHits = engine.getHitCount();

    auto coeff = [sampleRate] (double seconds) { return 1.0f - (float) std::exp (-1.0 / (seconds * sampleRate)); };
    learnFastAtt = coeff (0.0005);
    learnFastRel = coeff (0.020);
    learnSlowAtt = coeff (0.100);
    learnSlowRel = coeff (0.300);
    learnFast = learnSlow = 0.0f;
    learnPos = -1;
}

//==============================================================================
void BandTriggerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // The instrument has a MIDI input only because Live requires one. Drop
    // anything arriving on it so only the trigger notes go out.
    midi.clear();

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const double sr = currentSampleRate.load();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // Lookahead = latency reported to the host, which delays everything else
    // on the track to line up with our delayed audio and MIDI.
    const int lookahead = juce::roundToInt (lookaheadParam->load() * 0.001 * sr);
    if (lookahead != currentLatency)
    {
        currentLatency = lookahead;
        setLatencySamples (lookahead);
    }

    const bool bypassed = bypassParam != nullptr && bypassParam->get();
    const bool solo = soloParam->load() > 0.5f && ! bypassed;

    engine.setBand (freqParam->load(), widthParam->load());
    engine.setThresholdDb (thresholdParam->load());
    engine.setRetriggerMs (retriggerParam->load());
    engine.setSensitivity (sensitivityParam->load() * 0.01f);
    engine.setLookaheadSamples (lookahead);
    engine.setNote ((int) noteParam->load(), (int) channelParam->load());
    engine.setDetectionEnabled (! bypassed);

    const int channels = juce::jmin (numIn, (int) audioDelays.size());
    const float monoScale = numIn > 0 ? 1.0f / (float) numIn : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numIn; ++ch)
            mono += buffer.getSample (ch, i);
        mono *= monoScale;

        const float band = engine.processSample (mono, sampleClock + i);
        const float bandDelayed = soloDelay.process (band, lookahead);

        if (isInstrument)
        {
            // Instrument: output is silent unless soloing the band.
            for (int ch = 0; ch < numOut; ++ch)
                buffer.setSample (ch, i, solo ? bandDelayed : 0.0f);
        }
        else
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const float delayed = audioDelays[(size_t) ch].process (buffer.getSample (ch, i), lookahead);
                buffer.setSample (ch, i, solo ? bandDelayed : delayed);
            }
        }

        // --- GUI feeds -------------------------------------------------------
        spectrumScratch[(size_t) spectrumScratchCount++] = mono;
        if (spectrumScratchCount == (int) spectrumScratch.size())
        {
            pushSpectrum (spectrumScratch.data(), spectrumScratchCount);
            spectrumScratchCount = 0;
        }

        envChunkMax = juce::jmax (envChunkMax, engine.getEnvelopeDb());
        if (engine.getHitCount() != lastSeenHits)
        {
            lastSeenHits = engine.getHitCount();
            envChunkHit = true;
            lastVelocity.store (engine.getLastVelocity());
            hitCounter.fetch_add (1);
        }
        if (++envChunkCount >= envChunkSize)
        {
            pushEnvPoint ({ envChunkMax, envChunkHit });
            envChunkCount = 0;
            envChunkMax = -120.0f;
            envChunkHit = false;
        }

        if (learnArmed.load (std::memory_order_relaxed))
            processLearn (mono);
    }

    // Emit every note scheduled inside this block at its exact sample offset.
    engine.popEventsBefore (sampleClock + numSamples, [&] (const bt::NoteEvent& e) {
        const int offset = juce::jlimit (0, juce::jmax (0, numSamples - 1), (int) (e.time - sampleClock));
        if (e.velocity > 0)
            midi.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity), offset);
        else
            midi.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), offset);
    });

    sampleClock += numSamples;
}

//==============================================================================
void BandTriggerProcessor::processLearn (float mono)
{
    const float r = std::abs (mono);
    learnFast += (r > learnFast ? learnFastAtt : learnFastRel) * (r - learnFast);
    learnSlow += (r > learnSlow ? learnSlowAtt : learnSlowRel) * (r - learnSlow);

    if (learnPos < 0)
    {
        // A transient: fast envelope jumps well above the slow one (~10 dB) and
        // above -40 dBFS.
        if (learnFast > 0.01f && learnFast > 3.0f * learnSlow)
            learnPos = 0;
        else
            return;
    }

    learnBuffer[(size_t) learnPos++] = mono;
    if (learnPos >= learnSize)
    {
        learnPos = -1;
        learnArmed.store (false);
        learnReady.store (true, std::memory_order_release);
    }
}

bool BandTriggerProcessor::takeLearnCapture (std::array<float, learnSize>& dest)
{
    if (! learnReady.load (std::memory_order_acquire))
        return false;
    dest = learnBuffer;
    learnReady.store (false);
    return true;
}

//==============================================================================
void BandTriggerProcessor::pushSpectrum (const float* data, int num)
{
    const auto scope = spectrumFifo.write (num);
    if (scope.blockSize1 > 0) std::copy (data, data + scope.blockSize1, spectrumData.begin() + scope.startIndex1);
    if (scope.blockSize2 > 0) std::copy (data + scope.blockSize1, data + scope.blockSize1 + scope.blockSize2,
                                         spectrumData.begin() + scope.startIndex2);
}

int BandTriggerProcessor::readSpectrumSamples (float* dest, int maxNum)
{
    const auto scope = spectrumFifo.read (juce::jmin (maxNum, spectrumFifo.getNumReady()));
    if (scope.blockSize1 > 0) std::copy_n (spectrumData.begin() + scope.startIndex1, scope.blockSize1, dest);
    if (scope.blockSize2 > 0) std::copy_n (spectrumData.begin() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
    return scope.blockSize1 + scope.blockSize2;
}

void BandTriggerProcessor::pushEnvPoint (EnvPoint p)
{
    const auto scope = envFifo.write (1);
    if (scope.blockSize1 > 0) envData[(size_t) scope.startIndex1] = p;
    else if (scope.blockSize2 > 0) envData[(size_t) scope.startIndex2] = p;
}

int BandTriggerProcessor::readEnvelopePoints (EnvPoint* dest, int maxNum)
{
    const auto scope = envFifo.read (juce::jmin (maxNum, envFifo.getNumReady()));
    if (scope.blockSize1 > 0) std::copy_n (envData.begin() + scope.startIndex1, scope.blockSize1, dest);
    if (scope.blockSize2 > 0) std::copy_n (envData.begin() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
    return scope.blockSize1 + scope.blockSize2;
}

//==============================================================================
juce::String BandTriggerProcessor::getInstanceName() const
{
    return apvts.state.getProperty ("name", "Kick").toString();
}

void BandTriggerProcessor::setInstanceName (const juce::String& name)
{
    apvts.state.setProperty ("name", name, nullptr);
}

void BandTriggerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BandTriggerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BandTriggerProcessor::createEditor() { return new BandTriggerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BandTriggerProcessor(); }
