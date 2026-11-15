#pragma once

// The writing critic: listens to a rendered song the way a producer would and scores it, 0..1, on
// four things (each the mean of a few plain rules, so a low score says what to fix):
//   hook     the drop's lead: range, steps and filled leaps, a climax late in the phrase that isn't
//            hammered, phrase ends and strong beats on chord tones, a rhythm that repeats with
//            variation, a singable density
//   parts    the parts together: no sustained semitone rubs between parts, melodic parts not stacked
//            in one register and one rhythm, no mud below the bass's register, a counter that answers
//   journey  the sections: intro and breakdown under the drops, a build that rises, every drop at
//            least as big as the one before, the last one the biggest, an outro that lets go, every
//            boundary audible, later drops not copies of the first
//   harmony  the bass on chord tones at the changes, smooth voice leading, open voicings low down,
//            strong-beat lead notes that belong to the chord (or resolve by step), a progression
//            with somewhere to go
// The song draft writes a few candidates and keeps the best; the tests hold the scores up across
// every mood and style.

#include "ChordEngine.h"
#include "Alias.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace chords::critic
{
    struct Report
    {
        float hook = 0.0f, parts = 0.0f, journey = 0.0f, harmony = 0.0f, total = 0.0f;
        juce::StringArray notes;   // the rules that scored lowest, in words
        std::map<juce::String, float> rules;
    };

    namespace detail
    {
        /** 1 inside [lo, hi], falling to 0 over `soft` outside it. */
        inline float band (float v, float lo, float hi, float soft)
        {
            if (v < lo) return juce::jmax (0.0f, 1.0f - (lo - v) / soft);
            if (v > hi) return juce::jmax (0.0f, 1.0f - (v - hi) / soft);
            return 1.0f;
        }

        inline int sectionAt (const suite::SectionMap& map, double q)
        {
            const auto bar = (int) std::floor (q / 4.0) + 1;
            for (int i = 0; i < map.count; ++i)
                if (bar >= map.sections[(size_t) i].startBar && bar < map.endBar (i)) return i;
            return -1;
        }

        inline bool chordTone (const Settings& s, const suite::SectionMap& map, int pitch, double q)
        {
            const auto i = sectionAt (map, q);
            const auto shift = i >= 0 ? map.keyShiftOf (i) : 0;
            const auto pc = ((pitch - shift) % 12 + 12) % 12;
            for (auto t : chordTonesAt (s, q)) if (t == pc) return true;
            return false;
        }

        inline std::vector<Note> window (const std::vector<Note>& notes, double from, double to)
        {
            std::vector<Note> out;
            for (const auto& n : notes) if (n.start >= from - 1.0e-6 && n.start < to - 1.0e-6) out.push_back (n);
            std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
            return out;
        }

        inline std::pair<double, double> sectionQ (const suite::SectionMap& map, int i)
        {
            return { 4.0 * (map.sections[(size_t) i].startBar - 1), 4.0 * (map.endBar (i) - 1) };
        }

        inline int firstOf (const suite::SectionMap& map, suite::SectionType t)
        {
            for (int i = 0; i < map.count; ++i) if (map.sections[(size_t) i].type == t) return i;
            return -1;
        }

        struct Rules
        {
            std::map<juce::String, float> r;
            void add (const juce::String& name, float v) { r[name] = juce::jlimit (0.0f, 1.0f, v); }
            float mean (const juce::String& prefix) const
            {
                float sum = 0.0f; int n = 0;
                for (const auto& [k, v] : r) if (k.startsWith (prefix)) { sum += v; ++n; }   // ("raw." values are for reports)
                return n > 0 ? sum / (float) n : 1.0f;
            }
        };

        /** A rule's name, for your taste. */
        inline juce::String ruleName (const juce::String& rule)
        {
            static const std::map<juce::String, juce::String> names {
                { "hook.range", "the hook's range" }, { "hook.steps", "a hook that moves by steps and thirds" }, { "hook.gapfill", "leaps answered by a step back" },
                { "hook.climax", "a late climax" }, { "hook.ends", "phrases ending on the chord" }, { "hook.strong", "strong beats on chord tones" },
                { "hook.motif", "a rhythm that repeats" }, { "hook.density", "a singable density" }, { "parts.rubs", "no rubs between parts" },
                { "parts.crowd", "melodic parts apart" }, { "parts.mud", "a clear low end" }, { "parts.answer", "a counter that answers" },
                { "journey.intro", "a light intro" }, { "journey.breakdown", "a breakdown that drops away" }, { "journey.build", "a rising build" },
                { "journey.drops", "bigger later drops" }, { "journey.outro", "an outro that lets go" }, { "journey.edges", "audible section changes" },
                { "journey.repeat", "later drops that develop" }, { "harmony.bass", "the bass on the chord" }, { "harmony.leading", "smooth voice leading" },
                { "harmony.voicing", "open low voicings" }, { "harmony.lead", "the hook on its chords" }, { "harmony.motion", "a progression that moves" } };
            const auto it = names.find (rule);
            return it != names.end() ? it->second : rule;
        }

        /** The words for a rule that scored low. */
        inline juce::String explain (const juce::String& rule)
        {
            static const std::map<juce::String, juce::String> words {
                { "hook.range", "the hook's range is too narrow or too wide (a 5th to an octave and a 4th)" },
                { "hook.steps", "the hook leaps too much: steps and the chord's thirds, with few leaps of a 4th or more" },
                { "hook.gapfill", "the hook's leaps aren't answered by a step back the other way" },
                { "hook.climax", "the hook peaks too early (or keeps hitting its top note)" },
                { "hook.ends", "the hook's phrases don't end on a chord tone" },
                { "hook.strong", "the hook's strong beats aren't on chord tones" },
                { "hook.motif", "the hook's rhythm doesn't repeat enough to stick (or never varies)" },
                { "hook.density", "the hook is too busy or too sparse to sing" },
                { "parts.rubs", "parts hold semitone rubs against each other" },
                { "parts.crowd", "melodic parts sit in the same register with the same rhythm" },
                { "parts.mud", "too many notes below the bass's register" },
                { "parts.answer", "the counter-melody plays with the hook instead of answering it" },
                { "journey.intro", "the intro is nearly as full as the drop" },
                { "journey.breakdown", "the breakdown doesn't drop away from the drops" },
                { "journey.build", "the build doesn't rise" },
                { "journey.drops", "a later drop is smaller than the one before" },
                { "journey.outro", "the outro doesn't let go" },
                { "journey.edges", "some section changes can't be heard" },
                { "journey.repeat", "the later drops copy the first" },
                { "harmony.bass", "the bass misses the chord at the changes" },
                { "harmony.leading", "the chords jump between voicings" },
                { "harmony.voicing", "the chords are voiced too close down low (mud)" },
                { "harmony.lead", "the hook's strong beats clash with the chords" },
                { "harmony.motion", "the progression has nowhere to go (too few chords)" } };
            const auto it = words.find (rule);
            return it != words.end() ? it->second : rule;
        }
    }

    /** The song's parts as rendered (main parts and whatever library layers play). */
    inline std::map<SongPart, std::vector<Note>> renderParts (const Settings& s, const suite::SectionMap& map, const SongPatterns& sp)
    {
        std::map<SongPart, std::vector<Note>> parts;
        for (int p = (int) SongPart::Chords; p <= (int) SongPart::Guitar; ++p)
        {
            const auto part = (SongPart) p;
            if (part == SongPart::Harmony) continue;
            auto notes = songClip (s, map, sp, part);
            if (! notes.empty()) parts[part] = std::move (notes);
        }
        return parts;
    }

    /** Your taste: a weight per rule (1 = as written), learned from the versions you picked. */
    using Weights = std::map<juce::String, float>;

    inline Report judge (const Settings& s, const suite::SectionMap& map, const std::map<SongPart, std::vector<Note>>& parts, const Weights* taste = nullptr)
    {
        using namespace detail;
        using ST = suite::SectionType;
        Rules R;
        const auto drop1 = firstOf (map, ST::Drop);
        auto partNotes = [&] (SongPart p) -> const std::vector<Note>& { static const std::vector<Note> none; const auto it = parts.find (p); return it != parts.end() ? it->second : none; };
        // A chord tone is one of the notes the chords actually play over that chord (breakdown
        // re-harmonisations, tensions into the drops and the key change included); where the chords
        // rest, the progression's own chord.
        const auto& chordPart = partNotes (SongPart::Chords);
        auto isChordTone = [&] (int pitch, double q)
        {
            const auto L = s.quartersPerChord > 0.0 ? s.quartersPerChord : 4.0;
            const auto c0 = std::floor (q / L + 1.0e-9) * L;
            bool any = false;
            for (const auto& n : chordPart)
                if (n.start >= c0 - 1.0e-6 && n.start < c0 + L - 1.0e-6)
                {
                    any = true;
                    if (((n.pitch - pitch) % 12 + 12) % 12 == 0) return true;
                }
            return any ? false : chordTone (s, map, pitch, q);
        };

        //==============================================================================
        // Hook: the first 8 bars of the first drop
        if (drop1 >= 0)
        {
            const auto [from, to] = sectionQ (map, drop1);
            const auto hook = window (partNotes (SongPart::Lead), from, juce::jmin (to, from + 32.0));
            if (hook.size() < 8)
            {
                R.add ("hook.density", 0.2f);
            }
            else
            {
                int lo = 127, hi = 0;
                for (const auto& n : hook) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
                R.add ("hook.range", band ((float) (hi - lo), 7.0f, 17.0f, 6.0f));   // a 5th to an octave and a 4th
                R.r["raw.range"] = (float) (hi - lo);
                int steps = 0, moves = 0, leaps = 0, filled = 0, bigLeaps = 0;
                for (size_t i = 1; i < hook.size(); ++i)
                {
                    const auto d = hook[i].pitch - hook[i - 1].pitch;
                    if (d == 0) continue;
                    ++moves;
                    if (std::abs (d) <= 2) ++steps;
                    if (std::abs (d) >= 5) ++bigLeaps;   // a 4th or more (thirds are the chord's own skips: trance hooks arpeggiate)
                    if (std::abs (d) >= 5 && i + 1 < hook.size())
                    {
                        ++leaps;
                        const auto next = hook[i + 1].pitch - hook[i].pitch;
                        if (next * d < 0 || next == 0) ++filled;   // turns back (or holds) after the leap
                    }
                }
                R.add ("hook.steps", moves > 0 ? band ((float) bigLeaps / (float) moves, 0.0f, 0.35f, 0.3f) * (steps + bigLeaps == moves && bigLeaps == 0 && steps == moves ? 0.8f : 1.0f) : 0.2f);
                R.r["raw.steps"] = moves > 0 ? (float) steps / (float) moves : 0.0f;
                R.add ("hook.gapfill", leaps > 0 ? band ((float) filled / (float) leaps, 0.6f, 1.0f, 0.6f) : 1.0f);
                // the climax: its first arrival late-ish in the 8 bars, and not hammered
                double firstTop = -1.0; int tops = 0;
                for (const auto& n : hook) if (n.pitch == hi) { ++tops; if (firstTop < 0.0) firstTop = n.start - from; }
                R.add ("hook.climax", band ((float) (firstTop / 32.0), 0.3f, 0.9f, 0.3f) * (tops <= 4 ? 1.0f : 0.7f));
                // phrase ends (each 4 bars) on chord tones
                float ends = 0.0f; int phrases = 0;
                for (double ph = from; ph < from + 32.0 - 1.0e-6; ph += 16.0)
                {
                    const Note* last = nullptr;
                    for (const auto& n : hook) if (n.start >= ph && n.start < ph + 15.0) last = &n;   // (a pickup in the last beat leads on)
                    if (last == nullptr) continue;
                    ++phrases;
                    ends += isChordTone (last->pitch, last->start) ? 1.0f : 0.0f;
                }
                R.add ("hook.ends", phrases > 0 ? ends / (float) phrases : 0.5f);
                int strong = 0, strongTones = 0;
                for (const auto& n : hook)
                    if (std::abs (n.start - std::round (n.start)) < 0.01) { ++strong; strongTones += isChordTone (n.pitch, n.start) ? 1 : 0; }
                R.add ("hook.strong", strong > 0 ? band ((float) strongTones / (float) strong, 0.7f, 1.0f, 0.4f) : 0.5f);
                // the rhythm bar by bar: some bars share a rhythm (it sticks), not all (it develops)
                std::array<uint32_t, 8> rhythm {};
                for (const auto& n : hook)
                {
                    const auto b = (int) std::floor ((n.start - from) / 4.0);
                    if (b < 0 || b >= 8) continue;
                    const auto step = juce::jlimit (0, 15, (int) std::lround (std::fmod (n.start - from, 4.0) * 4.0));
                    rhythm[(size_t) b] |= 1u << step;
                }
                int repeats = 0, bars = 0;
                for (int b = 0; b < 8; ++b)
                {
                    if (rhythm[(size_t) b] == 0) continue;
                    ++bars;
                    for (int o = 0; o < 8; ++o) if (o != b && rhythm[(size_t) o] == rhythm[(size_t) b]) { ++repeats; break; }
                }
                R.add ("hook.motif", bars > 0 ? band ((float) repeats / (float) bars, 0.35f, 0.85f, 0.35f) : 0.3f);
                R.add ("hook.density", band ((float) hook.size() / 8.0f, 2.5f, 9.0f, 3.0f));
            }
        }

        //==============================================================================
        // Parts together: the first drop and the first breakdown
        {
            float rubs = 0.0f, crowd = 0.0f, mud = 0.0f; int windows = 0;
            for (auto type : { ST::Drop, ST::Breakdown })
            {
                const auto i = firstOf (map, type);
                if (i < 0) continue;
                ++windows;
                const auto [from, to] = sectionQ (map, i);
                const auto end = juce::jmin (to, from + 32.0);
                // sustained notes sounding at each 8th, by part
                int points = 0, rubbing = 0, muddy = 0;
                for (double q = from; q < end - 1.0e-6; q += 0.5)
                {
                    std::vector<std::pair<SongPart, int>> sounding;
                    int lowNotes = 0;
                    for (const auto& [part, notes] : parts)
                        for (const auto& n : notes)
                            if (n.start <= q + 1.0e-6 && n.start + n.length > q + 0.25 && n.length >= 0.5)
                            {
                                sounding.push_back ({ part, n.pitch });
                                if (n.pitch < 52 && part != SongPart::Bass && part != SongPart::Timpani && part != SongPart::Acid) ++lowNotes;
                            }
                    if (sounding.empty()) continue;
                    ++points;
                    bool rub = false;
                    for (size_t a = 0; a < sounding.size() && ! rub; ++a)
                        for (size_t b = a + 1; b < sounding.size() && ! rub; ++b)
                        {
                            if (sounding[a].first == sounding[b].first) continue;
                            const auto d = std::abs (sounding[a].second - sounding[b].second);
                            if ((d == 1 || d == 13) && juce::jmin (sounding[a].second, sounding[b].second) > 52) rub = true;   // minor 2nds and 9ths (a major 7th is a maj7 or m9 pad)
                        }
                    rubbing += rub ? 1 : 0;
                    muddy += lowNotes >= 2 ? 1 : 0;
                }
                rubs += points > 0 ? band ((float) rubbing / (float) points, 0.0f, 0.04f, 0.15f) : 1.0f;
                mud += points > 0 ? band ((float) muddy / (float) points, 0.0f, 0.1f, 0.3f) : 1.0f;
                // melodic parts in one register with one rhythm
                std::vector<std::pair<int, std::vector<double>>> melodic;   // median pitch, onsets
                for (auto p : { SongPart::Arp, SongPart::Lead, SongPart::Counter, SongPart::Chops, SongPart::Candy, SongPart::Bells, SongPart::Guitar })
                {
                    const auto w = window (partNotes (p), from, end);
                    if (w.size() < 4) continue;
                    std::vector<int> pitches; std::vector<double> on;
                    for (const auto& n : w) { pitches.push_back (n.pitch); on.push_back (n.start); }
                    std::nth_element (pitches.begin(), pitches.begin() + (long) pitches.size() / 2, pitches.end());
                    melodic.push_back ({ pitches[pitches.size() / 2], on });
                }
                int crowded = 0;
                for (size_t a = 0; a < melodic.size(); ++a)
                    for (size_t b = a + 1; b < melodic.size(); ++b)
                    {
                        if (std::abs (melodic[a].first - melodic[b].first) > 4) continue;
                        const auto& sparse = melodic[a].second.size() <= melodic[b].second.size() ? melodic[a].second : melodic[b].second;
                        const auto& dense = &sparse == &melodic[a].second ? melodic[b].second : melodic[a].second;
                        int together = 0;
                        for (auto t : sparse) for (auto u : dense) if (std::abs (t - u) < 0.01) { ++together; break; }
                        if ((float) together / (float) sparse.size() > 0.6f) ++crowded;
                    }
                crowd += juce::jmax (0.0f, 1.0f - 0.34f * (float) crowded);
            }
            if (windows > 0)
            {
                R.add ("parts.rubs", rubs / (float) windows);
                R.add ("parts.crowd", crowd / (float) windows);
                R.add ("parts.mud", mud / (float) windows);
            }
            // the counter answers the hook (its notes mostly where the hook isn't starting one)
            if (drop1 >= 0)
            {
                const auto [from, to] = sectionQ (map, drop1);
                const auto counter = window (partNotes (SongPart::Counter), from, to), lead = window (partNotes (SongPart::Lead), from, to);
                if (counter.size() >= 4 && ! lead.empty())
                {
                    int with = 0;
                    for (const auto& c : counter) for (const auto& l : lead) if (std::abs (c.start - l.start) < 0.01) { ++with; break; }
                    R.add ("parts.answer", band ((float) with / (float) counter.size(), 0.0f, 0.4f, 0.4f));
                }
            }
        }

        //==============================================================================
        // The journey: energy per section (notes per bar, plus the parts playing)
        {
            std::vector<float> energy ((size_t) map.count, 0.0f);
            std::vector<int> active ((size_t) map.count, 0);
            for (int i = 0; i < map.count; ++i)
            {
                const auto [from, to] = sectionQ (map, i);
                const auto bars = juce::jmax (1.0, (to - from) / 4.0);
                int notes = 0;
                for (const auto& [part, ns] : parts)
                {
                    int here = 0;
                    for (const auto& n : ns) if (n.start >= from && n.start < to) ++here;
                    if (here > 0) ++active[(size_t) i];
                    notes += here;
                }
                energy[(size_t) i] = (float) (notes / bars) + 2.0f * (float) active[(size_t) i];
            }
            std::vector<int> drops;
            for (int i = 0; i < map.count; ++i) if (map.sections[(size_t) i].type == ST::Drop) drops.push_back (i);
            const auto dropE = drops.empty() ? 1.0f : energy[(size_t) drops.front()];
            if (const auto intro = firstOf (map, ST::Intro); intro >= 0 && ! drops.empty())
                R.add ("journey.intro", band (energy[(size_t) intro] / juce::jmax (1.0f, dropE), 0.0f, 0.75f, 0.35f));
            if (const auto bd = firstOf (map, ST::Breakdown); bd >= 0 && ! drops.empty())
            {
                float around = 1.0e9f;
                for (auto d : drops) if (std::abs (d - bd) <= 2) around = juce::jmin (around, energy[(size_t) d]);
                if (around > 1.0e8f) around = dropE;
                R.add ("journey.breakdown", band (energy[(size_t) bd] / juce::jmax (1.0f, around), 0.0f, 0.75f, 0.35f));
            }
            if (const auto build = firstOf (map, ST::Build); build >= 0)
            {
                const auto [from, to] = sectionQ (map, build);
                const auto mid = 0.5 * (from + to);
                int first = 0, second = 0;
                for (const auto& [part, ns] : parts)
                    for (const auto& n : ns) { if (n.start >= from && n.start < mid) ++first; else if (n.start >= mid && n.start < to) ++second; }
                R.add ("journey.build", second >= (int) std::ceil (first * 1.1) && second > 0 ? 1.0f : first == 0 && second == 0 ? 0.3f : 0.5f);
            }
            if (drops.size() >= 2)
            {
                float ok = 0.0f;
                for (size_t k = 1; k < drops.size(); ++k) ok += energy[(size_t) drops[k]] >= 0.95f * energy[(size_t) drops[k - 1]] ? 1.0f : 0.0f;
                ok /= (float) (drops.size() - 1);
                const auto lastBiggest = energy[(size_t) drops.back()] >= 1.05f * energy[(size_t) drops.front()] ? 1.0f : 0.6f;
                R.add ("journey.drops", 0.5f * (ok + lastBiggest));
                // later drops aren't copies: the hook's first 8 bars, moved to the first drop's start
                const auto [f0, t0] = sectionQ (map, drops.front());
                const auto [f1, t1] = sectionQ (map, drops.back());
                const auto a = window (partNotes (SongPart::Lead), f0, f0 + 32.0), b = window (partNotes (SongPart::Lead), f1, f1 + 32.0);
                bool same = a.size() == b.size() && ! a.empty();
                const auto shift = map.keyShiftOf (drops.back()) - map.keyShiftOf (drops.front());
                for (size_t k = 0; same && k < a.size(); ++k)
                    same = a[k].pitch + shift == b[k].pitch && std::abs ((a[k].start - f0) - (b[k].start - f1)) < 0.01;
                // other parts count too: a fuller arrangement is a development
                R.add ("journey.repeat", same && active[(size_t) drops.back()] <= active[(size_t) drops.front()] ? 0.6f : 1.0f);
            }
            if (const auto outro = (map.count > 0 && map.sections[(size_t) map.count - 1].type == ST::Outro) ? map.count - 1 : -1; outro >= 0 && ! drops.empty())
                R.add ("journey.outro", band (energy[(size_t) outro] / juce::jmax (1.0f, energy[(size_t) drops.back()]), 0.0f, 0.8f, 0.3f));
            if (map.count >= 2)
            {
                int heard = 0;
                for (int i = 1; i < map.count; ++i)
                {
                    const auto a = energy[(size_t) i - 1], b = energy[(size_t) i];
                    heard += (std::abs (a - b) > 0.1f * juce::jmax (a, b) || active[(size_t) i] != active[(size_t) i - 1]) ? 1 : 0;
                }
                R.add ("journey.edges", (float) heard / (float) (map.count - 1));
            }
        }

        //==============================================================================
        // Harmony
        {
            const auto L = s.quartersPerChord;
            if (drop1 >= 0 && L > 0.0)
            {
                const auto [from, to] = sectionQ (map, drop1);
                const auto bass = window (partNotes (SongPart::Bass), from, to);
                int changes = 0, onTone = 0;
                for (auto t = from; t < to - 1.0e-6; t += L)
                {
                    const Note* at = nullptr;
                    for (const auto& n : bass) if (n.start <= t + 0.26 && n.start + n.length > t - 1.0e-6) { at = &n; if (n.start >= t - 1.0e-6) break; }
                    if (at == nullptr) continue;
                    ++changes;
                    onTone += isChordTone (at->pitch, t + 0.01) ? 1 : 0;
                }
                if (changes > 0) R.add ("harmony.bass", band ((float) onTone / (float) changes, 0.85f, 1.0f, 0.4f));

                // the chords: groups of 3+ notes starting together
                const auto ch = window (partNotes (SongPart::Chords), from, to);
                std::vector<std::vector<int>> groups;
                for (size_t k = 0; k < ch.size();)
                {
                    std::vector<int> g;
                    const auto t = ch[k].start;
                    while (k < ch.size() && std::abs (ch[k].start - t) < 0.01) g.push_back (ch[k++].pitch);
                    if (g.size() >= 3) { std::sort (g.begin(), g.end()); if (groups.empty() || groups.back() != g) groups.push_back (g); }
                }
                if (groups.size() >= 2)
                {
                    float move = 0.0f; int pairs = 0, muddy = 0;
                    for (size_t k = 1; k < groups.size(); ++k)
                    {
                        const auto& a = groups[k - 1]; const auto& b = groups[k];
                        float m = 0.0f;   // each voice of the new chord from the nearest of the last one
                        for (auto pb : b) { int best = 127; for (auto pa : a) best = juce::jmin (best, std::abs (pa - pb)); m += (float) best; }
                        move += m / (float) b.size(); ++pairs;
                    }
                    for (const auto& g : groups)
                        for (size_t v = 1; v < g.size(); ++v)
                            if (g[v] < 55 && g[v] - g[v - 1] < 5) { ++muddy; break; }
                    R.add ("harmony.leading", band (move / (float) pairs, 0.0f, 4.0f, 5.0f));
                    R.add ("harmony.voicing", band ((float) muddy / (float) groups.size(), 0.0f, 0.1f, 0.4f));
                }
                // strong-beat hook notes against the chord (an appoggiatura that resolves by step is fine)
                const auto lead = window (partNotes (SongPart::Lead), from, to);
                int strong = 0, bad = 0;
                for (size_t k = 0; k < lead.size(); ++k)
                {
                    const auto& n = lead[k];
                    if (std::abs (n.start - std::round (n.start)) > 0.01) continue;
                    ++strong;
                    if (isChordTone (n.pitch, n.start)) continue;
                    const auto resolves = k + 1 < lead.size() && std::abs (lead[k + 1].pitch - n.pitch) <= 2 && isChordTone (lead[k + 1].pitch, lead[k + 1].start);
                    bad += resolves ? 0 : 1;
                }
                if (strong > 0) R.add ("harmony.lead", band ((float) bad / (float) strong, 0.0f, 0.15f, 0.3f));
            }
            std::vector<int> distinct;
            for (int k = 0; k < juce::jlimit (1, maxSlots, s.count); ++k)
            {
                const auto d = s.slots[(size_t) k].degree;
                if (std::find (distinct.begin(), distinct.end(), d) == distinct.end()) distinct.push_back (d);
            }
            R.add ("harmony.motion", distinct.size() >= 3 ? 1.0f : distinct.size() == 2 ? 0.6f : 0.2f);
        }

        Report out;
        out.hook = R.mean ("hook."); out.parts = R.mean ("parts."); out.journey = R.mean ("journey."); out.harmony = R.mean ("harmony.");
        out.total = 0.25f * (out.hook + out.parts + out.journey + out.harmony);
        if (taste != nullptr && ! taste->empty())
        {
            // with your taste: every rule weighted by how much your picks cared about it
            float sum = 0.0f, wsum = 0.0f;
            for (const auto& [k, v] : R.r)
            {
                if (k.startsWith ("raw.")) continue;
                const auto it = taste->find (k);
                const auto w = it != taste->end() ? it->second : 1.0f;
                sum += w * v; wsum += w;
            }
            if (wsum > 0.0f) out.total = sum / wsum;
        }
        out.rules = R.r;
        std::vector<std::pair<float, juce::String>> low;
        for (const auto& [k, v] : R.r) if (v < 0.6f && ! k.startsWith ("raw.")) low.push_back ({ v, k });
        std::sort (low.begin(), low.end());
        for (const auto& [v, k] : low) out.notes.add (detail::explain (k));
        return out;
    }

    inline Report judge (const Settings& s, const suite::SectionMap& map, const SongPatterns& sp, const Weights* taste = nullptr)
    {
        return judge (s, map, renderParts (s, map, sp), taste);
    }

    /** The best of `tries` candidate songs (library seed and melody seed), by the critic's total.
        The first candidate is the current one, so the best is never worse than what's there. */
    struct Pick { uint32_t libSeed = 1; int melodySeed = 1; Report report; float before = 0.0f; uint32_t firstLibSeed = 1; int firstMelodySeed = 1; Report first; };
    inline Pick bestOf (const Settings& s, const suite::SectionMap& map, SongPatterns sp, int tries, uint32_t seed, const Weights* taste = nullptr)
    {
        Pick best;
        best.libSeed = sp.libSeed; best.melodySeed = sp.melody.seed;
        best.report = judge (s, map, sp, taste);
        best.before = best.report.total;
        best.first = best.report; best.firstLibSeed = sp.libSeed; best.firstMelodySeed = sp.melody.seed;
        auto h = seed * 2654435761u + 0x9e3779b9u;
        for (int k = 1; k < tries; ++k)
        {
            h ^= h << 13; h ^= h >> 17; h ^= h << 5;
            sp.libSeed = 1u + h % 9999u;
            sp.melody.seed = 1 + (int) ((h >> 8) % 999u);
            const auto r = judge (s, map, sp, taste);
            if (r.total > best.report.total + 1.0e-4f) { best.report = r; best.libSeed = sp.libSeed; best.melodySeed = sp.melody.seed; }
        }
        return best;
    }

    //==============================================================================
    /** Your taste, from the versions you picked: "Critic taste.txt" in the suite folder, a line per
        pick (both versions' rule scores and which you chose). A rule your picks keep agreeing with
        weighs more, one they keep overruling weighs less; it counts from three picks on. */
    namespace taste
    {
        inline juce::File& fileOverride() { static juce::File f; return f; }
        inline juce::File file() { return fileOverride() != juce::File() ? fileOverride() : suite::Alias::file ("Critic taste.txt"); }

        inline void record (const Report& a, const Report& b, bool choseA)
        {
            juce::StringArray parts { juce::String (juce::Time::getCurrentTime().toMilliseconds()), choseA ? "A" : "B" };
            for (const auto& [k, v] : a.rules) if (! k.startsWith ("raw.")) parts.add ("a." + k + "=" + juce::String (v, 3));
            for (const auto& [k, v] : b.rules) if (! k.startsWith ("raw.")) parts.add ("b." + k + "=" + juce::String (v, 3));
            file().getParentDirectory().createDirectory();
            file().appendText (parts.joinIntoString ("\t") + "\n");
        }

        struct Learned { Weights weights; int picks = 0; };
        inline Learned learn()
        {
            Learned out;
            const auto f = file();
            if (! f.existsAsFile()) return out;
            for (const auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
            {
                const auto t = juce::StringArray::fromTokens (line, "\t", "");
                if (t.size() < 3) continue;
                const auto choseA = t[1] == "A";
                std::map<juce::String, float> a, b;
                for (int i = 2; i < t.size(); ++i)
                {
                    const auto key = t[i].upToFirstOccurrenceOf ("=", false, false), value = t[i].fromFirstOccurrenceOf ("=", false, false);
                    if (key.startsWith ("a.")) a[key.substring (2)] = value.getFloatValue();
                    else if (key.startsWith ("b.")) b[key.substring (2)] = value.getFloatValue();
                }
                ++out.picks;
                for (const auto& [k, va] : a)
                {
                    const auto it = b.find (k);
                    if (it == b.end()) continue;
                    const auto winner = choseA ? va : it->second, loser = choseA ? it->second : va;
                    auto& w = out.weights.try_emplace (k, 1.0f).first->second;
                    if (winner > loser + 0.05f) w = juce::jmin (3.0f, w + 0.25f);         // you agreed with it
                    else if (winner < loser - 0.05f) w = juce::jmax (0.25f, w - 0.25f);   // you overruled it
                }
            }
            if (out.picks < 3) out.weights.clear();   // too few picks to go on
            return out;
        }

        /** In words: the rules your picks weigh most and least. */
        inline juce::String describe (const Learned& l)
        {
            if (l.picks < 3) return "Your taste: " + juce::String (l.picks) + " pick" + (l.picks == 1 ? "" : "s") + " so far (it counts from three).";
            std::vector<std::pair<float, juce::String>> w;
            for (const auto& [k, v] : l.weights) if (std::abs (v - 1.0f) > 0.01f) w.push_back ({ v, k });
            std::sort (w.begin(), w.end());
            juce::StringArray lines { "Your taste, from " + juce::String (l.picks) + " picks:" };
            if (w.empty()) lines.add ("  the rules as written (your picks haven't leaned either way yet)");
            for (auto it = w.rbegin(); it != w.rend() && it->first > 1.0f; ++it) lines.add ("  matters more to you: " + detail::ruleName (it->second) + " (x" + juce::String (it->first, 2) + ")");
            for (const auto& [v, k] : w) if (v < 1.0f) lines.add ("  matters less to you: " + detail::ruleName (k) + " (x" + juce::String (v, 2) + ")");
            return lines.joinIntoString ("\n");
        }
    }
}
