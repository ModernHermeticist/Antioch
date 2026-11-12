#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AutopilotStrip.h"
#include "SuiteUI.h"

class QuintessenceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit QuintessenceEditor (QuintessenceProcessor&);
    ~QuintessenceEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    void drawCurve (juce::Graphics&, juce::Rectangle<int>);

    QuintessenceProcessor& processor;
    suite::ui::LookAndFeel lnf;
    suite::ui::Toggle autoEq, autoMb;
    suite::ui::Choice role;
    juce::TextButton relearnButton { "Re-learn" }, tipsButton { "Spire tips" };
    std::vector<std::unique_ptr<suite::ui::Knob>> bandKnobs, mbKnobs;
    std::vector<std::unique_ptr<suite::ui::Toggle>> bandLocks, mbLocks;
    suite::ui::Knob lowCut, highCut, width;
    suite::ui::Toggle lowLock, highLock, widthLock;
    juce::Rectangle<int> curveArea, controlArea, cutArea, mbArea, logArea;
    std::array<juce::Rectangle<int>, qn::numSlots> slotAreas;
    std::array<juce::Rectangle<int>, qn::numMbBands> meterAreas;

    suite::ui::AutopilotStrip autopilotStrip;   // what the autopilot is doing on this track now

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuintessenceEditor)
};
