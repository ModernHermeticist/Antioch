#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AutopilotStrip.h"
#include "DirectorBus.h"
#include "SuiteUI.h"
#include "Checklist.h"
#include "Masking.h"

/** Pre-export checklist: pass / warn / fail for what Mentalism has measured. */
class ChecklistPanel : public juce::Component, private juce::Timer
{
public:
    explicit ChecklistPanel (MentalismProcessor& p);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { refresh(); }
    void refresh();
    void resized() override;
    MentalismProcessor& processor;
    std::vector<checklist::Item> items;
    juce::TextButton fixButton, reportButton { "Save mix report" };
    suite::Fix pendingFix = suite::Fix::None;
    uint32_t sentSeq = 0;
};

/** Fix history: every fix and rough mix sent from Mentalism, with Undo for each. */
class FixHistoryPanel : public juce::Component, private juce::Timer
{
public:
    explicit FixHistoryPanel (MentalismProcessor& p) : processor (p) { refresh(); setSize (560, 360); startTimerHz (4); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { refresh(); }
    void refresh();

    MentalismProcessor& processor;
    std::vector<MentalismProcessor::FixRecord> records;
    juce::OwnedArray<juce::TextButton> undoButtons;
};

class MentalismEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MentalismEditor (MentalismProcessor&);
    ~MentalismEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintLoudness (juce::Graphics&, juce::Rectangle<int>);
    void paintStereo (juce::Graphics&, juce::Rectangle<int>);
    void paintBalance (juce::Graphics&, juce::Rectangle<int>);
    void paintAdvice (juce::Graphics&, juce::Rectangle<int>);
    void paintArrangement (juce::Graphics&, juce::Rectangle<int>);
    suite::ui::TabBar balanceTabs { { "TONAL BALANCE", "ARRANGEMENT", "MASKING" },
                                    { suite::ui::Colours::brass, suite::ui::Colours::ember, suite::ui::Colours::amethyst } };
    void paintMasking (juce::Graphics&, juce::Rectangle<int>);
    void updateCarveButtons();
    std::vector<masking::Clash> clashes;
    std::array<juce::TextButton, 5> carveButtons;
    juce::Rectangle<int> arrangementContent, arrangementNotes;

    // Fix-it buttons, one per shown problem, plus undo and a confirmation line.
    std::array<juce::TextButton, 4> fixButtons;
    std::array<suite::Fix, 4> shownFixes {};
    std::array<uint32_t, 4> shownFixData {};
    juce::TextButton undoFixButton { "Undo last fix" };
    juce::Label fixStatus;
    uint32_t lastFixSeq = 0, lastUndoSeq = 0;
    suite::Fix lastFix = suite::Fix::None;
    int fixStatusTicks = 0;
    void sendFix (int row);
    void setParameterFromEditor (const juce::String& id, float plainValue);
    void layoutFixButtons();
    void updateFixStatus();

    MentalismProcessor& processor;
    suite::ui::LookAndFeel lnf;

    suite::ui::Choice loudTarget, tonalTarget;
    juce::TextButton resetButton { "Reset" }, loadButton { "Load reference..." }, roughMixButton { "Rough mix" }, roughUndoButton { "Undo" }, historyButton { "History" }, likeMeButton { "Like me?" }, checklistButton { "Export checklist" },
                     matchButton { "Match reference" }, mySoundButton { "Learn my sound..." };
    uint32_t matchSeq = 0;
    juce::TextButton finishButton { "Finish the song" };
    juce::TextButton problemsButton { "Problems" };
    std::unique_ptr<suite::ui::Toggle> problemWatch;
    std::unique_ptr<suite::ui::Toggle> autoCoach, compareAB, autoMasking, autoBalance;
    juce::Label automationStatus;
    juce::Label roughStatus;
    juce::Label referenceLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    MentalismProcessor::Report rep;
    juce::Rectangle<int> loudArea, stereoArea, balanceArea, adviceArea;
    juce::Rectangle<int> loudContent, stereoContent, balanceContent, adviceContent;

    suite::ui::AutopilotStrip autopilotStrip;   // what the autopilot is doing on this track now

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalismEditor)
};
