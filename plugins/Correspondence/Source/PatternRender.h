#pragma once

// Plays a pattern-library family over the progression (included by ChordEngine.h, where
// Settings, buildChord and voiceProgression are defined). See PatternLibrary.h for the notation.

#include "PatternLibrary.h"

namespace chords::lib
{
    using patterns::Part;

    inline Part libraryPart (SongPart part)
    {
        switch (part)
        {
            case SongPart::Bass: return Part::Bass;
            case SongPart::Arp: return Part::Arp;
            case SongPart::Chords: return Part::Chords;
            case SongPart::Counter: return Part::Counter;
            case SongPart::Piano: return Part::Piano;
            case SongPart::Strings: return Part::Strings;
            case SongPart::Brass: return Part::Brass;
            case SongPart::Choir: return Part::Choir;
            case SongPart::Timpani: return Part::Timpani;
            case SongPart::Acid: return Part::Acid;
            case SongPart::Chops: return Part::Chops;
            case SongPart::Candy: return Part::Candy;
            case SongPart::Lead: case SongPart::Harmony: default: return Part::Lead;
        }
    }

    /** The parts the library adds to the song (each its own track). */
    inline bool isLayerPart (SongPart part) { return (int) part >= (int) SongPart::Piano; }

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
            default:
                return 0;
        }
    }

    /** Where the library's layers play (kind, or -1 for silence). Piano and orchestra are layers
        in any style: breakdowns, intros and the final drop; the Orchestral style brings them into
        every drop. The electronic specifics play where the style uses them (see `styleGated`). */
    inline int placement (SongPart part, suite::SectionType type, bool laterDrop, bool lastDrop, patterns::Style style)
    {
        using T = suite::SectionType;
        const auto orchestral = style == patterns::Style::Orchestral;
        const auto drop = type == T::Drop;
        switch (part)
        {
            case SongPart::Piano:
                if (type == T::Breakdown) return 0;                       // the ballad
                if (type == T::Intro || type == T::Outro) return 2;       // broken chords
                if (drop && (lastDrop || orchestral)) return 1;           // comping under the last drop
                if (type == T::Groove && orchestral) return 2;
                return -1;
            case SongPart::Strings:
                if (type == T::Breakdown || type == T::Outro) return 0;   // legato
                if (type == T::Build) return 1;                           // an ostinato that climbs
                if (drop && (laterDrop || orchestral)) return 1;
                return -1;
            case SongPart::Brass:
                if (type == T::Breakdown && orchestral) return 0;         // swells
                if (drop && (lastDrop || orchestral)) return 1;           // stabs
                return -1;
            case SongPart::Choir:
            {
                if (type == T::Breakdown) return 0;
                const auto chanting = style == patterns::Style::Tech || style == patterns::Style::Psy || style == patterns::Style::Classic;
                if (drop && (lastDrop || orchestral)) return chanting ? 1 : 0;   // a chant in the driving styles, a pad in the rest
                return -1;
            }
            case SongPart::Timpani:
                if (type == T::Build) return 1;                           // rolls into the drop
                if (drop) return 0;                                       // hits on the phrase starts
                return -1;
            case SongPart::Acid:  return type == T::Groove || type == T::Build || drop ? 0 : -1;
            case SongPart::Chops: return type == T::Groove || drop ? 0 : -1;
            case SongPart::Candy: return type == T::Groove || type == T::Build || type == T::Breakdown || drop ? 0 : -1;
            default: return -1;
        }
    }

    /** The electronic specifics only play when the song's style has a family of its own for them. */
    inline bool styleGated (SongPart part) { return part == SongPart::Acid || part == SongPart::Chops || part == SongPart::Candy; }

    inline int registerFor (Part p, const Settings& s, int leadOctave)
    {
        switch (p)
        {
            case Part::Bass: case Part::Timpani: return s.bassOctave;
            case Part::Acid:    return s.bassOctave + 1;
            case Part::Lead:    return leadOctave;
            case Part::Chops:   return 5;
            case Part::Counter: return 4;
            case Part::Candy:   return 6;
            default:            return s.registerOctave;
        }
    }
    inline bool melodic (Part p) { return p == Part::Lead || p == Part::Chops || p == Part::Counter || p == Part::Candy; }
    inline bool chordal (Part p) { return p == Part::Chords || p == Part::Piano || p == Part::Strings || p == Part::Brass || p == Part::Choir; }
    inline bool monophonic (Part p) { return p == Part::Bass || p == Part::Lead || p == Part::Counter || p == Part::Chops || p == Part::Candy; }

    /** One voice of a family over the progression. */
    inline std::vector<Note> renderVoice (const patterns::Family& f, const patterns::Voice& main, const patterns::Voice& fill, int level, bool fillAll,
                                          const Settings& s, double lengthQ, int leadOctave)
    {
        std::vector<Note> out;
        const auto count = juce::jlimit (1, maxSlots, s.count);
        const auto L = s.quartersPerChord;
        const auto& keySteps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, s.scale)].steps;
        auto inKey = [&] (int p) { return std::find (keySteps.begin(), keySteps.end(), ((p - s.root) % 12 + 12) % 12) != keySteps.end(); };
        auto scaleStep = [&] (int p, int dir) { do p += dir; while (! inKey (p)); return p; };
        const auto mel = melodic (f.part);
        const auto shift = 12 * main.octave;
        const auto centre = 12 * (registerFor (f.part, s, leadOctave) + 1) + 4 + shift;
        std::vector<std::vector<int>> voiced;
        if (chordal (f.part)) voiced = voiceProgression (s);

        struct Ev { double start; std::vector<int> pitches; int velocity; double holdEnd; bool ghost, roll, slide; };
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
            const auto answer = mel && bar % 8 >= 6;   // hooks and lines answer themselves in bars 7-8
            const auto& src = fillAll || answer ? fill : main;
            const auto barIndex = mel ? (answer ? bar % 8 - 6 : bar % 8) : bar;
            const auto ch = src.bars[(size_t) barIndex % src.bars.size()][(size_t) stepInBar];
            if (ch == '.') { holding = false; continue; }
            if (ch == '-') { if (holding && ! evs.empty()) evs.back().holdEnd = q + 0.25; continue; }
            if (ch == '~') { if (! evs.empty()) evs.back().slide = true; continue; }

            const auto chord = buildChord (s, s.slots[(size_t) chordIdx]);
            auto intervals = chord.intervals;
            std::sort (intervals.begin(), intervals.end());
            const auto rootPc = chordRootPitchClass (s, chordIdx);
            const auto nextPc = chordRootPitchClass (s, (chordIdx + 1) % count);
            const auto base = 12 * (registerFor (f.part, s, leadOctave) + 1) + rootPc + shift;
            const auto from = prev >= 0 ? prev : (mel ? centre : base);
            auto toneOffset = [&] (char c)
            {
                switch (c)
                {
                    case 'r': case 'R': case 'z': return 0;
                    case 't': case 'T': return intervals.size() > 1 ? intervals[1] : 4;
                    case 'f': case 'F': return intervals.size() > 2 ? intervals[2] : 7;
                    default:            return intervals.size() > 3 && intervals[3] < 12 ? intervals[3] : 12;   // s / S
                }
            };
            auto nearest = [] (int pc, int to) { auto p = to - (((to - pc) % 12) + 12) % 12; if (to - p > 6) p += 12; return p; };
            auto nextRoot = [&] { return nearest (nextPc, mel ? from : base); };

            std::vector<int> pitches;
            auto velocity = stepInBar % 4 == 0 ? 112 : stepInBar % 2 == 0 ? 102 : 92;
            auto ghost = false, roll = false;
            switch (ch)
            {
                case 'r': case 't': case 'f': case 's': case 'R': case 'T': case 'F': case 'S':
                {
                    const auto up = std::isupper ((unsigned char) ch) ? 12 : 0;
                    if (mel) pitches = { nearest ((rootPc + toneOffset (ch)) % 12, from) + up };
                    else pitches = { base + toneOffset (ch) + up };
                    velocity += up > 0 ? 6 : 0;
                    break;
                }
                case 'z': pitches = { base }; roll = true; break;
                case 'l': pitches = { base - 12 >= 24 ? base - 12 : base }; break;
                case '9': pitches = { scaleStep (mel ? nearest (rootPc, from) : base, 1) }; break;
                case '4': pitches = { scaleStep (scaleStep (scaleStep (mel ? nearest (rootPc, from) : base, 1), 1), 1) }; break;
                case 'u': pitches = { scaleStep (from, 1) }; break;
                case 'd': pitches = { scaleStep (from, -1) }; break;
                case 'k': pitches = { from }; break;
                case 'n': { const auto t = nextRoot(); pitches = { t == from ? t : scaleStep (t, t > from ? -1 : 1) }; break; }
                case 'N': { const auto t = nextRoot(); pitches = { t > from ? t - 1 : t + 1 }; break; }
                case ',':
                    pitches = chordal (f.part) && ! voiced.empty() ? voiced[(size_t) chordIdx] : std::vector<int> { from };
                    if (chordal (f.part)) for (auto& p : pitches) p += shift;
                    ghost = true; velocity = 52;
                    break;
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
                    if (! voiced.empty()) for (auto& p : pitches) p += shift;
                    if (ch == 'C') pitches.push_back (*std::max_element (pitches.begin(), pitches.end()) + 12);
                    if (ch == 'o') { const auto lo = *std::min_element (pitches.begin(), pitches.end()); pitches = { lo, lo + 7 }; }
                    velocity -= 8;
                    break;
                }
                default: holding = false; continue;
            }
            for (auto& p : pitches)
            {
                if (mel) { while (p > centre + 19) p -= 12; while (p < centre - 10) p += 12; }   // singable
                p = juce::jlimit (0, 127, p);
            }
            velocity = juce::jlimit (1, 127, (int) std::lround (velocity * levelVel[(size_t) juce::jlimit (0, 2, level)]));
            evs.push_back ({ q, pitches, velocity, -1.0, ghost, roll, false });
            prev = pitches.front();
            holding = ! ghost;
        }

        // Lengths: held notes to the end of their hold, slides into the next note, rolls as
        // 32nds rising in velocity, the rest a share (the gate) of the gap to the next note
        for (size_t i = 0; i < evs.size(); ++i)
        {
            const auto& e = evs[i];
            const auto next = i + 1 < evs.size() ? evs[i + 1].start : lengthQ;
            if (e.roll)
            {
                const auto end = e.holdEnd > 0.0 ? e.holdEnd : e.start + 0.25;
                const auto steps = juce::jmax (1, (int) std::lround ((end - e.start) * 8.0));
                for (int k = 0; k < steps; ++k)
                    out.push_back ({ e.pitches.front(), e.start + k * 0.125, 0.11,
                                     juce::jlimit (1, 127, 60 + (int) std::lround ((e.velocity - 50) * (double) k / juce::jmax (1, steps - 1))) });
                continue;
            }
            auto len = e.holdEnd > 0.0 ? e.holdEnd - e.start - 0.02 : std::min (4.0, (next - e.start) * (double) f.gate);
            if (e.ghost) len = std::min (len, 0.12);
            len = juce::jlimit (0.05, juce::jmax (0.05, next - e.start - 0.01), len);
            if (e.slide && i + 1 < evs.size()) len = next - e.start + 0.03;   // overlaps the next note: a glide on a 303-style synth
            for (auto p : e.pitches) out.push_back ({ p, e.start, len, e.velocity });
        }
        return out;
    }

    /** `lengthQ` quarters of the family at `level` (0-2) over the progression, from its first chord.
        `fillAll`: every bar plays the fill (the phrase-end bar). Melodic roles repeat their cell and
        answer it with the fill in the last two bars of every eight. Voices are played together. */
    inline std::vector<Note> render (const patterns::Family& f, int level, bool fillAll, const Settings& s, double lengthQ, int leadOctave = 5)
    {
        const auto mainVoices = patterns::voices (f.level[(size_t) juce::jlimit (0, 2, level)]);
        const auto fillVoices = patterns::voices (f.fill.empty() ? f.level[2] : f.fill);
        std::vector<Note> out;
        for (size_t v = 0; v < mainVoices.size(); ++v)
        {
            const auto& fillVoice = v < fillVoices.size() ? fillVoices[v] : mainVoices[v];
            auto notes = renderVoice (f, mainVoices[v], fillVoice, level, fillAll, s, lengthQ, leadOctave);
            out.insert (out.end(), notes.begin(), notes.end());
        }
        std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return out;
    }
}
