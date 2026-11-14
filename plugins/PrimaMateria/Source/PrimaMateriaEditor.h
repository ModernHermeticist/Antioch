#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AutopilotStrip.h"
#include "DirectorBus.h"
#include "SceneUI.h"

class PrimaMateriaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PrimaMateriaEditor (PrimaMateriaProcessor&);
    ~PrimaMateriaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    PrimaMateriaProcessor& processor;
    suite::ui::LookAndFeel lnf;
    suite::ui::HeaderBar header;
    std::vector<suite::ui::Action> actions();
    juce::ComboBox roleBox;
    juce::TextButton saveRole { "Save as my role" };
    suite::ui::Toggle follow, ramp, laterOn, lowManager;
    suite::ui::Choice morph, gap, monoLows;
    std::vector<std::unique_ptr<suite::ui::Knob>> knobs;   // [type * 3 + row]
    juce::Label status;
    std::unique_ptr<suite::ui::Knob> trim, breath, pump, sweep, vocalRoom, rideVocal, inputGain;
    std::unique_ptr<suite::ui::Choice> answer;
    juce::TextButton gainStageButton { "Gain-stage all tracks" };
    juce::TextButton suggestionsButton { "Suggested mixer moves..." }, previewButton { "Preview" };
    std::unique_ptr<juce::FileChooser> chooser;
    void showSuggestions();
    std::unique_ptr<suite::ui::Toggle> autoStage;
    juce::Label inputLabel;
    juce::Rectangle<int> gridArea;
    std::array<juce::Rectangle<int>, pm::numColumns> columns;
    int shownType = -2;

    suite::ui::AutopilotStrip autopilotStrip;   // what the autopilot is doing on this track now

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrimaMateriaEditor)
};
