#include "KickBus.h"
#include "SharedMemory.h"

#include <atomic>
#include <chrono>
#include <cstring>

namespace suite::KickBus
{
    namespace
    {
        struct Shared
        {
            std::atomic<uint32_t> seq;     // odd while being written
            std::atomic<uint32_t> stamp;   // seconds; 0 = withdrawn
            KickPatternSet set;
        };

        Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_kickbus_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }

        uint32_t now() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }
    }

    void publish (const KickPatternSet& set) noexcept
    {
        auto& s = shared();
        s.seq.fetch_add (1, std::memory_order_acq_rel);
        std::memcpy (static_cast<void*> (&s.set), &set, sizeof (KickPatternSet));
        s.stamp.store (now(), std::memory_order_release);
        s.seq.fetch_add (1, std::memory_order_acq_rel);
    }

    KickPatternSet read (int maxAgeSeconds) noexcept
    {
        auto& s = shared();
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto before = s.seq.load (std::memory_order_acquire);
            if (before & 1u) continue;
            KickPatternSet copy;
            std::memcpy (static_cast<void*> (&copy), &s.set, sizeof (KickPatternSet));
            const auto stamp = s.stamp.load (std::memory_order_acquire);
            if (s.seq.load (std::memory_order_acquire) != before) continue;
            const auto age = (int64_t) now() - (int64_t) stamp;
            if (stamp == 0 || age > maxAgeSeconds || age < -2) return {};
            return copy;
        }
        return {};
    }

    void withdraw() noexcept { shared().stamp.store (0, std::memory_order_release); }
}
