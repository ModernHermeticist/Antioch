#pragma once

// The label-ready loop between Mentalism (on the master) and Ouroboros (the dashboard):
//  - Mentalism publishes what it measures on the master (loudness, peak, correlation, mono
//    loss, the tone against the guide, clipping, the readiness score);
//  - Ouroboros publishes the overall label-ready score and the next action, for Mentalism's strip.
// Shared memory; stale after 30 s.

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>

namespace suite::LabelReadyBus
{
    struct Master
    {
        bool valid = false;
        float lufs = -100.0f, truePeakDb = -100.0f, correlation = 1.0f, lowCorrelation = 1.0f, monoLossDb = 0.0f, measuredSeconds = 0.0f;
        int clippedSamples = 0, readiness = 0;
        std::array<float, 10> toneDiff {};   // the master's tone against the guide, per octave band (dB)
    };

    namespace detail
    {
        struct Shared
        {
            std::atomic<uint32_t> masterSeq, masterStamp, overallStamp;
            Master master;
            std::atomic<int32_t> overall;
            char next[160];
            std::atomic<uint32_t> needs[12], needsStamp, revoked[12];   // the label-ready autopilot: which fixes are needed (bit per feature)
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_labelready_v4", sizeof (Shared)));
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

    inline void publishMaster (const Master& m) noexcept
    {
        auto& s = detail::shared();
        s.masterSeq.fetch_add (1);
        std::memcpy (&s.master, &m, sizeof (Master));
        s.masterSeq.fetch_add (1);
        s.masterStamp.store (detail::nowSeconds());
    }
    inline Master master (uint32_t maxAgeSeconds = 30) noexcept
    {
        auto& s = detail::shared();
        for (int a = 0; a < 4; ++a)
        {
            const auto before = s.masterSeq.load();
            if ((before & 1u) != 0) continue;
            const auto st = s.masterStamp.load();
            if (st == 0 || detail::nowSeconds() - st > maxAgeSeconds) return {};
            Master m;
            std::memcpy (&m, &s.master, sizeof (Master));
            if (s.masterSeq.load() != before) continue;
            m.valid = true;
            return m;
        }
        return {};
    }
    inline void publishOverall (int score, const juce::String& next) noexcept
    {
        auto& s = detail::shared();
        const auto utf = next.toStdString();
        std::memset (s.next, 0, sizeof (s.next));
        std::memcpy (s.next, utf.data(), std::min (utf.size(), sizeof (s.next) - 1));
        s.overall.store (score);
        s.overallStamp.store (detail::nowSeconds());
    }
    /** "LABEL-READY 78: next ..." (empty when Ouroboros isn't scoring). */
    inline juce::String overallLine()
    {
        auto& s = detail::shared();
        const auto st = s.overallStamp.load();
        if (st == 0 || detail::nowSeconds() - st > 30) return {};
        char buf[160];
        std::memcpy (buf, s.next, sizeof (buf));
        buf[sizeof (buf) - 1] = 0;
        return "label-ready " + juce::String (s.overall.load()) + "/100, next: " + juce::String::fromUTF8 (buf);
    }
    /** Ouroboros: the autopilot switches whose problem the checks found (a bit per feature). */
    /** A bit per autopilot feature (twelve words: every switch). */
    using Needs = std::array<uint32_t, 12>;
    inline void setBit (Needs& n, int feature) noexcept { if (feature >= 0 && feature < 384) n[(size_t) feature / 32] |= 1u << (feature % 32); }
    inline bool hasBit (const Needs& n, int feature) noexcept { return feature >= 0 && feature < 384 && (n[(size_t) feature / 32] & (1u << (feature % 32))) != 0; }

    inline void publishNeeds (const Needs& mask) noexcept
    {
        auto& s = detail::shared();
        for (size_t i = 0; i < 12; ++i) s.needs[i].store (mask[i]);
        s.needsStamp.store (detail::nowSeconds());
    }
    /** Is the fix for this feature needed now (Ouroboros' checks found its problem in the last minute)? */
    inline bool needed (int feature) noexcept
    {
        auto& s = detail::shared();
        const auto st = s.needsStamp.load();
        if (st == 0 || detail::nowSeconds() - st > 60 || feature < 0 || feature >= 384) return false;
        return (s.needs[(size_t) feature / 32].load() & (1u << (feature % 32))) != 0;
    }
    /** The self-check (Ouroboros): a plugin fix that switched on and didn't help is revoked; its plugin switches it off for this song. */
    inline void publishRevoked (const Needs& mask) noexcept { auto& s = detail::shared(); for (size_t i = 0; i < 12; ++i) s.revoked[i].store (mask[i]); }
    inline bool revoked (int feature) noexcept
    {
        if (feature < 0 || feature >= 384) return false;
        return (detail::shared().revoked[(size_t) feature / 32].load() & (1u << (feature % 32))) != 0;
    }
    inline void reset() noexcept { auto& s = detail::shared(); s.masterStamp.store (0); s.overallStamp.store (0); s.needsStamp.store (0); for (auto& n : s.needs) n.store (0); for (auto& r : s.revoked) r.store (0); }
}
