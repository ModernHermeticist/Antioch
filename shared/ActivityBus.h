#pragma once

// ActivityBus: how active the vocal and the lead are right now (0..1), so other tracks can
// make room. Solve et Coagula (hearing the vocal) and Vibration (on the lead) write; Prima
// Materia (vocal-aware levels) and Antioch (lead-aware space) read. Values older than a
// quarter of a second count as silent, so a plugin that stops or is removed lets go.

#include "SharedMemory.h"
#include <atomic>
#include <chrono>
#include <cstdint>

namespace suite::ActivityBus
{
    enum Source { vocal = 0, lead = 1, vocalOverMusic = 2, riser = 3, sibilance = 4, dropHit = 5, phraseEnd = 6, numSources = 7 };
    // vocalOverMusic: dB, written raw; riser: Azoth's riser and swell; sibilance: the vocal's "s" (Quintessence on the vocal);
    // dropHit: each drop's first beat (Ouroboros); phraseEnd: the last bar of each 8-bar phrase, where fills play (Ouroboros)

    namespace detail
    {
        struct Shared { std::atomic<float> level[numSources]; std::atomic<uint32_t> stampMs[numSources]; };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_activitybus_v2", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowMs() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
        }
    }

    /** Audio thread: this source's activity now (0 = silent, 1 = fully active). */
    inline void write (Source s, float level) noexcept
    {
        detail::shared().level[s].store (level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level), std::memory_order_relaxed);
        detail::shared().stampMs[s].store (detail::nowMs(), std::memory_order_relaxed);
    }
    /** Audio thread: this source's activity (0 if nothing wrote it in the last 250 ms). */
    inline float read (Source s) noexcept
    {
        const auto age = detail::nowMs() - detail::shared().stampMs[s].load (std::memory_order_relaxed);
        return age < 250u ? detail::shared().level[s].load (std::memory_order_relaxed) : 0.0f;
    }
    /** A raw value (vocalOverMusic: how far the vocal sits above the music, dB). */
    inline void writeValue (Source s, float value) noexcept
    {
        detail::shared().level[s].store (value, std::memory_order_relaxed);
        detail::shared().stampMs[s].store (detail::nowMs(), std::memory_order_relaxed);
    }
    /** The raw value, or `missing` if nothing wrote it in the last 250 ms. */
    inline float readValue (Source s, float missing) noexcept
    {
        const auto age = detail::nowMs() - detail::shared().stampMs[s].load (std::memory_order_relaxed);
        return age < 250u ? detail::shared().level[s].load (std::memory_order_relaxed) : missing;
    }
    inline void clear() noexcept { for (int i = 0; i < numSources; ++i) { detail::shared().level[i].store (0.0f); detail::shared().stampMs[i].store (0); } }
}
