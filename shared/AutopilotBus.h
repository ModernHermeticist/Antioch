#pragma once

// The autopilot: what the suite manages by itself, moment to moment, so the tune can come out of
// the sounds and effects you choose. Three groups, each with one switch (Ouroboros' Autopilot page):
//   harshness   - harsh moments on the master, spiky bands, drive on high notes, bright tails,
//                 resonances several tracks share
//   stability   - runaway feedback and filter resonance, one-off peaks, DC and subsonics, very low
//                 bass notes, level wobble, stereo collapse, clicks at section changes
//   arrangement - the density manager (too many parts at once) and turn-taking between melodic parts
// The bus also carries the live signals the plugins share for it (the master's harsh moment, the
// resonances each track hears) and a count of everything done, for the health readout.

#include "SharedMemory.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace suite::Autopilot
{
    enum Group : uint32_t { harshness = 1u, stability = 2u, arrangement = 4u };

    enum Counter
    {
        harshDips, spikesTamed, driveEased, tailsDamped, sharedResonances,
        runawaysCaught, peaksCaught, lowNotesFolded, wobbleRidden, widthRidden, clicksSmoothed,
        partsThinned, turnsTaken, numCounters
    };
    inline const char* counterName (int c)
    {
        static const char* n[] = { "harsh moments dipped", "spiky bands tamed", "drive eased on high notes", "tails darkened", "shared resonances",
                                   "runaways caught", "peaks caught", "low bass notes folded up", "level wobbles ridden", "stereo collapses ridden",
                                   "section-change clicks smoothed", "parts thinned (density)", "turns taken (melodic parts)" };
        return c >= 0 && c < numCounters ? n[c] : "";
    }

    constexpr int maxHarshIds = 4, maxResonances = 16;

    namespace detail
    {
        struct Shared
        {
            std::atomic<uint32_t> disabled;                 // groups switched off
            std::atomic<float> harshAmount;                 // 0..1: how harsh the master is right now
            std::atomic<int32_t> harshBand;                 // octave band of the harshness (6 = 2 kHz, 7 = 4 kHz, 8 = 8 kHz)
            std::atomic<uint32_t> harshIds[maxHarshIds];    // the tracks causing it (low 12 bits of their Quintessence's id)
            std::atomic<uint32_t> harshMs;
            std::atomic<uint32_t> counters[numCounters];
            std::atomic<float> resonanceHz[maxResonances];  // the resonances each Quintessence hears now
            std::atomic<uint32_t> resonanceId[maxResonances], resonanceMs[maxResonances];
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_autopilot_v1", sizeof (Shared)));
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

    inline bool on (Group g) noexcept { return (detail::shared().disabled.load() & g) == 0; }
    inline void set (Group g, bool enabled) noexcept
    {
        auto& d = detail::shared().disabled;
        if (enabled) d.fetch_and (~(uint32_t) g); else d.fetch_or ((uint32_t) g);
    }

    inline uint32_t disabledGroups() noexcept { return detail::shared().disabled.load(); }   // saved with Ouroboros
    inline void setDisabledGroups (uint32_t mask) noexcept { detail::shared().disabled.store (mask & 7u); }

    inline void count (Counter c, uint32_t n = 1) noexcept { detail::shared().counters[c].fetch_add (n); }
    inline uint32_t countOf (Counter c) noexcept { return detail::shared().counters[c].load(); }

    /** Mentalism: the master is harsh right now (amount 0..1, the band, the tracks causing it). */
    inline void publishHarsh (float amount, int band, const uint32_t* ids, int n) noexcept
    {
        auto& s = detail::shared();
        for (int i = 0; i < maxHarshIds; ++i) s.harshIds[i].store (i < n ? (ids[i] & 0xfffu) | 0x1000u : 0u);
        s.harshBand.store (band);
        s.harshAmount.store (amount);
        s.harshMs.store (detail::nowMs());
    }
    /** How harsh the master is now (0 if it isn't, or the news is older than half a second). */
    inline float harshNow (int* band = nullptr) noexcept
    {
        auto& s = detail::shared();
        if (detail::nowMs() - s.harshMs.load() > 500u) return 0.0f;
        if (band != nullptr) *band = s.harshBand.load();
        return s.harshAmount.load();
    }
    /** ...and whether this track (its Quintessence's id) is one of the causes. */
    inline bool harshCulprit (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        for (auto& h : s.harshIds) if (h.load() == ((id & 0xfffu) | 0x1000u)) return true;
        return false;
    }

    /** Quintessence: the resonance this track hears now (0 = none). Posts expire after 20 s. */
    inline void postResonance (uint32_t id, float hz) noexcept
    {
        auto& s = detail::shared();
        int slot = -1, oldest = 0;
        for (int i = 0; i < maxResonances; ++i)
        {
            if (s.resonanceId[i].load() == id) { slot = i; break; }
            if (s.resonanceMs[i].load() < s.resonanceMs[oldest].load()) oldest = i;
        }
        if (slot < 0) slot = oldest;
        s.resonanceId[slot].store (id);
        s.resonanceHz[slot].store (hz);
        s.resonanceMs[slot].store (detail::nowMs());
    }
    /** How many other tracks hear a resonance within a sixth of an octave of `hz`. */
    inline int othersRingingAt (uint32_t id, float hz) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        int n = 0;
        for (int i = 0; i < maxResonances; ++i)
        {
            const auto other = s.resonanceHz[i].load();
            if (s.resonanceId[i].load() == id || other <= 0.0f || hz <= 0.0f || now - s.resonanceMs[i].load() > 20000u) continue;
            if (std::abs (std::log2 (other / hz)) < 1.0f / 6.0f) ++n;
        }
        return n;
    }

    inline void reset() noexcept   // tests
    {
        auto& s = detail::shared();
        s.disabled.store (0); s.harshAmount.store (0.0f); s.harshMs.store (0);
        for (auto& h : s.harshIds) h.store (0);
        for (auto& c : s.counters) c.store (0);
        for (int i = 0; i < maxResonances; ++i) { s.resonanceId[i].store (0); s.resonanceHz[i].store (0.0f); s.resonanceMs[i].store (0); }
    }
}
