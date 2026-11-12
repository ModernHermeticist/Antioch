#pragma once

// Plays a pattern-library family over the progression (included by ChordEngine.h, where
// Settings, buildChord and voiceProgression are defined). See PatternLibrary.h for the notation.

#include "PatternLibrary.h"

namespace chords::lib
{
    /** Which kind of family a song-pattern choice asks for (-1: not from the library). */
    inline int kindFor (SongPart part, int choice, suite::SectionType type)
    {
        using T = suite::SectionType;
        if (choice <= 0) return -1;
        switch (part)
        {
            case SongPart::Bass:
                if (choice == 6) return -1;                    // "Mine": your learned bassline stays yours
                return choice == 4 ? 0 : (choice == 2 || choice == 3) ? 2 : 1;
            case SongPart::Arp:
                if (type == T::Intro || type == T::Outro || type == T::Breakdown) return 0;   // the quiet sections: the melodic arps
                return choice >= 7 ? 2 : 1;
            case SongPart::Chords:
                return choice == 1 ? 0 : choice == 4 ? 2 : 1;
            case SongPart::Lead: case SongPart::Harmony: case SongPart::Counter:
                return 0;
        }
        return -1;
    }

    inline patterns::Part libraryPart (SongPart part)
    {
        return part == SongPart::Bass ? patterns::Part::Bass : part == SongPart::Arp ? patterns::Part::Arp
             : part == SongPart::Chords ? patterns::Part::Chords : patterns::Part::Lead;
    }

    /** `lengthQ` quarters of the family at `level` (0-2) over the progression, from its first chord.
        `fillAll`: every bar plays the fill (the phrase-end bar). Leads repeat their two-bar cell and
        answer it with the fill in the last two bars of every eight. */
    inline std::vector<Note> render (const patterns::Family& f, int level, bool fillAll, const Settings& s, double lengthQ, int leadOctave = 5)
    {
        using patterns::Part;
        std::vector<Note> out;
        const auto count = juce::jlimit (1, maxSlots, s.count);
        const auto L = s.quartersPerChord;
        const auto mainBars = patterns::bars (f.level[(size_t) juce::jlimit (0, 2, level)]);
        const auto fillBars = patterns::bars (f.fill != nullptr ? f.fill : f.level[2]);
        const auto& keySteps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, s.scale)].steps;
        auto inKey = [&] (int p) { return std::find (keySteps.begin(), keySteps.end(), ((p - s.root) % 12 + 12) % 12) != keySteps.end(); };
        auto scaleStep = [&] (int p, int dir) { do p += dir; while (! inKey (p)); return p; };
        const auto lead = f.part == Part::Lead;
        const auto centre = 12 * (leadOctave + 1) + 4;
        std::vector<std::vector<int>> voiced;
        if (f.part == Part::Chords) voiced = voiceProgression (s);

        struct Ev { double start; std::vector<int> pitches; int velocity; double holdEnd; bool ghost; };
        std::vector<Ev> evs;
        int prev = -1, arpIndex = 0;
        bool holding = false;
        const auto totalSteps = (int) std::lround (lengthQ * 4.0);
        static const float levelVel[] = { 0.86f, 0.93f, 1.0f };

        for (int g = 0; g < totalSteps; ++g)
        {
            const auto q = g * 0.25;
            const auto chordIdx = (int) std::floor (q / L + 1.0e-9) % count;
            const auto bar = g / 16, stepInBar = g % 16;
            const auto answer = lead && bar % 8 >= 6;
            const auto& src = fillAll || answer ? fillBars : mainBars;
            const auto barIndex = lead ? (answer ? bar % 8 - 6 : bar % 8) : bar;
            const auto ch = src[(size_t) barIndex % src.size()][(size_t) stepInBar];
            if (ch == '.') { holding = false; continue; }
            if (ch == '-') { if (holding && ! evs.empty()) evs.back().holdEnd = q + 0.25; continue; }

            const auto chord = buildChord (s, s.slots[(size_t) chordIdx]);
            auto intervals = chord.intervals;
            std::sort (intervals.begin(), intervals.end());
            const auto rootPc = chordRootPitchClass (s, chordIdx);
            const auto nextPc = chordRootPitchClass (s, (chordIdx + 1) % count);
            const auto base = f.part == Part::Bass ? 12 * (s.bassOctave + 1) + rootPc
                            : f.part == Part::Lead ? 12 * (leadOctave + 1) + rootPc
                            : 12 * (s.registerOctave + 1) + rootPc;
            const auto from = prev >= 0 ? prev : (lead ? centre : base);
            auto toneOffset = [&] (char c)
            {
                switch (c)
                {
                    case 'r': case 'R': return 0;
                    case 't': case 'T': return intervals.size() > 1 ? intervals[1] : 4;
                    case 'f': case 'F': return intervals.size() > 2 ? intervals[2] : 7;
                    default:            return intervals.size() > 3 && intervals[3] < 12 ? intervals[3] : 12;   // s / S
                }
            };
            auto nearest = [] (int pc, int to) { auto p = to - (((to - pc) % 12) + 12) % 12; if (to - p > 6) p += 12; return p; };
            auto nextRoot = [&] { return nearest (nextPc, f.part == Part::Bass ? base : from); };

            std::vector<int> pitches;
            auto velocity = stepInBar % 4 == 0 ? 112 : stepInBar % 2 == 0 ? 102 : 92;
            auto ghost = false;
            switch (ch)
            {
                case 'r': case 't': case 'f': case 's': case 'R': case 'T': case 'F': case 'S':
                {
                    const auto up = std::isupper ((unsigned char) ch) ? 12 : 0;
                    if (lead) pitches = { nearest ((rootPc + toneOffset (ch)) % 12, from) + up };
                    else pitches = { base + toneOffset (ch) + up };
                    velocity += up > 0 ? 6 : 0;
                    break;
                }
                case 'l': pitches = { base - 12 >= 24 ? base - 12 : base }; break;
                case '9': pitches = { scaleStep (base, 1) }; break;
                case '4': pitches = { scaleStep (scaleStep (scaleStep (base, 1), 1), 1) }; break;
                case 'u': pitches = { scaleStep (from, 1) }; break;
                case 'd': pitches = { scaleStep (from, -1) }; break;
                case 'k': pitches = { from }; break;
                case 'n': { const auto t = nextRoot(); pitches = { t == from ? t : scaleStep (t, t > from ? -1 : 1) }; break; }
                case 'N': { const auto t = nextRoot(); pitches = { t > from ? t - 1 : t + 1 }; break; }
                case ',': pitches = { from }; ghost = true; velocity = 52; break;
                case 'a': case 'A': case 'b':
                {
                    std::vector<int> tones;
                    for (int o = 0; o < juce::jlimit (1, 3, s.arpOctaves); ++o)
                        for (auto iv : intervals) if (iv < 12) tones.push_back (base + 12 * o + iv);
                    std::sort (tones.begin(), tones.end());
                    tones.erase (std::unique (tones.begin(), tones.end()), tones.end());
                    const auto n = (int) tones.size();
                    std::vector<int> order;
                    switch (f.order)
                    {
                        case patterns::down:     for (int k = n - 1; k >= 0; --k) order.push_back (k); break;
                        case patterns::upDown:   for (int k = 0; k < n; ++k) order.push_back (k); for (int k = n - 2; k > 0; --k) order.push_back (k); break;
                        case patterns::converge: for (int lo = 0, hi = n - 1; lo <= hi; ++lo, --hi) { order.push_back (lo); if (hi != lo) order.push_back (hi); } break;
                        case patterns::skip:     for (int k = 0; k + 2 < n; ++k) { order.push_back (k); order.push_back (k + 2); } break;
                        default:                 for (int k = 0; k < n; ++k) order.push_back (k); break;
                    }
                    if (order.empty()) order.push_back (0);
                    if (ch == 'b') arpIndex = juce::jmax (0, arpIndex - 2);
                    pitches = { tones[(size_t) order[(size_t) (arpIndex++ % (int) order.size())]] + (ch == 'A' ? 12 : 0) };
                    break;
                }
                case 'c': case 'C': case 'o':
                {
                    pitches = voiced.empty() ? std::vector<int> { base, base + toneOffset ('t'), base + toneOffset ('f') } : voiced[(size_t) chordIdx];
                    if (ch == 'C') pitches.push_back (*std::max_element (pitches.begin(), pitches.end()) + 12);
                    if (ch == 'o') pitches = { *std::min_element (pitches.begin(), pitches.end()), *std::min_element (pitches.begin(), pitches.end()) + 7 };
                    velocity -= 8;
                    break;
                }
                default: holding = false; continue;
            }
            if (ch == ',' && f.part == Part::Chords && ! voiced.empty()) pitches = voiced[(size_t) chordIdx];
            // Leads stay in a singable range; everything stays in MIDI range
            for (auto& p : pitches)
            {
                if (lead) { while (p > centre + 19) p -= 12; while (p < centre - 10) p += 12; }
                p = juce::jlimit (0, 127, p);
            }
            velocity = juce::jlimit (1, 127, (int) std::lround (velocity * levelVel[(size_t) juce::jlimit (0, 2, level)]));
            evs.push_back ({ q, pitches, velocity, -1.0, ghost });
            prev = pitches.front();
            holding = ! ghost;
        }

        // Lengths: held notes to the end of their hold; the rest a share (the gate) of the gap to the next note
        for (size_t i = 0; i < evs.size(); ++i)
        {
            const auto& e = evs[i];
            const auto next = i + 1 < evs.size() ? evs[i + 1].start : lengthQ;
            auto len = e.holdEnd > 0.0 ? e.holdEnd - e.start - 0.02 : std::min (4.0, (next - e.start) * (double) f.gate);
            if (e.ghost) len = std::min (len, 0.12);
            len = juce::jlimit (0.05, juce::jmax (0.05, next - e.start - 0.01), len);
            for (auto p : e.pitches) out.push_back ({ p, e.start, len, e.velocity });
        }
        return out;
    }
}
