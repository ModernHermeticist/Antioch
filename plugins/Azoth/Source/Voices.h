#pragma once

// Synthesised transition sounds. Each voice is triggered, then rendered sample by
// sample until it decays. No samples or allocation: everything is noise, sines
// and filters, so it's light and always in tune with the song's key.

#include "SuiteDSP.h"

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
}
