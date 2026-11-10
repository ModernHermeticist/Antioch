#pragma once

// Arrangement coach: judges the song's energy arc from loudness measured per section
// (sections come from the Ouroboros map). Pure logic, so it's directly testable.

#include "SectionBus.h"
#include "Analysis.h"
#include "FixBus.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace coach
{
    /** Loudness gathered for one section of the map (power = mean square, K-weighted). */
    struct SectionStat
    {
        double power = 0.0, firstHalf = 0.0, secondHalf = 0.0;
        int blocks = 0, firstBlocks = 0, secondBlocks = 0;

        float lufs() const { return blocks > 0 ? analysis::powerToLufs (power / blocks) : -100.0f; }
        float firstLufs() const { return firstBlocks > 0 ? analysis::powerToLufs (firstHalf / firstBlocks) : -100.0f; }
        float secondLufs() const { return secondBlocks > 0 ? analysis::powerToLufs (secondHalf / secondBlocks) : -100.0f; }
        bool heard() const { return blocks >= 10; }   // at least ~1 s of material
    };

    constexpr float minContrast = 2.0f, maxContrast = 12.0f, minRise = 1.0f, dropTolerance = 0.7f, introHeadroom = 1.5f;

    struct Result
    {
        juce::StringArray notes;       // problems first, then confirmations
        std::vector<suite::Fix> fixes; // per note: what the fix-it button would do (None for confirmations)
        int problems = 0;
        bool complete = true;          // every section heard
    };

    inline juce::String barsOf (const suite::SectionMap& m, int i)
    {
        return "bars " + juce::String (m.sections[(size_t) i].startBar) + "-" + juce::String (m.endBar (i) - 1);
    }

    inline Result analyse (const suite::SectionMap& map, const std::vector<SectionStat>& stats)
    {
        using T = suite::SectionType;
        Result r;
        juce::StringArray good;
        auto fmt = [] (float v) { return juce::String (std::abs (v), 1); };
        auto heard = [&] (int i) { return i >= 0 && i < (int) stats.size() && stats[(size_t) i].heard(); };

        std::vector<int> drops;
        for (int i = 0; i < map.count; ++i)
        {
            if (! heard (i)) r.complete = false;
            if (map.sections[(size_t) i].type == T::Drop && heard (i)) drops.push_back (i);
        }

        for (int i = 0; i < map.count; ++i)
        {
            if (! heard (i)) continue;
            const auto type = map.sections[(size_t) i].type;
            const auto& st = stats[(size_t) i];

            if (type == T::Breakdown)
            {
                // Compare with the drop that follows (or the last drop before it).
                int drop = -1;
                for (auto d : drops) if (d > i) { drop = d; break; }
                if (drop < 0 && ! drops.empty()) drop = drops.back();
                if (drop < 0) continue;
                const auto contrast = stats[(size_t) drop].lufs() - st.lufs();
                if (contrast < minContrast)
                {
                    r.notes.add ("Breakdown (" + barsOf (map, i) + ") is only " + fmt (contrast) + " LU below the drop: the drop won't feel "
                                 "like a lift. Thin the breakdown out or take more of its low end away.");
                    r.fixes.push_back (suite::Fix::BreakdownQuieter);
                    ++r.problems;
                }
                else if (contrast > maxContrast)
                {
                    r.notes.add ("Breakdown (" + barsOf (map, i) + ") is " + fmt (contrast) + " LU below the drop: people turn it up and then "
                                 "get blasted. Bring the breakdown up a little.");
                    r.fixes.push_back (suite::Fix::BreakdownLouder);
                    ++r.problems;
                }
                else good.add ("Breakdown to drop contrast " + fmt (contrast) + " LU: good.");
            }
            else if (type == T::Build && st.firstBlocks >= 5 && st.secondBlocks >= 5)
            {
                const auto rise = st.secondLufs() - st.firstLufs();
                if (rise < minRise)
                {
                    r.notes.add ("Build (" + barsOf (map, i) + ") only rises " + juce::String (rise, 1) + " LU: let it grow with risers, "
                                 "more top end or Cause & Effect's wash.");
                    r.fixes.push_back (suite::Fix::BuildRise);
                    ++r.problems;
                }
                else good.add ("Build (" + barsOf (map, i) + ") rises " + fmt (rise) + " LU: good.");
            }
            else if (type == T::Intro && ! drops.empty())
            {
                const auto headroom = stats[(size_t) drops.front()].lufs() - st.lufs();
                if (headroom < introHeadroom)
                {
                    r.notes.add ("Intro is only " + fmt (headroom) + " LU below the drop: leave headroom so the drop lands.");
                    r.fixes.push_back (suite::Fix::IntroQuieter);
                    ++r.problems;
                }
            }
        }

        for (size_t k = 1; k < drops.size(); ++k)
        {
            const auto diff = stats[(size_t) drops[k]].lufs() - stats[(size_t) drops[k - 1]].lufs();
            if (diff < -dropTolerance)
            {
                r.notes.add ("Drop " + juce::String ((int) k + 1) + " (" + barsOf (map, drops[k]) + ") is " + fmt (diff) + " LU quieter than drop "
                             + juce::String ((int) k) + ": a later drop should hit at least as hard.");
                r.fixes.push_back (suite::Fix::LaterDropBigger);
                ++r.problems;
            }
            else good.add ("Drop " + juce::String ((int) k + 1) + " holds up against drop " + juce::String ((int) k) + ".");
        }

        r.notes.addArray (good);
        if (! r.complete)
            r.notes.add ("Play the whole song through once to check every section.");
        r.fixes.resize ((size_t) r.notes.size(), suite::Fix::None);
        return r;
    }

    /** What a fix-it button does, for its tooltip and the confirmation line. */
    inline juce::String describe (suite::Fix fix)
    {
        switch (fix)
        {
            case suite::Fix::BreakdownQuieter: return "Prima Materia: breakdowns 2 dB quieter";
            case suite::Fix::BreakdownLouder:  return "Prima Materia: breakdowns 2 dB louder";
            case suite::Fix::BuildRise:        return "Cause & Effect: more riser, noise and wash; Azoth: more swell and roll";
            case suite::Fix::IntroQuieter:     return "Prima Materia: intro 2 dB quieter";
            case suite::Fix::LaterDropBigger:  return "Prima Materia: Drop 2+ 1.5 dB up; Azoth: bigger later drops";
            case suite::Fix::RoughMix:         return "Prima Materia: each track's trim set for its role, relative to the kick";
            case suite::Fix::DjIntroOutro:     return "Prima Materia: pads, leads and vocals out of the intro and outro";
            case suite::Fix::ReferenceTone:    return "Lapis: low and high tone moved towards the reference";
            case suite::Fix::Carve:            return "Prima Materia: a dip carved in the less important track";
            case suite::Fix::GainStage:        return "Prima Materia: every track's input set to -18 dBFS RMS";
            case suite::Fix::None:
            default:                           return {};
        }
    }
}
