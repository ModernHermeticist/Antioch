#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AutopilotStrip.h"
#include "SuiteUI.h"
#include "Spectrum.h"
#include "SceneUI.h"

class VibrationEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VibrationEditor (VibrationProcessor&);
    ~VibrationEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    VibrationProcessor& processor;
    suite::ui::LookAndFeel lnf;

    suite::ui::Knob euphoria;
    juce::ComboBox presetBox;
    juce::Label macroInfo, autoGainInfo;
    suite::ui::Toggle autoGain;
    suite::ui::Knob output;
    suite::SpectrumView spectrum;
    suite::ui::TabBar viewTabs;
    std::unique_ptr<suite::ui::ScenePanel> scenePanel;

    suite::ui::Knob lowCut, mud, presence, air, drive, satMix, mbDepth, mbTime, width, monoBelow, pitchBright, tension;

    juce::TextButton fingerprintButton { "Lead fingerprint..." };
    juce::Label fingerprintStatus;
    std::unique_ptr<juce::AlertWindow> nameWindow;
    void showFingerprintMenu();

    juce::Rectangle<int> macroArea, spectrumArea, eqArea, satArea, mbArea, widthArea;

    std::unique_ptr<suite::ui::TweakBanner> tweakBanner;

    suite::ui::AutopilotStrip autopilotStrip;   // what the autopilot is doing on this track now

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VibrationEditor)
};
