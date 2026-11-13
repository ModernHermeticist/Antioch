#pragma once

// Pre-export checklist: a pass/warn/fail list from what Mentalism has measured.
// Pure logic, so it's directly testable.

#include "PluginProcessor.h"
#include "Registry.h"
#include "DjPhrasing.h"

namespace checklist
{
    enum class State { Pass, Warn, Fail, Unknown };
    struct Item { juce::String name, detail; State state = State::Unknown; suite::Fix fix = suite::Fix::None; bool optional = false; };

    /** Intro and outro for DJs: beat-led, with no pads, leads or vocals (from the Prima Materia
        tracks in the suite registry; masks say which section types each track sounds in). */
    inline Item djCheck (const std::vector<suite::SuiteEntry>& entries, bool hasIntroOrOutro)
    {
        Item item { "DJ-friendly intro/outro", {}, State::Unknown, suite::Fix::None, true };
        if (! hasIntroOrOutro) { item.detail = "No intro or outro in the map"; return item; }
        bool anyTracks = false, kickInIntro = false;
        juce::StringArray melodic;
        const auto introBit = 1u << (int) suite::SectionType::Intro, outroBit = 1u << (int) suite::SectionType::Outro;
        for (const auto& e : entries)
        {
            if (e.plugin != "Prima Materia" || ! e.has (suite::Status::roleSet)) continue;
            anyTracks = true;
            const auto role = (int) e.metrics[0];
            const auto mask = (uint32_t) e.metrics[3];
            if (role == 1 && (mask & introBit) != 0) kickInIntro = true;
            if ((role == 4 || role == 5 || role == 6) && (mask & (introBit | outroBit)) != 0)
                melodic.add (e.track.isNotEmpty() ? e.track : juce::String ("a ") + (role == 4 ? "pad" : role == 5 ? "lead" : "vocal") + " track");
        }
        if (! anyTracks) { item.detail = "Put Prima Materia (with roles) on your tracks to check"; return item; }
        if (! melodic.isEmpty())
        {
            item.state = State::Warn;
            item.detail = "Melodic parts in the intro/outro: " + melodic.joinIntoString (", ");
            item.fix = suite::Fix::DjIntroOutro;
        }
        else if (! kickInIntro)
        {
            item.state = State::Warn;
            item.detail = "No kick in the intro: DJs mix in on the beat";
        }
        else
        {
            item.state = State::Pass;
            item.detail = "Beat-led intro and outro";
        }
        return item;
    }

    inline std::vector<Item> check (const MentalismProcessor::Report& rep, int clippedSamples,
                                    const std::vector<suite::SuiteEntry>& suiteEntries = {})
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
        else if (rep.premaster)
            add ("Loudness", State::Pass, juce::String (m.integratedLufs, 1) + " LUFS integrated: a premaster's loudness is reported, never chased (mastering sets it)");
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
            add (rep.premaster ? "Headroom (premaster)" : "True peak", m.truePeakDb <= rep.ceiling + 0.05f ? State::Pass : State::Fail,
                 juce::String (m.truePeakDb, 1) + " dBTP; " + (rep.premaster ? "a label-ready premaster peaks at or under " : "ceiling ") + juce::String (rep.ceiling, 1)
                     + (rep.premaster ? " dBFS" + (m.truePeakDb > rep.ceiling + 0.05f ? juce::String (": bring the master down ") + juce::String (m.truePeakDb - rep.ceiling, 1) + " dB" : juce::String())
                                      : juce::String (" dBTP")));
        if (rep.premaster)
        {
            // nothing on the master: Lapis is a reference (its master preview must be off for the render)
            bool preview = false;
            for (const auto& e : suiteEntries) preview = preview || (e.plugin == "Lapis" && e.has (suite::Status::masterPreview));
            add ("No master limiting", preview ? State::Fail : State::Pass,
                 preview ? juce::String ("Lapis' Hear the master is on: switch it off before rendering (the premaster has no limiting)")
                         : juce::String ("Nothing limits or clips the master (Lapis is a reference)"));
            if (m.integratedLufs > -70.0f && m.truePeakDb > -99.0f)
            {
                const auto crest = m.truePeakDb - m.integratedLufs;
                add ("Dynamics intact", crest >= suite::premaster::minCrestDb ? State::Pass : State::Warn,
                     juce::String (crest, 1) + " dB from the loudness to the peaks" + (crest < suite::premaster::minCrestDb ? ": something is squashing the mix" : ""));
            }
            add ("Render format", State::Pass, "24-bit (or 32-bit float) WAV, no dither, at the project's sample rate");
            int squashed = 0, clipping = 0;
            for (const auto& e : suiteEntries) { squashed += e.has (suite::Status::squashedInput) ? 1 : 0; clipping += e.has (suite::Status::clipping) ? 1 : 0; }
            add ("Nothing squashed before the master", squashed == 0 ? State::Pass : State::Warn,
                 squashed == 0 ? juce::String ("no track arrives flattened") : juce::String (squashed) + " track(s) arrive squashed (a limiter or heavy compression on them): back it off");
            add ("No clipping inside tracks", clipping == 0 ? State::Pass : State::Fail,
                 clipping == 0 ? juce::String ("no track goes over full scale") : juce::String (clipping) + " track(s) clip inside: gain staging brings them down");
            if (m.integratedLufs > -70.0f)
                add ("Loudness window", m.integratedLufs <= -12.0f ? State::Pass : State::Warn,
                     juce::String (m.integratedLufs, 1) + " LUFS" + (m.integratedLufs > -12.0f ? ": loud for a premaster (is something squashing it?)" : ": a premaster's range"));
            if (rep.readiness.total > 0)
                add ("Premaster readiness", rep.readiness.total >= 85 ? State::Pass : rep.readiness.total >= 70 ? State::Warn : State::Fail,
                     juce::String (rep.readiness.total) + "/100; holding it back most: " + rep.readiness.weakest());
        }
        add ("Clipping", clippedSamples == 0 ? State::Pass : State::Fail,
             clippedSamples == 0 ? juce::String ("No samples at full scale")
                                 : juce::String (clippedSamples) + " samples hit 0 dBFS: lower the master" + (rep.premaster ? " (a premaster never clips)" : " or use Lapis"));

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
            bool introOrOutro = false;
            for (int i = 0; i < rep.map.count; ++i)
                introOrOutro = introOrOutro || rep.map.sections[(size_t) i].type == suite::SectionType::Intro
                                            || rep.map.sections[(size_t) i].type == suite::SectionType::Outro;
            out.push_back (djCheck (suiteEntries, introOrOutro));
            {
                const auto issues = suite::dj::phrasingIssues (rep.map);
                Item phr { "DJ phrasing", issues.isEmpty() ? juce::String ("Intro and outro in whole 16-bar phrases; sections on the 8-bar grid")
                                                          : "Off for DJs: " + issues.joinIntoString ("; "),
                           issues.isEmpty() ? State::Pass : State::Warn, issues.isEmpty() ? suite::Fix::None : suite::Fix::DjPhrasing, true };
                out.push_back (phr);
            }
            add ("Energy arc", ! rep.arrangement.complete ? State::Unknown : rep.arrangement.problems == 0 ? State::Pass : State::Warn,
                 ! rep.arrangement.complete ? juce::String ("Play every section once")
                 : rep.arrangement.problems == 0 ? juce::String ("The coach is happy")
                                                 : juce::String (rep.arrangement.problems) + " arrangement note(s): see the Arrangement tab");
        }
        return out;
    }

    inline bool readyToExport (const std::vector<Item>& items)
    {
        for (const auto& i : items) if (i.state == State::Fail || (i.state == State::Unknown && ! i.optional)) return false;
        return true;
    }
}
