#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <deque>

namespace ui
{
namespace colours
{
    const juce::Colour ground   { 0xff121417 };
    const juce::Colour card     { 0xff1a1d21 };
    const juce::Colour cardEdge { 0xff262a30 };
    const juce::Colour control  { 0xff23272c };
    const juce::Colour grid     { 0xff262a30 };
    const juce::Colour track    { 0xff2c3036 };
    const juce::Colour text     { 0xffe6e8ea };
    const juce::Colour muted    { 0xff8a9099 };
    const juce::Colour spectrum { 0xff9aa3ae };
    const juce::Colour envelope { 0xffc9d0d8 };
    const juce::Colour accent   { 0xfff2a23a };
}

juce::Font sansFont (float height, bool bold = false);
juce::Font monoFont (float height, bool bold = false);

//==============================================================================
class BandTriggerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BandTriggerLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return sansFont (13.0f, true); }
};

//==============================================================================
// Rotary knob with its value above the name, as in the mockup.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// "−  C1 36  +" style stepper for integer parameters.
class Stepper : public juce::Component
{
public:
    Stepper (juce::RangedAudioParameter&, const juce::String& title);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() { repaint(); }

private:
    void step (int delta);

    juce::RangedAudioParameter& param;
    juce::String title;
    juce::TextButton down { juce::String::fromUTF8 ("\xe2\x88\x92") }, up { "+" };
};

//==============================================================================
// Log-frequency spectrum with the draggable detection band.
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (BandTriggerProcessor&);

    void setSpectrum (const float* binDb, int numBins, int fftSize, double sampleRate);
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr float minHz = 20.0f, maxHz = 20000.0f, minDb = -90.0f, maxDb = 0.0f;

private:
    enum class Drag { none, centre, low, high };

    juce::Rectangle<float> plotArea() const;
    float xForFreq (float hz) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    Drag hitTestBand (float x) const;
    void setParam (const char* id, float value);

    BandTriggerProcessor& processor;
    juce::RangedAudioParameter& freqParam;
    juce::RangedAudioParameter& widthParam;

    std::vector<float> pixelDb; // smoothed spectrum, one value per pixel column
    Drag drag = Drag::none;
    float dragStartCentre = 60.0f, dragStartFreq = 60.0f;
};

//==============================================================================
// Scrolling band envelope with threshold line and hit markers.
class EnvelopeView : public juce::Component
{
public:
    explicit EnvelopeView (BandTriggerProcessor&);
    void addPoints (const BandTriggerProcessor::EnvPoint* pts, int num);
    void paint (juce::Graphics&) override;

private:
    BandTriggerProcessor& processor;
    std::deque<BandTriggerProcessor::EnvPoint> points;
};

} // namespace ui

//==============================================================================
class BandTriggerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BandTriggerEditor (BandTriggerProcessor&);
    ~BandTriggerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateSpectrum();
    void finishLearn (const std::array<float, BandTriggerProcessor::learnSize>& capture);

    BandTriggerProcessor& owner;
    ui::BandTriggerLookAndFeel lnf;

    // header
    juce::TextEditor nameEditor;
    juce::TextButton soloButton { "Solo band" }, bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment, bypassAttachment;

    // main
    ui::SpectrumView spectrum;
    ui::EnvelopeView envelope;
    ui::Stepper noteStepper, channelStepper;
    juce::TextButton learnButton { "Learn from hit" };

    // knobs
    juce::OwnedArray<ui::Knob> knobs;

    // spectrum analysis
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> analysisRing = std::vector<float> ((size_t) fftSize, 0.0f);
    int analysisWritePos = 0;
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> binDb = std::vector<float> ((size_t) fftSize / 2, -120.0f);
    std::vector<float> readScratch = std::vector<float> ((size_t) BandTriggerProcessor::spectrumFifoSize);

    // hit light
    int lastHitCount = 0;
    float hitGlow = 0.0f;
    juce::Rectangle<int> hitCard, midiCard, spectrumCard, envelopeCard, knobCard, lightArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandTriggerEditor)
};
