#include "MorphBus.h"
#include "SharedMemory.h"

#include <atomic>
#include <chrono>
#include <cmath>

namespace suite::MorphBus
{
    namespace
    {
        // [ stamp seconds : 32 | slot : 8 | amount * 65535 : 16 | spare : 8 ]
        struct Shared { std::atomic<uint64_t> word; };

        Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_morphbus_v1", sizeof (Shared)));
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

    void publish (int slot, float amount) noexcept
    {
        const auto a = (uint64_t) std::lround ((amount < 0.0f ? 0.0f : amount > 1.0f ? 1.0f : amount) * 65535.0f);
        shared().word.store (((uint64_t) now() << 32) | ((uint64_t) (slot & 0xff) << 24) | (a << 8), std::memory_order_release);
    }

    void withdraw() noexcept { shared().word.store (0, std::memory_order_release); }

    MorphState read (int maxAgeSeconds) noexcept
    {
        const auto w = shared().word.load (std::memory_order_acquire);
        const auto stamp = (uint32_t) (w >> 32);
        const auto age = (int64_t) now() - (int64_t) stamp;
        MorphState m;
        if (stamp == 0 || age < -2 || age > maxAgeSeconds)
            return m;
        m.valid = true;
        m.slot = (int) ((w >> 24) & 0xff);
        m.amount = (float) ((w >> 8) & 0xffff) / 65535.0f;
        return m;
    }
}
