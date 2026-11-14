#pragma once

// The master's pitch classes, for key detection: Mentalism (on the master) listens with a
// KeyDetector and publishes its long-term chroma here; the key detector inside Correspondence
// (what Emerald Tablet was) reads it, so the key comes from the whole mix wherever Correspondence sits.

#include "SharedMemory.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace suite::MasterKeyBus
{
    struct Chroma { bool valid = false; std::array<float, 12> chroma {}; float seconds = 0.0f; };

    namespace detail
    {
        struct Shared
        {
            std::atomic<uint32_t> stamp;              // seconds since the epoch of the last publish (0 = never)
            std::atomic<float> chroma[12];
            std::atomic<float> seconds;
            std::atomic<uint32_t> resetRequests;      // bumped by the detector to start listening again
        };
        inline uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }
        inline Shared& shared() noexcept
        {
            static Shared local {};
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_masterkeybus_v1", sizeof (Shared)));
                return m != nullptr ? m : &local;
            }();
            return *s;
        }
    }

    /** Mentalism: the master's long-term chroma (0..1 per pitch class) and the seconds of music heard. */
    inline void publish (const std::array<float, 12>& chroma, float seconds) noexcept
    {
        auto& s = detail::shared();
        for (int i = 0; i < 12; ++i) s.chroma[i].store (chroma[(size_t) i], std::memory_order_relaxed);
        s.seconds.store (seconds, std::memory_order_relaxed);
        s.stamp.store (detail::nowSeconds(), std::memory_order_release);
    }
    inline void withdraw() noexcept { detail::shared().stamp.store (0, std::memory_order_release); }

    /** The master's chroma, if Mentalism published it in the last few seconds. */
    inline Chroma read (int maxAgeSeconds = 3) noexcept
    {
        auto& s = detail::shared();
        Chroma c;
        const auto stamp = s.stamp.load (std::memory_order_acquire);
        const auto age = (int64_t) detail::nowSeconds() - (int64_t) stamp;
        if (stamp == 0 || age < -2 || age > maxAgeSeconds) return c;
        c.valid = true;
        for (int i = 0; i < 12; ++i) c.chroma[(size_t) i] = s.chroma[i].load (std::memory_order_relaxed);
        c.seconds = s.seconds.load (std::memory_order_relaxed);
        return c;
    }

    /** The key detector asks Mentalism to forget what it heard and listen again. */
    inline void requestReset() noexcept { detail::shared().resetRequests.fetch_add (1); }
    inline uint32_t resetRequests() noexcept { return detail::shared().resetRequests.load(); }
}
