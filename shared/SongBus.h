#pragma once

// What the song is made of, for the label-ready checks: Correspondence publishes the hook (the
// first drop's lead), the bass, the counter-line, the chord roots per kind of section and the key,
// a few times a minute. Shared memory; older than 60 s counts as gone.

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <chrono>
#include <vector>
#include <map>
#include <set>
#include <cmath>
#include <algorithm>

namespace suite::SongBus
{
    struct Note { int pitch = 60; float start = 0.0f, length = 0.25f; int velocity = 100; };
    constexpr int maxHook = 96, maxBass = 512, maxCounter = 96, numTypes = 6, maxChords = 8;

    struct Song
    {
        bool valid = false;
        int root = 9, scale = 0, finalLift = 0;
        double quartersPerChord = 4.0;
        std::vector<Note> hook, bass, counter;     // song positions (quarters)
        std::array<std::array<int, maxChords>, numTypes> chordRoots {};   // per SectionType: the chords' roots (semitones above the key, -1 = none)
        std::array<int, numTypes> chordCount {};
        double hookStartQ = 0.0;                   // where the hook begins (the first drop)
    };

    namespace detail
    {
        struct N { int16_t pitch, velocity; float start, length; };
        struct Shared
        {
            std::atomic<uint32_t> seq, stamp;
            int32_t root, scale, finalLift, hookCount, bassCount, counterCount;
            float qpc, hookStart;
            N hook[maxHook], bass[maxBass], counter[maxCounter];
            int32_t chordRoots[numTypes][maxChords], chordCount[numTypes];
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_songbus_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (steady_clock::now().time_since_epoch()).count() + 1;
        }
    }

    inline void publish (const Song& song) noexcept
    {
        auto& s = detail::shared();
        s.seq.fetch_add (1);
        s.root = song.root; s.scale = song.scale; s.finalLift = song.finalLift; s.qpc = (float) song.quartersPerChord; s.hookStart = (float) song.hookStartQ;
        auto copy = [] (const std::vector<Note>& from, detail::N* to, int max, int32_t& count)
        {
            count = (int32_t) std::min<size_t> (from.size(), (size_t) max);
            for (int i = 0; i < count; ++i) to[i] = { (int16_t) from[(size_t) i].pitch, (int16_t) from[(size_t) i].velocity, from[(size_t) i].start, from[(size_t) i].length };
        };
        copy (song.hook, s.hook, maxHook, s.hookCount);
        copy (song.bass, s.bass, maxBass, s.bassCount);
        copy (song.counter, s.counter, maxCounter, s.counterCount);
        for (int t = 0; t < numTypes; ++t)
        {
            s.chordCount[t] = song.chordCount[(size_t) t];
            for (int c = 0; c < maxChords; ++c) s.chordRoots[t][c] = song.chordRoots[(size_t) t][(size_t) c];
        }
        s.stamp.store (detail::nowSeconds());
        s.seq.fetch_add (1);
    }
    inline Song read (int maxAgeSeconds = 60)
    {
        Song out;
        auto& s = detail::shared();
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = s.seq.load();
            if ((before & 1u) != 0) continue;
            const auto st = s.stamp.load();
            if (st == 0 || detail::nowSeconds() - st > (uint32_t) maxAgeSeconds) return {};
            Song r;
            r.valid = true;
            r.root = s.root; r.scale = s.scale; r.finalLift = s.finalLift; r.quartersPerChord = s.qpc; r.hookStartQ = s.hookStart;
            auto copy = [] (const detail::N* from, int count, int max, std::vector<Note>& to)
            {
                for (int i = 0; i < juce::jlimit (0, max, count); ++i) to.push_back ({ from[i].pitch, from[i].start, from[i].length, from[i].velocity });
            };
            copy (s.hook, s.hookCount, maxHook, r.hook);
            copy (s.bass, s.bassCount, maxBass, r.bass);
            copy (s.counter, s.counterCount, maxCounter, r.counter);
            for (int t = 0; t < numTypes; ++t)
            {
                r.chordCount[(size_t) t] = juce::jlimit (0, maxChords, (int) s.chordCount[t]);
                for (int c = 0; c < maxChords; ++c) r.chordRoots[(size_t) t][(size_t) c] = s.chordRoots[t][c];
            }
            if (s.seq.load() != before) continue;
            return r;
        }
        return out;
    }
    inline void withdraw() noexcept { detail::shared().stamp.store (0); }

    /** How strong a hook is as writing (0..1): range 7-15 semitones, the peak late, a rhythm it
        keeps with long and short notes, ending on a chord tone, the second half repeating the
        first with some change. The same measures the label-ready writing checks use. */
    inline float hookScore (const std::vector<Note>& h, double startQ, int root, int scale)
    {
        if (h.size() < 4) return 0.0f;
        auto ramp = [] (float v, float good, float bad)
        {
            if (std::abs (good - bad) < 1.0e-9f) return 1.0f;
            return std::clamp ((v - bad) / (good - bad), 0.0f, 1.0f);
        };
        int lo = 127, hi = 0; size_t peakAt = 0;
        for (size_t i = 0; i < h.size(); ++i) { lo = std::min (lo, h[i].pitch); if (h[i].pitch > hi) { hi = h[i].pitch; peakAt = i; } }
        const auto range = (float) (hi - lo);
        const auto rangeScore = std::min (ramp (range, 7.0f, 3.0f), ramp (range, 15.0f, 20.0f));
        const auto peakScore = (float) peakAt / (float) std::max<size_t> (1, h.size() - 1) >= 0.4f ? 1.0f : 0.6f;
        std::map<int, uint32_t> perBar;
        std::set<int> lengths;
        for (const auto& n : h)
        {
            const auto q = (double) n.start - startQ;
            perBar[(int) std::floor (q / 4.0)] |= 1u << std::clamp ((int) std::lround (std::fmod (std::max (0.0, q), 4.0) * 4.0), 0, 15);
            lengths.insert ((int) std::lround (n.length * 4.0f));
        }
        std::map<uint32_t, int> counts;
        for (const auto& [b, mask] : perBar) ++counts[mask];
        int most = 0;
        for (const auto& [m, c] : counts) most = std::max (most, c);
        const auto identity = perBar.empty() ? 0.0f : (float) most / (float) perBar.size();
        const auto rhythmScore = std::min (ramp (identity, 0.5f, 0.2f), lengths.size() >= 2 ? 1.0f : 0.6f);
        const auto lastPc = ((h.back().pitch - root) % 12 + 12) % 12;
        const auto stable = lastPc == 0 || lastPc == 7 || lastPc == (scale == 1 || scale == 5 ? 4 : 3);
        const auto half = h.size() / 2;
        int same = 0;
        for (size_t i = 0; i < half && half + i < h.size(); ++i) same += h[i].pitch == h[half + i].pitch ? 1 : 0;
        const auto rep = half > 0 ? (float) same / (float) half : 0.0f;
        const auto repScore = std::min (ramp (rep, 0.5f, 0.2f), ramp (rep, 0.85f, 1.0f));
        return (rangeScore + peakScore + rhythmScore + (stable ? 1.0f : 0.6f) + repScore) / 5.0f;
    }
}
