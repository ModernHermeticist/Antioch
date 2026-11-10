#pragma once

// UI for section scenes: the section colours, and a panel to edit each section
// type's offsets. Header-only so plugins that don't use scenes don't pay for it.

#include "Scenes.h"
#include "SuiteUI.h"

namespace suite::ui
{
    inline juce::Colour sectionColour (SectionType t)
    {
        switch (t)
        {
            case SectionType::Intro:     return Colours::verdigris.darker (0.3f);
            case SectionType::Groove:    return Colours::moss;
            case SectionType::Breakdown: return Colours::amethyst;
            case SectionType::Build:     return Colours::brass;
            case SectionType::Drop:      return Colours::ember;
            case SectionType::Outro:     return Colours::dimText;
            case SectionType::count:     break;
        }
        return Colours::dimText;
    }

    /** Shows each scene parameter's in-use value as a ghost ring on its knob (anywhere under `root`),
        or hides the rings when no scene is active. Call from the editor's timer. */
    inline void updateSceneGhosts (juce::Component& root, const SceneEngine& engine)
    {
        const auto active = engine.getActiveType() >= 0;
        std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
        {
            if (auto* knob = dynamic_cast<Knob*> (&c))
                for (int i = 0; i < engine.getNumParams(); ++i)
                    if (knob->paramId == engine.getParamId (i))
                        knob->setGhost (active ? engine.getEffective (i) : -1.0f);
            for (auto* child : c.getChildren())
                visit (*child);
        };
        visit (root);
    }

    /** Pick a section type, then set how far each scene parameter moves in it. */
    class ScenePanel : public juce::Component, private juce::Timer
    {
    public:
        ScenePanel (SceneEngine& e, juce::AudioProcessorValueTreeState& state)
            : engine (e), enabled (state, SceneEngine::enabledId, "Scenes on", Colours::ember), morph (state, SceneEngine::morphId, "Morph")
        {
            for (int t = 0; t < numSceneSlots; ++t)
            {
                const auto later = t == laterDropSlot;
                const auto colour = later ? sectionColour (SectionType::Drop).brighter (0.35f) : sectionColour ((SectionType) t);
                auto b = std::make_unique<juce::TextButton> (later ? juce::String ("Drop 2+") : juce::String (sectionName ((SectionType) t)));
                b->setClickingTogglesState (false);
                b->setColour (juce::TextButton::buttonColourId, colour.withAlpha (0.25f));
                b->setColour (juce::TextButton::buttonOnColourId, colour);
                if (later) b->setTooltip ("Added on top of the Drop scene for the second and later drops");
                b->onClick = [this, t] { selectType (t); };
                addAndMakeVisible (*b);
                typeButtons.push_back (std::move (b));
            }

            for (int i = 0; i < engine.getNumParams(); ++i)
            {
                auto row = std::make_unique<Row>();
                row->label.setText (engine.getLabel (i), juce::dontSendNotification);
                row->label.setFont (juce::FontOptions (13.0f));
                row->label.setJustificationType (juce::Justification::centredRight);
                row->slider.setSliderStyle (juce::Slider::LinearBar);
                row->slider.setRange (-100.0, 100.0, 1.0);
                row->slider.setTextValueSuffix ("%");
                row->slider.setDoubleClickReturnValue (true, 0.0);
                row->slider.setColour (juce::Slider::trackColourId, Colours::ember.withAlpha (0.5f));
                row->slider.setColour (juce::Slider::backgroundColourId, Colours::background);
                row->slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
                row->slider.onValueChange = [this, i, s = &row->slider] { engine.setOffset (selected, i, (float) s->getValue() / 100.0f); };
                addAndMakeVisible (row->label);
                addAndMakeVisible (row->slider);
                rows.push_back (std::move (row));
            }

            suggested.onClick = [this] { engine.applySuggested(); selectType (selected); };
            clear.onClick = [this] { engine.clearAll(); selectType (selected); };
            for (auto* c : std::initializer_list<juce::Component*> { &enabled, &morph, &suggested, &clear, &status, &hint })
                addAndMakeVisible (c);
            status.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            hint.setFont (juce::FontOptions (12.0f));
            hint.setJustificationType (juce::Justification::topLeft);
            hint.setText ("Offsets are added to your knob settings while Ouroboros says the song is in that section "
                          "(double-click a bar to reset it). Knobs show a ghost ring with the value in use.",
                          juce::dontSendNotification);

            selectType ((int) SectionType::Breakdown);
            startTimerHz (10);
        }

        void selectType (int t)
        {
            selected = juce::jlimit (0, numSceneSlots - 1, t);
            for (int i = 0; i < numSceneSlots; ++i)
                typeButtons[(size_t) i]->setToggleState (i == selected, juce::dontSendNotification);
            for (int i = 0; i < (int) rows.size(); ++i)
                rows[(size_t) i]->slider.setValue (engine.getOffset (selected, i) * 100.0, juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override
        {
            drawPanel (g, getLocalBounds(), "Section scenes", Colours::ember);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12, 8).withTrimmedTop (14);
            auto top = r.removeFromTop (50);
            clear.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 30).translated (0, 8));
            suggested.setBounds (top.removeFromRight (110).withSizeKeepingCentre (104, 30).translated (0, 8));
            morph.setBounds (top.removeFromRight (150));
            enabled.setBounds (top.removeFromRight (130).withSizeKeepingCentre (120, 30).translated (0, 8));
            status.setBounds (top.translated (0, 8));
            r.removeFromTop (8);

            auto types = r.removeFromTop (32);
            const auto w = types.getWidth() / numSceneSlots;
            for (auto& b : typeButtons) b->setBounds (types.removeFromLeft (w).reduced (2));
            r.removeFromTop (10);

            hint.setBounds (r.removeFromBottom (34));
            const auto rowH = juce::jmin (30, r.getHeight() / juce::jmax (1, (int) rows.size()));
            for (auto& row : rows)
            {
                auto line = r.removeFromTop (rowH);
                row->label.setBounds (line.removeFromLeft (170).reduced (6, 2));
                row->slider.setBounds (line.reduced (4, 3));
            }
        }

    private:
        void timerCallback() override
        {
            const auto active = engine.getActiveType();
            status.setColour (juce::Label::textColourId, active >= 0 ? sectionColour ((SectionType) active) : Colours::dimText);
            status.setText (active >= 0 ? juce::String ("Now: ") + sectionName ((SectionType) active) + (engine.isInLaterDrop() ? " 2+" : "")
                                        : engine.isOuroborosPresent() ? juce::String ("Ouroboros found: waiting for playback")
                                                                      : juce::String ("No Ouroboros: scenes inactive"),
                            juce::dontSendNotification);
        }

        struct Row { juce::Label label; juce::Slider slider; };

        SceneEngine& engine;
        Toggle enabled;
        Choice morph;
        juce::TextButton suggested { "Suggested" }, clear { "Clear all" };
        juce::Label status, hint;
        std::vector<std::unique_ptr<juce::TextButton>> typeButtons;
        std::vector<std::unique_ptr<Row>> rows;
        int selected = 0;
    };
}
