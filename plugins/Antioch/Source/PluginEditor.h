#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AutopilotStrip.h"
#include "DirectorBus.h"
#include "SuiteUI.h"
#include "SceneUI.h"

class EchoPanel;
class ReverbPanel;
class ThrowPanel;

class AntiochEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AntiochEditor (AntiochProcessor&);
    ~AntiochEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AntiochProcessor& processor;
    suite::ui::LookAndFeel lnf;

    suite::ui::Toggle syncButton;
    juce::Label bpmLabel, keyStatus;

    suite::ui::Choice rootChoice;
    suite::ui::Toggle followButton;
    suite::ui::Knob octave, dry, echoLevel, verbLevel, throwLevel, output;

    suite::ui::TabBar tabs;
    std::unique_ptr<EchoPanel> echoPanel;
    std::unique_ptr<ReverbPanel> reverbPanel;
    std::unique_ptr<ThrowPanel> throwPanel;
    std::unique_ptr<suite::ui::ScenePanel> scenePanel;

    juce::Rectangle<int> masterArea, panelArea;

    std::unique_ptr<suite::ui::TweakBanner> tweakBanner;

    suite::ui::AutopilotStrip autopilotStrip;   // what the autopilot is doing on this track now

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AntiochEditor)
};
