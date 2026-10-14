#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

//==============================================================================
// Live filter-curve display: plots the combined magnitude response of the
// EQ bands + tilt + output gain on a log-frequency axis.
class FilterCurveDisplay : public juce::Component,
                           private juce::Timer
{
public:
    explicit FilterCurveDisplay (QEQProcessor& p);
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    QEQProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterCurveDisplay)
};

//==============================================================================
class QEQEditor : public juce::AudioProcessorEditor
{
public:
    explicit QEQEditor (QEQProcessor&);
    ~QEQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Creates a labelled rotary knob bound to an APVTS parameter.
    juce::Slider& addKnob (const char* paramId, const juce::String& labelText,
                           const juce::String& suffix);

    QEQProcessor& processor;

    FilterCurveDisplay curveDisplay;

    juce::Label titleLabel;

    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::Label>> knobLabels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;

    // Band section titles.
    juce::Label lowLabel, mid1Label, mid2Label, highLabel, masterLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QEQEditor)
};
