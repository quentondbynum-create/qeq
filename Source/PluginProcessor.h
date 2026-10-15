#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>

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
static constexpr int kQEQNumBands = 8;
static constexpr int kQEQMaxStages = 4;   // 48 dB/oct max for cuts

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
    QEQTiltShelf,
    QEQFlatTilt,
    QEQAllPass,
    QEQNumShapes
};

inline juce::String getQEQBandShapeName (int shape)
{
    static const char* names[QEQNumShapes] =
    {
        "Bell", "Low Shelf", "High Shelf",
        "Low Pass", "High Pass", "Notch", "Band Pass",
        "Tilt Shelf", "Flat Tilt", "All Pass"
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

// Per-band channel processing mode.
enum QEQBandMSMode
{
    QEQMS_Stereo = 0,
    QEQMS_Left,
    QEQMS_Right,
    QEQMS_Mid,
    QEQMS_Side,
    QEQNumMSModes
};

inline juce::String getQEQMSModeName (int mode)
{
    static const char* names[QEQNumMSModes] =
        { "Stereo", "Left", "Right", "Mid", "Side" };
    return juce::String (names[juce::jlimit (0, QEQNumMSModes - 1, mode)]);
}

// Dynamic EQ detector source.
enum QEQDynSource
{
    QEQDynInternal = 0,
    QEQDynExternal,
    QEQNumDynSources
};

// Which shapes support the dynamic EQ mode (Pro-Q: Bell and Shelf types).
inline bool qeqShapeSupportsDynamic (int shape)
{
    return shape == QEQBell || shape == QEQLowShelf || shape == QEQHighShelf
        || shape == QEQTiltShelf || shape == QEQFlatTilt || shape == QEQNotch;
}

// Slope choice index -> dB/oct and stage count.
inline int qeqSlopeDbPerOct (int slopeChoice)
{
    static const int slopes[4] = { 12, 24, 36, 48 };
    return slopes[juce::jlimit (0, 3, slopeChoice)];
}

//==============================================================================
// Parameter IDs. Per-band IDs are "b<band><name>", e.g. "b3freq".
struct QEQBandParamIds
{
    juce::String freq, gain, q, shape, slope, ms;
    juce::String solo, bypass;
    juce::String dyn, dynAtk, dynRel, dynThr, dynRng, dynSrc;
};

inline const QEQBandParamIds& getQEQBandParamIds (int band)
{
    static QEQBandParamIds ids[kQEQNumBands];
    static bool initialised = false;
    if (! initialised)
    {
        for (int b = 0; b < kQEQNumBands; ++b)
        {
            const juce::String p = "b" + juce::String (b);
            ids[b].freq   = p + "freq";
            ids[b].gain   = p + "gain";
            ids[b].q      = p + "q";
            ids[b].shape  = p + "shape";
            ids[b].slope  = p + "slope";
            ids[b].ms     = p + "ms";
            ids[b].solo   = p + "solo";
            ids[b].bypass = p + "bypass";
            ids[b].dyn    = p + "dyn";
            ids[b].dynAtk = p + "dynAtk";
            ids[b].dynRel = p + "dynRel";
            ids[b].dynThr = p + "dynThr";
            ids[b].dynRng = p + "dynRng";
            ids[b].dynSrc = p + "dynSrc";
        }
        initialised = true;
    }
    return ids[juce::jlimit (0, kQEQNumBands - 1, band)];
}

//==============================================================================
/*
    QEQ — character EQ for Headroom Audio.

    Signal path (per block):
      8 fully-configurable bands (shape/freq/gain/Q/slope/M-S/solo/bypass +
        dynamic EQ with internal/external detector)
        -> spectral tilt (low-shelf + high-shelf pair)
        -> Heat waveshaper (tanh saturation)
        -> dry/wet mix
        -> auto-gain trim + output gain

    CRITICAL: every IIR stage uses ProcessorDuplicator so left and right
    channels have INDEPENDENT filter state. A bare dsp::IIR::Filter shares
    one state across channels, which causes cross-channel contamination
    ("weird pan") and an incorrect filter response.
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

    static constexpr const char* kHeat     = "heat";      // %, 0..100
    static constexpr const char* kTilt     = "tilt";      // dB, -6..+6
    static constexpr const char* kOutput   = "output";    // dB, -24..+24
    static constexpr const char* kMix      = "mix";       // %, 0..100
    static constexpr const char* kAutoGain = "autoGain";  // bool

    // Editor size persistence (set by the editor, read on construction).
    int storedEditorWidth  = 1000;
    int storedEditorHeight = 700;

    // ---- per-band DSP state ----
    using DupFilter = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    struct BandDSP
    {
        DupFilter stages[kQEQMaxStages];
        int   numStages = 1;
        float envState  = 0.0f;   // dynamic detector envelope (linear)
        float dynGainDb = 0.0f;   // smoothed dynamic gain offset (dB)

        // Cached params to avoid redundant coefficient rebuilds.
        float lastFreq = -1.0f, lastGain = -1000.0f, lastQ = -1.0f;
        float lastDynGain = 0.0f;
        int   lastShape = -1, lastSlope = -1, lastMs = -1;
        bool  needsReset = true;
    };

    BandDSP bands[kQEQNumBands];

    // Combined magnitude response in dB at a frequency (UI curve display).
    // Respects per-band solo/bypass states. Excludes Heat (nonlinear).
    float getMagnitudeResponseDb (float frequencyHz) const;

    // Per-band magnitude response in dB (all stages combined).
    float getBandMagnitudeResponseDb (int bandIndex, float frequencyHz) const;

    // ---- per-band state accessors (for UI + DSP) ----
    int  getBandShape (int band) const;
    int  getBandSlope (int band) const;   // 0..3 -> 12/24/36/48 dB/oct
    int  getBandMSMode (int band) const;
    bool getBandSolo (int band) const;
    bool getBandBypassed (int band) const;
    bool isBandAudible (int band) const;  // solo/bypass resolved
    bool getBandDynOn (int band) const;

    // ---- spectrum analyzer feeds (UI only, lock-free) ----
    static constexpr int analyserFifoSize = 2048;
    juce::AbstractFifo analyserFifoPre  { analyserFifoSize };
    juce::AbstractFifo analyserFifoPost { analyserFifoSize };
    float analyserFifoDataPre[analyserFifoSize]  = {};
    float analyserFifoDataPost[analyserFifoSize] = {};

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    float getRawFloat (const juce::String& paramId) const
    {
        if (auto* p = apvts.getRawParameterValue (paramId))
            return p->load();
        return 0.0f;
    }

    // Build the per-stage coefficient sets for a band. Handles multi-stage
    // slopes (LP/HP) and two-stage tilts.
    static std::vector<juce::dsp::IIR::Coefficients<float>::Ptr>
        makeBandStages (int shape, int slopeChoice, double sampleRate,
                        float freqHz, float q, float gainDb);

    // (Re)build a band's filter stages from its current parameters plus a
    // dynamic gain offset (dB). Resets stage state when the topology changes.
    void rebuildBand (int band, float freqHz, float q, float gainDb,
                      int shape, int slopeChoice);

    // Process one band (M/S handling + dynamic EQ + stages).
    void processBand (int band, juce::AudioBuffer<float>& buffer,
                      const juce::AudioBuffer<float>& sidechain,
                      bool hasSidechain, int numSamples, int numChannels);

    void feedAnalyser (juce::AbstractFifo& fifo, float* fifoData,
                       const juce::AudioBuffer<float>& buffer);

    DupFilter tiltLow, tiltHigh;
    HeatShaper heatShaper;
    juce::dsp::Gain<float> outputGain, autoGainTrim;

    juce::AudioBuffer<float> dryBuffer;

    double lastSampleRate = 44100.0;

    float lastHeat = -1.0f, lastTilt = -1.0f, lastOutput = -1.0f;
    float lastMix = -1.0f;
    bool  lastAutoGain = false;
    float autoGainSmoothDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QEQProcessor)
};
