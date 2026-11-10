#include "TargetBus.h"
#include "SharedMemory.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>

namespace suite::TargetBus
{
    namespace
    {
        // [ stamp seconds : 32 | lufs tenths + 1000 : 16 | ceiling hundredths + 1000 : 16 ]
        struct Shared { std::atomic<uint64_t> word; };

        uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }

        Shared& shared() noexcept
        {
            static Shared local { 0 };
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_targetbus_v1", sizeof (Shared)));
                return m != nullptr ? m : &local;
            }();
            return *s;
        }
    }

    void publish (float lufs, float ceilingDbtp) noexcept
    {
        const auto l = (uint64_t) (uint16_t) (std::lround (lufs * 10.0f) + 1000);
        const auto c = (uint64_t) (uint16_t) (std::lround (ceilingDbtp * 100.0f) + 1000);
        shared().word.store (((uint64_t) nowSeconds() << 32) | (l << 16) | c, std::memory_order_release);
    }

    void withdraw() noexcept { shared().word.store (0, std::memory_order_release); }

    LoudnessTarget read (int maxAgeSeconds) noexcept
    {
        const auto w = shared().word.load (std::memory_order_acquire);
        LoudnessTarget t;
        const auto stamp = (uint32_t) (w >> 32);
        const auto age = (int64_t) nowSeconds() - (int64_t) stamp;
        if (stamp == 0 || age < -2 || age > maxAgeSeconds)
            return t;
        t.valid = true;
        t.lufs = ((int) ((w >> 16) & 0xffff) - 1000) / 10.0f;
        t.ceilingDbtp = ((int) (w & 0xffff) - 1000) / 100.0f;
        return t;
    }
}
