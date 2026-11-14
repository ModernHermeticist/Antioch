#pragma once

// Quintessence's second set of label-ready fixes on its own track:
//  - a tone per kind of section: ten octave bands, set step by step by Ouroboros toward the label
//    (the drop's tone, the breakdown's, ...), smoothly between sections;
//  - a width per kind of section (the image opening into the drop, narrower where mono suffers);
//  - drive: gentle saturation on a part that doesn't cut through (level kept);
//  - the kick's tail shortened in the drops (room for the bass), its hit untouched;
//  - the sub handed over through the builds (the bass's lows thinning as the build rises, back on
//    the drop);
//  - a choke: hats and FX cut for the last 16th before each drop (tails don't smear the hit);
//  - a subsonic filter (under 20 Hz) where the track carries energy there.
// Each acts only when latched (Ouroboros' checks asked) and its autopilot switch is on.

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

namespace qn
{
    class LabelDsp2
    {
    public:
        struct Context
        {
            double ppq = 0.0, bpm = 120.0, barQ = 4.0;
            int sectionType = -1, role = 0;
            float buildProgress = 0.0f;
            bool nextIsDrop = false, playing = false;
            double sectionEndQ = 0.0;
            int occurrence = 0;   // which drop this is (0 = the first)
        };
        // the mix fixes' share here: each drop's own tone (row 6 of toneDb: the later drops), the kick's tail following the tempo
        std::atomic<bool> laterDropTone { false }, kickTailTempo { false };
        std::atomic<bool> sectionTone { false }, sectionWidth { false }, drive { false }, kickTail { false }, subHandover { false }, choke { false }, subsonic { false };
        std::array<std::array<std::atomic<float>, 10>, 7> toneDb {};   // per kind of section, per octave band (dB, +-6); row 6: the later drops
        std::array<std::atomic<float>, 6> widthOffset {};              // per kind of section: -0.5 .. +0.5 (side gain - 1)

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            for (auto& ch : eq) for (auto& b : ch) b = {};
            for (auto& g : appliedDb) g.fill (0.0f);
            appliedType = -99;
            const auto a = std::exp (-2.0 * juce::MathConstants<double>::pi * 20.0 / fs);
            hpA = (float) a;
            hpState = { 0.0f, 0.0f }; hpPrev = { 0.0f, 0.0f };
            subA = 1.0f - (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 120.0 / fs);
            subLp = { 0.0f, 0.0f };
            envFast = envSlow = 0.0f; sinceHit = 1 << 30; tailGain = chokeGain = 1.0f;
            fastA = 1.0f - (float) std::exp (-1.0 / (0.003 * fs)); slowA = 1.0f - (float) std::exp (-1.0 / (0.1 * fs));
        }

        void process (float* const* ch, int chs, int n, const Context& c) noexcept
        {
            const auto t = juce::jlimit (-1, 5, c.sectionType);
            // the section's tone (coefficients when the section or its gains change); a later drop has its own row once it's set
            auto toneRow = t;
            if (laterDropTone.load() && t == 4 && c.occurrence >= 1)
                for (auto& v : toneDb[6]) if (std::abs (v.load()) > 0.01f) { toneRow = 6; break; }
            if (sectionTone.load() && t >= 0)
            {
                bool changed = toneRow != appliedType;
                for (int b = 0; b < 10; ++b)
                {
                    const auto want = juce::jlimit (-6.0f, 6.0f, toneDb[(size_t) toneRow][(size_t) b].load());
                    auto& now = appliedDb[(size_t) 0][(size_t) b];
                    const auto next = now + juce::jlimit (-0.25f, 0.25f, want - now);   // a quarter dB per block at most: no clicks
                    if (std::abs (next - now) > 1.0e-4f || changed) { now = next; setPeak (b, now); changed = true; }
                }
                appliedType = toneRow;
                for (int c2 = 0; c2 < juce::jmin (2, chs); ++c2)
                    for (int b = 0; b < 10; ++b)
                        if (std::abs (appliedDb[0][(size_t) b]) > 0.01f)
                        {
                            auto& f = eq[(size_t) c2][(size_t) b];
                            for (int i = 0; i < n; ++i) ch[c2][i] = f.process (ch[c2][i]);
                        }
            }
            const auto qps = c.bpm / 60.0 / fs;
            const auto width = sectionWidth.load() && t >= 0 ? 1.0f + juce::jlimit (-0.5f, 0.5f, widthOffset[(size_t) t].load()) : 1.0f;
            const auto handover = subHandover.load() && c.role == 2 && t == 3 ? juce::jlimit (0.0f, 1.0f, c.buildProgress) : 0.0f;
            const auto subCut = 1.0f - juce::Decibels::decibelsToGain (-6.0f * handover);
            const auto chokeOn = choke.load() && (c.role == 3 || c.role == 7) && c.nextIsDrop && c.playing;
            const auto tailOn = kickTail.load() && c.role == 1 && t == 4;
            const auto tempoScale = kickTailTempo.load() ? juce::jlimit (0.6, 1.5, 128.0 / juce::jmax (60.0, c.bpm)) : 1.0;   // the tail times set at 128 BPM
            for (int i = 0; i < n; ++i)
            {
                // the kick's tail: after 120 ms from each hit, down to -6 dB until the next one
                if (tailOn)
                {
                    const auto a = chs > 1 ? 0.5f * (std::abs (ch[0][i]) + std::abs (ch[1][i])) : std::abs (ch[0][i]);
                    envFast += fastA * (a - envFast); envSlow += slowA * (a - envSlow);
                    if (envFast > 2.0f * envSlow && envFast > 1.0e-3f && sinceHit > (int) (0.08 * tempoScale * fs)) sinceHit = 0; else ++sinceHit;
                    const auto target = sinceHit > (int) (0.12 * tempoScale * fs) ? 0.5f : 1.0f;
                    tailGain += (target > tailGain ? 0.02f : 0.0005f) * (target - tailGain);
                }
                else tailGain += 0.01f * (1.0f - tailGain);
                // the choke: the last 16th before the drop
                const auto q = c.ppq + qps * i;
                const auto inChoke = chokeOn && q >= c.sectionEndQ - 0.25 && q < c.sectionEndQ;
                chokeGain += 0.01f * ((inChoke ? 0.0f : 1.0f) - chokeGain);
                const auto g = tailGain * chokeGain;
                float l = ch[0][i], r = chs > 1 ? ch[1][i] : l;
                // width
                if (std::abs (width - 1.0f) > 1.0e-4f && chs > 1)
                {
                    const auto m = 0.5f * (l + r), s = 0.5f * (l - r) * width;
                    l = m + s; r = m - s;
                }
                // the sub handover
                if (subCut > 1.0e-4f)
                {
                    subLp[0] += subA * (l - subLp[0]); subLp[1] += subA * (r - subLp[1]);
                    l -= subCut * subLp[0]; r -= subCut * subLp[1];
                }
                // drive: tanh at about +4 dB into it, half mixed in, level kept
                if (drive.load())
                {
                    constexpr float k = 1.6f;   // tanh (k x) / k: unity for small signals, rounding the peaks
                    l = 0.5f * l + 0.5f * std::tanh (k * l) / k;
                    r = 0.5f * r + 0.5f * std::tanh (k * r) / k;
                }
                // subsonics
                if (subsonic.load())
                {
                    const float in[2] = { l, r };
                    float* out[2] = { &l, &r };
                    for (int k2 = 0; k2 < 2; ++k2)
                    {
                        const auto y = hpA * (hpState[(size_t) k2] + in[k2] - hpPrev[(size_t) k2]);
                        hpPrev[(size_t) k2] = in[k2]; hpState[(size_t) k2] = y;
                        *out[k2] = y;
                    }
                }
                ch[0][i] = l * g;
                if (chs > 1) ch[1][i] = r * g;
            }
        }
        float tailGainNow() const noexcept { return tailGain; }
        float chokeGainNow() const noexcept { return chokeGain; }

    private:
        struct Biquad
        {
            float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            float process (float x) noexcept { const auto y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        };
        void setPeak (int band, float db)
        {
            static const float centres[10] = { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
            const auto f0 = juce::jmin (centres[band], (float) fs * 0.45f);
            const auto A = std::pow (10.0f, db / 40.0f), w0 = 2.0f * juce::MathConstants<float>::pi * f0 / (float) fs;
            const auto alpha = std::sin (w0) / (2.0f * 1.0f);
            const auto a0 = 1.0f + alpha / A;
            for (auto& chn : eq)
            {
                auto& q = chn[(size_t) band];
                q.b0 = (1.0f + alpha * A) / a0; q.b1 = -2.0f * std::cos (w0) / a0; q.b2 = (1.0f - alpha * A) / a0;
                q.a1 = -2.0f * std::cos (w0) / a0; q.a2 = (1.0f - alpha / A) / a0;
            }
        }
        double fs = 44100.0;
        std::array<std::array<Biquad, 10>, 2> eq {};
        std::array<std::array<float, 10>, 1> appliedDb {};
        int appliedType = -99;
        float hpA = 0.997f, subA = 0.01f, envFast = 0.0f, envSlow = 0.0f, fastA = 0.1f, slowA = 0.01f, tailGain = 1.0f, chokeGain = 1.0f;
        std::array<float, 2> hpState {}, hpPrev {}, subLp {};
        int sinceHit = 1 << 30;
    };
}
