#pragma once

// Automatic mix fixes and checks, from what every Prima Materia reports (each track's role and
// its long-term octave-band levels in the drops) and the master's tone against the target.
//
//  - Mud cleanup: too much 250 Hz on the master in the drops -> the tracks putting most there
//    (never the kick, the bass or the vocal) get a dip in that band.
//  - Harshness finder: too much 2-4 kHz -> the same, for the band that's worst (never the vocal).
//  - Translation check: what disappears on a phone or earbuds, and in mono.

#include "Advisor.h"
#include "Registry.h"
#include "Analysis.h"
#include <algorithm>
#include <cmath>

namespace mixfix
{
    // Prima Materia roles: 0 none, 1 kick, 2 bass, 3 hats, 4 pads, 5 lead, 6 vocal, 7 FX
    struct Culprit { uint32_t id = 0; juce::String track; int role = -1; float db = -100.0f; };

    inline float powerSum (const std::array<float, 10>& bands, int from, int to)
    {
        double p = 0.0;
        for (int b = from; b <= to; ++b) p += std::pow (10.0, bands[(size_t) b] / 10.0);
        return p > 1.0e-12 ? (float) (10.0 * std::log10 (p)) : -120.0f;
    }

    /** The tracks putting most into `band` (up to two, within 6 dB of the loudest), skipping the given roles. */
    inline std::vector<Culprit> culprits (const std::vector<suite::SuiteEntry>& entries, int band, std::initializer_list<int> skipRoles)
    {
        // Prima Materia's report, or Quintessence's on a track without one (fixes go to that EQ)
        auto pmOn = [&] (const juce::String& track)
        {
            return std::any_of (entries.begin(), entries.end(), [&] (const suite::SuiteEntry& o)
                                { return o.plugin == "Prima Materia" && o.track == track && o.has (suite::Status::hasBands); });
        };
        std::vector<Culprit> all;
        for (const auto& e : entries)
        {
            if (! e.has (suite::Status::hasBands)) continue;
            if (e.plugin != "Prima Materia" && ! (e.plugin == "Quintessence" && (e.track.isEmpty() || ! pmOn (e.track)))) continue;
            const auto role = (int) e.metrics[0];
            if (std::find (skipRoles.begin(), skipRoles.end(), role) != skipRoles.end()) continue;
            all.push_back ({ e.id, e.track, role, e.bands[(size_t) band] });
        }
        std::sort (all.begin(), all.end(), [] (const Culprit& a, const Culprit& b) { return a.db > b.db; });
        std::vector<Culprit> out;
        for (const auto& c : all)
            if (out.size() < 2 && (out.empty() || c.db > out.front().db - 6.0f)) out.push_back (c);
        return out;
    }

    struct ToneFix { int band = -1; float excessDb = 0.0f; std::vector<Culprit> tracks; juce::String text; };

    /** Mud (250 Hz) and harshness (2 or 4 kHz, the worse) on the master, more than 2 dB over the target. */
    inline std::vector<ToneFix> toneFixes (const advisor::Bands& masterDiff, bool haveTonal, const std::vector<suite::SuiteEntry>& entries)
    {
        std::vector<ToneFix> out;
        if (! haveTonal) return out;
        if (masterDiff[3] > 2.0f)
        {
            ToneFix f { 3, masterDiff[3], culprits (entries, 3, { 1, 2, 6 }), {} };
            if (! f.tracks.empty())
            {
                juce::StringArray names;
                for (auto& c : f.tracks) names.add (c.track);
                f.text = "Mud: 250 Hz is " + juce::String (f.excessDb, 1) + " dB over the target: a dip there on " + names.joinIntoString (" and ");
                out.push_back (f);
            }
        }
        const auto harshBand = masterDiff[7] > masterDiff[6] ? 7 : 6;
        if (masterDiff[(size_t) harshBand] > 2.0f)
        {
            ToneFix f { harshBand, masterDiff[(size_t) harshBand], culprits (entries, harshBand, { 1, 6 }), {} };
            if (! f.tracks.empty())
            {
                juce::StringArray names;
                for (auto& c : f.tracks) names.add (c.track);
                f.text = "Harshness: " + juce::String (harshBand == 7 ? "4 kHz" : "2 kHz") + " is " + juce::String (f.excessDb, 1)
                         + " dB over the target: a dip there on " + names.joinIntoString (" and ");
                out.push_back (f);
            }
        }
        return out;
    }

    /** What doesn't translate: parts that vanish on a phone (300 Hz-4 kHz) or on earbuds (little
        under 80 Hz), and the mix in mono. */
    inline juce::StringArray translation (const std::vector<suite::SuiteEntry>& entries, float monoLossDb)
    {
        static const char* roleNames[] = { "track", "kick", "bass", "hats", "pads", "lead", "vocal", "FX" };
        juce::StringArray out;
        for (const auto& e : entries)
        {
            if (e.plugin != "Prima Materia" || ! e.has (suite::Status::hasBands)) continue;
            const auto role = juce::jlimit (0, 7, (int) e.metrics[0]);
            const auto full = powerSum (e.bands, 0, 9), phone = powerSum (e.bands, 4, 7), sub = powerSum (e.bands, 0, 1);
            const auto name = "the " + juce::String (roleNames[role]) + " ('" + e.track + "')";
            if ((role == 5 || role == 6) && full - phone > 6.0f)
                out.add ("On a phone, " + name + " loses " + juce::String (full - phone, 0) + " dB: most of it is outside 300 Hz-4 kHz. A brighter or more mid-forward layer keeps it there.");
            if ((role == 1 || role == 2) && full - sub < 1.5f)
                out.add ("On earbuds and phones, " + name + " nearly vanishes: almost all of it is below 80 Hz. Some drive (Vibration) or an upper layer gives it harmonics people hear.");
        }
        if (monoLossDb > 3.0f)
            out.add ("In mono (club systems, phones, Bluetooth speakers) the mix loses " + juce::String (monoLossDb, 1)
                     + " dB: check wide layers and phase-inverted copies; Mono lows in Prima Materia keeps the low end safe.");
        if (out.isEmpty()) out.add ("Translates: nothing vanishes on a phone or earbuds, and the mix holds up in mono.");
        return out;
    }
}
