#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"

class SolveEtCoagulaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SolveEtCoagulaEditor (SolveEtCoagulaProcessor&);
    ~SolveEtCoagulaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SolveEtCoagulaProcessor& processor;
    suite::ui::LookAndFeel lnf;
    suite::ui::Knob depth, sensitivity, focus, mix;
    suite::ui::Choice speed;
    juce::Label status;
    juce::Rectangle<int> bandArea, controlArea;
    std::array<float, Unmasker::numBands> cutShown {}, keyShown {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolveEtCoagulaEditor)
};
