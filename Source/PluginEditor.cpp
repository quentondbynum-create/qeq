#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
    const juce::Colour kBg        = juce::Colour (0xff111116);
    const juce::Colour kPanel     = juce::Colour (0xff1A1A20);
    const juce::Colour kGrid      = juce::Colour (0xff2A2A32);
    const juce::Colour kText      = juce::Colour (0xffE8E8EC);
    const juce::Colour kTextDim   = juce::Colour (0xff88888F);
    const juce::Colour kTotalCurve= juce::Colour (0xffF5F5F7);
    const juce::Colour kSpecFill  = juce::Colour (0xff1E6F8E);
    const juce::Colour kSpecLine  = juce::Colour (0xff4CC9F0);
    const juce::Colour kSoloOn    = juce::Colour (0xffFFD500);
    const juce::Colour kBypassOn  = juce::Colour (0xffFF6B35);
    const juce::Colour kHeartOn   = juce::Colour (0xffFF3B5C);
    const juce::Colour kDynOn     = juce::Colour (0xff4CC9F0);

    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
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
// Factory presets (8 bands each).
const QEQFactoryPreset QEQEditor::factoryPresets[8] =
{
    { "Init",
      { 80.0f, 200.0f, 600.0f, 1500.0f, 4000.0f, 9000.0f, 12000.0f, 16000.0f },
      { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
      { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 1.0f, 0.71f, 0.71f },
      { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
        QEQBell, QEQBell, QEQHighShelf, QEQLowPass },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      0.0f, 0.0f, 0.0f, 100.0f },

    { "Vocal Shine",
      { 80.0f, 250.0f, 900.0f, 3000.0f, 5000.0f, 8000.0f, 10000.0f, 16000.0f },
      { 0.0f, 0.0f, -2.0f, 3.0f, 0.0f, 0.0f, 5.0f, 0.0f },
      { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 1.0f, 0.71f, 0.71f },
      { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
        QEQBell, QEQBell, QEQHighShelf, QEQLowPass },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      8.0f, 0.0f, 0.0f, 100.0f },

    { "Bass Punch",
      { 60.0f, 120.0f, 200.0f, 800.0f, 2500.0f, 6000.0f, 9000.0f, 16000.0f },
      { 0.0f, 5.0f, -2.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f },
      { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 1.0f, 0.71f, 0.71f },
      { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
        QEQBell, QEQBell, QEQHighShelf, QEQLowPass },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      15.0f, 0.0f, 0.0f, 100.0f },

    { "Telephone",
      { 300.0f, 400.0f, 1000.0f, 2000.0f, 3000.0f, 3400.0f, 8000.0f, 16000.0f },
      { 0.0f, 0.0f, 5.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f },
      { 0.71f, 0.71f, 1.5f, 2.0f, 1.0f, 0.71f, 0.71f, 0.71f },
      { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
        QEQBell, QEQBell, QEQHighShelf, QEQLowPass },
      { 1, 0, 0, 0, 0, 0, 1, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      20.0f, 0.0f, 0.0f, 100.0f },

    { "Air Lift",
      { 80.0f, 200.0f, 600.0f, 1500.0f, 4000.0f, 9000.0f, 12000.0f, 16000.0f },
      { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f },
      { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 1.0f, 0.71f, 0.71f },
      { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
        QEQBell, QEQBell, QEQHighShelf, QEQLowPass },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      0.0f, 1.0f, 0.0f, 100.0f },

    { "Mud Remover",
      { 90.0f, 200.0f, 280.0f, 500.0f, 2000.0f, 6000.0f, 10000.0f, 16000.0f },
      { 0.0f, 0.0f, -4.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f },
      { 0.71f, 0.71f, 1.4f, 1.0f, 1.0f, 1.0f, 0.71f, 0.71f },
      { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
        QEQBell, QEQBell, QEQHighShelf, QEQLowPass },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      0.0f, 0.0f, 0.0f, 100.0f },

    { "Warmth",
      { 80.0f, 150.0f, 400.0f, 1500.0f, 4000.0f, 9000.0f, 12000.0f, 16000.0f },
      { 0.0f, 2.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
      { 0.71f, 0.71f, 0.8f, 1.0f, 1.0f, 1.0f, 0.71f, 0.71f },
      { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
        QEQBell, QEQBell, QEQHighShelf, QEQLowPass },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      30.0f, -2.5f, 0.0f, 100.0f },

    { "Smile Curve",
      { 80.0f, 100.0f, 600.0f, 1000.0f, 3000.0f, 9000.0f, 10000.0f, 16000.0f },
      { 0.0f, 4.0f, 0.0f, -3.0f, 0.0f, 0.0f, 4.0f, 0.0f },
      { 0.71f, 0.71f, 1.0f, 0.7f, 1.0f, 1.0f, 0.71f, 0.71f },
      { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
        QEQBell, QEQBell, QEQHighShelf, QEQLowPass },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      { 0, 0, 0, 0, 0, 0, 0, 0 },
      5.0f, 0.0f, 0.0f, 100.0f },
};

//==============================================================================
EQDisplay::EQDisplay (QEQProcessor& p) : processor (p)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    std::fill (std::begin (scopeLevels), std::end (scopeLevels), kAnalyserFloorDb);
    startTimerHz (30);
}

EQDisplay::~EQDisplay()
{
    stopTimer();
}

void EQDisplay::resized()
{
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
    const float t = juce::jlimit (-displayRangeDb, displayRangeDb, db) / displayRangeDb;
    return plotArea.getCentreY() - t * (plotArea.getHeight() / 2.0f);
}

float EQDisplay::yToDb (float y) const
{
    const float t = (plotArea.getCentreY() - y) / (plotArea.getHeight() / 2.0f);
    return juce::jlimit (-displayRangeDb, displayRangeDb, t * displayRangeDb);
}

juce::AudioParameterFloat* EQDisplay::getFloatParam (const juce::String& paramId) const
{
    return dynamic_cast<juce::AudioParameterFloat*> (
        processor.apvts.getParameter (paramId));
}

float EQDisplay::getBandFreq (int band) const
{
    if (auto* p = getFloatParam (getQEQBandInfo (band).ids.freq))
        return p->get();
    return 1000.0f;
}

float EQDisplay::getBandGain (int band) const
{
    if (auto* p = getFloatParam (getQEQBandInfo (band).ids.gain))
        return p->get();
    return 0.0f;
}

void EQDisplay::setFloatParamValue (const juce::String& paramId, float value)
{
    if (auto* p = getFloatParam (paramId))
    {
        const auto& range = p->getNormalisableRange();
        const float clamped = juce::jlimit (range.start, range.end, value);
        p->setValueNotifyingHost (p->convertTo0to1 (clamped));
    }
}

void EQDisplay::setChoiceParam (const juce::String& paramId, int choiceIndex)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
            processor.apvts.getParameter (paramId)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) choiceIndex));
}

void EQDisplay::setBoolParam (const juce::String& paramId, bool value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterBool*> (
            processor.apvts.getParameter (paramId)))
        p->setValueNotifyingHost (value ? 1.0f : 0.0f);
}

int EQDisplay::bandAtPosition (juce::Point<float> pos) const
{
    for (int b = 0; b < kQEQNumBands; ++b)
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
    const int currentSlope = processor.getBandSlope (band);
    const int currentMS    = processor.getBandMSMode (band);
    const bool dynOn       = processor.getBandDynOn (band);
    const bool dynAvail    = qeqShapeSupportsDynamic (currentShape);

    juce::PopupMenu shapeMenu;
    for (int s = 0; s < QEQNumShapes; ++s)
        shapeMenu.addItem (100 + s, getQEQBandShapeName (s),
                           true, s == currentShape);

    juce::PopupMenu menu;
    menu.addSubMenu ("Shape", shapeMenu);

    // Slope submenu (only meaningful for LP/HP).
    if (currentShape == QEQLowPass || currentShape == QEQHighPass)
    {
        juce::PopupMenu slopeMenu;
        static const char* slopeNames[4] =
            { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
        for (int s = 0; s < 4; ++s)
            slopeMenu.addItem (200 + s, slopeNames[s], true, s == currentSlope);
        menu.addSubMenu ("Slope", slopeMenu);
    }

    // Channel mode submenu.
    {
        juce::PopupMenu msMenu;
        for (int m = 0; m < QEQNumMSModes; ++m)
            msMenu.addItem (300 + m, getQEQMSModeName (m), true, m == currentMS);
        menu.addSubMenu ("Channel Mode", msMenu);
    }

    menu.addSeparator();
    if (dynAvail)
        menu.addItem (4, "Dynamic EQ", true, dynOn);
    menu.addItem (1, "Solo", true, processor.getBandSolo (band));
    menu.addItem (2, "Bypass", true, processor.getBandBypassed (band));
    menu.addItem (3, "Reset Gain");

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetScreenArea (
            juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)),
        [this, band] (int result)
        {
            const auto& info = getQEQBandInfo (band);
            if (result >= 100 && result < 200)
                setChoiceParam (info.ids.shape, result - 100);
            else if (result >= 200 && result < 300)
                setChoiceParam (info.ids.slope, result - 200);
            else if (result >= 300 && result < 400)
                setChoiceParam (info.ids.ms, result - 300);
            else if (result == 1)
                setBoolParam (info.ids.solo, ! processor.getBandSolo (band));
            else if (result == 2)
                setBoolParam (info.ids.bypass, ! processor.getBandBypassed (band));
            else if (result == 3)
                setFloatParamValue (info.ids.gain, 0.0f);
            else if (result == 4)
                setBoolParam (info.ids.dyn, ! processor.getBandDynOn (band));
        });
}

//==============================================================================
// Spectrum grab: double-click empty space retargets the least-used band
// (or the nearest band) to the click point as a bell.
void EQDisplay::spectrumGrab (juce::Point<float> pos)
{
    const float freq = xToFreq (pos.x);
    const float gain = juce::jlimit (-15.0f, 15.0f, yToDb (pos.y));

    // Prefer an unused band (|gain| < 0.5 dB); else the nearest handle.
    int target = -1;
    float bestDist = 1.0e9f;
    for (int b = 0; b < kQEQNumBands; ++b)
    {
        if (std::abs (getBandGain (b)) < 0.5f) { target = b; break; }
        const juce::Point<float> c (freqToX (getBandFreq (b)),
                                    dbToY (getBandGain (b)));
        const float d = c.getDistanceFrom (pos);
        if (d < bestDist) { bestDist = d; target = b; }
    }

    if (target < 0)
        return;

    const auto& info = getQEQBandInfo (target);
    const bool wasUnused = std::abs (getBandGain (target)) < 0.5f;

    setFloatParamValue (info.ids.freq, freq);
    setFloatParamValue (info.ids.gain, gain);
    if (wasUnused)
    {
        setChoiceParam (info.ids.shape, QEQBell);
        setChoiceParam (info.ids.ms, QEQMS_Stereo);
        setBoolParam (info.ids.bypass, false);
    }

    selectedBand = target;
    if (onBandSelected) onBandSelected (target);
    if (onBandGrabbed)  onBandGrabbed (target);
    repaint();
}

//==============================================================================
void EQDisplay::timerCallback()
{
    if (! frozen)
        updateSpectrum();
    repaint();
}

void EQDisplay::updateSpectrum()
{
    const double sr = processor.getSampleRate();
    analyserSampleRate = sr > 0.0 ? sr : 44100.0;

    auto& fifo = analyserPre ? processor.analyserFifoPre
                             : processor.analyserFifoPost;
    float* fifoData = analyserPre ? processor.analyserFifoDataPre
                                  : processor.analyserFifoDataPost;

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
            ring[ringWritePos] = fifoData[start1 + i];
            ringWritePos = (ringWritePos + 1) % fftSize;
        }
        for (int i = 0; i < size2; ++i)
        {
            ring[ringWritePos] = fifoData[start2 + i];
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

    constexpr float magScale = 4.0f / (float) fftSize;

    for (int i = 0; i < fftSize / 2; ++i)
    {
        const float db = juce::jlimit (kAnalyserFloorDb, kAnalyserCeilDb,
            20.0f * std::log10 (fftWork[i] * magScale + 1.0e-9f));

        if (db > scopeLevels[i])
            scopeLevels[i] = db;
        else
            scopeLevels[i] = juce::jmax (kAnalyserFloorDb, scopeLevels[i] - 3.0f);
    }
}

//==============================================================================
juce::String EQDisplay::noteNameForFreq (float freqHz)
{
    // Nearest MIDI note name.
    static const char* names[12] =
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float midi = 69.0f + 12.0f * std::log2 (freqHz / 440.0f);
    const int m = juce::jlimit (0, 127, (int) std::round (midi));
    return juce::String (names[m % 12]) + juce::String (m / 12 - 1);
}

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

        juce::String label = freqLabel (f);
        if (showPiano)
        {
            // Show note name under C notes.
            const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
            const int m = (int) std::round (midi);
            if (m % 12 == 0)
                label = noteNameForFreq (f);
        }

        g.drawText (label, (int) x - 25, (int) plotArea.getBottom() + 4,
                    50, 16, juce::Justification::centredTop, false);
    }

    // Piano roll: light note lanes for every semitone when enabled.
    if (showPiano)
    {
        g.setColour (kGrid.withAlpha (0.35f));
        for (int m = 12; m <= 108; ++m)
        {
            const float f = 440.0f * std::pow (2.0f, (m - 69) / 12.0f);
            if (f < 20.0f || f > 20000.0f)
                continue;
            const float x = freqToX (f);
            const bool isC = (m % 12 == 0);
            g.setColour (isC ? kTextDim.withAlpha (0.5f) : kGrid.withAlpha (0.3f));
            g.drawVerticalLine ((int) x, plotArea.getY(), plotArea.getBottom());
        }
    }

    const float dbStep = (displayRangeDb > 20.0f) ? 6.0f : 3.0f;
    for (float db = -displayRangeDb; db <= displayRangeDb + 0.01f; db += dbStep)
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
    const float curveClip = displayRangeDb + 1.0f;

    auto buildCurve = [&] (std::function<float (float)> dbAt)
    {
        juce::Path p;
        for (int i = 0; i < numPts; ++i)
        {
            const float t = (float) i / (float) (numPts - 1);
            const float f = std::pow (10.0f, logMin + t * (logMax - logMin));
            const float y = dbToY (juce::jlimit (-curveClip, curveClip, dbAt (f)));
            const float x = plotArea.getX() + t * plotArea.getWidth();
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        return p;
    };

    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const bool audible = processor.isBandAudible (b);
        auto p = buildCurve ([this, b] (float f)
                             { return processor.getBandMagnitudeResponseDb (b, f); });
        g.setColour (getQEQBandInfo (b).colour.withAlpha (audible ? 0.45f : 0.12f));
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

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
    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& info = getQEQBandInfo (b);
        const juce::Point<float> c (freqToX (getBandFreq (b)),
                                    dbToY (getBandGain (b)));
        const bool selected = (b == selectedBand);
        const bool audible  = processor.isBandAudible (b);
        const bool soloed   = processor.getBandSolo (b);
        const bool dynOn    = processor.getBandDynOn (b);
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

        // M/S mode badge (non-stereo).
        const int msMode = processor.getBandMSMode (b);
        if (msMode != QEQMS_Stereo)
        {
            const float bx = c.x - kHandleRadius - 5.0f;
            const float by = c.y - kHandleRadius - 5.0f;
            g.setColour (kSpecLine.withAlpha (alpha));
            g.fillEllipse (bx, by, 16.0f, 16.0f);
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (9.0f, juce::Font::bold));
            const char* msTag = "?";
            switch (msMode)
            {
                case QEQMS_Left:  msTag = "L"; break;
                case QEQMS_Right: msTag = "R"; break;
                case QEQMS_Mid:   msTag = "M"; break;
                case QEQMS_Side:  msTag = "S"; break;
                default: break;
            }
            g.drawText (msTag, (int) bx, (int) by, 16, 16,
                        juce::Justification::centred, false);
        }

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

        // Dynamic badge.
        if (dynOn)
        {
            const float bx = c.x - 8.0f;
            const float by = c.y + kHandleRadius - 5.0f;
            g.setColour (kDynOn.withAlpha (alpha));
            g.fillEllipse (bx, by, 16.0f, 16.0f);
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (9.0f, juce::Font::bold));
            g.drawText ("D", (int) bx, (int) by, 16, 16,
                        juce::Justification::centred, false);
        }
    }

    // Analyzer mode / freeze indicator.
    g.setFont (10.0f);
    g.setColour (kTextDim);
    juce::String modeText = analyserPre ? "PRE" : "POST";
    if (frozen) modeText += " · FROZEN";
    g.drawText (modeText, (int) plotArea.getRight() - 110, (int) plotArea.getY() + 4,
                104, 14, juce::Justification::right, false);

    g.setColour (kGrid);
    g.drawRect (getLocalBounds(), 1);
}

//==============================================================================
void EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    const int band = bandAtPosition (e.position);
    if (band < 0)
        return;

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

    setFloatParamValue (info.ids.freq, xToFreq (freqToX (dragStartFreq) + dx));
    setFloatParamValue (info.ids.gain, yToDb (dbToY (dragStartGain) + dy));
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

    if (auto* p = getFloatParam (getQEQBandInfo (band).ids.q))
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
    {
        // Double-click a handle: reset its gain.
        setFloatParamValue (getQEQBandInfo (band).ids.gain, 0.0f);
    }
    else if (plotArea.contains (e.position))
    {
        // Double-click empty space: spectrum grab.
        spectrumGrab (e.position);
    }
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

    if (heat > 0.02f)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colour (0xffFF5A1F).withAlpha (0.10f * heat * (float) (4 - i)));
            const float r = radius + (float) i * 5.0f;
            g.fillEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
        }
    }

    g.setColour (kGrid);
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.strokePath (track, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    const float angle = rotaryStartAngle
                        + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    g.setColour (juce::Colour (0xffFF9F1C).interpolatedWith (
                     juce::Colour (0xffFF2A1F), heat));
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                         rotaryStartAngle, angle, true);
    g.strokePath (value, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    const float inner = radius - 8.0f;
    g.setColour (kPanel);
    g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);
    g.setColour (kGrid);
    g.drawEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f, 1.0f);

    const juce::Point<float> tip (centre.x + std::sin (angle) * (inner - 3.0f),
                                  centre.y - std::cos (angle) * (inner - 3.0f));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawLine (centre.x, centre.y, tip.x, tip.y, 2.5f);
}

//==============================================================================
QEQEditor::QEQEditor (QEQProcessor& p)
    : AudioProcessorEditor (p), processor (p), display (p)
{
    // Restore last size (clamped to the resizable limits).
    const int w = juce::jlimit (700, 1400, processor.storedEditorWidth);
    const int h = juce::jlimit (520, 1000, processor.storedEditorHeight);
    setSize (w, h);
    setResizable (true, true);
    setResizeLimits (700, 520, 1400, 1000);

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
    display.onBandGrabbed  = [this] (int b) { selectBand (b); };
    addAndMakeVisible (display);

    //---- bottom: selected band ----
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
    makeCaption (slopeCaption, "SLOPE");
    makeCaption (msCaption, "MODE");

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

    slopeBox.addItem ("12 dB/oct", 1);
    slopeBox.addItem ("24 dB/oct", 2);
    slopeBox.addItem ("36 dB/oct", 3);
    slopeBox.addItem ("48 dB/oct", 4);
    styleComboBox (slopeBox);
    addAndMakeVisible (slopeBox);

    for (int m = 0; m < QEQNumMSModes; ++m)
        msBox.addItem (getQEQMSModeName (m), m + 1);
    styleComboBox (msBox);
    addAndMakeVisible (msBox);

    soloBtn.setButtonText ("S");
    soloBtn.setClickingTogglesState (true);
    styleToggleButton (soloBtn, kSoloOn);
    addAndMakeVisible (soloBtn);

    bypassBtn.setButtonText ("B");
    bypassBtn.setClickingTogglesState (true);
    styleToggleButton (bypassBtn, kBypassOn);
    addAndMakeVisible (bypassBtn);

    //---- bottom: dynamics ----
    dynTitleLabel.setText ("DYNAMICS", juce::dontSendNotification);
    dynTitleLabel.setFont (juce::Font (11.0f, juce::Font::bold));
    dynTitleLabel.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (dynTitleLabel);

    dynBtn.setButtonText ("DYN");
    dynBtn.setClickingTogglesState (true);
    styleToggleButton (dynBtn, kDynOn);
    addAndMakeVisible (dynBtn);

    dynSrcBox.addItem ("Internal", 1);
    dynSrcBox.addItem ("External", 2);
    styleComboBox (dynSrcBox);
    addAndMakeVisible (dynSrcBox);

    makeCaption (dynAtkCaption, "ATK");
    makeCaption (dynRelCaption, "REL");
    makeCaption (dynThrCaption, "THR");
    makeCaption (dynRngCaption, "RNG");

    styleHorizontalSlider (dynAtkSlider, " ms", kDynOn);
    styleHorizontalSlider (dynRelSlider, " ms", kDynOn);
    styleHorizontalSlider (dynThrSlider, " dB", kDynOn);
    styleHorizontalSlider (dynRngSlider, " dB", kDynOn);
    addAndMakeVisible (dynAtkSlider);
    addAndMakeVisible (dynRelSlider);
    addAndMakeVisible (dynThrSlider);
    addAndMakeVisible (dynRngSlider);

    //---- bottom: global ----
    mixCaption.setText ("MIX", juce::dontSendNotification);
    mixCaption.setFont (juce::Font (11.0f, juce::Font::bold));
    mixCaption.setColour (juce::Label::textColourId, kTextDim);
    mixCaption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (mixCaption);

    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
    mixKnob.setTextValueSuffix (" %");
    mixKnob.setColour (juce::Slider::rotarySliderFillColourId, kSpecLine);
    mixKnob.setColour (juce::Slider::rotarySliderOutlineColourId, kGrid);
    mixKnob.setColour (juce::Slider::thumbColourId, kText);
    mixKnob.setColour (juce::Slider::textBoxTextColourId, kTextDim);
    mixKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (mixKnob);
    mixAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, QEQProcessor::kMix, mixKnob);

    analyserCaption.setText ("ANALYZER", juce::dontSendNotification);
    analyserCaption.setFont (juce::Font (11.0f, juce::Font::bold));
    analyserCaption.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (analyserCaption);

    analyserPreBtn.setButtonText ("PRE");
    analyserPreBtn.setClickingTogglesState (true);
    styleSmallButton (analyserPreBtn);
    analyserPreBtn.onClick = [this] { onAnalyserModeClicked (true); };
    addAndMakeVisible (analyserPreBtn);

    analyserPostBtn.setButtonText ("POST");
    analyserPostBtn.setClickingTogglesState (true);
    styleSmallButton (analyserPostBtn);
    analyserPostBtn.setToggleState (true, juce::dontSendNotification);
    analyserPostBtn.onClick = [this] { onAnalyserModeClicked (false); };
    addAndMakeVisible (analyserPostBtn);

    freezeBtn.setButtonText ("FREEZE");
    freezeBtn.setClickingTogglesState (true);
    styleSmallButton (freezeBtn);
    freezeBtn.onClick = [this] { onFreezeClicked(); };
    addAndMakeVisible (freezeBtn);

    rangeBtn.setButtonText ("12 dB");
    styleSmallButton (rangeBtn);
    rangeBtn.onClick = [this] { onRangeClicked(); };
    addAndMakeVisible (rangeBtn);

    pianoBtn.setButtonText ("PIANO");
    pianoBtn.setClickingTogglesState (true);
    styleSmallButton (pianoBtn);
    pianoBtn.onClick = [this] { onPianoClicked(); };
    addAndMakeVisible (pianoBtn);

    autoGainBtn.setButtonText ("AUTO GAIN");
    autoGainBtn.setClickingTogglesState (true);
    styleSmallButton (autoGainBtn);
    autoGainBtn.onClick = [this] { onAutoGainClicked(); };
    addAndMakeVisible (autoGainBtn);
    autoGainAttachment = std::make_unique<ButtonAttachment> (
        processor.apvts, QEQProcessor::kAutoGain, autoGainBtn);

    //---- bottom-right: signature (panel blends into the background —
    // the old red tint is gone) ----
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

    // Watch the params that change band UI visibility/behaviour.
    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        processor.apvts.addParameterListener (ids.shape, this);
        processor.apvts.addParameterListener (ids.slope, this);
        processor.apvts.addParameterListener (ids.ms, this);
        processor.apvts.addParameterListener (ids.dyn, this);
    }
}

QEQEditor::~QEQEditor()
{
    // Persist the current size for next open.
    processor.storedEditorWidth  = getWidth();
    processor.storedEditorHeight = getHeight();

    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        processor.apvts.removeParameterListener (ids.shape, this);
        processor.apvts.removeParameterListener (ids.slope, this);
        processor.apvts.removeParameterListener (ids.ms, this);
        processor.apvts.removeParameterListener (ids.dyn, this);
    }

    heatKnob.setLookAndFeel (nullptr);
}

void QEQEditor::parameterChanged (const juce::String& parameterID, float)
{
    if (selectedBand < 0)
        return;

    const auto& ids = getQEQBandInfo (selectedBand).ids;
    if (parameterID == ids.shape || parameterID == ids.slope
        || parameterID == ids.ms || parameterID == ids.dyn)
    {
        // Listener may fire off the message thread — defer to it.
        juce::MessageManager::callAsync ([this] { updateBandDependentUI(); });
    }
}

//==============================================================================
void QEQEditor::selectBand (int index)
{
    index = juce::jlimit (0, kQEQNumBands - 1, index);
    const auto& info = getQEQBandInfo (index);

    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    shapeAttachment.reset();
    slopeAttachment.reset();
    msAttachment.reset();
    soloAttachment.reset();
    bypassAttachment.reset();
    dynAttachment.reset();
    dynSrcAttachment.reset();
    dynAtkAttachment.reset();
    dynRelAttachment.reset();
    dynThrAttachment.reset();
    dynRngAttachment.reset();

    freqAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.ids.freq, freqSlider);
    gainAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.ids.gain, gainSlider);
    qAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.ids.q, qSlider);
    shapeAttachment = std::make_unique<ComboBoxAttachment> (
        processor.apvts, info.ids.shape, shapeBox);
    slopeAttachment = std::make_unique<ComboBoxAttachment> (
        processor.apvts, info.ids.slope, slopeBox);
    msAttachment = std::make_unique<ComboBoxAttachment> (
        processor.apvts, info.ids.ms, msBox);
    soloAttachment = std::make_unique<ButtonAttachment> (
        processor.apvts, info.ids.solo, soloBtn);
    bypassAttachment = std::make_unique<ButtonAttachment> (
        processor.apvts, info.ids.bypass, bypassBtn);

    dynAttachment = std::make_unique<ButtonAttachment> (
        processor.apvts, info.ids.dyn, dynBtn);
    dynSrcAttachment = std::make_unique<ComboBoxAttachment> (
        processor.apvts, info.ids.dynSrc, dynSrcBox);
    dynAtkAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.ids.dynAtk, dynAtkSlider);
    dynRelAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.ids.dynRel, dynRelSlider);
    dynThrAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.ids.dynThr, dynThrSlider);
    dynRngAttachment = std::make_unique<SliderAttachment> (
        processor.apvts, info.ids.dynRng, dynRngSlider);

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
    const bool isCut = (shape == QEQLowPass || shape == QEQHighPass);
    const bool isAllPass = (shape == QEQAllPass);
    const bool dynAvail = qeqShapeSupportsDynamic (shape);
    const bool dynOn = processor.getBandDynOn (selectedBand) && dynAvail;

    gainSlider.setEnabled (! isCut && ! isAllPass);
    gainSlider.setAlpha ((isCut || isAllPass) ? 0.35f : 1.0f);

    slopeCaption.setEnabled (isCut);
    slopeBox.setEnabled (isCut);
    slopeBox.setAlpha (isCut ? 1.0f : 0.35f);

    dynBtn.setEnabled (dynAvail);
    dynBtn.setAlpha (dynAvail ? 1.0f : 0.35f);
    const bool dynUI = dynOn;
    dynSrcBox.setEnabled (dynUI);
    dynAtkSlider.setEnabled (dynUI);
    dynRelSlider.setEnabled (dynUI);
    dynThrSlider.setEnabled (dynUI);
    dynRngSlider.setEnabled (dynUI);
    const float dynAlpha = dynUI ? 1.0f : 0.35f;
    dynSrcBox.setAlpha (dynAlpha);
    dynAtkSlider.setAlpha (dynAlpha);
    dynRelSlider.setAlpha (dynAlpha);
    dynThrSlider.setAlpha (dynAlpha);
    dynRngSlider.setAlpha (dynAlpha);

    const auto& info = getQEQBandInfo (selectedBand);
    juce::String title = "BAND " + juce::String (selectedBand + 1) + "  ·  "
                         + getQEQBandShapeName (shape).toUpperCase();
    if (isCut)
        title += "  ·  " + juce::String (qeqSlopeDbPerOct (
            processor.getBandSlope (selectedBand))) + " dB/oct";
    title += "  ·  " + getQEQMSModeName (processor.getBandMSMode (selectedBand)).toUpperCase();
    if (dynOn)
        title += "  ·  DYN";
    bandTitleLabel.setText (title, juce::dontSendNotification);
    bandTitleLabel.setColour (juce::Label::textColourId, info.colour);
}

//==============================================================================
void QEQEditor::onAnalyserModeClicked (bool pre)
{
    analyserPreBtn.setToggleState (pre, juce::dontSendNotification);
    analyserPostBtn.setToggleState (! pre, juce::dontSendNotification);
    display.setAnalyserPre (pre);
}

void QEQEditor::onRangeClicked()
{
    static bool is30 = false;
    is30 = ! is30;
    rangeBtn.setButtonText (is30 ? "30 dB" : "12 dB");
    display.setDisplayRangeDb (is30 ? 30.0f : 12.0f);
}

void QEQEditor::onPianoClicked()
{
    display.setShowPiano (pianoBtn.getToggleState());
}

void QEQEditor::onFreezeClicked()
{
    display.setFrozen (freezeBtn.getToggleState());
}

void QEQEditor::onAutoGainClicked()
{
    // Attachment handles the parameter; nothing extra needed.
}

//==============================================================================
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

void QEQEditor::styleSmallButton (juce::TextButton& btn)
{
    btn.setColour (juce::TextButton::buttonColourId, kPanel);
    btn.setColour (juce::TextButton::buttonOnColourId, kSpecLine);
    btn.setColour (juce::TextButton::textColourOffId, kTextDim);
    btn.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    btn.setColour (juce::ComboBox::outlineColourId, kGrid);
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

void QEQEditor::setFloatParam (const juce::String& paramId, float value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
            processor.apvts.getParameter (paramId)))
    {
        const auto& range = p->getNormalisableRange();
        p->setValueNotifyingHost (
            p->convertTo0to1 (juce::jlimit (range.start, range.end, value)));
    }
}

void QEQEditor::setChoiceParam (const juce::String& paramId, int choice)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
            processor.apvts.getParameter (paramId)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) choice));
}

void QEQEditor::setBoolParam (const juce::String& paramId, bool value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterBool*> (
            processor.apvts.getParameter (paramId)))
        p->setValueNotifyingHost (value ? 1.0f : 0.0f);
}

void QEQEditor::applyFactoryPreset (const QEQFactoryPreset& p)
{
    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& info = getQEQBandInfo (b);
        setFloatParam (info.ids.freq, p.freq[b]);
        setFloatParam (info.ids.gain, p.gain[b]);
        setFloatParam (info.ids.q, p.q[b]);
        setChoiceParam (info.ids.shape, p.shape[b]);
        setChoiceParam (info.ids.slope, p.slope[b]);
        setChoiceParam (info.ids.ms, p.ms[b]);
        setBoolParam (info.ids.solo, false);
        setBoolParam (info.ids.bypass, false);
        setBoolParam (info.ids.dyn, false);
    }

    setFloatParam (QEQProcessor::kHeat, p.heat);
    setFloatParam (QEQProcessor::kTilt, p.tilt);
    setFloatParam (QEQProcessor::kOutput, p.output);
    setFloatParam (QEQProcessor::kMix, p.mix);
    setBoolParam (QEQProcessor::kAutoGain, false);

    if (selectedBand >= 0)
        selectBand (selectedBand);
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

    if (selectedBand >= 0)
        selectBand (selectedBand);
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

    // Thin divider above the bottom panel. (The old red-tinted signature
    // panel is gone — the signature section now sits on the same background
    // as the Band section.)
    const int bottomY = getHeight() - bottomPanelHeight();
    g.setColour (kGrid);
    g.drawHorizontalLine (bottomY, 8, getWidth() - 8);
}

int QEQEditor::bottomPanelHeight() const
{
    // Scale the bottom panel a little with window height, clamped.
    return juce::jlimit (190, 230, getHeight() / 4 + 40);
}

void QEQEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();
    const int topH = 56;
    const int bottomH = bottomPanelHeight();
    const int bottomY = H - bottomH;

    //---- top bar ----
    titleLabel.setBounds (52, 8, 76, 36);
    brandLabel.setBounds (128, 24, 140, 18);

    prevPresetBtn.setBounds (246, 14, 30, 28);
    presetBox.setBounds (280, 12, juce::jmin (224, W - 560), 32);
    nextPresetBtn.setBounds (280 + presetBox.getWidth() + 4, 14, 30, 28);
    heartBtn.setBounds (nextPresetBtn.getRight() + 4, 12, 34, 32);
    savePresetBtn.setBounds (heartBtn.getRight() + 4, 14, 62, 28);

    outputKnob.setBounds (W - 76, 2, 60, 44);
    outputCaption.setBounds (W - 76, 44, 60, 12);

    //---- display ----
    display.setBounds (8, topH + 4, W - 16, bottomY - topH - 12);

    //---- bottom panel columns ----
    const int bx = 8;
    const int bw = W - 16;
    const int colBandX = bx;
    const int colBandW = (int) (bw * 0.36f);
    const int colDynX  = colBandX + colBandW;
    const int colDynW  = (int) (bw * 0.22f);
    const int colGlobX = colDynX + colDynW;
    const int colGlobW = (int) (bw * 0.22f);
    const int colSigX  = colGlobX + colGlobW;
    const int colSigW  = bx + bw - colSigX;

    const int pad = 8;
    int y = bottomY + 6;

    // Band column.
    bandTitleLabel.setBounds (colBandX + pad, y, colBandW - pad * 2, 20);
    y += 24;
    {
        const int labelW = 52, ctrlX = colBandX + pad + labelW + 4;
        const int ctrlW = colBandW - pad * 2 - labelW - 4;
        const int rowH = 24;
        shapeCaption.setBounds (colBandX + pad, y, labelW, rowH);
        shapeBox.setBounds (ctrlX, y, ctrlW / 2 - 2, rowH);
        slopeCaption.setBounds (ctrlX + ctrlW / 2 + 2, y, 44, rowH);
        slopeBox.setBounds (ctrlX + ctrlW / 2 + 50, y, ctrlW / 2 - 50, rowH);
        y += rowH + 2;
        msCaption.setBounds (colBandX + pad, y, labelW, rowH);
        msBox.setBounds (ctrlX, y, ctrlW / 2 - 2, rowH);
        soloBtn.setBounds (ctrlX + ctrlW / 2 + 2, y, (ctrlW / 2 - 6) / 2, rowH);
        bypassBtn.setBounds (soloBtn.getRight() + 4, y, (ctrlW / 2 - 6) / 2, rowH);
        y += rowH + 4;
        freqCaption.setBounds (colBandX + pad, y, labelW, 22);
        freqSlider.setBounds (ctrlX, y, ctrlW, 22);
        y += 24;
        gainCaption.setBounds (colBandX + pad, y, labelW, 22);
        gainSlider.setBounds (ctrlX, y, ctrlW, 22);
        y += 24;
        qCaption.setBounds (colBandX + pad, y, labelW, 22);
        qSlider.setBounds (ctrlX, y, ctrlW, 22);
    }

    // Dynamics column.
    {
        int dy = bottomY + 6;
        dynTitleLabel.setBounds (colDynX + pad, dy, colDynW - pad * 2, 20);
        dy += 24;
        dynBtn.setBounds (colDynX + pad, dy, 64, 24);
        dynSrcBox.setBounds (colDynX + pad + 70, dy, colDynW - pad * 2 - 70, 24);
        dy += 28;
        const int labelW = 40, ctrlX = colDynX + pad + labelW + 4;
        const int ctrlW = colDynW - pad * 2 - labelW - 4;
        dynAtkCaption.setBounds (colDynX + pad, dy, labelW, 22);
        dynAtkSlider.setBounds (ctrlX, dy, ctrlW, 22);
        dy += 24;
        dynRelCaption.setBounds (colDynX + pad, dy, labelW, 22);
        dynRelSlider.setBounds (ctrlX, dy, ctrlW, 22);
        dy += 24;
        dynThrCaption.setBounds (colDynX + pad, dy, labelW, 22);
        dynThrSlider.setBounds (ctrlX, dy, ctrlW, 22);
        dy += 24;
        dynRngCaption.setBounds (colDynX + pad, dy, labelW, 22);
        dynRngSlider.setBounds (ctrlX, dy, ctrlW, 22);
    }

    // Global column.
    {
        int gy = bottomY + 6;
        mixCaption.setBounds (colGlobX + pad, gy, 70, 16);
        analyserCaption.setBounds (colGlobX + pad + 76, gy, colGlobW - pad * 2 - 76, 16);
        gy += 20;
        mixKnob.setBounds (colGlobX + pad, gy, 76, 76);
        analyserPreBtn.setBounds (colGlobX + pad + 82, gy, 52, 22);
        analyserPostBtn.setBounds (colGlobX + pad + 138, gy, 52, 22);
        gy += 26;
        freezeBtn.setBounds (colGlobX + pad + 82, gy, 52, 22);
        rangeBtn.setBounds (colGlobX + pad + 138, gy, 52, 22);
        gy += 26;
        pianoBtn.setBounds (colGlobX + pad + 82, gy, 52, 22);
        autoGainBtn.setBounds (colGlobX + pad + 138, gy, 80, 22);
    }

    // Signature column.
    {
        const int knobSize = juce::jmin (104, bottomH - 60);
        signatureLabel.setBounds (colSigX + pad, bottomY + 6, colSigW - pad * 2, 18);
        const int ky = bottomY + 28;
        heatKnob.setBounds (colSigX + pad, ky, knobSize, knobSize);
        heatCaption.setBounds (colSigX + pad - 10, ky + knobSize + 2, knobSize + 20, 16);
        tiltKnob.setBounds (heatKnob.getRight() + 6, ky + 8, knobSize - 16, knobSize - 16);
        tiltCaption.setBounds (tiltKnob.getX() - 10, ky + knobSize + 2, knobSize + 4, 16);
    }
}
