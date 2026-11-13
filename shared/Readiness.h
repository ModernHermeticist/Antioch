#pragma once

// The premaster readiness score: one number (0-100) for "is this label-ready?", with its parts,
// so every automatic fix has something to raise and you can see what holds it back. Parts with
// nothing measured yet are left out (the rest are re-weighted).

#include "Premaster.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace suite::premaster
{
    struct ReadinessInput
    {
        float truePeakDb = -100.0f, lufs = -100.0f;
        bool previewOn = false;            // Lapis' master preview left on
        float masterLimitingDb = 0.0f;
        float lowCorrelation = 2.0f, correlation = 2.0f, monoLossDb = -1.0f;   // 2 / -1: not measured
        float worstToneDb = -1.0f;         // the biggest departure from the tonal guide (dB), -1: not measured
        int clippedSamples = 0, clippingTracks = 0, squashedTracks = 0;
        float breakdownGapLu = -1.0f;      // how far the quietest breakdown sits under the drops (LU), -1: not measured
        float subShareDb = 0.0f;           // the sub (under 60 Hz) against the guide (dB, + = more), 0 = fine / not measured
    };

    struct ReadinessPart { juce::String name; float score = 1.0f, weight = 1.0f; juce::String detail; bool measured = true; };
    struct Readiness
    {
        int total = 0;
        std::vector<ReadinessPart> parts;
        juce::String weakest() const
        {
            const ReadinessPart* w = nullptr;
            for (const auto& p : parts) if (p.measured && (w == nullptr || p.score * p.weight < w->score * w->weight - 1.0e-6f) && p.score < 0.999f) w = &p;
            return w != nullptr ? w->name + ": " + w->detail : juce::String ("nothing holding it back");
        }
        juce::String text() const
        {
            juce::StringArray l;
            l.add ("Premaster readiness: " + juce::String (total) + "/100");
            for (const auto& p : parts)
                l.add ("  " + p.name + ": " + (p.measured ? juce::String (juce::roundToInt (p.score * 100.0f)) + "%" : juce::String ("not measured yet")) + "  " + p.detail);
            return l.joinIntoString ("\n");
        }
    };

    namespace detail
    {
        inline float ramp (float v, float good, float bad)   // 1 at good, 0 at bad (either direction)
        {
            if (good == bad) return v == good ? 1.0f : 0.0f;
            return juce::jlimit (0.0f, 1.0f, (v - bad) / (good - bad));
        }
    }

    inline Readiness readiness (const ReadinessInput& in)
    {
        using detail::ramp;
        Readiness r;
        auto add = [&r] (const juce::String& n, float w, bool measured, float score, const juce::String& d) { r.parts.push_back ({ n, juce::jlimit (0.0f, 1.0f, score), w, d, measured }); };
        const auto havePeak = in.truePeakDb > -99.0f, haveLufs = in.lufs > -70.0f;
        add ("Headroom", 20.0f, havePeak, ramp (in.truePeakDb, peakDbfs, -1.0f),
             havePeak ? juce::String (in.truePeakDb, 1) + " dBTP (at or under -6)" : juce::String());
        add ("Nothing on the master", 15.0f, true, in.previewOn ? 0.0f : ramp (in.masterLimitingDb, 0.0f, 3.0f),
             in.previewOn ? juce::String ("Lapis' master preview is on") : in.masterLimitingDb > 0.1f ? "limited " + juce::String (in.masterLimitingDb, 1) + " dB" : juce::String ("untouched"));
        const auto crest = in.truePeakDb - in.lufs;
        add ("Dynamics", 15.0f, havePeak && haveLufs, juce::jmin (ramp (crest, 9.0f, 4.0f), in.squashedTracks > 0 ? 0.7f : 1.0f),
             havePeak && haveLufs ? juce::String (crest, 1) + " dB peak over loudness" + (in.squashedTracks > 0 ? ", " + juce::String (in.squashedTracks) + " track(s) arrive squashed" : juce::String()) : juce::String());
        const auto haveLow = in.lowCorrelation < 1.5f;
        add ("Low end", 15.0f, haveLow, juce::jmin (ramp (in.lowCorrelation, 0.9f, 0.4f), ramp (std::abs (in.subShareDb), 2.0f, 8.0f)),
             haveLow ? "mono below 120 Hz " + juce::String (in.lowCorrelation, 2) + (std::abs (in.subShareDb) > 2.0f ? ", sub " + juce::String (in.subShareDb, 1) + " dB off the guide" : juce::String()) : juce::String());
        const auto haveMono = in.monoLossDb >= 0.0f;
        add ("Mono", 10.0f, haveMono, juce::jmin (ramp (in.monoLossDb, 3.0f, 8.0f), in.correlation < 0.0f ? 0.3f : 1.0f),
             haveMono ? "the mono sum " + juce::String (in.monoLossDb, 1) + " dB down" : juce::String());
        const auto haveTone = in.worstToneDb >= 0.0f;
        add ("Tonal balance", 10.0f, haveTone, ramp (in.worstToneDb, 3.0f, 8.0f), haveTone ? "worst band " + juce::String (in.worstToneDb, 1) + " dB off the guide" : juce::String());
        add ("Clean", 10.0f, true, (in.clippedSamples > 0 ? 0.3f : 1.0f) * (in.clippingTracks > 0 ? 0.6f : 1.0f),
             in.clippedSamples > 0 ? juce::String (in.clippedSamples) + " samples at full scale" : in.clippingTracks > 0 ? juce::String (in.clippingTracks) + " track(s) clip inside" : juce::String ("no clipping"));
        const auto haveArc = in.breakdownGapLu >= 0.0f;
        add ("Breakdowns for mastering", 5.0f, haveArc, ramp (in.breakdownGapLu, 10.0f, 15.0f),
             haveArc ? juce::String (in.breakdownGapLu, 1) + " LU under the drops (at most 10, so mastering needn't pull them up)" : juce::String());
        float sum = 0.0f, weights = 0.0f;
        for (const auto& p : r.parts) if (p.measured) { sum += p.score * p.weight; weights += p.weight; }
        r.total = weights > 0.0f ? juce::roundToInt (100.0f * sum / weights) : 0;
        return r;
    }
}
