#pragma once

// The second set of label-ready clean-ups for Correspondence's parts (latched where Ouroboros'
// checks ask, each with its autopilot switch):
//  - pickups into each drop: three notes climbing into the lead's first note, an octave pickup
//    on the bass;
//  - part fills at each 8-bar phrase end: a bass run, the arp's last beat an octave up;
//  - the arp moved off the hook's rhythm: arp notes landing with a hook note in the drops go;
//  - the melody's peak late in each phrase: an early-peaking phrase's last two beats lifted;
//  - the counter-melody only from the second drop (an escalation);
//  - bass note lengths sized to the section: short in the drops, joined up in the breakdowns;
//  - the bass locked to the kit's groove: off-grid 16ths moved to the offbeat 8th.
// Included by ChordEngine.h after LabelPolish.h.

namespace chords::polish
{
    inline void apply2 (std::vector<Note>& notes, const Settings& s, const suite::SectionMap& map, const SongPatterns& p, SongPart part, const std::vector<Note>& hook)
    {
        const auto& P = p.polish;
        using ST = suite::SectionType;
        const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, s.scale)].steps;
        auto snap = [&] (int pitch, int shift)
        {
            for (int d = 0; d < 12; ++d)
                for (int sign : { -1, 1 })
                {
                    const auto cand = pitch + sign * d;
                    const auto pc = ((cand - s.root - shift) % 12 + 12) % 12;
                    if (std::find (steps.begin(), steps.end(), pc) != steps.end()) return cand;
                }
            return pitch;
        };
        auto typeAt = [&map] (double q, double& start, double& end)
        {
            int idx = -1;
            for (int i = 0; i < map.count; ++i) if ((map.sections[(size_t) i].startBar - 1) * 4.0 <= q + 1.0e-6) idx = i;
            if (idx < 0) return ST::Intro;
            start = (map.sections[(size_t) idx].startBar - 1) * 4.0; end = (map.endBar (idx) - 1) * 4.0;
            return map.sections[(size_t) idx].type;
        };
        // drops' starts (and which drop)
        std::vector<std::pair<double, int>> dropStarts;
        for (int i = 0; i < map.count; ++i) if (map.sections[(size_t) i].type == ST::Drop) dropStarts.push_back ({ (map.sections[(size_t) i].startBar - 1) * 4.0, i });

        if (part == SongPart::Counter && P.counterLater && ! dropStarts.empty())
        {
            const auto from = dropStarts[0].first, to = (map.endBar (dropStarts[0].second) - 1) * 4.0;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [from, to] (const Note& n) { return n.start >= from - 1.0e-6 && n.start < to; }), notes.end());
        }
        if (part == SongPart::Arp && P.arpOffHook && ! hook.empty())
        {
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n)
            {
                double st = 0.0, en = 0.0;
                if (typeAt (n.start, st, en) != ST::Drop) return false;
                return std::any_of (hook.begin(), hook.end(), [&n] (const Note& h) { return std::abs (h.start - n.start) < 0.01; });
            }), notes.end());
        }
        if (part == SongPart::Bass && (P.bassLength || P.grooveLock))
        {
            std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
            for (size_t i = 0; i < notes.size(); ++i)
            {
                auto& n = notes[i];
                double st = 0.0, en = 0.0;
                const auto t = typeAt (n.start, st, en);
                if (P.grooveLock && p.kitOffbeatOnly)
                {
                    const auto frac = n.start - std::floor (n.start);
                    if (std::abs (frac - 0.25) < 1.0e-3 || std::abs (frac - 0.75) < 1.0e-3) n.start = std::floor (n.start) + 0.5;
                }
                if (P.bassLength)
                {
                    if (t == ST::Drop) n.length = juce::jmin (n.length, 0.375);
                    else if (t == ST::Breakdown && i + 1 < notes.size())
                        n.length = juce::jlimit (n.length, 4.0, notes[i + 1].start - n.start);
                }
            }
        }
        if (part == SongPart::Bass && P.partFills)
        {
            std::vector<Note> fills;
            for (int i = 0; i < map.count; ++i)
            {
                const auto st = (map.sections[(size_t) i].startBar - 1) * 4.0, en = (map.endBar (i) - 1) * 4.0;
                const auto shift = map.keyShiftOf (i);
                for (auto phraseEnd = st + 32.0; phraseEnd <= en + 1.0e-6; phraseEnd += 32.0)
                {
                    const auto from = phraseEnd - 1.0;
                    int base = -1;
                    for (const auto& n : notes) if (n.start >= from - 4.0 && n.start < from) base = n.pitch;
                    if (base < 0) continue;
                    notes.erase (std::remove_if (notes.begin(), notes.end(), [from, phraseEnd] (const Note& n) { return n.start >= from - 1.0e-6 && n.start < phraseEnd; }), notes.end());
                    const int run[4] = { 0, 3, 7, 12 };
                    for (int k = 0; k < 4; ++k) fills.push_back ({ snap (base + run[k], shift), from + 0.25 * k, 0.2, 92 });
                }
            }
            notes.insert (notes.end(), fills.begin(), fills.end());
        }
        if (part == SongPart::Arp && P.partFills)
            for (auto& n : notes)
            {
                double st = 0.0, en = 0.0;
                typeAt (n.start, st, en);
                const auto inPhrase = std::fmod (n.start - st, 32.0);
                if (inPhrase >= 31.0 - 1.0e-6 && n.pitch + 12 <= 96) n.pitch += 12;
            }
        if (P.pickups && (part == SongPart::Lead || part == SongPart::Bass))
            for (const auto& [q, idx] : dropStarts)
            {
                if (q < 1.0) continue;
                const auto shift = map.keyShiftOf (idx);
                int first = -1;
                for (const auto& n : notes) if (n.start >= q - 1.0e-6 && n.start < q + 0.5) { first = n.pitch; break; }
                if (first < 0) continue;
                notes.erase (std::remove_if (notes.begin(), notes.end(), [q] (const Note& n) { return n.start >= q - 0.75 - 1.0e-6 && n.start < q; }), notes.end());
                if (part == SongPart::Lead)
                {
                    const int steps3[3] = { -5, -3, -1 };
                    for (int k = 0; k < 3; ++k) notes.push_back ({ snap (first + steps3[k], shift), q - 0.75 + 0.25 * k, 0.22, 88 });
                }
                else notes.push_back ({ first + 12, q - 0.5, 0.4, 96 });
            }
        if (part == SongPart::Lead && P.peaksLate)
            for (const auto& [q, idx] : dropStarts)
            {
                const auto end = (map.endBar (idx) - 1) * 4.0;
                for (auto ph = q; ph + 32.0 <= end + 1.0e-6; ph += 32.0)
                {
                    int peak = 0; double peakAt = ph; int lateMax = 0;
                    for (const auto& n : notes) if (n.start >= ph && n.start < ph + 32.0) { if (n.pitch > peak) { peak = n.pitch; peakAt = n.start; } if (n.start >= ph + 30.0) lateMax = juce::jmax (lateMax, n.pitch); }
                    if (peak == 0 || peakAt >= ph + 16.0 || lateMax == 0 || lateMax + 12 > juce::jmin (91, peak + 5)) continue;
                    for (auto& n : notes) if (n.start >= ph + 30.0 && n.start < ph + 32.0) n.pitch += 12;
                }
            }
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    }
}
