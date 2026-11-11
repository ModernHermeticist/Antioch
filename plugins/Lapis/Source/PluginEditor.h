#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"

class LapisEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LapisEditor (LapisProcessor&);
    ~LapisEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    LapisProcessor& processor;
    suite::ui::LookAndFeel lnf;
    suite::ui::Choice target, mode;
    suite::ui::Knob ceiling, clip, release, manualGain, toneLow, toneHigh;
    juce::TextButton resetButton { "Reset learning" }, fingerprintButton { "Master fingerprint..." };
    std::unique_ptr<juce::AlertWindow> nameWindow;
    void showFingerprintMenu();
    void showNote (const juce::String& text) { note = text; noteUntil = juce::Time::getMillisecondCounterHiRes() + 8000.0; status.setText (text, juce::dontSendNotification); }
    juce::String note;
    double noteUntil = 0.0;
    juce::Label status;
    juce::Rectangle<int> meterArea, controlArea;
    float grShown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LapisEditor)
};
