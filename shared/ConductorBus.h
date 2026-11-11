#pragma once

// The mix conductor: one automatic move at a time, across the whole mix. Every plugin that
// moves by itself (Quintessence, Antioch's space, Vibration's drive, Lapis' tilt, Mentalism's
// balance loop) asks for the turn before a move and gives it back once it has checked it.
// While a move holds the turn, Mentalism measures what it did to the master and leaves a
// verdict: a move that made the master worse is undone by its owner, even if it helped the
// track it was made on. A turn not given back in 15 s lapses, so a plugin that's removed or
// stopped can't block the others.

#include "SharedMemory.h"
#include <atomic>
#include <chrono>
#include <cstdint>

namespace suite::Conductor
{
    enum Verdict { none = 0, fine = 1, worse = 2 };
    constexpr uint32_t holdMs = 15000;

    namespace detail
    {
        struct Shared
        {
            std::atomic<uint32_t> holder, grantedMs, seq;
            std::atomic<int32_t> verdict;
            std::atomic<uint32_t> waiting, reservedUntilMs;   // fairness: whoever was turned away goes next
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_conductor_v1", sizeof (Shared)));
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

    /** Asks for the turn (id: the plugin instance). True if it's yours now. */
    inline bool request (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        auto current = s.holder.load();
        if (current == id) return true;
        const auto waiting = s.waiting.load();
        if ((current != 0 && now - s.grantedMs.load() < holdMs)
            || (waiting != 0 && waiting != id && (int32_t) (s.reservedUntilMs.load() - now) > 0))   // someone else is next
        {
            if (waiting == 0) s.waiting.store (id);
            return false;
        }
        if (waiting == id) s.waiting.store (0);
        if (! s.holder.compare_exchange_strong (current, id)) return false;
        s.grantedMs.store (now);
        s.verdict.store (none);
        s.seq.fetch_add (1);
        return true;
    }

    /** Gives the turn back (only if it's yours). */
    inline void release (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        auto expected = id;
        if (s.holder.compare_exchange_strong (expected, 0u) && s.waiting.load() != 0)
            s.reservedUntilMs.store (detail::nowMs() + 5000);   // the one that waited gets the next turn
    }

    /** A plugin going away: gives back its turn and its place in the queue. */
    inline void forget (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        auto w = id;
        s.waiting.compare_exchange_strong (w, 0u);
        auto expected = id;
        s.holder.compare_exchange_strong (expected, 0u);
    }

    /** What the master did while your move held the turn (none: nobody judged it). */
    inline Verdict verdictFor (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        return s.holder.load() == id ? (Verdict) s.verdict.load() : none;
    }

    /** Mentalism: who holds the turn (0: nobody), since when, and which turn it is. */
    inline uint32_t holder() noexcept
    {
        auto& s = detail::shared();
        const auto h = s.holder.load();
        return h != 0 && detail::nowMs() - s.grantedMs.load() < holdMs ? h : 0u;
    }
    inline uint32_t heldForMs() noexcept { return detail::nowMs() - detail::shared().grantedMs.load(); }
    inline uint32_t turn() noexcept { return detail::shared().seq.load(); }
    inline void judge (Verdict v) noexcept { detail::shared().verdict.store (v); }

    inline void reset() noexcept   // tests
    {
        auto& s = detail::shared();
        s.holder.store (0); s.verdict.store (none); s.waiting.store (0); s.reservedUntilMs.store (0);
    }
}
