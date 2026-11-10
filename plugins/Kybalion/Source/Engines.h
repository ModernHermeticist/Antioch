#pragma once

// Rhythm (trance gate) and Polarity (ducker). Both are driven by the musical
// position in quarter notes, so they sit exactly on the host's grid.

#include "SuiteDSP.h"
#include "Parameters.h"
#include <array>
#include <cmath>

//==============================================================================
class TranceGate
{
public:
    struct Settings
    {
        std::array<float, Params::numSteps> steps {};
        double stepQ = 0.25;
        float length = 0.6f, attackMs = 2.0f, releaseMs = 15.0f, depth = 1.0f, swing = 0.0f;
        int stereoOffset = 0;
        float filter = 0.0f, res = 0.25f;
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        env = { 1.0f, 1.0f };
        for (auto& f : filters) f.reset();
    }

    void update (const Settings& newSettings)
    {
        s = newSettings;
        attackCoeff  = 1.0f - std::exp (-1.0f / (juce::jmax (0.1f, s.attackMs)  * 0.001f * sr));
        releaseCoeff = 1.0f - std::exp (-1.0f / (juce::jmax (0.1f, s.releaseMs) * 0.001f * sr));
    }

    /** Which step is playing at `posQ`, and how far through it we are (0..1), with swing. */
    void locate (double posQ, int& step, double& phase) const noexcept
    {
        const auto pairQ = 2.0 * s.stepQ;
        const auto pairIndex = (long long) std::floor (posQ / pairQ);
        auto inPair = posQ - (double) pairIndex * pairQ;
        const auto boundary = s.stepQ * (1.0 + s.swing);   // swing delays every second step

        long long index = pairIndex * 2;
        if (inPair < boundary)
        {
            phase = inPair / boundary;
        }
        else
        {
            ++index;
            phase = (inPair - boundary) / (pairQ - boundary);
        }
        step = (int) (((index % Params::numSteps) + Params::numSteps) % Params::numSteps);
    }

    void process (double posQ, float& left, float& right) noexcept
    {
        int step; double phase;
        locate (posQ, step, phase);
        currentStep = step;

        float* ch[2] = { &left, &right };
        for (size_t c = 0; c < 2; ++c)
        {
            const auto idx = (step + (c == 1 ? s.stereoOffset : 0)) % Params::numSteps;
            const auto target = phase < (double) s.length ? s.steps[(size_t) idx] : 0.0f;
            env[c] += (target > env[c] ? attackCoeff : releaseCoeff) * (target - env[c]);

            auto x = *ch[c];
            if (s.filter > 0.001f)
            {
                // The gate also opens a low-pass filter: closed steps sound darker, not just quieter.
                const auto cutoff = 20000.0f * std::pow (300.0f / 20000.0f, s.filter * (1.0f - env[c]));
                filters[c].set (cutoff, 0.7f + 5.0f * s.res, sr);
                x = filters[c].lowpass (x);
            }
            *ch[c] = x * (1.0f - s.depth * (1.0f - env[c]));
        }
    }

    int getCurrentStep() const noexcept { return currentStep; }
    float getEnvelope() const noexcept { return env[0]; }

private:
    Settings s;
    std::array<float, 2> env { 1.0f, 1.0f };
    std::array<suite::Svf, 2> filters;
    float sr = 44100.0f, attackCoeff = 0.1f, releaseCoeff = 0.01f;
    int currentStep = 0;
};

//==============================================================================
class Ducker
{
public:
    struct Settings
    {
        float depth = 0.85f, attackMs = 1.0f, holdMs = 20.0f, releaseMs = 250.0f, curve = 1.0f;
        float splitHz = 0.0f;
        bool breakdown = false;
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& f : crossover) f.reset();
        gain = 1.0f;
        smooth = 1.0f - std::exp (-1.0f / (0.0003f * sr)); // 0.3 ms de-click
    }

    void update (const Settings& newSettings)
    {
        s = newSettings;
        if (s.splitHz >= Params::Duck::fullBandBelow)
            for (auto& f : crossover) f.set (s.splitHz, 0.70710678f, sr); // Butterworth stages -> LR4
    }

    /** Duck amount (0..1) `ms` after a trigger: attack down, hold, then a curved release. */
    float amountAt (float ms) const noexcept
    {
        if (s.breakdown || ms < 0.0f)
            return 0.0f;
        if (ms < s.attackMs)
            return s.depth * ms / s.attackMs;
        ms -= s.attackMs;
        if (ms < s.holdMs)
            return s.depth;
        const auto r = (ms - s.holdMs) / juce::jmax (1.0f, s.releaseMs);
        return r >= 1.0f ? 0.0f : s.depth * (1.0f - std::pow (r, s.curve));
    }

    void process (float msSinceTrigger, float& left, float& right) noexcept
    {
        const auto target = 1.0f - amountAt (msSinceTrigger);
        gain += smooth * (target - gain);

        if (s.splitHz >= Params::Duck::fullBandBelow)
        {
            // Duck only the lows. A 4th-order Linkwitz-Riley crossover keeps the bands
            // cleanly apart (a simple "x minus low-pass" split leaks most of the bass into
            // the high band through phase shift) and the bands sum flat when not ducking.
            float* ch[2] = { &left, &right };
            for (size_t c = 0; c < 2; ++c)
            {
                const auto x = *ch[c];
                const auto low  = crossover[c * 4 + 1].lowpass (crossover[c * 4 + 0].lowpass (x));
                const auto high = crossover[c * 4 + 3].highpass (crossover[c * 4 + 2].highpass (x));
                *ch[c] = low * gain + high;
            }
        }
        else
        {
            left *= gain;
            right *= gain;
        }
    }

    float getGain() const noexcept { return gain; }

private:
    Settings s;
    std::array<suite::Svf, 8> crossover; // per channel: 2 low-pass + 2 high-pass stages
    float sr = 44100.0f, gain = 1.0f, smooth = 0.5f;
};

//==============================================================================
/** Detects kick hits in a sidechain signal and reports time since the last one. */
class SidechainTrigger
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        follower.set (0.3f, 60.0f, sr);
        follower.reset();
        msSince = 1.0e9f;
        armed = true;
    }

    float process (float sidechain, float thresholdGain) noexcept
    {
        const auto e = follower.process (sidechain);
        msSince += 1000.0f / sr;

        if (armed && e > thresholdGain)
        {
            msSince = 0.0f;
            armed = false;
        }
        else if (! armed && e < thresholdGain * 0.5f && msSince > 40.0f)
        {
            armed = true; // re-arm once the hit has died down
        }
        return msSince;
    }

private:
    suite::EnvelopeFollower follower;
    float sr = 44100.0f, msSince = 1.0e9f;
    bool armed = true;
};
