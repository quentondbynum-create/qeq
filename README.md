# QEQ — character EQ

**Headroom Audio** · v0.1.0

A 4-band character EQ with saturation and spectral tilt, built with JUCE 7.

## Signal path

1. **LOW** — low shelf, 20–500 Hz, ±15 dB
2. **MID 1** — peak, 100–2000 Hz, ±15 dB, Q 0.3–6
3. **MID 2** — peak, 800–12000 Hz, ±15 dB, Q 0.3–6
4. **HIGH** — high shelf, 3–20 kHz, ±15 dB
5. **TILT** — global spectral tilt, −6…+6 dB (lows duck as highs lift)
6. **HEAT** — tanh waveshaper saturation, 0–100%
7. **OUT** — output gain, −24…+24 dB

The UI shows a live filter-curve display (combined magnitude response of the
linear stages) in the same dark/purple QFlip theme.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

Artefacts land in `build/QEQ_artefacts/`. First build downloads JUCE 7.0.12
via CMake FetchContent (~150 MB, cached afterwards).

## Parameters

All parameters live in `AudioProcessorValueTreeState`; filter coefficients
refresh from APVTS values each block (only when changed). State save/recall
via the standard APVTS XML blob.
