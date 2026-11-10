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
    juce::Rectangle<int> arrangementContent, arrangementNotes;

    // Fix-it buttons, one per shown problem, plus undo and a confirmation line.
    std::array<juce::TextButton, 4> fixButtons;
    std::array<suite::Fix, 4> shownFixes {};
    juce::TextButton undoFixButton { "Undo last fix" };
    juce::Label fixStatus;
    uint32_t lastFixSeq = 0, lastUndoSeq = 0;
    suite::Fix lastFix = suite::Fix::None;
    int fixStatusTicks = 0;
    void sendFix (int row);
    void layoutFixButtons();
    void updateFixStatus();

    MentalismProcessor& processor;
    suite::ui::LookAndFeel lnf;

    suite::ui::Choice loudTarget, tonalTarget;
    juce::TextButton resetButton { "Reset" }, loadButton { "Load reference..." }, roughMixButton { "Rough mix" }, roughUndoButton { "Undo" };
    juce::Label roughStatus;
    juce::Label referenceLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    MentalismProcessor::Report rep;
    juce::Rectangle<int> loudArea, stereoArea, balanceArea, adviceArea;
    juce::Rectangle<int> loudContent, stereoContent, balanceContent, adviceContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalismEditor)
};
