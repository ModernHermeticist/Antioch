#pragma once

// Vocal unmasking: a sidechain-keyed dynamic EQ.
//
// The sidechain (the vocal) is split into 8 bands (2/3-octave spacing, 250 Hz - 6.3 kHz)
// and each band's level is followed. Where the vocal is both loud enough (above the
// sensitivity threshold) and prominent (within `focus` dB of its own loudest band),
// a matching peaking cut opens on the music, up to `depth` dB. With no vocal there are
// no cuts, and a 0 dB peaking filter is an exact pass-through.

#include "SuiteDSP.h"
#include <array>
#include <atomic>

class Unmasker
{
public:
    static constexpr int numBands = 8;
    static constexpr std::array<float, numBands> centres { 250.0f, 400.0f, 630.0f, 1000.0f, 1600.0f, 2500.0f, 4000.0f, 6300.0f };

    struct Settings
    {
        float depthDb = 6.0f;          // largest cut per band
        float thresholdDb = -40.0f;    // vocal band level where cutting starts (full cut 12 dB above)
        float focusDb = 12.0f;         // bands more than this below the vocal's loudest band are left alone
        float attackMs = 10.0f, releaseMs = 150.0f;
        float mix = 1.0f;              // 0 = no cuts
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (int b = 0; b < numBands; ++b)
        {
            detect[(size_t) b].set (centres[(size_t) b], 2.0f, sr);
            detect[(size_t) b].reset();
            level[(size_t) b].reset();
            for (auto& ch : cut) { ch[(size_t) b].reset(); ch[(size_t) b].setPeak (centres[(size_t) b], cutQ, 0.0f, sr); }
            gainDb[(size_t) b] = 0.0f;
            shownCut[(size_t) b].store (0.0f);
            shownKey[(size_t) b].store (-100.0f);
        }
        counter = 0;
        update (settings);
    }

    void update (const Settings& s) noexcept
    {
        settings = s;
        for (auto& e : level) e.set (s.attackMs, s.releaseMs, sr);
        smoothing = 1.0f - std::exp (-(float) updateInterval / (0.001f * std::max (1.0f, s.attackMs) * sr));
        releaseSmoothing = 1.0f - std::exp (-(float) updateInterval / (0.001f * std::max (1.0f, s.releaseMs) * sr));
    }

    /** Process one stereo sample; `key` is the (mono) sidechain sample, 0 when there's none. */
    void process (float& l, float& r, float key) noexcept
    {
        for (int b = 0; b < numBands; ++b)
            level[(size_t) b].process (detect[(size_t) b].bandpass (key));

        if (++counter >= updateInterval)
        {
            counter = 0;
            recompute();
        }

        for (int b = 0; b < numBands; ++b)
        {
            l = cut[0][(size_t) b].process (l);
            r = cut[1][(size_t) b].process (r);
        }
    }

    /** Any thread: the current cut and vocal level per band (for the display). */
    float getCutDb (int band) const noexcept { return shownCut[(size_t) band].load (std::memory_order_relaxed); }
    float getKeyDb (int band) const noexcept { return shownKey[(size_t) band].load (std::memory_order_relaxed); }

private:
    void recompute() noexcept
    {
        std::array<float, numBands> db {};
        float loudest = -100.0f;
        for (int b = 0; b < numBands; ++b)
        {
            db[(size_t) b] = juce::Decibels::gainToDecibels (level[(size_t) b].value() * 0.7071f, -100.0f);   // RMS of a sine in the band
            shownKey[(size_t) b].store (db[(size_t) b], std::memory_order_relaxed);
            loudest = std::max (loudest, db[(size_t) b]);
        }

        for (int b = 0; b < numBands; ++b)
        {
            const auto loudEnough = juce::jlimit (0.0f, 1.0f, (db[(size_t) b] - settings.thresholdDb) / 12.0f);
            const auto prominent = juce::jlimit (0.0f, 1.0f, 1.0f - (loudest - db[(size_t) b]) / std::max (1.0f, settings.focusDb));
            const auto target = -settings.depthDb * settings.mix * loudEnough * prominent;
            auto& g = gainDb[(size_t) b];
            g += (target < g ? smoothing : releaseSmoothing) * (target - g);
            if (std::abs (g) < 1.0e-4f && std::abs (target) < 1.0e-9f) g = 0.0f;
            shownCut[(size_t) b].store (g, std::memory_order_relaxed);
            for (auto& ch : cut)
                ch[(size_t) b].setPeak (centres[(size_t) b], cutQ, g, sr);
        }
    }

    static constexpr float cutQ = 1.8f;
    static constexpr int updateInterval = 32;

    float sr = 48000.0f, smoothing = 0.1f, releaseSmoothing = 0.01f;
    Settings settings;
    std::array<suite::Svf, numBands> detect;
    std::array<suite::EnvelopeFollower, numBands> level;
    std::array<std::array<suite::Biquad, numBands>, 2> cut;
    std::array<float, numBands> gainDb {};
    std::array<std::atomic<float>, numBands> shownCut {}, shownKey {};
    int counter = 0;
};
