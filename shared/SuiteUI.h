#pragma once

// Shared look-and-feel and widgets so every plugin in the suite looks related.

#include <juce_audio_processors/juce_audio_processors.h>

namespace suite::ui
{
    namespace Colours
    {
        const juce::Colour background { 0xff15161c };
        const juce::Colour panel      { 0xff1f2129 };
        const juce::Colour outline    { 0xff2e313c };
        const juce::Colour text       { 0xffd9dbe3 };
        const juce::Colour dimText    { 0xff8a8e9c };
        const juce::Colour brass      { 0xffe0b25a };
        const juce::Colour verdigris  { 0xff6fc3c9 };
        const juce::Colour amethyst   { 0xffc77dd8 };
        const juce::Colour moss       { 0xff8fd17a };
        const juce::Colour ember      { 0xffe8735a };
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
    };

    //==============================================================================
    /** Rotary knob with a caption above and a value read-out below, bound to a parameter. */
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption,
              juce::Colour colour = Colours::brass);

        void resized() override;

        /** Shows a second, thin ring at `normalised` (0..1): the value actually in use when
            something (e.g. a section scene) offsets the knob. Pass a negative value to hide it. */
        void setGhost (float normalised);

        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label caption;
        int maxHeight = 130;
        const juce::String paramId;

    private:
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    };

    /** Toggle button bound to a bool parameter. */
    class Toggle : public juce::ToggleButton
    {
    public:
        Toggle (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text,
                juce::Colour colour = Colours::brass);

    private:
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    };

    /** Combo box with a caption above, bound to a choice parameter. */
    class Choice : public juce::Component
    {
    public:
        Choice (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
        void resized() override;

        juce::ComboBox box;
        juce::Label caption;

    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    /** Draws a rounded panel with an optional title in the top-left corner. */
    void drawPanel (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title = {},
                    juce::Colour titleColour = Colours::dimText);

    /** Draws the plugin name + tagline header. */
    void drawHeader (juce::Graphics&, juce::Rectangle<int> area, const juce::String& name,
                     const juce::String& tagline, juce::Colour colour);

    /** Lays components out left-to-right in equal-width cells. */
    void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items);

    /** A row of tab buttons; calls onChange with the selected index. */
    class TabBar : public juce::Component
    {
    public:
        TabBar (juce::StringArray names, std::vector<juce::Colour> colours);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        int getSelected() const noexcept { return selected; }
        void setSelected (int index);

        std::function<void (int)> onChange;

    private:
        juce::StringArray names;
        std::vector<juce::Colour> colours;
        int selected = 0;
    };
}
