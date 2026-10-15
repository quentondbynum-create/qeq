#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <complex>

//==============================================================================
QEQProcessor::QEQProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    // See prepareDSP(): the DSP must be valid from construction, because a
    // host may call processBlock before prepareToPlay.
    prepareDSP (44100.0, 512);
}

QEQProcessor::~QEQProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout QEQProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqRange = []()
    {
        return juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.35f);
    };
    auto gainRange = []()
    {
        return juce::NormalisableRange<float> (-30.0f, 30.0f, 0.1f);
    };
    auto qRange = []()
    {
        return juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f);
    };

    const float defaultFreqs[kQEQNumBands] =
        { 80.0f, 200.0f, 600.0f, 1500.0f, 4000.0f, 9000.0f, 12000.0f, 16000.0f };
    const int defaultShapes[kQEQNumBands] =
        { QEQHighPass, QEQLowShelf, QEQBell, QEQBell,
          QEQBell, QEQBell, QEQHighShelf, QEQLowPass };

    juce::StringArray slopeChoices { "12 dB/oct", "24 dB/oct",
                                     "36 dB/oct", "48 dB/oct" };
    juce::StringArray msChoices { "Stereo", "Left", "Right", "Mid", "Side" };
    juce::StringArray dynSrcChoices { "Internal", "External" };

    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        const juce::String bn = "Band " + juce::String (b + 1);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ids.freq, bn + " Freq", freqRange(), defaultFreqs[b]));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ids.gain, bn + " Gain", gainRange(), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ids.q, bn + " Q", qRange(), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            ids.shape, bn + " Shape", getQEQBandShapeChoices(), defaultShapes[b]));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            ids.slope, bn + " Slope", slopeChoices, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            ids.ms, bn + " Channel Mode", msChoices, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            ids.solo, bn + " Solo", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            ids.bypass, bn + " Bypass", false));

        // Dynamic EQ.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            ids.dyn, bn + " Dynamic", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ids.dynAtk, bn + " Dyn Attack",
            juce::NormalisableRange<float> (0.1f, 1000.0f, 0.1f, 0.4f), 10.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ids.dynRel, bn + " Dyn Release",
            juce::NormalisableRange<float> (10.0f, 5000.0f, 1.0f, 0.4f), 100.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ids.dynThr, bn + " Dyn Threshold",
            juce::NormalisableRange<float> (-60.0f, 0.0f, 0.5f), -24.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            ids.dynRng, bn + " Dyn Range",
            juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), -6.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            ids.dynSrc, bn + " Dyn Source", dynSrcChoices, 0));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kHeat, "Heat", juce::NormalisableRange<float> (0.0f, 100.0f, 0.5f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kTilt, "Tilt", juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kOutput, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kMix, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.5f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        kAutoGain, "Auto Gain", false));

    return layout;
}

//==============================================================================
void QEQProcessor::prepareDSP (double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock));
    spec.numChannels      = 2; // always stereo-capable; mono uses ch 0

    for (int b = 0; b < kQEQNumBands; ++b)
    {
        for (int s = 0; s < kQEQMaxStages; ++s)
            bands[b].stages[s].prepare (spec);
        bands[b].needsReset = true;
        bands[b].envState = 0.0f;
        bands[b].dynGainDb = 0.0f;
        bands[b].lastFreq = -1.0f;
    }

    tiltLow.prepare (spec);
    tiltHigh.prepare (spec);
    heatShaper.prepare (spec);
    outputGain.prepare (spec);
    autoGainTrim.prepare (spec);

    dryBuffer.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);

    lastHeat = lastTilt = lastOutput = lastMix = -1.0f;
    lastAutoGain = false;
    autoGainSmoothDb = 0.0f;
}

void QEQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepareDSP (sampleRate, samplesPerBlock);
}

void QEQProcessor::releaseResources() {}

bool QEQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getChannelSet (true, 0);
    const auto mainOut = layouts.getChannelSet (false, 0);

    if (mainOut != juce::AudioChannelSet::mono()
        && mainOut != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (mainIn != mainOut)
        return false;
   #endif

    // Sidechain bus (index 1): may be disabled, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (sc != juce::AudioChannelSet::disabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
// Build the per-stage coefficients for a band. Returns one entry per stage.
// LP/HP cascade N biquads for the selected slope; tilts use two shelves.
std::vector<juce::dsp::IIR::Coefficients<float>::Ptr>
QEQProcessor::makeBandStages (int shape, int slopeChoice, double sampleRate,
                              float freqHz, float q, float gainDb)
{
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    std::vector<Coeffs::Ptr> stages;

    const float f  = juce::jlimit (20.0f, 20000.0f, freqHz);
    const float qq = juce::jmax (0.05f, q);
    const float g  = juce::Decibels::decibelsToGain (gainDb);

    switch (juce::jlimit (0, QEQNumShapes - 1, shape))
    {
        case QEQLowShelf:
            stages.push_back (Coeffs::makeLowShelf (sampleRate, f, qq, g));
            break;

        case QEQHighShelf:
            stages.push_back (Coeffs::makeHighShelf (sampleRate, f, qq, g));
            break;

        case QEQLowPass:
        {
            const int n = juce::jlimit (1, kQEQMaxStages,
                                        qeqSlopeDbPerOct (slopeChoice) / 12);
            for (int i = 0; i < n; ++i)
                stages.push_back (Coeffs::makeLowPass (sampleRate, f, qq));
            break;
        }

        case QEQHighPass:
        {
            const int n = juce::jlimit (1, kQEQMaxStages,
                                        qeqSlopeDbPerOct (slopeChoice) / 12);
            for (int i = 0; i < n; ++i)
                stages.push_back (Coeffs::makeHighPass (sampleRate, f, qq));
            break;
        }

        case QEQNotch:
        {
            auto c = Coeffs::makeNotch (sampleRate, f, qq);
            if (c != nullptr)
            {
                float* co = c->coefficients.begin();
                co[0] *= g; co[1] *= g; co[2] *= g;
            }
            stages.push_back (c);
            break;
        }

        case QEQBandPass:
        {
            auto c = Coeffs::makeBandPass (sampleRate, f, qq);
            if (c != nullptr)
            {
                float* co = c->coefficients.begin();
                co[0] *= g; co[1] *= g; co[2] *= g;
            }
            stages.push_back (c);
            break;
        }

        case QEQTiltShelf:
        {
            // Seesaw around f: lows cut by g/2, highs boosted by g/2.
            const float half = juce::Decibels::decibelsToGain (gainDb * 0.5f);
            stages.push_back (Coeffs::makeLowShelf  (sampleRate, f, qq, 1.0f / half));
            stages.push_back (Coeffs::makeHighShelf (sampleRate, f, qq, half));
            break;
        }

        case QEQFlatTilt:
        {
            // True flat tilt: flat at the extreme lows/highs, tilting
            // through the middle around f. Built from two shelf pairs —
            // each pair sums to zero outside its band, so the extremes
            // stay flat:
            //   dip  (-g/2) between f/4 and f   via two low shelves
            //   hump (+g/2) between f and 4*f   via two high shelves
            // Two-octave-wide bands so the tilt reaches its full depth
            // despite the shelves' gradual Q=0.71 transitions.
            const float half = juce::Decibels::decibelsToGain (gainDb * 0.5f);
            const float invHalf = 1.0f / half;
            const float fLo = juce::jlimit (20.0f, 20000.0f, f * 0.25f);
            const float fHi = juce::jlimit (20.0f, 20000.0f, f * 4.0f);
            constexpr float shelfQ = 0.71f;
            stages.push_back (Coeffs::makeLowShelf  (sampleRate, fLo, shelfQ, half));
            stages.push_back (Coeffs::makeLowShelf  (sampleRate, f,   shelfQ, invHalf));
            stages.push_back (Coeffs::makeHighShelf (sampleRate, f,   shelfQ, half));
            stages.push_back (Coeffs::makeHighShelf (sampleRate, fHi, shelfQ, invHalf));
            break;
        }

        case QEQAllPass:
            stages.push_back (Coeffs::makeAllPass (sampleRate, f, qq));
            break;

        case QEQBell:
        default:
            stages.push_back (Coeffs::makePeakFilter (sampleRate, f, qq, g));
            break;
    }

    // Never return empty — fall back to a neutral bell.
    if (stages.empty() || stages[0] == nullptr)
    {
        stages.clear();
        stages.push_back (Coeffs::makePeakFilter (sampleRate, f, qq, 1.0f));
    }

    return stages;
}

void QEQProcessor::rebuildBand (int band, float freqHz, float q, float gainDb,
                                int shape, int slopeChoice)
{
    BandDSP& bd = bands[band];
    auto stages = makeBandStages (shape, slopeChoice, lastSampleRate,
                                  freqHz, q, gainDb);

    const int newNum = juce::jmin (kQEQMaxStages, (int) stages.size());
    const bool topologyChanged = (newNum != bd.numStages);

    // CRITICAL: ProcessorDuplicator's internal filters hold a ptr to the
    // shared coefficients object created at prepare() time. Reassigning
    // duplicator.state does NOT reach them — we must modify the shared
    // object in place via operator=.
    static const auto passthrough = []()
    {
        auto c = new juce::dsp::IIR::Coefficients<float>();
        // True passthrough: b0=1, all others 0.
        (*c) = []()
        {
            juce::dsp::IIR::Coefficients<float> p;
            p.coefficients = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
            return p;
        }();
        return c;
    }();

    bd.numStages = newNum;
    for (int s = 0; s < kQEQMaxStages; ++s)
    {
        if (s < newNum && stages[(size_t) s] != nullptr)
            *bd.stages[s].state = *stages[(size_t) s];
        else
            *bd.stages[s].state = *passthrough;
    }

    if (topologyChanged || bd.needsReset)
    {
        for (int s = 0; s < kQEQMaxStages; ++s)
            bd.stages[s].reset();
        bd.needsReset = false;
    }
}

//==============================================================================
void QEQProcessor::processBand (int band, juce::dsp::AudioBlock<float>& mainBlock,
                                const juce::AudioBuffer<float>& sidechain,
                                bool hasSidechain, int numSamples, int numChannels)
{
    BandDSP& bd = bands[band];
    const auto& ids = getQEQBandParamIds (band);
    const double sr = lastSampleRate;

    const float freq  = getRawFloat (ids.freq);
    const float gain  = getRawFloat (ids.gain);
    const float q     = getRawFloat (ids.q);
    const int   shape = juce::jlimit (0, QEQNumShapes - 1,
                                      (int) getRawFloat (ids.shape));
    const int   slope = juce::jlimit (0, 3, (int) getRawFloat (ids.slope));
    const int   ms    = juce::jlimit (0, QEQNumMSModes - 1,
                                      (int) getRawFloat (ids.ms));
    const bool  dynOn = getRawFloat (ids.dyn) > 0.5f
                        && qeqShapeSupportsDynamic (shape);

    //---- dynamic EQ detector ----
    float dynTarget = 0.0f;
    if (dynOn)
    {
        const int dynSrc = (int) getRawFloat (ids.dynSrc);
        const bool useExternal = (dynSrc == QEQDynExternal) && hasSidechain;

        const float* detL = useExternal ? sidechain.getReadPointer (0)
                                        : mainBlock.getChannelPointer (0);
        const int detCh = useExternal ? sidechain.getNumChannels() : numChannels;
        const float* detR = (detCh > 1)
            ? (useExternal ? sidechain.getReadPointer (1) : mainBlock.getChannelPointer (1))
            : detL;
        const int detN = useExternal ? sidechain.getNumSamples() : numSamples;

        const float atkMs = juce::jmax (0.1f, getRawFloat (ids.dynAtk));
        const float relMs = juce::jmax (1.0f,  getRawFloat (ids.dynRel));
        const float atkCoef = std::exp (-1.0f / (atkMs * 0.001f * (float) sr));
        const float relCoef = std::exp (-1.0f / (relMs * 0.001f * (float) sr));

        float env = bd.envState;
        const int n = juce::jmin (detN, numSamples);
        for (int i = 0; i < n; ++i)
        {
            const float a = 0.5f * (std::abs (detL[i]) + std::abs (detR[i]));
            if (a > env) env = atkCoef * env + (1.0f - atkCoef) * a;
            else         env = relCoef * env + (1.0f - relCoef) * a;
        }
        bd.envState = env;

        const float envDb = juce::Decibels::gainToDecibels (env + 1.0e-9f);
        const float thrDb = getRawFloat (ids.dynThr);
        const float over  = envDb - thrDb;
        const float amt   = juce::jlimit (0.0f, 1.0f, over / 12.0f);
        dynTarget = getRawFloat (ids.dynRng) * amt;
    }

    // Smooth the dynamic gain offset per block to avoid zipper noise.
    bd.dynGainDb += 0.25f * (dynTarget - bd.dynGainDb);

    //---- rebuild coefficients when anything changed ----
    const bool paramsChanged =
           freq  != bd.lastFreq  || gain != bd.lastGain
        || q     != bd.lastQ     || shape != bd.lastShape
        || slope != bd.lastSlope || ms != bd.lastMs;
    const bool dynChanged =
        std::abs (bd.dynGainDb - bd.lastDynGain) > 0.004f;

    if (paramsChanged || dynChanged || bd.needsReset)
    {
        if (ms != bd.lastMs)
            bd.needsReset = true; // stale per-channel state on mode change

        rebuildBand (band, freq, q, gain + bd.dynGainDb, shape, slope);

        bd.lastFreq = freq; bd.lastGain = gain; bd.lastQ = q;
        bd.lastShape = shape; bd.lastSlope = slope; bd.lastMs = ms;
        bd.lastDynGain = bd.dynGainDb;
    }

    //---- M/S encode (in place: L=mid, R=side) ----
    const bool useMS = (ms == QEQMS_Mid || ms == QEQMS_Side) && numChannels > 1;
    float* L = mainBlock.getChannelPointer (0);
    float* R = numChannels > 1 ? mainBlock.getChannelPointer (1) : nullptr;

    if (useMS)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = L[i], r = R[i];
            L[i] = 0.5f * (l + r);
            R[i] = 0.5f * (l - r);
        }
    }

    //---- run the stages ----
    // mainBlock is already restricted to the prepared channel count, so the
    // duplicators can never index past their internal filters.
    int chToProcess = -1; // -1 = all channels
    if (ms == QEQMS_Left || ms == QEQMS_Mid)        chToProcess = 0;
    else if (ms == QEQMS_Right || ms == QEQMS_Side) chToProcess = 1;

    if (chToProcess < 0 || numChannels < 2)
    {
        for (int s = 0; s < bd.numStages; ++s)
            bd.stages[s].process (
                juce::dsp::ProcessContextReplacing<float> (mainBlock));
    }
    else
    {
        auto chBlock = mainBlock.getSingleChannelBlock ((size_t) chToProcess);
        juce::dsp::ProcessContextReplacing<float> ctx (chBlock);
        for (int s = 0; s < bd.numStages; ++s)
            bd.stages[s].process (ctx);
    }

    //---- M/S decode ----
    if (useMS)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = L[i], s = R[i];
            L[i] = m + s;
            R[i] = m - s;
        }
    }
}

//==============================================================================
void QEQProcessor::feedAnalyser (juce::AbstractFifo& fifo, float* fifoData,
                                 const juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const float* left  = buffer.getReadPointer (0);
    const float* right = buffer.getNumChannels() > 1
                         ? buffer.getReadPointer (1) : left;

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
        fifoData[start1 + i] = 0.5f * (left[i] + right[i]);
    for (int i = 0; i < size2; ++i)
        fifoData[start2 + i] = 0.5f * (left[size1 + i] + right[size1 + i]);

    fifo.finishedWrite (size1 + size2);
}

//==============================================================================
void QEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();

    // Defensive: never dereference channel pointers on an empty buffer.
    // (A host should never send one, but crashing is not an option.)
    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    // Sidechain bus (index 1). May be unconnected.
    const juce::AudioBuffer<float> scBuffer = getBusBuffer (buffer, true, 1);
    const bool hasSidechain = scBuffer.getNumChannels() > 0
                              && scBuffer.getNumSamples() >= numSamples;

    // Pre-EQ analyzer feed (input signal).
    feedAnalyser (analyserFifoPre, analyserFifoDataPre, buffer);

    // Dry copy for the mix knob.
    if (dryBuffer.getNumChannels() < numChannels
        || dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize (numChannels, numSamples, false, false, true);
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // The host buffer may carry MORE channels than the main bus (the AU
    // wrapper appends sidechain-bus channels to the same buffer). Every dsp
    // object is prepared for 2 channels max, so restrict the processing
    // block to the main-bus channels. (Feeding extra channels to the
    // ProcessorDuplicators crashed Logic 2026-10-15.)
    juce::dsp::AudioBlock<float> mainBlock (buffer);
    auto dspBlock = mainBlock.getSubsetChannelBlock (0, (size_t) numChannels);

    //---- EQ bands ----
    for (int b = 0; b < kQEQNumBands; ++b)
        if (isBandAudible (b))
            processBand (b, dspBlock, scBuffer, hasSidechain, numSamples, numChannels);

    //---- spectral tilt ----
    {
        const float tilt = getRawFloat (kTilt);
        if (tilt != lastTilt)
        {
            const double sr = lastSampleRate;
            const float shelfQ = 0.7071f;
            // Modify in place: the duplicator's internal filters share this object.
            *tiltLow.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                sr, 400.0, shelfQ, juce::Decibels::decibelsToGain (-tilt));
            *tiltHigh.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                sr, 4000.0, shelfQ, juce::Decibels::decibelsToGain (tilt));
            lastTilt = tilt;
        }

        juce::dsp::AudioBlock<float>& block = dspBlock;
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        tiltLow.process (ctx);
        tiltHigh.process (ctx);
    }

    //---- Heat ----
    {
        const float heat = getRawFloat (kHeat);
        if (heat != lastHeat)
        {
            heatShaper.drive = 1.0f + heat * 0.09f;
            lastHeat = heat;
        }

        juce::dsp::AudioBlock<float>& block = dspBlock;
        heatShaper.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    //---- Mix (dry/wet) ----
    {
        const float mix = juce::jlimit (0.0f, 100.0f, getRawFloat (kMix)) / 100.0f;
        if (mix < 0.999f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* wet = buffer.getWritePointer (ch);
                const float* dry = dryBuffer.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    wet[i] = dry[i] + (wet[i] - dry[i]) * mix;
            }
        }
        lastMix = mix;
    }

    //---- Auto Gain (compensates the mean of the static EQ curve) ----
    {
        const bool autoOn = getRawFloat (kAutoGain) > 0.5f;
        float targetDb = 0.0f;

        if (autoOn)
        {
            // Mean of the total static curve over 24 log-spaced points.
            float sum = 0.0f;
            for (int i = 0; i < 24; ++i)
            {
                const float t = (float) i / 23.0f;
                const float f = std::pow (10.0f, std::log10 (20.0f)
                                          + t * (std::log10 (20000.0f) - std::log10 (20.0f)));
                sum += getMagnitudeResponseDb (f);
            }
            targetDb = -(sum / 24.0f);
            targetDb = juce::jlimit (-24.0f, 24.0f, targetDb);
        }

        // Smooth toward the target to avoid jumps.
        autoGainSmoothDb += 0.05f * (targetDb - autoGainSmoothDb);
        if (std::abs (autoGainSmoothDb) < 0.01f)
            autoGainSmoothDb = 0.0f;

        autoGainTrim.setGainDecibels (autoGainSmoothDb);
        lastAutoGain = autoOn;

        juce::dsp::AudioBlock<float>& block = dspBlock;
        autoGainTrim.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    //---- Output gain ----
    {
        const float output = getRawFloat (kOutput);
        if (output != lastOutput)
        {
            outputGain.setGainDecibels (output);
            lastOutput = output;
        }

        juce::dsp::AudioBlock<float>& block = dspBlock;
        outputGain.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // Post-EQ analyzer feed (output signal).
    feedAnalyser (analyserFifoPost, analyserFifoDataPost, buffer);
}

//==============================================================================
int QEQProcessor::getBandShape (int band) const
{
    band = juce::jlimit (0, kQEQNumBands - 1, band);
    return juce::jlimit (0, QEQNumShapes - 1,
                         (int) getRawFloat (getQEQBandParamIds (band).shape));
}

int QEQProcessor::getBandSlope (int band) const
{
    band = juce::jlimit (0, kQEQNumBands - 1, band);
    return juce::jlimit (0, 3, (int) getRawFloat (getQEQBandParamIds (band).slope));
}

int QEQProcessor::getBandMSMode (int band) const
{
    band = juce::jlimit (0, kQEQNumBands - 1, band);
    return juce::jlimit (0, QEQNumMSModes - 1,
                         (int) getRawFloat (getQEQBandParamIds (band).ms));
}

bool QEQProcessor::getBandSolo (int band) const
{
    return getRawFloat (getQEQBandParamIds (
        juce::jlimit (0, kQEQNumBands - 1, band)).solo) > 0.5f;
}

bool QEQProcessor::getBandBypassed (int band) const
{
    return getRawFloat (getQEQBandParamIds (
        juce::jlimit (0, kQEQNumBands - 1, band)).bypass) > 0.5f;
}

bool QEQProcessor::isBandAudible (int band) const
{
    band = juce::jlimit (0, kQEQNumBands - 1, band);

    bool anySolo = false;
    for (int b = 0; b < kQEQNumBands; ++b)
        if (getBandSolo (b)) { anySolo = true; break; }

    if (anySolo)
        return getBandSolo (band);
    return ! getBandBypassed (band);
}

bool QEQProcessor::getBandDynOn (int band) const
{
    return getRawFloat (getQEQBandParamIds (
        juce::jlimit (0, kQEQNumBands - 1, band)).dyn) > 0.5f;
}

//==============================================================================
// |H(e^jw)| for a JUCE biquad. JUCE normalises a0 to 1 and stores the
// coefficients as {b0, b1, b2, a1, a2} (5 elements, a0 omitted).
static float biquadMagnitudeDb (const juce::Array<float>& co, double frequencyHz,
                                double sampleRate)
{
    jassert (co.size() >= 5);
    const double b0 = co[0], b1 = co[1], b2 = co[2];
    const double a1 = co[3], a2 = co[4];

    const double w = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
    const std::complex<double> z1 = std::exp (std::complex<double> (0.0, -w));
    const std::complex<double> z2 = z1 * z1;

    const std::complex<double> num = b0 + b1 * z1 + b2 * z2;
    const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;

    const double mag = std::abs (num) / juce::jmax (1.0e-12, std::abs (den));
    return juce::Decibels::gainToDecibels (mag);
}

float QEQProcessor::getBandMagnitudeResponseDb (int bandIndex, float frequencyHz) const
{
    bandIndex = juce::jlimit (0, kQEQNumBands - 1, bandIndex);
    const float f = juce::jlimit (20.0f, 20000.0f, frequencyHz);
    const auto& ids = getQEQBandParamIds (bandIndex);

    auto stages = makeBandStages (getBandShape (bandIndex),
                                  getBandSlope (bandIndex),
                                  lastSampleRate,
                                  getRawFloat (ids.freq),
                                  getRawFloat (ids.q),
                                  getRawFloat (ids.gain));

    float totalDb = 0.0f;
    for (auto& c : stages)
        if (c != nullptr)
            totalDb += biquadMagnitudeDb (c->coefficients, f, lastSampleRate);

    return totalDb;
}

float QEQProcessor::getMagnitudeResponseDb (float frequencyHz) const
{
    const double sr = lastSampleRate;
    const float f = juce::jlimit (20.0f, 20000.0f, frequencyHz);

    const float tilt   = getRawFloat (kTilt);
    const float output = getRawFloat (kOutput);

    const float shelfQ = 0.7071f;
    float totalDb = output;

    for (int b = 0; b < kQEQNumBands; ++b)
        if (isBandAudible (b))
            totalDb += getBandMagnitudeResponseDb (b, f);

    auto addFilter = [&] (juce::dsp::IIR::Coefficients<float>::Ptr&& c)
    {
        totalDb += biquadMagnitudeDb (c->coefficients, f, sr);
    };

    addFilter (juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sr, 400.0, shelfQ, juce::Decibels::decibelsToGain (-tilt)));
    addFilter (juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sr, 4000.0, shelfQ, juce::Decibels::decibelsToGain (tilt)));

    return totalDb;
}

//==============================================================================
juce::AudioProcessorEditor* QEQProcessor::createEditor()
{
    return new QEQEditor (*this);
}

bool QEQProcessor::hasEditor() const                     { return true; }
const juce::String QEQProcessor::getName() const          { return JucePlugin_Name; }
bool QEQProcessor::acceptsMidi() const                    { return false; }
bool QEQProcessor::producesMidi() const                   { return false; }
bool QEQProcessor::isMidiEffect() const                   { return false; }
double QEQProcessor::getTailLengthSeconds() const          { return 0.0; }
int QEQProcessor::getNumPrograms()                        { return 1; }
int QEQProcessor::getCurrentProgram()                     { return 0; }
void QEQProcessor::setCurrentProgram (int)                {}
const juce::String QEQProcessor::getProgramName (int)     { return {}; }
void QEQProcessor::changeProgramName (int, const juce::String&) {}

void QEQProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        state.setProperty ("editorWidth",  storedEditorWidth,  nullptr);
        state.setProperty ("editorHeight", storedEditorHeight, nullptr);
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

void QEQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.isValid())
            {
                // Editor size (may be absent in older states).
                const int w = tree.getProperty ("editorWidth", 0);
                const int h = tree.getProperty ("editorHeight", 0);
                if (w >= 700 && h >= 500)
                {
                    storedEditorWidth  = w;
                    storedEditorHeight = h;
                }
                apvts.replaceState (tree);
            }
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QEQProcessor();
}
