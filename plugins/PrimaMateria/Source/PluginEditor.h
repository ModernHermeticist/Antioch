#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
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
    juce::ComboBox roleBox;
    suite::ui::Toggle follow, ramp;
    suite::ui::Choice morph, gap;
    std::vector<std::unique_ptr<suite::ui::Knob>> knobs;   // [type * 3 + row]
    juce::Label status;
    juce::Rectangle<int> gridArea;
    std::array<juce::Rectangle<int>, pm::numTypes> columns;
    int shownType = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrimaMateriaEditor)
};
