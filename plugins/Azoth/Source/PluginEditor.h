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
    suite::ui::Knob impact, crash, swell, down, roll, level, tone, rollPitch, laterDrops;
    suite::ui::Choice swellLen, rollStart, gap;
    suite::ui::Toggle follow, grooveCrash, reverseOn;
    suite::ui::Knob reverseLevel, riser, trackRiser;
    suite::ui::Choice riserRange;
    juce::Label riserNote;
    suite::ui::Choice reverseTarget, reverseLength;
    juce::Label reverseNote;
    std::array<juce::TextButton, 4> auditions;
    juce::Label status, next;
    suite::ui::Choice dropFx, dropFxLen, buildShape;
    suite::ui::Knob buildFilter;
    suite::ui::Toggle preLift, riserChords;
    suite::ui::Choice rollFrom, rollTo, rollCurve;
    suite::ui::Knob rollVel;
    juce::TextButton samplesButton { "Your samples..." };
    juce::Label samplesNote;
    std::unique_ptr<juce::FileChooser> chooser;
    void showSamplesMenu();
    void updateSamplesNote();
    juce::Rectangle<int> elementArea, shapeArea, riserArea, reverseArea, entranceArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AzothEditor)
};
