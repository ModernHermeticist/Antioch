#pragma once

// MixFixBus: which mix fixes each plugin has on right now, on which track, and whether it's still
// settling (a fix just switched on or off, or a Quintessence mid-pass). Each plugin publishes about
// once a second (MixLatch does it); Ouroboros lists them on one page, waits for the plugins to
// settle before judging, and counts fixes piling up on one track.

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <chrono>
#include <vector>

namespace suite::MixFixBus
{
    constexpr int maxSlots = 96, words = 12;

    struct Entry
    {
        uint32_t id = 0;
        char plugin[16] {}, track[48] {};
        std::array<uint32_t, words> active {};   // a bit per autopilot feature
        int count = 0;                           // how many are on
        float levelDb = 0.0f;                    // how far the level-moving fixes move this track now (dB)
        uint32_t busyUntilMs = 0;                // settling until then (steady-clock ms)
        bool budgetHit = false;                  // a budget (level or build) is holding a fix back
        bool has (int feature) const noexcept { return feature >= 0 && feature < words * 32 && (active[(size_t) feature / 32] & (1u << (feature % 32))) != 0; }
        juce::String pluginName() const { return juce::String::fromUTF8 (plugin); }
        juce::String trackName() const { return juce::String::fromUTF8 (track); }
    };

    namespace detail
    {
        struct Slot { std::atomic<uint32_t> seq, stamp; Entry e; };
        struct Shared { Slot slots[maxSlots]; };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_mixfixbus_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (steady_clock::now().time_since_epoch()).count() + 1;
        }
    }
    inline uint32_t nowMs() noexcept
    {
        using namespace std::chrono;
        return (uint32_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
    }

    inline void publish (const Entry& e) noexcept
    {
        if (e.id == 0) return;
        auto& s = detail::shared();
        const auto now = detail::nowSeconds();
        int slot = -1;
        for (int i = 0; i < maxSlots && slot < 0; ++i) if (s.slots[i].e.id == e.id && s.slots[i].stamp.load() != 0) slot = i;
        for (int i = 0; i < maxSlots && slot < 0; ++i) if (s.slots[i].stamp.load() == 0 || now - s.slots[i].stamp.load() > 10) slot = i;
        if (slot < 0) return;
        auto& sl = s.slots[slot];
        sl.seq.fetch_add (1);
        sl.e = e;
        sl.seq.fetch_add (1);
        sl.stamp.store (now);
    }
    inline void withdraw (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        for (auto& sl : s.slots) if (sl.e.id == id) sl.stamp.store (0);
    }
    /** Everything published in the last ten seconds. */
    inline std::vector<Entry> all()
    {
        std::vector<Entry> out;
        auto& s = detail::shared();
        const auto now = detail::nowSeconds();
        for (auto& sl : s.slots)
        {
            const auto st = sl.stamp.load();
            if (st == 0 || now - st > 10) continue;
            for (int tries = 0; tries < 4; ++tries)
            {
                const auto a = sl.seq.load();
                if (a & 1u) continue;
                Entry e = sl.e;
                if (sl.seq.load() == a) { out.push_back (e); break; }
            }
        }
        return out;
    }
    /** Is any plugin still settling? */
    inline bool anyBusy()
    {
        const auto now = nowMs();
        for (const auto& e : all()) if ((int32_t) (e.busyUntilMs - now) > 0) return true;
        return false;
    }
    inline void reset() noexcept { for (auto& sl : detail::shared().slots) { sl.stamp.store (0); sl.e = {}; } }
}
