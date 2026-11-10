#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"
#include <optional>

/** The song as coloured section blocks. Drag a boundary to move it, double-click to split,
    right-click for type/delete, click to select. Shows the playhead. */
class Timeline : public juce::Component
{
public:
    explicit Timeline (OuroborosProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    int selected = 0;
    std::function<void()> onSelectionChanged;
    std::optional<suite::SectionMap> preview;   // a Listen proposal shown instead of the map

private:
    float xForBar (double bar) const;      // bar is 1-based, may be fractional
    double barForX (float x) const;
    int boundaryNear (float x) const;     // index of the section whose start is under x, or -1

    OuroborosProcessor& processor;
    int dragging = -1;
};

/** Arrange: a few choices -> a generated map, shown as a preview to Apply or Discard. */
class ArrangePanel : public juce::Component
{
public:
    ArrangePanel (arrangement::GenSettings initial, double bpm, std::function<void (arrangement::GenSettings)> onGenerate);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    arrangement::GenSettings current() const;
    void updateDuration();

    double bpm;
    std::function<void (arrangement::GenSettings)> generate;
    juce::Slider bars { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox drops, intro, breakdown;
    juce::Label barsLabel, duration, dropsLabel, introLabel, breakdownLabel;
    juce::TextButton go { "Generate" };
};

class OuroborosEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OuroborosEditor (OuroborosProcessor&);
    ~OuroborosEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshSelection();

    OuroborosProcessor& processor;
    suite::ui::LookAndFeel lnf;

    juce::ComboBox templateBox;
    juce::Slider lengthSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label lengthCaption, nowLabel, nextLabel, selectionLabel, legend;
    suite::ui::Toggle broadcast;
    suite::ui::Choice snap;
    Timeline timeline;

    std::vector<std::unique_ptr<juce::TextButton>> markButtons, typeButtons;
    juce::TextButton deleteButton { "Delete section" };
    juce::TextButton listenButton { "LISTEN" }, proposeButton { "Propose map" }, applyButton { "Apply" }, discardButton { "Discard" };
    juce::Label listenStatus;
    juce::TextButton arrangeButton { "Arrange..." };
    arrangement::GenSettings lastArrange;
    void showArrange();
    bool wasPlaying = false;
    void propose();

    juce::Rectangle<int> songArea, markArea, selectArea, nowArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OuroborosEditor)
};
