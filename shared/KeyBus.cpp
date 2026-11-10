#include "KeyBus.h"

#include <atomic>
#include <chrono>
#include <cstdint>

#include "SharedMemory.h"

namespace suite::KeyBus
{
    namespace
    {
        // Layout: [ timestamp seconds : 32 | magic : 8 | scale : 8 | root : 8 | valid : 8 ]
        struct Shared
        {
            std::atomic<uint64_t> word;        // current key
            std::atomic<uint64_t> suggestion;  // [ stamp : 32 | sequence : 16 | scale : 8 | root : 8 ]
        };

        static_assert (std::atomic<uint64_t>::is_always_lock_free, "KeyBus needs lock-free 64-bit atomics");

        constexpr uint64_t magic = 0xA7, detectorMagic = 0xA8;

        uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }

        Shared* mapShared() noexcept
        {
            return static_cast<Shared*> (openSharedBlock ("antioch_suite_keybus_v2", sizeof (Shared)));
        }

        Shared& shared() noexcept
        {
            // Falls back to process-local memory if the OS refuses shared memory.
            static Shared local {};
            static Shared* s = [] { auto* m = mapShared(); return m != nullptr ? m : &local; }();
            return *s;
        }
    }

    void publish (int root, int scale, bool fromDetector) noexcept
    {
        const uint64_t word = ((uint64_t) nowSeconds() << 32) | ((fromDetector ? detectorMagic : magic) << 24)
                            | ((uint64_t) (scale & 0xff) << 16) | ((uint64_t) (root & 0xff) << 8) | 1u;
        shared().word.store (word, std::memory_order_release);
    }

    void withdraw() noexcept
    {
        shared().word.store (0, std::memory_order_release);
    }

    KeyInfo read (int maxAgeSeconds) noexcept
    {
        const auto word = shared().word.load (std::memory_order_acquire);
        KeyInfo info;

        const auto m = (word >> 24) & 0xff;
        if ((word & 0xff) == 0 || (m != magic && m != detectorMagic))
            return info;
        info.fromDetector = m == detectorMagic;

        const auto stamp = (uint32_t) (word >> 32);
        const auto age = (int64_t) nowSeconds() - (int64_t) stamp;
        if (age < -2 || age > maxAgeSeconds)
            return info;

        info.valid = true;
        info.root = (int) ((word >> 8) & 0xff) % 12;
        info.scale = (int) ((word >> 16) & 0xff);
        return info;
    }

    void suggest (int root, int scale) noexcept
    {
        auto& s = shared();
        const auto previous = s.suggestion.load (std::memory_order_acquire);
        const auto sequence = (uint64_t) ((((previous >> 16) & 0xffff) + 1) & 0xffff);
        s.suggestion.store (((uint64_t) nowSeconds() << 32) | (sequence << 16) | ((uint64_t) (scale & 0xff) << 8) | (uint64_t) (root & 0xff),
                            std::memory_order_release);
    }

    KeySuggestion readSuggestion (int maxAgeSeconds) noexcept
    {
        const auto w = shared().suggestion.load (std::memory_order_acquire);
        KeySuggestion k;
        const auto stamp = (uint32_t) (w >> 32);
        const auto age = (int64_t) nowSeconds() - (int64_t) stamp;
        if (stamp == 0 || age < -2 || age > maxAgeSeconds)
            return k;
        k.valid = true;
        k.sequence = (uint32_t) ((w >> 16) & 0xffff);
        k.scale = (int) ((w >> 8) & 0xff);
        k.root = (int) (w & 0xff) % 12;
        return k;
    }
}
