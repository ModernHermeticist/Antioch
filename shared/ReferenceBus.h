#pragma once

// The reference's tone, for every Quintessence: Mentalism publishes the octave-band shape of the
// reference track you loaded; each Quintessence moves its role's target by how that reference
// differs from the uplifting trance guide. A finished mix can't be split back into its parts,
// so this is the reference's overall colour (brighter, warmer, more low end) passed on to every
// part, not each part's own balance. Values older than 10 s count as gone.

#include "SharedMemory.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace suite::ReferenceBus
{
    namespace detail
    {
        struct Shared { std::atomic<uint32_t> stamp; std::array<std::atomic<float>, 10> bands; };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_referencebus_v1", sizeof (Shared)));
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

    /** The uplifting trance guide the reference is compared with (octave bands, 31.5 Hz-16 kHz). */
    inline const std::array<float, 10>& guide()
    {
        static const std::array<float, 10> g { -4.0f, 1.5f, 1.5f, -1.5f, -3.5f, -5.0f, -6.5f, -8.5f, -11.0f, -17.0f };
        return g;
    }

    inline void publish (const std::array<float, 10>& bands) noexcept
    {
        auto& s = detail::shared();
        for (size_t b = 0; b < 10; ++b) s.bands[b].store (bands[b]);
        s.stamp.store (detail::nowSeconds() + 1);   // 0 means never
    }
    inline void withdraw() noexcept { detail::shared().stamp.store (0); }

    /** How the reference differs from the guide, per band (shape only: the mean of 63 Hz-8 kHz
        removed; limited to +-4 dB). False if no reference is published. */
    inline bool offset (std::array<float, 10>& out) noexcept
    {
        auto& s = detail::shared();
        const auto stamp = s.stamp.load();
        if (stamp == 0 || detail::nowSeconds() + 1 - stamp > 10) return false;
        float meanR = 0.0f, meanG = 0.0f;
        for (size_t b = 1; b <= 8; ++b) { meanR += s.bands[b].load(); meanG += guide()[b]; }
        meanR /= 8.0f; meanG /= 8.0f;
        for (size_t b = 0; b < 10; ++b)
        {
            const auto d = (s.bands[b].load() - meanR) - (guide()[b] - meanG);
            out[b] = d < -4.0f ? -4.0f : d > 4.0f ? 4.0f : d;
        }
        return true;
    }
}
