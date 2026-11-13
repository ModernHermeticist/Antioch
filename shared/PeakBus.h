#pragma once

// Where the master's peaks come from: every Quintessence posts its track's recent peak (the
// last 2 s, dBFS) and the song position of its loudest hit. Mentalism names the tracks that set
// the master's peak, so a peak is fixed at its source instead of every track coming down.
// Shared memory; entries older than 3 s count as gone.

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>

namespace suite::PeakBus
{
    constexpr int maxTracks = 48, nameChars = 32;
    struct Entry { uint32_t id = 0; juce::String track; int role = 0; float peakDb = -100.0f; double atQ = 0.0; };

    namespace detail
    {
        struct Slot
        {
            std::atomic<uint32_t> id, ms;
            std::atomic<int32_t> role;
            std::atomic<float> peak;
            std::atomic<double> atQ;
            std::atomic<uint32_t> name[nameChars / 4];
        };
        struct Shared { Slot slots[maxTracks]; };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_peakbus_v1", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowMs() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count() | 1u;
        }
    }

    /** Message thread (a few times a second): this track's recent peak. */
    inline void post (uint32_t id, const juce::String& track, int role, float peakDb, double atQ) noexcept
    {
        auto& s = detail::shared();
        int slot = -1, oldest = 0;
        for (int i = 0; i < maxTracks; ++i)
        {
            if (s.slots[i].id.load() == id) { slot = i; break; }
            if (s.slots[i].ms.load() < s.slots[oldest].ms.load()) oldest = i;
        }
        if (slot < 0) slot = oldest;
        auto& d = s.slots[slot];
        d.role.store (role); d.peak.store (peakDb); d.atQ.store (atQ);
        const auto utf = track.toStdString();
        for (int w = 0; w < nameChars / 4; ++w)
        {
            uint32_t v = 0;
            for (int k = 0; k < 4; ++k) { const auto idx = (size_t) (w * 4 + k); v |= (uint32_t) (uint8_t) (idx < utf.size() ? utf[idx] : 0) << (8 * k); }
            d.name[w].store (v);
        }
        d.id.store (id);
        d.ms.store (detail::nowMs());
    }
    inline void withdraw (uint32_t id) noexcept { for (auto& d : detail::shared().slots) if (d.id.load() == id) { d.id.store (0); d.ms.store (0); } }

    /** Every track heard in the last 3 s, loudest peak first. */
    inline std::vector<Entry> read()
    {
        std::vector<Entry> out;
        const auto now = detail::nowMs();
        for (auto& d : detail::shared().slots)
        {
            if (d.id.load() == 0 || now - d.ms.load() > 3000u) continue;
            Entry e; e.id = d.id.load(); e.role = d.role.load(); e.peakDb = d.peak.load(); e.atQ = d.atQ.load();
            std::string n;
            for (int w = 0; w < nameChars / 4; ++w) { const auto v = d.name[w].load(); for (int k = 0; k < 4; ++k) if (const auto c = (char) ((v >> (8 * k)) & 0xffu); c != 0) n.push_back (c); }
            e.track = juce::String::fromUTF8 (n.c_str());
            out.push_back (e);
        }
        std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b) { return a.peakDb > b.peakDb; });
        return out;
    }
    inline void reset() noexcept { for (auto& d : detail::shared().slots) { d.id.store (0); d.ms.store (0); } }   // tests

    /** The tracks that set a master peak: the loudest few, within 6 dB of the loudest. */
    inline juce::StringArray culprits (const std::vector<Entry>& e)
    {
        juce::StringArray out;
        if (e.empty()) return out;
        for (const auto& x : e)
            if (x.peakDb > e.front().peakDb - 6.0f && out.size() < 3)
                out.add ((x.track.isNotEmpty() ? x.track : juce::String ("a track")) + " (" + juce::String (x.peakDb, 1) + " dBFS)");
        return out;
    }
}
