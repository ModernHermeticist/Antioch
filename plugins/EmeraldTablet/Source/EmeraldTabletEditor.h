#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "EmeraldTabletProcessor.h"
#include "SuiteUI.h"

class EmeraldTabletEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EmeraldTabletEditor (EmeraldTabletProcessor&);
    ~EmeraldTabletEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    EmeraldTabletProcessor& processor;
    suite::ui::LookAndFeel lnf;
    suite::ui::HeaderBar header;
    suite::ui::Toggle autoSend, broadcastAlone;
    juce::TextButton sendButton { "Send now" }, resetButton { "Reset" };
    juce::Rectangle<int> resultArea, chromaArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmeraldTabletEditor)
};
