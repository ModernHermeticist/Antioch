#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SceneUI.h"
#include "AutopilotStrip.h"
#include "DirectorBus.h"

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
    suite::ui::HeaderBar header;
    std::vector<suite::ui::Action> actions();
    int heightFor (bool advanced) const { return advanced ? 1014 : 800; }
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
    suite::ui::Toggle preLift, riserChords, rollPitchSpeed, impactTune;
    suite::ui::Choice rollFrom, rollTo, rollCurve;
    suite::ui::Knob rollVel;
    juce::TextButton samplesButton { "Samples and presets..." };
    juce::Label samplesNote;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> nameWindow;
    void showSamplesMenu();
    void updateSamplesNote();
    juce::Rectangle<int> elementArea, shapeArea, riserArea, reverseArea, entranceArea;

    suite::ui::AutopilotStrip directorStrip;   // the suite director's cue for this plugin

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AzothEditor)
};
