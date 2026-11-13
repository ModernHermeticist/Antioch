#pragma once

// Writing: how the song's parts are shaped beyond the library's cells. Included by ChordEngine.h
// after PatternRender.h (Settings, Note, buildChord and the library are defined by then).
//
// Melody:      an 8-bar phrase arc (question, varied answer, climb, cadence), a climax kept for the
//              drops, motif development per section (augment, fragment, invert, sequence),
//              suspensions and leading tones, singability, displacement, density arc, the hook's
//              signature interval, ornaments.
// Rhythm:      per-role micro-timing and style grooves, velocity contours, articulation by energy,
//              ghost notes by energy, a breath before the drop, triplet turns in fills.
// Harmony:     chord-aware colours, passing chords in fills, pedal points, modal mixture in
//              breakdowns, faster chords into the turnaround, the key change prepared, bass on the
//              third on a descending walk, open/close voicings with the hook on top.
// Parts:       a register for every part, counterpoint for the counter-line, doublings, answers
//              in the hook's rests, the push budget.

#include "PatternLibrary.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace chords::writing
{
    /** Which groups of writing are on (Correspondence's Writing menu). */
    struct Options
    {
        bool phrase = false;      // 1, 2, 6, 26-29: phrase arc, climax, singability, displacement, cadences, density, signature interval
        bool develop = false;     // 3, 46, 48, 50: development per section, foreshadowing, the hook in the bass, section signatures
        bool harmony = false;     // 4, 5, 12, 13, 15, 37-41: tension notes, leading tones, colours, passing chords, walk, pedals, mixture, acceleration, key prep, voicings
        bool feel = false;        // 7-9, 33, 34, 36: micro-timing, grooves, velocity contours, articulation, ghosts by energy, triplet turns
        bool parts = false;       // 10, 11, 14, 17, 42-44: interlock, push budget, voice leading, answers, registers, counterpoint, doublings
        bool arrange = false;     // 16, 18-22, 35, 45, 47, 49: energy curves, entries, transition fills, kit variants, shared motifs, breakdown voice, breath, tutti, DJ intro, vocal answers
        bool ornaments = false;   // 30, 31: grace notes, trills, mordents; doubling only where the range allows
        bool learned = false;     // 24, 25: your edits and favourites
        bool any() const { return phrase || develop || harmony || feel || parts || arrange || ornaments || learned; }
        static Options all() { Options o; o.phrase = o.develop = o.harmony = o.feel = o.parts = o.arrange = o.ornaments = o.learned = true; return o; }
    };

    //==============================================================================
    // Helpers
    inline uint32_t mixSeed (uint32_t a, uint32_t b) { return patterns::hash (a * 2654435761u ^ (b + 0x9e3779b9u)); }
    inline bool inScale (const Settings& s, int pitch)
    {
        const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, s.scale)].steps;
        return std::find (steps.begin(), steps.end(), ((pitch - s.root) % 12 + 12) % 12) != steps.end();
    }
    inline int scaleMove (const Settings& s, int pitch, int steps)
    {
        const auto dir = steps >= 0 ? 1 : -1;
        for (int k = 0; k < std::abs (steps); ++k) { do pitch += dir; while (! inScale (s, pitch)); }
        return pitch;
    }
    inline int snapToScale (const Settings& s, int pitch) { for (int d = 0; d < 3; ++d) { if (inScale (s, pitch + d)) return pitch + d; if (inScale (s, pitch - d)) return pitch - d; } return pitch; }
    /** The chord slot playing at `q` quarters into a progression cycle. */
    inline int slotAt (const Settings& s, double q)
    {
        const auto n = juce::jlimit (1, maxSlots, s.count);
        return ((int) std::floor (q / s.quartersPerChord + 1.0e-9) % n + n) % n;
    }
    /** Pitch classes of the chord at a slot. */
    inline std::vector<int> chordPcs (const Settings& s, int slot)
    {
        const auto c = buildChord (s, s.slots[(size_t) slot]);
        std::vector<int> out;
        for (auto iv : c.intervals) out.push_back (((s.root + c.root + iv) % 12 + 12) % 12);
        return out;
    }
    inline bool inChord (const std::vector<int>& pcs, int pitch) { return std::find (pcs.begin(), pcs.end(), (pitch % 12 + 12) % 12) != pcs.end(); }
    /** The nearest pitch to `pitch` whose class is in `pcs` (ties go up when `up`). */
    inline int nearestTone (const std::vector<int>& pcs, int pitch, bool up = true)
    {
        for (int d = 0; d < 12; ++d)
        {
            if (up) { if (inChord (pcs, pitch + d)) return pitch + d; if (inChord (pcs, pitch - d)) return pitch - d; }
            else { if (inChord (pcs, pitch - d)) return pitch - d; if (inChord (pcs, pitch + d)) return pitch + d; }
        }
        return pitch;
    }
    inline void sortByStart (std::vector<Note>& v) { std::stable_sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.start < b.start; }); }
    inline int maxPitch (const std::vector<Note>& v) { int m = 0; for (const auto& n : v) m = std::max (m, n.pitch); return m; }
    inline int medianPitch (std::vector<Note> v)
    {
        if (v.empty()) return 0;
        std::sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.pitch < b.pitch; });
        return v[v.size() / 2].pitch;
    }
    /** The monophonic line's notes sounding at `q` (empty if none). */
    inline const Note* soundingAt (const std::vector<Note>& line, double q)
    {
        const Note* best = nullptr;
        for (const auto& n : line) if (n.start <= q + 1.0e-6 && n.start + n.length > q + 1.0e-6) best = &n;
        return best;
    }

    //==============================================================================
    // A. Melody

    /** 1, 26, 27, 28, 29: one cycle of a hook (`cycleQ` long, made of 8-bar phrases) shaped as a
        phrase: bars 1-2 the question, 3-4 a varied repeat (a new ending, or displaced by an 8th),
        5-6 a climb (up a scale step, the signature interval at its start), 7-8 the answer (left as
        written). Half-phrase ends sit on an open note (the 5th or 2nd), phrase ends land home.
        Busy hooks thin out at the start of the phrase and fill in toward its peak. */
    inline std::vector<Note> phraseArc (std::vector<Note> hook, const Settings& s, double cycleQ, uint32_t seed)
    {
        if (hook.size() < 3) return hook;
        sortByStart (hook);
        // the signature: the hook's widest interval in its first two bars
        int signature = 0;
        for (size_t i = 1; i < hook.size() && hook[i].start < 8.0; ++i) signature = std::max (signature, std::abs (hook[i].pitch - hook[i - 1].pitch));
        std::vector<Note> out;
        for (double p = 0.0; p < cycleQ - 1.0e-9; p += 32.0)
        {
            const auto variant = mixSeed (seed, (uint32_t) (p / 32.0)) % 2u;   // a new ending, or displaced
            std::vector<Note> phrase;
            for (auto n : hook) if (n.start >= p - 1.0e-9 && n.start < p + 32.0 - 1.0e-9) phrase.push_back (n);
            if (phrase.empty()) continue;
            // density arc: 16th movement in bar 1 thins to its 8ths when the hook is busy
            int barOne = 0;
            for (const auto& n : phrase) if (n.start < p + 4.0) ++barOne;
            for (auto& n : phrase)
            {
                const auto local = n.start - p;
                const auto bar = (int) std::floor (local / 4.0 + 1.0e-9);
                if (bar == 0 && barOne >= 8 && std::fmod (local * 4.0, 2.0) > 0.5) { n.velocity = -1; continue; }   // drop the off-16ths
                if (bar == 2 || bar == 3)
                {
                    if (variant == 1) n.start += 0.5;   // 26: displaced by an 8th
                }
                if (bar == 4 || bar == 5) n.pitch = scaleMove (s, n.pitch, 1);   // 1: the climb
            }
            phrase.erase (std::remove_if (phrase.begin(), phrase.end(), [] (const Note& n) { return n.velocity < 0; }), phrase.end());
            // 29: the signature interval opens the climb (if the climb doesn't have one that wide)
            if (signature >= 5)
            {
                Note* first = nullptr; const Note* before = nullptr;
                for (auto& n : phrase) { if (n.start >= p + 16.0 - 1.0e-6 && first == nullptr) first = &n; }
                for (const auto& n : phrase) if (n.start < p + 16.0 - 1.0e-6) before = &n;
                if (first != nullptr && before != nullptr && std::abs (first->pitch - before->pitch) < signature - 1)
                {
                    const auto pcs = chordPcs (s, slotAt (s, first->start));
                    const auto target = nearestTone (pcs, before->pitch + signature);
                    if (std::abs (target - before->pitch) <= 12) first->pitch = target;
                }
            }
            // 1 (variant 0): the repeat's last note changes (a step up, then home)
            if (variant == 0)
            {
                Note* last = nullptr;
                for (auto& n : phrase) if (n.start >= p + 8.0 - 1.0e-6 && n.start < p + 16.0 - 1.0e-6) last = &n;
                if (last != nullptr) last->pitch = nearestTone (chordPcs (s, slotAt (s, last->start)), scaleMove (s, last->pitch, 1));
            }
            // 27: cadences: the half-phrase ends open, the phrase ends home
            auto lastIn = [&] (double from, double to) { Note* r = nullptr; for (auto& n : phrase) if (n.start >= from - 1.0e-6 && n.start < to - 1.0e-6) r = &n; return r; };
            if (auto* half = lastIn (p + 8.0, p + 16.0); half != nullptr)
            {
                const auto rootPc = (s.root % 12 + 12) % 12;
                const int open[] = { (rootPc + 7) % 12, (rootPc + 2) % 12 };
                int best = half->pitch, dist = 99;
                for (auto pc : open) for (int d = -6; d <= 6; ++d) if (((half->pitch + d) % 12 + 12) % 12 == pc && std::abs (d) < dist) { dist = std::abs (d); best = half->pitch + d; }
                half->pitch = best;
            }
            if (auto* end = lastIn (p + 24.0, p + 32.0); end != nullptr && end->length >= 0.5)
            {
                const auto rootPc = (s.root % 12 + 12) % 12;
                for (int d = 0; d <= 6; ++d)
                {
                    if (((end->pitch - d) % 12 + 12) % 12 == rootPc) { end->pitch -= d; break; }
                    if (((end->pitch + d) % 12 + 12) % 12 == rootPc) { end->pitch += d; break; }
                }
            }
            for (auto& n : phrase) if (n.start < p + 32.0 - 1.0e-9) { n.length = std::min (n.length, p + 32.0 - n.start - 0.01); out.push_back (n); }
        }
        sortByStart (out);
        return out;
    }

    /** 6: singable: no leap wider than a 10th (folded by an octave), a leap of a 6th or more
        followed by a step back the other way where the next note was going on in the same
        direction, and the whole line inside an octave and a half of its median. */
    inline std::vector<Note> singable (std::vector<Note> line, const Settings& s)
    {
        sortByStart (line);
        const auto mid = medianPitch (line);
        for (auto& n : line) { while (n.pitch > mid + 12) n.pitch -= 12; while (n.pitch < mid - 9) n.pitch += 12; }
        for (size_t i = 1; i < line.size(); ++i)
        {
            auto leap = line[i].pitch - line[i - 1].pitch;
            if (std::abs (leap) > 15) { line[i].pitch -= 12 * (leap > 0 ? 1 : -1); leap = line[i].pitch - line[i - 1].pitch; }
            if (i + 1 < line.size() && std::abs (leap) >= 8)
            {
                const auto next = line[i + 1].pitch - line[i].pitch;
                if ((next > 0) == (leap > 0) && std::abs (next) >= 3)
                    line[i + 1].pitch = scaleMove (s, line[i].pitch, leap > 0 ? -1 : 1);   // recover by a step
            }
        }
        return line;
    }
    /** A line's singability, 0-1 (used by the library's quality score and tests). */
    inline float singability (const std::vector<Note>& line)
    {
        if (line.size() < 2) return 1.0f;
        int lo = 127, hi = 0, bad = 0;
        for (size_t i = 0; i < line.size(); ++i)
        {
            lo = std::min (lo, line[i].pitch); hi = std::max (hi, line[i].pitch);
            if (i > 0 && std::abs (line[i].pitch - line[i - 1].pitch) > 12) ++bad;
            if (i > 1)
            {
                const auto a = line[i - 1].pitch - line[i - 2].pitch, b = line[i].pitch - line[i - 1].pitch;
                if (std::abs (a) >= 8 && (a > 0) == (b > 0) && std::abs (b) >= 3) ++bad;
            }
        }
        const auto range = hi - lo;
        return juce::jlimit (0.0f, 1.0f, 1.0f - 0.15f * (float) bad / (float) line.size() * 10.0f - (range > 19 ? 0.04f * (float) (range - 19) : 0.0f));
    }

    /** 2: a section's hook against the song's climax: breakdowns stay under it, the final drop
        goes past it once per phrase (its highest note there moves to the next chord tone up). */
    inline void climax (std::vector<Note>& line, const Settings& s, int hookTop, bool breakdown, bool finalDrop, double sectionStart)
    {
        if (line.empty() || hookTop <= 0) return;
        if (breakdown)
            for (auto& n : line)
                while (n.pitch >= hookTop) n.pitch = nearestTone (chordPcs (s, slotAt (s, n.start - sectionStart)), n.pitch - 2, false);
        if (finalDrop)
        {
            std::map<int, Note*> topPerPhrase;
            for (auto& n : line)
            {
                const auto phrase = (int) std::floor ((n.start - sectionStart) / 32.0 + 1.0e-9);
                auto& t = topPerPhrase[phrase];
                if (t == nullptr || n.pitch > t->pitch) t = &n;
            }
            for (auto& [phrase, n] : topPerPhrase)
            {
                juce::ignoreUnused (phrase);
                if (n->pitch >= hookTop - 1) n->pitch = nearestTone (chordPcs (s, slotAt (s, n->start - sectionStart)), n->pitch + 2);
            }
        }
    }

    /** 3: the hook developed: 1 augmented (twice as slow), 2 fragmented (its first beat, repeated
        and climbing), 3 inverted (mirrored around its first note, kept in key), 4 sequenced (up a
        step every two bars). */
    enum Development { devNone, devAugment, devFragment, devInvert, devSequence };
    inline std::vector<Note> develop (const std::vector<Note>& hook, int how, const Settings& s, double cycleQ)
    {
        if (hook.empty() || how == devNone) return hook;
        std::vector<Note> out;
        auto line = hook; sortByStart (line);
        switch (how)
        {
            case devAugment:
                for (auto n : line) { n.start *= 2.0; n.length *= 2.0; if (n.start < cycleQ - 1.0e-9) out.push_back (n); }
                break;
            case devFragment:
            {
                std::vector<Note> cell;
                for (const auto& n : line) if (n.start < 2.0 - 1.0e-9) cell.push_back (n);
                if (cell.empty()) cell.push_back (line.front());
                int step = 0;
                for (double q = 0.0; q < cycleQ - 1.0e-9; q += 2.0, ++step)
                    for (auto n : cell)
                    {
                        n.start += q; n.length = std::min (n.length, 1.9);
                        n.pitch = scaleMove (s, n.pitch, (step / 2) % 4);   // climbing every bar
                        if (n.start < cycleQ - 1.0e-9) out.push_back (n);
                    }
                break;
            }
            case devInvert:
            {
                const auto axis = line.front().pitch;
                for (auto n : line) { n.pitch = snapToScale (s, 2 * axis - n.pitch); out.push_back (n); }
                break;
            }
            case devSequence:
                for (auto n : line)
                {
                    const auto steps = (int) std::floor (n.start / 8.0 + 1.0e-9) % 4;
                    n.pitch = scaleMove (s, n.pitch, steps);
                    out.push_back (n);
                }
                break;
            default: return hook;
        }
        return out;
    }

    /** 46: foreshadowing: only the first three notes of each two bars. */
    inline std::vector<Note> foreshadow (const std::vector<Note>& hook)
    {
        std::vector<Note> out;
        std::map<int, int> count;
        for (const auto& n : hook)
        {
            const auto cell = (int) std::floor (n.start / 8.0 + 1.0e-9);
            if (count[cell]++ < 3) out.push_back (n);
        }
        return out;
    }

    /** 4, 5: tension: a long note landing on a chord change on the chord's third starts on the
        fourth for an 8th (a suspension) one time in three; a note arriving at a chord change after
        an 8th of rest gets a 16th leading tone from below, one time in three. */
    inline std::vector<Note> tension (std::vector<Note> line, const Settings& s, double sectionStart, uint32_t seed)
    {
        sortByStart (line);
        const auto L = s.quartersPerChord;
        std::vector<Note> added;
        for (size_t i = 0; i < line.size(); ++i)
        {
            auto& n = line[i];
            const auto local = n.start - sectionStart;
            const auto onChange = std::abs (local / L - std::round (local / L)) < 1.0e-6;
            if (! onChange) continue;
            const auto pcs = chordPcs (s, slotAt (s, local));
            const auto r = mixSeed (seed, (uint32_t) std::lround (n.start * 4.0)) % 3u;
            if (r == 0 && n.length >= 1.0 && pcs.size() >= 2 && (n.pitch % 12 + 12) % 12 == pcs[1])
            {
                Note sus = n; sus.length = 0.5; sus.pitch = scaleMove (s, n.pitch, 1);
                n.start += 0.5; n.length -= 0.5;
                added.push_back (sus);
            }
            else if (r == 1 && n.start >= 0.25)
            {
                const auto prevEnd = i > 0 ? line[i - 1].start + line[i - 1].length : -1.0;
                if (prevEnd <= n.start - 0.5 + 1.0e-6)
                    added.push_back ({ scaleMove (s, n.pitch, -1), n.start - 0.25, 0.22, std::max (1, n.velocity - 18) });
            }
        }
        line.insert (line.end(), added.begin(), added.end());
        sortByStart (line);
        return line;
    }

    /** 36: a triplet turn: the fill bar's last beat as three 8th-note triplets of its notes. */
    inline void tripletTurn (std::vector<Note>& line, double barStart)
    {
        const auto beat = barStart + 3.0;
        std::vector<int> pitches;
        int vel = 96;
        for (const auto& n : line) if (n.start >= beat - 1.0e-6 && n.start < beat + 1.0 - 1.0e-6) { pitches.push_back (n.pitch); vel = n.velocity; }
        if (pitches.empty()) return;
        line.erase (std::remove_if (line.begin(), line.end(), [beat] (const Note& n) { return n.start >= beat - 1.0e-6 && n.start < beat + 1.0 - 1.0e-6; }), line.end());
        for (int k = 0; k < 3; ++k) line.push_back ({ pitches[(size_t) k % pitches.size()], beat + k / 3.0, 0.3, vel - 4 * k });
    }

    /** 30: ornaments: a grace note (a 32nd from a step above) before some long notes, a mordent
        on the phrase's top note, a trill on the fill's last long note. */
    inline std::vector<Note> ornament (std::vector<Note> line, const Settings& s, uint32_t seed, bool trill)
    {
        sortByStart (line);
        std::vector<Note> out;
        const auto top = maxPitch (line);
        bool mordentDone = false;
        for (size_t i = 0; i < line.size(); ++i)
        {
            auto n = line[i];
            const auto prevEnd = i > 0 ? line[i - 1].start + line[i - 1].length : -1.0;
            const auto room = n.start - prevEnd >= 0.12;
            if (n.length >= 1.0 && room && n.start >= 0.125 && mixSeed (seed, (uint32_t) i) % 4u == 0u)
                out.push_back ({ scaleMove (s, n.pitch, 1), n.start - 0.125, 0.11, std::max (1, n.velocity - 20) });
            if (! mordentDone && n.pitch == top && n.length >= 0.75)
            {
                mordentDone = true;
                out.push_back ({ n.pitch, n.start, 0.12, n.velocity });
                out.push_back ({ scaleMove (s, n.pitch, 1), n.start + 0.125, 0.12, n.velocity - 10 });
                n.start += 0.25; n.length -= 0.25;
            }
            if (trill && i + 1 == line.size() && n.length >= 1.0)
            {
                const auto upper = scaleMove (s, n.pitch, 1);
                const auto half = n.length * 0.5;
                for (double q = 0.0; q < half - 1.0e-6; q += 0.125)
                    out.push_back ({ (int) std::lround (q / 0.125) % 2 == 0 ? n.pitch : upper, n.start + q, 0.11, n.velocity - 6 });
                n.start += half; n.length -= half;
            }
            out.push_back (n);
        }
        sortByStart (out);
        return out;
    }

    //==============================================================================
    // B. Rhythm and feel

    /** 7, 33: micro-timing per role, on top of the style's groove: the bass sits a touch behind,
        hats-like parts (arp, candy, acid) a touch ahead in the driving styles, the lead and
        strings lay back, chords on the grid. In quarters (138 BPM: 0.01 q is ~4 ms). */
    inline std::vector<Note> microTiming (std::vector<Note> notes, patterns::Part part, patterns::Style style)
    {
        using P = patterns::Part;
        using St = patterns::Style;
        auto shift = 0.0;
        switch (part)
        {
            case P::Bass: case P::Acid: shift = 0.008; break;
            case P::Arp: case P::Candy: shift = style == St::Psy || style == St::Tech ? -0.004 : 0.0; break;
            case P::Lead: case P::Counter: case P::Chops: shift = 0.006; break;
            case P::Strings: case P::Choir: shift = 0.012; break;
            default: break;
        }
        // the style's groove: tech swings its off-16ths a little, progressive drags the offbeat 8ths
        for (auto& n : notes)
        {
            const auto inBeat = n.start - std::floor (n.start + 1.0e-9);
            const auto sixteenth = (int) std::lround (inBeat * 4.0);
            const auto onGrid = std::abs (inBeat * 4.0 - sixteenth) < 1.0e-3;
            auto groove = 0.0;
            if (onGrid && style == St::Tech && (sixteenth == 1 || sixteenth == 3)) groove = 0.03;
            if (onGrid && style == St::Progressive && sixteenth == 2) groove = 0.01;
            n.start = std::max (0.0, n.start + shift + groove);
        }
        return notes;
    }

    /** 8, 33: velocity contours: inside the bar (beats strong, 8ths medium, 16ths light, in each
        style's own accent pattern), over the phrase (a swell to bar 5, a settle at the end), and
        through builds (a crescendo). Accents written in the pattern keep their lift. */
    inline std::vector<Note> velocityContour (std::vector<Note> notes, double sectionStart, double sectionEnd, bool build, patterns::Style style)
    {
        using St = patterns::Style;
        static const int straight[16] { 10, -6, 0, -8, 4, -6, 0, -8, 8, -6, 0, -8, 4, -6, 0, -8 };
        static const int psy[16]      { 10, -10, 2, 6, 4, -10, 2, 6, 8, -10, 2, 6, 4, -10, 2, 6 };   // the rolling psy accent on the "a"
        static const int ctr332[16]   { 10, -6, -4, 8, -6, -4, 8, -6, 6, -6, -4, 8, -6, -4, 8, -6 }; // 3-3-2 accents
        const auto* shape = style == St::Psy ? psy : style == St::Uplifting || style == St::Classic ? ctr332 : straight;
        for (auto& n : notes)
        {
            const auto inBar = n.start - sectionStart - 4.0 * std::floor ((n.start - sectionStart) / 4.0 + 1.0e-9);
            const auto k = juce::jlimit (0, 15, (int) std::lround (inBar * 4.0) % 16);
            const auto phrasePos = std::fmod ((n.start - sectionStart) / 32.0, 1.0);
            const auto swell = (int) std::lround (6.0 * std::sin (juce::MathConstants<double>::pi * std::min (1.0, phrasePos * 1.6)));
            const auto cresc = build ? (int) std::lround (16.0 * ((n.start - sectionStart) / std::max (1.0, sectionEnd - sectionStart)) - 8.0) : 0;
            n.velocity = juce::jlimit (1, 127, n.velocity + shape[k] / 2 + swell + cresc);
        }
        return notes;
    }

    /** 9: articulation by energy: short and detached in quiet sections, as written at mid, a
        little longer in drops (held notes and slides are left alone). */
    inline std::vector<Note> articulate (std::vector<Note> notes, int level)
    {
        static const double factor[] { 0.72, 1.0, 1.12 };
        sortByStart (notes);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& n = notes[i];
            if (n.length >= 1.0) continue;
            double next = n.start + n.length * 2.0;
            for (size_t j = i + 1; j < notes.size(); ++j) if (notes[j].start > n.start + 1.0e-6) { next = notes[j].start; break; }
            n.length = juce::jlimit (0.05, std::max (0.05, next - n.start - 0.01), n.length * factor[juce::jlimit (0, 2, level)]);
        }
        return notes;
    }

    /** 34: how many ghost notes a section gets: none in intros and outros, some in grooves and
        breakdowns, more through builds, the most in drops. */
    inline float ghostAmount (suite::SectionType t)
    {
        using T = suite::SectionType;
        switch (t)
        {
            case T::Intro: case T::Outro: return 0.0f;
            case T::Groove: case T::Breakdown: return 0.5f;
            case T::Build: return 0.75f;
            case T::Drop: return 1.0f;
            case T::count: default: return 0.5f;
        }
    }

    /** 35: a breath before the drop: nothing starts in the last beat before a drop, and notes ring
        no further than its start (the timpani's roll keeps going). */
    inline void breathBeforeDrop (std::vector<Note>& notes, double dropStart)
    {
        notes.erase (std::remove_if (notes.begin(), notes.end(), [dropStart] (const Note& n) { return n.start >= dropStart - 1.0 - 1.0e-6 && n.start < dropStart - 1.0e-6; }), notes.end());
        for (auto& n : notes) if (n.start < dropStart - 1.0 && n.start + n.length > dropStart - 1.0) n.length = std::max (0.05, dropStart - 1.0 - n.start - 0.01);
    }

    //==============================================================================
    // C. Harmony

    /** 37: a pedal: the bass holds the key's root (in the octave nearest each note) under the
        changing chords. */
    inline void pedal (std::vector<Note>& bass, const Settings& s)
    {
        const auto rootPc = (s.root % 12 + 12) % 12;
        for (auto& n : bass)
        {
            auto p = n.pitch - (((n.pitch - rootPc) % 12) + 12) % 12;
            if (n.pitch - p > 6) p += 12;
            n.pitch = p;
        }
    }

    /** 38: modal mixture for a breakdown: in a minor key the iv becomes IV (the Dorian lift); in a
        major key the IV becomes iv (the minor plagal). */
    inline Settings modalMixture (Settings s)
    {
        const auto minorKey = s.scale == 0 || s.scale == 2 || s.scale == 3 || s.scale == 4;
        for (int k = 0; k < juce::jlimit (1, maxSlots, s.count); ++k)
            if (s.slots[(size_t) k].degree == 4 && s.slots[(size_t) k].quality == Quality::Auto)
                s.slots[(size_t) k].quality = minorKey ? Quality::Major : Quality::Minor;
        return s;
    }

    /** 12: chord colours that fit: no added 9th where it would be a minor 9th over the root
        (a b9 is the trance's sourest clash), no 7th on a diminished chord. Returns false when the
        colour should be left off this chord. */
    inline bool colourFits (const Settings& s, int slot, bool ninth)
    {
        const auto c = buildChord (s, s.slots[(size_t) slot]);
        const auto d = juce::jlimit (1, 7, s.slots[(size_t) slot].degree) - 1;
        if (ninth)
        {
            const auto nine = suite::music::scaleStep (s.scale, d + 1) - c.root;
            return ((nine % 12) + 12) % 12 != 1;
        }
        const auto dim = c.intervals.size() >= 3 && c.intervals[1] == 3 && c.intervals[2] == 6;
        return ! dim;
    }

    /** 13: a passing chord in the fill's last two beats: the next chord's dominant (a major chord
        a fifth above it). Chord parts move to its nearest tones; the bass to its root. */
    inline void passingChord (std::vector<Note>& notes, const Settings& s, double barStart, double nextChordQ, bool bass)
    {
        const auto nextSlot = slotAt (s, nextChordQ);
        const auto nextRoot = chordRootPitchClass (s, nextSlot);
        const auto domRoot = (nextRoot + 7) % 12;
        const std::vector<int> dom { domRoot, (domRoot + 4) % 12, (domRoot + 7) % 12 };
        for (auto& n : notes)
        {
            if (n.start < barStart + 2.0 - 1.0e-6 || n.start >= barStart + 4.0 - 1.0e-6) continue;
            if (bass) { auto p = n.pitch - (((n.pitch - domRoot) % 12) + 12) % 12; if (n.pitch - p > 6) p += 12; n.pitch = p; }
            else n.pitch = nearestTone (dom, n.pitch);
        }
    }

    /** 15: a walking bass on the chord's third where that makes a step down from the last chord's
        bass (i - v6 - VI: the classic descending line). */
    inline void walkOnThirds (std::vector<Note>& bass, const Settings& s, double sectionStart)
    {
        sortByStart (bass);
        int prevPitch = -1;
        int prevSlot = -1;
        for (auto& n : bass)
        {
            const auto slot = slotAt (s, n.start - sectionStart);
            if (slot != prevSlot && prevPitch > 0)
            {
                const auto pcs = chordPcs (s, slot);
                if (pcs.size() >= 2)
                {
                    const auto third = nearestTone ({ pcs[1] }, n.pitch);
                    const auto rootDist = std::abs (n.pitch - prevPitch), thirdDist = std::abs (third - prevPitch);
                    if (thirdDist >= 1 && thirdDist <= 2 && rootDist > 2 && third < prevPitch)
                    {
                        const auto delta = third - n.pitch;
                        for (auto& m : bass) if (slotAt (s, m.start - sectionStart) == slot && std::abs (m.start - n.start) < s.quartersPerChord && m.start >= n.start - 1.0e-6) m.pitch += delta;
                    }
                }
            }
            if (slot != prevSlot) prevSlot = slot;
            prevPitch = n.pitch;
        }
    }

    /** 39: the turnaround: the 7th bar of each phrase in a drop runs the chords twice as fast
        (taken from `fast`, the same part rendered with half-length chords). */
    inline void accelerate (std::vector<Note>& notes, const std::vector<Note>& fast, double sectionStart, double sectionEnd)
    {
        auto inTurn = [&] (double q) { const auto bar = (int) std::floor ((q - sectionStart) / 4.0 + 1.0e-9); return bar % 8 == 6 && q < sectionEnd - 8.0; };
        notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n) { return inTurn (n.start); }), notes.end());
        for (const auto& n : fast) if (inTurn (n.start)) notes.push_back (n);
        sortByStart (notes);
    }

    /** 40: the key change prepared: the build's last bar moves onto the new key's V (a major chord:
        every note to its nearest tone; the bass to its root). */
    inline void prepareKeyChange (std::vector<Note>& notes, int newRootPc, double lastBarStart, bool bass)
    {
        const auto v = (newRootPc + 7) % 12;
        const std::vector<int> triad { v, (v + 4) % 12, (v + 7) % 12 };
        for (auto& n : notes)
        {
            if (n.start < lastBarStart - 1.0e-6) continue;
            if (bass) { auto p = n.pitch - (((n.pitch - v) % 12) + 12) % 12; if (n.pitch - p > 6) p += 12; n.pitch = p; }
            else n.pitch = nearestTone (triad, n.pitch);
        }
    }

    /** 41: voicings by energy: open (the second note an octave up) in breakdowns, close (inside an
        octave and a half) in drops; and the hook's note on top where it's in the chord. */
    inline void voiceByEnergy (std::vector<Note>& chordNotes, bool open, const std::vector<Note>* hook)
    {
        std::map<long long, std::vector<size_t>> groups;
        for (size_t i = 0; i < chordNotes.size(); ++i) groups[(long long) std::llround (chordNotes[i].start * 960.0)].push_back (i);
        for (auto& [t, idx] : groups)
        {
            if (idx.size() < 3) continue;
            std::sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return chordNotes[a].pitch < chordNotes[b].pitch; });
            if (open) chordNotes[idx[1]].pitch += 12;
            else
            {
                const auto low = chordNotes[idx[0]].pitch;
                for (auto i : idx) while (chordNotes[i].pitch > low + 16) chordNotes[i].pitch -= 12;
            }
            if (hook != nullptr)
                if (const auto* h = soundingAt (*hook, (double) t / 960.0))
                {
                    const auto hpc = (h->pitch % 12 + 12) % 12;
                    for (int rot = 0; rot < 3; ++rot)
                    {
                        std::sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return chordNotes[a].pitch < chordNotes[b].pitch; });
                        auto& top = chordNotes[idx.back()];
                        if ((top.pitch % 12 + 12) % 12 == hpc) break;
                        if (std::none_of (idx.begin(), idx.end(), [&] (size_t i) { return (chordNotes[i].pitch % 12 + 12) % 12 == hpc; })) break;
                        chordNotes[idx.front()].pitch += 12;   // rotate up until the hook's note is on top
                    }
                }
        }
    }

    //==============================================================================
    // D/J. Parts together

    /** 42: every part's register (MIDI notes): a section's line moves by octaves to sit around the
        middle of its band, then single notes fold in. */
    inline std::pair<int, int> band (patterns::Part p)
    {
        using P = patterns::Part;
        switch (p)
        {
            case P::Bass: case P::Timpani: return { 28, 55 };
            case P::Acid:    return { 36, 64 };
            case P::Chords: case P::Piano: case P::Strings: case P::Brass: case P::Choir: return { 45, 84 };
            case P::Arp:     return { 52, 88 };
            case P::Lead:    return { 57, 91 };
            case P::Counter: return { 50, 79 };
            case P::Chops:   return { 57, 86 };
            case P::Candy:   return { 69, 100 };
            case P::count: default: return { 24, 108 };
        }
    }
    inline void keepInBand (std::vector<Note>& notes, patterns::Part p, bool shiftWhole = true)
    {
        if (notes.empty()) return;
        const auto [lo, hi] = band (p);
        const auto mid = (lo + hi) / 2;
        auto med = medianPitch (notes);
        int shift = 0;
        while (shiftWhole && med + shift > hi - 6) shift -= 12;
        while (shiftWhole && med + shift < lo + 6) shift += 12;
        for (auto& n : notes)
        {
            n.pitch += shift;
            while (n.pitch > hi) n.pitch -= 12;
            while (n.pitch < lo) n.pitch += 12;
        }
        juce::ignoreUnused (mid);
    }

    /** 43, 14: counterpoint against the hook: on beats 1 and 3 the counter sits a third or sixth
        from the hook (moving to the nearest chord tone that does), never above it, and where both
        move it prefers contrary motion; no parallel fifths or octaves between them. */
    inline std::vector<Note> counterpoint (std::vector<Note> counter, const std::vector<Note>& hook, const Settings& s, double sectionStart)
    {
        sortByStart (counter);
        int prevC = -1, prevH = -1;
        for (auto& n : counter)
        {
            const auto* h = soundingAt (hook, n.start);
            if (h == nullptr) { prevC = n.pitch; continue; }
            while (n.pitch >= h->pitch) n.pitch -= 12;   // under the hook
            const auto pcs = chordPcs (s, slotAt (s, n.start - sectionStart));
            const auto strong = std::fmod (n.start, 2.0) < 1.0e-6;
            auto good = [&] (int p)
            {
                const auto iv = ((h->pitch - p) % 12 + 12) % 12;
                if (strong && ! (iv == 3 || iv == 4 || iv == 8 || iv == 9)) return false;
                if (prevC >= 0 && prevH >= 0)
                {
                    const auto was = ((prevH - prevC) % 12 + 12) % 12;
                    if ((was == 0 || was == 7) && was == iv && p != prevC) return false;   // parallels
                }
                return true;
            };
            if (! good (n.pitch))
            {
                int best = n.pitch, bestScore = 1000;
                for (int d = -7; d <= 7; ++d)
                {
                    const auto p = n.pitch + d;
                    if (p >= h->pitch || ! inChord (pcs, p) || ! good (p)) continue;
                    // contrary motion where the hook moves
                    const auto hookDir = prevH >= 0 ? h->pitch - prevH : 0, myDir = prevC >= 0 ? p - prevC : 0;
                    const auto score = std::abs (d) * 2 + ((hookDir != 0 && myDir != 0 && (hookDir > 0) == (myDir > 0)) ? 3 : 0);
                    if (score < bestScore) { bestScore = score; best = p; }
                }
                n.pitch = best;
            }
            prevC = n.pitch; prevH = h->pitch;
        }
        return counter;
    }

    /** 17: answers in the hook's rests: notes of an answering part (counter, candy, chops, brass)
        that start while the hook holds a long note or moves stay soft, and those in the hook's
        rests of a beat or more come forward. */
    inline void answerInRests (std::vector<Note>& part, const std::vector<Note>& hook)
    {
        for (auto& n : part)
        {
            const auto* h = soundingAt (hook, n.start);
            n.velocity = juce::jlimit (1, 127, n.velocity + (h == nullptr ? 8 : -10));
        }
    }

    /** 44: strings doubling the hook a diatonic third below (the final drop). */
    inline std::vector<Note> thirdsBelow (const std::vector<Note>& hook, const Settings& s)
    {
        std::vector<Note> out;
        for (auto n : hook) { n.pitch = scaleMove (s, n.pitch, -2); n.velocity = std::max (1, n.velocity - 14); out.push_back (n); }
        return out;
    }
    /** 44: brass stabs doubling the bass's rhythm on the first beat of every other bar. */
    inline std::vector<Note> brassOnBassHits (const std::vector<Note>& bass, const Settings& s, double sectionStart, double sectionEnd)
    {
        std::vector<Note> out;
        for (const auto& b : bass)
        {
            const auto local = b.start - sectionStart;
            const auto bar = (int) std::floor (local / 4.0 + 1.0e-9);
            if (bar % 2 != 0 || local - bar * 4.0 > 1.0 - 1.0e-6 || b.start >= sectionEnd) continue;
            const auto pcs = chordPcs (s, slotAt (s, local));
            const auto root = 60 + pcs[0] - (pcs[0] > 7 ? 12 : 0);
            for (auto pc : pcs) out.push_back ({ nearestTone ({ pc }, root + 2), b.start, 0.35, 104 });
        }
        return out;
    }

    /** 48: the hook in the bass: the hook's notes in a bar, moved down into the bass's register. */
    inline std::vector<Note> hookInBass (const std::vector<Note>& hook, double barStart)
    {
        std::vector<Note> out;
        for (auto n : hook)
            if (n.start >= barStart - 1.0e-6 && n.start < barStart + 4.0 - 1.0e-6)
            {
                while (n.pitch > 52) n.pitch -= 12;
                n.length = std::min (n.length, 0.9);
                out.push_back (n);
            }
        return out;
    }

    /** 21: the arp made from the hook: the arp keeps its rhythm, and its notes walk the hook's
        pitch classes in order (each moved to the nearest of them that's in the chord there). */
    inline std::vector<Note> arpFromHook (std::vector<Note> arp, const std::vector<Note>& hook, const Settings& s, double sectionStart)
    {
        std::vector<int> pcs;
        for (const auto& h : hook) { const auto pc = (h.pitch % 12 + 12) % 12; if (pcs.empty() || pcs.back() != pc) pcs.push_back (pc); if (pcs.size() >= 8) break; }
        if (pcs.size() < 2) return arp;
        sortByStart (arp);
        size_t k = 0;
        double lastStart = -1.0;
        for (auto& n : arp)
        {
            if (std::abs (n.start - lastStart) < 1.0e-6) continue;   // chords in the arp: leave them
            lastStart = n.start;
            const auto chord = chordPcs (s, slotAt (s, n.start - sectionStart));
            auto pc = pcs[k++ % pcs.size()];
            if (! inChord (chord, pc)) pc = (nearestTone (chord, pc + 60) % 12 + 12) % 12;
            n.pitch = nearestTone ({ pc }, n.pitch);
        }
        return arp;
    }

    /** 50: the song's signature figure: the hook's first three notes as 8ths, in a part's register,
        at the start of a section. */
    inline std::vector<Note> signatureFigure (const std::vector<Note>& hook, double at, int registerCentre)
    {
        std::vector<Note> out;
        auto line = hook; sortByStart (line);
        for (size_t k = 0; k < line.size() && k < 3; ++k)
        {
            auto p = line[k].pitch;
            while (p > registerCentre + 7) p -= 12;
            while (p < registerCentre - 6) p += 12;
            out.push_back ({ p, at + 0.5 * (double) k, 0.45, 96 - 6 * (int) k });
        }
        return out;
    }

    /** 19: the section's last bar, by what comes next: into a build it thins to every other note,
        into a drop its last beat rolls (16ths of its last note), into a breakdown it stops after
        beat 2 (and its notes there ring out). */
    inline void transitionBar (std::vector<Note>& notes, double barStart, suite::SectionType next)
    {
        using T = suite::SectionType;
        auto inBar = [&] (const Note& n) { return n.start >= barStart - 1.0e-6 && n.start < barStart + 4.0 - 1.0e-6; };
        if (next == T::Build)
        {
            int k = 0;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n) { return inBar (n) && (k++ % 2) == 1; }), notes.end());
        }
        else if (next == T::Drop)
        {
            // beat 3 rolls in 16ths on the last note before it; beat 4 is left for the breath before the drop
            int lastPitch = -1, vel = 96;
            for (const auto& n : notes) if (inBar (n) && n.start < barStart + 2.0) { lastPitch = n.pitch; vel = n.velocity; }
            if (lastPitch < 0) return;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n) { return n.start >= barStart + 2.0 - 1.0e-6 && n.start < barStart + 3.0 - 1.0e-6; }), notes.end());
            for (auto& n : notes) if (inBar (n) && n.start < barStart + 2.0 && n.start + n.length > barStart + 2.0) n.length = barStart + 2.0 - n.start - 0.01;
            for (int k = 0; k < 4; ++k) notes.push_back ({ lastPitch, barStart + 2.0 + 0.25 * k, 0.2, juce::jlimit (1, 127, vel - 12 + 6 * k) });
        }
        else if (next == T::Breakdown)
        {
            notes.erase (std::remove_if (notes.begin(), notes.end(), [&] (const Note& n) { return n.start >= barStart + 2.0 - 1.0e-6 && n.start < barStart + 4.0 - 1.0e-6; }), notes.end());
            for (auto& n : notes) if (inBar (n)) n.length = std::max (n.length, barStart + 4.0 - n.start - 0.05);
        }
        sortByStart (notes);
    }

    //==============================================================================
    // F. Learning from you: favourites, bans and edits (a text file next to the style log)

    using patterns::learned;
    using patterns::saveLearned;
    using patterns::setFavourite;
    inline juce::String partKey (patterns::Part p) { return patterns::partFiles()[(int) p]; }

    /** 24: learns from your edit of an exported part: how many notes you kept (density), how much
        louder or softer, how far you moved it (octaves) and how long you made the notes. */
    inline juce::String learnEdit (patterns::Part part, const std::vector<Note>& original, const std::vector<Note>& edited)
    {
        if (original.empty() || edited.empty()) return "Nothing to compare.";
        auto avg = [] (const std::vector<Note>& v, auto get) { double s = 0.0; for (const auto& n : v) s += get (n); return s / (double) v.size(); };
        const auto density = (float) edited.size() / (float) original.size();
        const auto vel = (float) (avg (edited, [] (const Note& n) { return (double) n.velocity; }) - avg (original, [] (const Note& n) { return (double) n.velocity; }));
        const auto oct = (float) std::round ((avg (edited, [] (const Note& n) { return (double) n.pitch; }) - avg (original, [] (const Note& n) { return (double) n.pitch; })) / 12.0);
        const auto gate = (float) (avg (edited, [] (const Note& n) { return n.length; }) / std::max (0.01, avg (original, [] (const Note& n) { return n.length; })));
        auto& l = learned();
        const auto p = (int) part;
        auto blend = [] (std::map<int, float>& m, int key, float v, float neutral) { const auto old = m.count (key) ? m[key] : neutral; m[key] = old + 0.5f * (v - old); };
        blend (l.density, p, juce::jlimit (0.3f, 1.5f, density), 1.0f);
        blend (l.velocity, p, juce::jlimit (-30.0f, 30.0f, vel), 0.0f);
        blend (l.octave, p, juce::jlimit (-2.0f, 2.0f, oct), 0.0f);
        blend (l.gate, p, juce::jlimit (0.4f, 2.0f, gate), 1.0f);
        saveLearned();
        return partKey (part) + ": " + juce::String (juce::roundToInt (density * 100.0f)) + "% of the notes, " + (vel >= 0 ? "+" : "") + juce::String (vel, 0)
             + " velocity, " + juce::String ((int) oct) + " octave(s), notes x" + juce::String (gate, 2) + ". Later songs lean that way.";
    }

    /** 24: your edits applied to a part: thinned (the softest off-beat notes go first), louder or
        softer, moved by whole octaves, longer or shorter. */
    inline std::vector<Note> applyLearned (std::vector<Note> notes, patterns::Part part)
    {
        const auto& l = learned();
        const auto p = (int) part;
        if (! l.density.count (p)) return notes;
        const auto density = l.density.at (p);
        if (density < 0.97f)
        {
            std::vector<std::pair<int, size_t>> order;
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const auto offbeat = std::fmod (notes[i].start, 1.0) > 1.0e-6;
                order.push_back ({ notes[i].velocity - (offbeat ? 20 : 0), i });
            }
            std::sort (order.begin(), order.end());
            const auto drop = (size_t) std::lround ((1.0f - density) * (float) notes.size());
            std::set<size_t> gone;
            for (size_t k = 0; k < drop && k < order.size(); ++k) gone.insert (order[k].second);
            std::vector<Note> kept;
            for (size_t i = 0; i < notes.size(); ++i) if (! gone.count (i)) kept.push_back (notes[i]);
            notes = kept;
        }
        for (auto& n : notes)
        {
            n.velocity = juce::jlimit (1, 127, n.velocity + (int) std::lround (l.velocity.at (p)));
            n.pitch = juce::jlimit (0, 127, n.pitch + 12 * (int) std::lround (l.octave.at (p)));
            n.length = std::max (0.05, n.length * (double) l.gate.at (p));
        }
        return notes;
    }
}
