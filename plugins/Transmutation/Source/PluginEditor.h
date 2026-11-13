#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"
#include "AutopilotStrip.h"

/** The plan on the song: sections along the top, each move as a block, the play position. */
class PlanTimeline : public juce::Component
{
public:
    explicit PlanTimeline (TransmutationProcessor& p) : processor (p) {}
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onSelect;   // index into the plan's moves (-1 = none)
    int selected = -1;
    const trm::Plan& shownPlan() const { return plan; }

private:
    juce::Rectangle<float> rectFor (int index) const;
    double totalQ() const;
    TransmutationProcessor& processor;
    trm::Plan plan;
    suite::SectionMap map;
    bool haveMap = false;
    std::vector<int> lanes;
    int laneCount = 1;
    double pos = 0.0;
};

/** All 118 moves with Auto / Always / Never (click the right-hand word to change it). */
class EffectList : public juce::Component, private juce::ListBoxModel
{
public:
    explicit EffectList (TransmutationProcessor&);
    void resized() override;
    void setCategory (int c);   // -1 = all
    void refresh() { list.repaint(); }
    std::function<void (int)> onAudition;

private:
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::String getTooltipForRow (int row) override;
    TransmutationProcessor& processor;
    juce::ListBox list;
    std::vector<int> rows;
};

class TransmutationEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TransmutationEditor (TransmutationProcessor&);
    ~TransmutationEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showSelected();

    TransmutationProcessor& processor;
    suite::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 400 };

    suite::ui::Knob spice, builds, drops, amount, output;
    suite::ui::Choice genre, role;
    suite::ui::Toggle director;
    juce::Label status, nowPlaying, moveInfo;
    PlanTimeline timeline;
    juce::TextButton lockButton { "Lock this moment" }, rerollButton { "Re-roll this moment" }, neverButton { "Never use this move" },
                     playButton { "Play it now" }, rerollAllButton { "Re-roll the whole track" };
    juce::ComboBox category;
    EffectList effectList;
    suite::ui::AutopilotStrip autopilot;

    juce::Rectangle<int> leftArea, planArea, listArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransmutationEditor)
};
