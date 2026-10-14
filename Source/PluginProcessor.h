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
/*
    QEQ — character EQ for Headroom Audio.

    Signal path:
      4-band EQ (low shelf / peak / peak / high shelf)
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

    static constexpr const char* kLowFreq  = "lowFreq";   // Hz, 20..500
    static constexpr const char* kLowGain  = "lowGain";   // dB, -15..+15
    static constexpr const char* kMid1Freq = "mid1Freq";  // Hz, 100..2000
    static constexpr const char* kMid1Gain = "mid1Gain";  // dB, -15..+15
    static constexpr const char* kMid1Q    = "mid1Q";     // 0.3..6
    static constexpr const char* kMid2Freq = "mid2Freq";  // Hz, 800..12000
    static constexpr const char* kMid2Gain = "mid2Gain";  // dB, -15..+15
    static constexpr const char* kMid2Q    = "mid2Q";     // 0.3..6
    static constexpr const char* kHighFreq = "highFreq";  // Hz, 3000..20000
    static constexpr const char* kHighGain = "highGain";  // dB, -15..+15
    static constexpr const char* kHeat     = "heat";      // %, 0..100 (tanh drive)
    static constexpr const char* kTilt     = "tilt";      // dB, -6..+6 (spectral tilt)
    static constexpr const char* kOutput   = "output";    // dB, -24..+24

    // Combined linear magnitude response in dB at a frequency (used by the
    // UI curve display; the waveshaper is nonlinear and excluded).
    float getMagnitudeResponseDb (float frequencyHz) const;

    // Per-band linear magnitude response in dB (band 0..3 = low shelf,
    // mid1 peak, mid2 peak, high shelf). Used by the UI curve display.
    float getBandMagnitudeResponseDb (int bandIndex, float frequencyHz) const;

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

    enum ChainIndex
    {
        LowShelf = 0,
        Mid1     = 1,
        Mid2     = 2,
        HighShelf= 3,
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
    float lastLowFreq  = -1.0f, lastLowGain  = -1.0f;
    float lastMid1Freq = -1.0f, lastMid1Gain = -1.0f, lastMid1Q = -1.0f;
    float lastMid2Freq = -1.0f, lastMid2Gain = -1.0f, lastMid2Q = -1.0f;
    float lastHighFreq = -1.0f, lastHighGain = -1.0f;
    float lastHeat  = -1.0f, lastTilt = -1.0f, lastOutput = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QEQProcessor)
};
