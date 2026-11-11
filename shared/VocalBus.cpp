#include "VocalBus.h"
#include "SharedMemory.h"

#include <atomic>

namespace suite::VocalBus
{
    namespace
    {
        struct Shared
        {
            std::atomic<uint32_t> words[maxBars / 32];
            std::atomic<uint32_t> any;
        };

        Shared& shared() noexcept
        {
            static Shared local {};
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_vocalbus_v1", sizeof (Shared)));
                return m != nullptr ? m : &local;
            }();
            return *s;
        }
    }

    void mark (int bar, bool vocal) noexcept
    {
        if (bar < 1 || bar > maxBars) return;
        const auto i = bar - 1;
        auto& w = shared().words[i / 32];
        const auto bit = 1u << (i % 32);
        if (vocal) { w.fetch_or (bit); shared().any.store (1); }
        else w.fetch_and (~bit);
    }

    void clear() noexcept
    {
        for (auto& w : shared().words) w.store (0);
        shared().any.store (0);
    }

    bool sings (int bar) noexcept
    {
        if (bar < 1 || bar > maxBars) return false;
        const auto i = bar - 1;
        return ((shared().words[i / 32].load() >> (i % 32)) & 1u) != 0;
    }

    bool any() noexcept { return shared().any.load() != 0; }

    std::vector<bool> read()
    {
        if (shared().any.load() == 0) return {};
        std::vector<bool> out ((size_t) maxBars, false);
        int last = -1;
        for (int i = 0; i < maxBars; ++i)
            if ((shared().words[i / 32].load() >> (i % 32)) & 1u) { out[(size_t) i] = true; last = i; }
        out.resize ((size_t) (last + 1));
        return out;
    }
}
