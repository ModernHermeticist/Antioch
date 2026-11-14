#pragma once

// Writing tools (the writing round): the music logic behind Correspondence's Write menu, as
// plain functions so each one is testable on its own.
//  - Locks: bars of a part you like, kept through every regenerate.
//  - Takes: each regenerate is a take; step back and forward, keep bars from any of them.
//  - One motif, the whole song: every form of the hook derived from one 2-bar source.
//  - Hook doctor: what makes a hook memorable, scored and explained, with one-note edits.
//  - Hum a hook: pitch tracking and note segmentation, snapped to the key.
//  - Topline from lyrics: syllables and stresses, placed on the beat over the chords.
//  - Originality: a hook against your past hooks and the pattern library.
//  - Emotional arc: a feeling per section, as settings and chords.
//  - Trance harmony moves: signature progressions, dropped in with a preview and Undo.
//  - Feel from a reference: swing and accents (statistics only) from an audio file.
//  - Rhythmic identity: the hook's rhythmic cell shared by the bass and arp.
//  - Taste: what you rate up and down, learned, ranking new takes.
//  - Why it works: a line of theory under each suggestion.

#include "ChordEngine.h"
#include "SongBus.h"
#include "PatternLibrary.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace chords::tools
{
    //==============================================================================
    // Small helpers
    inline std::vector<Note> sorted (std::vector<Note> v) { std::stable_sort (v.begin(), v.end(), [] (const Note& a, const Note& b) { return a.start < b.start; }); return v; }
    inline int barOf (double q, double barQ = 4.0) { return (int) std::floor (q / barQ + 1.0e-9); }
    inline std::vector<Note> rebased (std::vector<Note> v)
    {
        if (v.empty()) return v;
        auto first = v.front().start;
        for (const auto& n : v) first = std::min (first, n.start);
        const auto bar = std::floor (first / 4.0 + 1.0e-9) * 4.0;
        for (auto& n : v) n.start -= bar;
        return sorted (v);
    }
    inline std::vector<Note> window (const std::vector<Note>& v, double from, double to)
    {
        std::vector<Note> out;
        for (const auto& n : v) if (n.start >= from - 1.0e-9 && n.start < to - 1.0e-9) out.push_back (n);
        return out;
    }

    //==============================================================================
    // Locks
    struct Lock
    {
        std::set<int> bars;          // 0-based bars of the song
        std::vector<Note> notes;     // the notes kept there
        bool empty() const { return bars.empty(); }
    };
    /** Keep bars [fromBar, toBar] (0-based, inclusive) of `current` in the lock. */
    inline Lock lockBars (const std::vector<Note>& current, Lock lock, int fromBar, int toBar, double barQ = 4.0)
    {
        for (int b = std::max (0, fromBar); b <= toBar; ++b)
        {
            if (lock.bars.count (b))   // replace what was kept there
                lock.notes.erase (std::remove_if (lock.notes.begin(), lock.notes.end(), [b, barQ] (const Note& n) { return barOf (n.start, barQ) == b; }), lock.notes.end());
            lock.bars.insert (b);
            for (const auto& n : current) if (barOf (n.start, barQ) == b) lock.notes.push_back (n);
        }
        lock.notes = sorted (lock.notes);
        return lock;
    }
    inline Lock unlockBars (Lock lock, int fromBar, int toBar, double barQ = 4.0)
    {
        for (int b = fromBar; b <= toBar; ++b) lock.bars.erase (b);
        lock.notes.erase (std::remove_if (lock.notes.begin(), lock.notes.end(), [&] (const Note& n) { return lock.bars.count (barOf (n.start, barQ)) == 0; }), lock.notes.end());
        return lock;
    }
    /** A freshly generated part with the locked bars put back (a note from an unlocked bar ringing
        into a locked one is cut at the locked bar). */
    inline std::vector<Note> mergeLocks (const std::vector<Note>& fresh, const Lock& lock, double barQ = 4.0)
    {
        if (lock.empty()) return fresh;
        std::vector<Note> out;
        for (auto n : fresh)
        {
            const auto b = barOf (n.start, barQ);
            if (lock.bars.count (b)) continue;
            // cut at the next locked bar
            for (int k = b + 1; (double) k * barQ < n.start + n.length - 1.0e-9; ++k)
                if (lock.bars.count (k)) { n.length = std::max (0.05, (double) k * barQ - n.start); break; }
            out.push_back (n);
        }
        for (const auto& n : lock.notes) if (lock.bars.count (barOf (n.start, barQ))) out.push_back (n);
        return sorted (out);
    }
    inline juce::String toText (const Lock& l)
    {
        juce::StringArray bars, notes;
        for (auto b : l.bars) bars.add (juce::String (b));
        for (const auto& n : l.notes) notes.add (juce::String (n.pitch) + ":" + juce::String (n.start, 4) + ":" + juce::String (n.length, 4) + ":" + juce::String (n.velocity));
        return bars.joinIntoString (",") + "|" + notes.joinIntoString (";");
    }
    inline Lock lockFromText (const juce::String& t)
    {
        Lock l;
        for (const auto& b : juce::StringArray::fromTokens (t.upToFirstOccurrenceOf ("|", false, false), ",", "")) if (b.isNotEmpty()) l.bars.insert (b.getIntValue());
        for (const auto& s : juce::StringArray::fromTokens (t.fromFirstOccurrenceOf ("|", false, false), ";", ""))
        {
            const auto v = juce::StringArray::fromTokens (s, ":", "");
            if (v.size() == 4) l.notes.push_back ({ v[0].getIntValue(), v[1].getDoubleValue(), v[2].getDoubleValue(), v[3].getIntValue() });
        }
        return l;
    }

    //==============================================================================
    // Takes
    struct Take
    {
        int melodySeed = 7, melodyStyle = 1;
        uint32_t libSeed = 1;
        std::vector<Note> notes;    // the part as it was (for keeping bars from it)
        int rating = 0;             // +1 / -1 / 0
        juce::String why;
    };
    struct TakeHistory
    {
        std::vector<Take> takes;
        int current = -1;
        static constexpr int maxTakes = 24;
        void push (const Take& t)
        {
            if (current >= 0 && current + 1 < (int) takes.size()) takes.erase (takes.begin() + current + 1, takes.end());   // a new branch
            takes.push_back (t);
            if ((int) takes.size() > maxTakes) takes.erase (takes.begin());
            current = (int) takes.size() - 1;
        }
        const Take* back() { if (current <= 0) return nullptr; return &takes[(size_t) --current]; }
        const Take* forward() { if (current + 1 >= (int) takes.size()) return nullptr; return &takes[(size_t) ++current]; }
        const Take* now() const { return current >= 0 && current < (int) takes.size() ? &takes[(size_t) current] : nullptr; }
    };

    //==============================================================================
    // One motif, the whole song: each section's form of the 2-bar hook
    /** The hook (first 2 bars, rebased to 0) in a section of `lengthQ` quarters starting at `startQ`. */
    inline std::vector<Note> motifForm (const std::vector<Note>& hook2, const Settings& s, suite::SectionType type, int occurrence, double startQ, double lengthQ)
    {
        using T = suite::SectionType;
        std::vector<Note> out;
        if (hook2.empty() || lengthQ <= 0.0) return out;
        const auto cycle = 8.0;
        auto tile = [&] (const std::vector<Note>& cell, double cellQ, int velocity, std::function<void (Note&, int)> each = {})
        {
            int k = 0;
            for (double q = 0.0; q < lengthQ - 1.0e-9; q += cellQ, ++k)
                for (auto n : cell)
                {
                    n.start += startQ + q;
                    if (n.start >= startQ + lengthQ - 1.0e-9) continue;
                    n.length = std::min (n.length, startQ + lengthQ - n.start);
                    n.velocity = velocity;
                    if (each) each (n, k);
                    out.push_back (n);
                }
        };
        switch (type)
        {
            case T::Intro:    // a teaser: the first three notes of every other cycle, soft, in the second half
            {
                std::vector<Note> t;
                for (const auto& n : writing::foreshadow (hook2)) t.push_back (n);
                tile (t, 2.0 * cycle, 70, [&] (Note& n, int) { if (n.start < startQ + lengthQ * 0.5) n.velocity = 0; });
                out.erase (std::remove_if (out.begin(), out.end(), [] (const Note& n) { return n.velocity == 0; }), out.end());
                break;
            }
            case T::Groove:   // a fragment: the hook's opening every four bars
                tile (writing::foreshadow (hook2), 2.0 * cycle, 85);
                break;
            case T::Breakdown:  // the hook slow: twice as long, soft
                tile (writing::develop (hook2, writing::devAugment, s, 2.0 * cycle), 2.0 * cycle, 80);
                break;
            case T::Build:      // the opening cell, climbing bar by bar, louder toward the drop
                tile (writing::develop (hook2, writing::devFragment, s, cycle), cycle, 90,
                      [&] (Note& n, int) { n.velocity = juce::jlimit (60, 120, 80 + (int) (40.0 * (n.start - startQ) / lengthQ)); });
                break;
            case T::Drop:       // the hook itself; later drops a sequence of it (each cycle a step higher, then back)
                if (occurrence == 0) tile (hook2, cycle, 105);
                else tile (hook2, cycle, 112, [&] (Note& n, int k) { static const int seq[4] = { 0, 1, 2, 1 }; n.pitch = writing::scaleMove (s, n.pitch, seq[k % 4]); });
                break;
            case T::Outro:      // the opening once per cycle, fading
                tile (writing::foreshadow (hook2), cycle, 80, [&] (Note& n, int) { n.velocity = juce::jlimit (30, 100, 90 - (int) (60.0 * (n.start - startQ) / lengthQ)); });
                break;
            default: break;
        }
        return sorted (out);
    }
    /** The whole song's lead from one hook, section by section. */
    inline std::vector<Note> motifPlan (const std::vector<Note>& hook, const Settings& s, const suite::SectionMap& map, double barQ = 4.0)
    {
        std::vector<Note> out;
        const auto hook2 = window (rebased (hook), 0.0, 8.0);
        std::array<int, (int) suite::SectionType::count> seen {};
        for (int i = 0; i < map.count; ++i)
        {
            const auto& sec = map.sections[(size_t) i];
            const auto start = barQ * (sec.startBar - 1), length = barQ * (map.endBar (i) - sec.startBar);
            const auto occ = seen[(size_t) sec.type]++;
            for (const auto& n : motifForm (hook2, s, sec.type, occ, start, length)) out.push_back (n);
        }
        return sorted (out);
    }

    //==============================================================================
    // Hook doctor
    struct HookEdit { int index = -1, from = 0, to = 0; float gain = 0.0f; juce::String why; };
    struct HookReport
    {
        float score = 0.0f;                                         // 0..1
        float repetition = 0.0f, intervals = 0.0f, range = 0.0f, rhythm = 0.0f, peak = 0.0f, ending = 0.0f;
        juce::StringArray notes;                                    // in words
        std::vector<HookEdit> edits;                                // one-note changes that raise the score, best first
    };
    namespace detail
    {
        inline float ramp (float v, float good, float bad) { if (std::abs (good - bad) < 1.0e-9f) return v >= good ? 1.0f : 0.0f; return juce::jlimit (0.0f, 1.0f, (v - bad) / (good - bad)); }
        inline std::vector<suite::SongBus::Note> toBus (const std::vector<Note>& v)
        {
            std::vector<suite::SongBus::Note> o;
            for (const auto& n : v) o.push_back ({ n.pitch, (float) n.start, (float) n.length, n.velocity });
            return o;
        }
    }
    inline HookReport hookDoctor (const std::vector<Note>& hookIn, const Settings& s, bool withEdits = true)
    {
        using namespace detail;
        HookReport r;
        const auto h = window (rebased (hookIn), 0.0, 8.0);
        if (h.size() < 3) { r.notes.add ("too few notes to judge (three or more in two bars)"); return r; }
        // repetition: bar 2 echoes bar 1's rhythm with a change (AA')
        std::set<int> b1, b2;
        for (const auto& n : h) { const auto st = (int) std::lround (n.start * 4.0); (st < 16 ? b1 : b2).insert (st % 16); }
        int inter = 0; for (auto x : b1) inter += b2.count (x) ? 1 : 0;
        const auto uni = (int) (b1.size() + b2.size()) - inter;
        const auto jac = uni > 0 ? (float) inter / (float) uni : 0.0f;
        r.repetition = jac >= 0.95f ? 0.8f : ramp (jac, 0.6f, 0.0f);
        // intervals: mostly steps, a leap or two
        int steps = 0, leaps = 0;
        for (size_t i = 1; i < h.size(); ++i) { const auto d = std::abs (h[i].pitch - h[i - 1].pitch); if (d <= 2) ++steps; if (d >= 5) ++leaps; }
        const auto stepShare = (float) steps / (float) (h.size() - 1);
        r.intervals = 0.5f * std::min (ramp (stepShare, 0.5f, 0.15f), ramp (stepShare, 0.85f, 1.0f)) + (leaps >= 1 && leaps <= 3 ? 0.5f : leaps == 0 ? 0.2f : 0.3f);
        // range
        int lo = 127, hi = 0; size_t peakAt = 0; int peakCount = 0;
        for (size_t i = 0; i < h.size(); ++i) { lo = std::min (lo, h[i].pitch); if (h[i].pitch > hi) { hi = h[i].pitch; peakAt = i; } }
        for (const auto& n : h) peakCount += n.pitch == hi ? 1 : 0;
        r.range = std::min (ramp ((float) (hi - lo), 7.0f, 3.0f), ramp ((float) (hi - lo), 15.0f, 20.0f));
        // rhythm: two or three lengths, a cell it keeps
        std::set<int> lens; for (const auto& n : h) lens.insert ((int) std::lround (n.length * 4.0));
        r.rhythm = lens.size() >= 2 && lens.size() <= 3 ? 1.0f : lens.size() == 1 ? 0.5f : 0.6f;
        // the peak: late, and only once
        const auto where = (float) peakAt / (float) std::max<size_t> (1, h.size() - 1);
        r.peak = (where >= 0.4f ? 1.0f : 0.5f) * (peakCount == 1 ? 1.0f : 0.7f);
        // the ending on a chord tone of its chord
        const auto last = h.back();
        const auto pcs = chordTonesAt (s, last.start);
        r.ending = std::find (pcs.begin(), pcs.end(), (last.pitch % 12 + 12) % 12) != pcs.end() ? 1.0f : 0.4f;
        const auto mean = (r.repetition + r.intervals + r.range + r.rhythm + r.peak + r.ending) / 6.0f;
        r.score = juce::jlimit (0.0f, 1.0f, 0.5f * mean + 0.5f * suite::SongBus::hookScore (toBus (h), 0.0, s.root, s.scale));
        // in words
        r.notes.add (jac >= 0.95f ? "bar 2 repeats bar 1's rhythm exactly: memorable, a small change would make it sing"
                   : jac >= 0.6f ? "bar 2 echoes bar 1's rhythm with a change (AA'): the shape listeners remember"
                                 : "bar 2's rhythm has little to do with bar 1's: harder to remember");
        r.notes.add (juce::String (juce::roundToInt (stepShare * 100.0f)) + "% steps, " + juce::String (leaps) + " leap" + (leaps == 1 ? "" : "s")
                     + (leaps == 0 ? ": smooth but flat, one leap would give it a shape" : leaps > 3 ? ": jumpy, hard to sing" : ": steps carry it, the leap is the moment"));
        r.notes.add ("a range of " + juce::String (hi - lo) + " semitones" + (hi - lo < 7 ? ": narrow, it won't soar" : hi - lo > 15 ? ": wide, hard to sing" : ""));
        r.notes.add (where >= 0.4f ? "its peak comes late (" + juce::String (juce::roundToInt (where * 100.0f)) + "% in): it arrives rather than starts"
                                   : "its peak comes early: the rest feels like coming down");
        if (peakCount > 1) r.notes.add ("the top note comes " + juce::String (peakCount) + " times: once makes it special");
        r.notes.add (r.ending >= 1.0f ? "it ends on a chord tone: it lands" : "it ends off the chord: it hangs (good only if it leads somewhere)");
        if (! withEdits) return r;
        // one-note edits: each note tried a scale step or two either way, the best gain per note
        for (size_t i = 1; i < h.size(); ++i)
        {
            HookEdit best;
            for (int d : { -2, -1, 1, 2 })
            {
                auto t = h;
                t[i].pitch = writing::scaleMove (s, h[i].pitch, d);
                const auto rr = hookDoctor (t, s, false);
                const auto gain = rr.score - r.score;
                if (gain > best.gain + 1.0e-4f)
                {
                    best = { (int) i, h[i].pitch, t[i].pitch, gain, {} };
                    // what got better
                    const std::pair<float, const char*> parts[] = { { rr.intervals - r.intervals, "a smoother line" }, { rr.range - r.range, "a better range" },
                                                                    { rr.peak - r.peak, "a later, single peak" }, { rr.ending - r.ending, "it lands on the chord" } };
                    const auto* top = &parts[0];
                    for (const auto& p : parts) if (p.first > top->first) top = &p;
                    best.why = top->first > 0.0f ? juce::String (top->second) : juce::String ("a stronger overall shape");
                }
            }
            if (best.index >= 0 && best.gain > 0.01f) r.edits.push_back (best);
        }
        std::sort (r.edits.begin(), r.edits.end(), [] (const HookEdit& a, const HookEdit& b) { return a.gain > b.gain; });
        if (r.edits.size() > 3) r.edits.resize (3);
        return r;
    }
    /** The hook with one edit applied (the note index into the rebased, windowed hook). */
    inline std::vector<Note> applyEdit (const std::vector<Note>& hookIn, const HookEdit& e)
    {
        auto h = window (rebased (hookIn), 0.0, 8.0);
        if (e.index >= 0 && e.index < (int) h.size()) h[(size_t) e.index].pitch = e.to;
        return h;
    }

    //==============================================================================
    // Hum a hook
    /** The pitch of a short mono frame (Hz), 0 when it isn't clearly pitched. Normalised
        autocorrelation over 70-1000 Hz with a clarity threshold. */
    inline float detectPitch (const float* x, int n, double sr)
    {
        if (n < 64) return 0.0f;
        double e0 = 0.0;
        for (int i = 0; i < n; ++i) e0 += (double) x[i] * x[i];
        if (e0 / n < 1.0e-6) return 0.0f;
        const auto minLag = (int) (sr / 1000.0), maxLag = std::min (n / 2, (int) (sr / 70.0));
        float best = 0.0f; int bestLag = -1;
        std::vector<float> c ((size_t) maxLag + 2, 0.0f);
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double acc = 0.0, ea = 0.0, eb = 0.0;
            for (int i = 0; i + lag < n; ++i) { acc += (double) x[i] * x[i + lag]; ea += (double) x[i] * x[i]; eb += (double) x[i + lag] * x[i + lag]; }
            c[(size_t) lag] = ea > 0.0 && eb > 0.0 ? (float) (acc / std::sqrt (ea * eb)) : 0.0f;
        }
        // the first strong peak (not the biggest: octave errors)
        const auto top = *std::max_element (c.begin() + minLag, c.begin() + maxLag + 1);
        for (int lag = minLag + 1; lag < maxLag; ++lag)
            if (c[(size_t) lag] >= 0.85f * top && c[(size_t) lag] >= c[(size_t) lag - 1] && c[(size_t) lag] >= c[(size_t) lag + 1]) { best = c[(size_t) lag]; bestLag = lag; break; }
        if (bestLag < 0 || best < 0.6f) return 0.0f;
        // parabolic refinement
        const auto a = c[(size_t) bestLag - 1], b = c[(size_t) bestLag], d = c[(size_t) bestLag + 1];
        const auto den = a - 2.0f * b + d;
        const auto shift = std::abs (den) > 1.0e-9f ? 0.5f * (a - d) / den : 0.0f;
        return (float) (sr / ((double) bestLag + shift));
    }
    struct PitchFrame { float hz = 0.0f, rms = 0.0f; };
    /** Frames (each `frameSeconds` long) to a motif: steady stretches become notes, quantised to 16ths,
        snapped to the key, rebased to the first note and kept to `bars` bars. */
    inline std::vector<Note> notesFromFrames (const std::vector<PitchFrame>& frames, double frameSeconds, double bpm, int root, int scale, int bars = 2)
    {
        std::vector<Note> out;
        const auto qPerFrame = frameSeconds * bpm / 60.0;
        auto midiOf = [] (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); };
        size_t i = 0;
        while (i < frames.size())
        {
            if (frames[i].hz <= 0.0f) { ++i; continue; }
            const auto startF = i;
            std::vector<float> ps { midiOf (frames[i].hz) };
            size_t j = i + 1;
            while (j < frames.size() && frames[j].hz > 0.0f)
            {
                std::vector<float> sortedPs (ps); std::sort (sortedPs.begin(), sortedPs.end());
                const auto med = sortedPs[sortedPs.size() / 2];
                if (std::abs (midiOf (frames[j].hz) - med) > 0.7f) break;   // a new note
                ps.push_back (midiOf (frames[j].hz)); ++j;
            }
            const auto lenQ = (double) (j - startF) * qPerFrame;
            if (lenQ >= 0.15)   // about 60 ms at 138: shorter is a scoop or noise
            {
                std::sort (ps.begin(), ps.end());
                const auto pitch = (int) std::lround (ps[ps.size() / 2]);
                out.push_back ({ pitch, (double) startF * qPerFrame, lenQ, 100 });
            }
            i = j;
        }
        if (out.empty()) return out;
        // quantise, rebase, snap
        const auto first = std::round (out.front().start * 4.0) / 4.0;
        Settings s; s.root = root; s.scale = scale;
        std::vector<Note> q;
        for (auto n : out)
        {
            n.start = std::max (0.0, std::round (n.start * 4.0) / 4.0 - first);
            n.length = std::max (0.25, std::round (n.length * 4.0) / 4.0);
            n.pitch = writing::snapToScale (s, n.pitch);
            if (n.start < 4.0 * bars - 1.0e-9 && (q.empty() || n.start > q.back().start + 1.0e-9)) q.push_back (n);
        }
        for (size_t k = 0; k + 1 < q.size(); ++k) q[k].length = std::min (q[k].length, q[k + 1].start - q[k].start);
        return q;
    }

    //==============================================================================
    // Topline from lyrics
    struct Syllable { juce::String text; bool stressed = false, wordEnd = false, phraseEnd = false; };
    inline std::vector<Syllable> syllables (const juce::String& lyric)
    {
        static const juce::StringArray weak { "a", "an", "the", "to", "of", "and", "in", "on", "my", "your", "i", "you", "we", "it", "is", "for", "with", "at", "be", "me",
                                              "but", "or", "as", "so", "our", "his", "her", "their", "that", "this", "are", "was", "were", "am", "by", "from", "do" };
        static const juce::StringArray unstressedPrefix { "a", "be", "re", "de", "un", "for", "to", "ex", "em", "en", "in" };
        std::vector<Syllable> out;
        juce::StringArray words;
        words.addTokens (lyric.replaceCharacters ("\n", " "), " ", "");
        words.removeEmptyStrings();
        auto isVowel = [] (juce::juce_wchar c) { return juce::String ("aeiouy").containsChar (juce::CharacterFunctions::toLowerCase (c)); };
        for (int w = 0; w < words.size(); ++w)
        {
            const auto raw = words[w];
            const auto phraseEnd = raw.endsWithAnyOf (",.;!?") || w == words.size() - 1;
            const auto word = raw.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'");
            if (word.isEmpty()) continue;
            const auto lower = word.toLowerCase();
            // vowel groups, a silent final e
            std::vector<int> groupStarts;
            for (int i = 0; i < lower.length(); ++i)
                if (isVowel (lower[i]) && (i == 0 || ! isVowel (lower[i - 1]))) groupStarts.push_back (i);
            if (groupStarts.size() > 1 && lower.endsWith ("e") && ! lower.endsWith ("le") && groupStarts.back() == lower.length() - 1) groupStarts.pop_back();
            if (groupStarts.empty()) groupStarts.push_back (0);
            const auto n = (int) groupStarts.size();
            // stress: the first syllable, unless it's an unstressed prefix of a longer word; weak words none
            int stressAt = 0;
            if (n >= 2) for (const auto& p : unstressedPrefix) if (lower.startsWith (p) && groupStarts[1] <= p.length() + 1) { stressAt = 1; break; }
            if (n >= 3 && (lower.endsWith ("tion") || lower.endsWith ("sion"))) stressAt = n - 2;
            for (int k = 0; k < n; ++k)
            {
                Syllable sy;
                const auto from = k == 0 ? 0 : groupStarts[(size_t) k] - (groupStarts[(size_t) k] > groupStarts[(size_t) k - 1] + 1 ? 1 : 0);
                const auto to = k + 1 < n ? groupStarts[(size_t) k + 1] - (groupStarts[(size_t) k + 1] > groupStarts[(size_t) k] + 1 ? 1 : 0) : word.length();
                sy.text = word.substring (from, to) + (k + 1 < n ? "-" : "");
                sy.stressed = n == 1 ? ! weak.contains (lower) : k == stressAt;
                sy.wordEnd = k == n - 1;
                sy.phraseEnd = sy.wordEnd && phraseEnd;
                out.push_back (sy);
            }
        }
        return out;
    }
    struct Topline { std::vector<Note> notes; juce::StringArray lyric; juce::String shape; float singable = 0.0f; };
    /** A topline: stressed syllables on the beat (chord tones), the weak ones between (stepwise),
        the line's peak on a stressed syllable about two thirds in, the last held. `bars` 2 or 4. */
    inline Topline topline (const std::vector<Syllable>& syl, const Settings& s, int bars, uint32_t seed, double startQ = 0.0)
    {
        Topline t;
        if (syl.empty()) return t;
        const auto total16 = 16 * juce::jlimit (1, 8, bars);
        // rhythm (in 16ths): stressed on beats, weak on 8ths, crowded lines fall back to 16ths
        std::vector<int> at;
        int pos = 0;
        const auto crowded = (int) syl.size() > total16 / 3;
        for (size_t i = 0; i < syl.size(); ++i)
        {
            if (syl[i].stressed && pos % 4 != 0 && ! crowded) pos += 4 - pos % 4;
            at.push_back (pos);
            pos += syl[i].stressed ? (crowded ? 2 : 4) : (crowded ? 1 : 2);
            if (syl[i].phraseEnd && i + 1 < syl.size()) pos += 2;   // a breath
        }
        if (at.back() >= total16)   // squeeze into the bars
        {
            const auto k = (double) (total16 - 4) / (double) std::max (1, at.back());
            for (auto& a : at) a = (int) std::floor (a * k);
            for (size_t i = 1; i < at.size(); ++i) at[i] = std::max (at[i], at[i - 1] + 1);
        }
        // pitch: an arc through chord tones, the peak on a stressed syllable about two thirds in
        juce::Random rng ((juce::int64) seed * 7919 + 11);
        const auto shapeKind = (int) (seed % 3);   // 0 arch, 1 rising, 2 falling-then-lifting
        t.shape = shapeKind == 0 ? "an arch" : shapeKind == 1 ? "rising to the end" : "falling, then lifting";
        size_t peakIdx = 0; float bestDist = 1.0e9f;
        for (size_t i = 0; i < syl.size(); ++i)
            if (syl[i].stressed && std::abs ((float) i / (float) syl.size() - (shapeKind == 1 ? 0.85f : 0.65f)) < bestDist) { bestDist = std::abs ((float) i / (float) syl.size() - (shapeKind == 1 ? 0.85f : 0.65f)); peakIdx = i; }
        const auto centre = 12 * 5 + s.root;                       // around C4-B4 plus the key: a comfortable vocal register
        int prev = writing::snapToScale (s, centre + (shapeKind == 2 ? 7 : 0) + (int) (rng.nextInt (3)) - 1);
        for (size_t i = 0; i < syl.size(); ++i)
        {
            const auto q = startQ + at[i] * 0.25;
            const auto pcs = chordTonesAt (s, q);
            const auto progress = (float) i / (float) std::max<size_t> (1, syl.size() - 1);
            float target;
            if (shapeKind == 0) target = (float) centre + 7.0f * std::sin (juce::MathConstants<float>::pi * std::min (1.0f, progress / 0.65f * 0.5f + (progress > 0.65f ? 0.5f : 0.0f)));
            else if (shapeKind == 1) target = (float) centre + 9.0f * progress;
            else target = (float) centre + 7.0f - 9.0f * std::sin (juce::MathConstants<float>::pi * progress);
            if (i == peakIdx) target += 3.0f;
            int p;
            if (syl[i].stressed || i == 0 || i + 1 == syl.size())
                p = writing::nearestTone (pcs, (int) std::lround (target));                     // on the chord
            else
                p = writing::scaleMove (s, prev, (int) std::lround (target) > prev ? 1 : (int) std::lround (target) < prev ? -1 : 0);   // a step toward it
            if (std::abs (p - prev) > 9) p += p > prev ? -12 : 12;
            const auto end16 = i + 1 < at.size() ? at[i + 1] : total16;
            const auto len = std::max (1, (syl[i].phraseEnd ? end16 : std::min (end16, at[i] + (syl[i].stressed ? 4 : 2))) - at[i]);
            t.notes.push_back ({ p, q, len * 0.25 * 0.95, syl[i].stressed ? 100 : 82 });
            t.lyric.add (syl[i].text);
            prev = p;
        }
        // the last syllable lands on the root or the third of its chord, held
        if (! t.notes.empty())
        {
            auto& last = t.notes.back();
            const auto pcs = chordTonesAt (s, last.start);
            if (! pcs.empty()) last.pitch = writing::nearestTone ({ pcs[0], pcs.size() > 1 ? pcs[1] : pcs[0] }, last.pitch);
            last.length = std::max (last.length, startQ + total16 * 0.25 - last.start - 0.1);
        }
        t.notes = writing::singable (t.notes, s);
        t.singable = writing::singability (t.notes);
        return t;
    }
    /** A topline as a MIDI file with its lyrics (lyric meta events, one per note). */
    inline juce::MidiFile toplineMidi (const Topline& t, double bpm)
    {
        juce::MidiMessageSequence seq;
        constexpr int ppq = 960;
        seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jmax (1.0, bpm))), 0.0);
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Topline"), 0.0);
        for (size_t i = 0; i < t.notes.size(); ++i)
        {
            const auto& n = t.notes[i];
            const auto on = n.start * ppq, off = (n.start + n.length) * ppq;
            if (i < (size_t) t.lyric.size()) seq.addEvent (juce::MidiMessage::textMetaEvent (5, t.lyric[(int) i]), on);
            seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) juce::jlimit (1, 127, n.velocity)), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), off);
        }
        seq.updateMatchedPairs();
        juce::MidiFile f;
        f.setTicksPerQuarterNote (ppq);
        f.addTrack (seq);
        return f;
    }

    //==============================================================================
    // Originality
    /** How alike two melodies are, 0..1: their interval sequences (any key) and their rhythms. */
    inline float melodicSimilarity (const std::vector<Note>& aIn, const std::vector<Note>& bIn)
    {
        const auto a = window (rebased (aIn), 0.0, 8.0), b = window (rebased (bIn), 0.0, 8.0);
        if (a.size() < 3 || b.size() < 3) return 0.0f;
        std::vector<int> ia, ib;
        for (size_t i = 1; i < a.size(); ++i) ia.push_back (a[i].pitch - a[i - 1].pitch);
        for (size_t i = 1; i < b.size(); ++i) ib.push_back (b[i].pitch - b[i - 1].pitch);
        // the longest run of identical intervals
        int longest = 0;
        for (size_t i = 0; i < ia.size(); ++i)
            for (size_t j = 0; j < ib.size(); ++j)
            {
                int k = 0;
                while (i + (size_t) k < ia.size() && j + (size_t) k < ib.size() && ia[i + (size_t) k] == ib[j + (size_t) k]) ++k;
                longest = std::max (longest, k);
            }
        const auto intervalSim = (float) longest / (float) std::min (ia.size(), ib.size());
        std::set<int> ra, rb;
        for (const auto& n : a) ra.insert ((int) std::lround (n.start * 4.0));
        for (const auto& n : b) rb.insert ((int) std::lround (n.start * 4.0));
        int inter = 0; for (auto x : ra) inter += rb.count (x) ? 1 : 0;
        const auto rhythmSim = (float) inter / (float) std::max<size_t> (1, ra.size() + rb.size() - (size_t) inter);
        return 0.65f * intervalSim + 0.35f * rhythmSim;
    }
    struct NearRepeat { juce::String name; float similarity = 0.0f; };
    inline std::vector<NearRepeat> nearRepeats (const std::vector<Note>& hook, const std::vector<std::pair<juce::String, std::vector<Note>>>& candidates, float threshold = 0.75f)
    {
        std::vector<NearRepeat> out;
        for (const auto& [name, notes] : candidates)
            if (const auto sim = melodicSimilarity (hook, notes); sim >= threshold) out.push_back ({ name, sim });
        std::sort (out.begin(), out.end(), [] (const NearRepeat& x, const NearRepeat& y) { return x.similarity > y.similarity; });
        return out;
    }

    //==============================================================================
    // Emotional arc
    struct ArcPlan
    {
        juce::String name, story;                                   // the feelings, in words
        std::vector<std::pair<juce::String, float>> params;        // settings it sets (Undo puts them back)
        std::vector<std::pair<int, Quality>> qualities;             // chord colours (slot, quality)
        juce::StringArray why;                                     // the theory, one line each
    };
    inline std::vector<ArcPlan> arcPlans (const Settings& s)
    {
        const auto minor = s.scale != 1;
        std::vector<ArcPlan> out;
        {
            ArcPlan p; p.name = "Classic uplifting";
            p.story = "calm intro, yearning breakdown, rising build, euphoric drop, the last drop lifted";
            p.params = { { Params::songBreakdown, 1.0f }, { Params::writingId (2), 1.0f }, { Params::writingId (0), 1.0f }, { Params::songLift, 1.0f } };
            p.qualities = { { 0, Quality::Add9 } };
            p.why = { "the breakdown opens on VI: the same notes, a softer home - it yearns",
                      "tension notes and suspensions into each drop (Writing > Harmony): the build leans forward",
                      "the add9 on the home chord: open and bright where it releases",
                      "the lead an octave up in the later drops: the peak is saved for last" };
            out.push_back (p);
        }
        {
            ArcPlan p; p.name = "Dark to light";
            p.story = "a dark, driving start, a breakdown that turns major, a hopeful release";
            p.params = { { Params::songBreakdown, 3.0f }, { Params::writingId (2), 1.0f }, { Params::songLift, 1.0f } };
            p.qualities = minor ? std::vector<std::pair<int, Quality>> { { 0, Quality::Minor } } : std::vector<std::pair<int, Quality>> {};
            p.why = { "the breakdown lifts to III halfway: the relative major, light breaking through",
                      "the home chord stays plain minor: the darkness is the contrast",
                      "tensions into the drop, then the lead lifted: the light wins at the end" };
            out.push_back (p);
        }
        {
            ArcPlan p; p.name = "Melancholic";
            p.story = "bittersweet throughout: a breakdown on IV, suspended colours, no big lift";
            p.params = { { Params::songBreakdown, 2.0f }, { Params::writingId (2), 1.0f }, { Params::songLift, 0.0f } };
            p.qualities = { { std::max (0, std::min (s.count, maxSlots) - 1), Quality::Sus4 } };
            p.why = { "the breakdown starts on IV: away from home, looking back",
                      "the last chord suspended: each loop hangs before it resolves",
                      "no octave lift: the drop stays intimate rather than triumphant" };
            out.push_back (p);
        }
        {
            ArcPlan p; p.name = "Driving";
            p.story = "relentless: the same progression in the breakdown, tension carried, little release";
            p.params = { { Params::songBreakdown, 0.0f }, { Params::writingId (2), 1.0f }, { Params::songLift, 0.0f } };
            p.qualities = { { 0, Quality::Power } };
            p.why = { "the breakdown keeps the drop's progression: no change of scene",
                      "power chords at home: hard and neutral, neither happy nor sad",
                      "tensions without a lift: momentum rather than arrival" };
            out.push_back (p);
        }
        return out;
    }
    /** The chords each kind of section will play with a plan's colours, in numerals. */
    inline juce::String arcChords (const Settings& sIn, const ArcPlan& p)
    {
        auto s = sIn;
        for (const auto& [slot, q] : p.qualities) if (slot >= 0 && slot < maxSlots) s.slots[(size_t) slot].quality = q;
        juce::StringArray drop, bd;
        for (int i = 0; i < juce::jlimit (1, maxSlots, s.count); ++i) drop.add (romanNumeral (s, s.slots[(size_t) i]));
        float bdStart = 0.0f;
        for (const auto& [id, v] : p.params) if (id == Params::songBreakdown) bdStart = v;
        const auto b = bdStart >= 0.5f && bdStart < 1.5f ? startingOn (s, 6) : bdStart >= 1.5f && bdStart < 2.5f ? startingOn (s, 4) : s;
        for (int i = 0; i < juce::jlimit (1, maxSlots, b.count); ++i) bd.add (romanNumeral (b, b.slots[(size_t) i]));
        return "drops: " + drop.joinIntoString (" - ") + "    breakdowns: " + bd.joinIntoString (" - ") + (bdStart >= 2.5f ? " (lifting to III halfway)" : "");
    }

    //==============================================================================
    // Trance harmony moves
    struct Move { juce::String name, why; Settings result; };
    inline std::vector<Move> harmonyMoves (const Settings& s)
    {
        std::vector<Move> out;
        const auto n = juce::jlimit (1, maxSlots, s.count);
        const auto minor = s.scale != 1;
        auto add = [&] (const juce::String& name, const juce::String& why, Settings r)
        {
            bool changed = false;
            for (int i = 0; i < n; ++i) changed = changed || r.slots[(size_t) i].degree != s.slots[(size_t) i].degree || r.slots[(size_t) i].quality != s.slots[(size_t) i].quality;
            if (changed) out.push_back ({ name, why, r });
        };
        if (minor && n >= 3)
        {
            auto r = s; r.slots[(size_t) n - 2] = { 6, Quality::Major }; r.slots[(size_t) n - 1] = { 7, Quality::Major };
            add ("The VI - VII - i lift", "the loop ends VI then VII, climbing a step each time into home: the classic trance lift (the step up feels like rising)", r);
        }
        if (! minor && n >= 3)
        {
            auto r = s; r.slots[(size_t) n - 1] = { 4, Quality::Minor };
            add ("The borrowed iv", "the IV turns minor before home (borrowed from the minor key): a bittersweet catch in the throat", r);
        }
        if (n >= 2)
        {
            auto r = s; r.slots[(size_t) n - 1].quality = Quality::Sus4;
            add ("Suspend the turnaround", "the last chord's third held back as a 4th: the loop hangs, then resolves on the downbeat", r);
        }
        {
            auto r = s; r.slots[0].quality = Quality::Add9;
            add ("Add the 9th at home", "the home chord with its 9th: open and shimmering, the sound of a trance pad", r);
        }
        if (minor && n >= 2)
        {
            auto r = s; r.slots[0] = { 6, Quality::Major };
            add ("Start on VI", "the progression opens on VI: the same notes as home but softer - the breakdown colour", r);
        }
        if (minor && n >= 4)
        {
            auto r = s; r.slots[0] = { 1, Quality::Minor }; r.slots[1] = { 7, Quality::Major }; r.slots[2] = { 6, Quality::Major }; r.slots[3] = { 7, Quality::Major };
            add ("i - VII - VI - VII (the pendulum)", "home, a step down, another, back up: the bass rocks like a pendulum under a held melody note", r);
        }
        if (n >= 2)
        {
            auto r = s;
            for (int i = 0; i < n; ++i) if (r.slots[(size_t) i].degree == 5) r.slots[(size_t) i].quality = minor ? Quality::Major : Quality::Seventh;
            add ("Make the V pull", minor ? "the V turns major (harmonic minor): its raised 7th leans hard into home" : "the V gets its 7th: a stronger pull home", r);
        }
        return out;
    }

    //==============================================================================
    // Feel from a reference (statistics only: swing and accents, never notes)
    struct Feel
    {
        bool valid = false;
        float swing = 0.0f;                  // the off 16ths late by this share of a 16th
        std::array<float, 16> accent {};     // relative loudness of each 16th in a bar (max 1)
        double bpm = 0.0;
    };
    inline Feel feelFromAudio (const juce::AudioBuffer<float>& audio, double sr, double bpmHint)
    {
        Feel f;
        const auto n = audio.getNumSamples();
        if (n < (int) (sr * 4.0)) return f;
        // an onset envelope: rectified positive energy change in 5 ms hops of the high band
        const auto hop = std::max (1, (int) (sr * 0.005));
        std::vector<float> env;
        float lp = 0.0f, prevE = 0.0f;
        const auto a = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 1500.0f / (float) sr);
        for (int i = 0; i + hop <= n; i += hop)
        {
            double e = 0.0;
            for (int k = 0; k < hop; ++k)
            {
                float x = 0.0f;
                for (int c = 0; c < audio.getNumChannels(); ++c) x += audio.getSample (c, i + k);
                lp += a * (x - lp);
                const auto hpx = x - lp;
                e += (double) hpx * hpx;
            }
            const auto ee = (float) std::sqrt (e / hop);
            env.push_back (std::max (0.0f, ee - prevE));
            prevE = ee;
        }
        // the tempo: the hint if the envelope agrees, else the best between 110 and 160
        const auto hopSec = (double) hop / sr;
        auto score = [&] (double bpm)
        {
            const auto period = 60.0 / bpm / hopSec;
            double acc = 0.0;
            for (size_t i = 0; i + (size_t) (period * 4) < env.size(); ++i) acc += (double) env[i] * env[i + (size_t) std::lround (period)];
            return acc;
        };
        double bestBpm = bpmHint > 0.0 ? bpmHint : 138.0, best = -1.0;
        for (double b = 110.0; b <= 160.0; b += 0.5) if (const auto sc = score (b) * (std::abs (b - bpmHint) < 3.0 ? 1.15 : 1.0); sc > best) { best = sc; bestBpm = b; }
        f.bpm = bestBpm;
        // the beat's phase: where the envelope peaks on the quarter grid
        const auto quarter = 60.0 / bestBpm / hopSec, sixteenth = quarter / 4.0;
        double bestPhase = 0.0, bestSum = -1.0;
        for (double ph = 0.0; ph < quarter; ph += 1.0)
        {
            double sum = 0.0;
            for (double t = ph; t < (double) env.size(); t += quarter) sum += env[(size_t) t];
            if (sum > bestSum) { bestSum = sum; bestPhase = ph; }
        }
        // fold the onsets onto a bar's 16ths; the odd 16ths' average lateness is the swing
        std::array<double, 16> sum {}; std::array<int, 16> cnt {};
        double late = 0.0, lateW = 0.0;
        for (size_t i = 0; i < env.size(); ++i)
        {
            if (env[i] <= 0.0f) continue;
            const auto pos = ((double) i - bestPhase) / sixteenth;
            if (pos < 0.0) continue;
            const auto step = (int) std::lround (pos);
            const auto off = pos - step;                      // -0.5..0.5 of a 16th
            const auto k = ((step % 16) + 16) % 16;
            sum[(size_t) k] += env[i]; ++cnt[(size_t) k];
            if (k % 2 == 1 && std::abs (off) < 0.45) { late += off * env[i]; lateW += env[i]; }
        }
        float top = 0.0f;
        for (int k = 0; k < 16; ++k) { f.accent[(size_t) k] = cnt[(size_t) k] > 0 ? (float) sum[(size_t) k] : 0.0f; top = std::max (top, f.accent[(size_t) k]); }
        if (top <= 0.0f) return f;
        for (auto& v : f.accent) v /= top;
        f.swing = lateW > 0.0 ? juce::jlimit (0.0f, 0.6f, (float) (late / lateW)) : 0.0f;
        f.valid = true;
        return f;
    }
    /** A part with a feel: the off 16ths moved late by the swing, each 16th's velocity by its accent. */
    inline std::vector<Note> applyFeel (std::vector<Note> notes, const Feel& f, float amount = 1.0f)
    {
        if (! f.valid || amount <= 0.0f) return notes;
        for (auto& n : notes)
        {
            const auto pos = n.start * 4.0;
            const auto step = (int) std::lround (pos);
            if (std::abs (pos - step) > 0.05) continue;       // off the grid already: leave it
            const auto k = ((step % 16) + 16) % 16;
            if (k % 2 == 1) n.start += amount * f.swing * 0.25;
            n.velocity = juce::jlimit (1, 127, (int) std::lround (n.velocity * (1.0f - amount * 0.35f * (1.0f - f.accent[(size_t) k]))));
        }
        return notes;
    }

    //==============================================================================
    // Rhythmic identity
    /** The hook's rhythmic cell: which 16ths of its first bar it starts notes on. */
    inline uint32_t rhythmCell (const std::vector<Note>& hook)
    {
        uint32_t cell = 0;
        for (const auto& n : window (rebased (hook), 0.0, 4.0)) cell |= 1u << juce::jlimit (0, 15, (int) std::lround (n.start * 4.0));
        return cell;
    }
    /** A part leaning on the cell: notes on the cell's 16ths accented, the others softer. */
    inline std::vector<Note> lockToCell (std::vector<Note> part, uint32_t cell)
    {
        if (cell == 0) return part;
        for (auto& n : part)
        {
            const auto pos = n.start * 4.0;
            const auto step = (int) std::lround (pos);
            const auto onCell = std::abs (pos - step) < 0.1 && (cell & (1u << (((step % 16) + 16) % 16))) != 0;
            n.velocity = juce::jlimit (1, 127, n.velocity + (onCell ? 15 : -10));
        }
        return part;
    }

    //==============================================================================
    // Taste
    struct Features { float density = 0.0f, range = 0.0f, steps = 0.0f, registerMid = 0.0f, offbeat = 0.0f, length = 0.0f; };
    inline Features featuresOf (const std::vector<Note>& v)
    {
        Features f;
        if (v.size() < 2) return f;
        double end = 0.0; int lo = 127, hi = 0, st = 0, off = 0; double len = 0.0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            end = std::max (end, v[i].start + v[i].length); lo = std::min (lo, v[i].pitch); hi = std::max (hi, v[i].pitch); len += v[i].length;
            if (i > 0 && std::abs (v[i].pitch - v[i - 1].pitch) <= 2) ++st;
            if (std::abs (v[i].start - std::round (v[i].start)) > 0.1) ++off;
        }
        f.density = (float) v.size() / (float) std::max (1.0, end / 4.0);
        f.range = (float) (hi - lo);
        f.steps = (float) st / (float) (v.size() - 1);
        auto ps = v; std::sort (ps.begin(), ps.end(), [] (const Note& a, const Note& b) { return a.pitch < b.pitch; });
        f.registerMid = (float) ps[ps.size() / 2].pitch;
        f.offbeat = (float) off / (float) v.size();
        f.length = (float) (len / v.size());
        return f;
    }
    struct Taste
    {
        std::array<double, 6> up {}, down {};
        int ups = 0, downs = 0;
        static std::array<float, 6> vec (const Features& f) { return { f.density / 8.0f, f.range / 24.0f, f.steps, (f.registerMid - 48.0f) / 36.0f, f.offbeat, f.length }; }
        void rate (const Features& f, bool liked)
        {
            const auto v = vec (f);
            for (size_t k = 0; k < 6; ++k) (liked ? up : down)[k] += v[k];
            (liked ? ups : downs)++;
        }
        bool ready() const { return ups >= 2; }
        /** Closer to what you liked, further from what you didn't (lower is better). */
        float distance (const Features& f) const
        {
            const auto v = vec (f);
            float d = 0.0f;
            for (size_t k = 0; k < 6; ++k)
            {
                if (ups > 0) d += (float) std::pow (v[k] - up[k] / ups, 2.0);
                if (downs > 0) d -= 0.5f * (float) std::pow (v[k] - down[k] / downs, 2.0);
            }
            return d;
        }
        juce::String toText() const
        {
            juce::StringArray a;
            for (auto x : up) a.add (juce::String (x, 4));
            for (auto x : down) a.add (juce::String (x, 4));
            a.add (juce::String (ups)); a.add (juce::String (downs));
            return a.joinIntoString (",");
        }
        static Taste fromText (const juce::String& t)
        {
            Taste ta;
            const auto a = juce::StringArray::fromTokens (t, ",", "");
            if (a.size() != 14) return ta;
            for (int k = 0; k < 6; ++k) { ta.up[(size_t) k] = a[k].getDoubleValue(); ta.down[(size_t) k] = a[6 + k].getDoubleValue(); }
            ta.ups = a[12].getIntValue(); ta.downs = a[13].getIntValue();
            return ta;
        }
    };

    //==============================================================================
    // Why it works
    inline juce::StringArray explain (const std::vector<Note>& lineIn, const Settings& s)
    {
        juce::StringArray out;
        const auto line = sorted (lineIn);
        if (line.size() < 3) return out;
        int onBeat = 0, onBeatChord = 0;
        for (const auto& n : line)
            if (std::abs (n.start - std::round (n.start)) < 0.05)
            {
                ++onBeat;
                const auto pcs = chordTonesAt (s, n.start);
                onBeatChord += std::find (pcs.begin(), pcs.end(), (n.pitch % 12 + 12) % 12) != pcs.end() ? 1 : 0;
            }
        if (onBeat > 0)
            out.add (juce::String (juce::roundToInt (100.0f * onBeatChord / onBeat)) + "% of the notes on the beat are chord tones"
                     + (onBeatChord * 4 >= onBeat * 3 ? ": anchored to the harmony, the rest can wander" : ": it rubs against the chords - colour, or a reason to change a note"));
        size_t peak = 0; for (size_t i = 0; i < line.size(); ++i) if (line[i].pitch > line[peak].pitch) peak = i;
        const auto where = (float) peak / (float) (line.size() - 1);
        out.add ("the highest note comes " + juce::String (juce::roundToInt (where * 100.0f)) + "% of the way in"
                 + (where >= 0.5f ? ": a late peak feels like arriving" : ": an early peak, then the line settles"));
        for (size_t i = 1; i + 1 < line.size(); ++i)
        {
            const auto leap = line[i].pitch - line[i - 1].pitch, next = line[i + 1].pitch - line[i].pitch;
            if (std::abs (leap) >= 5 && std::abs (next) <= 2 && (leap > 0) != (next > 0) && next != 0)
            { out.add ("a leap of " + juce::String (std::abs (leap)) + " semitones answered by a step back: tension, then release"); break; }
        }
        const auto& last = line.back();
        const auto pcs = chordTonesAt (s, last.start);
        const auto pc = (last.pitch % 12 + 12) % 12;
        if (! pcs.empty())
            out.add (pc == pcs[0] ? juce::String ("it ends on the chord's root: resolved, a full stop")
                   : pcs.size() > 1 && pc == pcs[1] ? juce::String ("it ends on the chord's third: warm, it could go on")
                   : std::find (pcs.begin(), pcs.end(), pc) != pcs.end() ? juce::String ("it ends on the chord's fifth: open, floating")
                                                                          : juce::String ("it ends off the chord: a question, it wants the next phrase"));
        return out;
    }
}
