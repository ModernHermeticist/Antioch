#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"
#include "SuiteCheck.h"
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
    int energyDragging = -1;               // section whose energy line is being dragged
    int energyLineNear (float x, float y) const;   // section whose energy line is under (x, y), or -1
    float yForEnergy (float energy) const;
};

/** Suite overview: every suite plugin loaded, on which track, and setup advice. */
class SuitePanel : public juce::Component, private juce::Timer
{
public:
    explicit SuitePanel (OuroborosProcessor* p = nullptr) : processor (p) { refresh(); setSize (660, 610); startTimerHz (2); }
    void paint (juce::Graphics&) override;
    void refresh();

private:
    void timerCallback() override { refresh(); }
    OuroborosProcessor* processor;
    std::vector<suite::SuiteEntry> entries;
    std::vector<suitecheck::Advice> advice;
    suitecheck::Advice next;
    juce::StringArray notes;
};

/** Snapshot per section: a snapshot slot for each section type. */
class SectionSnapPanel : public juce::Component
{
public:
    explicit SectionSnapPanel (OuroborosProcessor&);
    void paint (juce::Graphics& g) override { g.fillAll (suite::ui::Colours::panel); }
    void resized() override;
private:
    std::vector<std::unique_ptr<suite::ui::Choice>> choices;
    juce::Label note;
};

/** Arrange: a few choices -> a generated map, shown as a preview to Apply or Discard. */
/** Recipes: save the whole suite's settings under a name, or load a saved recipe. */
class RecipePanel : public juce::Component, private juce::Timer
{
public:
    explicit RecipePanel (OuroborosProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void send (suite::RecipeAction);
    juce::ComboBox names;
    juce::TextButton save { "Save" }, load { "Load" }, saveSignature { "Save as my signature" }, rollBack { "Roll back" };
    std::unique_ptr<suite::ui::Choice> autosave;
    juce::ComboBox history;
    juce::Label caption, result;
    uint32_t sentSeq = 0;
    juce::String sentWhat;
};

/** New song: a style and a key set up the whole suite. */
class SessionPanel : public juce::Component, private juce::Timer
{
public:
    explicit SessionPanel (OuroborosProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateInfo();
    juce::String moodText() const;

    OuroborosProcessor& processor;
    juce::ComboBox style, root, scale, mood;
    juce::ToggleButton signature { "Start from my signature" };
    juce::Label styleLabel, keyLabel, moodLabel, info, result;
    juce::TextButton go { "Set up the song" };
    uint32_t sentSeq = 0;
};

class ArrangePanel : public juce::Component
{
public:
    ArrangePanel (arrangement::GenSettings initial, double bpm, std::function<void (arrangement::GenSettings)> onGenerate,
                  std::function<void (int totalBars)> onLikeMine = {});
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    arrangement::GenSettings current() const;
    void updateDuration();

    double bpm;
    std::function<void (arrangement::GenSettings)> generate;
    std::function<void (int)> likeMine;
    juce::TextButton likeMineButton { "Like my songs" };
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
    std::vector<std::pair<juce::String, suite::SectionMap>> userTemplates;
    void fillTemplates();
    std::unique_ptr<juce::AlertWindow> nameWindow;
    suite::ui::Knob intensity;
    suite::ui::Toggle live;
    suite::ui::Choice entrance, ride;
    juce::Label liveHelp;
    std::unique_ptr<juce::FileChooser> markerChooser;
    void exportMarkers();
    juce::Rectangle<int> performArea;
    juce::Slider lengthSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label lengthCaption, nowLabel, nextLabel, selectionLabel, legend;
    suite::ui::Toggle broadcast;
    suite::ui::Choice snap, lift, finalSnap, dropGap;
    Timeline timeline;

    std::vector<std::unique_ptr<juce::TextButton>> markButtons, typeButtons;
    juce::TextButton deleteButton { "Delete section" };
    juce::TextButton listenButton { "LISTEN" }, proposeButton { "Propose map" }, applyButton { "Apply" }, discardButton { "Discard" };
    juce::Label listenStatus;

    // Whole-suite snapshots: click a slot to recall it (or pick an empty one), Save stores the
    // current settings of every suite plugin there; right-click a slot to clear it.
    struct SlotButton : juce::TextButton
    {
        bool rightClick = false;
        void clicked (const juce::ModifierKeys& m) override { rightClick = m.isPopupMenu(); }
    };
    std::array<SlotButton, suite::SuiteProcessor::numSnapshotSlots> slotButtons;
    juce::TextButton saveSnapshotButton { "Save" };
    juce::Label snapshotStatus;
    int currentSlot = 0, suiteTicks = 0;
    uint32_t lastSnapshotSeq = 0;
    juce::String lastSnapshotVerb;
    void snapshotCommand (suite::SnapshotAction, int slot);
    void refreshSnapshots();
    juce::TextButton arrangeButton { "Arrange..." }, suiteButton { "Suite" }, referenceButton { "Reference..." }, sessionButton { "New song..." }, recipesButton { "Recipes..." }, versionsButton { "Make versions" };
    uint32_t versionsSeq = 0;
    std::unique_ptr<juce::FileChooser> referenceChooser;
    double referenceBpm = 138.0;
    juce::String previewNote;
    juce::Rectangle<int> statusWide, statusNarrow;   // the status line, without / with Apply and Discard showing
    void chooseReference();
    arrangement::GenSettings lastArrange;
    void showArrange();
    bool wasPlaying = false;
    void propose();

    juce::Rectangle<int> songArea, markArea, selectArea, nowArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OuroborosEditor)
};
