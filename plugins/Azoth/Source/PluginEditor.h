#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SceneUI.h"

class AzothEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AzothEditor (AzothProcessor&);
    ~AzothEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AzothProcessor& processor;
    suite::ui::LookAndFeel lnf;
    suite::ui::Knob impact, crash, swell, down, roll, level, tone, rollPitch;
    suite::ui::Choice swellLen, rollStart, gap;
    suite::ui::Toggle follow, grooveCrash;
    std::array<juce::TextButton, 4> auditions;
    juce::Label status, next;
    juce::Rectangle<int> elementArea, shapeArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AzothEditor)
};
