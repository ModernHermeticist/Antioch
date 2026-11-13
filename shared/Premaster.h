#pragma once

// The suite's goal: a label-ready UNMASTERED track (a premaster). Every automatic tuning drives
// towards it:
//  - peaks at or under -6 dBFS (true peak), so the mastering engineer has headroom;
//  - nothing limiting, clipping or squashing the master bus (Lapis is a reference only: it can
//    play how the track would master, for A/B, but never in the render);
//  - the loudness lands wherever the mix lands (typically -18 to -14 LUFS integrated): it's
//    reported, never chased;
//  - 24-bit (or 32-bit float) WAV, no dither, at the project's sample rate.

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <chrono>

namespace suite::premaster
{
    constexpr float peakDbfs = -6.0f;          // the highest true peak a premaster may have
    constexpr float typicalLowLufs = -18.0f;   // where premasters usually land (integrated)...
    constexpr float typicalHighLufs = -14.0f;  // ... (a guide, never a target)
    constexpr float minCrestDb = 7.0f;         // true peak over integrated loudness: less than this looks limited or clipped

    /** How far the master's peaks must come down to meet the premaster's headroom (0 = fine). */
    inline float overBy (float truePeakDb) noexcept { return juce::jmax (0.0f, truePeakDb - peakDbfs); }

    /** One line per check: "OK: ..." or "FIX: ...". */
    inline juce::StringArray checks (float truePeakDb, float lufs, float masterLimitingDb, bool dithered)
    {
        juce::StringArray out;
        out.add (truePeakDb <= peakDbfs + 0.05f ? "OK: peaks " + juce::String (truePeakDb, 1) + " dBTP, at or under " + juce::String (peakDbfs, 0) + " dBFS"
                                                : "FIX: peaks " + juce::String (truePeakDb, 1) + " dBTP: bring the master fader (or every track) down "
                                                      + juce::String (overBy (truePeakDb), 1) + " dB for " + juce::String (peakDbfs, 0) + " dBFS headroom");
        if (masterLimitingDb > 0.1f) out.add ("FIX: the master is limited up to " + juce::String (masterLimitingDb, 1) + " dB: take the limiting off (Lapis: reference only)");
        else out.add ("OK: no limiting or clipping on the master");
        const auto crest = truePeakDb - lufs;
        if (lufs > -70.0f)
        {
            if (crest < minCrestDb) out.add ("FIX: only " + juce::String (crest, 1) + " dB between the peaks and the loudness: something on the master (or the tracks) is squashing it");
            else out.add ("OK: dynamics intact (" + juce::String (crest, 1) + " dB peak over loudness)");
            out.add ("Loudness: " + juce::String (lufs, 1) + " LUFS integrated" + (lufs > typicalHighLufs + 1.0f ? " (loud for a premaster: check nothing is squashing it)" : lufs < typicalLowLufs - 4.0f ? " (quiet: fine, but check the gain staging)" : ""));
        }
        out.add (dithered ? "FIX: dither is on: render a premaster at 24-bit (or 32-bit float) without dither" : "OK: 24-bit, no dither");
        return out;
    }
    inline bool ready (float truePeakDb, float lufs, float masterLimitingDb, bool dithered)
    {
        for (const auto& l : checks (truePeakDb, lufs, masterLimitingDb, dithered)) if (l.startsWith ("FIX")) return false;
        return true;
    }

    /** The headroom keeper: Mentalism (on the master) hears the peaks; when they go over the
        premaster's -6 dBFS it asks every Quintessence to come down together (a master fader
        pulled down, the balance and the dynamics untouched, no limiter anywhere); when there's
        room to spare again it lets them back up. Shared memory; stale after 10 s (0 dB). */
    namespace headroom
    {
        namespace detail
        {
            struct Shared { std::atomic<float> trimDb; std::atomic<uint32_t> stampMs; };
            inline Shared& shared() noexcept
            {
                static Shared* s = []
                {
                    auto* m = static_cast<Shared*> (suite::openSharedBlock ("antioch_suite_headroom_v1", sizeof (Shared)));
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
        inline void publish (float trimDb) noexcept { detail::shared().trimDb.store (trimDb); detail::shared().stampMs.store (detail::nowMs()); }
        inline void withdraw() noexcept { detail::shared().stampMs.store (0); }
        /** Every track's trim now (dB, 0 or less). */
        inline float trimDb() noexcept
        {
            const auto st = detail::shared().stampMs.load();
            return st != 0 && detail::nowMs() - st < 10000u ? juce::jlimit (-24.0f, 0.0f, detail::shared().trimDb.load()) : 0.0f;
        }
        /** One step of the keeper (4 a second): the master's recent sample peak (dB) and the trim now. */
        inline float step (float recentPeakDb, float trim) noexcept
        {
            if (recentPeakDb > peakDbfs - 0.3f) return juce::jmax (-24.0f, trim - juce::jlimit (0.25f, 1.5f, 0.5f * (recentPeakDb - (peakDbfs - 0.5f))));   // over: down
            if (recentPeakDb < peakDbfs - 2.0f && recentPeakDb > -60.0f && trim < 0.0f) return juce::jmin (0.0f, trim + 0.1f);   // plenty of room: slowly back
            return trim;
        }
    }
}
