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
        double brightFirst = 0.0, brightSecond = 0.0;   // builds: sum of (8 kHz minus 1 kHz band level, dB) per half
        int blocks = 0, firstBlocks = 0, secondBlocks = 0;
        /** True when the build has real top end to judge: 8 kHz within 15 dB of 1 kHz (mixes sit about 6 dB under;
            a lone mid tone leaks about 18 dB into the 8 kHz band). */
        bool hasTopEnd() const { return secondBlocks > 0 && brightSecond / secondBlocks > -15.0; }
        /** How much brighter (dB, top end against the mids) the second half of a build is than the first. */
        float brightnessRise() const { return firstBlocks > 0 && secondBlocks > 0 ? (float) (brightSecond / secondBlocks - brightFirst / firstBlocks) : 0.0f; }

        float lufs() const { return blocks > 0 ? analysis::powerToLufs (power / blocks) : -100.0f; }
        float firstLufs() const { return firstBlocks > 0 ? analysis::powerToLufs (firstHalf / firstBlocks) : -100.0f; }
        float secondLufs() const { return secondBlocks > 0 ? analysis::powerToLufs (secondHalf / secondBlocks) : -100.0f; }
        bool heard() const { return blocks >= 10; }   // at least ~1 s of material
    };

    constexpr float minBrightening = 1.0f;   // a build should also get brighter (top end against the mids), dB
    constexpr float arcLuPerEnergy = 12.0f, arcTolerance = 3.0f;   // the energy curve: 12 LU between "silent" and "full"
    constexpr float minContrast = 2.0f, maxContrast = 12.0f, minRise = 1.0f, dropTolerance = 0.7f, introHeadroom = 1.5f;

    struct Result
    {
        juce::StringArray notes;       // problems first, then confirmations
        std::vector<suite::Fix> fixes; // per note: what the fix-it button would do (None for confirmations)
        std::vector<uint32_t> fixData; // per note: the fix's data (ArcMatch: which section and how much)
        int problems = 0;
        bool complete = true;          // every section heard
    };

    inline juce::String barsOf (const suite::SectionMap& m, int i)
    {
        return "bars " + juce::String (m.sections[(size_t) i].startBar) + "-" + juce::String (m.endBar (i) - 1);
    }

    /** The numbers the coach judges by: generic, or learned from your finished songs. */
    struct Thresholds
    {
        float minContrast = coach::minContrast, maxContrast = coach::maxContrast, minRise = coach::minRise;
        bool personal = false;
        int songs = 0;
    };

    /** Your own thresholds from finished songs (style log "arc" entries; 3 or more needed):
        breakdown contrast within 3 LU of your usual, builds rising at least half your usual. */
    inline Thresholds learnThresholds (const std::vector<float>& contrasts, const std::vector<float>& rises)
    {
        Thresholds t;
        if (contrasts.size() < 3) return t;
        auto median = [] (std::vector<float> v) { std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
        const auto c = median (contrasts);
        t.minContrast = juce::jmax (1.0f, c - 3.0f);
        t.maxContrast = juce::jmin (16.0f, c + 3.0f);
        if (rises.size() >= 3) t.minRise = juce::jmax (0.5f, 0.5f * median (rises));
        t.personal = true;
        t.songs = (int) contrasts.size();
        return t;
    }

    inline Result analyse (const suite::SectionMap& map, const std::vector<SectionStat>& stats, const Thresholds& limits = {})
    {
        const auto minContrast = limits.minContrast, maxContrast = limits.maxContrast, minRise = limits.minRise;
        const juce::String yours = limits.personal ? " (your usual: " + juce::String ((limits.minContrast + limits.maxContrast) / 2.0f, 0) + " LU)" : juce::String();
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

            const auto drawn = map.sections[(size_t) i].energy >= 0;   // your curve decides this one (arc match, below)
            if (type == T::Breakdown && ! drawn)
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
                                 "like a lift. Thin the breakdown out or take more of its low end away." + yours);
                    r.fixes.push_back (suite::Fix::BreakdownQuieter);
                    ++r.problems;
                }
                else if (contrast > maxContrast)
                {
                    r.notes.add ("Breakdown (" + barsOf (map, i) + ") is " + fmt (contrast) + " LU below the drop: people turn it up and then "
                                 "get blasted. Bring the breakdown up a little." + yours);
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
                                 "more top end or bigger Transmutation builds.");
                    r.fixes.push_back (suite::Fix::BuildRise);
                    ++r.problems;
                }
                else if (st.hasTopEnd() && st.brightnessRise() < minBrightening)
                {
                    r.notes.add ("Build (" + barsOf (map, i) + ") gets louder but not brighter (" + juce::String (st.brightnessRise(), 1)
                                 + " dB more top end): open a filter across it, or add a riser or noise sweep.");
                    r.fixes.push_back (suite::Fix::BuildRise);
                    ++r.problems;
                }
                else good.add ("Build (" + barsOf (map, i) + ") rises " + fmt (rise) + " LU"
                               + (st.hasTopEnd() ? " and opens up " + fmt (st.brightnessRise()) + " dB" : juce::String()) + ": good.");
            }
            else if (type == T::Intro && ! drops.empty() && ! drawn)
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

        // Sections too short for their job: a build under 8 bars can't rise; a breakdown under 16
        // can't reset the energy. The fix lengthens it in Ouroboros. Only judged on song-length maps
        // (64 bars or more): a loop or a sketch isn't arranged yet.
        r.fixData.resize (r.fixes.size(), 0);
        for (int i = 0; i < (map.totalBars >= 64 ? map.count : 0); ++i)
        {
            const auto type = map.sections[(size_t) i].type;
            const auto bars = map.endBar (i) - map.sections[(size_t) i].startBar;
            const auto shortest = type == T::Build ? 8 : type == T::Breakdown ? 16 : 0;
            if (shortest == 0 || bars >= shortest) continue;
            r.notes.add (juce::String (suite::sectionName (type)) + " (" + barsOf (map, i) + ") is only " + juce::String (bars) + " bars: "
                         + (type == T::Build ? "a build needs 8 or more to rise." : "a breakdown needs 16 or more to reset the energy."));
            r.fixes.push_back (suite::Fix::ExtendSection);
            r.fixData.push_back (suite::packArc (i, 8.0f));
            ++r.problems;
        }

        // Arc match: sections whose energy you drew on Ouroboros' curve should sit where the curve
        // puts them, relative to the first drop (12 LU from an energy of 0 to 100%).
        r.fixData.resize (r.fixes.size(), 0);
        if (! drops.empty())
        {
            const auto ref = drops.front();
            const auto refLufs = stats[(size_t) ref].lufs(), refEnergy = map.energyOf (ref);
            bool anyDrawn = false, allMatch = true;
            for (int i = 0; i < map.count; ++i)
            {
                if (i == ref || ! heard (i) || map.sections[(size_t) i].energy < 0) continue;
                anyDrawn = true;
                const auto expected = refLufs + arcLuPerEnergy * (map.energyOf (i) - refEnergy);
                const auto off = stats[(size_t) i].lufs() - expected;
                if (std::abs (off) <= arcTolerance) continue;
                allMatch = false;
                r.notes.add (juce::String (suite::sectionName (map.sections[(size_t) i].type)) + " (" + barsOf (map, i) + ") is " + fmt (off) + " LU "
                             + (off > 0.0f ? "louder" : "quieter") + " than your energy curve asks for.");
                r.fixes.push_back (suite::Fix::ArcMatch);
                r.fixData.push_back (suite::packArc (i, juce::jlimit (-9.0f, 9.0f, -off)));
                ++r.problems;
            }
            if (anyDrawn && allMatch) good.add ("The song follows your energy curve.");
        }

        r.notes.addArray (good);
        if (! r.complete)
            r.notes.add ("Play the whole song through once to check every section.");
        r.fixes.resize ((size_t) r.notes.size(), suite::Fix::None);
        r.fixData.resize ((size_t) r.notes.size(), 0);
        return r;
    }

    /** What a fix-it button does, for its tooltip and the confirmation line. */
    inline juce::String describe (suite::Fix fix)
    {
        switch (fix)
        {
            case suite::Fix::BreakdownQuieter: return "Prima Materia: breakdowns 2 dB quieter";
            case suite::Fix::BreakdownLouder:  return "Prima Materia: breakdowns 2 dB louder";
            case suite::Fix::BuildRise:        return "Transmutation: the builds work harder; Azoth: more swell and roll";
            case suite::Fix::IntroQuieter:     return "Prima Materia: intro 2 dB quieter";
            case suite::Fix::LaterDropBigger:  return "Prima Materia: Drop 2+ 1.5 dB up; Azoth: bigger later drops";
            case suite::Fix::RoughMix:         return "Prima Materia: each track's trim set for its role, relative to the kick";
            case suite::Fix::DjIntroOutro:     return "Prima Materia: pads, leads and vocals out of the intro and outro";
            case suite::Fix::ReferenceTone:    return "Lapis: low and high tone moved towards the reference";
            case suite::Fix::Carve:            return "Prima Materia: a dip carved in the less important track";
            case suite::Fix::GainStage:        return "Prima Materia: every track's input set to -18 dBFS RMS";
            case suite::Fix::ExtendSection:    return "Ouroboros: that section 8 bars longer (the rest of the song moves along)";
            case suite::Fix::DjPhrasing:       return "Ouroboros: intro and outro to whole 16-bar phrases, sections onto the 8-bar grid";
            case suite::Fix::ArcMatch:         return "Prima Materia: that section's level moved towards your energy curve";
            case suite::Fix::None:
            default:                           return {};
        }
    }
}
