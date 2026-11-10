#pragma once

// Synthesised transition sounds. Each voice is triggered, then rendered sample by
// sample until it decays. No samples or allocation: everything is noise, sines
// and filters, so it's light and always in tune with the song's key.

#include "SuiteDSP.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>

namespace azoth
{
    /** Pitched sub boom (falls to the song's root) plus a click. */
    class Impact
    {
    public:
        void prepare (float sampleRate) { sr = sampleRate; active = false; }
        void trigger (float endHz, float level) { t = 0.0f; phase = 0.0f; target = endHz; gain = level; active = gain > 0.0f; }
        float process() noexcept
        {
            if (! active) return 0.0f;
            const auto hz = target + (110.0f - target) * std::exp (-t / 0.06f);
            phase += hz / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            const auto body = std::sin (suite::pi * 2.0f * phase) * std::exp (-t / 0.9f);
            const auto click = t < 0.005f ? noise.process() * (1.0f - t / 0.005f) * 0.5f : 0.0f;
            t += 1.0f / sr;
            if (t > 4.0f) active = false;
            return (body + click) * gain;
        }
    private:
        suite::Noise noise { 0x1A2B3C4Du };
        float sr = 48000.0f, t = 0.0f, phase = 0.0f, target = 55.0f, gain = 0.0f;
        bool active = false;
    };

    /** Bright, long noise crash. `tone` 0..1 sets how bright. */
    class Crash
    {
    public:
        void prepare (float sampleRate) { sr = sampleRate; active = false; hp.reset(); lp.reset(); }
        void trigger (float level, float tone)
        {
            t = 0.0f; gain = level; active = gain > 0.0f;
            hp.set (2500.0f, 0.7f, sr);
            lp.set (6000.0f + 12000.0f * tone, 0.7f, sr);
        }
        float process() noexcept
        {
            if (! active) return 0.0f;
            const auto env = std::min (1.0f, t / 0.002f) * std::exp (-t / 0.8f);
            t += 1.0f / sr;
            if (t > 5.0f) active = false;
            return lp.lowpass (hp.highpass (noise.process())) * env * gain * 0.8f;
        }
    private:
        suite::Noise noise { 0x5EED1234u };
        suite::Svf hp, lp;
        float sr = 48000.0f, t = 0.0f, gain = 0.0f;
        bool active = false;
    };

    /** Falling filtered noise + sub drop, over `seconds`. */
    class Downlifter
    {
    public:
        void prepare (float sampleRate) { sr = sampleRate; active = false; bp.reset(); }
        void trigger (float seconds, float subStartHz, float level)
        {
            t = 0.0f; length = std::max (0.25f, seconds); subHz = subStartHz; phase = 0.0f; gain = level; active = gain > 0.0f;
        }
        float process() noexcept
        {
            if (! active) return 0.0f;
            const auto x = t / length;
            bp.set (8000.0f * std::pow (150.0f / 8000.0f, std::min (1.0f, x)), 1.2f, sr);
            const auto noiseEnv = (1.0f - std::min (1.0f, x)) * std::min (1.0f, t / 0.01f);
            const auto hz = subHz * std::exp2 (-2.0f * std::min (1.0f, x * 2.0f));   // two octaves down over half the length
            phase += hz / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            const auto sub = std::sin (suite::pi * 2.0f * phase) * std::exp (-t / (length * 0.35f));
            t += 1.0f / sr;
            if (x >= 1.0f && t > length + 0.5f) active = false;
            return (bp.bandpass (noise.process()) * noiseEnv * 1.5f + 0.6f * sub) * gain;
        }
    private:
        suite::Noise noise { 0xD0D0CAFEu };
        suite::Svf bp;
        float sr = 48000.0f, t = 0.0f, length = 4.0f, subHz = 110.0f, phase = 0.0f, gain = 0.0f;
        bool active = false;
    };

    /** Short synth snare: band-passed noise plus a tuned body. */
    class Snare
    {
    public:
        void prepare (float sampleRate) { sr = sampleRate; active = false; bp.reset(); }
        void trigger (float level, float pitchRatio)
        {
            t = 0.0f; gain = level; ratio = pitchRatio; phase = 0.0f; active = gain > 0.0f;
            bp.set (1800.0f * pitchRatio, 0.8f, sr);
        }
        float process() noexcept
        {
            if (! active) return 0.0f;
            phase += 190.0f * ratio / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            const auto out = bp.bandpass (noise.process()) * 2.0f * std::exp (-t / 0.07f)
                           + 0.5f * std::sin (suite::pi * 2.0f * phase) * std::exp (-t / 0.04f);
            t += 1.0f / sr;
            if (t > 0.5f) active = false;
            return out * gain;
        }
    private:
        suite::Noise noise { 0xBEEF0001u };
        suite::Svf bp;
        float sr = 48000.0f, t = 0.0f, gain = 0.0f, ratio = 1.0f, phase = 0.0f;
        bool active = false;
    };

    /** Tonal riser: detuned saws on the song's root and fifth, sweeping up over the build
        while a filter opens. `progress` runs 0..1 across the riser; silent outside. */
    class Riser
    {
    public:
        void prepare (float sampleRate)
        {
            sr = sampleRate;
            for (size_t i = 0; i < saws.size(); ++i) saws[i].setPhase ((float) i * 0.23f);
            filter.reset();
            counter = 0;
        }

        float process (float progress, float rootHz, float rangeSemitones, float level) noexcept
        {
            if (progress < 0.0f || progress >= 1.0f || level <= 0.0f)
                return 0.0f;
            if (counter-- <= 0)
            {
                counter = 32;
                const auto bend = std::exp2 (rangeSemitones * std::pow (progress, 1.6f) / 12.0f);
                const float detune[] = { 1.0f, 1.007f, 0.993f, 1.5f, 1.505f };
                for (size_t i = 0; i < saws.size(); ++i)
                    saws[i].setFrequency (rootHz * bend * detune[i], sr);
                filter.set (300.0f * std::pow (40.0f, progress), 0.9f, sr);
            }
            float x = 0.0f;
            for (size_t i = 0; i < saws.size(); ++i)
                x += saws[i].process() * (i < 3 ? 1.0f : 0.5f);
            return filter.lowpass (x) * 0.12f * level * std::pow (progress, 1.5f);
        }

    private:
        float sr = 48000.0f;
        std::array<suite::SawOsc, 5> saws;
        suite::Svf filter;
        int counter = 0;
    };

    //==============================================================================
    /** One of your own samples (mono), with the rate it was recorded at. */
    struct Sample
    {
        juce::AudioBuffer<float> mono;
        double rate = 48000.0;
        juce::String path;
    };

    /** Plays a user sample in place of a synthesised voice. The message thread hands over a
        new sample with setSample(); the audio thread picks it up on the next trigger. The
        processor keeps every sample it ever handed over alive, so nothing is freed here. */
    class SamplePlayer
    {
    public:
        void prepare (float sampleRate) { hostRate = sampleRate; active = false; }
        void setSample (std::shared_ptr<const Sample> s) { std::atomic_store (&pending, std::move (s)); }
        bool loaded() const { return std::atomic_load (&pending) != nullptr; }
        void trigger (float level)
        {
            current = std::atomic_load (&pending);
            active = current != nullptr && level > 0.0f && current->mono.getNumSamples() > 1;
            if (! active) return;
            pos = 0.0;
            gain = level;
            step = current->rate / juce::jmax (1.0f, hostRate);
        }
        float process() noexcept
        {
            if (! active) return 0.0f;
            const auto i = (int) pos;
            const auto n = current->mono.getNumSamples();
            if (i + 1 >= n) { active = false; return 0.0f; }
            const auto* d = current->mono.getReadPointer (0);
            const auto f = (float) (pos - i);
            pos += step;
            return (d[i] + f * (d[i + 1] - d[i])) * gain;
        }
    private:
        std::shared_ptr<const Sample> pending, current;
        float hostRate = 48000.0f, gain = 0.0f;
        double pos = 0.0, step = 1.0;
        bool active = false;
    };
}
