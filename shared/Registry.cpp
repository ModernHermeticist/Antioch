#include "Registry.h"
#include "SharedMemory.h"

#include <atomic>
#include <chrono>
#include <cstring>

namespace suite::Registry
{
    namespace
    {
        struct Slot
        {
            std::atomic<uint32_t> seq;     // odd while being written (seqlock)
            std::atomic<uint32_t> id;      // 0 = free
            std::atomic<uint32_t> stamp;   // seconds
            char plugin[24];
            char track[56];
            uint32_t flags;
            float metrics[4];
        };

        struct Shared { Slot slots[maxEntries]; };

        Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_registry_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }

        uint32_t now() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (system_clock::now().time_since_epoch()).count();
        }

        void copyText (char* dest, size_t size, const juce::String& text) noexcept
        {
            std::memset (dest, 0, size);
            text.copyToUTF8 (dest, size - 1);
        }
    }

    void announce (uint32_t id, const juce::String& plugin, const juce::String& track, uint32_t flags,
                   const std::array<float, 4>& metrics) noexcept
    {
        if (id == 0) return;
        auto& s = shared();
        const auto t = now();

        Slot* mine = nullptr;
        for (auto& slot : s.slots)
            if (slot.id.load (std::memory_order_acquire) == id) { mine = &slot; break; }

        if (mine == nullptr)   // claim a free or stale slot
            for (auto& slot : s.slots)
            {
                auto current = slot.id.load (std::memory_order_acquire);
                const auto stale = current != 0 && t - slot.stamp.load() > 10u;
                if ((current == 0 || stale) && slot.id.compare_exchange_strong (current, id))
                {
                    mine = &slot;
                    break;
                }
            }
        if (mine == nullptr)
            return;   // table full

        mine->seq.fetch_add (1, std::memory_order_acq_rel);   // odd: writing
        copyText (mine->plugin, sizeof (mine->plugin), plugin);
        copyText (mine->track, sizeof (mine->track), track);
        mine->flags = flags;
        for (size_t i = 0; i < 4; ++i) mine->metrics[i] = metrics[i];
        mine->stamp.store (t, std::memory_order_release);
        mine->seq.fetch_add (1, std::memory_order_acq_rel);   // even: done
    }

    void remove (uint32_t id) noexcept
    {
        for (auto& slot : shared().slots)
        {
            auto current = id;
            if (slot.id.compare_exchange_strong (current, 0u))
                return;
        }
    }

    std::vector<SuiteEntry> list (int maxAgeSeconds)
    {
        std::vector<SuiteEntry> out;
        const auto t = now();
        for (auto& slot : shared().slots)
        {
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto before = slot.seq.load (std::memory_order_acquire);
                if (before & 1u) continue;
                const auto id = slot.id.load (std::memory_order_acquire);
                if (id == 0) break;
                const auto stamp = slot.stamp.load (std::memory_order_acquire);
                SuiteEntry e;
                e.id = id;
                e.plugin = juce::String::fromUTF8 (slot.plugin, (int) strnlen (slot.plugin, sizeof (slot.plugin)));
                e.track = juce::String::fromUTF8 (slot.track, (int) strnlen (slot.track, sizeof (slot.track)));
                e.flags = slot.flags;
                for (size_t i = 0; i < 4; ++i) e.metrics[i] = slot.metrics[i];
                e.ageSeconds = (int) (t - stamp);
                if (slot.seq.load (std::memory_order_acquire) != before) continue;
                if (e.ageSeconds <= maxAgeSeconds && e.ageSeconds >= -2) out.push_back (e);
                break;
            }
        }
        return out;
    }

    void reset() noexcept
    {
        for (auto& slot : shared().slots) { slot.id.store (0); slot.stamp.store (0); slot.seq.store (0); }
    }
}
