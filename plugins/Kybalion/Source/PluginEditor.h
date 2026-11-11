#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SuiteUI.h"
#include "SceneUI.h"

/** 16 bars you click or drag to draw step levels; the playing step lights up. */
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (KybalionProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();

private:
    void edit (const juce::MouseEvent&);
    juce::Rectangle<float> stepBounds (int i) const;

    KybalionProcessor& processor;
    std::array<juce::RangedAudioParameter*, Params::numSteps> params {};
    int playing = -1;
    bool erasing = false;
};

/** Plots the duck envelope over one cycle with a moving marker and a gain-reduction meter. */
class DuckView : public juce::Component
{
public:
    explicit DuckView (KybalionProcessor& p) : processor (p) {}
    void paint (juce::Graphics&) override;

private:
    KybalionProcessor& processor;
};

class KybalionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KybalionEditor (KybalionProcessor&);
    ~KybalionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    KybalionProcessor& processor;
    suite::ui::LookAndFeel lnf;

    suite::ui::Toggle gateOn, duckOn, followSections;
    juce::Label sectionStatus;
    suite::ui::Knob output;
    juce::Label bpmLabel;
    suite::ui::TabBar tabs;

    // Rhythm
    juce::Component gatePanel;
    StepGrid grid;
    juce::ComboBox patternBox;
    std::vector<std::pair<juce::String, std::array<float, Params::numSteps>>> userPatterns;
    std::unique_ptr<juce::AlertWindow> nameWindow;
    void fillPatterns();
    juce::TextButton randomButton { "Randomise" };
    suite::ui::Choice gateRate, fills, fillStyle, chordFollow;
    std::unique_ptr<suite::ui::Toggle> dynamicGate;
    suite::ui::Knob length, attack, release, depth, swing, stereo, filter, res;

    // Polarity
    juce::Component duckPanel;
    DuckView duckView;
    juce::ComboBox presetBox;
    suite::ui::Choice source, duckRate;
    suite::ui::Knob nudge, duckDepth, duckAttack, hold, duckRelease, curve, split, threshold;
    suite::ui::Toggle breakdown, shareKick;
    juce::Label sourceStatus;
    juce::TextButton fitButton { "Fit to kick" };

    std::unique_ptr<suite::ui::ScenePanel> scenePanel;
    juce::Rectangle<int> masterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KybalionEditor)
};
