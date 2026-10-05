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

    for (int b = 0; b < bands::count; ++b)
    {
        const auto& p = bands::presets[b];
        const String prefix = "Band " + String (b + 1) + " ";
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), " | ");

        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { bands::id (b, bands::on), 1 }, prefix + "On", p.on));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { bands::id (b, bands::freq), 1 }, prefix + "Frequency", logRange (20.0f, 20000.0f), p.freq,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatFrequency (v); })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { bands::id (b, bands::width), 1 }, prefix + "Width", NormalisableRange<float> (0.1f, 4.0f, 0.01f), p.width,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " oct"; })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { bands::id (b, bands::thresh), 1 }, prefix + "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), p.threshold,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

        NormalisableRange<float> retrigRange (10.0f, 500.0f, 1.0f);
        retrigRange.setSkewForCentre (80.0f);
        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { bands::id (b, bands::retrig), 1 }, prefix + "Retrigger", retrigRange, 50.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));

        group->addChild (std::make_unique<AudioParameterFloat> (
            ParameterID { bands::id (b, bands::sens), 1 }, prefix + "Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 70.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })));

        group->addChild (std::make_unique<AudioParameterInt> (
            ParameterID { bands::id (b, bands::note), 1 }, prefix + "MIDI Note", 0, 127, p.note,
            AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
                // Ableton / Drum Rack naming: note 36 = C1
                return MidiMessage::getMidiNoteName (v, true, true, 3) + " " + String (v);
            })));

        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamID::lookahead, 1 }, "Lookahead", NormalisableRange<float> (0.0f, 30.0f, 0.1f), 15.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; })));

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
    for (int b = 0; b < bands::count; ++b)
    {
        auto& bp = bandParams[(size_t) b];
        bp.on     = apvts.getRawParameterValue (bands::id (b, bands::on));
        bp.freq   = apvts.getRawParameterValue (bands::id (b, bands::freq));
        bp.width  = apvts.getRawParameterValue (bands::id (b, bands::width));
        bp.thresh = apvts.getRawParameterValue (bands::id (b, bands::thresh));
        bp.retrig = apvts.getRawParameterValue (bands::id (b, bands::retrig));
        bp.sens   = apvts.getRawParameterValue (bands::id (b, bands::sens));
        bp.note   = apvts.getRawParameterValue (bands::id (b, bands::note));

        const juce::Identifier key ("name" + juce::String (b + 1));
        if (! apvts.state.hasProperty (key))
            apvts.state.setProperty (key, bands::presets[b].name, nullptr);
    }

    lookaheadParam = apvts.getRawParameterValue (ParamID::lookahead);
    channelParam   = apvts.getRawParameterValue (ParamID::channel);
    soloParam      = apvts.getRawParameterValue (ParamID::solo);
    bypassParam    = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamID::bypass));

    if (! apvts.state.hasProperty ("selected"))
        apvts.state.setProperty ("selected", 0, nullptr);
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
    for (size_t b = 0; b < engines.size(); ++b)
    {
        engines[b].prepare (sampleRate);
        lastSeenHits[b] = engines[b].getHitCount();
    }

    const int maxDelay = (int) std::ceil (maxLookaheadMs * 0.001 * sampleRate) + 1;
    for (auto& d : audioDelays)
        d.prepare (maxDelay);
    soloDelay.prepare (maxDelay);

    currentLatency = -1;
    sampleClock = 0;

    envChunkSize = juce::jmax (1, juce::roundToInt (sampleRate * 0.004));
    envChunkCount = 0;
    envChunk.db.fill (-120.0f);
    envChunk.hits = 0;

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
    const int soloBand = juce::jlimit (0, bands::count - 1, selectedBand.load (std::memory_order_relaxed));
    const int channel = (int) channelParam->load();

    for (size_t b = 0; b < engines.size(); ++b)
    {
        const auto& bp = bandParams[b];
        auto& e = engines[b];
        e.setBand (bp.freq->load(), bp.width->load());
        e.setThresholdDb (bp.thresh->load());
        e.setRetriggerMs (bp.retrig->load());
        e.setSensitivity (bp.sens->load() * 0.01f);
        e.setLookaheadSamples (lookahead);
        e.setNote ((int) bp.note->load(), channel);
        e.setDetectionEnabled (! bypassed && bp.on->load() > 0.5f);
    }

    const int channels = juce::jmin (numIn, (int) audioDelays.size());
    const float monoScale = numIn > 0 ? 1.0f / (float) numIn : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numIn; ++ch)
            mono += buffer.getSample (ch, i);
        mono *= monoScale;

        // Every band runs, even switched-off ones, so their envelopes stay
        // live in the GUI while you set them up. They just don't send notes.
        float soloSample = 0.0f;
        for (size_t b = 0; b < engines.size(); ++b)
        {
            const float y = engines[b].processSample (mono, sampleClock + i);
            if ((int) b == soloBand)
                soloSample = y;

            envChunk.db[b] = juce::jmax (envChunk.db[b], engines[b].getEnvelopeDb());
            if (engines[b].getHitCount() != lastSeenHits[b])
            {
                lastSeenHits[b] = engines[b].getHitCount();
                envChunk.hits |= (uint8_t) (1u << b);
                lastVelocities[b].store (engines[b].getLastVelocity(), std::memory_order_relaxed);
                hitCounters[b].fetch_add (1, std::memory_order_relaxed);
            }
        }

        const float soloDelayed = soloDelay.process (soloSample, lookahead);

        if (isInstrument)
        {
            // Instrument: output is silent unless soloing the band.
            for (int ch = 0; ch < numOut; ++ch)
                buffer.setSample (ch, i, solo ? soloDelayed : 0.0f);
        }
        else
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const float delayed = audioDelays[(size_t) ch].process (buffer.getSample (ch, i), lookahead);
                buffer.setSample (ch, i, solo ? soloDelayed : delayed);
            }
        }

        // --- GUI feeds -------------------------------------------------------
        spectrumScratch[(size_t) spectrumScratchCount++] = mono;
        if (spectrumScratchCount == (int) spectrumScratch.size())
        {
            pushSpectrum (spectrumScratch.data(), spectrumScratchCount);
            spectrumScratchCount = 0;
        }

        if (++envChunkCount >= envChunkSize)
        {
            pushEnvFrame (envChunk);
            envChunkCount = 0;
            envChunk.db.fill (-120.0f);
            envChunk.hits = 0;
        }

        if (learnArmed.load (std::memory_order_relaxed))
            processLearn (mono);
    }

    // Emit every note scheduled inside this block at its exact sample offset.
    for (auto& e : engines)
    {
        e.popEventsBefore (sampleClock + numSamples, [&] (const bt::NoteEvent& ev) {
            const int offset = juce::jlimit (0, juce::jmax (0, numSamples - 1), (int) (ev.time - sampleClock));
            if (ev.velocity > 0)
                midi.addEvent (juce::MidiMessage::noteOn (ev.channel, ev.note, (juce::uint8) ev.velocity), offset);
            else
                midi.addEvent (juce::MidiMessage::noteOff (ev.channel, ev.note), offset);
        });
    }

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

void BandTriggerProcessor::pushEnvFrame (const EnvFrame& f)
{
    const auto scope = envFifo.write (1);
    if (scope.blockSize1 > 0) envData[(size_t) scope.startIndex1] = f;
    else if (scope.blockSize2 > 0) envData[(size_t) scope.startIndex2] = f;
}

int BandTriggerProcessor::readEnvelopeFrames (EnvFrame* dest, int maxNum)
{
    const auto scope = envFifo.read (juce::jmin (maxNum, envFifo.getNumReady()));
    if (scope.blockSize1 > 0) std::copy_n (envData.begin() + scope.startIndex1, scope.blockSize1, dest);
    if (scope.blockSize2 > 0) std::copy_n (envData.begin() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
    return scope.blockSize1 + scope.blockSize2;
}

//==============================================================================
void BandTriggerProcessor::setSelectedBand (int band)
{
    band = juce::jlimit (0, bands::count - 1, band);
    selectedBand.store (band);
    apvts.state.setProperty ("selected", band, nullptr);
}

juce::String BandTriggerProcessor::getBandName (int band) const
{
    return apvts.state.getProperty ("name" + juce::String (band + 1), bands::presets[band].name).toString();
}

void BandTriggerProcessor::setBandName (int band, const juce::String& name)
{
    apvts.state.setProperty ("name" + juce::String (band + 1), name, nullptr);
}

float BandTriggerProcessor::getBandValue (int band, const char* param) const
{
    if (auto* v = apvts.getRawParameterValue (bands::id (band, param)))
        return v->load();
    return 0.0f;
}

void BandTriggerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BandTriggerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            selectedBand.store (juce::jlimit (0, bands::count - 1, (int) apvts.state.getProperty ("selected", 0)));
        }
    }
}

juce::AudioProcessorEditor* BandTriggerProcessor::createEditor() { return new BandTriggerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BandTriggerProcessor(); }
