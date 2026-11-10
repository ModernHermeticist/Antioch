#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class AntiochLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AntiochLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class AntiochEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AntiochEditor (AntiochProcessor&);
    ~AntiochEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct TapRow
    {
        juce::Label title;
        juce::ToggleButton on;
        juce::ComboBox division;
        juce::Label harmonicNote;
        Knob time, pitch, feedback, harmonic, level, pan;
        std::unique_ptr<ButtonAttachment> onAttachment;
        std::unique_ptr<ComboAttachment> divisionAttachment;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void updateTapLabels();

    AntiochProcessor& processor;
    AntiochLookAndFeel lnf;

    juce::ToggleButton syncButton { "Tempo Sync" };
    std::unique_ptr<ButtonAttachment> syncAttachment;
    juce::Label bpmLabel;

    juce::ComboBox rootBox;
    juce::Label rootLabel;
    std::unique_ptr<ComboAttachment> rootAttachment;

    Knob octave, resonance, resMix, damping, lowCut, mix, output;
    std::array<TapRow, Params::numTaps> rows;

    juce::Rectangle<int> globalArea, tapArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AntiochEditor)
};
