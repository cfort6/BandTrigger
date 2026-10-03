#include "PluginEditor.h"

using namespace juce;

namespace ui
{
//==============================================================================
Font sansFont (float height, bool bold)
{
    return Font (FontOptions (height, bold ? Font::bold : Font::plain));
}

Font monoFont (float height, bool bold)
{
    return Font (FontOptions (Font::getDefaultMonospacedFontName(), height, bold ? Font::bold : Font::plain));
}

//==============================================================================
BandTriggerLookAndFeel::BandTriggerLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, colours::ground);
    setColour (TextButton::buttonColourId, colours::control);
    setColour (TextButton::buttonOnColourId, colours::accent);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, colours::ground);
    setColour (TextEditor::backgroundColourId, Colour (0xff1d2024));
    setColour (TextEditor::outlineColourId, colours::track);
    setColour (TextEditor::focusedOutlineColourId, colours::accent);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::highlightColourId, colours::accent.withAlpha (0.35f));
    setColour (CaretComponent::caretColourId, colours::accent);
    setColour (Label::textColourId, colours::text);
}

void BandTriggerLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                               float startAngle, float endAngle, Slider&)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float lineW = 5.0f;
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const PathStrokeType stroke (lineW, PathStrokeType::curved, PathStrokeType::rounded);

    Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, stroke);

    if (pos > 0.001f)
    {
        Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (colours::accent);
        g.strokePath (value, stroke);
    }

    const float innerR = arcR - 8.0f;
    g.setColour (colours::cardEdge);
    g.fillEllipse (c.x - innerR, c.y - innerR, innerR * 2.0f, innerR * 2.0f);

    const Point<float> tip (c.x + std::sin (angle) * (innerR - 3.0f), c.y - std::cos (angle) * (innerR - 3.0f));
    g.setColour (colours::text);
    g.drawLine ({ c, tip }, 2.5f);
}

void BandTriggerLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& background,
                                                   bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = background;
    if (down)             fill = fill.brighter (0.15f);
    else if (highlighted) fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (b.getToggleState() ? colours::accent : colours::track);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& state, const String& paramId, const String& displayName)
    : name (displayName)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setTitle (displayName);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);

    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().removeFromTop (68).withSizeKeepingCentre (68, 68));
}

void Knob::paint (Graphics& g)
{
    auto r = getLocalBounds();
    r.removeFromTop (70);
    g.setColour (colours::text);
    g.setFont (monoFont (14.0f, true));
    g.drawText (slider.getTextFromValue (slider.getValue()), r.removeFromTop (20), Justification::centred);
    g.setColour (colours::muted);
    g.setFont (sansFont (11.0f));
    g.drawText (name.toUpperCase(), r.removeFromTop (16), Justification::centred);
}

//==============================================================================
Stepper::Stepper (RangedAudioParameter& p, const String& t) : param (p), title (t)
{
    down.setTitle (title + " down");
    up.setTitle (title + " up");
    down.onClick = [this] { step (-1); };
    up.onClick   = [this] { step (+1); };
    addAndMakeVisible (down);
    addAndMakeVisible (up);
}

void Stepper::step (int delta)
{
    const auto range = param.getNormalisableRange();
    const float current = param.convertFrom0to1 (param.getValue());
    const float next = jlimit (range.start, range.end, std::round (current) + (float) delta);
    param.beginChangeGesture();
    param.setValueNotifyingHost (param.convertTo0to1 (next));
    param.endChangeGesture();
    repaint();
}

void Stepper::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (22);
    down.setBounds (r.removeFromLeft (44).withHeight (44));
    up.setBounds (r.removeFromRight (44).withHeight (44));
}

void Stepper::paint (Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (colours::muted);
    g.setFont (sansFont (12.0f));
    g.drawText (title, r.removeFromTop (22), Justification::topLeft);
    g.setColour (colours::text);
    g.setFont (monoFont (18.0f, true));
    g.drawText (param.getCurrentValueAsText(), r.withHeight (44).reduced (44, 0), Justification::centred);
}

//==============================================================================
SpectrumView::SpectrumView (BandTriggerProcessor& p)
    : processor (p),
      freqParam (*p.apvts.getParameter (ParamID::freq)),
      widthParam (*p.apvts.getParameter (ParamID::width))
{
    setTitle ("Spectrum and detection band");
}

Rectangle<float> SpectrumView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (36.0f).withTrimmedBottom (20.0f).withTrimmedTop (4.0f);
}

float SpectrumView::xForFreq (float hz) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float SpectrumView::freqForX (float x) const
{
    const auto a = plotArea();
    const float t = jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth());
    return minHz * std::pow (maxHz / minHz, t);
}

float SpectrumView::yForDb (float db) const
{
    const auto a = plotArea();
    return jmap (jlimit (minDb, maxDb, db), maxDb, minDb, a.getY(), a.getBottom());
}

void SpectrumView::resized()
{
    pixelDb.assign ((size_t) jmax (1, (int) plotArea().getWidth()), minDb);
}

void SpectrumView::setSpectrum (const float* bins, int numBins, int fftSize, double sampleRate)
{
    const auto a = plotArea();
    const int w = (int) pixelDb.size();
    const double binHz = sampleRate / fftSize;

    for (int px = 0; px < w; ++px)
    {
        const double f0 = freqForX (a.getX() + (float) px);
        const double f1 = freqForX (a.getX() + (float) px + 1.0f);
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        float db;

        if (b1 - b0 < 1.0)
        {
            // Fewer than one bin per pixel (low end): interpolate.
            const double bc = 0.5 * (b0 + b1);
            const int i = jlimit (0, numBins - 2, (int) bc);
            const float frac = (float) (bc - i);
            db = bins[i] + frac * (bins[i + 1] - bins[i]);
        }
        else
        {
            // Many bins per pixel (high end): take the loudest.
            db = minDb - 30.0f;
            for (int i = jlimit (0, numBins - 1, (int) b0); i <= jlimit (0, numBins - 1, (int) b1); ++i)
                db = jmax (db, bins[i]);
        }

        // Fast rise, gentle fall so the display is readable.
        auto& shown = pixelDb[(size_t) px];
        shown = db > shown ? db : jmax (db, shown - 1.2f);
    }
    repaint();
}

void SpectrumView::paint (Graphics& g)
{
    const auto a = plotArea();
    const double sr = processor.getCurrentSampleRate();

    // grid
    g.setColour (colours::grid);
    for (float db = 0.0f; db >= minDb; db -= 18.0f)
        g.drawHorizontalLine ((int) yForDb (db), a.getX(), a.getRight());

    static const float gridHz[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    static const char* gridLabels[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    for (float hz : gridHz)
        g.drawVerticalLine ((int) xForFreq (hz), a.getY(), a.getBottom());

    // labels
    g.setColour (colours::muted);
    g.setFont (monoFont (11.0f));
    for (int i = 0; i < 10; ++i)
    {
        const float x = xForFreq (gridHz[i]);
        g.drawText (gridLabels[i], Rectangle<float> (x - 20.0f, a.getBottom() + 4.0f, 40.0f, 14.0f),
                    i == 9 ? Justification::centredRight : Justification::centred);
    }
    for (float db = 0.0f; db >= minDb; db -= 18.0f)
        g.drawText (String ((int) db), Rectangle<float> (0.0f, yForDb (db) - 7.0f, 30.0f, 14.0f), Justification::centredRight);

    // spectrum
    if (! pixelDb.empty())
    {
        Path fill;
        fill.startNewSubPath (a.getX(), a.getBottom());
        for (size_t px = 0; px < pixelDb.size(); ++px)
            fill.lineTo (a.getX() + (float) px, yForDb (pixelDb[px]));
        fill.lineTo (a.getRight(), a.getBottom());
        fill.closeSubPath();

        g.setColour (Colour (0xff3a4049).withAlpha (0.55f));
        g.fillPath (fill);

        Path line;
        for (size_t px = 0; px < pixelDb.size(); ++px)
        {
            const float x = a.getX() + (float) px, y = yForDb (pixelDb[px]);
            if (px == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        g.setColour (colours::spectrum);
        g.strokePath (line, PathStrokeType (1.5f, PathStrokeType::curved));
    }

    // detection band
    const float centre = freqParam.convertFrom0to1 (freqParam.getValue());
    const float width  = widthParam.convertFrom0to1 (widthParam.getValue());
    double lo, hi;
    bt::bandEdges (centre, width, lo, hi);
    const float xLo = xForFreq ((float) lo), xHi = xForFreq ((float) hi), xC = xForFreq (centre);

    g.saveState();
    g.reduceClipRegion (a.toNearestInt());

    g.setColour (colours::accent.withAlpha (0.16f));
    g.fillRect (Rectangle<float>::leftTopRightBottom (xLo, a.getY(), xHi, a.getBottom()));
    g.setColour (colours::accent);
    g.drawLine (xLo, a.getY(), xLo, a.getBottom(), 2.0f);
    g.drawLine (xHi, a.getY(), xHi, a.getBottom(), 2.0f);

    // actual filter response, on the same dB scale
    bt::BandPass bp;
    bp.set (sr, lo, hi);
    Path response;
    bool started = false;
    for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
    {
        const float hz = freqForX (x);
        if (hz >= sr * 0.5) break;
        const float y = yForDb (bt::gainToDb ((float) bp.magnitudeAt (sr, hz)));
        if (! started) { response.startNewSubPath (x, y); started = true; }
        else response.lineTo (x, y);
    }
    Path dashed;
    const float dashes[] = { 4.0f, 4.0f };
    PathStrokeType (1.5f).createDashedStroke (dashed, response, dashes, 2);
    g.fillPath (dashed);

    // handles
    const float midY = a.getCentreY();
    g.setColour (colours::accent);
    g.fillEllipse (xC - 7.0f, yForDb (-6.0f) - 7.0f, 14.0f, 14.0f);
    g.setColour (colours::ground);
    g.drawEllipse (xC - 7.0f, yForDb (-6.0f) - 7.0f, 14.0f, 14.0f, 2.0f);
    for (float x : { xLo, xHi })
    {
        g.setColour (colours::ground);
        g.fillEllipse (x - 5.0f, midY - 5.0f, 10.0f, 10.0f);
        g.setColour (colours::accent);
        g.drawEllipse (x - 5.0f, midY - 5.0f, 10.0f, 10.0f, 2.0f);
    }
    g.restoreState();
}

SpectrumView::Drag SpectrumView::hitTestBand (float x) const
{
    const float centre = freqParam.convertFrom0to1 (freqParam.getValue());
    const float width  = widthParam.convertFrom0to1 (widthParam.getValue());
    double lo, hi;
    bt::bandEdges (centre, width, lo, hi);
    if (std::abs (x - xForFreq ((float) lo)) < 8.0f) return Drag::low;
    if (std::abs (x - xForFreq ((float) hi)) < 8.0f) return Drag::high;
    return Drag::centre;
}

void SpectrumView::setParam (const char* id, float value)
{
    auto* p = processor.apvts.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void SpectrumView::mouseMove (const MouseEvent& e)
{
    setMouseCursor (hitTestBand ((float) e.x) == Drag::centre ? MouseCursor::DraggingHandCursor
                                                               : MouseCursor::LeftRightResizeCursor);
}

void SpectrumView::mouseDown (const MouseEvent& e)
{
    drag = hitTestBand ((float) e.x);
    dragStartCentre = freqParam.convertFrom0to1 (freqParam.getValue());
    dragStartFreq = freqForX ((float) e.x);
    (drag == Drag::centre ? freqParam : widthParam).beginChangeGesture();
}

void SpectrumView::mouseDrag (const MouseEvent& e)
{
    const float f = freqForX ((float) e.x);
    if (drag == Drag::centre)
    {
        // Move relative to where you grabbed, so the band doesn't jump.
        setParam (ParamID::freq, jlimit (minHz, maxHz, dragStartCentre * f / dragStartFreq));
    }
    else if (drag != Drag::none)
    {
        // Drag an edge: centre stays put, width follows the mouse.
        const float centre = freqParam.convertFrom0to1 (freqParam.getValue());
        setParam (ParamID::width, jlimit (0.1f, 4.0f, 2.0f * std::abs (std::log2 (f / centre))));
    }
}

void SpectrumView::mouseUp (const MouseEvent&)
{
    if (drag != Drag::none)
        (drag == Drag::centre ? freqParam : widthParam).endChangeGesture();
    drag = Drag::none;
}

void SpectrumView::mouseDoubleClick (const MouseEvent& e)
{
    freqParam.beginChangeGesture();
    setParam (ParamID::freq, freqForX ((float) e.x));
    freqParam.endChangeGesture();
}

void SpectrumView::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wheel)
{
    const float width = widthParam.convertFrom0to1 (widthParam.getValue());
    const float delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 1.5f;
    widthParam.beginChangeGesture();
    setParam (ParamID::width, jlimit (0.1f, 4.0f, width + delta));
    widthParam.endChangeGesture();
}

//==============================================================================
EnvelopeView::EnvelopeView (BandTriggerProcessor& p) : processor (p)
{
    setTitle ("Band envelope");
}

void EnvelopeView::addPoints (const BandTriggerProcessor::EnvPoint* pts, int num)
{
    for (int i = 0; i < num; ++i)
        points.push_back (pts[i]);
    const size_t maxPoints = (size_t) jmax (1, getWidth() - 36);
    while (points.size() > maxPoints)
        points.pop_front();
    repaint();
}

void EnvelopeView::paint (Graphics& g)
{
    const auto a = getLocalBounds().toFloat().withTrimmedLeft (36.0f).withTrimmedTop (12.0f);
    auto yFor = [&a] (float db) { return jmap (jlimit (-60.0f, 0.0f, db), 0.0f, -60.0f, a.getY(), a.getBottom() - 2.0f); };

    g.setColour (colours::grid);
    g.drawHorizontalLine ((int) a.getBottom() - 1, a.getX(), a.getRight());

    const float thresh = processor.apvts.getRawParameterValue (ParamID::threshold)->load();
    const float ty = yFor (thresh);

    // trace, newest point at the right edge
    Path trace;
    const float x0 = a.getRight() - (float) points.size();
    for (size_t i = 0; i < points.size(); ++i)
    {
        const float x = x0 + (float) i, y = yFor (points[i].db);
        if (i == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
    }
    g.setColour (colours::envelope);
    g.strokePath (trace, PathStrokeType (1.75f, PathStrokeType::curved));

    // threshold
    Path tl, dashed;
    tl.startNewSubPath (a.getX(), ty);
    tl.lineTo (a.getRight(), ty);
    const float dashes[] = { 5.0f, 4.0f };
    PathStrokeType (1.5f).createDashedStroke (dashed, tl, dashes, 2);
    g.setColour (colours::accent);
    g.fillPath (dashed);
    g.setFont (monoFont (10.0f));
    g.drawText (String (roundToInt (thresh)), Rectangle<float> (0.0f, ty - 7.0f, 30.0f, 14.0f), Justification::centredRight);

    // hit markers
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (! points[i].hit) continue;
        const float x = x0 + (float) i;
        Path tri;
        tri.addTriangle (x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 8.0f);
        g.fillPath (tri);
    }
}

} // namespace ui

//==============================================================================
BandTriggerEditor::BandTriggerEditor (BandTriggerProcessor& p)
    : AudioProcessorEditor (&p),
      owner (p),
      spectrum (p),
      envelope (p),
      noteStepper (*p.apvts.getParameter (ParamID::note), "Note"),
      channelStepper (*p.apvts.getParameter (ParamID::channel), "Channel")
{
    setLookAndFeel (&lnf);

    nameEditor.setFont (ui::sansFont (15.0f));
    nameEditor.setIndents (12, 9);
    nameEditor.setText (owner.getInstanceName(), false);
    nameEditor.setTitle ("Instance name");
    nameEditor.onTextChange = [this] { owner.setInstanceName (nameEditor.getText()); };
    nameEditor.onReturnKey = [] { Component::unfocusAllComponents(); };
    addAndMakeVisible (nameEditor);

    soloButton.setClickingTogglesState (true);
    bypassButton.setClickingTogglesState (true);
    soloAttachment   = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (owner.apvts, ParamID::solo, soloButton);
    bypassAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (owner.apvts, ParamID::bypass, bypassButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (bypassButton);

    addAndMakeVisible (spectrum);
    addAndMakeVisible (envelope);
    addAndMakeVisible (noteStepper);
    addAndMakeVisible (channelStepper);

    learnButton.onClick = [this] { owner.armLearn(); };
    addAndMakeVisible (learnButton);

    knobs.add (new ui::Knob (owner.apvts, ParamID::freq, "Frequency"));
    knobs.add (new ui::Knob (owner.apvts, ParamID::width, "Width"));
    knobs.add (new ui::Knob (owner.apvts, ParamID::threshold, "Threshold"));
    knobs.add (new ui::Knob (owner.apvts, ParamID::retrigger, "Retrigger"));
    knobs.add (new ui::Knob (owner.apvts, ParamID::sensitivity, "Sensitivity"));
    knobs.add (new ui::Knob (owner.apvts, ParamID::lookahead, "Lookahead"));
    for (auto* k : knobs)
        addAndMakeVisible (k);

    lastHitCount = owner.hitCounter.load();
    setSize (980, 700);
    startTimerHz (30);
}

BandTriggerEditor::~BandTriggerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void BandTriggerEditor::resized()
{
    nameEditor.setBounds (452, 24, 150, 36);
    bypassButton.setBounds (getWidth() - 20 - 90, 24, 90, 36);
    soloButton.setBounds (bypassButton.getX() - 8 - 104, 24, 104, 36);

    spectrumCard = { 20, 78, 720, 310 };
    envelopeCard = { 20, 402, 720, 144 };
    hitCard      = { 754, 78, 206, 150 };
    midiCard     = { 754, 242, 206, 304 };
    knobCard     = { 20, 560, 940, 120 };

    spectrum.setBounds (spectrumCard.getX() + 14, spectrumCard.getY() + 38, 692, 238);
    envelope.setBounds (envelopeCard.getX() + 14, envelopeCard.getY() + 36, 692, 94);

    lightArea = hitCard.withSizeKeepingCentre (72, 72).translated (0, 2);

    auto m = midiCard.reduced (16);
    m.removeFromTop (26);
    noteStepper.setBounds (m.removeFromTop (70));
    m.removeFromTop (10);
    channelStepper.setBounds (m.removeFromTop (70));
    learnButton.setBounds (m.removeFromBottom (40));

    auto k = knobCard.reduced (18, 12);
    const int kw = k.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (k.removeFromLeft (kw));
}

static void drawCard (Graphics& g, Rectangle<int> r, const String& title)
{
    g.setColour (ui::colours::card);
    g.fillRoundedRectangle (r.toFloat(), 10.0f);
    g.setColour (ui::colours::cardEdge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (ui::colours::muted);
        g.setFont (ui::sansFont (12.0f, true));
        g.drawText (title, r.reduced (14).removeFromTop (16), Justification::centredLeft);
    }
}

void BandTriggerEditor::paint (Graphics& g)
{
    using namespace ui;
    g.fillAll (colours::ground);

    // header
    g.setColour (colours::text);
    g.setFont (monoFont (20.0f, true));
    g.drawText ("BANDTRIGGER", 20, 24, 170, 36, Justification::centredLeft);
    g.setColour (colours::muted);
    g.setFont (sansFont (13.0f));
    g.drawText (BandTriggerProcessor::isInstrument ? String::fromUTF8 ("instrument \xc2\xb7 sidechain \xe2\x86\x92 MIDI")
                                                   : String::fromUTF8 ("single band \xc2\xb7 audio in \xe2\x86\x92 MIDI out"),
                190, 24, 220, 36, Justification::centredLeft);
    g.setFont (sansFont (12.0f));
    g.drawText ("NAME", 404, 24, 44, 36, Justification::centredRight);

    // spectrum card
    drawCard (g, spectrumCard, "SPECTRUM");
    {
        auto& fp = *owner.apvts.getParameter (ParamID::freq);
        auto& wp = *owner.apvts.getParameter (ParamID::width);
        const float c = fp.convertFrom0to1 (fp.getValue()), w = wp.convertFrom0to1 (wp.getValue());
        double lo, hi;
        bt::bandEdges (c, w, lo, hi);
        auto fmt = [&fp] (double hz) { return fp.getText (fp.convertTo0to1 ((float) hz), 0); };
        g.setColour (colours::accent);
        g.setFont (monoFont (13.0f));
        g.drawText (fmt (lo) + String::fromUTF8 (" \xe2\x80\x93 ") + fmt (hi) + String::fromUTF8 (" \xc2\xb7 center ") + fmt (c),
                    spectrumCard.reduced (14).removeFromTop (16), Justification::centredRight);
    }
    g.setColour (colours::muted);
    g.setFont (sansFont (12.0f));
    g.drawText (String::fromUTF8 ("Drag the band to move it \xc2\xb7 drag an edge or scroll to change width \xc2\xb7 double-click to jump"),
                spectrumCard.reduced (14).removeFromBottom (16), Justification::centredLeft);

    // envelope card
    drawCard (g, envelopeCard, "BAND ENVELOPE");
    g.setColour (colours::muted);
    g.setFont (sansFont (12.0f));
    g.drawText ("markers = MIDI notes sent", envelopeCard.reduced (14).removeFromTop (16), Justification::centredRight);

    // hit card
    drawCard (g, hitCard, "HIT");
    {
        const auto l = lightArea.toFloat();
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (colours::accent.withAlpha (0.06f * hitGlow));
            g.fillEllipse (l.expanded ((float) i * 6.0f));
        }
        g.setColour (colours::track.interpolatedWith (colours::accent, 0.15f + 0.85f * hitGlow));
        g.fillEllipse (l);
        g.setColour (colours::muted);
        g.setFont (monoFont (13.0f));
        const int vel = owner.lastVelocity.load();
        g.drawText (vel > 0 ? "vel " + String (vel) : String ("waiting"),
                    hitCard.withTop (lightArea.getBottom() + 6).withHeight (20), Justification::centred);
    }

    drawCard (g, midiCard, "MIDI OUT");
    drawCard (g, knobCard, {});
}

//==============================================================================
void BandTriggerEditor::timerCallback()
{
    updateSpectrum();

    BandTriggerProcessor::EnvPoint pts[512];
    int n;
    while ((n = owner.readEnvelopePoints (pts, 512)) > 0)
        envelope.addPoints (pts, n);

    const int hits = owner.hitCounter.load();
    const float previousGlow = hitGlow;
    if (hits != lastHitCount)
    {
        lastHitCount = hits;
        hitGlow = 1.0f;
    }
    else
    {
        hitGlow *= 0.8f;
        if (hitGlow < 0.01f) hitGlow = 0.0f;
    }
    if (std::abs (hitGlow - previousGlow) > 1.0e-4f)
        repaint (hitCard);

    const bool listening = owner.isLearnArmed();
    learnButton.setButtonText (listening ? String::fromUTF8 ("Listening\xe2\x80\xa6 hit a drum") : String ("Learn from hit"));
    learnButton.setToggleState (listening, dontSendNotification);

    std::array<float, BandTriggerProcessor::learnSize> capture;
    if (owner.takeLearnCapture (capture))
        finishLearn (capture);

    noteStepper.refresh();
    channelStepper.refresh();
    repaint (spectrumCard.withHeight (34));

    // keep the name in sync if the host loads a preset
    if (! nameEditor.hasKeyboardFocus (true) && nameEditor.getText() != owner.getInstanceName())
        nameEditor.setText (owner.getInstanceName(), false);
}

void BandTriggerEditor::updateSpectrum()
{
    const int n = owner.readSpectrumSamples (readScratch.data(), (int) readScratch.size());
    for (int i = 0; i < n; ++i)
    {
        analysisRing[(size_t) analysisWritePos] = readScratch[(size_t) i];
        analysisWritePos = (analysisWritePos + 1) % fftSize;
    }

    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = analysisRing[(size_t) ((analysisWritePos + i) % fftSize)];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    // A full-scale sine reads 0 dB: Hann coherent gain is 0.5, one-sided spectrum.
    const float norm = 4.0f / (float) fftSize;
    for (int i = 0; i < fftSize / 2; ++i)
        binDb[(size_t) i] = bt::gainToDb (fftData[(size_t) i] * norm);

    spectrum.setSpectrum (binDb.data(), fftSize / 2, fftSize, owner.getCurrentSampleRate());
}

void BandTriggerEditor::finishLearn (const std::array<float, BandTriggerProcessor::learnSize>& capture)
{
    // Zero-padded FFT of the captured hit; the loudest bin sets the band centre.
    constexpr int n = BandTriggerProcessor::learnSize;
    dsp::WindowingFunction<float> learnWindow ((size_t) n, dsp::WindowingFunction<float>::hann, false);
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (capture.begin(), capture.end(), fftData.begin());
    learnWindow.multiplyWithWindowingTable (fftData.data(), (size_t) n);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const double sr = owner.getCurrentSampleRate();
    const double binHz = sr / fftSize;
    const int first = jmax (1, (int) (30.0 / binHz));
    const int last = jmin (fftSize / 2 - 1, (int) (15000.0 / binHz));
    int best = first;
    for (int i = first; i <= last; ++i)
        if (fftData[(size_t) i] > fftData[(size_t) best])
            best = i;

    auto* fp = owner.apvts.getParameter (ParamID::freq);
    fp->beginChangeGesture();
    fp->setValueNotifyingHost (fp->convertTo0to1 ((float) (best * binHz)));
    fp->endChangeGesture();
}
