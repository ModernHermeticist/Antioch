#pragma once

// Pre-export checklist: a pass/warn/fail list from what Mentalism has measured.
// Pure logic, so it's directly testable.

#include "PluginProcessor.h"

namespace checklist
{
    enum class State { Pass, Warn, Fail, Unknown };
    struct Item { juce::String name, detail; State state = State::Unknown; };

    inline std::vector<Item> check (const MentalismProcessor::Report& rep, int clippedSamples)
    {
        std::vector<Item> out;
        const auto& m = rep.measurement;
        auto add = [&out] (const juce::String& name, State s, const juce::String& detail) { out.push_back ({ name, detail, s }); };
        const auto enough = rep.measuredSeconds >= 30.0f;

        add ("Measured the song", enough ? State::Pass : State::Unknown,
             enough ? juce::String ((int) rep.measuredSeconds) + " s of audio" : "Play the whole song from the start (Reset first)");

        // Loudness against the target.
        if (m.integratedLufs <= -70.0f)
            add ("Loudness", State::Unknown, "Nothing measured yet");
        else
        {
            const auto diff = m.integratedLufs - rep.targetLufs;
            add ("Loudness", std::abs (diff) <= 1.0f ? State::Pass : std::abs (diff) <= 2.0f ? State::Warn : State::Fail,
                 juce::String (m.integratedLufs, 1) + " LUFS integrated; target " + juce::String (rep.targetLufs, 1)
                     + " (" + (diff >= 0.0f ? "+" : "") + juce::String (diff, 1) + " LU)");
        }

        // Peaks.
        if (m.truePeakDb <= -99.0f)
            add ("True peak", State::Unknown, "Nothing measured yet");
        else
            add ("True peak", m.truePeakDb <= rep.ceiling + 0.05f ? State::Pass : State::Fail,
                 juce::String (m.truePeakDb, 1) + " dBTP; ceiling " + juce::String (rep.ceiling, 1) + " dBTP");
        add ("Clipping", clippedSamples == 0 ? State::Pass : State::Fail,
             clippedSamples == 0 ? juce::String ("No samples at full scale")
                                 : juce::String (clippedSamples) + " samples hit 0 dBFS: lower the master or use Lapis");

        // Mono and phase.
        if (rep.measuredSeconds > 2.0f)
        {
            add ("Low end in mono", m.lowCorrelation >= 0.9f ? State::Pass : m.lowCorrelation >= 0.7f ? State::Warn : State::Fail,
                 "Correlation below 120 Hz: " + juce::String (m.lowCorrelation, 2) + (m.lowCorrelation < 0.9f ? " (narrow the bass and kick)" : ""));
            add ("Mono compatibility", m.monoLossDb <= 3.0f && m.correlation >= 0.0f ? State::Pass : m.correlation < -0.2f ? State::Fail : State::Warn,
                 "Mono sum " + juce::String (std::max (0.0f, m.monoLossDb), 1) + " dB down; correlation " + juce::String (m.correlation, 2));
        }
        else
            add ("Mono compatibility", State::Unknown, "Nothing measured yet");

        // Sections (with Ouroboros).
        if (rep.arrangementAvailable && rep.map.count > 0)
        {
            juce::StringArray silent;
            for (int i = 0; i < rep.map.count && i < (int) rep.sections.size(); ++i)
            {
                const auto& st = rep.sections[(size_t) i];
                const auto type = rep.map.sections[(size_t) i].type;
                if (st.heard() && st.lufs() < -45.0f && type != suite::SectionType::Intro && type != suite::SectionType::Outro)
                    silent.add (juce::String (suite::sectionName (type)) + " (" + coach::barsOf (rep.map, i) + ")");
            }
            add ("No silent sections", silent.isEmpty() ? (rep.arrangement.complete ? State::Pass : State::Unknown) : State::Fail,
                 silent.isEmpty() ? (rep.arrangement.complete ? juce::String ("Every section has sound") : juce::String ("Not every section heard yet"))
                                  : "Nearly silent: " + silent.joinIntoString (", "));
            add ("Energy arc", ! rep.arrangement.complete ? State::Unknown : rep.arrangement.problems == 0 ? State::Pass : State::Warn,
                 ! rep.arrangement.complete ? juce::String ("Play every section once")
                 : rep.arrangement.problems == 0 ? juce::String ("The coach is happy")
                                                 : juce::String (rep.arrangement.problems) + " arrangement note(s): see the Arrangement tab");
        }
        return out;
    }

    inline bool readyToExport (const std::vector<Item>& items)
    {
        for (const auto& i : items) if (i.state == State::Fail || i.state == State::Unknown) return false;
        return true;
    }
}
