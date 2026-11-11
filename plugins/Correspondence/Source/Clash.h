#pragma once

// Clash check: parts written by Correspondence that sit on top of each other. For each kind of
// section in the map, the range each part plays in (its 10th to 90th percentile pitch); two
// melodic parts whose ranges mostly overlap (60% or more of the smaller one) clash: they blur
// together however they're mixed. Each clash comes with a fix that moves one part out of the
// way (the lead up an octave, the arp over fewer octaves).

#include "ChordEngine.h"
#include <algorithm>
#include <set>

namespace clash
{
    struct Clash
    {
        suite::SectionType type = suite::SectionType::Drop;
        juce::String text, fixText, paramId;
        float newValue = 0.0f;
    };

    struct Range { int lo = 0, hi = -1; bool valid() const { return hi >= lo; } };

    inline Range rangeOf (std::vector<int> pitches)
    {
        if (pitches.size() < 4) return {};
        std::sort (pitches.begin(), pitches.end());
        return { pitches[pitches.size() / 10], pitches[(pitches.size() * 9) / 10] };
    }

    inline float overlap (Range a, Range b)
    {
        if (! a.valid() || ! b.valid()) return 0.0f;
        const auto inter = std::min (a.hi, b.hi) - std::max (a.lo, b.lo);
        const auto smaller = std::max (1, std::min (a.hi - a.lo, b.hi - b.lo));
        return inter < 0 ? 0.0f : (float) inter / (float) smaller;
    }

    inline juce::String noteName (int p) { return suite::music::noteNames()[((p % 12) + 12) % 12] + juce::String (p / 12 - 1); }

    /** `melodyOctave` and `arpOctaves`: the current settings (for the fixes). */
    inline std::vector<Clash> find (const chords::Settings& s, const suite::SectionMap& map, const chords::SongPatterns& patterns,
                                    int melodyOctave, int arpOctaves, const juce::String& melodyOctaveId, const juce::String& arpOctavesId)
    {
        std::vector<Clash> out;
        if (map.count == 0) return out;
        const std::pair<chords::SongPart, const char*> parts[] { { chords::SongPart::Chords, "chords" }, { chords::SongPart::Arp, "arp" },
                                                                 { chords::SongPart::Lead, "lead" }, { chords::SongPart::Counter, "counter-melody" } };
        std::vector<std::vector<chords::Note>> clips;
        for (const auto& [part, name] : parts) clips.push_back (chords::songClip (s, map, patterns, part));
        std::set<int> seen;
        for (int i = 0; i < map.count; ++i)
        {
            const auto type = map.sections[(size_t) i].type;
            if (! seen.insert ((int) type).second) continue;
            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0, endQ = (map.endBar (i) - 1) * 4.0;
            std::vector<Range> ranges;
            for (const auto& clip : clips)
            {
                std::vector<int> p;
                for (const auto& n : clip) if (n.start >= startQ && n.start < endQ) p.push_back (n.pitch);
                ranges.push_back (rangeOf (p));
            }
            auto check = [&] (int a, int b)
            {
                if (overlap (ranges[(size_t) a], ranges[(size_t) b]) < 0.6f) return;
                Clash c;
                c.type = type;
                c.text = juce::String (suite::sectionName (type)) + ": the " + parts[a].second + " (" + noteName (ranges[(size_t) a].lo) + "-" + noteName (ranges[(size_t) a].hi)
                         + ") and the " + parts[b].second + " (" + noteName (ranges[(size_t) b].lo) + "-" + noteName (ranges[(size_t) b].hi) + ") play in the same register";
                if ((a == 2 || b == 2) && melodyOctave < 6) { c.fixText = "the lead up an octave"; c.paramId = melodyOctaveId; c.newValue = (float) melodyOctave + 1.0f; }
                else if ((a == 1 || b == 1) && arpOctaves > 1) { c.fixText = "the arp over one octave fewer"; c.paramId = arpOctavesId; c.newValue = (float) arpOctaves - 1.0f; }
                else c.fixText = "move one of them an octave in the piano roll";
                out.push_back (c);
            };
            check (2, 1);   // lead and arp
            check (2, 0);   // lead and chords
            check (1, 0);   // arp and chords
            check (3, 1);   // counter and arp
        }
        return out;
    }
}
