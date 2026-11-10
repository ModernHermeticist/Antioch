#pragma once

// Masking map: which tracks fight for the same frequencies in the drops, from the spectra
// the Prima Materia tracks share (octave bands). Pure logic, so it's directly testable.

#include "Registry.h"
#include "Analysis.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace masking
{
    struct Clash
    {
        uint32_t carveId = 0;          // the track to carve (instance id)
        int band = 0;                  // octave band index (analysis::bandCentres)
        juce::String carveTrack, keepTrack, text;
        float severity = 0.0f;         // larger = worse
    };

    /** Which of two roles gives way: vocal > lead > kick > bass > pads > hats > FX (> unset). */
    inline int priority (int role)
    {
        static const int rank[] = { 0, 5, 4, 2, 3, 6, 7, 1 };   // Neutral, Kick, Bass, Hats, Pads, Lead, Vocal, Atmos
        return juce::isPositiveAndBelow (role, 8) ? rank[role] : 0;
    }

    inline juce::String bandName (int b)
    {
        const auto hz = analysis::bandCentres()[(size_t) b];
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    /** Clashes between Prima Materia tracks, worst first (at most `maxClashes`). Bands 250 Hz to
        8 kHz (the low end is the low-end manager's job). Two tracks clash in a band when it's
        within 6 dB of each track's own loudest band and they're within 6 dB of each other there. */
    inline std::vector<Clash> find (const std::vector<suite::SuiteEntry>& entries, int maxClashes = 5)
    {
        std::vector<const suite::SuiteEntry*> tracks;
        for (const auto& e : entries)
            if (e.plugin == "Prima Materia" && e.has (suite::Status::hasBands)) tracks.push_back (&e);

        std::vector<Clash> out;
        for (size_t i = 0; i < tracks.size(); ++i)
            for (size_t j = i + 1; j < tracks.size(); ++j)
            {
                const auto& a = *tracks[i];
                const auto& b = *tracks[j];
                auto peak = [] (const suite::SuiteEntry& e) { float m = -200.0f; for (auto v : e.bands) m = std::max (m, v); return m; };
                const auto pa = peak (a), pb = peak (b);
                Clash best;
                for (int band = 3; band <= 8; ++band)
                {
                    const auto la = a.bands[(size_t) band], lb = b.bands[(size_t) band];
                    if (la < -70.0f || lb < -70.0f) continue;
                    const auto promA = la - pa, promB = lb - pb, gap = std::abs (la - lb);
                    if (promA < -6.0f || promB < -6.0f || gap > 6.0f) continue;
                    const auto severity = 12.0f + std::min (promA, promB) - gap;   // both prominent, similar level
                    if (severity > best.severity) { best.severity = severity; best.band = band; }
                }
                if (best.severity <= 0.0f) continue;
                const auto aKeeps = priority ((int) a.metrics[0]) >= priority ((int) b.metrics[0]);
                const auto& keep = aKeeps ? a : b;
                const auto& give = aKeeps ? b : a;
                best.carveId = give.id;
                best.carveTrack = give.track.isNotEmpty() ? give.track : juce::String ("(unnamed)");
                best.keepTrack = keep.track.isNotEmpty() ? keep.track : juce::String ("(unnamed)");
                best.text = best.carveTrack + " and " + best.keepTrack + " clash at " + bandName (best.band) + " in the drops: carve "
                            + best.carveTrack;
                out.push_back (best);
            }
        std::sort (out.begin(), out.end(), [] (const Clash& x, const Clash& y) { return x.severity > y.severity; });
        if ((int) out.size() > maxClashes) out.resize ((size_t) maxClashes);
        return out;
    }
}
