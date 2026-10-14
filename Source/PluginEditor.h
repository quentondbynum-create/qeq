#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

//==============================================================================
// Per-band metadata shared by the display and the bottom panel.
struct QEQBandInfo
{
    const char* freqParam;
    const char* gainParam;
    const char* qParam;      // nullptr for shelf bands
    const char* name;        // "Low Shelf", "Peak 1", ...
    juce::Colour colour;
};

inline const QEQBandInfo& getQEQBandInfo (int index)
{
    static const QEQBandInfo bands[4] =
    {
        { QEQProcessor::kLowFreq,  QEQProcessor::kLowGain,  nullptr,
          "Low Shelf",  juce::Colour (0xffFF9F1C) },
        { QEQProcessor::kMid1Freq, QEQProcessor::kMid1Gain, QEQProcessor::kMid1Q,
          "Peak 1",     juce::Colour (0xff2EC4B6) },
        { QEQProcessor::kMid2Freq, QEQProcessor::kMid2Gain, QEQProcessor::kMid2Q,
          "Peak 2",     juce::Colour (0xff4CC9F0) },
        { QEQProcessor::kHighFreq, QEQProcessor::kHighGain, nullptr,
          "High Shelf", juce::Colour (0xffB5179E) },
    };
    return bands[juce::jlimit (0, 3, index)];
}

//==============================================================================
// Pro-Q style interactive display: log-frequency grid, real-time spectrum
// analyzer, per-band + total EQ curves, and draggable band handles.
class EQDisplay : public juce::Component,
                  private juce::Timer
{
public:
    explicit EQDisplay (QEQProcessor& p);
    ~EQDisplay() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    int getSelectedBand() const noexcept { return selectedBand; }
    void setSelectedBand (int b)
    {
        b = juce::jlimit (0, 3, b);
        if (b != selectedBand) { selectedBand = b; repaint(); }
    }

    // Fired when the user clicks a handle (the editor re-points its sliders).
    std::function<void (int)> onBandSelected;

private:
    void timerCallback() override;

    // Pixel <-> value mapping (log frequency, linear dB).
    float freqToX (float freqHz) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;

    // Index of the handle within grabRadius of pos, or -1.
    int bandAtPosition (juce::Point<float> pos) const;

    juce::AudioParameterFloat* getParam (const char* paramId) const;
    float getBandFreq (int band) const;
    float getBandGain (int band) const;
    void setParamValue (const char* paramId, float value);

    // Spectrum analyzer: pull FIFO samples into our ring, run FFT, smooth.
    void updateSpectrum();

    QEQProcessor& processor;
    int selectedBand = 0;

    juce::Rectangle<float> plotArea;

    // Drag state.
    int draggedBand = -1;
    juce::Point<float> dragStartPos;
    float dragStartFreq = 0.0f, dragStartGain = 0.0f;

    // Analyzer state.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window {
        (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    float fftWork[2 * fftSize] = {};
    float scopeLevels[fftSize / 2] = {};   // smoothed dB per bin
    float ring[fftSize] = {};
    int ringWritePos = 0;
    int ringCount = 0;
    bool ringFilled = false;
    double analyserSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQDisplay)
};

//==============================================================================
// Rotary knob with an orange/red glow that intensifies with the value,
// used for the signature HEAT control.
struct HeatLookAndFeel : public juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;
};

//==============================================================================
class QEQEditor : public juce::AudioProcessorEditor
{
public:
    explicit QEQEditor (QEQProcessor&);
    ~QEQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    // Re-point the bottom-panel sliders at the given band's parameters.
    void selectBand (int index);

    static void styleHorizontalSlider (juce::Slider& s, const juce::String& suffix,
                                       const juce::Colour& accent);

    QEQProcessor& processor;

    EQDisplay display;

    // Top bar.
    juce::Label titleLabel, brandLabel;
    juce::Label outputCaption;
    juce::Slider outputKnob;
    std::unique_ptr<SliderAttachment> outputAttachment;

    // Bottom-left: selected band.
    juce::Label bandTitleLabel;
    juce::Label freqCaption, gainCaption, qCaption;
    juce::Slider freqSlider, gainSlider, qSlider;
    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    int selectedBand = -1;

    // Bottom-right: signature section.
    juce::Label signatureLabel;
    juce::Slider heatKnob, tiltKnob;
    juce::Label heatCaption, tiltCaption;
    std::unique_ptr<SliderAttachment> heatAttachment, tiltAttachment;
    HeatLookAndFeel heatLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QEQEditor)
};
