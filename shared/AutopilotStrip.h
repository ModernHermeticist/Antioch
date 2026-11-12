#pragma once

// A thin strip along the bottom of a plugin's window: what the autopilot is doing on this
// track right now ("making room at 2 kHz for a louder part"), so its moves are never a mystery.

#include <juce_gui_basics/juce_gui_basics.h>
#include "SuiteUI.h"
#include <functional>

namespace suite::ui
{
    class AutopilotStrip : public juce::Component, private juce::Timer
    {
    public:
        static constexpr int height = 26;
        /** Returns what's happening now, a few words per move, joined with ";  " (empty: nothing). */
        std::function<juce::StringArray()> moves;

        AutopilotStrip() { setInterceptsMouseClicks (false, false); startTimerHz (4); }

        juce::String shownText() const { return shown; }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Colours::panel);
            g.fillRoundedRectangle (r, 5.0f);
            const auto busy = shown.isNotEmpty();
            g.setColour (busy ? Colours::verdigris : Colours::outline);
            g.fillEllipse (r.getX() + 9.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.setColour (Colours::dimText);
            g.drawText ("AUTOPILOT", juce::Rectangle<float> (r.getX() + 24.0f, r.getY(), 80.0f, r.getHeight()), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (12.5f));
            g.setColour (busy ? Colours::text : Colours::dimText);
            g.drawText (busy ? shown : juce::String ("nothing to do on this track right now"),
                        juce::Rectangle<float> (r.getX() + 104.0f, r.getY(), r.getWidth() - 112.0f, r.getHeight()), juce::Justification::centredLeft, true);
        }

    private:
        void timerCallback() override
        {
            const auto now = moves ? moves().joinIntoString (";  ") : juce::String();
            if (now != shown) { shown = now; repaint(); }
        }
        juce::String shown;
    };
}
