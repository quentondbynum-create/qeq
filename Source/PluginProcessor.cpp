#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
QEQProcessor::QEQProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

QEQProcessor::~QEQProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout QEQProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqRange = [] (float lo, float hi)
    {
        // Skewed so the knob sweeps logarithmically, like real EQ hardware.
        return juce::NormalisableRange<float> (lo, hi, 1.0f, 0.35f);
    };
    auto gainRange = [] (float lo, float hi)
    {
        return juce::NormalisableRange<float> (lo, hi, 0.1f);
    };
    auto qRange = []()
    {
        return juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f);
    };

    const char* bandNames[4] = { "Band 1", "Band 2", "Band 3", "Band 4" };
    const float defaultFreqs[4] = { 120.0f, 500.0f, 3000.0f, 10000.0f };
    const int   defaultShapes[4] = { QEQLowShelf, QEQBell, QEQBell, QEQHighShelf };

    for (int b = 0; b < 4; ++b)
    {
        const juce::String bn (bandNames[b]);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            kBandFreqIds[b], bn + " Freq", freqRange (20.0f, 20000.0f), defaultFreqs[b]));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            kBandGainIds[b], bn + " Gain", gainRange (-15.0f, 15.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            kBandQIds[b], bn + " Q", qRange(), 1.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            kBandShapeIds[b], bn + " Shape",
            getQEQBandShapeChoices(), defaultShapes[b]));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            kBandSoloIds[b], bn + " Solo", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            kBandBypassIds[b], bn + " Bypass", false));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kHeat, "Heat",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.5f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kTilt, "Tilt", gainRange (-6.0f, 6.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kOutput, "Output", gainRange (-24.0f, 24.0f), 0.0f));

    return layout;
}

//==============================================================================
void QEQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels      = static_cast<juce::uint32> (getTotalNumOutputChannels());

    chain.prepare (spec);

    // Force a full coefficient refresh on the first block.
    for (int b = 0; b < 4; ++b)
    {
        lastBandFreq[b] = lastBandGain[b] = lastBandQ[b] = -1.0f;
        lastBandShape[b] = -1;
    }
    lastHeat = lastTilt = lastOutput = -1.0f;
    updateFilters();
}

void QEQProcessor::releaseResources() {}

bool QEQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
}

void QEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    updateFilters();

    // Feed the spectrum analyzer FIFO with mono-summed input (pre-EQ).
    {
        const int numSamples = buffer.getNumSamples();
        const float* left  = buffer.getReadPointer (0);
        const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;

        int start1, size1, start2, size2;
        analyserFifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            analyserFifoData[start1 + i] = 0.5f * (left[i] + right[i]);
        for (int i = 0; i < size2; ++i)
            analyserFifoData[start2 + i] = 0.5f * (left[size1 + i] + right[size1 + i]);

        analyserFifo.finishedWrite (size1 + size2);
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    chain.process (context);
}

//==============================================================================
juce::dsp::IIR::Coefficients<float>::Ptr QEQProcessor::makeBandCoefficients (
    int shape, double sampleRate, float freqHz, float q, float gainDb)
{
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    const float f  = juce::jlimit (20.0f, 20000.0f, freqHz);
    const float qq = juce::jmax (0.05f, q);
    const float g  = juce::Decibels::decibelsToGain (gainDb);

    switch (juce::jlimit (0, QEQNumShapes - 1, shape))
    {
        case QEQLowShelf:  return Coeffs::makeLowShelf  (sampleRate, f, qq, g);
        case QEQHighShelf: return Coeffs::makeHighShelf (sampleRate, f, qq, g);
        case QEQLowPass:   return Coeffs::makeLowPass   (sampleRate, f, qq);
        case QEQHighPass:  return Coeffs::makeHighPass  (sampleRate, f, qq);
        case QEQNotch:
        {
            // JUCE's notch has no gain control; scale the numerator so the
            // gain knob still does something musical (uniform dB shift).
            auto c = Coeffs::makeNotch (sampleRate, f, qq);
            if (c != nullptr)
            {
                float* co = c->coefficients.begin(); // b0,b1,b2,a0,a1,a2
                co[0] *= g; co[1] *= g; co[2] *= g;
            }
            return c;
        }
        case QEQBandPass:
        {
            // JUCE's bandpass is normalised to 0 dB peak; scale the numerator
            // so the peak sits at the gain knob's value.
            auto c = Coeffs::makeBandPass (sampleRate, f, qq);
            if (c != nullptr)
            {
                float* co = c->coefficients.begin();
                co[0] *= g; co[1] *= g; co[2] *= g;
            }
            return c;
        }
        case QEQBell:
        default:           return Coeffs::makePeakFilter (sampleRate, f, qq, g);
    }
}

void QEQProcessor::setBandCoefficients (
    int band, juce::dsp::IIR::Coefficients<float>::Ptr c)
{
    if (c == nullptr)
        return;

    switch (band)
    {
        case 0: *chain.get<Band0>().coefficients = *c; break;
        case 1: *chain.get<Band1>().coefficients = *c; break;
        case 2: *chain.get<Band2>().coefficients = *c; break;
        case 3: *chain.get<Band3>().coefficients = *c; break;
        default: break;
    }
}

void QEQProcessor::setBandBypassed (int band, bool shouldBypass)
{
    switch (band)
    {
        case 0: chain.setBypassed<Band0> (shouldBypass); break;
        case 1: chain.setBypassed<Band1> (shouldBypass); break;
        case 2: chain.setBypassed<Band2> (shouldBypass); break;
        case 3: chain.setBypassed<Band3> (shouldBypass); break;
        default: break;
    }
}

//==============================================================================
int QEQProcessor::getBandShape (int band) const
{
    band = juce::jlimit (0, 3, band);
    return juce::jlimit (0, QEQNumShapes - 1,
                         (int) getRawFloat (kBandShapeIds[band]));
}

bool QEQProcessor::getBandSolo (int band) const
{
    return getRawFloat (kBandSoloIds[juce::jlimit (0, 3, band)]) > 0.5f;
}

bool QEQProcessor::getBandBypassed (int band) const
{
    return getRawFloat (kBandBypassIds[juce::jlimit (0, 3, band)]) > 0.5f;
}

bool QEQProcessor::isBandAudible (int band) const
{
    band = juce::jlimit (0, 3, band);

    bool anySolo = false;
    for (int b = 0; b < 4; ++b)
        if (getBandSolo (b)) { anySolo = true; break; }

    if (anySolo)
        return getBandSolo (band);
    return ! getBandBypassed (band);
}

//==============================================================================
void QEQProcessor::updateFilters()
{
    float freq[4], gain[4], q[4];
    int   shape[4];
    bool  solo[4], bypass[4];

    for (int b = 0; b < 4; ++b)
    {
        freq[b]   = getRawFloat (kBandFreqIds[b]);
        gain[b]   = getRawFloat (kBandGainIds[b]);
        q[b]      = getRawFloat (kBandQIds[b]);
        shape[b]  = juce::jlimit (0, QEQNumShapes - 1,
                                  (int) getRawFloat (kBandShapeIds[b]));
        solo[b]   = getRawFloat (kBandSoloIds[b]) > 0.5f;
        bypass[b] = getRawFloat (kBandBypassIds[b]) > 0.5f;
    }

    const float heat   = getRawFloat (kHeat);
    const float tilt   = getRawFloat (kTilt);
    const float output = getRawFloat (kOutput);

    const double sr = lastSampleRate;
    const bool anySolo = solo[0] || solo[1] || solo[2] || solo[3];

    for (int b = 0; b < 4; ++b)
    {
        // Solo wins: when any band is soloed, only soloed bands are audible.
        setBandBypassed (b, anySolo ? ! solo[b] : bypass[b]);

        if (freq[b]  != lastBandFreq[b]
            || gain[b]  != lastBandGain[b]
            || q[b]     != lastBandQ[b]
            || shape[b] != lastBandShape[b])
        {
            setBandCoefficients (b, makeBandCoefficients (
                shape[b], sr, freq[b], q[b], gain[b]));
            lastBandFreq[b] = freq[b]; lastBandGain[b] = gain[b];
            lastBandQ[b] = q[b];       lastBandShape[b] = shape[b];
        }
    }

    if (tilt != lastTilt)
    {
        // Global spectral tilt: lows duck while highs lift (or vice versa).
        const float shelfQ = 0.7071f;
        *chain.get<TiltLow>().coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                sr, 400.0, shelfQ, juce::Decibels::decibelsToGain (-tilt));
        *chain.get<TiltHigh>().coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                sr, 4000.0, shelfQ, juce::Decibels::decibelsToGain (tilt));
        lastTilt = tilt;
    }

    if (heat != lastHeat)
    {
        // 1x = clean, up to 10x = heavy saturation.
        chain.get<Heat>().drive = 1.0f + heat * 0.09f;
        lastHeat = heat;
    }

    if (output != lastOutput)
    {
        chain.get<Output>().setGainDecibels (output);
        lastOutput = output;
    }
}

//==============================================================================
// |H(e^jw)| for a JUCE biquad: coefficients are {b0,b1,b2,a0,a1,a2}, a0 == 1.
static float biquadMagnitudeDb (const juce::Array<float>& co, double frequencyHz, double sampleRate)
{
    jassert (co.size() >= 6);
    const double b0 = co[0], b1 = co[1], b2 = co[2];
    const double a0 = co[3], a1 = co[4], a2 = co[5];

    const double w = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
    const std::complex<double> z1 = std::exp (std::complex<double> (0.0, -w));
    const std::complex<double> z2 = z1 * z1;

    const std::complex<double> num = b0 + b1 * z1 + b2 * z2;
    const std::complex<double> den = a0 + a1 * z1 + a2 * z2;

    const double mag = std::abs (num) / juce::jmax (1.0e-12, std::abs (den));
    return juce::Decibels::gainToDecibels (mag);
}

float QEQProcessor::getBandMagnitudeResponseDb (int bandIndex, float frequencyHz) const
{
    bandIndex = juce::jlimit (0, 3, bandIndex);
    const float f = juce::jlimit (20.0f, 20000.0f, frequencyHz);

    auto c = makeBandCoefficients (getBandShape (bandIndex), lastSampleRate,
                                   getRawFloat (kBandFreqIds[bandIndex]),
                                   getRawFloat (kBandQIds[bandIndex]),
                                   getRawFloat (kBandGainIds[bandIndex]));

    if (c == nullptr)
        return 0.0f;

    return biquadMagnitudeDb (c->coefficients, f, lastSampleRate);
}

float QEQProcessor::getMagnitudeResponseDb (float frequencyHz) const
{
    const double sr = lastSampleRate;
    const float f = juce::jlimit (20.0f, 20000.0f, frequencyHz);

    const float tilt   = getRawFloat (kTilt);
    const float output = getRawFloat (kOutput);

    const float shelfQ = 0.7071f;
    float totalDb = output;

    // Only audible bands (solo/bypass resolved) contribute to the display.
    for (int b = 0; b < 4; ++b)
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

bool QEQProcessor::hasEditor() const
{
    return true;
}

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
        std::unique_ptr<juce::XmlElement> xml (state.createXml());
        copyXmlToBinary (*xml, destData);
    }
}

void QEQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QEQProcessor();
}
