#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"

/** Big arc showing the live build amount, the timed-build bar count and DROP flashes. */
class BuildMeter : public juce::Component
{
public:
    explicit BuildMeter (CauseEffectProcessor& p) : processor (p) {}

    void update();
    void paint (juce::Graphics&) override;

private:
    CauseEffectProcessor& processor;
    float level = 0.0f;
    double progress = -1.0, lengthBars = 16.0;
    int lastDrops = 0;
    double dropFlashUntil = 0.0;
};

class CauseEffectEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CauseEffectEditor (CauseEffectProcessor&);
    ~CauseEffectEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    CauseEffectProcessor& processor;
    suite::ui::LookAndFeel lnf;

    BuildMeter meter;
    suite::ui::Knob buildKnob, curve;
    suite::ui::Toggle trigger, dropCut, followSections;
    suite::ui::Choice length;

    suite::ui::Knob filterMax, filterRes, wash, washDecay, noise, riser, stutter, widen, output;
    suite::ui::Choice riserRange, stutterMax, rootChoice;
    suite::ui::Toggle followKey;
    juce::Label keyStatus, hint;

    juce::Rectangle<int> buildArea, filterArea, washArea, riserArea, motionArea, keyArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CauseEffectEditor)
};
