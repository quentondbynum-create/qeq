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
    const char* qParam;
    const char* shapeParam;
    const char* soloParam;
    const char* bypassParam;
    juce::Colour colour;
};

inline const QEQBandInfo& getQEQBandInfo (int index)
{
    static const QEQBandInfo bands[4] =
    {
        { QEQProcessor::kBandFreqIds[0], QEQProcessor::kBandGainIds[0],
          QEQProcessor::kBandQIds[0],
          QEQProcessor::kBandShapeIds[0], QEQProcessor::kBandSoloIds[0],
          QEQProcessor::kBandBypassIds[0], juce::Colour (0xffFF9F1C) },
        { QEQProcessor::kBandFreqIds[1], QEQProcessor::kBandGainIds[1],
          QEQProcessor::kBandQIds[1],
          QEQProcessor::kBandShapeIds[1], QEQProcessor::kBandSoloIds[1],
          QEQProcessor::kBandBypassIds[1], juce::Colour (0xff2EC4B6) },
        { QEQProcessor::kBandFreqIds[2], QEQProcessor::kBandGainIds[2],
          QEQProcessor::kBandQIds[2],
          QEQProcessor::kBandShapeIds[2], QEQProcessor::kBandSoloIds[2],
          QEQProcessor::kBandBypassIds[2], juce::Colour (0xff4CC9F0) },
        { QEQProcessor::kBandFreqIds[3], QEQProcessor::kBandGainIds[3],
          QEQProcessor::kBandQIds[3],
          QEQProcessor::kBandShapeIds[3], QEQProcessor::kBandSoloIds[3],
          QEQProcessor::kBandBypassIds[3], juce::Colour (0xffB5179E) },
    };
    return bands[juce::jlimit (0, 3, index)];
}

//==============================================================================
// Factory preset definition: full plugin state for one preset.
struct QEQFactoryPreset
{
    const char* name;
    float freq[4];
    float gain[4];
    float q[4];
    int   shape[4];
    float heat;
    float tilt;
    float output;
};

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
    void setChoiceParam (const char* paramId, int choiceIndex);
    void setBoolParam (const char* paramId, bool value);

    // Right-click popup menu for a band handle.
    void showBandMenu (int band, juce::Point<int> screenPos);

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
class QEQEditor : public juce::AudioProcessorEditor,
                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit QEQEditor (QEQProcessor&);
    ~QEQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Re-point the bottom-panel controls at the given band's parameters.
    void selectBand (int index);

    // Refresh shape-dependent UI (gain slider enable, band title).
    void updateBandDependentUI();

    // APVTS listener: watch the 4 shape params.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    static void styleHorizontalSlider (juce::Slider& s, const juce::String& suffix,
                                       const juce::Colour& accent);
    static void styleComboBox (juce::ComboBox& box);
    static void styleToggleButton (juce::TextButton& btn,
                                   const juce::Colour& onColour);

    // ---- preset system ----
    juce::File getQEQDataDir() const;
    juce::File getPresetDir() const;
    juce::File getFavoritesFile() const;
    void scanUserPresets();
    void loadFavorites();
    void saveFavorites();
    bool isFavorite (const juce::String& name) const;
    void refreshPresetMenu();
    void onPresetChosen();
    void applyPresetByName (const juce::String& name);
    void applyFactoryPreset (const QEQFactoryPreset& preset);
    void applyUserPreset (const juce::String& name);
    void promptSavePreset();
    void goToPreset (int direction);   // -1 = prev, +1 = next
    void updateHeartButton();
    void setFloatParam (const char* paramId, float value);

    static const QEQFactoryPreset factoryPresets[8];

    QEQProcessor& processor;

    EQDisplay display;

    // Top bar.
    juce::Label titleLabel, brandLabel;
    juce::ComboBox presetBox;
    juce::TextButton prevPresetBtn, nextPresetBtn, savePresetBtn, heartBtn;
    juce::String currentPresetName;
    juce::StringArray favoriteNames;
    juce::StringArray userPresetNames;    // file base names, no extension
    juce::StringArray presetMenuLookup;   // parallel to combo item IDs
    juce::Label outputCaption;
    juce::Slider outputKnob;
    std::unique_ptr<SliderAttachment> outputAttachment;

    // Bottom-left: selected band.
    juce::Label bandTitleLabel;
    juce::Label freqCaption, gainCaption, qCaption, shapeCaption;
    juce::Slider freqSlider, gainSlider, qSlider;
    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    juce::ComboBox shapeBox;
    std::unique_ptr<ComboBoxAttachment> shapeAttachment;
    juce::TextButton soloBtn, bypassBtn;
    std::unique_ptr<ButtonAttachment> soloAttachment, bypassAttachment;
    int selectedBand = -1;

    // Bottom-right: signature section.
    juce::Label signatureLabel;
    juce::Slider heatKnob, tiltKnob;
    juce::Label heatCaption, tiltCaption;
    std::unique_ptr<SliderAttachment> heatAttachment, tiltAttachment;
    HeatLookAndFeel heatLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QEQEditor)
};
