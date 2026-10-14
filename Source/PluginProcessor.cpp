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

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kLowFreq, "Low Freq", freqRange (20.0f, 500.0f), 120.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kLowGain, "Low Gain", gainRange (-15.0f, 15.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kMid1Freq, "Mid 1 Freq", freqRange (100.0f, 2000.0f), 500.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kMid1Gain, "Mid 1 Gain", gainRange (-15.0f, 15.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kMid1Q, "Mid 1 Q",
        juce::NormalisableRange<float> (0.3f, 6.0f, 0.01f, 0.5f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kMid2Freq, "Mid 2 Freq", freqRange (800.0f, 12000.0f), 3000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kMid2Gain, "Mid 2 Gain", gainRange (-15.0f, 15.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kMid2Q, "Mid 2 Q",
        juce::NormalisableRange<float> (0.3f, 6.0f, 0.01f, 0.5f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kHighFreq, "High Freq", freqRange (3000.0f, 20000.0f), 10000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        kHighGain, "High Gain", gainRange (-15.0f, 15.0f), 0.0f));

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
    lastLowFreq = lastLowGain = -1.0f;
    lastMid1Freq = lastMid1Gain = lastMid1Q = -1.0f;
    lastMid2Freq = lastMid2Gain = lastMid2Q = -1.0f;
    lastHighFreq = lastHighGain = -1.0f;
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

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    chain.process (context);
}

//==============================================================================
void QEQProcessor::updateFilters()
{
    const float lowFreq  = apvts.getRawParameterValue (kLowFreq)->load();
    const float lowGain  = apvts.getRawParameterValue (kLowGain)->load();
    const float mid1Freq = apvts.getRawParameterValue (kMid1Freq)->load();
    const float mid1Gain = apvts.getRawParameterValue (kMid1Gain)->load();
    const float mid1Q    = apvts.getRawParameterValue (kMid1Q)->load();
    const float mid2Freq = apvts.getRawParameterValue (kMid2Freq)->load();
    const float mid2Gain = apvts.getRawParameterValue (kMid2Gain)->load();
    const float mid2Q    = apvts.getRawParameterValue (kMid2Q)->load();
    const float highFreq = apvts.getRawParameterValue (kHighFreq)->load();
    const float highGain = apvts.getRawParameterValue (kHighGain)->load();
    const float heat     = apvts.getRawParameterValue (kHeat)->load();
    const float tilt     = apvts.getRawParameterValue (kTilt)->load();
    const float output   = apvts.getRawParameterValue (kOutput)->load();

    const double sr = lastSampleRate;
    const float shelfQ = 0.7071f; // Butterworth-ish shelf slope

    if (lowFreq != lastLowFreq || lowGain != lastLowGain)
    {
        *chain.get<LowShelf>().coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                sr, lowFreq, shelfQ, juce::Decibels::decibelsToGain (lowGain));
        lastLowFreq = lowFreq; lastLowGain = lowGain;
    }

    if (mid1Freq != lastMid1Freq || mid1Gain != lastMid1Gain || mid1Q != lastMid1Q)
    {
        *chain.get<Mid1>().coefficients =
            *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sr, mid1Freq, mid1Q, juce::Decibels::decibelsToGain (mid1Gain));
        lastMid1Freq = mid1Freq; lastMid1Gain = mid1Gain; lastMid1Q = mid1Q;
    }

    if (mid2Freq != lastMid2Freq || mid2Gain != lastMid2Gain || mid2Q != lastMid2Q)
    {
        *chain.get<Mid2>().coefficients =
            *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sr, mid2Freq, mid2Q, juce::Decibels::decibelsToGain (mid2Gain));
        lastMid2Freq = mid2Freq; lastMid2Gain = mid2Gain; lastMid2Q = mid2Q;
    }

    if (highFreq != lastHighFreq || highGain != lastHighGain)
    {
        *chain.get<HighShelf>().coefficients =
            *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                sr, highFreq, shelfQ, juce::Decibels::decibelsToGain (highGain));
        lastHighFreq = highFreq; lastHighGain = highGain;
    }

    if (tilt != lastTilt)
    {
        // Global spectral tilt: lows duck while highs lift (or vice versa).
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

float QEQProcessor::getMagnitudeResponseDb (float frequencyHz) const
{
    const double sr = lastSampleRate;
    const float f = juce::jlimit (20.0f, 20000.0f, frequencyHz);

    const float lowFreq  = apvts.getRawParameterValue (kLowFreq)->load();
    const float lowGain  = apvts.getRawParameterValue (kLowGain)->load();
    const float mid1Freq = apvts.getRawParameterValue (kMid1Freq)->load();
    const float mid1Gain = apvts.getRawParameterValue (kMid1Gain)->load();
    const float mid1Q    = apvts.getRawParameterValue (kMid1Q)->load();
    const float mid2Freq = apvts.getRawParameterValue (kMid2Freq)->load();
    const float mid2Gain = apvts.getRawParameterValue (kMid2Gain)->load();
    const float mid2Q    = apvts.getRawParameterValue (kMid2Q)->load();
    const float highFreq = apvts.getRawParameterValue (kHighFreq)->load();
    const float highGain = apvts.getRawParameterValue (kHighGain)->load();
    const float tilt     = apvts.getRawParameterValue (kTilt)->load();
    const float output   = apvts.getRawParameterValue (kOutput)->load();

    const float shelfQ = 0.7071f;
    float totalDb = output;

    auto addFilter = [&] (juce::dsp::IIR::Coefficients<float>::Ptr&& c)
    {
        totalDb += biquadMagnitudeDb (c->coefficients, f, sr);
    };

    addFilter (juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sr, lowFreq, shelfQ, juce::Decibels::decibelsToGain (lowGain)));
    addFilter (juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sr, mid1Freq, mid1Q, juce::Decibels::decibelsToGain (mid1Gain)));
    addFilter (juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sr, mid2Freq, mid2Q, juce::Decibels::decibelsToGain (mid2Gain)));
    addFilter (juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sr, highFreq, shelfQ, juce::Decibels::decibelsToGain (highGain)));
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
