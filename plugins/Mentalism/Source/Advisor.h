#pragma once

// Targets and plain-language suggestions. Pure logic: no audio, no UI.

#include "Analysis.h"
#include <juce_core/juce_core.h>

namespace advisor
{
    using Bands = std::array<float, analysis::numBands>;

    struct LoudnessTarget { const char* name; float lufs, ceilingDbtp; };

    inline const std::vector<LoudnessTarget>& loudnessTargets()
    {
        static const std::vector<LoudnessTarget> t
        {
            { "Club master (-8 LUFS)",  -8.0f, -0.3f },
            { "Loud club (-6 LUFS)",    -6.0f, -0.3f },
            { "Streaming (-14 LUFS)",  -14.0f, -1.0f },
            { "Match reference track",   0.0f, -0.3f },   // lufs taken from the reference
            { "My sound (your tracks)",  0.0f, -0.3f },   // learned from your finished tracks
        };
        return t;
    }
    constexpr int matchReferenceIndex = 3, mySoundIndex = 4;

    struct TonalTarget { const char* name; Bands curve; };

    /** Rough octave-band balance of finished tracks in each style (relative dB).
        A guide, not a rule: loading a reference track gives a far better target. */
    inline const std::vector<TonalTarget>& tonalTargets()
    {
        static const std::vector<TonalTarget> t
        {
            { "Uplifting trance (guide)",       { -4.0f, 1.5f, 1.5f, -1.5f, -3.5f, -5.0f, -6.5f, -8.5f, -11.0f, -17.0f } },
            { "Progressive / tech (guide)",     { -3.0f, 2.5f, 1.5f, -2.0f, -4.0f, -5.5f, -7.0f, -9.0f, -11.5f, -18.0f } },
            { "Reference track",                {} },
            { "My sound (your tracks)",         {} },
        };
        return t;
    }
    constexpr int referenceTonalIndex = 2, mySoundTonalIndex = 3;

    /** Removes the overall level so only the shape is compared (mean of 63 Hz..8 kHz = 0). */
    inline Bands normalise (const Bands& b)
    {
        float mean = 0.0f;
        for (int i = 1; i <= 8; ++i) mean += b[(size_t) i];
        mean /= 8.0f;
        Bands out;
        for (size_t i = 0; i < b.size(); ++i) out[i] = b[i] - mean;
        return out;
    }

    /** Measured minus target, per band, after normalising both. */
    inline Bands difference (const Bands& measured, const Bands& target)
    {
        const auto m = normalise (measured), t = normalise (target);
        Bands d;
        for (size_t i = 0; i < d.size(); ++i) d[i] = m[i] - t[i];
        return d;
    }

    struct Measurement
    {
        float integratedLufs = -100.0f, truePeakDb = -100.0f;
        float correlation = 1.0f, lowCorrelation = 1.0f, monoLossDb = 0.0f;
        bool haveTonal = false;
        Bands bandDiff {};
    };

    constexpr float tonalTolerance = 3.0f;

    inline juce::StringArray suggestions (const Measurement& m, float targetLufs, float ceilingDbtp, const juce::String& targetName)
    {
        juce::StringArray out;
        auto fmt = [] (float v) { return juce::String (std::abs (v), 1); };

        if (m.integratedLufs > -70.0f)
        {
            const auto diff = m.integratedLufs - targetLufs;
            if (diff < -2.0f)
                out.add (fmt (diff) + " dB quieter than " + targetName + ": push the master limiter or clipper harder, "
                         "or check gain staging before it.");
            else if (diff > 2.0f)
                out.add (fmt (diff) + " dB louder than " + targetName + ": ease off the limiter. Loud masters lose punch, "
                         "and streaming services turn them down anyway.");
        }

        if (m.truePeakDb > ceilingDbtp)
            out.add ("True peak " + juce::String (m.truePeakDb, 1) + " dBTP is over the " + juce::String (ceilingDbtp, 1)
                     + " dBTP ceiling: lower the limiter ceiling (or enable its true-peak mode) to avoid clipping on playback.");

        if (m.lowCorrelation < 0.8f)
            out.add ("The low end (below 120 Hz) isn't mono (correlation " + juce::String (m.lowCorrelation, 2)
                     + "): mono the kick and bass, e.g. Vibration's Mono Below, or remove wideners from bass parts.");

        if (m.correlation < 0.2f)
            out.add ("Stereo correlation is low (" + juce::String (m.correlation, 2) + "): in mono the mix loses "
                     + fmt (m.monoLossDb) + " dB. Check stereo wideners, chorus and phase-inverted layers.");

        if (m.haveTonal)
        {
            struct Band { const char* name; const char* heavy; const char* light; };
            static const Band bands[analysis::numBands] =
            {
                { "Sub rumble (31 Hz)",  "high-pass everything except kick and bass, and check long kick tails", "the very bottom is thin: check the kick/bass sub layer" },
                { "Sub (63 Hz)",         "kick and sub-bass are too dominant: tame the sub or shorten the kick", "the sub is light: more sub-bass, or a kick with more body" },
                { "Bass (125 Hz)",       "the bass is heavy: lower the bass or cut 100-150 Hz on it", "the bass is thin: raise the bass or its 100-150 Hz" },
                { "Low-mids (250 Hz)",   "classic mud: cut 200-400 Hz on pads, bass and reverbs", "the low-mids are scooped: the mix may sound hollow" },
                { "Low-mids (500 Hz)",   "boxy: cut around 400-600 Hz on pads and chords", "a little hollow around 500 Hz" },
                { "Mids (1 kHz)",        "honky mids: check leads and plucks around 1 kHz", "mids are recessed: leads may lack body" },
                { "Upper mids (2 kHz)",  "harsh: supersaw leads often pile up at 2-3 kHz, try a gentle cut", "upper mids are soft: the lead may not cut through" },
                { "Presence (4 kHz)",    "edgy: soften 3-5 kHz on leads, hats and vocals", "lacks presence: lift the leads around 3-5 kHz" },
                { "Brightness (8 kHz)",  "bright or hissy: tame hats, noise risers and top-end boosts", "dull: add air to leads and pads (e.g. Vibration's Air)" },
                { "Air (16 kHz)",        "a lot of very top end: check noise, hats and exciters", "little air above 12 kHz: a high shelf on the master bus or leads" },
            };

            std::vector<int> order;
            for (int i = 0; i < analysis::numBands; ++i)
                if (std::abs (m.bandDiff[(size_t) i]) > tonalTolerance) order.push_back (i);
            std::sort (order.begin(), order.end(), [&] (int a, int b) { return std::abs (m.bandDiff[(size_t) a]) > std::abs (m.bandDiff[(size_t) b]); });

            for (auto i : order)
            {
                const auto d = m.bandDiff[(size_t) i];
                out.add (juce::String (bands[i].name) + " is " + fmt (d) + " dB " + (d > 0 ? "above" : "below") + " the target: "
                         + (d > 0 ? bands[i].heavy : bands[i].light) + ".");
            }
        }

        if (out.isEmpty())
            out.add (m.haveTonal || m.integratedLufs > -70.0f ? "No big issues against this target."
                                                               : "Play your track (ideally the loudest section) to start measuring.");
        while (out.size() > 6)
            out.remove (out.size() - 1);
        return out;
    }
}
