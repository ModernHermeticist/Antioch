#pragma once

// Shared look-and-feel and widgets so every plugin in the suite looks and works alike:
// one dark palette with a per-plugin accent, one type scale, buttons, pill toggles, segmented tabs,
// panels; a header bar with the plugin's name, a status pill, a search box for every action
// (Ctrl/Cmd+K) and an Advanced switch (each plugin opens on its simple view; the manual controls
// live under Advanced).

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

namespace suite::ui
{
    namespace Colours
    {
        const juce::Colour background { 0xff121319 };
        const juce::Colour panel      { 0xff1b1d25 };
        const juce::Colour raised     { 0xff23262f };   // buttons, fields, the selected tab's track
        const juce::Colour outline    { 0xff2c2f3a };
        const juce::Colour text       { 0xffe6e8ee };
        const juce::Colour dimText    { 0xff8f94a3 };
        const juce::Colour faintText  { 0xff5f6473 };
        const juce::Colour brass      { 0xffe0b25a };
        const juce::Colour verdigris  { 0xff6fc3c9 };
        const juce::Colour amethyst   { 0xffc77dd8 };
        const juce::Colour moss       { 0xff8fd17a };
        const juce::Colour ember      { 0xffe8735a };
    }

    /** The type scale: everything takes its size from here. */
    namespace Type
    {
        constexpr float title = 24.0f, tagline = 13.0f, section = 11.5f, label = 12.0f, body = 13.0f, value = 13.0f, button = 13.0f;
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                const juce::Drawable* icon, const juce::Colour* textColour) override;
        juce::Font getPopupMenuFont() override;
        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
        int getPopupMenuBorderSize() override { return 6; }
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                            int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
        void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    };

    /** A button drawn filled with its accent (the one main action in an area). */
    inline void makePrimary (juce::TextButton& b, juce::Colour accent)
    {
        b.getProperties().set ("primary", true);
        b.setColour (juce::TextButton::buttonColourId, accent);
    }
    /** Shows or hides several components at once. */
    inline void setShown (std::initializer_list<juce::Component*> items, bool shown) { for (auto* c : items) if (c != nullptr) c->setVisible (shown); }

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

    /** Toggle button bound to a bool parameter (drawn as a pill with an on/off dot). */
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

    /** Draws a rounded panel with an optional title (and a short accent bar) in the top-left corner. */
    void drawPanel (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title = {},
                    juce::Colour titleColour = Colours::dimText);

    /** Draws the plugin name + tagline (plugins with a HeaderBar don't need it). */
    void drawHeader (juce::Graphics&, juce::Rectangle<int> area, const juce::String& name,
                     const juce::String& tagline, juce::Colour colour);

    /** Lays components out left-to-right in equal-width cells. */
    void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items);

    /** A segmented control: one rounded track, the selected segment filled with its colour. */
    class TabBar : public juce::Component
    {
    public:
        TabBar (juce::StringArray names, std::vector<juce::Colour> colours);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        int getSelected() const noexcept { return selected; }
        void setSelected (int index);

        std::function<void (int)> onChange;

    private:
        juce::StringArray names;
        std::vector<juce::Colour> colours;
        int selected = 0, hovered = -1;
    };

    //==============================================================================
    /** One thing the plugin can do, for the search box. */
    struct Action
    {
        juce::String name, detail, group;
        std::function<void()> run;
        bool enabled = true;
    };

    /** The search box: type to filter every action, arrows and Enter to run one. */
    class CommandPalette : public juce::Component, private juce::TextEditor::Listener, private juce::ListBoxModel, private juce::KeyListener
    {
    public:
        CommandPalette (std::vector<Action> actions, juce::Colour accent);
        void resized() override;
        void paint (juce::Graphics&) override;
        /** Opens it under `anchor` (a call-out box). */
        static void show (std::vector<Action> actions, juce::Component& anchor, juce::Colour accent);
        std::vector<int> filtered;   // indexes into actions, for tests
        void setQuery (const juce::String& q) { field.setText (q, juce::sendNotification); }
        bool runSelected();
    private:
        std::vector<Action> all;
        juce::Colour accent;
        juce::TextEditor field;
        juce::ListBox list;
        void textEditorTextChanged (juce::TextEditor&) override;
        void textEditorReturnKeyPressed (juce::TextEditor&) override { runSelected(); }
        void textEditorEscapeKeyPressed (juce::TextEditor&) override { close(); }
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        using juce::Component::keyPressed;
        int getNumRows() override { return (int) filtered.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { list.selectRow (row); runSelected(); }
        void refilter();
        void close();
    };

    /** The header every plugin shares: its name and tagline, a status pill, any buttons of its
        own, the search box (Ctrl/Cmd+K) and the Advanced switch (remembered with the project). */
    class HeaderBar : public juce::Component, private juce::KeyListener
    {
    public:
        HeaderBar (juce::String name, juce::String tagline, juce::Colour accent);
        ~HeaderBar() override;
        static constexpr int height = 52;

        /** Every action for the search box (asked for each time it opens). */
        std::function<std::vector<Action>()> actions;
        /** The Advanced switch: off = the simple view. `state` keeps it with the project. */
        void enableAdvanced (juce::ValueTree state, std::function<void (bool)> changed);
        bool advanced() const { return advancedOn; }
        void setAdvanced (bool on);
        /** A short status (e.g. "Autopilot: 3 fixes on"), coloured dot first. */
        void setStatus (const juce::String& text, juce::Colour dot = Colours::moss);
        /** Buttons or boxes of the plugin's own, right-aligned before the search box. */
        void addRight (juce::Component& c, int width);
        void setTagline (const juce::String& t) { tagline = t; repaint(); }
        void openSearch();

        void paint (juce::Graphics&) override;
        void resized() override;
        void parentHierarchyChanged() override;

        juce::TextButton searchButton, advancedButton;
    private:
        juce::String name, tagline, status;
        juce::Colour accent, statusDot;
        juce::ValueTree advState;
        std::function<void (bool)> onAdvanced;
        bool advancedOn = false, hasAdvanced = false;
        std::vector<std::pair<juce::Component*, int>> rights;
        juce::Component* keyTarget = nullptr;
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        using juce::Component::keyPressed;
    };

    /** Actions from a popup menu, recursively (each item becomes a search entry that picks it). */
    void addMenuActions (std::vector<Action>& into, const juce::PopupMenu& menu, const juce::String& group, std::function<void (int)> pick);
}
