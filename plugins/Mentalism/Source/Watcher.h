#pragma once

// Problem watcher: what's wrong in the session right now, from what every suite plugin reports
// (the registry) and what Mentalism measures on the master. Each problem comes with the fix
// that addresses it. Small, reversible fixes (gain staging a clipping track, riding a buried
// vocal) are applied by themselves; bigger ones (mono lows, a polarity flip, the session fix)
// are offered as one click. Either way they're checked and logged like every automatic move.

#include "Registry.h"
#include "FixBus.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace watcher
{
    struct Problem
    {
        juce::String key;          // the same problem keeps the same key while it lasts
        juce::String text;         // what's wrong, in a line
        juce::String fixText;      // what the fix does
        suite::Fix fix = suite::Fix::None;
        uint32_t data = 0;
        bool automatic = false;    // safe and small: applied by itself
    };

    struct Master
    {
        bool measured = false;
        float truePeakDb = -100.0f, ceilingDb = -0.3f, monoLossDb = 0.0f, lowCorrelation = 1.0f;
        bool premaster = false;  // true: the goal is a label-ready premaster (peaks at or under -6 dBFS, nothing on the master)
    };

    inline std::vector<Problem> scan (const std::vector<suite::SuiteEntry>& entries, const Master& master)
    {
        std::vector<Problem> out;
        bool haveLapis = false, havePrima = false, haveKybalion = false;
        for (const auto& e : entries)
        {
            haveLapis = haveLapis || e.plugin == "Lapis";
            havePrima = havePrima || e.plugin == "Prima Materia";
            haveKybalion = haveKybalion || e.plugin == "Kybalion";
        }
        auto where = [] (const suite::SuiteEntry& e) { return e.track.isNotEmpty() ? "'" + e.track + "'" : juce::String ("a track"); };
        for (const auto& e : entries)
            if (! master.premaster && e.plugin == "Lapis" && e.metrics[0] > 6.0f)   // the limiter holding more than 6 dB
                out.push_back ({ "limiter", "Lapis is holding back " + juce::String (e.metrics[0], 1) + " dB",
                                 "gain staging brings every track down together, so the limiter needn't clamp", suite::Fix::GainStage, 0, true });

        for (const auto& e : entries)
        {
            if (e.has (suite::Status::clipping) && e.plugin == "Prima Materia")
                out.push_back ({ "clip:" + e.track, where (e) + " is clipping (over 0 dBFS)", "gain staging brings its level down", suite::Fix::GainStage, 0, true });
            if (e.has (suite::Status::vocalBuried))
                out.push_back ({ "buried:" + e.track, "The vocal is buried under the music", "Prima Materia on the vocal rides it 6 dB over the music",
                                 suite::Fix::VocalRide, 0, havePrima });
            if (e.has (suite::Status::missingFiles))
                out.push_back ({ "missing:" + e.track, "Samples are missing in " + e.plugin + " on " + where (e),
                                 "put the synthesised sounds back where samples are missing", suite::Fix::SessionHealth, 0, false });
        }
        if (master.measured)
        {
            if (master.premaster)
            {
                if (master.truePeakDb > master.ceilingDb + 0.1f)   // the premaster's headroom: every track comes down together
                    out.push_back ({ "headroom", "The master peaks at " + juce::String (master.truePeakDb, 1) + " dBTP: a label-ready premaster stays at or under "
                                                     + juce::String (master.ceilingDb, 0) + " dBFS",
                                     "gain staging brings every track down together (no limiter: mastering does that)", suite::Fix::GainStage, 0, true });
                for (const auto& e : entries)
                    if (e.plugin == "Lapis" && e.has (suite::Status::masterPreview))
                        out.push_back ({ "preview", "Lapis' master preview is on: a render now would be a master, not the premaster",
                                         "switch off Hear the master in Lapis before rendering (nothing to click here)", suite::Fix::None, 0, false });
            }
            else if (master.truePeakDb > 0.0f && haveLapis)   // over full scale going into the limiter (Mentalism sits before Lapis)
                out.push_back ({ "headroom", "The master runs into Lapis at " + juce::String (master.truePeakDb, 1) + " dBTP",
                                 "gain staging brings every track down together, so the limiter needn't clamp", suite::Fix::GainStage, 0, true });
            if (! master.premaster && master.truePeakDb > master.ceilingDb + 0.1f && ! haveLapis)
                out.push_back ({ "peak", "The master peaks at " + juce::String (master.truePeakDb, 1) + " dBTP with no limiter",
                                 "put Lapis last on the master (nothing to click: it needs placing)", suite::Fix::None, 0, false });
            if (master.monoLossDb > 3.0f && havePrima)
                out.push_back ({ "mono", "The mix loses " + juce::String (master.monoLossDb, 1) + " dB in mono",
                                 "mono lows (below 120 Hz) on every Prima Materia", suite::Fix::MonoLows, 0, false });
            if (master.lowCorrelation < -0.2f && haveKybalion)
                out.push_back ({ "phase", "The low end is out of phase (correlation " + juce::String (master.lowCorrelation, 2) + ")",
                                 "flip the polarity where Kybalion hears the low end cancel against the kick", suite::Fix::PhaseFlip, 0, false });
        }
        return out;
    }
}
