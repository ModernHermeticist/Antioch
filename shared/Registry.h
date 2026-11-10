#pragma once

// Suite registry: every suite plugin instance announces itself (plugin, track name,
// a few status flags and numbers) about once a second in shared memory, so any plugin
// can see what's loaded where: Ouroboros' Suite tab, auto-roles, the rough mix.
// Entries that stop announcing for 5 s are treated as gone.

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <vector>

namespace suite
{
    namespace Status
    {
        enum : uint32_t
        {
            sidechainWanted    = 1u << 0,   // set up to use a sidechain input
            sidechainConnected = 1u << 1,
            followsOuroboros   = 1u << 2,
            hasMap             = 1u << 3,   // Ouroboros: has a section map
            kickSource         = 1u << 4,   // Kybalion: sharing its kick pattern
            roleSet            = 1u << 5,   // Prima Materia: a role preset is applied
            measuring          = 1u << 6    // has a loudness reading (metric 1)
        };
    }

    struct SuiteEntry
    {
        uint32_t id = 0;
        juce::String plugin, track;
        uint32_t flags = 0;
        std::array<float, 4> metrics {};   // plugin-specific (see each plugin's suiteStatus)
        int ageSeconds = 0;
        bool has (uint32_t f) const noexcept { return (flags & f) != 0; }
    };

    namespace Registry
    {
        constexpr int maxEntries = 64;
        /** Message thread: create or refresh this instance's entry. */
        void announce (uint32_t id, const juce::String& plugin, const juce::String& track, uint32_t flags,
                       const std::array<float, 4>& metrics) noexcept;
        void remove (uint32_t id) noexcept;
        /** Everything announced in the last `maxAgeSeconds`. */
        std::vector<SuiteEntry> list (int maxAgeSeconds = 5);
        void reset() noexcept;   // tests
    }
}
