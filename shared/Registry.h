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
            measuring          = 1u << 6,   // has a loudness reading (metric 1)
            hasBands           = 1u << 7,   // has a spectrum (bands, dB per octave band 31.5 Hz .. 16 kHz)
            fillsOn            = 1u << 8,   // Kybalion: gate fills before drops
            highLatency        = 1u << 9,   // reports a long latency (Azoth's reverse swells: 3 s)
            idle               = 1u << 10   // loaded but doing nothing (cleanup candidate)
        };
    }

    struct SuiteEntry
    {
        uint32_t id = 0;
        juce::String plugin, track;
        uint32_t flags = 0;
        std::array<float, 4> metrics {};   // plugin-specific (see each plugin's suiteStatus)
        std::array<float, 10> bands {};    // with hasBands: the track's spectrum in octave bands (dB)
        int ageSeconds = 0;
        bool has (uint32_t f) const noexcept { return (flags & f) != 0; }
    };

    namespace Registry
    {
        constexpr int maxEntries = 64;
        /** Message thread: create or refresh this instance's entry. */
        void announce (uint32_t id, const juce::String& plugin, const juce::String& track, uint32_t flags,
                       const std::array<float, 4>& metrics, const std::array<float, 10>& bands = {}) noexcept;
        void remove (uint32_t id) noexcept;
        /** Everything announced in the last `maxAgeSeconds`. */
        std::vector<SuiteEntry> list (int maxAgeSeconds = 5);
        void reset() noexcept;   // tests
    }
}
