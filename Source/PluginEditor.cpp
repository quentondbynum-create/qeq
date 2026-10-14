#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
    constexpr float kMaxDb   = 15.0f;
    constexpr float kHandleRadius = 11.0f;
    constexpr float kGrabRadius   = 15.0f;

    constexpr float kAnalyserFloorDb = -66.0f;
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
EQDisplay::EQDisplay (QEQProcessor& p) : processor (p)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
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

    // Copy oldest -> newest into the FFT work buffer (real in even slots).
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (ringWritePos + i) % fftSize;
        fftWork[2 * i]     = ring[idx];
        fftWork[2 * i + 1] = 0.0f;
    }

    window.multiplyWithWindowingTable (fftWork, (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftWork);

    for (int i = 0; i < fftSize / 2; ++i)
    {
        const float db = juce::jlimit (kAnalyserFloorDb, kAnalyserCeilDb,
            20.0f * std::log10 (fftWork[i] / (float) fftSize + 1.0e-9f));

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
    constexpr int numPts = 120;
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

    // Per-band contributions (thin, band color, 45% alpha).
    for (int b = 0; b < 4; ++b)
    {
        auto p = buildCurve ([this, b] (float f)
                             { return processor.getBandMagnitudeResponseDb (b, f); });
        g.setColour (getQEQBandInfo (b).colour.withAlpha (0.45f));
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

        if (selected)
        {
            g.setColour (info.colour.withAlpha (0.35f));
            g.fillEllipse (c.x - kHandleRadius - 4.0f, c.y - kHandleRadius - 4.0f,
                           (kHandleRadius + 4.0f) * 2.0f, (kHandleRadius + 4.0f) * 2.0f);
        }

        g.setColour (info.colour);
        g.fillEllipse (c.x - kHandleRadius, c.y - kHandleRadius,
                       kHandleRadius * 2.0f, kHandleRadius * 2.0f);
        g.setColour (kTotalCurve.withAlpha (0.9f));
        g.drawEllipse (c.x - kHandleRadius, c.y - kHandleRadius,
                       kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.5f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (juce::String (b + 1),
                    (int) (c.x - kHandleRadius), (int) (c.y - kHandleRadius),
                    (int) (kHandleRadius * 2.0f), (int) (kHandleRadius * 2.0f),
                    juce::Justification::centred, false);
    }

    g.setColour (kGrid);
    g.drawRect (getLocalBounds(), 1);
}

//==============================================================================
void EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int band = bandAtPosition (e.position);
    if (band >= 0)
    {
        selectedBand = band;
        draggedBand = band;
        dragStartPos = e.position;
        dragStartFreq = getBandFreq (band);
        dragStartGain = getBandGain (band);
        if (onBandSelected)
            onBandSelected (band);
        repaint();
    }
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

    const char* qId = getQEQBandInfo (band).qParam;
    if (qId == nullptr)
        return; // shelves have no Q

    if (auto* p = getParam (qId))
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

    //---- top bar ----
    titleLabel.setText ("QEQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (28.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (titleLabel);

    brandLabel.setText ("HEADROOM AUDIO", juce::dontSendNotification);
    brandLabel.setFont (11.0f);
    brandLabel.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (brandLabel);

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

    styleHorizontalSlider (freqSlider, " Hz", kTextDim);
    styleHorizontalSlider (gainSlider, " dB", kTextDim);
    styleHorizontalSlider (qSlider, "", kTextDim);
    addAndMakeVisible (freqSlider);
    addAndMakeVisible (gainSlider);
    addAndMakeVisible (qSlider);

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
}

QEQEditor::~QEQEditor()
{
    heatKnob.setLookAndFeel (nullptr);
}

void QEQEditor::selectBand (int index)
{
    index = juce::jlimit (0, 3, index);
    const auto& info = getQEQBandInfo (index);

    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();

    freqAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.freqParam, freqSlider);
    gainAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.gainParam, gainSlider);

    if (info.qParam != nullptr)
    {
        qAttachment = std::make_unique<SliderAttachment> (
            processor.apvts, info.qParam, qSlider);
        qSlider.setEnabled (true);
        qSlider.setAlpha (1.0f);
    }
    else
    {
        qSlider.setEnabled (false);
        qSlider.setAlpha (0.35f);
    }

    // Tint the band sliders to the selected band's color.
    for (auto* s : { &freqSlider, &gainSlider, &qSlider })
    {
        s->setColour (juce::Slider::trackColourId, info.colour.withAlpha (0.55f));
        s->setColour (juce::Slider::thumbColourId, info.colour);
    }

    bandTitleLabel.setText ("BAND " + juce::String (index + 1) + "  ·  "
                            + juce::String (info.name).toUpperCase(),
                            juce::dontSendNotification);
    bandTitleLabel.setColour (juce::Label::textColourId, info.colour);

    if (index != selectedBand)
    {
        selectedBand = index;
        display.setSelectedBand (index);
    }
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

void QEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

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
    titleLabel.setBounds (16, 8, 110, 36);
    brandLabel.setBounds (112, 24, 220, 18);
    outputKnob.setBounds (900 - 76, 2, 60, 44);
    outputCaption.setBounds (900 - 76, 44, 60, 12);

    //---- display ----
    display.setBounds (8, 60, 884, 396);

    //---- bottom panel (464..612) ----
    bandTitleLabel.setBounds (16, 466, 440, 22);

    const int rowX = 16, capW = 56, sliderW = 330;
    freqCaption.setBounds (rowX, 494, capW, 26);
    freqSlider.setBounds (rowX + capW, 494, sliderW, 26);
    gainCaption.setBounds (rowX, 526, capW, 26);
    gainSlider.setBounds (rowX + capW, 526, sliderW, 26);
    qCaption.setBounds (rowX, 558, capW, 26);
    qSlider.setBounds (rowX + capW, 558, sliderW, 26);

    //---- signature ----
    signatureLabel.setBounds (492, 468, 200, 18);
    heatKnob.setBounds (506, 488, 104, 100);
    heatCaption.setBounds (496, 590, 124, 16);
    tiltKnob.setBounds (656, 496, 88, 88);
    tiltCaption.setBounds (646, 590, 108, 16);
}
