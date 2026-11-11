#pragma once

// TempoBus: the song's tempo, bar by bar. Ouroboros notes the host tempo of every bar as the
// song plays, so a song that changes tempo (a slower breakdown, an automated ramp) gets the
// right times in the cue sheet, the song sheet and the vocalist's pack, and tempo changes in
// the exported MIDI. Everything that follows sections counts in beats, so it stays on the bar
// whatever the tempo does; this is only for turning bars into seconds and writing files.
// Up to 1024 bars, in named shared memory (0 = a bar not heard yet).

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <chrono>
#include <atomic>
#include <cmath>
#include <vector>

namespace suite::TempoBus
{
    constexpr int maxBars = 1024;

    namespace detail
    {
        struct Shared { std::atomic<float> bpm[maxBars]; std::atomic<uint32_t> stamp; };   // stamp: seconds, last noted
        inline uint32_t now() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_tempobus_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        /** Tempos noted more than 6 hours ago belong to another session: ignored. */
        inline bool fresh() noexcept { const auto s = shared().stamp.load (std::memory_order_relaxed); return s != 0 && now() - s < 6u * 3600u; }
    }

    /** Notes the tempo of bar (1-based). Audio-thread safe. */
    inline void mark (int bar, float bpm) noexcept
    {
        if (bar < 1 || bar > maxBars || bpm <= 1.0f) return;
        auto& s = detail::shared();
        if (! detail::fresh()) for (auto& b : s.bpm) b.store (0.0f, std::memory_order_relaxed);   // a new session starts clean
        s.bpm[bar - 1].store (bpm, std::memory_order_relaxed);
        s.stamp.store (detail::now(), std::memory_order_relaxed);
    }
    inline void clear() noexcept { for (auto& b : detail::shared().bpm) b.store (0.0f); detail::shared().stamp.store (0); }

    /** The tempo of bar (1-based), or `fallback` if it hasn't been heard. */
    inline float bpmOf (int bar, float fallback) noexcept
    {
        const auto v = bar >= 1 && bar <= maxBars && detail::fresh() ? detail::shared().bpm[bar - 1].load (std::memory_order_relaxed) : 0.0f;
        return v > 1.0f ? v : fallback;
    }

    /** Seconds from the song start to the start of bar (1-based), bar by bar at the noted tempos. */
    inline double secondsTo (int bar, float fallbackBpm, double barQuarters = 4.0) noexcept
    {
        // Bars not heard yet carry on at the last tempo heard (the song's tempo before any).
        double s = 0.0;
        auto last = fallbackBpm;
        for (int b = 1; b < bar && b <= maxBars; ++b) { last = bpmOf (b, last); s += barQuarters * 60.0 / last; }
        if (bar > maxBars + 1) s += (bar - maxBars - 1) * barQuarters * 60.0 / last;
        return s;
    }

    /** Where the tempo changes up to bar `lastBar`: (bar, bpm) pairs, the first at bar 1. */
    inline std::vector<std::pair<int, float>> changes (int lastBar, float fallbackBpm)
    {
        std::vector<std::pair<int, float>> out;
        auto last = fallbackBpm;
        for (int b = 1; b <= lastBar && b <= maxBars; ++b)
        {
            const auto v = last = bpmOf (b, last);   // unheard bars carry on at the last tempo heard
            if (out.empty() || std::abs (out.back().second - v) > 0.05f) out.push_back ({ b, v });
        }
        return out;
    }

    inline juce::String clock (double seconds)
    {
        const auto s = (int) std::floor (seconds + 1.0e-6);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}
