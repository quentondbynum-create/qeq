#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      = juce::Colour (0xff14141a);
    const juce::Colour kPanel   = juce::Colour (0xff1e1e26);
    const juce::Colour kAccent  = juce::Colour (0xff9d4edd); // Qondabeat purple
    const juce::Colour kWave    = juce::Colour (0xffc77dff);
    const juce::Colour kTextDim = juce::Colour (0xff8a8a99);
}

//==============================================================================
FilterCurveDisplay::FilterCurveDisplay (QEQProcessor& p) : processor (p)
{
    startTimerHz (30);
}

void FilterCurveDisplay::timerCallback()
{
    repaint();
}

void FilterCurveDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (kPanel);
    g.setColour (kAccent.withAlpha (0.25f));
    g.drawRect (bounds, 1.0f);

    const float left = 8.0f, right = 8.0f, top = 8.0f, bottom = 20.0f;
    auto plot = bounds.reduced (left, top).withTrimmedRight (right - left)
                          .withTrimmedBottom (bottom - top);

    const double minDb = -18.0, maxDb = 18.0;
    const double logMin = std::log10 (20.0), logMax = std::log10 (20000.0);

    auto freqToX = [&] (double f)
    {
        const double t = (std::log10 (f) - logMin) / (logMax - logMin);
        return plot.getX() + (float) t * plot.getWidth();
    };
    auto dbToY = [&] (double db)
    {
        const double t = (db - minDb) / (maxDb - minDb);
        return plot.getBottom() - (float) t * plot.getHeight();
    };

    // Grid: octave lines + dB lines.
    g.setColour (kTextDim.withAlpha (0.25f));
    g.setFont (9.0f);
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = freqToX (f);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        const juce::String label = f >= 1000.0
            ? juce::String (f / 1000.0, f >= 10000.0 ? 0 : 1) + "k"
            : juce::String ((int) f);
        g.setColour (kTextDim.withAlpha (0.7f));
        g.drawText (label, (int) x - 20, (int) plot.getBottom() + 2, 40, 14,
                    juce::Justification::centredTop, false);
        g.setColour (kTextDim.withAlpha (0.25f));
    }
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        const float y = dbToY (db);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
    }
    // 0 dB line a touch brighter.
    g.setColour (kTextDim.withAlpha (0.5f));
    g.drawHorizontalLine ((int) dbToY (0.0), plot.getX(), plot.getRight());

    // The curve itself.
    juce::Path path;
    const int numPoints = (int) plot.getWidth();
    for (int i = 0; i <= numPoints; ++i)
    {
        const double t = (double) i / (double) numPoints;
        const double f = std::pow (10.0, logMin + t * (logMax - logMin));
        const float db = juce::jlimit ((float) minDb, (float) maxDb,
                                       processor.getMagnitudeResponseDb ((float) f));
        const float x = plot.getX() + (float) t * plot.getWidth();
        const float y = dbToY (db);
        if (i == 0) path.startNewSubPath (x, y);
        else        path.lineTo (x, y);
    }

    // Soft fill under the curve.
    juce::Path fill = path;
    fill.lineTo (plot.getRight(), dbToY (0.0));
    fill.lineTo (plot.getX(), dbToY (0.0));
    fill.closeSubPath();
    g.setColour (kAccent.withAlpha (0.12f));
    g.fillPath (fill);

    g.setColour (kWave);
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

//==============================================================================
QEQEditor::QEQEditor (QEQProcessor& p)
    : AudioProcessorEditor (p), processor (p), curveDisplay (p)
{
    setSize (660, 560);

    titleLabel.setText ("QEQ  —  character EQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (curveDisplay);

    auto sectionLabel = [] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (11.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, kAccent);
        l.setJustificationType (juce::Justification::centred);
    };
    sectionLabel (lowLabel,    "LOW");
    sectionLabel (mid1Label,   "MID 1");
    sectionLabel (mid2Label,   "MID 2");
    sectionLabel (highLabel,   "HIGH");
    sectionLabel (masterLabel, "MASTER");
    addAndMakeVisible (lowLabel);
    addAndMakeVisible (mid1Label);
    addAndMakeVisible (mid2Label);
    addAndMakeVisible (highLabel);
    addAndMakeVisible (masterLabel);

    // Band 1 — low shelf.
    addKnob (QEQProcessor::kLowFreq, "FREQ", " Hz");
    addKnob (QEQProcessor::kLowGain, "GAIN", " dB");
    // Band 2 — peak.
    addKnob (QEQProcessor::kMid1Freq, "FREQ", " Hz");
    addKnob (QEQProcessor::kMid1Gain, "GAIN", " dB");
    addKnob (QEQProcessor::kMid1Q, "Q", "");
    // Band 3 — peak.
    addKnob (QEQProcessor::kMid2Freq, "FREQ", " Hz");
    addKnob (QEQProcessor::kMid2Gain, "GAIN", " dB");
    addKnob (QEQProcessor::kMid2Q, "Q", "");
    // Band 4 — high shelf.
    addKnob (QEQProcessor::kHighFreq, "FREQ", " Hz");
    addKnob (QEQProcessor::kHighGain, "GAIN", " dB");
    // Master.
    addKnob (QEQProcessor::kHeat, "HEAT", " %");
    addKnob (QEQProcessor::kTilt, "TILT", " dB");
    addKnob (QEQProcessor::kOutput, "OUT", " dB");
}

QEQEditor::~QEQEditor() = default;

juce::Slider& QEQEditor::addKnob (const char* paramId, const juce::String& labelText,
                                  const juce::String& suffix)
{
    auto slider = std::make_unique<juce::Slider>();
    slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 16);
    slider->setTextValueSuffix (suffix);
    slider->setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    slider->setColour (juce::Slider::rotarySliderOutlineColourId, kTextDim.withAlpha (0.4f));
    slider->setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider.get());

    auto label = std::make_unique<juce::Label>();
    label->setText (labelText, juce::dontSendNotification);
    label->setFont (10.0f);
    label->setColour (juce::Label::textColourId, kTextDim);
    label->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label.get());

    attachments.push_back (
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.apvts, paramId, *slider));

    juce::Slider& ref = *slider;
    knobs.push_back (std::move (slider));
    knobLabels.push_back (std::move (label));
    return ref;
}

void QEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
}

void QEQEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    titleLabel.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);

    curveDisplay.setBounds (area.removeFromTop (150));
    area.removeFromTop (10);

    // Four band columns across the width.
    const int knobW = 78, knobH = 92, labelH = 18;
    auto bands = area.removeFromTop (labelH + knobH * 2 + 8);
    const int colW = bands.getWidth() / 4;

    auto layoutBand = [&] (juce::Rectangle<int> col, juce::Label& name,
                           std::initializer_list<int> knobIndices)
    {
        name.setBounds (col.removeFromTop (labelH));
        col.removeFromTop (4);
        int i = 0;
        const int n = (int) knobIndices.size();
        for (int idx : knobIndices)
        {
            // Two knobs per row; Q sits on the second row for the peak bands.
            const int row = i / 2, perRow = (n == 3 && i == 2) ? 1 : 2;
            const int rowW = perRow * knobW;
            const int x0 = col.getX() + (col.getWidth() - rowW) / 2 + (i % 2) * knobW;
            const int y0 = col.getY() + row * knobH;
            knobs[(size_t) idx]->setBounds (x0, y0, knobW, knobH - 14);
            knobLabels[(size_t) idx]->setBounds (x0, y0 + knobH - 14, knobW, 14);
            ++i;
        }
    };

    auto c0 = bands.removeFromLeft (colW);
    auto c1 = bands.removeFromLeft (colW);
    auto c2 = bands.removeFromLeft (colW);
    auto c3 = bands;

    layoutBand (c0, lowLabel,  { 0, 1 });        // low shelf: freq, gain
    layoutBand (c1, mid1Label, { 2, 3, 4 });     // mid 1: freq, gain, Q
    layoutBand (c2, mid2Label, { 5, 6, 7 });     // mid 2: freq, gain, Q
    layoutBand (c3, highLabel, { 8, 9 });        // high shelf: freq, gain

    area.removeFromTop (10);

    // Master row: HEAT / TILT / OUT centred.
    masterLabel.setBounds (area.removeFromTop (labelH));
    area.removeFromTop (4);
    const int masterW = 3 * knobW;
    const int mx = area.getX() + (area.getWidth() - masterW) / 2;
    for (int i = 0; i < 3; ++i)
    {
        const int idx = 10 + i;
        const int x0 = mx + i * knobW;
        knobs[(size_t) idx]->setBounds (x0, area.getY(), knobW, knobH - 14);
        knobLabels[(size_t) idx]->setBounds (x0, area.getY() + knobH - 14, knobW, 14);
    }
}
