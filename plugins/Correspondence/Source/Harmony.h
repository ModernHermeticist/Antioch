#pragma once

// Harmony explorer: colour for the progression you have, each idea with what it does to the
// song. They change one chord's degree or type (the engine works in the key's degrees, so a
// chord from outside the key, like a flat VI in a major key, isn't offered).

#include "ChordEngine.h"

namespace harmony
{
    struct Idea
    {
        juce::String name, effect;
        chords::Settings result;
    };

    inline std::vector<Idea> ideas (const chords::Settings& s)
    {
        using Q = chords::Quality;
        std::vector<Idea> out;
        const auto minor = s.scale != 1;
        const auto n = juce::jlimit (1, chords::maxSlots, s.count);
        auto add = [&] (const juce::String& name, const juce::String& effect, chords::Settings r)
        {
            bool changed = false;
            for (int i = 0; i < n; ++i)
                changed = changed || r.slots[(size_t) i].degree != s.slots[(size_t) i].degree || r.slots[(size_t) i].quality != s.slots[(size_t) i].quality;
            if (changed) out.push_back ({ name, effect, r });
        };
        auto find = [&] (int degree) { for (int i = 0; i < n; ++i) if (s.slots[(size_t) i].degree == degree) return i; return -1; };

        if (minor)
        {
            if (const auto v = find (5); v >= 0)
            {
                auto r = s; r.slots[(size_t) v].quality = Q::Major;
                add ("Major V", "the V turns major (harmonic minor): it pulls home to i much harder", r);
            }
            else if (n >= 2)
            {
                auto r = s; r.slots[(size_t) n - 1] = { 5, Q::Major };
                add ("End on a major V", "the last chord becomes a major V: every loop leans hard into the next downbeat", r);
            }
            if (const auto iv = find (4); iv >= 0)
            {
                auto r = s; r.slots[(size_t) iv].quality = Q::Major;
                add ("Dorian IV", "the iv turns major: brighter and more hopeful, without leaving the key's feel", r);
            }
            if (s.slots[0].degree == 1)
            {
                auto r = s; r.slots[0].quality = Q::Major;
                add ("Picardy I", "the home chord turns major: a sunrise moment (best saved for the final drop or the outro)", r);
            }
            if (s.slots[0].degree == 1 && find (6) != 0)
            {
                auto r = s; r.slots[0].degree = 6;
                add ("Start on VI", "the progression opens on VI instead of i: softer, more emotional, the classic breakdown colour", r);
            }
        }
        else
        {
            if (const auto iv = find (4); iv >= 0)
            {
                auto r = s; r.slots[(size_t) iv].quality = Q::Minor;
                add ("Borrowed iv", "the IV turns minor (borrowed from the minor key): bittersweet, the classic emotional turn", r);
            }
            if (const auto vi = find (6); vi < 0 && n >= 3)
            {
                auto r = s; r.slots[1].degree = 6; r.slots[1].quality = Q::Auto;
                add ("Bring in vi", "the second chord becomes vi: a touch of melancholy in a bright progression", r);
            }
        }
        {
            auto r = s; r.slots[(size_t) n - 1].quality = Q::Sus4;
            add ("Sus4 before the loop", "the last chord hangs as a sus4: tension that resolves on the next downbeat (great before a drop)", r);
        }
        {
            auto r = s; r.slots[0].quality = Q::Add9;
            add ("Add9 shimmer", "the first chord gains a 9th: airier and more open, a trance pad favourite", r);
        }
        if (n >= 2)
        {
            auto r = s; r.slots[(size_t) n - 2].quality = Q::Seventh;
            add ("7th into the turn", "the second-to-last chord gains a 7th: it slides into the last one more smoothly", r);
        }
        {
            auto r = s;
            for (int i = 0; i < n; ++i) r.slots[(size_t) i].quality = Q::Sus2;
            add ("Open sus2 colour", "every chord becomes a sus2: no thirds, wide and ambiguous, very uplifting on supersaws", r);
        }
        return out;
    }

    //==============================================================================
    /** Variation engine: alternatives for one kind of section, as changes to its song patterns
        (chords, bass, arp and lead choices; breakdowns can also be re-harmonised). */
    struct Variation
    {
        juce::String name, effect;
        int chords = -1, bass = -1, arp = -1, lead = -1, breakdownStart = -1;   // -1 = unchanged
    };

    inline std::vector<Variation> variations (const chords::SongPatterns& p, suite::SectionType type)
    {
        const auto t = (size_t) type;
        const auto c = p.chords[t], b = p.bass[t], a = p.arp[t], l = p.lead[t];
        std::vector<Variation> out;
        // Chords: 0 none, 1 sustain, 2 offbeat stabs, 3 pluck 3-3-2, 4 16th gate. Bass: 0 none, 1 offbeat,
        // 2 rolling, 3 galloping, 4 sustain, 5 walking, 6 mine. Arp: 0 none, 1 up, 2 down, 3 up-down, 4 bounce. Lead: 0 none, 1 melody, 2 teaser.
        {
            Variation v { "Re-rhythmed", "the same notes in a new rhythm: different chord stabs, bass and arp movement" };
            v.chords = c == 3 ? 2 : c == 2 ? 4 : c == 4 ? 3 : c == 1 ? 3 : 0;
            v.bass = b == 2 ? 3 : b == 3 ? 2 : b == 1 ? 2 : b == 4 ? 1 : b == 5 ? 2 : b;
            v.arp = a == 1 ? 4 : a == 4 ? 3 : a == 0 ? 0 : 1;
            out.push_back (v);
        }
        {
            Variation v { "Thinned out", "less going on: sustained chords, no arp, the lead as a teaser, a simpler bass" };
            v.chords = c == 0 ? 0 : 1;
            v.arp = 0;
            v.lead = l == 1 ? 2 : l;
            v.bass = (b == 2 || b == 3 || b == 5) ? 1 : b;
            out.push_back (v);
        }
        {
            Variation v { "Fuller", "more energy: gated chords, an arp, the lead in full" };
            v.chords = c <= 1 ? 4 : c;
            v.arp = a == 0 ? 1 : a;
            v.lead = l == 0 ? 2 : 1;
            v.bass = (b == 0 && type != suite::SectionType::Breakdown) ? 1 : b;
            out.push_back (v);
        }
        if (type == suite::SectionType::Breakdown)
        {
            Variation v { "Re-harmonised", "" };
            v.breakdownStart = (p.breakdownStart + 1) % 4;
            static const char* what[] = { "the breakdown plays the progression as it is",
                                          "the breakdown starts the progression on VI: softer, and the drop's i lands harder",
                                          "the breakdown starts on IV: open and hopeful",
                                          "the breakdown lifts to III halfway, rising before the build" };
            v.effect = what[v.breakdownStart];
            out.push_back (v);
        }
        // Drop what changes nothing.
        out.erase (std::remove_if (out.begin(), out.end(), [&] (const Variation& v)
        {
            return (v.chords < 0 || v.chords == c) && (v.bass < 0 || v.bass == b) && (v.arp < 0 || v.arp == a) && (v.lead < 0 || v.lead == l)
                && (v.breakdownStart < 0 || v.breakdownStart == p.breakdownStart);
        }), out.end());
        return out;
    }
}
