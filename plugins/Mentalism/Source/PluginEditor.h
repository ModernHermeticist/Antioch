#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"

class MentalismEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MentalismEditor (MentalismProcessor&);
    ~MentalismEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintLoudness (juce::Graphics&, juce::Rectangle<int>);
    void paintStereo (juce::Graphics&, juce::Rectangle<int>);
    void paintBalance (juce::Graphics&, juce::Rectangle<int>);
    void paintAdvice (juce::Graphics&, juce::Rectangle<int>);
    void paintArrangement (juce::Graphics&, juce::Rectangle<int>);
    suite::ui::TabBar balanceTabs { { "TONAL BALANCE", "ARRANGEMENT" }, { suite::ui::Colours::brass, suite::ui::Colours::ember } };
    juce::Rectangle<int> arrangementContent;

    MentalismProcessor& processor;
    suite::ui::LookAndFeel lnf;

    suite::ui::Choice loudTarget, tonalTarget;
    juce::TextButton resetButton { "Reset" }, loadButton { "Load reference..." };
    juce::Label referenceLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    MentalismProcessor::Report rep;
    juce::Rectangle<int> loudArea, stereoArea, balanceArea, adviceArea;
    juce::Rectangle<int> loudContent, stereoContent, balanceContent, adviceContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalismEditor)
};
