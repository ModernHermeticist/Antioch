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
            std::atomic<float> waitingNeed;                   // ...the neediest of them
            std::atomic<uint32_t> waitingMs;
            std::atomic<int32_t> masterBand;                  // Mentalism: the master's worst band now (-1: none)
            std::atomic<float> masterDiffDb;
            std::atomic<uint32_t> masterMs;
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_conductor_v2", sizeof (Shared)));
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

    /** Asks for the turn (id: the plugin instance; need: how far it is from where it should be,
        in dB, plus how much of the master's current problem it's causing). The turn goes by need:
        while the turn is taken, the neediest asker is remembered and goes next; on a tie, the one
        that asked first. True if it's yours now. */
    inline bool request (uint32_t id, float need = 0.0f) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        auto current = s.holder.load();
        if (current == id) return true;
        const auto waiting = s.waiting.load();
        const auto waitFresh = waiting != 0 && now - s.waitingMs.load() < 10000;   // a waiter that stopped asking drops out
        const auto busy = current != 0 && now - s.grantedMs.load() < holdMs;
        const auto someoneNeedier = waitFresh && waiting != id && s.waitingNeed.load() >= need;
        if (busy || someoneNeedier)
        {
            if (! waitFresh || waiting == id || need > s.waitingNeed.load())
            {
                s.waiting.store (id); s.waitingNeed.store (need); s.waitingMs.store (now);
            }
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
        auto expected = id;
        detail::shared().holder.compare_exchange_strong (expected, 0u);   // the neediest waiter goes next (see request)
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

    /** Mentalism: where the master is furthest off its target (band 0-9, dB; + = too much). */
    inline void publishMasterProblem (int band, float diffDb) noexcept
    {
        auto& s = detail::shared();
        s.masterBand.store (band); s.masterDiffDb.store (diffDb); s.masterMs.store (detail::nowMs());
    }
    inline bool masterProblem (int& band, float& diffDb) noexcept
    {
        auto& s = detail::shared();
        if (detail::nowMs() - s.masterMs.load() > 20000 || s.masterBand.load() < 0) return false;
        band = s.masterBand.load(); diffDb = s.masterDiffDb.load();
        return true;
    }

    inline void reset() noexcept   // tests
    {
        auto& s = detail::shared();
        s.holder.store (0); s.verdict.store (none); s.waiting.store (0); s.reservedUntilMs.store (0); s.waitingNeed.store (0.0f); s.masterBand.store (-1);
    }
}
