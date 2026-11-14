#pragma once

// Quintessence's label-ready fixes on its own track, each switched on (latched, saved with the
// project) when Ouroboros' checks find the problem and its autopilot switch is on:
//  - pad motion: a slow filter movement over the 8-bar phrase on a pad that never changes;
//  - the breakdown low cut: a 120 Hz high-pass in the breakdowns (not on kick or bass);
//  - input staging: what comes in brought to about -18 dBFS RMS for the processing, and given
//    back after it (the level going out doesn't change);
//  - the vocal delay: an early vocal delayed onto the grid (a fix message, up to 60 ms).

#include "SuiteDSP.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace qn
{
    class LabelDsp
    {
    public:
        std::atomic<bool> padMotion { false }, breakdownLowCut { false }, inputStage { false };
        std::atomic<float> stageDb { 0.0f }, vocalDelayMs { 0.0f };

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (auto& f : lowCut) f.set (120.0f, 0.707f, fs);
            shelfA = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 2500.0f / fs);
            delay.assign ((size_t) (0.07 * sampleRate) + 2, 0.0f);
            delay2 = delay;
            writePos = 0;
            lp = { 0.0f, 0.0f };
            cutMix = 0.0f; stageGain = 1.0f; delayNow = 0.0f;
        }
        /** Before the processing: input staging. */
        void input (float* const* ch, int chs, int n) noexcept
        {
            const auto target = inputStage.load() ? juce::Decibels::decibelsToGain (juce::jlimit (-12.0f, 12.0f, stageDb.load())) : 1.0f;
            for (int c = 0; c < chs; ++c) juce::FloatVectorOperations::multiply (ch[c], stageGain, n);   // block-constant; ramped below
            stageGain += 0.05f * (target - stageGain);
            stageNow = stageGain;
        }
        /** After the processing: the level given back, pad motion, the breakdown low cut, the vocal delay. */
        void output (float* const* ch, int chs, int n, double ppq, double bpm, double barQ, int sectionType, int role, bool playing) noexcept
        {
            if (std::abs (stageNow - 1.0f) > 1.0e-5f) for (int c = 0; c < chs; ++c) juce::FloatVectorOperations::multiply (ch[c], 1.0f / stageNow, n);
            const auto qps = bpm / 60.0 / (double) fs;
            const auto cutTarget = breakdownLowCut.load() && role != 1 && role != 2 && sectionType == 2 ? 1.0f : 0.0f;
            const auto motion = padMotion.load() && playing;
            const auto delaySamples = juce::jlimit (0.0f, (float) delay.size() - 2.0f, vocalDelayMs.load() * 0.001f * fs);
            for (int i = 0; i < n; ++i)
            {
                cutMix += 0.0005f * (cutTarget - cutMix);
                // pad motion: +-1.5 dB on the top, a sine over the 8-bar phrase
                float shelfGain = 0.0f;
                if (motion)
                {
                    const auto phase = std::fmod ((ppq + qps * i) / (8.0 * juce::jmax (1.0, barQ)), 1.0);
                    shelfGain = juce::Decibels::decibelsToGain (1.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase)) - 1.0f;
                }
                delayNow += 0.0002f * (delaySamples - delayNow);
                for (int c = 0; c < chs && c < 2; ++c)
                {
                    auto x = ch[c][i];
                    if (motion)
                    {
                        lp[(size_t) c] += shelfA * (x - lp[(size_t) c]);
                        x += shelfGain * (x - lp[(size_t) c]);
                    }
                    if (cutMix > 1.0e-4f) x += cutMix * (lowCut[(size_t) c].highpass (x) - x);
                    else lowCut[(size_t) c].highpass (x);   // keep the filter warm
                    if (delayNow > 0.01f)
                    {
                        auto& line = c == 0 ? delay : delay2;
                        line[(size_t) writePos] = x;
                        const auto size = (int) line.size();
                        const auto rp = (float) writePos - delayNow;
                        const auto r0 = (int) std::floor (rp);
                        const auto frac = rp - (float) r0;
                        const auto a = line[(size_t) ((r0 % size + size) % size)], b = line[(size_t) (((r0 + 1) % size + size) % size)];
                        x = a + frac * (b - a);
                    }
                    ch[c][i] = x;
                }
                if (delayNow > 0.01f) writePos = (writePos + 1) % (int) delay.size();
            }
        }
        float delayInUseMs() const noexcept { return delayNow / fs * 1000.0f; }

    private:
        float fs = 44100.0f, shelfA = 0.3f, cutMix = 0.0f, stageGain = 1.0f, stageNow = 1.0f, delayNow = 0.0f;
        std::array<suite::Svf, 2> lowCut;
        std::array<float, 2> lp { 0.0f, 0.0f };
        std::vector<float> delay, delay2;
        int writePos = 0;
    };
}
