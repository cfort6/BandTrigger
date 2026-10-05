#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <deque>
#include <functional>

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

    // One colour per band, similar brightness so none dominates.
    const juce::Colour band[bands::count] = {
        juce::Colour (0xfff2a23a), // amber
        juce::Colour (0xff4fb3e8), // sky
        juce::Colour (0xff6fd08c), // green
        juce::Colour (0xffe86f9a), // pink
        juce::Colour (0xffa98bf0), // violet
        juce::Colour (0xff4fd1c5), // teal
        juce::Colour (0xfff07a5a), // coral
        juce::Colour (0xffe8d45a), // yellow
    };
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

    juce::Colour accent = colours::band[0];
};

//==============================================================================
// Rotary knob with its value above the name. Can be re-pointed at another
// band's parameter when the selected band changes.
class Knob : public juce::Component
{
public:
    explicit Knob (const juce::String& name);
    void bind (juce::AudioProcessorValueTreeState&, const juce::String& paramId);
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
    explicit Stepper (const juce::String& title);
    void setParameter (juce::RangedAudioParameter* p) { param = p; repaint(); }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void step (int delta);

    juce::RangedAudioParameter* param = nullptr;
    juce::String title;
    juce::TextButton down { juce::String::fromUTF8 ("\xe2\x88\x92") }, up { "+" };
};

//==============================================================================
// One of the 8 band selector buttons along the top. Flashes on each hit.
class BandButton : public juce::Button
{
public:
    explicit BandButton (int index);
    void update (const juce::String& name, const juce::String& noteText, bool on, bool selected, float glow);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    int index;
    juce::String name, noteText;
    bool on = true, selected = false;
    float glow = 0.0f;
};

//==============================================================================
// Log-frequency spectrum showing every band. Click a band's centre line to
// select it; drag left/right to move the selected band, up/down to widen or
// narrow it.
class SpectrumView : public juce::Component
{
public:
    SpectrumView (BandTriggerProcessor&, std::function<void (int)> onSelectBand);

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
    static constexpr float pixelsPerOctaveOfWidth = 60.0f; // vertical drag sensitivity

private:
    juce::Rectangle<float> plotArea() const;
    float xForFreq (float hz) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    int bandAtX (float x) const;
    juce::RangedAudioParameter& param (int band, const char* name) const;
    void setParam (int band, const char* name, float value);
    void paintBand (juce::Graphics&, int band, bool selected) const;

    BandTriggerProcessor& processor;
    std::function<void (int)> onSelectBand;

    std::vector<float> pixelDb; // smoothed spectrum, one value per pixel column

    bool dragging = false;
    int dragBand = 0;
    juce::Point<float> dragStart;
    float dragStartFreq = 60.0f, dragStartWidth = 1.0f;
};

//==============================================================================
// Scrolling envelope of the selected band, with threshold and hit markers.
class EnvelopeView : public juce::Component
{
public:
    explicit EnvelopeView (BandTriggerProcessor&);
    void addFrames (const BandTriggerProcessor::EnvFrame* frames, int num);
    void paint (juce::Graphics&) override;

private:
    BandTriggerProcessor& processor;
    std::deque<BandTriggerProcessor::EnvFrame> frames;
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

    void selectBand (int band);

private:
    void timerCallback() override;
    void bindSelectedBand();
    void updateBandButtons();
    void updateSpectrum();
    void finishLearn (const std::array<float, BandTriggerProcessor::learnSize>& capture);

    BandTriggerProcessor& owner;
    ui::BandTriggerLookAndFeel lnf;
    int shownBand = -1;

    // header
    juce::TextButton soloButton { "Solo band" }, bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment, bypassAttachment;

    // band selector row
    juce::OwnedArray<ui::BandButton> bandButtons;
    std::array<float, bands::count> glow {};
    std::array<int, bands::count> lastHitCounts {};

    // main
    ui::SpectrumView spectrum;
    ui::EnvelopeView envelope;

    // selected band panel
    juce::TextEditor nameEditor;
    juce::TextButton onButton { "Band on" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
    ui::Stepper noteStepper { "Note" };
    juce::TextButton learnButton { "Learn from hit" };

    // global MIDI
    ui::Stepper channelStepper { "Channel (all bands)" };

    // knobs: 5 per-band + lookahead (global)
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

    juce::Rectangle<int> bandRow, spectrumCard, envelopeCard, bandCard, midiCard, knobCard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandTriggerEditor)
};
