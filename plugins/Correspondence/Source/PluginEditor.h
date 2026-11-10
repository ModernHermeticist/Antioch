#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"

/** A tile you drag straight into FL Studio's playlist or piano roll as a .mid clip. */
class DragTile : public juce::Component
{
public:
    DragTile (CorrespondenceProcessor&, juce::String kind, juce::Colour);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    CorrespondenceProcessor& processor;
    juce::String kind;
    juce::Colour colour;
    bool dragging = false, unavailable = false;
};

/** Mini piano roll of the voiced progression, bass and current preview position. */
class RollView : public juce::Component
{
public:
    void setData (std::vector<std::vector<int>> chordsIn, std::vector<int> bassIn, int countIn, int highlightIn,
                  std::vector<chords::Note> melodyIn = {}, double quartersPerChordIn = 4.0);
    void paint (juce::Graphics&) override;
    std::function<void (int)> onSlotClicked;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<std::vector<int>> voiced;
    std::vector<int> bass;
    std::vector<chords::Note> melodyNotes;
    double quartersPerChord = 4.0;
    int count = 4, highlight = -1;
};

class CorrespondenceEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit CorrespondenceEditor (CorrespondenceProcessor&);
    ~CorrespondenceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Drop a MIDI clip anywhere on the plugin to teach Melodia your motif.
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    bool dropHover = false;
    struct SlotView : public juce::Component
    {
        SlotView (juce::AudioProcessorValueTreeState&, int index);
        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        juce::String roman, name;
        bool active = true, lit = false;
        juce::ComboBox degree, quality;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> degreeAttachment, qualityAttachment;
        std::function<void()> onClick;
    };

    void timerCallback() override;
    void auditionSlot (int slot, double seconds);
    void togglePlay();

    CorrespondenceProcessor& processor;
    suite::ui::LookAndFeel lnf;

    suite::ui::Choice root, scale, perChord;
    suite::ui::Knob count;
    suite::ui::Toggle broadcast, acceptDetected;
    juce::Label broadcastStatus, presetCaption;
    juce::ComboBox presetBox;

    std::array<std::unique_ptr<SlotView>, chords::maxSlots> slots;
    RollView roll;

    suite::ui::Choice voicing, chordPattern, bassPattern, arpPattern;
    suite::ui::Toggle voiceLead, addBass;
    suite::ui::Knob registerOct, bassOctave, arpOctaves, previewVolume, humanise;

    DragTile chordTile, bassTile, arpTile, songChordTile, songBassTile, songArpTile, melodyTile, songLeadTile;
    suite::ui::Choice melodyStyle;
    juce::TextButton newMelodyButton { "New melody" };
    std::vector<chords::Note> melodyNotes;
    size_t nextMelodyNote = 0;
    juce::TextButton playButton { "Play" }, saveButton { "Export all..." }, songButton { "Song patterns..." };
    void showSongPatterns();
    std::unique_ptr<juce::FileChooser> chooser;

    chords::Settings settings;
    std::vector<std::vector<int>> voiced;
    int playingSlot = -1;
    double playStart = 0.0, auditionStopAt = 0.0;
    bool playing = false;

    juce::Rectangle<int> keyArea, progressionArea, voicingArea, patternArea, exportArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CorrespondenceEditor)
};
