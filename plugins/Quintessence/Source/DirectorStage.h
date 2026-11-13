#pragma once

// Quintessence's part in the suite director's plan: the mix around each moment.
//  - Pre-mix: two bars before a moment led on another track, this track makes room (a gentle
//    presence dip, up to 2.5 dB) and hands it back as the moment ends.
//  - The hook's first bar back: the competing parts (pads, FX) dip on the drop's first bar.
//  - Low end: the bass's sub clears for exactly the beats the impact needs.
//  - The build held back: the last 4 bars into a drop come down a little (a level ride, no
//    limiter: the premaster keeps its peaks), so the drop lands bigger at the same level.
//  - Stereo: narrow into the drop, wide on the one (not on kick or bass).
//  - The director's silences (every track with Quintessence falls silent; the kick plays through a
//    kick-only bar).
//  - The drop's entrance (the director's scene move): a slam (everything on the one), staggered
//    (the kick on the one, the bass a beat later, the rest a beat after that) or through a filter
//    that opens over the first bar (everything but the kick).
// State is refreshed every 32 samples; every change is ramped.

#include "DirectorBus.h"
#include "SuiteDSP.h"
#include <array>

namespace qn
{
    class DirectorStage
    {
    public:
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            for (auto& b : presence) b.reset();
            for (auto& s : sub) s.reset();
            for (auto& s : sub) s.set (90.0f, 0.707f, sr);
            presenceDb = 0.0f; subMix = 0.0f; gain = 1.0f; side = 1.0f; mute = 1.0f; filterMix = 0.0f;
            for (auto& e : entrance) { e.reset(); e.set (20000.0f * 0.45f > sr * 0.45f ? sr * 0.45f : 9000.0f, 0.707f, sr); }
            ramp = 1.0f / (0.02f * sr);
            muteStep = 1.0f / (0.003f * sr);
            setPresence (0.0f);
        }

        /** The part of the plan this stage plays (for the strip and the tests). */
        struct Targets { float presenceDb = 0.0f, gainDb = 0.0f, side = 1.0f, subClear = 0.0f, filterOpen = 1.0f; bool silent = false; };

        /** What the plan asks of a track in `role` at `q` (quarters). */
        static Targets targetsAt (const suite::DirectorBus::Follower& f, double q, int role)
        {
            namespace dr = suite::director;
            Targets t;
            if (! f.isActive()) return t;
            const auto barQ = (double) f.info().barQ;
            for (int i = 0; i < f.size(); ++i)
            {
                const auto& m = f[i];
                if (q < m.startQ - 2.0 * barQ - 1.0e-6 || q > m.endQ + barQ) continue;
                // the track that leads this moment
                int heroRole = m.heroRole;
                if (heroRole == 0)
                    heroRole = m.hero == dr::pAzoth ? 7 : m.hero == dr::pKybalion ? 4 : m.hero == dr::pCorrespondence ? 5 : 0;
                // pre-mix: room for the hero, ramping in over the 2 bars before its window
                if (heroRole != 0 && role != heroRole && role != 1 && role != 2 && m.premixDb < 0.0f)
                {
                    const auto in = juce::jlimit (0.0, 1.0, (q - (m.startQ - 2.0 * barQ)) / (2.0 * barQ));
                    if (q < m.endQ) t.presenceDb = juce::jmin (t.presenceDb, m.premixDb * (float) in);
                }
                // a measured loudness jump, evened out over the first 2 bars after the change
                if (m.evenDb < 0.0f && q >= m.atQ && q < m.atQ + 2.0 * barQ)
                    t.gainDb = juce::jmin (t.gainDb, m.evenDb * (float) (1.0 - (q - m.atQ) / (2.0 * barQ)));
                if (m.kind & (dr::kIntoDrop | dr::kPin))
                {
                    // the hook's first bar back: pads and FX step aside
                    if ((role == 4 || role == 7) && q >= m.atQ && q < m.atQ + barQ) t.presenceDb = juce::jmin (t.presenceDb, m.hookDipDb);
                    // the build held back (4 bars, rising to the full hold), released on the one
                    if (m.buildHoldDb > 0.0f && q < m.atQ && q >= m.atQ - 4.0 * barQ && role != 7)
                        t.gainDb = juce::jmin (t.gainDb, -m.buildHoldDb * (float) ((q - (m.atQ - 4.0 * barQ)) / (4.0 * barQ)));
                    // the entrance: staggered, or through an opening filter
                    if (m.gesture[dr::pScene] == dr::gScStagger && q >= m.atQ)
                    {
                        if ((role == 2 && q < m.atQ + 1.0) || (role != 1 && role != 2 && q < m.atQ + 2.0)) t.silent = true;
                    }
                    if (m.gesture[dr::pScene] == dr::gScFilter && role != 1 && q >= m.atQ && q < m.atQ + barQ)
                        t.filterOpen = juce::jmin (t.filterOpen, (float) ((q - m.atQ) / barQ));
                    // the bass's sub clears for the impact
                    if (role == 2 && m.lowClearBeats > 0.0f && q >= m.atQ && q < m.atQ + m.lowClearBeats) t.subClear = 1.0f;
                    // narrow into it, wide on the one
                    if (role != 1 && role != 2)
                    {
                        if (q < m.atQ && q >= m.atQ - barQ) t.side = juce::jmin (t.side, 1.0f - m.narrowBefore * (float) ((q - (m.atQ - barQ)) / barQ));
                        else if (q >= m.atQ && q < m.atQ + barQ) t.side = juce::jmax (t.side, 1.0f + 0.5f * m.wideOnHit * (float) (1.0 - (q - m.atQ) / barQ));
                    }
                }
            }
            if (const auto s = f.silenceAt (q); s >= 0) t.silent = t.silent || ! (f[s].silence == dr::gSiKickOnly && role == 1);
            return t;
        }

        /** Plays the stage over a block (stereo or mono). Returns true if it touched the audio. */
        bool process (float* const* ch, int chs, int n, double ppq, double qPerSample, const suite::DirectorBus::Follower& f, int role, uint32_t& silenceReport)
        {
            if (! f.isActive() && presenceDb == 0.0f && gain >= 1.0f && side == 1.0f && subMix <= 0.0f && mute >= 1.0f && filterMix <= 0.0f) return false;
            Targets t;
            for (int i = 0; i < n; ++i)
            {
                if ((i & 31) == 0)
                {
                    t = f.isActive() ? targetsAt (f, ppq + i * qPerSample, role) : Targets {};
                    if (std::abs (t.presenceDb - presenceDb) > 0.01f)
                    {
                        presenceDb += juce::jlimit (-0.1f, 0.1f, t.presenceDb - presenceDb);   // 0.1 dB per 32 samples: smooth
                        setPresence (presenceDb);
                    }
                    if (t.silent) ++silenceReport;
                    // the entrance filter: 300 Hz opening to 20 kHz (on a log scale) across the bar
                    if (t.filterOpen < 1.0f)
                    {
                        const auto hz = 300.0f * std::pow (20000.0f / 300.0f, t.filterOpen);
                        for (auto& l : entrance) l.set (juce::jmin (hz, 0.45f * sr), 0.707f, sr);
                    }
                }
                filterMix += juce::jlimit (-ramp * 4.0f, ramp * 4.0f, (t.filterOpen < 1.0f ? 1.0f : 0.0f) - filterMix);
                const auto targetGain = juce::Decibels::decibelsToGain (t.gainDb);
                gain += juce::jlimit (-ramp, ramp, targetGain - gain);
                side += juce::jlimit (-ramp, ramp, t.side - side);
                subMix += juce::jlimit (-ramp * 4.0f, ramp * 4.0f, t.subClear - subMix);
                mute += juce::jlimit (-muteStep, muteStep, (t.silent ? 0.0f : 1.0f) - mute);
                float x[2] = { ch[0][i], chs > 1 ? ch[1][i] : ch[0][i] };
                for (int c = 0; c < 2; ++c)
                {
                    if (std::abs (presenceDb) > 0.01f) x[c] = presence[(size_t) c].process (x[c]);
                    const auto hp = sub[(size_t) c].highpass (x[c]);
                    if (subMix > 1.0e-4f) x[c] += subMix * (hp - x[c]);
                    if (filterMix > 1.0e-4f) { const auto lp = entrance[(size_t) c].lowpass (x[c]); x[c] += filterMix * (lp - x[c]); }
                }
                if (chs > 1 && std::abs (side - 1.0f) > 1.0e-4f)
                {
                    const auto mid = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]) * side;
                    x[0] = mid + s; x[1] = mid - s;
                }
                const auto g = gain * mute;
                ch[0][i] = x[0] * g;
                if (chs > 1) ch[1][i] = x[1] * g;
            }
            return true;
        }

        float presenceNow() const noexcept { return presenceDb; }
        float gainNow() const noexcept { return gain; }

    private:
        void setPresence (float db) { for (auto& b : presence) b.setPeak (2500.0f, 0.7f, db, sr); }
        float sr = 44100.0f, presenceDb = 0.0f, subMix = 0.0f, gain = 1.0f, side = 1.0f, mute = 1.0f, ramp = 0.001f, muteStep = 0.01f, filterMix = 0.0f;
        std::array<suite::Svf, 2> entrance;
        std::array<suite::Biquad, 2> presence;
        std::array<suite::Svf, 2> sub;
    };
}
