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
    setColour (TextButton::buttonOnColourId, accent);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, colours::ground);
    setColour (TextEditor::backgroundColourId, Colour (0xff1d2024));
    setColour (TextEditor::outlineColourId, colours::track);
    setColour (TextEditor::focusedOutlineColourId, accent);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (CaretComponent::caretColourId, accent);
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
        g.setColour (accent);
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
    g.setColour (b.getToggleState() ? accent : colours::track);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

//==============================================================================
Knob::Knob (const String& displayName) : name (displayName)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setTitle (displayName);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::bind (AudioProcessorValueTreeState& state, const String& paramId)
{
    attachment.reset();
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    repaint();
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
Stepper::Stepper (const String& t) : title (t)
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
    if (param == nullptr)
        return;
    const auto range = param->getNormalisableRange();
    const float current = param->convertFrom0to1 (param->getValue());
    const float next = jlimit (range.start, range.end, std::round (current) + (float) delta);
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 (next));
    param->endChangeGesture();
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
    if (param != nullptr)
    {
        g.setColour (colours::text);
        g.setFont (monoFont (18.0f, true));
        g.drawText (param->getCurrentValueAsText(), r.withHeight (44).reduced (44, 0), Justification::centred);
    }
}

//==============================================================================
BandButton::BandButton (int i) : Button ("Band " + String (i + 1)), index (i)
{
    setTitle ("Select band " + String (i + 1));
}

void BandButton::update (const String& n, const String& note, bool bandOn, bool isSelected, float g)
{
    if (n != name || note != noteText || bandOn != on || isSelected != selected || std::abs (g - glow) > 0.01f)
    {
        name = n;
        noteText = note;
        on = bandOn;
        selected = isSelected;
        glow = g;
        setTitle ("Band " + String (index + 1) + ": " + name + (on ? "" : " (off)"));
        repaint();
    }
}

void BandButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    const auto col = colours::band[index];
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    auto fill = selected ? colours::control : colours::card;
    if (down) fill = fill.brighter (0.12f);
    else if (highlighted) fill = fill.brighter (0.06f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);

    if (glow > 0.0f)
    {
        g.setColour (col.withAlpha (0.35f * glow));
        g.fillRoundedRectangle (r, 6.0f);
    }

    // colour tab along the top
    g.setColour (col.withAlpha (on ? 1.0f : 0.3f));
    g.fillRoundedRectangle (r.withHeight (4.0f).reduced (8.0f, 0.0f).translated (0.0f, 4.0f), 2.0f);

    g.setColour (selected ? col : colours::cardEdge);
    g.drawRoundedRectangle (r, 6.0f, selected ? 2.0f : 1.0f);

    auto area = getLocalBounds().reduced (10, 0).withTrimmedTop (12);
    g.setColour (on ? colours::text : colours::muted);
    g.setFont (sansFont (13.0f, true));
    g.drawFittedText (name, area.removeFromTop (18), Justification::centredLeft, 1, 0.8f);

    auto bottom = area.removeFromTop (16);
    g.setColour (colours::muted);
    g.setFont (monoFont (11.0f));
    g.drawText (noteText, bottom, Justification::centredLeft);
    if (! on)
        g.drawText ("OFF", bottom, Justification::centredRight);
}

//==============================================================================
SpectrumView::SpectrumView (BandTriggerProcessor& p, std::function<void (int)> onSelect)
    : processor (p), onSelectBand (std::move (onSelect))
{
    setTitle ("Spectrum and detection bands");
    setMouseCursor (MouseCursor::UpDownLeftRightResizeCursor);
}

RangedAudioParameter& SpectrumView::param (int band, const char* name) const
{
    return *processor.apvts.getParameter (bands::id (band, name));
}

void SpectrumView::setParam (int band, const char* name, float value)
{
    auto& p = param (band, name);
    p.setValueNotifyingHost (p.convertTo0to1 (value));
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

void SpectrumView::paintBand (Graphics& g, int b, bool selected) const
{
    const auto a = plotArea();
    const double sr = processor.getCurrentSampleRate();
    const bool on = processor.isBandOn (b);
    const auto col = colours::band[b];

    const float centre = processor.getBandValue (b, bands::freq);
    const float width  = processor.getBandValue (b, bands::width);
    double lo, hi;
    bt::bandEdges (centre, width, lo, hi);
    const float xLo = xForFreq ((float) lo), xHi = xForFreq ((float) hi), xC = xForFreq (centre);

    // Selected band at full strength; the others recede. A switched-off band
    // is only drawn while it's selected.
    const float strength = (selected ? 1.0f : 0.32f) * (on ? 1.0f : 0.55f);

    g.setColour (col.withAlpha ((selected ? 0.16f : 0.07f) * (on ? 1.0f : 0.6f)));
    g.fillRect (Rectangle<float>::leftTopRightBottom (xLo, a.getY(), xHi, a.getBottom()));

    g.setColour (col.withAlpha (strength * (selected ? 1.0f : 0.8f)));
    const float edgeW = selected ? 1.5f : 1.0f;
    g.drawLine (xLo, a.getY(), xLo, a.getBottom(), edgeW);
    g.drawLine (xHi, a.getY(), xHi, a.getBottom(), edgeW);

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
    if (selected)
    {
        Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        PathStrokeType (1.5f).createDashedStroke (dashed, response, dashes, 2);
        g.fillPath (dashed);
    }
    else
    {
        g.strokePath (response, PathStrokeType (1.0f));
    }

    // centre line: click it to select the band
    g.setColour (col.withAlpha (selected ? 1.0f : strength * 1.4f));
    g.drawLine (xC, a.getY(), xC, a.getBottom(), selected ? 2.5f : 1.5f);

    // name tag at the top of the centre line
    const String label = processor.getBandName (b) + (on ? "" : " (off)");
    g.setFont (sansFont (12.0f, selected));
    g.setColour (col.withAlpha (selected ? 1.0f : jmin (1.0f, strength * 2.2f)));
    g.drawText (label, Rectangle<float> (xC + 5.0f, a.getY() + 3.0f + (float) (b % 3) * 14.0f, 120.0f, 14.0f),
                Justification::centredLeft);

    if (selected)
    {
        const float hy = yForDb (-6.0f);
        g.setColour (col);
        g.fillEllipse (xC - 7.0f, hy - 7.0f, 14.0f, 14.0f);
        g.setColour (colours::ground);
        g.drawEllipse (xC - 7.0f, hy - 7.0f, 14.0f, 14.0f, 2.0f);
    }
}

void SpectrumView::paint (Graphics& g)
{
    const auto a = plotArea();

    // grid
    g.setColour (colours::grid);
    for (float db = 0.0f; db >= minDb; db -= 18.0f)
        g.drawHorizontalLine ((int) yForDb (db), a.getX(), a.getRight());

    static const float gridHz[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    static const char* gridLabels[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    for (float hz : gridHz)
        g.drawVerticalLine ((int) xForFreq (hz), a.getY(), a.getBottom());

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

    // bands: others first (dimmed), selected on top
    g.saveState();
    g.reduceClipRegion (a.toNearestInt());
    const int sel = processor.getSelectedBand();
    for (int b = 0; b < bands::count; ++b)
        if (b != sel && processor.isBandOn (b))
            paintBand (g, b, false);
    paintBand (g, sel, true);
    g.restoreState();
}

int SpectrumView::bandAtX (float x) const
{
    // Nearest centre line within reach. Ties go to the selected band so you
    // can still grab it where bands overlap.
    constexpr float reach = 10.0f;
    const int sel = processor.getSelectedBand();
    int best = -1;
    float bestDist = reach;
    for (int b = 0; b < bands::count; ++b)
    {
        if (b != sel && ! processor.isBandOn (b))
            continue;
        const float d = std::abs (x - xForFreq (processor.getBandValue (b, bands::freq))) - (b == sel ? 0.5f : 0.0f);
        if (d < bestDist)
        {
            bestDist = d;
            best = b;
        }
    }
    return best;
}

void SpectrumView::mouseMove (const MouseEvent& e)
{
    setMouseCursor (bandAtX (e.position.x) >= 0 ? MouseCursor::PointingHandCursor
                                                 : MouseCursor::UpDownLeftRightResizeCursor);
}

void SpectrumView::mouseDown (const MouseEvent& e)
{
    const int hit = bandAtX (e.position.x);
    const int sel = processor.getSelectedBand();
    dragBand = hit >= 0 ? hit : sel;
    if (dragBand != sel && onSelectBand)
        onSelectBand (dragBand);

    dragStart = e.position;
    dragStartFreq = processor.getBandValue (dragBand, bands::freq);
    dragStartWidth = processor.getBandValue (dragBand, bands::width);
    param (dragBand, bands::freq).beginChangeGesture();
    param (dragBand, bands::width).beginChangeGesture();
    dragging = true;
    setMouseCursor (MouseCursor::UpDownLeftRightResizeCursor);
}

void SpectrumView::mouseDrag (const MouseEvent& e)
{
    if (! dragging)
        return;

    // Shift = fine adjustment. A small dead zone on each axis stops a
    // sideways drag from also nudging the width, and vice versa.
    const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
    constexpr float deadZone = 3.0f;
    auto beyond = [] (float d) { return std::abs (d) <= deadZone ? 0.0f : d - std::copysign (deadZone, d); };

    const float dx = beyond (e.position.x - dragStart.x);
    const float dy = beyond (e.position.y - dragStart.y);

    // left/right: move the centre, in octaves matching the axis under the mouse
    const float octavesPerPixel = std::log2 (maxHz / minHz) / plotArea().getWidth();
    setParam (dragBand, bands::freq, jlimit (minHz, maxHz, dragStartFreq * std::pow (2.0f, dx * octavesPerPixel * fine)));

    // up = wider, down = narrower
    setParam (dragBand, bands::width, jlimit (0.1f, 4.0f, dragStartWidth - dy / pixelsPerOctaveOfWidth * fine));
}

void SpectrumView::mouseUp (const MouseEvent& e)
{
    if (dragging)
    {
        param (dragBand, bands::freq).endChangeGesture();
        param (dragBand, bands::width).endChangeGesture();
    }
    dragging = false;
    mouseMove (e);
}

void SpectrumView::mouseDoubleClick (const MouseEvent& e)
{
    // Jump the selected band's centre to where you double-clicked.
    const int sel = processor.getSelectedBand();
    auto& p = param (sel, bands::freq);
    p.beginChangeGesture();
    setParam (sel, bands::freq, freqForX (e.position.x));
    p.endChangeGesture();
}

void SpectrumView::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wheel)
{
    const int sel = processor.getSelectedBand();
    const float width = processor.getBandValue (sel, bands::width);
    const float delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 1.5f;
    auto& p = param (sel, bands::width);
    p.beginChangeGesture();
    setParam (sel, bands::width, jlimit (0.1f, 4.0f, width + delta));
    p.endChangeGesture();
}

//==============================================================================
EnvelopeView::EnvelopeView (BandTriggerProcessor& p) : processor (p)
{
    setTitle ("Selected band envelope");
}

void EnvelopeView::addFrames (const BandTriggerProcessor::EnvFrame* f, int num)
{
    for (int i = 0; i < num; ++i)
        frames.push_back (f[i]);
    const size_t maxFrames = (size_t) jmax (1, getWidth() - 36);
    while (frames.size() > maxFrames)
        frames.pop_front();
    repaint();
}

void EnvelopeView::paint (Graphics& g)
{
    const int sel = processor.getSelectedBand();
    const auto col = colours::band[sel];
    const auto a = getLocalBounds().toFloat().withTrimmedLeft (36.0f).withTrimmedTop (12.0f);
    auto yFor = [&a] (float db) { return jmap (jlimit (-60.0f, 0.0f, db), 0.0f, -60.0f, a.getY(), a.getBottom() - 2.0f); };

    g.setColour (colours::grid);
    g.drawHorizontalLine ((int) a.getBottom() - 1, a.getX(), a.getRight());

    const float thresh = processor.getBandValue (sel, bands::thresh);
    const float ty = yFor (thresh);

    // trace, newest frame at the right edge
    Path trace;
    const float x0 = a.getRight() - (float) frames.size();
    for (size_t i = 0; i < frames.size(); ++i)
    {
        const float x = x0 + (float) i, y = yFor (frames[i].db[(size_t) sel]);
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
    g.setColour (col);
    g.fillPath (dashed);
    g.setFont (monoFont (10.0f));
    g.drawText (String (roundToInt (thresh)), Rectangle<float> (0.0f, ty - 7.0f, 30.0f, 14.0f), Justification::centredRight);

    // hit markers for this band
    const auto bit = (uint8_t) (1u << sel);
    for (size_t i = 0; i < frames.size(); ++i)
    {
        if ((frames[i].hits & bit) == 0) continue;
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
      spectrum (p, [this] (int b) { selectBand (b); }),
      envelope (p)
{
    setLookAndFeel (&lnf);

    soloButton.setClickingTogglesState (true);
    bypassButton.setClickingTogglesState (true);
    soloButton.setTooltip ("Hear only what the selected band's detector hears");
    soloAttachment   = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (owner.apvts, ParamID::solo, soloButton);
    bypassAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (owner.apvts, ParamID::bypass, bypassButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (bypassButton);

    for (int b = 0; b < bands::count; ++b)
    {
        auto* button = bandButtons.add (new ui::BandButton (b));
        button->onClick = [this, b] { selectBand (b); };
        addAndMakeVisible (button);
        lastHitCounts[(size_t) b] = owner.hitCounters[b].load();
    }

    addAndMakeVisible (spectrum);
    addAndMakeVisible (envelope);

    nameEditor.setFont (ui::sansFont (15.0f));
    nameEditor.setIndents (12, 9);
    nameEditor.setTitle ("Band name");
    nameEditor.onTextChange = [this] {
        if (shownBand >= 0)
            owner.setBandName (shownBand, nameEditor.getText());
    };
    nameEditor.onReturnKey = [] { Component::unfocusAllComponents(); };
    addAndMakeVisible (nameEditor);

    onButton.setClickingTogglesState (true);
    addAndMakeVisible (onButton);
    addAndMakeVisible (noteStepper);

    learnButton.onClick = [this] { owner.armLearn(); };
    addAndMakeVisible (learnButton);

    channelStepper.setParameter (owner.apvts.getParameter (ParamID::channel));
    addAndMakeVisible (channelStepper);

    for (auto* name : { "Frequency", "Width", "Threshold", "Retrigger", "Sensitivity", "Lookahead" })
        addAndMakeVisible (knobs.add (new ui::Knob (name)));
    knobs[5]->bind (owner.apvts, ParamID::lookahead);

    bindSelectedBand();
    setSize (980, 748);
    startTimerHz (30);
}

BandTriggerEditor::~BandTriggerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void BandTriggerEditor::selectBand (int band)
{
    owner.setSelectedBand (band);
    bindSelectedBand();
}

void BandTriggerEditor::bindSelectedBand()
{
    const int sel = owner.getSelectedBand();
    shownBand = sel;

    const auto col = ui::colours::band[sel];
    lnf.accent = col;
    lnf.setColour (TextButton::buttonOnColourId, col);
    lnf.setColour (TextEditor::focusedOutlineColourId, col);
    lnf.setColour (TextEditor::highlightColourId, col.withAlpha (0.35f));
    lnf.setColour (CaretComponent::caretColourId, col);

    const char* perBand[] = { bands::freq, bands::width, bands::thresh, bands::retrig, bands::sens };
    for (int i = 0; i < 5; ++i)
        knobs[i]->bind (owner.apvts, bands::id (sel, perBand[i]));

    onAttachment.reset();
    onAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (owner.apvts, bands::id (sel, bands::on), onButton);
    noteStepper.setParameter (owner.apvts.getParameter (bands::id (sel, bands::note)));
    nameEditor.setText (owner.getBandName (sel), false);

    updateBandButtons();
    repaint();
    for (auto* c : getChildren())
        c->repaint();
}

void BandTriggerEditor::updateBandButtons()
{
    for (int b = 0; b < bands::count; ++b)
    {
        auto* noteParam = owner.apvts.getParameter (bands::id (b, bands::note));
        bandButtons[b]->update (owner.getBandName (b), noteParam->getCurrentValueAsText(), owner.isBandOn (b),
                                b == shownBand, glow[(size_t) b]);
    }
}

//==============================================================================
void BandTriggerEditor::resized()
{
    bypassButton.setBounds (getWidth() - 20 - 90, 20, 90, 36);
    soloButton.setBounds (bypassButton.getX() - 8 - 104, 20, 104, 36);

    bandRow      = { 20, 70, 940, 52 };
    spectrumCard = { 20, 136, 720, 304 };
    envelopeCard = { 20, 454, 720, 140 };
    bandCard     = { 754, 136, 206, 314 };
    midiCard     = { 754, 464, 206, 130 };
    knobCard     = { 20, 608, 940, 120 };

    {
        auto row = bandRow;
        const int gap = 8;
        const int w = (row.getWidth() - gap * (bands::count - 1)) / bands::count;
        for (auto* b : bandButtons)
        {
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
        }
    }

    spectrum.setBounds (spectrumCard.getX() + 14, spectrumCard.getY() + 38, 692, 232);
    envelope.setBounds (envelopeCard.getX() + 14, envelopeCard.getY() + 36, 692, 94);

    auto c = bandCard.reduced (16);
    c.removeFromTop (26);
    nameEditor.setBounds (c.removeFromTop (36));
    c.removeFromTop (10);
    onButton.setBounds (c.removeFromTop (36));
    c.removeFromTop (12);
    noteStepper.setBounds (c.removeFromTop (66));
    learnButton.setBounds (c.removeFromBottom (40));

    auto m = midiCard.reduced (16);
    m.removeFromTop (26);
    channelStepper.setBounds (m.removeFromTop (66));

    auto k = knobCard.reduced (18, 12);
    const int kw = k.getWidth() / knobs.size();
    for (auto* knob : knobs)
        knob->setBounds (k.removeFromLeft (kw));
}

static void drawCard (Graphics& g, Rectangle<int> r, const String& title, Colour titleColour = ui::colours::muted)
{
    g.setColour (ui::colours::card);
    g.fillRoundedRectangle (r.toFloat(), 10.0f);
    g.setColour (ui::colours::cardEdge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (titleColour);
        g.setFont (ui::sansFont (12.0f, true));
        g.drawText (title, r.reduced (14).removeFromTop (16), Justification::centredLeft);
    }
}

void BandTriggerEditor::paint (Graphics& g)
{
    using namespace ui;
    const int sel = jmax (0, shownBand);
    const auto col = colours::band[sel];
    g.fillAll (colours::ground);

    // header
    g.setColour (colours::text);
    g.setFont (monoFont (20.0f, true));
    g.drawText ("BANDTRIGGER", 20, 20, 170, 36, Justification::centredLeft);
    g.setColour (colours::muted);
    g.setFont (sansFont (13.0f));
    g.drawText (BandTriggerProcessor::isInstrument ? String::fromUTF8 ("8 bands \xc2\xb7 sidechain \xe2\x86\x92 MIDI")
                                                   : String::fromUTF8 ("8 bands \xc2\xb7 audio in \xe2\x86\x92 MIDI out"),
                190, 20, 260, 36, Justification::centredLeft);

    // spectrum card
    drawCard (g, spectrumCard, "SPECTRUM");
    {
        auto& fp = *owner.apvts.getParameter (bands::id (sel, bands::freq));
        const float c = owner.getBandValue (sel, bands::freq), w = owner.getBandValue (sel, bands::width);
        double lo, hi;
        bt::bandEdges (c, w, lo, hi);
        auto fmt = [&fp] (double hz) { return fp.getText (fp.convertTo0to1 ((float) hz), 0); };
        g.setColour (col);
        g.setFont (monoFont (13.0f));
        g.drawText (owner.getBandName (sel) + String::fromUTF8 (" \xc2\xb7 ") + fmt (lo) + String::fromUTF8 (" \xe2\x80\x93 ")
                        + fmt (hi) + String::fromUTF8 (" \xc2\xb7 center ") + fmt (c),
                    spectrumCard.reduced (14).removeFromTop (16), Justification::centredRight);
    }
    g.setColour (colours::muted);
    g.setFont (sansFont (12.0f));
    g.drawText (String::fromUTF8 ("Click a center line to pick a band \xc2\xb7 drag \xe2\x86\x94 frequency \xc2\xb7 drag \xe2\x86\x95 width (up = wider) \xc2\xb7 Shift = fine"),
                spectrumCard.reduced (14).removeFromBottom (16), Justification::centredLeft);

    // envelope card
    drawCard (g, envelopeCard, "BAND ENVELOPE");
    g.setColour (colours::muted);
    g.setFont (sansFont (12.0f));
    g.drawText (owner.getBandName (sel) + String::fromUTF8 (" \xc2\xb7 markers = MIDI notes sent"),
                envelopeCard.reduced (14).removeFromTop (16), Justification::centredRight);

    // selected band card
    drawCard (g, bandCard, "BAND " + String (sel + 1), col);
    {
        const int vel = owner.lastVelocities[sel].load();
        g.setColour (colours::muted);
        g.setFont (monoFont (12.0f));
        g.drawText (vel > 0 ? "last vel " + String (vel) : String ("no hits yet"),
                    bandCard.reduced (14).removeFromTop (16), Justification::centredRight);
    }

    drawCard (g, midiCard, "MIDI OUT");
    drawCard (g, knobCard, {});
}

//==============================================================================
void BandTriggerEditor::timerCallback()
{
    updateSpectrum();

    BandTriggerProcessor::EnvFrame frames[512];
    int n;
    while ((n = owner.readEnvelopeFrames (frames, 512)) > 0)
        envelope.addFrames (frames, n);

    for (int b = 0; b < bands::count; ++b)
    {
        const int hits = owner.hitCounters[b].load();
        auto& gl = glow[(size_t) b];
        if (hits != lastHitCounts[(size_t) b])
        {
            lastHitCounts[(size_t) b] = hits;
            gl = 1.0f;
        }
        else
        {
            gl *= 0.8f;
            if (gl < 0.02f) gl = 0.0f;
        }
    }

    // follow selection changes made elsewhere (preset load, state restore)
    if (owner.getSelectedBand() != shownBand)
        bindSelectedBand();
    else
        updateBandButtons();

    const bool listening = owner.isLearnArmed();
    learnButton.setButtonText (listening ? String::fromUTF8 ("Listening\xe2\x80\xa6 hit a drum") : String ("Learn from hit"));
    learnButton.setToggleState (listening, dontSendNotification);

    std::array<float, BandTriggerProcessor::learnSize> capture;
    if (owner.takeLearnCapture (capture))
        finishLearn (capture);

    noteStepper.repaint();
    channelStepper.repaint();
    repaint (spectrumCard.withHeight (34));
    repaint (bandCard.withHeight (34));

    // keep the name in sync if the host loads a preset
    if (! nameEditor.hasKeyboardFocus (true) && nameEditor.getText() != owner.getBandName (shownBand))
        nameEditor.setText (owner.getBandName (shownBand), false);
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
    // Zero-padded FFT of the captured hit; the loudest bin sets the selected
    // band's centre, and switches the band on.
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

    const int sel = owner.getSelectedBand();
    auto* fp = owner.apvts.getParameter (bands::id (sel, bands::freq));
    fp->beginChangeGesture();
    fp->setValueNotifyingHost (fp->convertTo0to1 ((float) (best * binHz)));
    fp->endChangeGesture();

    auto* on = owner.apvts.getParameter (bands::id (sel, bands::on));
    if (on->getValue() < 0.5f)
    {
        on->beginChangeGesture();
        on->setValueNotifyingHost (1.0f);
        on->endChangeGesture();
    }
}
