#pragma once

// The autopilot: what the suite manages by itself, moment to moment, so the tune can come out of
// the sounds and effects you choose. Four groups, each with one switch (Ouroboros' Autopilot page):
//   harshness   - harsh moments on the master, spiky bands, drive on high notes, bright tails,
//                 resonances several tracks share
//   stability   - runaway feedback and filter resonance, one-off peaks, DC and subsonics, very low
//                 bass notes, level wobble, stereo collapse, clicks at section changes
//   arrangement - the density manager (too many parts at once) and turn-taking between melodic parts
//   mixing      - tracks making room for each other moment to moment: masking (with the lead's notes),
//                 the low end's owner, transients on the kick, out-of-key FX, mono lows, even bass
//                 notes and drum hits, harmonics for small speakers, moving resonances, the noise
//                 floor, effect returns, a correction per kind of section, reverb making room
// The bus also carries the live signals the plugins share for it (the master's harsh moment, the
// resonances each track hears) and a count of everything done, for the health readout.

#include "SharedMemory.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace suite::Autopilot
{
    enum Group : uint32_t { harshness = 1u, stability = 2u, arrangement = 4u, mixing = 8u };

    enum Counter
    {
        harshDips, spikesTamed, driveEased, tailsDamped, sharedResonances,
        runawaysCaught, peaksCaught, lowNotesFolded, wobbleRidden, widthRidden, clicksSmoothed,
        partsThinned, turnsTaken,
        masksDipped, partsLifted, lowEndCleared, transientsSoftened, outOfKeyNotched, bassNotesEvened, harmonicsAdded,
        resonancesSoothed, hitsEvened, tailsQuietened, returnsCleaned, sectionCorrections, reverbMadeRoom, monoLowsKept,
        numCounters
    };
    inline const char* counterName (int c)
    {
        static const char* n[] = { "harsh moments dipped", "spiky bands tamed", "drive eased on high notes", "tails darkened", "shared resonances",
                                   "runaways caught", "peaks caught", "low bass notes folded up", "level wobbles ridden", "stereo collapses ridden",
                                   "section-change clicks smoothed", "parts thinned (density)", "turns taken (melodic parts)",
                                   "masking dips (making room)", "lead / vocal lifted through a busy moment", "low end left to the kick and bass",
                                   "transients softened on the kick", "out-of-key FX notes cut", "bass notes evened out", "harmonics added for small speakers",
                                   "moving resonances soothed", "drum hits evened out", "quiet tails cleaned (noise floor)", "effect returns cleaned",
                                   "groove / intro / outro corrections (EQ per section)", "reverb making room (kick, lead, vocal)", "tracks with mono lows" };
        return c >= 0 && c < numCounters ? n[c] : "";
    }

    constexpr int maxHarshIds = 4, maxResonances = 16, maxLive = 32, liveBands = 10;

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
            // What each Quintessence plays right now: its role and ten octave bands (dB), so tracks
            // can make room for each other; and the lead's note (Vibration)
            std::atomic<uint32_t> liveId[maxLive], liveMs[maxLive];
            std::atomic<int32_t> liveRole[maxLive];
            std::atomic<float> liveBand[maxLive][liveBands];
            std::atomic<float> leadHz;
            std::atomic<uint32_t> leadMs;
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_autopilot_v2", sizeof (Shared)));
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
    inline void setDisabledGroups (uint32_t mask) noexcept { detail::shared().disabled.store (mask & 15u); }

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

    /** Who goes first when two parts want the same space (role index as in Quintessence: 1 kick,
        2 bass, 3 hats/perc, 4 pads, 5 lead, 6 vocal, 7 FX, 8 bus). 0 = takes no part. */
    inline int priorityOf (int role) noexcept
    {
        switch (role) { case 6: return 5; case 5: return 4; case 1: case 2: return 3; case 4: return 2; case 3: case 7: return 1; default: return 0; }
    }

    /** Quintessence, every block: what this track plays now. */
    inline void postLive (uint32_t id, int role, const float* bandsDb) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        int slot = -1, oldest = 0;
        for (int i = 0; i < maxLive; ++i)
        {
            if (s.liveId[i].load() == id) { slot = i; break; }
            if (s.liveMs[i].load() < s.liveMs[oldest].load()) oldest = i;
        }
        if (slot < 0) slot = oldest;
        for (int b = 0; b < liveBands; ++b) s.liveBand[slot][b].store (bandsDb[b]);
        s.liveRole[slot].store (role);
        s.liveId[slot].store (id);
        s.liveMs[slot].store (now);
    }
    inline void withdrawLive (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        for (int i = 0; i < maxLive; ++i) if (s.liveId[i].load() == id) { s.liveId[i].store (0); s.liveMs[i].store (0); }
    }
    /** Calls fn (role, bandsDb[10]) for every other track heard in the last 300 ms. */
    template <typename Fn> inline void forEachLive (uint32_t self, Fn&& fn) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        for (int i = 0; i < maxLive; ++i)
        {
            const auto id = s.liveId[i].load();
            if (id == 0 || id == self || now - s.liveMs[i].load() > 300u) continue;
            float bands[liveBands];
            for (int b = 0; b < liveBands; ++b) bands[b] = s.liveBand[i][b].load();
            fn ((int) s.liveRole[i].load(), (const float*) bands);
        }
    }
    /** Vibration: the lead's note now (Hz). */
    inline void publishLeadPitch (float hz) noexcept { detail::shared().leadHz.store (hz); detail::shared().leadMs.store (detail::nowMs()); }
    inline float leadPitch() noexcept
    {
        auto& s = detail::shared();
        return detail::nowMs() - s.leadMs.load() > 300u ? 0.0f : s.leadHz.load();
    }

    inline void reset() noexcept   // tests
    {
        auto& s = detail::shared();
        s.disabled.store (0); s.harshAmount.store (0.0f); s.harshMs.store (0);
        for (auto& h : s.harshIds) h.store (0);
        for (auto& c : s.counters) c.store (0);
        for (int i = 0; i < maxResonances; ++i) { s.resonanceId[i].store (0); s.resonanceHz[i].store (0.0f); s.resonanceMs[i].store (0); }
        for (int i = 0; i < maxLive; ++i) { s.liveId[i].store (0); s.liveMs[i].store (0); }
        s.leadHz.store (0.0f); s.leadMs.store (0);
    }
}
