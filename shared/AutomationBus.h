#pragma once

// Hear without the automation: one switch (in Ouroboros) that takes every automatic move out of
// the sound at once (Quintessence, Antioch's space level, Vibration's drive, Lapis' tilt,
// Prima Materia's phrase ride), so you can A/B what the suite's automation adds against your own
// settings. Nothing is changed or forgotten: switch it back and the moves are there again.
// It lapses after 10 minutes, so it can't be left on by accident.

#include "SharedMemory.h"
#include <atomic>
#include <chrono>
#include <cstdint>

namespace suite::AutomationBus
{
    namespace detail
    {
        struct Shared { std::atomic<uint32_t> bypassUntil; };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_automationbus_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (steady_clock::now().time_since_epoch()).count();
        }
    }
    inline void setBypassed (bool on) noexcept { detail::shared().bypassUntil.store (on ? detail::nowSeconds() + 600 : 0); }
    /** Audio thread too: is the automation taken out of the sound right now? */
    inline bool bypassed() noexcept
    {
        const auto until = detail::shared().bypassUntil.load (std::memory_order_relaxed);
        return until != 0 && detail::nowSeconds() < until;
    }
}
