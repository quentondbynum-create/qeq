#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

//==============================================================================
// Per-band metadata shared by the display and the bottom panel.
struct QEQBandInfo
{
    QEQBandParamIds ids;
    juce::Colour colour;
};

inline const QEQBandInfo& getQEQBandInfo (int index)
{
    static QEQBandInfo bands[kQEQNumBands];
    static bool initialised = false;
    if (! initialised)
    {
        static const juce::uint32 colours[kQEQNumBands] =
        {
            0xffFF9F1C, 0xff2EC4B6, 0xff4CC9F0, 0xffB5179E,
            0xffFFD500, 0xff80ED99, 0xff9D4EDD, 0xffFF6B35,
        };
        for (int b = 0; b < kQEQNumBands; ++b)
        {
            bands[b].ids    = getQEQBandParamIds (b);
            bands[b].colour = juce::Colour (colours[b]);
        }
        initialised = true;
    }
    return bands[juce::jlimit (0, kQEQNumBands - 1, index)];
}

//==============================================================================
// Factory preset definition: full plugin state for one preset.
struct QEQFactoryPreset
{
    const char* name;
    float freq[kQEQNumBands];
    float gain[kQEQNumBands];
    float q[kQEQNumBands];
    int   shape[kQEQNumBands];
    int   slope[kQEQNumBands];
    int   ms[kQEQNumBands];
    float heat;
    float tilt;
    float output;
    float mix;
};

//==============================================================================
// Pro-Q style interactive display: log-frequency grid, real-time spectrum
// analyzer (pre/post, freeze), piano roll, per-band + total EQ curves,
// draggable band handles, and spectrum grab (double-click to create).
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
        b = juce::jlimit (0, kQEQNumBands - 1, b);
        if (b != selectedBand) { selectedBand = b; repaint(); }
    }

    // Display options (set by the editor's bottom-bar controls).
    void setDisplayRangeDb (float rangeDb) { displayRangeDb = rangeDb; repaint(); }
    void setShowPiano (bool s)             { showPiano = s; repaint(); }
    void setFrozen (bool f)                { frozen = f; }
    void setAnalyserPre (bool pre)         { analyserPre = pre; }

    // Fired when the user clicks a handle (the editor re-points its sliders).
    std::function<void (int)> onBandSelected;
    // Fired after spectrum grab creates/retargets a band.
    std::function<void (int)> onBandGrabbed;

private:
    void timerCallback() override;

    // Pixel <-> value mapping (log frequency, linear dB).
    float freqToX (float freqHz) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;

    // Index of the handle within grabRadius of pos, or -1.
    int bandAtPosition (juce::Point<float> pos) const;

    juce::AudioParameterFloat* getFloatParam (const juce::String& paramId) const;
    float getBandFreq (int band) const;
    float getBandGain (int band) const;
    void setFloatParamValue (const juce::String& paramId, float value);
    void setChoiceParam (const juce::String& paramId, int choiceIndex);
    void setBoolParam (const juce::String& paramId, bool value);

    // Right-click popup menu for a band handle.
    void showBandMenu (int band, juce::Point<int> screenPos);

    // Spectrum grab: double-click empty space creates/retargets a band.
    void spectrumGrab (juce::Point<float> pos);

    // Spectrum analyzer: pull FIFO samples into our ring, run FFT, smooth.
    void updateSpectrum();

    static juce::String noteNameForFreq (float freqHz);

    QEQProcessor& processor;
    int selectedBand = 0;

    juce::Rectangle<float> plotArea;

    float displayRangeDb = 12.0f;
    bool showPiano = false;
    bool frozen = false;
    bool analyserPre = false; // false = post-EQ (Pro-Q default)

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
    float scopeLevels[fftSize / 2] = {};
    float ring[fftSize] = {};
    int ringWritePos = 0;
    int ringCount = 0;
    bool ringFilled = false;
    double analyserSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQDisplay)
};

//==============================================================================
// Rotary knob with an orange glow that intensifies with the value,
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
    int bottomPanelHeight() const;
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Re-point the bottom-panel controls at the given band's parameters.
    void selectBand (int index);

    // Refresh shape/slope/dynamics-dependent UI.
    void updateBandDependentUI();

    // APVTS listener.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    // Bottom-bar handlers.
    void onAnalyserModeClicked (bool pre);
    void onRangeClicked();
    void onPianoClicked();
    void onFreezeClicked();
    void onAutoGainClicked();

    static void styleHorizontalSlider (juce::Slider& s, const juce::String& suffix,
                                       const juce::Colour& accent);
    static void styleComboBox (juce::ComboBox& box);
    static void styleToggleButton (juce::TextButton& btn,
                                   const juce::Colour& onColour);
    static void styleSmallButton (juce::TextButton& btn);

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
    void goToPreset (int direction);
    void updateHeartButton();
    void setFloatParam (const juce::String& paramId, float value);
    void setChoiceParam (const juce::String& paramId, int choice);
    void setBoolParam (const juce::String& paramId, bool value);

    static const QEQFactoryPreset factoryPresets[8];

    QEQProcessor& processor;

    EQDisplay display;

    // Top bar.
    juce::Label titleLabel, brandLabel;
    juce::Image logoImage;
    juce::ComboBox presetBox;
    juce::TextButton prevPresetBtn, nextPresetBtn, savePresetBtn, heartBtn;
    juce::String currentPresetName;
    juce::StringArray favoriteNames;
    juce::StringArray userPresetNames;
    juce::StringArray presetMenuLookup;
    juce::Label outputCaption;
    juce::Slider outputKnob;
    std::unique_ptr<SliderAttachment> outputAttachment;

    // Bottom-left: selected band.
    juce::Label bandTitleLabel;
    juce::Label freqCaption, gainCaption, qCaption;
    juce::Label shapeCaption, slopeCaption, msCaption;
    juce::Slider freqSlider, gainSlider, qSlider;
    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    juce::ComboBox shapeBox, slopeBox, msBox;
    std::unique_ptr<ComboBoxAttachment> shapeAttachment, slopeAttachment, msAttachment;
    juce::TextButton soloBtn, bypassBtn;
    std::unique_ptr<ButtonAttachment> soloAttachment, bypassAttachment;
    int selectedBand = -1;

    // Bottom-middle: dynamics.
    juce::Label dynTitleLabel;
    juce::TextButton dynBtn;
    std::unique_ptr<ButtonAttachment> dynAttachment;
    juce::ComboBox dynSrcBox;
    std::unique_ptr<ComboBoxAttachment> dynSrcAttachment;
    juce::Label dynAtkCaption, dynRelCaption, dynThrCaption, dynRngCaption;
    juce::Slider dynAtkSlider, dynRelSlider, dynThrSlider, dynRngSlider;
    std::unique_ptr<SliderAttachment> dynAtkAttachment, dynRelAttachment,
                                      dynThrAttachment, dynRngAttachment;

    // Bottom-middle-right: global.
    juce::Label mixCaption, analyserCaption;
    juce::Slider mixKnob;
    std::unique_ptr<SliderAttachment> mixAttachment;
    juce::TextButton analyserPreBtn, analyserPostBtn, freezeBtn;
    juce::TextButton rangeBtn, pianoBtn, autoGainBtn;
    std::unique_ptr<ButtonAttachment> autoGainAttachment;

    // Bottom-right: signature.
    juce::Label signatureLabel;
    juce::Slider heatKnob, tiltKnob;
    juce::Label heatCaption, tiltCaption;
    std::unique_ptr<SliderAttachment> heatAttachment, tiltAttachment;
    HeatLookAndFeel heatLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QEQEditor)
};
