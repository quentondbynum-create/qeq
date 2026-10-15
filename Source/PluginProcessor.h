#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
// tanh saturator with a settable drive amount. Normalised so quiet signals
// pass at unity gain; increasing drive adds harmonics and gentle limiting.
struct HeatShaper
{
    void prepare (const juce::dsp::ProcessSpec&) {}
    void reset() {}

    template <typename ProcessContext>
    void process (const ProcessContext& context) noexcept
    {
        if (context.isBypassed)
            return;

        auto&& block = context.getOutputBlock();
        const float norm = 1.0f / std::tanh (drive);

        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            float* data = block.getChannelPointer (ch);
            const auto n = block.getNumSamples();

            for (size_t i = 0; i < n; ++i)
                data[i] = std::tanh (drive * data[i]) * norm;
        }
    }

    float drive = 1.0f; // 1 = clean, up to 10 = heavy saturation
};

//==============================================================================
// Band shape indices. MUST match the order of the AudioParameterChoice
// string list created in createParameterLayout().
enum QEQBandShape
{
    QEQBell = 0,
    QEQLowShelf,
    QEQHighShelf,
    QEQLowPass,
    QEQHighPass,
    QEQNotch,
    QEQBandPass,
    QEQNumShapes
};

inline juce::String getQEQBandShapeName (int shape)
{
    static const char* names[QEQNumShapes] =
    {
        "Bell", "Low Shelf", "High Shelf",
        "Low Pass", "High Pass", "Notch", "Band Pass"
    };
    return juce::String (names[juce::jlimit (0, QEQNumShapes - 1, shape)]);
}

inline juce::StringArray getQEQBandShapeChoices()
{
    juce::StringArray choices;
    for (int s = 0; s < QEQNumShapes; ++s)
        choices.add (getQEQBandShapeName (s));
    return choices;
}

//==============================================================================
/*
    QEQ — character EQ for Headroom Audio.

    Signal path:
      4 fully-configurable bands (shape/freq/gain/Q + solo/bypass each)
        -> spectral tilt (low-shelf + high-shelf pair)
        -> Heat waveshaper (tanh saturation)
        -> output gain
*/
class QEQProcessor : public juce::AudioProcessor
{
public:
    QEQProcessor();
    ~QEQProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- parameters ----
    juce::AudioProcessorValueTreeState apvts;

    static constexpr const char* kLowFreq  = "lowFreq";   // Hz, 20..20000
    static constexpr const char* kLowGain  = "lowGain";   // dB, -15..+15
    static constexpr const char* kLowQ     = "lowQ";      // 0.1..10
    static constexpr const char* kMid1Freq = "mid1Freq";  // Hz, 20..20000
    static constexpr const char* kMid1Gain = "mid1Gain";  // dB, -15..+15
    static constexpr const char* kMid1Q    = "mid1Q";     // 0.1..10
    static constexpr const char* kMid2Freq = "mid2Freq";  // Hz, 20..20000
    static constexpr const char* kMid2Gain = "mid2Gain";  // dB, -15..+15
    static constexpr const char* kMid2Q    = "mid2Q";     // 0.1..10
    static constexpr const char* kHighFreq = "highFreq";  // Hz, 20..20000
    static constexpr const char* kHighGain = "highGain";  // dB, -15..+15
    static constexpr const char* kHighQ    = "highQ";     // 0.1..10
    static constexpr const char* kHeat     = "heat";      // %, 0..100 (tanh drive)
    static constexpr const char* kTilt     = "tilt";      // dB, -6..+6 (spectral tilt)
    static constexpr const char* kOutput   = "output";    // dB, -24..+24

    // Per-band parameter ID tables (band 0..3).
    static constexpr const char* kBandFreqIds[4]   = { kLowFreq,  kMid1Freq,  kMid2Freq,  kHighFreq };
    static constexpr const char* kBandGainIds[4]   = { kLowGain,  kMid1Gain,  kMid2Gain,  kHighGain };
    static constexpr const char* kBandQIds[4]      = { kLowQ,     kMid1Q,     kMid2Q,     kHighQ };
    static constexpr const char* kBandShapeIds[4]  = { "bandShape0",  "bandShape1",  "bandShape2",  "bandShape3" };
    static constexpr const char* kBandSoloIds[4]   = { "bandSolo0",   "bandSolo1",   "bandSolo2",   "bandSolo3" };
    static constexpr const char* kBandBypassIds[4] = { "bandBypass0", "bandBypass1", "bandBypass2", "bandBypass3" };

    // Combined linear magnitude response in dB at a frequency (used by the
    // UI curve display; the waveshaper is nonlinear and excluded).
    // Respects per-band solo/bypass states.
    float getMagnitudeResponseDb (float frequencyHz) const;

    // Per-band linear magnitude response in dB, respecting the band's shape.
    // Used by the UI curve display.
    float getBandMagnitudeResponseDb (int bandIndex, float frequencyHz) const;

    // ---- per-band state accessors (thread-safe, for UI + DSP) ----
    int  getBandShape (int band) const;
    bool getBandSolo (int band) const;
    bool getBandBypassed (int band) const;   // raw bypass parameter
    bool isBandAudible (int band) const;     // solo/bypass resolved

    // ---- spectrum analyzer feed (UI only) ----
    // processBlock pushes mono-summed input here; the editor's display
    // component pulls it on a timer and runs an FFT. Lock-free.
    static constexpr int analyserFifoSize = 2048;
    juce::AbstractFifo analyserFifo { analyserFifoSize };
    float analyserFifoData[analyserFifoSize] = {};

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Reads APVTS values and refreshes filter coefficients / shaper / gain.
    // Called every block; only touches the DSP when a value actually changed.
    void updateFilters();

    // Build IIR coefficients for a band from its shape/freq/Q/gain.
    static juce::dsp::IIR::Coefficients<float>::Ptr
        makeBandCoefficients (int shape, double sampleRate,
                              float freqHz, float q, float gainDb);

    void setBandCoefficients (int band,
                              juce::dsp::IIR::Coefficients<float>::Ptr c);
    void setBandBypassed (int band, bool shouldBypass);

    float getRawFloat (const char* paramId) const
    {
        if (auto* p = apvts.getRawParameterValue (paramId))
            return p->load();
        return 0.0f;
    }

    enum ChainIndex
    {
        Band0 = 0,
        Band1 = 1,
        Band2 = 2,
        Band3 = 3,
        TiltLow  = 4,
        TiltHigh = 5,
        Heat     = 6,
        Output   = 7
    };

    using Filter = juce::dsp::IIR::Filter<float>;

    juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter, Filter, Filter,
                              HeatShaper,
                              juce::dsp::Gain<float>> chain;

    double lastSampleRate = 44100.0;

    // Cached parameter values so coefficient updates only happen on change.
    float lastBandFreq[4]  = { -1.0f, -1.0f, -1.0f, -1.0f };
    float lastBandGain[4]  = { -1.0f, -1.0f, -1.0f, -1.0f };
    float lastBandQ[4]     = { -1.0f, -1.0f, -1.0f, -1.0f };
    int   lastBandShape[4] = { -1, -1, -1, -1 };
    float lastHeat  = -1.0f, lastTilt = -1.0f, lastOutput = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QEQProcessor)
};
