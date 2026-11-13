#pragma once

// Correspondence writes for the suite director's plan:
//  - It leaves room: no note sounds through the director's silences (the kick plays through a
//    kick-only bar), and the arrangement edits happen in the MIDI (the bass out for the bar
//    before a drop, the arp resting before a breakdown, the pads coming in late).
//  - Harmony moves: a borrowed bVI-bVII into a drop, a pedal under the build, a V pickup into
//    the new key, a sus4 resolving on the one; a half-time feel for the breakdown's first bars.
//  - Transition parts (the "Song Transitions" clip): pickup fills, riser arps, chord stabs and the
//    hook's first notes, alone, in the breakdown, wherever the director gives Correspondence a part.
// The tempo moves go into every export's tempo track (DirectorBus::tempoChanges).

#include "ChordEngine.h"
#include "DirectorBus.h"
#include "Music.h"

namespace chords::directed
{
    namespace dr = suite::director;
    enum class Part { Chords, Bass, Arp, Lead, Drums, Pads, Other };

    inline Part partOf (const juce::String& kind)
    {
        if (kind == "Song Drums") return Part::Drums;
        if (kind.endsWith ("Bass") || kind == "Song Sub") return Part::Bass;
        if (kind.endsWith ("Arp")) return Part::Arp;
        if (kind == "Song Chords") return Part::Chords;
        if (kind.endsWith ("Lead") || kind.contains ("Lead ") || kind.endsWith ("Harmony") || kind.endsWith ("Counter")) return Part::Lead;
        if (kind.endsWith ("Piano") || kind.endsWith ("Strings") || kind.endsWith ("Choir") || kind.endsWith ("Layer")) return Part::Pads;
        return Part::Other;
    }

    namespace detail
    {
        /** Cuts every note out of [a, b): notes starting inside go, notes ringing into it stop at a. */
        inline int clear (std::vector<Note>& notes, double a, double b, int keepPitch = -1)
        {
            int changed = 0;
            std::vector<Note> out;
            for (auto n : notes)
            {
                if (n.pitch == keepPitch) { out.push_back (n); continue; }
                if (n.start >= a - 1.0e-6 && n.start < b - 1.0e-6) { ++changed; continue; }
                if (n.start < a && n.start + n.length > a + 1.0e-6) { n.length = juce::jmax (0.05, a - n.start); ++changed; }
                out.push_back (n);
            }
            notes = std::move (out);
            return changed;
        }
        inline int nearestOctave (int pc, int around) { auto p = around - ((around - pc) % 12 + 12) % 12; if (around - p > 6) p += 12; return p; }
        inline double meanPitch (const std::vector<Note>& notes, double a, double b, double fallback)
        {
            double sum = 0.0; int n = 0;
            for (const auto& x : notes) if (x.start >= a - 1.0e-6 && x.start < b) { sum += x.pitch; ++n; }
            return n > 0 ? sum / n : fallback;
        }
        inline void triad (std::vector<Note>& notes, int rootPc, int around, double start, double length, int velocity, bool minor = false)
        {
            const auto r = nearestOctave (rootPc, around);
            for (int iv : { 0, minor ? 3 : 4, 7 }) notes.push_back ({ r + iv, start, length, velocity });
        }
    }

    /** Applies the plan to one part's notes. Returns how many notes it changed. */
    inline int apply (std::vector<Note>& notes, Part part, const std::vector<dr::Moment>& plan, double barQ, int root, int scale, int finalLift)
    {
        int changed = 0;
        juce::ignoreUnused (scale);
        for (const auto& m : plan)
        {
            // arrangement edits
            if ((m.edit == dr::eBassOut && part == Part::Bass) || (m.edit == dr::eArpOut && part == Part::Arp)
                || (m.edit == dr::ePadsLate && (part == Part::Chords || part == Part::Pads)) || (m.edit == dr::eDrumsOut && part == Part::Drums))
                changed += detail::clear (notes, m.editStartQ, m.editEndQ);
            if (m.acts (dr::pCorrespondence) && m.gesture[dr::pCorrespondence] == dr::gCoBassOut && part == Part::Bass)
                changed += detail::clear (notes, m.atQ - barQ, m.atQ);
            // harmony into the drop
            if (m.kind == dr::kIntoDrop && (part == Part::Chords || part == Part::Bass))
            {
                const auto a = (double) m.atQ - barQ, b = (double) m.atQ;
                const auto around = (int) std::round (detail::meanPitch (notes, a - barQ, b, part == Part::Bass ? 40.0 : 64.0));
                const auto vel = 96;
                switch (m.harmony)
                {
                    case dr::hBorrowed:
                    {
                        changed += detail::clear (notes, a, b);
                        const auto six = (root + 8) % 12, seven = (root + 10) % 12;
                        if (part == Part::Bass)
                        {
                            notes.push_back ({ detail::nearestOctave (six, around), a, 0.5 * barQ - 0.05, vel });
                            notes.push_back ({ detail::nearestOctave (seven, around), a + 0.5 * barQ, 0.5 * barQ - 0.05, vel });
                        }
                        else
                        {
                            detail::triad (notes, six, around, a, 0.5 * barQ - 0.05, vel);
                            detail::triad (notes, seven, around, a + 0.5 * barQ, 0.5 * barQ - 0.05, vel);
                        }
                        changed += 2;
                        break;
                    }
                    case dr::hKeyPrep:
                    {
                        changed += detail::clear (notes, b - 2.0, b);
                        const auto five = (root + finalLift + 7) % 12;
                        if (part == Part::Bass) notes.push_back ({ detail::nearestOctave (five, around), b - 2.0, 1.95, vel });
                        else detail::triad (notes, five, around, b - 2.0, 1.95, vel);
                        ++changed;
                        break;
                    }
                    case dr::hSus:
                        if (part == Part::Chords)
                        {
                            // in the last bar, each chord's third rises to the fourth (resolved by the drop's first chord)
                            std::map<long, std::vector<size_t>> byStart;
                            for (size_t i = 0; i < notes.size(); ++i)
                                if (notes[i].start >= a - 1.0e-6 && notes[i].start < b) byStart[std::lround (notes[i].start * 96.0)].push_back (i);
                            for (auto& [t, idx] : byStart)
                            {
                                int low = 999;
                                for (auto i : idx) low = juce::jmin (low, notes[i].pitch);
                                for (auto i : idx)
                                {
                                    const auto iv = ((notes[i].pitch - low) % 12 + 12) % 12;
                                    if (iv == 3 || iv == 4) { notes[i].pitch += 5 - iv; ++changed; }
                                }
                            }
                        }
                        break;
                    case dr::hPedal:
                        if (part == Part::Bass && m.section > 0)
                        {
                            // the build before it: the bass holds the tonic (its rhythm kept)
                            double buildStart = a;
                            for (const auto& o : plan) if (o.kind == dr::kIntoBuild && o.atQ < m.atQ && o.atQ > buildStart - 64.0 * barQ) buildStart = o.atQ;
                            if (buildStart >= a) buildStart = m.atQ - 8.0 * barQ;
                            for (auto& n : notes)
                                if (n.start >= buildStart - 1.0e-6 && n.start < b) { n.pitch = detail::nearestOctave (root, n.pitch); ++changed; }
                        }
                        break;
                    default: break;
                }
            }
            // a half-time feel for the breakdown's first two bars (the bass)
            if (m.kind == dr::kIntoBreak && m.tempo == dr::tHalftime && part == Part::Bass)
            {
                std::vector<Note> out;
                int k = 0;
                for (auto n : notes)
                {
                    if (n.start >= m.atQ - 1.0e-6 && n.start < m.atQ + 2.0 * barQ)
                    {
                        if ((k++ % 2) == 1) { ++changed; continue; }
                        n.length = juce::jmin (n.length * 2.0, (double) m.atQ + 2.0 * barQ - n.start);
                    }
                    out.push_back (n);
                }
                notes = std::move (out);
            }
        }
        // last: the director's silences (nothing written above may sound through them)
        for (const auto& m : plan)
            if (m.silence != dr::gNone)
                changed += detail::clear (notes, m.silenceStartQ, m.silenceEndQ, part == Part::Drums && m.silence == dr::gSiKickOnly ? 36 : -1);
        std::sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.start < y.start; });
        return changed;
    }

    /** The director's transition parts for Correspondence: one clip ("Song Transitions"). */
    inline std::vector<Note> transitionsClip (const std::vector<dr::Moment>& plan, double barQ, int root, int scale, const std::vector<Note>& lead)
    {
        std::vector<Note> out;
        const auto minor = scale != 1;
        auto degree = [&] (int d) { return suite::music::scaleStep (scale, d); };
        for (const auto& m : plan)
        {
            if (! m.acts (dr::pCorrespondence)) continue;
            const auto hero = m.hero == dr::pCorrespondence;
            const auto vel = hero ? 104 : 84;
            const auto base = 72 + root;   // written in the song's key (the key change is Ouroboros' to make)
            switch (m.gesture[dr::pCorrespondence])
            {
                case dr::gCoPickup:
                {
                    const auto end = m.silence != dr::gNone ? (double) m.silenceStartQ : (double) m.atQ;
                    for (int k = 0; k < 4; ++k) out.push_back ({ base - 12 + degree (4 + k), end - 1.0 + 0.25 * k, 0.22, vel - 8 + 4 * k });
                    break;
                }
                case dr::gCoRiserArp:
                {
                    const auto end = m.silence != dr::gNone ? (double) m.silenceStartQ : (double) m.atQ;
                    const auto start = end - 2.0 * barQ;
                    int k = 0;
                    for (auto q = start; q < end - 1.0e-6; q += 0.25, ++k)
                    {
                        const auto octave = (int) ((q - start) / (2.0 * barQ) * 2.0);
                        const auto step = degree ((k % 3) * 2) + 12 * octave;
                        out.push_back ({ base - 12 + step, q, 0.2, juce::jlimit (40, 120, 60 + (int) (50.0 * (q - start) / (end - start))) });
                    }
                    break;
                }
                case dr::gCoStab:
                    detail::triad (out, root % 12, 64, m.atQ, 0.45, vel, minor);
                    out.push_back ({ detail::nearestOctave (root % 12, 52), (double) m.atQ, 0.45, vel });
                    break;
                case dr::gCoHint:
                {
                    // the hook's first notes (from the first drop's lead), alone
                    double firstDrop = -1.0;
                    for (const auto& o : plan) if (o.kind == dr::kIntoDrop) { firstDrop = o.atQ; break; }
                    std::vector<Note> hook;
                    for (const auto& n : lead) if (firstDrop >= 0.0 && n.start >= firstDrop - 1.0e-6 && n.start < firstDrop + barQ && hook.size() < 5) hook.push_back (n);
                    if (hook.empty()) for (int k = 0; k < 3; ++k) hook.push_back ({ base + degree (k == 1 ? 2 : k == 2 ? 4 : 0), firstDrop + k, 0.9, vel });
                    const auto shift = (double) m.atQ - hook.front().start;
                    for (auto n : hook) { n.start += shift; n.velocity = juce::jmin (n.velocity, vel - 10); out.push_back (n); }
                    break;
                }
                default: break;
            }
        }
        std::sort (out.begin(), out.end(), [] (const Note& x, const Note& y) { return x.start < y.start; });
        return out;
    }
}
