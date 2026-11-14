#pragma once

// The label-ready clean-ups for the parts Correspondence writes, each with its own autopilot
// switch, switched on for this song when Ouroboros' checks found the problem (latched, saved with
// the project):
//  - bass notes stop a 16th before the next kick (the kick on every beat);
//  - bass notes outside the key snapped to the nearest note in it;
//  - the offbeat 8ths accented on the bass and arp, the rest a little softer;
//  - ghost notes (velocity under 45) on the bass and arp brought up;
//  - the bass's thirds on the beat moved to the chord's root;
//  - the chords' top notes an octave down where they'd sit in the lead's range.
// Included by ChordEngine.h just before songClip (it needs Settings, SongPatterns and the chords).

namespace chords::polish
{
    /** The section a song position is in, and where it starts (quarters). */
    inline int sectionAt (const suite::SectionMap& map, double q, double& startQ)
    {
        int idx = -1;
        for (int i = 0; i < map.count; ++i)
            if ((map.sections[(size_t) i].startBar - 1) * 4.0 <= q + 1.0e-6) { idx = i; startQ = (map.sections[(size_t) i].startBar - 1) * 4.0; }
        return idx;
    }

    inline void apply (std::vector<Note>& notes, const Settings& s, const suite::SectionMap& map, const SongPatterns& p, SongPart part, int leadLow)
    {
        const auto& P = p.polish;
        if (! P.any()) return;
        const auto isBass = part == SongPart::Bass, isArp = part == SongPart::Arp, isChords = part == SongPart::Chords;
        const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, s.scale)].steps;
        auto inKey = [&] (int pitch, int shift) { const auto pc = ((pitch - s.root - shift) % 12 + 12) % 12; return std::find (steps.begin(), steps.end(), pc) != steps.end(); };
        const auto qpc = juce::jmax (0.5, s.quartersPerChord);
        const auto slots = juce::jlimit (1, maxSlots, s.count);
        for (auto& n : notes)
        {
            double secStart = 0.0;
            const auto sec = sectionAt (map, n.start, secStart);
            const auto shift = sec >= 0 ? map.keyShiftOf (sec) : 0;
            if (isBass && P.clearKick)
            {
                const auto nextBeat = std::floor (n.start + 1.0e-6) + 1.0;
                if (n.start + n.length > nextBeat - 0.25 + 1.0e-6) n.length = juce::jmax (0.125, nextBeat - 0.25 - n.start);
            }
            if (isBass && P.inKey && ! inKey (n.pitch, shift))
                n.pitch = inKey (n.pitch - 1, shift) ? n.pitch - 1 : n.pitch + 1;
            if (isBass && P.roots && std::abs (n.start - std::round (n.start)) < 1.0e-3 && sec >= 0)
            {
                const auto slot = (int) std::floor ((n.start - secStart) / qpc) % slots;
                const auto rootPc = (chordRootPitchClass (s, slot) + shift + 120) % 12;
                const auto rel = ((n.pitch - rootPc) % 12 + 12) % 12;
                if (rel == 3 || rel == 4) n.pitch -= rel;   // the third down to the root
            }
            if ((isBass || isArp) && P.accents)
            {
                const auto frac = n.start - std::floor (n.start);
                n.velocity = std::abs (frac - 0.5) < 1.0e-3 ? juce::jmin (127, n.velocity + 10) : juce::jmax (1, n.velocity - 4);
            }
            if ((isBass || isArp) && P.ghosts && n.velocity < 45) n.velocity = juce::jmin (60, n.velocity + 15);
            if (isChords && P.voicingRoom && leadLow > 0)
                while (n.pitch >= leadLow - 1 && n.pitch - 12 >= 36) n.pitch -= 12;
        }
    }
}
