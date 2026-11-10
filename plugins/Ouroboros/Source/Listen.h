#pragma once

// Ouroboros "Listen": proposes a section map from per-bar measurements of the mix.
// Pure logic so it can be tested with synthetic songs.
//
// Per bar we get:
//   kickFlux - how much low-end (<100 Hz) *attack* there was (sum of rises of a fast
//              low-band envelope). Kicks give big values; sustained sub bass gives little.
//   highDb   - top-end (>6 kHz) energy in dB.
//   fullDb   - overall energy in dB.
//
// Heuristics for trance structure, working on 4-bar phrases:
//   - phrases with a kick vs without
//   - kickless phrases before the first kick: Intro
//   - first kick run: Intro (16 or 32 bars) then Groove
//   - kickless run in the middle: Breakdown, ending in a Build where the top end rises
//   - kick run after a breakdown: Drop
//   - near the end, where the top end falls away (or a kickless tail): Outro
// It's a proposal for the user to check, not a final answer.

#include "Arrangement.h"
#include <algorithm>
#include <numeric>
#include <vector>

namespace listen
{
    struct BarFeature
    {
        float kickFlux = 0.0f, highDb = -100.0f, fullDb = -100.0f;
        bool heard = false;
    };

    constexpr int phraseBars = 4;

    inline float percentile (std::vector<float> v, float p)
    {
        if (v.empty()) return 0.0f;
        std::sort (v.begin(), v.end());
        return v[(size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::round (p * (float) (v.size() - 1)))];
    }

    inline float mean (const std::vector<float>& v, int from, int to) // [from, to)
    {
        from = std::max (0, from); to = std::min ((int) v.size(), to);
        if (to <= from) return -100.0f;
        return std::accumulate (v.begin() + from, v.begin() + to, 0.0f) / (float) (to - from);
    }

    /** Returns an empty map (count 0) if there isn't enough to go on. */
    inline suite::SectionMap propose (const std::vector<BarFeature>& bars)
    {
        using T = suite::SectionType;
        suite::SectionMap out;

        // Song length: up to the last bar with real audio.
        int last = -1;
        for (int b = 0; b < (int) bars.size(); ++b)
            if (bars[(size_t) b].heard && bars[(size_t) b].fullDb > -60.0f) last = b;
        const auto totalBars = last + 1;
        if (totalBars < 16)
            return out;

        // Kick detection relative to the song's own loud kicks.
        std::vector<float> flux;
        for (int b = 0; b < totalBars; ++b) flux.push_back (bars[(size_t) b].kickFlux);
        const auto kickRef = percentile (flux, 0.9f);
        if (kickRef <= 0.0f)
            return out;

        const auto numPhrases = (totalBars + phraseBars - 1) / phraseBars;
        std::vector<bool> phraseKick ((size_t) numPhrases, false);
        std::vector<float> phraseHigh ((size_t) numPhrases, -100.0f);
        for (int p = 0; p < numPhrases; ++p)
        {
            int kicks = 0, n = 0;
            float high = 0.0f;
            for (int b = p * phraseBars; b < std::min (totalBars, (p + 1) * phraseBars); ++b, ++n)
            {
                if (bars[(size_t) b].kickFlux >= 0.25f * kickRef) ++kicks;
                high += bars[(size_t) b].highDb;
            }
            phraseKick[(size_t) p] = kicks * 2 >= n;
            phraseHigh[(size_t) p] = n > 0 ? high / (float) n : -100.0f;
        }

        // Runs of kick / no kick.
        struct Run { int start, length; bool kick; };
        std::vector<Run> runs;
        for (int p = 0; p < numPhrases; ++p)
        {
            if (runs.empty() || runs.back().kick != phraseKick[(size_t) p]) runs.push_back ({ p, 1, phraseKick[(size_t) p] });
            else ++runs.back().length;
        }

        std::vector<std::pair<int, T>> marks;   // (start phrase, type)
        bool seenKick = false, seenBreakdown = false;
        for (size_t r = 0; r < runs.size(); ++r)
        {
            const auto& run = runs[r];
            const auto isLast = r + 1 == runs.size();

            if (! run.kick)
            {
                if (! seenKick)                { marks.push_back ({ run.start, T::Intro }); continue; }
                if (isLast)                    { marks.push_back ({ run.start, T::Outro }); continue; }

                // Breakdown, with a Build where the top end rises in the last 2-4 phrases.
                seenBreakdown = true;
                int buildStart = -1;
                float bestRise = 2.0f; // dB
                for (int k = 2; k <= 4 && k < run.length; ++k)
                {
                    const auto split = run.start + run.length - k;
                    const auto rise = mean (phraseHigh, split, run.start + run.length) - mean (phraseHigh, run.start, split);
                    if (rise > bestRise) { bestRise = rise; buildStart = split; }
                }
                if (buildStart < 0 && run.length >= 4)
                    buildStart = run.start + run.length - 2;   // no clear rise: assume the classic 8-bar build

                marks.push_back ({ run.start, T::Breakdown });
                if (buildStart > run.start)
                    marks.push_back ({ buildStart, T::Build });
                continue;
            }

            // Kick run.
            if (! seenKick)
            {
                seenKick = true;
                const auto runBars = run.length * phraseBars;
                const auto introPhrases = marks.empty() ? (runBars >= 48 ? 8 : runBars >= 24 ? 4 : run.length) : 0;
                if (introPhrases > 0)
                    marks.push_back ({ run.start, T::Intro });
                if (introPhrases < run.length)
                    marks.push_back ({ run.start + introPhrases, T::Groove });
            }
            else
            {
                marks.push_back ({ run.start, seenBreakdown ? T::Drop : T::Groove });
            }

            // Outro: in the final kick run, where the top end falls well below the run's level.
            if (isLast && run.length >= 4)
            {
                std::vector<float> highs (phraseHigh.begin() + run.start, phraseHigh.begin() + run.start + run.length);
                const auto reference = percentile (highs, 0.5f);
                int outroStart = -1;
                for (int p = run.start + run.length - 1; p > run.start + 1; --p)
                {
                    if (phraseHigh[(size_t) p] < reference - 4.0f) outroStart = p;
                    else break;
                }
                if (outroStart > run.start && run.start + run.length - outroStart >= 2)
                    marks.push_back ({ outroStart, T::Outro });
            }
        }

        // Build the map, merging neighbours of the same type.
        out.totalBars = totalBars;
        for (const auto& [phrase, type] : marks)
        {
            const auto bar = phrase * phraseBars + 1;
            if (out.count > 0 && out.sections[(size_t) out.count - 1].type == type) continue;
            if (out.count > 0 && out.sections[(size_t) out.count - 1].startBar == bar)
            {
                out.sections[(size_t) out.count - 1].type = type;
                continue;
            }
            if (out.count < suite::SectionMap::maxSections)
                out.sections[(size_t) out.count++] = { bar, type };
        }
        if (out.count > 0)
            out.sections[0].startBar = 1;
        return out;
    }
}
