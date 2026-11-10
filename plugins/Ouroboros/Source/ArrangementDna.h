#pragma once

// Arrangement DNA: how you build songs, learned from the maps you saved as templates.
// It takes your most common structure (the order of section types), the typical length of
// each section in it and any energy you usually draw, and lays that out at the length you
// ask for, in whole 8-bar phrases.

#include "Arrangement.h"
#include <algorithm>
#include <map>
#include <vector>

namespace arrangement
{
    struct ArrangementDna
    {
        bool valid = false;
        int songs = 0;                       // maps with this structure
        int learnedFrom = 0;                 // maps looked at
        std::vector<SectionType> structure;  // your usual order of sections
        std::vector<int> bars;               // typical length of each (bars)
        std::vector<int> energy;             // typical drawn energy (-1 = usually untouched)
        int totalBars = 0;                   // typical song length
    };

    inline juce::String structureKey (const SectionMap& m)
    {
        juce::String key;
        for (int i = 0; i < m.count; ++i) key << (int) m.sections[(size_t) i].type << ",";
        return key;
    }

    /** Learns from your saved maps (two or more needed). */
    inline ArrangementDna learnDna (const std::vector<SectionMap>& maps)
    {
        ArrangementDna dna;
        dna.learnedFrom = (int) maps.size();
        std::map<juce::String, std::vector<const SectionMap*>> byStructure;
        for (const auto& m : maps)
            if (m.count > 0) byStructure[structureKey (m)].push_back (&m);
        const std::vector<const SectionMap*>* best = nullptr;
        for (const auto& [key, group] : byStructure)
            if (best == nullptr || group.size() > best->size()) best = &group;
        if (best == nullptr || maps.size() < 2) return dna;

        auto median = [] (std::vector<int> v)
        {
            std::sort (v.begin(), v.end());
            return v.empty() ? 0 : v[v.size() / 2];
        };
        const auto& first = *best->front();
        std::vector<int> totals;
        for (const auto* m : *best) totals.push_back (m->totalBars);
        dna.totalBars = median (totals);
        for (int i = 0; i < first.count; ++i)
        {
            std::vector<int> lengths, drawn;
            int untouched = 0;
            for (const auto* m : *best)
            {
                lengths.push_back (m->endBar (i) - m->sections[(size_t) i].startBar);
                if (m->sections[(size_t) i].energy >= 0) drawn.push_back (m->sections[(size_t) i].energy);
                else ++untouched;
            }
            dna.structure.push_back (first.sections[(size_t) i].type);
            dna.bars.push_back (juce::jmax (8, median (lengths)));
            dna.energy.push_back ((int) drawn.size() > untouched ? median (drawn) : -1);
        }
        dna.songs = (int) best->size();
        dna.valid = true;
        return dna;
    }

    /** Your structure at `totalBars` (0 = your usual length), in 8-bar phrases. */
    inline SectionMap generateLike (const ArrangementDna& dna, int totalBars = 0)
    {
        SectionMap m;
        if (! dna.valid || dna.structure.empty()) return m;
        const auto n = juce::jmin ((int) dna.structure.size(), SectionMap::maxSections);
        const auto target = juce::jmax (8 * n, (int) std::round ((totalBars > 0 ? totalBars : dna.totalBars) / 8.0) * 8);
        int usual = 0;
        for (int i = 0; i < n; ++i) usual += dna.bars[(size_t) i];
        const auto scale = (double) target / juce::jmax (1, usual);

        std::vector<int> phrases ((size_t) n);
        int sum = 0;
        for (int i = 0; i < n; ++i)
        {
            phrases[(size_t) i] = juce::jmax (1, (int) std::round (dna.bars[(size_t) i] * scale / 8.0));
            sum += phrases[(size_t) i];
        }
        // Fit exactly: grow or shrink the longest sections (drops and breakdowns first on ties).
        auto pick = [&] (bool grow)
        {
            int best = -1;
            for (int i = 0; i < n; ++i)
            {
                if (! grow && phrases[(size_t) i] <= 1) continue;
                const auto weight = phrases[(size_t) i] * 4 + (dna.structure[(size_t) i] == SectionType::Drop ? 2 : 0)
                                  + (dna.structure[(size_t) i] == SectionType::Breakdown ? 1 : 0);
                if (best < 0 || weight > phrases[(size_t) best] * 4 + (dna.structure[(size_t) best] == SectionType::Drop ? 2 : 0)
                                          + (dna.structure[(size_t) best] == SectionType::Breakdown ? 1 : 0))
                    best = i;
            }
            return best;
        };
        while (sum * 8 < target) { const auto i = pick (true); ++phrases[(size_t) i]; ++sum; }
        while (sum * 8 > target) { const auto i = pick (false); if (i < 0) break; --phrases[(size_t) i]; --sum; }

        int bar = 1;
        for (int i = 0; i < n; ++i)
        {
            m.sections[(size_t) i] = { bar, dna.structure[(size_t) i], dna.energy[(size_t) i] };
            bar += phrases[(size_t) i] * 8;
        }
        m.count = n;
        m.totalBars = bar - 1;
        return m;
    }

    //==============================================================================
    // Section markers for FL Studio: a MIDI file with a marker at every section, and a
    // plain cue sheet with bars and times.

    inline std::vector<std::pair<int, juce::String>> markerNames (const SectionMap& m)
    {
        std::vector<std::pair<int, juce::String>> out;
        std::array<int, (size_t) SectionType::count> seen {};
        for (int i = 0; i < m.count; ++i)
        {
            const auto type = m.sections[(size_t) i].type;
            const auto n = ++seen[(size_t) type];
            out.push_back ({ m.sections[(size_t) i].startBar, juce::String (sectionName (type)) + (n > 1 ? " " + juce::String (n) : juce::String()) });
        }
        return out;
    }

    inline juce::MidiFile markerMidi (const SectionMap& m, double bpm, double barQuarters = 4.0)
    {
        constexpr int ppq = 960;
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Song sections"), 0.0);
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / juce::jmax (1.0, bpm))), 0.0);
        seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (juce::roundToInt (barQuarters), 4), 0.0);
        for (const auto& [bar, name] : markerNames (m))
            seq.addEvent (juce::MidiMessage::textMetaEvent (6, name), (bar - 1) * barQuarters * ppq);
        seq.addEvent (juce::MidiMessage::endOfTrack(), m.totalBars * barQuarters * ppq);
        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        file.addTrack (seq);
        return file;
    }

    inline juce::String cueSheet (const SectionMap& m, double bpm, double barQuarters = 4.0)
    {
        juce::StringArray lines;
        const auto secondsPerBar = barQuarters * 60.0 / juce::jmax (1.0, bpm);
        auto time = [] (double s) { return juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2); };
        for (const auto& [bar, name] : markerNames (m))
            lines.add ("Bar " + juce::String (bar).paddedLeft (' ', 4) + "  " + time ((bar - 1) * secondsPerBar) + "  " + name);
        lines.add ("End " + juce::String (m.totalBars + 1).paddedLeft (' ', 4) + "  " + time (m.totalBars * secondsPerBar));
        return lines.joinIntoString ("\n") + "\n";
    }
}
