/*
    QEQLifecycleTest — headless AU/DAW-lifecycle regression harness for QEQ.

    Drives the REAL QEQProcessor::processBlock through the lifecycles a host
    like Logic Pro exercises, which the pure-DSP tests did not cover:

      1. processBlock called before prepareToPlay (host edge case)
      2. stereo prepare + process with default params
      3. mono render buffers (Logic mono track) after stereo prepare
      4. getStateInformation / setStateInformation round-trip, then process
      5. every band x every shape x every slope x every M/S mode (rebuildBand
         topology changes, incl. 4-stage FlatTilt -> 1-stage Bell)
      6. dynamic EQ on, internal detector, then external sidechain fed via a
         4-channel buffer the way the AU wrapper lays out buses
      7. global param automation: heat / tilt / mix / output / auto-gain
      8. re-prepare at different rates / block sizes between renders
      9. concurrent getMagnitudeResponseDb reads (editor curve) while rendering

    Build (from ~/workspace/qeq):
      see Tests/README or the manual compile line used in CI.

    Run under AddressSanitizer; any SEGV / ASan report = regression.
*/

#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <thread>
#include <atomic>
#include <vector>
#include <random>

namespace
{
int gFailures = 0;
int gChecks = 0;

#define CHECK(cond) do { \
    ++gChecks; \
    if (!(cond)) { ++gFailures; \
        std::printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

void fillTestSignal (juce::AudioBuffer<float>& buf, int seed = 7)
{
    std::mt19937 rng ((unsigned) seed);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    const int nCh = buf.getNumChannels();
    const int n = buf.getNumSamples();
    for (int ch = 0; ch < nCh; ++ch)
    {
        float* d = buf.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / 44100.0f;
            d[i] = 0.3f * std::sin (2.0f * 3.14159265f * 220.0f * t + (float) ch)
                 + 0.2f * std::sin (2.0f * 3.14159265f * 3000.0f * t)
                 + 0.1f * dist (rng);
        }
    }
}

void setParam (QEQProcessor& p, const juce::String& id, float value)
{
    if (auto* par = p.apvts.getParameter (id))
        par->setValueNotifyingHost (par->convertTo0to1 (value));
}

float getParam (QEQProcessor& p, const juce::String& id)
{
    if (auto* v = p.apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void processBlocks (QEQProcessor& p, int numChannels, int blockSize,
                    int numBlocks, int seedBase = 7)
{
    juce::AudioBuffer<float> buf (numChannels, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < numBlocks; ++i)
    {
        fillTestSignal (buf, seedBase + i);
        p.processBlock (buf, midi);
        // Output must be finite — a NaN/Inf here means the DSP blew up.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int s = 0; s < blockSize; ++s)
                CHECK (std::isfinite (d[s]));
        }
    }
}

// ---- 1. processBlock before prepareToPlay ---------------------------------
void test_process_before_prepare()
{
    std::printf ("[1] processBlock before prepareToPlay...\n");
    QEQProcessor p;
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    fillTestSignal (buf);
    p.processBlock (buf, midi); // must not crash; output may be garbage
    std::printf ("    ok\n");
}

// ---- 2. stereo basic -------------------------------------------------------
void test_stereo_basic()
{
    std::printf ("[2] stereo prepare + process, defaults...\n");
    QEQProcessor p;
    p.prepareToPlay (44100.0, 512);
    processBlocks (p, 2, 512, 10);
    p.releaseResources();
    std::printf ("    ok\n");
}

// ---- 3. mono render buffer after stereo prepare (Logic mono track) ---------
void test_mono_buffer()
{
    std::printf ("[3] mono render buffers...\n");
    QEQProcessor p;
    p.prepareToPlay (44100.0, 512);
    processBlocks (p, 1, 512, 10);
    // also with per-band channel modes forced to Right/Side on mono
    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        setParam (p, ids.ms, (float) QEQMS_Right);
    }
    processBlocks (p, 1, 256, 6);
    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        setParam (p, ids.ms, (float) QEQMS_Side);
    }
    processBlocks (p, 1, 256, 6);
    p.releaseResources();
    std::printf ("    ok\n");
}

// ---- 4. state save/restore -------------------------------------------------
void test_state_roundtrip()
{
    std::printf ("[4] state save/restore...\n");
    QEQProcessor p;
    // dial in non-default state first
    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        setParam (p, ids.freq, 100.0f * (b + 1));
        setParam (p, ids.gain, (b % 2 == 0) ? 6.0f : -6.0f);
        setParam (p, ids.q, 2.5f);
        setParam (p, ids.shape, (float) (b % QEQNumShapes));
        setParam (p, ids.slope, (float) (b % 4));
        setParam (p, ids.ms, (float) (b % QEQNumMSModes));
    }
    setParam (p, QEQProcessor::kHeat, 25.0f);
    setParam (p, QEQProcessor::kTilt, -3.0f);

    juce::MemoryBlock state;
    p.getStateInformation (state);
    CHECK (state.getSize() > 0);

    QEQProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    q.prepareToPlay (48000.0, 1024);
    processBlocks (q, 2, 1024, 8);

    // restore defaults over the top, process again
    QEQProcessor r;
    juce::MemoryBlock defState;
    r.getStateInformation (defState);
    q.setStateInformation (defState.getData(), (int) defState.getSize());
    processBlocks (q, 2, 512, 4);
    q.releaseResources();
    std::printf ("    ok\n");
}

// ---- 5. shape/slope/ms sweep (topology changes) ----------------------------
void test_topology_sweep()
{
    std::printf ("[5] shape/slope/channel-mode sweep...\n");
    QEQProcessor p;
    p.prepareToPlay (44100.0, 512);

    const int shapesToTest[QEQNumShapes] =
        { QEQBell, QEQLowShelf, QEQHighShelf, QEQLowPass, QEQHighPass,
          QEQNotch, QEQBandPass, QEQTiltShelf, QEQFlatTilt, QEQAllPass };

    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        for (int s = 0; s < QEQNumShapes; ++s)
        {
            setParam (p, ids.shape, (float) shapesToTest[s]);
            for (int slope = 0; slope < 4; ++slope)
            {
                setParam (p, ids.slope, (float) slope);
                for (int ms = 0; ms < QEQNumMSModes; ++ms)
                {
                    setParam (p, ids.ms, (float) ms);
                    // also wiggle freq/q/gain so coefficients rebuild
                    setParam (p, ids.freq, 50.0f + 40.0f * (float) (b * 7 + s));
                    setParam (p, ids.q, 0.5f + 0.3f * (float) slope);
                    setParam (p, ids.gain, -9.0f + 3.0f * (float) ms);
                    processBlocks (p, 2, 512, 1, 1000 + b * 100 + s * 10 + slope);
                }
            }
        }
        // solo + bypass toggles
        setParam (p, ids.solo, 1.0f);
        processBlocks (p, 2, 512, 2);
        setParam (p, ids.solo, 0.0f);
        setParam (p, ids.bypass, 1.0f);
        processBlocks (p, 2, 512, 2);
        setParam (p, ids.bypass, 0.0f);
    }
    p.releaseResources();
    std::printf ("    ok\n");
}

// ---- 6. dynamic EQ, internal + external sidechain ---------------------------
void test_dynamic_eq()
{
    std::printf ("[6] dynamic EQ (internal + external sidechain)...\n");
    QEQProcessor p;
    p.prepareToPlay (44100.0, 512);

    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        setParam (p, ids.shape, (float) QEQBell);
        setParam (p, ids.dyn, 1.0f);
        setParam (p, ids.dynAtk, 5.0f);
        setParam (p, ids.dynRel, 80.0f);
        setParam (p, ids.dynThr, -30.0f);
        setParam (p, ids.dynRng, -9.0f);
        setParam (p, ids.dynSrc, (float) QEQDynInternal);
    }
    processBlocks (p, 2, 512, 8);

    // External sidechain: AU-style bus layout — main stereo + sidechain
    // stereo appended in one buffer. getBusBuffer(buffer, true, 1) picks
    // up channels 2-3.
    {
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < kQEQNumBands; ++b)
            setParam (p, getQEQBandParamIds (b).dynSrc, (float) QEQDynExternal);
        for (int i = 0; i < 8; ++i)
        {
            fillTestSignal (buf, 500 + i);
            p.processBlock (buf, midi);
        }
    }

    // extreme detector settings
    for (int b = 0; b < kQEQNumBands; ++b)
    {
        const auto& ids = getQEQBandParamIds (b);
        setParam (p, ids.dynAtk, 0.1f);
        setParam (p, ids.dynRel, 5000.0f);
        setParam (p, ids.dynThr, -60.0f);
        setParam (p, ids.dynRng, 15.0f);
    }
    processBlocks (p, 2, 512, 6);
    p.releaseResources();
    std::printf ("    ok\n");
}

// ---- 7. global param automation --------------------------------------------
void test_global_automation()
{
    std::printf ("[7] heat/tilt/mix/output/auto-gain automation...\n");
    QEQProcessor p;
    p.prepareToPlay (44100.0, 512);

    for (int i = 0; i <= 20; ++i)
    {
        setParam (p, QEQProcessor::kHeat, 5.0f * (float) i);          // 0..100
        setParam (p, QEQProcessor::kTilt, -6.0f + 0.6f * (float) i);  // -6..+6
        setParam (p, QEQProcessor::kMix, 5.0f * (float) i);
        setParam (p, QEQProcessor::kOutput, -12.0f + 1.2f * (float) i);
        setParam (p, QEQProcessor::kAutoGain, (i % 2 == 0) ? 1.0f : 0.0f);
        processBlocks (p, 2, 512, 1, 900 + i);
    }
    p.releaseResources();
    std::printf ("    ok\n");
}

// ---- 8. re-prepare at different rates/sizes --------------------------------
void test_reprepare()
{
    std::printf ("[8] re-prepare across rates and block sizes...\n");
    QEQProcessor p;
    const double rates[3] = { 44100.0, 48000.0, 96000.0 };
    const int sizes[3] = { 64, 512, 2048 };
    for (int r = 0; r < 3; ++r)
        for (int s = 0; s < 3; ++s)
        {
            p.releaseResources();
            p.prepareToPlay (rates[r], sizes[s]);
            processBlocks (p, 2, sizes[s] / 2, 4); // host renders smaller
            processBlocks (p, 1, sizes[s] / 2, 2);  // ...and mono
        }
    p.releaseResources();
    std::printf ("    ok\n");
}

// ---- 9. concurrent curve reads while rendering ------------------------------
void test_concurrent_curve_reads()
{
    std::printf ("[9] concurrent magnitude-response reads...\n");
    QEQProcessor p;
    p.prepareToPlay (44100.0, 512);
    setParam (p, QEQProcessor::kAutoGain, 1.0f); // audio thread reads curve too

    std::atomic<bool> stop { false };
    std::thread reader ([&]
    {
        std::mt19937 rng (1234);
        std::uniform_real_distribution<float> f (20.0f, 20000.0f);
        while (! stop.load())
        {
            volatile float m = p.getMagnitudeResponseDb (f (rng));
            (void) m;
            for (int b = 0; b < kQEQNumBands; b += 3)
                (void) p.getBandMagnitudeResponseDb (b, f (rng));
        }
    });

    for (int i = 0; i < 40; ++i)
    {
        // wiggle params from the "UI thread" while rendering
        const int b = i % kQEQNumBands;
        const auto& ids = getQEQBandParamIds (b);
        setParam (p, ids.freq, 100.0f + 500.0f * (float) i);
        setParam (p, ids.shape, (float) (i % QEQNumShapes));
        processBlocks (p, 2, 512, 1, 2000 + i);
    }

    stop.store (true);
    reader.join();
    p.releaseResources();
    std::printf ("    ok\n");
}

} // namespace

int main()
{
    std::printf ("QEQ lifecycle regression harness\n");
    std::printf ("================================\n");

    if (std::getenv ("QEQ_SKIP_T1") == nullptr)
        test_process_before_prepare();
    else
        std::printf ("[1] skipped via QEQ_SKIP_T1\n");
    test_stereo_basic();
    test_mono_buffer();
    test_state_roundtrip();
    test_topology_sweep();
    test_dynamic_eq();
    test_global_automation();
    test_reprepare();
    test_concurrent_curve_reads();

    std::printf ("================================\n");
    std::printf ("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
