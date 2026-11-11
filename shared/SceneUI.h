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
            : engine (e), enabled (state, SceneEngine::enabledId, "Scenes on", Colours::ember), morph (state, SceneEngine::morphId, "Morph"),
              levelNeutral (state, SceneEngine::levelId, "Level-neutral", Colours::moss)
        {
            levelNeutral.setTooltip ("Scenes change the sound, not the volume: where a scene makes this plugin louder or quieter than in the drop, "
                                     "its output is brought back to the drop's level (up to 4 dB). Level changes you want come from Prima Materia");
            addAndMakeVisible (levelNeutral);
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

            print.setTooltip ("Writes the scene moves into this plugin's knobs as the song plays, so FL records them as automation "
                              "clips (turn on FL's automation recording first). Scenes switch off when you stop.");
            print.onClick = [this]
            {
                if (engine.getPrintState() == SceneEngine::PrintState::Idle) engine.armPrint();
                else engine.cancelPrint();
                timerCallback();
            };
            addAndMakeVisible (print);
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
            levelNeutral.setBounds (top.removeFromRight (140).withSizeKeepingCentre (132, 30).translated (0, 8));
            status.setBounds (top.translated (0, 8));
            r.removeFromTop (8);

            auto types = r.removeFromTop (32);
            const auto w = types.getWidth() / numSceneSlots;
            for (auto& b : typeButtons) b->setBounds (types.removeFromLeft (w).reduced (2));
            r.removeFromTop (10);

            {
                auto bottom = r.removeFromBottom (34);
                print.setBounds (bottom.removeFromRight (230).reduced (2, 2));
                hint.setBounds (bottom);
            }
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
            const auto ps = engine.getPrintState();
            print.setButtonText (ps == SceneEngine::PrintState::Armed ? "Armed: press play (click to cancel)"
                                 : ps == SceneEngine::PrintState::Printing ? "Printing... stop to finish"
                                 : engine.getPrintsDone() > 0 ? "Printed. Print again" : "Print to FL automation");
            print.setColour (juce::TextButton::buttonColourId, ps == SceneEngine::PrintState::Idle ? Colours::panel : Colours::ember.withAlpha (0.6f));
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
        Toggle levelNeutral;
        juce::TextButton suggested { "Suggested" }, clear { "Clear all" }, print { "Print to FL automation" };
        juce::Label status, hint;
        std::vector<std::unique_ptr<juce::TextButton>> typeButtons;
        std::vector<std::unique_ptr<Row>> rows;
        int selected = 0;
    };

    //==============================================================================
    /** "You turned X up in the breakdown: do it in every breakdown?" with Yes / No, along the
        bottom of the plugin window while a proposal waits. Add it with addChildComponent. */
    class TweakBanner : public juce::Component, private juce::Timer
    {
    public:
        explicit TweakBanner (SceneEngine& e) : engine (e)
        {
            text.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            text.setColour (juce::Label::textColourId, Colours::text);
            yes.onClick = [this] { engine.acceptProposal(); setVisible (false); };
            no.onClick = [this] { engine.dismissProposal(); setVisible (false); };
            for (auto* c : std::initializer_list<juce::Component*> { &text, &yes, &no }) addAndMakeVisible (c);
            startTimerHz (4);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::brass.withAlpha (0.92f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            g.setColour (Colours::background.withAlpha (0.85f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 5.0f);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 5);
            no.setBounds (r.removeFromRight (90));
            r.removeFromRight (6);
            yes.setBounds (r.removeFromRight (150));
            text.setBounds (r);
        }
    private:
        void timerCallback() override
        {
            const auto show = engine.getProposal().valid;
            if (show)
            {
                text.setText (engine.proposalText(), juce::dontSendNotification);
                if (auto* parent = getParentComponent())
                    setBounds (parent->getLocalBounds().removeFromBottom (48).reduced (16, 4));
                toFront (false);
            }
            if (show != isVisible()) setVisible (show);
        }
        SceneEngine& engine;
        juce::Label text;
        juce::TextButton yes { "Yes, make it a scene" }, no { "No" };
    };
}
