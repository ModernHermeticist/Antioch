#pragma once

// Writing around the vocal, and for a thin mix.
//  - avoidVocal: where a written note rubs against a note the vocal holds (a beat or longer)
//    by a semitone, a major seventh or a tritone, it moves to the nearest note in key that
//    doesn't (usually a step), so the lead, arp and counter-melody sit with the vocal.
//  - layerClip: for the kinds of section Mentalism hears as thin in the mids, a sustained chord
//    layer (open voicing, around middle C) to fill them out.

#include "ChordEngine.h"
#include "WritingBus.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace vocalaware
{
    inline bool rubs (int a, int b) { const auto i = ((a - b) % 12 + 12) % 12; return i == 1 || i == 11 || i == 6; }

    /** Returns how many notes it moved. */
    inline int avoidVocal (std::vector<chords::Note>& notes, const std::vector<suite::WritingBus::SungNote>& sung, const chords::Settings& s)
    {
        if (sung.empty()) return 0;
        const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, s.scale)].steps;
        auto inKey = [&] (int p) { return std::find (steps.begin(), steps.end(), ((p - s.root) % 12 + 12) % 12) != steps.end(); };
        int moved = 0;
        for (auto& n : notes)
        {
            std::vector<int> held;
            for (const auto& v : sung)
                if (v.length >= 1.0 - 1.0e-9 && v.start < n.start + n.length - 1.0e-9 && v.start + v.length > n.start + 1.0e-9) held.push_back (v.pitch);
            auto clashes = [&] (int p) { return std::any_of (held.begin(), held.end(), [&] (int h) { return rubs (p, h); }); };
            if (held.empty() || ! clashes (n.pitch)) continue;
            for (int d : { 1, -1, 2, -2, 3, -3 })
                if (inKey (n.pitch + d) && ! clashes (n.pitch + d)) { n.pitch += d; ++moved; break; }
        }
        return moved;
    }

    inline std::vector<chords::Note> layerClip (const chords::Settings& base, const suite::SectionMap& map, uint32_t thinMask)
    {
        std::vector<chords::Note> out;
        if (thinMask == 0 || map.count == 0) return out;
        auto s = base;
        s.voicing = chords::Voicing::Open;
        s.addBass = false;
        const auto voiced = chords::voiceProgression (s);
        if (voiced.empty()) return out;
        const auto L = s.quartersPerChord;
        const auto progressionQ = L * (double) voiced.size();
        for (int i = 0; i < map.count; ++i)
        {
            if ((thinMask & (1u << (int) map.sections[(size_t) i].type)) == 0) continue;
            const auto startQ = (map.sections[(size_t) i].startBar - 1) * 4.0, endQ = (map.endBar (i) - 1) * 4.0;
            for (auto q = startQ; q < endQ - 1.0e-9; q += L)
            {
                const auto slot = (size_t) std::floor (std::fmod (q - startQ, progressionQ) / L + 1.0e-9) % voiced.size();
                for (auto p : voiced[slot])
                {
                    auto pitch = p;
                    while (pitch < 55) pitch += 12;
                    while (pitch > 79) pitch -= 12;
                    out.push_back ({ pitch + map.keyShiftOf (i), q, std::min (L, endQ - q) - 0.05, 78 });
                }
            }
        }
        return out;
    }
}
