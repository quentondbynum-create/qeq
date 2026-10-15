#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
    const juce::Colour kBg        = juce::Colour (0xff111116);
    const juce::Colour kPanel     = juce::Colour (0xff1A1A20);
    const juce::Colour kSigPanel  = juce::Colour (0xff20141A); // signature tint
    const juce::Colour kGrid      = juce::Colour (0xff2A2A32);
    const juce::Colour kText      = juce::Colour (0xffE8E8EC);
    const juce::Colour kTextDim   = juce::Colour (0xff88888F);
    const juce::Colour kTotalCurve= juce::Colour (0xffF5F5F7);
    const juce::Colour kSpecFill  = juce::Colour (0xff1E6F8E);
    const juce::Colour kSpecLine  = juce::Colour (0xff4CC9F0);
    const juce::Colour kSoloOn    = juce::Colour (0xffFFD500);
    const juce::Colour kBypassOn  = juce::Colour (0xffFF6B35);
    const juce::Colour kHeartOn   = juce::Colour (0xffFF3B5C);

    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
    constexpr float kMaxDb   = 15.0f;
    constexpr float kHandleRadius = 11.0f;
    constexpr float kGrabRadius   = 15.0f;

    constexpr float kAnalyserFloorDb = -60.0f;
    constexpr float kAnalyserCeilDb  = 0.0f;

    juce::String freqLabel (float f)
    {
        if (f >= 1000.0f)
        {
            const float k = f / 1000.0f;
            return (k >= 10.0f ? juce::String ((int) std::round (k))
                               : juce::String (k, 1)) + "k";
        }
        return juce::String ((int) std::round (f));
    }
}

//==============================================================================
// Factory presets.
const QEQFactoryPreset QEQEditor::factoryPresets[8] =
{
    { "Init",
      { 120.0f, 500.0f, 3000.0f, 10000.0f },
      { 0.0f, 0.0f, 0.0f, 0.0f },
      { 0.71f, 1.0f, 1.0f, 0.71f },
      { QEQLowShelf, QEQBell, QEQBell, QEQHighShelf },
      0.0f, 0.0f, 0.0f },

    { "Vocal Shine",
      { 80.0f, 250.0f, 3000.0f, 10000.0f },
      { 0.0f, -2.0f, 3.0f, 5.0f },
      { 0.71f, 1.0f, 1.0f, 0.71f },
      { QEQHighPass, QEQBell, QEQBell, QEQHighShelf },
      8.0f, 0.0f, 0.0f },

    { "Bass Punch",
      { 120.0f, 200.0f, 2500.0f, 8000.0f },
      { 5.0f, -2.0f, 2.0f, 0.0f },
      { 0.71f, 1.0f, 1.0f, 0.71f },
      { QEQLowShelf, QEQBell, QEQBell, QEQHighShelf },
      15.0f, 0.0f, 0.0f },

    { "Telephone",
      { 400.0f, 1000.0f, 2000.0f, 3400.0f },
      { 0.0f, 5.0f, 3.0f, 0.0f },
      { 0.71f, 1.5f, 2.0f, 0.71f },
      { QEQHighPass, QEQBell, QEQBell, QEQLowPass },
      20.0f, 0.0f, 0.0f },

    { "Air Lift",
      { 120.0f, 500.0f, 3000.0f, 12000.0f },
      { 0.0f, 0.0f, 0.0f, 7.0f },
      { 0.71f, 1.0f, 1.0f, 0.71f },
      { QEQLowShelf, QEQBell, QEQBell, QEQHighShelf },
      0.0f, 1.0f, 0.0f },

    { "Mud Remover",
      { 90.0f, 280.0f, 500.0f, 10000.0f },
      { 0.0f, -4.0f, -2.0f, 0.0f },
      { 0.71f, 1.4f, 1.0f, 0.71f },
      { QEQHighPass, QEQBell, QEQBell, QEQHighShelf },
      0.0f, 0.0f, 0.0f },

    { "Warmth",
      { 150.0f, 400.0f, 3000.0f, 10000.0f },
      { 2.5f, 1.0f, 0.0f, 0.0f },
      { 0.71f, 0.8f, 1.0f, 0.71f },
      { QEQLowShelf, QEQBell, QEQBell, QEQHighShelf },
      30.0f, -2.5f, 0.0f },

    { "Smile Curve",
      { 100.0f, 1000.0f, 3000.0f, 10000.0f },
      { 4.0f, -3.0f, 0.0f, 4.0f },
      { 0.71f, 0.7f, 1.0f, 0.71f },
      { QEQLowShelf, QEQBell, QEQBell, QEQHighShelf },
      5.0f, 0.0f, 0.0f },
};

//==============================================================================
EQDisplay::EQDisplay (QEQProcessor& p) : processor (p)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    // Start at the floor so the analyzer doesn't flash full-scale on open.
    std::fill (std::begin (scopeLevels), std::end (scopeLevels), kAnalyserFloorDb);
    startTimerHz (30);
}

EQDisplay::~EQDisplay()
{
    stopTimer();
}

void EQDisplay::resized()
{
    // Margins: left for dB labels, bottom for freq labels.
    plotArea = getLocalBounds().toFloat().reduced (6.0f);
    plotArea.removeFromLeft (40.0f);
    plotArea.removeFromRight (10.0f);
    plotArea.removeFromTop (10.0f);
    plotArea.removeFromBottom (26.0f);
}

float EQDisplay::freqToX (float freqHz) const
{
    const float t = (std::log10 (juce::jlimit (kMinFreq, kMaxFreq, freqHz))
                     - std::log10 (kMinFreq))
                    / (std::log10 (kMaxFreq) - std::log10 (kMinFreq));
    return plotArea.getX() + t * plotArea.getWidth();
}

float EQDisplay::xToFreq (float x) const
{
    const float t = juce::jlimit (0.0f, 1.0f,
                                 (x - plotArea.getX()) / plotArea.getWidth());
    return std::pow (10.0f, std::log10 (kMinFreq)
                            + t * (std::log10 (kMaxFreq) - std::log10 (kMinFreq)));
}

float EQDisplay::dbToY (float db) const
{
    const float t = juce::jlimit (-kMaxDb, kMaxDb, db) / kMaxDb; // -1..1
    return plotArea.getCentreY() - t * (plotArea.getHeight() / 2.0f);
}

float EQDisplay::yToDb (float y) const
{
    const float t = (plotArea.getCentreY() - y) / (plotArea.getHeight() / 2.0f);
    return juce::jlimit (-kMaxDb, kMaxDb, t * kMaxDb);
}

juce::AudioParameterFloat* EQDisplay::getParam (const char* paramId) const
{
    return dynamic_cast<juce::AudioParameterFloat*> (
        processor.apvts.getParameter (paramId));
}

float EQDisplay::getBandFreq (int band) const
{
    if (auto* p = getParam (getQEQBandInfo (band).freqParam))
        return p->get();
    return 1000.0f;
}

float EQDisplay::getBandGain (int band) const
{
    if (auto* p = getParam (getQEQBandInfo (band).gainParam))
        return p->get();
    return 0.0f;
}

void EQDisplay::setParamValue (const char* paramId, float value)
{
    if (auto* p = getParam (paramId))
    {
        const auto& range = p->getNormalisableRange();
        const float clamped = juce::jlimit (range.start, range.end, value);
        p->setValueNotifyingHost (p->convertTo0to1 (clamped));
    }
}

void EQDisplay::setChoiceParam (const char* paramId, int choiceIndex)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
            processor.apvts.getParameter (paramId)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) choiceIndex));
}

void EQDisplay::setBoolParam (const char* paramId, bool value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterBool*> (
            processor.apvts.getParameter (paramId)))
        p->setValueNotifyingHost (value ? 1.0f : 0.0f);
}

int EQDisplay::bandAtPosition (juce::Point<float> pos) const
{
    for (int b = 0; b < 4; ++b)
    {
        const juce::Point<float> centre (freqToX (getBandFreq (b)),
                                         dbToY (getBandGain (b)));
        if (centre.getDistanceFrom (pos) <= kGrabRadius)
            return b;
    }
    return -1;
}

void EQDisplay::showBandMenu (int band, juce::Point<int> screenPos)
{
    const int currentShape = processor.getBandShape (band);

    juce::PopupMenu shapeMenu;
    for (int s = 0; s < QEQNumShapes; ++s)
        shapeMenu.addItem (100 + s, getQEQBandShapeName (s),
                           true, s == currentShape);

    juce::PopupMenu menu;
    menu.addSubMenu ("Shape", shapeMenu);
    menu.addSeparator();
    menu.addItem (1, "Solo", true, processor.getBandSolo (band));
    menu.addItem (2, "Bypass", true, processor.getBandBypassed (band));
    menu.addItem (3, "Reset Gain");

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)),
        [this, band] (int result)
        {
            const auto& info = getQEQBandInfo (band);
            if (result >= 100)
                setChoiceParam (info.shapeParam, result - 100);
            else if (result == 1)
                setBoolParam (info.soloParam, ! processor.getBandSolo (band));
            else if (result == 2)
                setBoolParam (info.bypassParam, ! processor.getBandBypassed (band));
            else if (result == 3)
                setParamValue (info.gainParam, 0.0f);
        });
}

//==============================================================================
void EQDisplay::timerCallback()
{
    updateSpectrum();
    repaint();
}

void EQDisplay::updateSpectrum()
{
    const double sr = processor.getSampleRate();
    analyserSampleRate = sr > 0.0 ? sr : 44100.0;

    // Drain the FIFO into our ring buffer (most-recent fftSize samples kept).
    auto& fifo = processor.analyserFifo;
    int numReady = fifo.getNumReady();
    int totalRead = 0;

    while (numReady > 0)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (numReady, start1, size1, start2, size2);

        if (size1 + size2 == 0)
            break;

        for (int i = 0; i < size1; ++i)
        {
            ring[ringWritePos] = processor.analyserFifoData[start1 + i];
            ringWritePos = (ringWritePos + 1) % fftSize;
        }
        for (int i = 0; i < size2; ++i)
        {
            ring[ringWritePos] = processor.analyserFifoData[start2 + i];
            ringWritePos = (ringWritePos + 1) % fftSize;
        }

        fifo.finishedRead (size1 + size2);
        totalRead += size1 + size2;
        numReady = fifo.getNumReady();
    }

    ringCount += totalRead;
    if (ringCount >= fftSize)
        ringFilled = true;
    if (! ringFilled)
        return;

    // Window the real samples FIRST, then interleave for the FFT.
    // (The windowing table must be applied to the real signal — applying it
    // to the interleaved complex work buffer windows the wrong samples.)
    float windowed[fftSize];
    for (int i = 0; i < fftSize; ++i)
        windowed[i] = ring[(ringWritePos + i) % fftSize];

    window.multiplyWithWindowingTable (windowed, (size_t) fftSize);

    for (int i = 0; i < fftSize; ++i)
    {
        fftWork[2 * i]     = windowed[i];
        fftWork[2 * i + 1] = 0.0f;
    }

    fft.performFrequencyOnlyForwardTransform (fftWork);

    // |X[k]| is unnormalised: a full-scale sine with a Hann window reads N/4
    // in its peak bin (x2 single-sided, x0.5 Hann coherent gain), so scale by
    // 4/N to make 0 dBFS read 0 dB.
    constexpr float magScale = 4.0f / (float) fftSize;

    for (int i = 0; i < fftSize / 2; ++i)
    {
        const float db = juce::jlimit (kAnalyserFloorDb, kAnalyserCeilDb,
            20.0f * std::log10 (fftWork[i] * magScale + 1.0e-9f));

        // Instant attack, ~90 dB/s release.
        if (db > scopeLevels[i])
            scopeLevels[i] = db;
        else
            scopeLevels[i] = juce::jmax (kAnalyserFloorDb, scopeLevels[i] - 3.0f);
    }
}

//==============================================================================
void EQDisplay::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);

    //---- grid ----
    g.setFont (10.0f);

    for (float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                     1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float x = freqToX (f);
        g.setColour (kGrid);
        g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());
        g.setColour (kTextDim);
        g.drawText (freqLabel (f), (int) x - 25, (int) plotArea.getBottom() + 4,
                    50, 16, juce::Justification::centredTop, false);
    }

    for (float db = -15.0f; db <= 15.0f; db += 3.0f)
    {
        const float y = dbToY (db);
        const bool isZero = std::abs (db) < 0.01f;
        g.setColour (isZero ? kTextDim.withAlpha (0.8f) : kGrid);
        g.drawHorizontalLine ((int) y, plotArea.getX(), plotArea.getRight());
        g.setColour (kTextDim);
        const juce::String label = (db > 0.0f ? "+" : "") + juce::String ((int) db);
        g.drawText (label, (int) plotArea.getX() - 40, (int) y - 8, 36, 16,
                    juce::Justification::centredRight, false);
    }

    //---- spectrum analyzer (behind the curves) ----
    {
        juce::Path specPath;
        const int w = (int) plotArea.getWidth();
        bool started = false;

        for (int i = 0; i <= w; ++i)
        {
            const float x = plotArea.getX() + (float) i;
            const float f = xToFreq (x);
            const float binF = juce::jlimit (0.0f, (float) fftSize / 2 - 1.001f,
                f * (float) fftSize / (float) analyserSampleRate);
            const int bin = (int) binF;
            const float frac = binF - (float) bin;
            const float db = scopeLevels[bin] * (1.0f - frac)
                           + scopeLevels[bin + 1] * frac;

            const float t = (db - kAnalyserFloorDb)
                            / (kAnalyserCeilDb - kAnalyserFloorDb);
            const float y = plotArea.getBottom() - t * plotArea.getHeight();

            if (! started) { specPath.startNewSubPath (x, y); started = true; }
            else           { specPath.lineTo (x, y); }
        }

        juce::Path fill = specPath;
        fill.lineTo (plotArea.getRight(), plotArea.getBottom());
        fill.lineTo (plotArea.getX(), plotArea.getBottom());
        fill.closeSubPath();

        juce::ColourGradient grad (kSpecFill.withAlpha (0.55f), plotArea.getX(), plotArea.getY(),
                                   kSpecFill.withAlpha (0.05f), plotArea.getX(), plotArea.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (fill);
        g.setColour (kSpecLine.withAlpha (0.7f));
        g.strokePath (specPath, juce::PathStrokeType (1.2f));
    }

    //---- EQ curves ----
    constexpr int numPts = 160;
    const float logMin = std::log10 (kMinFreq), logMax = std::log10 (kMaxFreq);

    auto buildCurve = [&] (std::function<float (float)> dbAt)
    {
        juce::Path p;
        for (int i = 0; i < numPts; ++i)
        {
            const float t = (float) i / (float) (numPts - 1);
            const float f = std::pow (10.0f, logMin + t * (logMax - logMin));
            const float y = dbToY (juce::jlimit (-16.0f, 16.0f, dbAt (f)));
            const float x = plotArea.getX() + t * plotArea.getWidth();
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        return p;
    };

    // Per-band contributions (thin, band color; dimmed when inaudible).
    for (int b = 0; b < 4; ++b)
    {
        const bool audible = processor.isBandAudible (b);
        auto p = buildCurve ([this, b] (float f)
                             { return processor.getBandMagnitudeResponseDb (b, f); });
        g.setColour (getQEQBandInfo (b).colour.withAlpha (audible ? 0.45f : 0.12f));
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    // Total curve (bright, 2.5px) + subtle fill.
    {
        auto p = buildCurve ([this] (float f)
                             { return processor.getMagnitudeResponseDb (f); });

        juce::Path fill = p;
        fill.lineTo (plotArea.getRight(), dbToY (0.0f));
        fill.lineTo (plotArea.getX(), dbToY (0.0f));
        fill.closeSubPath();
        g.setColour (kTotalCurve.withAlpha (0.05f));
        g.fillPath (fill);

        g.setColour (kTotalCurve);
        g.strokePath (p, juce::PathStrokeType (2.5f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    //---- band handles ----
    for (int b = 0; b < 4; ++b)
    {
        const auto& info = getQEQBandInfo (b);
        const juce::Point<float> c (freqToX (getBandFreq (b)),
                                    dbToY (getBandGain (b)));
        const bool selected = (b == selectedBand);
        const bool audible  = processor.isBandAudible (b);
        const bool soloed   = processor.getBandSolo (b);
        const float alpha   = audible ? 1.0f : 0.35f;

        if (selected)
        {
            g.setColour (info.colour.withAlpha (0.35f * alpha));
            g.fillEllipse (c.x - kHandleRadius - 4.0f, c.y - kHandleRadius - 4.0f,
                           (kHandleRadius + 4.0f) * 2.0f, (kHandleRadius + 4.0f) * 2.0f);
        }

        g.setColour (info.colour.withAlpha (alpha));
        g.fillEllipse (c.x - kHandleRadius, c.y - kHandleRadius,
                       kHandleRadius * 2.0f, kHandleRadius * 2.0f);
        g.setColour (kTotalCurve.withAlpha (0.9f * alpha));
        g.drawEllipse (c.x - kHandleRadius, c.y - kHandleRadius,
                       kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.5f);

        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1),
                    (int) (c.x - kHandleRadius), (int) (c.y - kHandleRadius),
                    (int) (kHandleRadius * 2.0f), (int) (kHandleRadius * 2.0f),
                    juce::Justification::centred, false);

        // Solo badge.
        if (soloed)
        {
            const float bx = c.x + kHandleRadius - 5.0f;
            const float by = c.y - kHandleRadius - 5.0f;
            g.setColour (kSoloOn);
            g.fillEllipse (bx, by, 16.0f, 16.0f);
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (10.0f, juce::Font::bold));
            g.drawText ("S", (int) bx, (int) by, 16, 16,
                        juce::Justification::centred, false);
        }
    }

    g.setColour (kGrid);
    g.drawRect (getLocalBounds(), 1);
}

//==============================================================================
void EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int band = bandAtPosition (e.position);
    if (band < 0)
        return;

    // Right-click: band options menu.
    if (e.mods.isPopupMenu())
    {
        showBandMenu (band, e.getScreenPosition());
        return;
    }

    selectedBand = band;
    draggedBand = band;
    dragStartPos = e.position;
    dragStartFreq = getBandFreq (band);
    dragStartGain = getBandGain (band);
    if (onBandSelected)
        onBandSelected (band);
    repaint();
}

void EQDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedBand < 0)
        return;

    const auto& info = getQEQBandInfo (draggedBand);
    const float dx = e.position.x - dragStartPos.x;
    const float dy = e.position.y - dragStartPos.y;

    // Horizontal = frequency (log), vertical = gain (up = boost).
    setParamValue (info.freqParam, xToFreq (freqToX (dragStartFreq) + dx));
    setParamValue (info.gainParam, yToDb (dbToY (dragStartGain) + dy));
}

void EQDisplay::mouseUp (const juce::MouseEvent&)
{
    draggedBand = -1;
}

void EQDisplay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (bandAtPosition (e.position) >= 0
                        ? juce::MouseCursor::PointingHandCursor
                        : juce::MouseCursor::NormalCursor);
}

void EQDisplay::mouseWheelMove (const juce::MouseEvent& e,
                                const juce::MouseWheelDetails& wheel)
{
    const int band = bandAtPosition (e.position);
    if (band < 0)
        return;

    if (auto* p = getParam (getQEQBandInfo (band).qParam))
    {
        const float norm = juce::jlimit (0.0f, 1.0f,
            p->convertTo0to1 (p->get()) + wheel.deltaY * 0.08f);
        p->setValueNotifyingHost (norm);
    }
}

void EQDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = bandAtPosition (e.position);
    if (band >= 0)
        setParamValue (getQEQBandInfo (band).gainParam, 0.0f);
}

//==============================================================================
void HeatLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y,
                                                (float) width, (float) height).reduced (8.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float heat = juce::jlimit (0.0f, 1.0f, sliderPosProportional);

    // Outer glow, intensifies with value.
    if (heat > 0.02f)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colour (0xffFF5A1F).withAlpha (0.10f * heat * (float) (4 - i)));
            const float r = radius + (float) i * 5.0f;
            g.fillEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
        }
    }

    // Track arc.
    g.setColour (kGrid);
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.strokePath (track, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // Value arc: orange -> red as heat rises.
    const float angle = rotaryStartAngle
                        + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    g.setColour (juce::Colour (0xffFF9F1C).interpolatedWith (
                     juce::Colour (0xffFF2A1F), heat));
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                         rotaryStartAngle, angle, true);
    g.strokePath (value, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // Knob body.
    const float inner = radius - 8.0f;
    g.setColour (kPanel);
    g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);
    g.setColour (kGrid);
    g.drawEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f, 1.0f);

    // Pointer.
    const juce::Point<float> tip (centre.x + std::sin (angle) * (inner - 3.0f),
                                  centre.y - std::cos (angle) * (inner - 3.0f));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawLine (centre.x, centre.y, tip.x, tip.y, 2.5f);
}

//==============================================================================
QEQEditor::QEQEditor (QEQProcessor& p)
    : AudioProcessorEditor (p), processor (p), display (p)
{
    setSize (900, 620);

    logoImage = juce::ImageCache::getFromMemory (BinaryData::headroomlogo_png,
                                                 BinaryData::headroomlogo_pngSize);

    //---- top bar ----
    titleLabel.setText ("QEQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (28.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (titleLabel);

    brandLabel.setText ("HEADROOM AUDIO", juce::dontSendNotification);
    brandLabel.setFont (11.0f);
    brandLabel.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (brandLabel);

    //---- preset menu ----
    styleComboBox (presetBox);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this] { onPresetChosen(); };
    addAndMakeVisible (presetBox);

    prevPresetBtn.setButtonText (juce::String::fromUTF8 ("◀"));
    styleToggleButton (prevPresetBtn, kGrid);
    prevPresetBtn.onClick = [this] { goToPreset (-1); };
    addAndMakeVisible (prevPresetBtn);

    nextPresetBtn.setButtonText (juce::String::fromUTF8 ("▶"));
    styleToggleButton (nextPresetBtn, kGrid);
    nextPresetBtn.onClick = [this] { goToPreset (1); };
    addAndMakeVisible (nextPresetBtn);

    heartBtn.setButtonText (juce::String::fromUTF8 ("♥"));
    heartBtn.setClickingTogglesState (true);
    heartBtn.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    heartBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    heartBtn.setColour (juce::TextButton::textColourOffId, kTextDim);
    heartBtn.setColour (juce::TextButton::textColourOnId, kHeartOn);
    heartBtn.onClick = [this]
    {
        if (currentPresetName.isEmpty())
        {
            heartBtn.setToggleState (false, juce::dontSendNotification);
            return;
        }
        if (isFavorite (currentPresetName))
            favoriteNames.removeString (currentPresetName);
        else
            favoriteNames.add (currentPresetName);
        saveFavorites();
        refreshPresetMenu();
    };
    addAndMakeVisible (heartBtn);

    savePresetBtn.setButtonText ("SAVE");
    styleToggleButton (savePresetBtn, kGrid);
    savePresetBtn.onClick = [this] { promptSavePreset(); };
    addAndMakeVisible (savePresetBtn);

    outputKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    outputKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
    outputKnob.setTextValueSuffix (" dB");
    outputKnob.setColour (juce::Slider::rotarySliderFillColourId, kTextDim);
    outputKnob.setColour (juce::Slider::rotarySliderOutlineColourId, kGrid);
    outputKnob.setColour (juce::Slider::thumbColourId, kText);
    outputKnob.setColour (juce::Slider::textBoxTextColourId, kTextDim);
    outputKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (outputKnob);
    outputAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, QEQProcessor::kOutput, outputKnob);

    outputCaption.setText ("OUT", juce::dontSendNotification);
    outputCaption.setFont (10.0f);
    outputCaption.setColour (juce::Label::textColourId, kTextDim);
    outputCaption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (outputCaption);

    //---- display ----
    display.onBandSelected = [this] (int b) { selectBand (b); };
    addAndMakeVisible (display);

    //---- bottom-left: selected band ----
    bandTitleLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    bandTitleLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (bandTitleLabel);

    auto makeCaption = [this] (juce::Label& l, const char* text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (11.0f);
        l.setColour (juce::Label::textColourId, kTextDim);
        addAndMakeVisible (l);
    };
    makeCaption (freqCaption, "FREQ");
    makeCaption (gainCaption, "GAIN");
    makeCaption (qCaption, "Q");
    makeCaption (shapeCaption, "SHAPE");

    styleHorizontalSlider (freqSlider, " Hz", kTextDim);
    styleHorizontalSlider (gainSlider, " dB", kTextDim);
    styleHorizontalSlider (qSlider, "", kTextDim);
    addAndMakeVisible (freqSlider);
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (qSlider);

    for (int s = 0; s < QEQNumShapes; ++s)
        shapeBox.addItem (getQEQBandShapeName (s), s + 1);
    styleComboBox (shapeBox);
    addAndMakeVisible (shapeBox);

    soloBtn.setButtonText ("S");
    soloBtn.setClickingTogglesState (true);
    styleToggleButton (soloBtn, kSoloOn);
    addAndMakeVisible (soloBtn);

    bypassBtn.setButtonText ("B");
    bypassBtn.setClickingTogglesState (true);
    styleToggleButton (bypassBtn, kBypassOn);
    addAndMakeVisible (bypassBtn);

    //---- bottom-right: signature ----
    signatureLabel.setText ("SIGNATURE", juce::dontSendNotification);
    signatureLabel.setFont (11.0f);
    signatureLabel.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (signatureLabel);

    heatKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    heatKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    heatKnob.setTextValueSuffix (" %");
    heatKnob.setLookAndFeel (&heatLookAndFeel);
    heatKnob.setColour (juce::Slider::textBoxTextColourId, kText);
    heatKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (heatKnob);
    heatAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, QEQProcessor::kHeat, heatKnob);

    heatCaption.setText ("HEAT — analog drive", juce::dontSendNotification);
    heatCaption.setFont (juce::Font (11.0f, juce::Font::bold));
    heatCaption.setColour (juce::Label::textColourId, juce::Colour (0xffFF9F1C));
    heatCaption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (heatCaption);

    tiltKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    tiltKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    tiltKnob.setTextValueSuffix (" dB");
    tiltKnob.setColour (juce::Slider::rotarySliderFillColourId, kSpecLine);
    tiltKnob.setColour (juce::Slider::rotarySliderOutlineColourId, kGrid);
    tiltKnob.setColour (juce::Slider::thumbColourId, kText);
    tiltKnob.setColour (juce::Slider::textBoxTextColourId, kText);
    tiltKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (tiltKnob);
    tiltAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, QEQProcessor::kTilt, tiltKnob);

    tiltCaption.setText ("TILT", juce::dontSendNotification);
    tiltCaption.setFont (juce::Font (11.0f, juce::Font::bold));
    tiltCaption.setColour (juce::Label::textColourId, kSpecLine);
    tiltCaption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (tiltCaption);

    selectBand (0);

    scanUserPresets();
    loadFavorites();
    refreshPresetMenu();

    // Watch shape params so the gain slider / band title follow shape changes.
    for (int b = 0; b < 4; ++b)
        processor.apvts.addParameterListener (QEQProcessor::kBandShapeIds[b], this);
}

QEQEditor::~QEQEditor()
{
    for (int b = 0; b < 4; ++b)
        processor.apvts.removeParameterListener (QEQProcessor::kBandShapeIds[b], this);

    heatKnob.setLookAndFeel (nullptr);
}

void QEQEditor::parameterChanged (const juce::String& parameterID, float)
{
    if (selectedBand < 0)
        return;

    if (parameterID == juce::String (getQEQBandInfo (selectedBand).shapeParam))
        updateBandDependentUI();
}

void QEQEditor::selectBand (int index)
{
    index = juce::jlimit (0, 3, index);
    const auto& info = getQEQBandInfo (index);

    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    shapeAttachment.reset();
    soloAttachment.reset();
    bypassAttachment.reset();

    freqAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.freqParam, freqSlider);
    gainAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.gainParam, gainSlider);
    qAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.qParam, qSlider);
    shapeAttachment = std::make_unique<ComboBoxAttachment> (
        processor.apvts, info.shapeParam, shapeBox);
    soloAttachment = std::make_unique<ButtonAttachment> (
        processor.apvts, info.soloParam, soloBtn);
    bypassAttachment = std::make_unique<ButtonAttachment> (
        processor.apvts, info.bypassParam, bypassBtn);

    // Tint the band sliders to the selected band's color.
    for (auto* s : { &freqSlider, &gainSlider, &qSlider })
    {
        s->setColour (juce::Slider::trackColourId, info.colour.withAlpha (0.55f));
        s->setColour (juce::Slider::thumbColourId, info.colour);
    }

    if (index != selectedBand)
    {
        selectedBand = index;
        display.setSelectedBand (index);
    }

    updateBandDependentUI();
}

void QEQEditor::updateBandDependentUI()
{
    if (selectedBand < 0)
        return;

    const int shape = processor.getBandShape (selectedBand);
    const bool isPassFilter = (shape == QEQLowPass || shape == QEQHighPass);

    gainSlider.setEnabled (! isPassFilter);
    gainSlider.setAlpha (isPassFilter ? 0.35f : 1.0f);

    const auto& info = getQEQBandInfo (selectedBand);
    bandTitleLabel.setText ("BAND " + juce::String (selectedBand + 1) + "  ·  "
                            + getQEQBandShapeName (shape).toUpperCase(),
                            juce::dontSendNotification);
    bandTitleLabel.setColour (juce::Label::textColourId, info.colour);
}

void QEQEditor::styleHorizontalSlider (juce::Slider& s, const juce::String& suffix,
                                       const juce::Colour& accent)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 18);
    s.setTextValueSuffix (suffix);
    s.setColour (juce::Slider::backgroundColourId, kGrid);
    s.setColour (juce::Slider::trackColourId, accent.withAlpha (0.55f));
    s.setColour (juce::Slider::thumbColourId, accent);
    s.setColour (juce::Slider::textBoxTextColourId, kText);
    s.setColour (juce::Slider::textBoxBackgroundColourId, kPanel);
    s.setColour (juce::Slider::textBoxOutlineColourId, kGrid);
}

void QEQEditor::styleComboBox (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, kPanel);
    box.setColour (juce::ComboBox::textColourId, kText);
    box.setColour (juce::ComboBox::outlineColourId, kGrid);
    box.setColour (juce::ComboBox::buttonColourId, kTextDim);
    box.setColour (juce::ComboBox::arrowColourId, kTextDim);
    box.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1E1E26));
    box.setColour (juce::PopupMenu::textColourId, kText);
    box.setColour (juce::PopupMenu::highlightedBackgroundColourId, kGrid);
    box.setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void QEQEditor::styleToggleButton (juce::TextButton& btn, const juce::Colour& onColour)
{
    btn.setColour (juce::TextButton::buttonColourId, kPanel);
    btn.setColour (juce::TextButton::buttonOnColourId, onColour);
    btn.setColour (juce::TextButton::textColourOffId, kTextDim);
    btn.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

//==============================================================================
// Preset system.
juce::File QEQEditor::getQEQDataDir() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Headroom Audio").getChildFile ("QEQ");
}

juce::File QEQEditor::getPresetDir() const
{
    return getQEQDataDir().getChildFile ("Presets");
}

juce::File QEQEditor::getFavoritesFile() const
{
    return getQEQDataDir().getChildFile ("favorites.txt");
}

void QEQEditor::scanUserPresets()
{
    userPresetNames.clear();

    const auto dir = getPresetDir();
    if (! dir.isDirectory())
        return;

    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false,
                                             "*.qeqpreset"))
        userPresetNames.add (f.getFileNameWithoutExtension());

    userPresetNames.sort (false);
}

void QEQEditor::loadFavorites()
{
    favoriteNames.clear();

    const auto file = getFavoritesFile();
    if (! file.existsAsFile())
        return;

    juce::StringArray lines;
    file.readLines (lines);
    for (auto& l : lines)
    {
        l = l.trim();
        if (l.isNotEmpty() && ! favoriteNames.contains (l))
            favoriteNames.add (l);
    }
}

void QEQEditor::saveFavorites()
{
    getQEQDataDir().createDirectory();
    getFavoritesFile().replaceWithText (favoriteNames.joinIntoString ("\n"));
}

bool QEQEditor::isFavorite (const juce::String& name) const
{
    return favoriteNames.contains (name);
}

void QEQEditor::refreshPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    presetMenuLookup.clear();

    int itemId = 0;
    auto addPresetItem = [&] (const juce::String& displayName,
                              const juce::String& lookupName)
    {
        presetBox.addItem (displayName, ++itemId);
        presetMenuLookup.add (lookupName);
    };

    bool needsSeparator = false;

    if (favoriteNames.size() > 0)
    {
        presetBox.addSectionHeading (juce::String::fromUTF8 ("♥ Favorites"));
        for (const auto& n : favoriteNames)
            addPresetItem (juce::String::fromUTF8 ("♥ ") + n, n);
        needsSeparator = true;
    }

    if (needsSeparator)
        presetBox.addSeparator();

    presetBox.addSectionHeading ("Factory");
    for (const auto& fp : factoryPresets)
        addPresetItem (fp.name, fp.name);

    if (userPresetNames.size() > 0)
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("User");
        for (const auto& n : userPresetNames)
            addPresetItem (n, n);
    }

    if (currentPresetName.isNotEmpty())
    {
        const int idx = presetMenuLookup.indexOf (currentPresetName);
        if (idx >= 0)
            presetBox.setSelectedId (idx + 1, juce::dontSendNotification);
    }

    updateHeartButton();
}

void QEQEditor::onPresetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id <= 0 || id > presetMenuLookup.size())
        return;

    applyPresetByName (presetMenuLookup[id - 1]);
}

void QEQEditor::applyPresetByName (const juce::String& name)
{
    for (const auto& fp : factoryPresets)
    {
        if (name == juce::String (fp.name))
        {
            applyFactoryPreset (fp);
            currentPresetName = name;
            refreshPresetMenu();
            return;
        }
    }

    applyUserPreset (name);
    currentPresetName = name;
    refreshPresetMenu();
}

void QEQEditor::setFloatParam (const char* paramId, float value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
            processor.apvts.getParameter (paramId)))
    {
        const auto& range = p->getNormalisableRange();
        p->setValueNotifyingHost (
            p->convertTo0to1 (juce::jlimit (range.start, range.end, value)));
    }
}

void QEQEditor::applyFactoryPreset (const QEQFactoryPreset& p)
{
    for (int b = 0; b < 4; ++b)
    {
        const auto& info = getQEQBandInfo (b);
        setFloatParam (info.freqParam, p.freq[b]);
        setFloatParam (info.gainParam, p.gain[b]);
        setFloatParam (info.qParam, p.q[b]);

        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter (info.shapeParam)))
            c->setValueNotifyingHost (c->convertTo0to1 ((float) p.shape[b]));

        if (auto* s = dynamic_cast<juce::AudioParameterBool*> (
                processor.apvts.getParameter (info.soloParam)))
            s->setValueNotifyingHost (0.0f);

        if (auto* bp = dynamic_cast<juce::AudioParameterBool*> (
                processor.apvts.getParameter (info.bypassParam)))
            bp->setValueNotifyingHost (0.0f);
    }

    setFloatParam (QEQProcessor::kHeat, p.heat);
    setFloatParam (QEQProcessor::kTilt, p.tilt);
    setFloatParam (QEQProcessor::kOutput, p.output);

    updateBandDependentUI();
}

void QEQEditor::applyUserPreset (const juce::String& name)
{
    const auto file = getPresetDir().getChildFile (name + ".qeqpreset");

    if (auto xml = juce::parseXML (file))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid())
            processor.apvts.replaceState (tree);
    }

    updateBandDependentUI();
}

void QEQEditor::promptSavePreset()
{
    auto* alert = new juce::AlertWindow ("Save Preset",
                                         "Enter a name for this preset:",
                                         juce::AlertWindow::NoIcon);
    alert->addTextEditor ("presetName", currentPresetName, "Preset name:");
    alert->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    alert->enterModalState (
        true,
        juce::ModalCallbackFunction::create ([this, alert] (int result)
        {
            std::unique_ptr<juce::AlertWindow> deleter (alert);

            if (result != 1)
                return;

            const auto name = alert->getTextEditorContents ("presetName").trim();
            if (name.isEmpty())
                return;

            const auto dir = getPresetDir();
            dir.createDirectory();

            if (auto state = processor.apvts.copyState(); state.isValid())
                if (auto xml = std::unique_ptr<juce::XmlElement> (state.createXml()))
                    xml->writeTo (dir.getChildFile (name + ".qeqpreset"));

            currentPresetName = name;
            scanUserPresets();
            refreshPresetMenu();
        }),
        false);
}

void QEQEditor::goToPreset (int direction)
{
    juce::StringArray all;
    for (const auto& fp : factoryPresets)
        all.add (fp.name);
    all.addArray (userPresetNames);

    if (all.isEmpty())
        return;

    int idx = all.indexOf (currentPresetName);
    if (idx < 0)
        idx = (direction > 0) ? -1 : 0;

    idx = (idx + direction + all.size()) % all.size();
    applyPresetByName (all[idx]);
}

void QEQEditor::updateHeartButton()
{
    const bool fav = currentPresetName.isNotEmpty()
                     && isFavorite (currentPresetName);
    heartBtn.setToggleState (fav, juce::dontSendNotification);
    heartBtn.setEnabled (currentPresetName.isNotEmpty());
}

//==============================================================================
void QEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // Headroom Audio brand mark — small and subtle, left of the title.
    if (logoImage.isValid())
        g.drawImageWithin (logoImage, 14, 12, 32, 32,
                           juce::RectanglePlacement::centred, false);

    // Signature section panel (bottom-right).
    auto sigArea = juce::Rectangle<int> (476, 462, 900 - 476 - 8, 620 - 462 - 8);
    g.setColour (kSigPanel);
    g.fillRoundedRectangle (sigArea.toFloat(), 8.0f);
    g.setColour (kGrid);
    g.drawRoundedRectangle (sigArea.toFloat(), 8.0f, 1.0f);

    // Thin divider above the bottom panel.
    g.setColour (kGrid);
    g.drawHorizontalLine (460, 8, 892);
}

void QEQEditor::resized()
{
    //---- top bar (0..56) ----
    titleLabel.setBounds (52, 8, 76, 36);
    brandLabel.setBounds (128, 24, 140, 18);

    prevPresetBtn.setBounds (246, 14, 30, 28);
    presetBox.setBounds (280, 12, 224, 32);
    nextPresetBtn.setBounds (508, 14, 30, 28);
    heartBtn.setBounds (542, 12, 34, 32);
    savePresetBtn.setBounds (580, 14, 62, 28);

    outputKnob.setBounds (900 - 76, 2, 60, 44);
    outputCaption.setBounds (900 - 76, 44, 60, 12);

    //---- display ----
    display.setBounds (8, 60, 884, 396);

    //---- bottom panel (464..612) ----
    bandTitleLabel.setBounds (16, 464, 440, 20);

    shapeCaption.setBounds (16, 488, 52, 24);
    shapeBox.setBounds (72, 488, 168, 24);
    soloBtn.setBounds (248, 488, 46, 24);
    bypassBtn.setBounds (298, 488, 46, 24);

    freqCaption.setBounds (16, 516, 52, 22);
    freqSlider.setBounds (72, 516, 330, 22);
    gainCaption.setBounds (16, 540, 52, 22);
    gainSlider.setBounds (72, 540, 330, 22);
    qCaption.setBounds (16, 564, 52, 22);
    qSlider.setBounds (72, 564, 330, 22);

    //---- signature ----
    signatureLabel.setBounds (492, 468, 200, 18);
    heatKnob.setBounds (506, 488, 104, 100);
    heatCaption.setBounds (496, 590, 124, 16);
    tiltKnob.setBounds (656, 496, 88, 88);
    tiltCaption.setBounds (646, 590, 108, 16);
}
