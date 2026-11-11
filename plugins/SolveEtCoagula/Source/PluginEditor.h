#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"
#include "SceneUI.h"

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
    suite::ui::TabBar viewTabs;
    std::unique_ptr<suite::ui::ScenePanel> scenePanel;
    suite::ui::Toggle chopsOn, chopsOctave;
    suite::ui::Choice chopsWhere, chopsPattern;
    suite::ui::Knob chopsLevel;
    juce::Label chopsStatus;
    juce::TextButton saveMidi { "Save vocal MIDI" };
    juce::Rectangle<int> bandArea, controlArea, chopsArea;
    std::array<float, Unmasker::numBands> cutShown {}, keyShown {};

    std::unique_ptr<suite::ui::TweakBanner> tweakBanner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolveEtCoagulaEditor)
};
