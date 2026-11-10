#pragma once

// Header-only DSP building blocks shared by every plugin in the suite.
// Everything here is allocation-free after prepare() and safe on the audio thread.

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

namespace suite
{
    constexpr float pi = juce::MathConstants<float>::pi;

    //==============================================================================
    /** Circular buffer with fractional (4-point Hermite) reads. */
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples)
        {
            size = juce::nextPowerOfTwo (maxDelaySamples + 4);
            mask = size - 1;
            buffer.assign ((size_t) size, 0.0f);
            writePos = 0;
        }

        void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        void push (float x) noexcept
        {
            buffer[(size_t) writePos] = x;
            writePos = (writePos + 1) & mask;
        }

        /** Reads the sample written `delay` samples ago (delay >= 1 after push). */
        float read (float delay) const noexcept
        {
            delay = juce::jlimit (1.0f, (float) (size - 4), delay);
            const auto whole = (int) delay;
            const auto frac  = delay - (float) whole;
            const auto base  = writePos - whole;

            const auto xm1 = at (base + 1);
            const auto x0  = at (base);
            const auto x1  = at (base - 1);
            const auto x2  = at (base - 2);

            const auto c1 = 0.5f * (x1 - xm1);
            const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * frac + c2) * frac + c1) * frac + x0;
        }

        /** Integer read, cheaper when no interpolation is needed. */
        float readInt (int delay) const noexcept { return at (writePos - juce::jlimit (1, size - 1, delay)); }

        int capacity() const noexcept { return size - 4; }

    private:
        float at (int index) const noexcept { return buffer[(size_t) (index & mask)]; }

        std::vector<float> buffer;
        int size = 0, mask = 0, writePos = 0;
    };

    //==============================================================================
    /** Two-head crossfading delay-line pitch shifter (granular, ~50 ms grains).
        Crossfades to a clean bypass at unison so unshifted signals keep exact
        timing and don't pick up comb colouration. */
    class PitchShifter
    {
    public:
        void prepare (double sampleRate, float windowSeconds = 0.05f)
        {
            window = (float) sampleRate * windowSeconds;
            line.prepare ((int) window * 2 + 8);
            fadeStep = (float) (1.0 / (0.03 * sampleRate));
            reset();
        }

        void reset()
        {
            line.reset();
            phase = 0.0f;
            shiftMix = targetMix;
        }

        void setSemitones (float semitones) noexcept
        {
            if (std::abs (semitones) < 0.01f)
            {
                targetMix = 0.0f;
                return;
            }

            targetMix = 1.0f;
            ratio = std::exp2 (semitones / 12.0f);
        }

        /** Average extra delay the shifter adds when engaged (for timing compensation). */
        float latency() const noexcept { return shiftMix * window * 0.5f; }

        float process (float x) noexcept
        {
            line.push (x);

            if (shiftMix < targetMix)      shiftMix = std::min (targetMix, shiftMix + fadeStep);
            else if (shiftMix > targetMix) shiftMix = std::max (targetMix, shiftMix - fadeStep);

            if (shiftMix <= 0.0f)
                return x;

            phase += (1.0f - ratio) / window;
            phase -= std::floor (phase);

            auto phase2 = phase + 0.5f;
            if (phase2 >= 1.0f) phase2 -= 1.0f;

            // sin^2 windows offset by half a period sum to exactly 1.
            const auto g1 = std::sin (pi * phase);
            const auto g2 = std::sin (pi * phase2);

            const auto shifted = line.read (1.0f + phase * window)  * g1 * g1
                               + line.read (1.0f + phase2 * window) * g2 * g2;

            return x + shiftMix * (shifted - x);
        }

    private:
        DelayLine line;
        float window = 2400.0f, phase = 0.0f, ratio = 1.0f;
        float shiftMix = 0.0f, targetMix = 0.0f, fadeStep = 0.001f;
    };

    //==============================================================================
    /** Feedback comb filter that rings at a given pitch, with damping inside
        the loop so high harmonics decay faster (like a plucked string). */
    class CombResonator
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            line.prepare ((int) (sr / 20.0) + 8); // down to 20 Hz
            reset();
        }

        void reset()
        {
            line.reset();
            lpState = 0.0f;
            currentDelay = targetDelay;
        }

        void setFrequency (float hz) noexcept
        {
            hz = juce::jlimit (20.0f, sampleRate * 0.45f, hz);
            targetDelay = sampleRate / hz;
            if (currentDelay <= 0.0f)
                currentDelay = targetDelay;
        }

        void setFeedback (float g) noexcept   { feedback = g; }
        void setDamping (float coeff) noexcept { dampCoeff = coeff; }

        float process (float x) noexcept
        {
            currentDelay += 0.002f * (targetDelay - currentDelay);

            const auto delayed = line.read (currentDelay);
            lpState += dampCoeff * (delayed - lpState);

            const auto y = x + feedback * lpState;
            line.push (y);

            // Normalise so the resonant peak stays roughly at input level.
            return y * (1.0f - feedback);
        }

    private:
        DelayLine line;
        float sampleRate = 44100.0f;
        float targetDelay = 0.0f, currentDelay = 0.0f;
        float feedback = 0.0f, dampCoeff = 0.5f, lpState = 0.0f;
    };

    //==============================================================================
    /** One-pole TPT filter giving both lowpass and highpass outputs (6 dB/oct). */
    class OnePole
    {
    public:
        void setCutoff (float hz, float sampleRate) noexcept
        {
            const auto g = std::tan (pi * juce::jlimit (5.0f, sampleRate * 0.49f, hz) / sampleRate);
            G = g / (1.0f + g);
        }

        void reset() noexcept { s = 0.0f; }

        float lowpass (float x) noexcept
        {
            const auto v = G * (x - s);
            const auto lp = v + s;
            s = lp + v;
            return lp;
        }

        float highpass (float x) noexcept { return x - lowpass (x); }

    private:
        float G = 0.5f, s = 0.0f;
    };

    //==============================================================================
    /** Two-pole TPT state-variable filter (12 dB/oct) with LP/HP/BP outputs. */
    class Svf
    {
    public:
        struct Out { float lp, bp, hp; };

        void set (float hz, float q, float sampleRate) noexcept
        {
            g = std::tan (pi * juce::jlimit (10.0f, sampleRate * 0.49f, hz) / sampleRate);
            k = 1.0f / juce::jmax (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void reset() noexcept { ic1 = ic2 = 0.0f; }

        Out process (float x) noexcept
        {
            const auto v3 = x - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return { v2, v1, x - k * v1 - v2 };
        }

        float lowpass (float x) noexcept  { return process (x).lp; }
        float highpass (float x) noexcept { return process (x).hp; }
        float bandpass (float x) noexcept { return process (x).bp; }

    private:
        float g = 0.5f, k = 1.4f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    //==============================================================================
    /** RBJ-cookbook biquad (transposed direct form II): peaking and shelving EQ. */
    class Biquad
    {
    public:
        void reset() noexcept { z1 = z2 = 0.0f; }

        void setPeak (float hz, float q, float gainDb, float sampleRate) noexcept
        {
            const auto A = std::pow (10.0f, gainDb / 40.0f);
            const auto w = 2.0f * pi * juce::jlimit (10.0f, sampleRate * 0.49f, hz) / sampleRate;
            const auto alpha = std::sin (w) / (2.0f * q);
            const auto cw = std::cos (w);
            setCoeffs (1.0f + alpha * A, -2.0f * cw, 1.0f - alpha * A, 1.0f + alpha / A, -2.0f * cw, 1.0f - alpha / A);
        }

        void setHighShelf (float hz, float gainDb, float sampleRate) noexcept { shelf (hz, gainDb, sampleRate, true); }
        void setLowShelf (float hz, float gainDb, float sampleRate) noexcept  { shelf (hz, gainDb, sampleRate, false); }

        float process (float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

    private:
        void shelf (float hz, float gainDb, float sampleRate, bool high) noexcept
        {
            const auto A = std::pow (10.0f, gainDb / 40.0f);
            const auto w = 2.0f * pi * juce::jlimit (10.0f, sampleRate * 0.49f, hz) / sampleRate;
            const auto cw = std::cos (w), sw = std::sin (w);
            const auto alpha = sw / 2.0f * std::sqrt (2.0f); // shelf slope S = 1
            const auto k = 2.0f * std::sqrt (A) * alpha;
            if (high)
                setCoeffs (A * ((A + 1) + (A - 1) * cw + k), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - k),
                           (A + 1) - (A - 1) * cw + k, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - k);
            else
                setCoeffs (A * ((A + 1) - (A - 1) * cw + k), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - k),
                           (A + 1) + (A - 1) * cw + k, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - k);
        }

        void setCoeffs (float nb0, float nb1, float nb2, float na0, float na1, float na2) noexcept
        {
            b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
        }

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
    };

    //==============================================================================
    /** 4th-order Linkwitz-Riley crossover: low + high sum to an allpass (flat magnitude). */
    class Crossover
    {
    public:
        void set (float hz, float sampleRate) noexcept
        {
            for (auto& f : stages) f.set (hz, 0.70710678f, sampleRate);
        }

        void reset() noexcept { for (auto& f : stages) f.reset(); }

        void process (float x, float& low, float& high) noexcept
        {
            low  = stages[1].lowpass (stages[0].lowpass (x));
            high = stages[3].highpass (stages[2].highpass (x));
        }

        /** Same phase response as process() but without splitting, to keep other bands aligned. */
        float allpass (float x) noexcept
        {
            float l, h;
            process (x, l, h);
            return l + h;
        }

    private:
        std::array<Svf, 4> stages;
    };

    //==============================================================================
    /** Peak envelope follower with separate attack and release. */
    class EnvelopeFollower
    {
    public:
        void set (float attackMs, float releaseMs, float sampleRate) noexcept
        {
            attack  = 1.0f - std::exp (-1.0f / (attackMs  * 0.001f * sampleRate));
            release = 1.0f - std::exp (-1.0f / (releaseMs * 0.001f * sampleRate));
        }

        void reset() noexcept { env = 0.0f; }

        float process (float x) noexcept
        {
            const auto a = std::abs (x);
            env += (a > env ? attack : release) * (a - env);
            return env;
        }

        float value() const noexcept { return env; }

    private:
        float attack = 0.1f, release = 0.001f, env = 0.0f;
    };

    //==============================================================================
    /** Schroeder allpass diffuser. */
    class Allpass
    {
    public:
        void prepare (int maxSamples) { line.prepare (maxSamples + 2); }
        void reset() { line.reset(); }
        void setDelay (float samples) noexcept { delay = samples; }

        float process (float x, float g = 0.62f) noexcept
        {
            const auto d = line.read (delay);
            const auto w = x + g * d;
            line.push (w);
            return d - g * w;
        }

    private:
        DelayLine line;
        float delay = 100.0f;
    };

    //==============================================================================
    /** 8-line feedback delay network reverb with input diffusion, in-loop damping
        and slowly modulated line lengths (lush, chorused tails without metallic ringing). */
    class FdnReverb
    {
    public:
        static constexpr int numLines = 8;

        void prepare (double sr)
        {
            sampleRate = (float) sr;
            const auto maxLen = (int) (0.12 * sr * maxSize) + 64;
            for (auto& l : lines)
                l.prepare (maxLen);

            static constexpr float diffMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
            for (size_t i = 0; i < diffusers.size(); ++i)
            {
                diffusers[i].prepare ((int) (0.02 * sr));
                // Left/right use slightly different lengths for decorrelation.
                diffusers[i].setDelay (diffMs[i % 4] * 0.001f * sampleRate * (i < 4 ? 1.0f : 1.13f));
            }

            for (int i = 0; i < numLines; ++i)
                lfoPhase[(size_t) i] = (float) i / (float) numLines;

            reset();
            update();
        }

        void reset()
        {
            for (auto& l : lines) l.reset();
            for (auto& d : diffusers) d.reset();
            damp.fill (0.0f);
        }

        /** decaySeconds = RT60, size 0..1, dampingHz = in-loop lowpass, modulation 0..1. */
        void setParameters (float decaySeconds, float size, float dampingHz, float modulation)
        {
            if (juce::exactlyEqual (decaySeconds, decay) && juce::exactlyEqual (size, sizeParam)
                && juce::exactlyEqual (dampingHz, dampHz) && juce::exactlyEqual (modulation, mod))
                return;
            decay = decaySeconds; sizeParam = size; dampHz = dampingHz; mod = modulation;
            update();
        }

        void process (float inL, float inR, float& outL, float& outR) noexcept
        {
            // Input diffusion
            for (size_t i = 0; i < 4; ++i)
            {
                inL = diffusers[i].process (inL);
                inR = diffusers[i + 4].process (inR);
            }

            std::array<float, numLines> v;
            for (size_t i = 0; i < (size_t) numLines; ++i)
            {
                lfoPhase[i] += lfoInc[i];
                if (lfoPhase[i] >= 1.0f) lfoPhase[i] -= 1.0f;
                const auto modOffset = modDepth * std::sin (2.0f * pi * lfoPhase[i]);

                const auto y = lines[i].read (lengths[i] + modOffset);
                damp[i] += dampCoeff * (y - damp[i]);
                v[i] = damp[i] * gains[i];
            }

            outL = v[0] - v[2] + v[4] - v[6] + 0.5f * (v[1] - v[7]);
            outR = v[1] - v[3] + v[5] - v[7] + 0.5f * (v[0] - v[6]);
            outL *= 0.35f;
            outR *= 0.35f;

            hadamard (v);

            for (size_t i = 0; i < (size_t) numLines; ++i)
                lines[i].push (v[i] + ((i & 1) ? inR : inL) * 0.5f);
        }

    private:
        static void hadamard (std::array<float, numLines>& v) noexcept
        {
            for (size_t h = 1; h < (size_t) numLines; h *= 2)
                for (size_t i = 0; i < (size_t) numLines; i += h * 2)
                    for (size_t j = i; j < i + h; ++j)
                    {
                        const auto a = v[j], b = v[j + h];
                        v[j] = a + b;
                        v[j + h] = a - b;
                    }

            const auto norm = 1.0f / std::sqrt ((float) numLines);
            for (auto& x : v) x *= norm;
        }

        void update()
        {
            static constexpr float baseMs[numLines] = { 31.7f, 37.1f, 41.3f, 43.9f, 47.3f, 53.1f, 59.9f, 67.7f };
            static constexpr float lfoHz[numLines]  = { 0.11f, 0.17f, 0.23f, 0.29f, 0.37f, 0.43f, 0.53f, 0.61f };
            const auto scale = 0.5f + juce::jlimit (0.0f, 1.0f, sizeParam) * (maxSize - 0.5f);

            for (size_t i = 0; i < (size_t) numLines; ++i)
            {
                lengths[i] = baseMs[i] * 0.001f * sampleRate * scale;
                gains[i] = std::pow (10.0f, -3.0f * lengths[i] / (juce::jmax (0.1f, decay) * sampleRate));
                lfoInc[i] = lfoHz[i] / sampleRate;
            }

            dampCoeff = 1.0f - std::exp (-2.0f * pi * juce::jlimit (200.0f, sampleRate * 0.45f, dampHz) / sampleRate);
            modDepth = mod * 0.0015f * sampleRate; // up to ~1.5 ms of wobble
        }

        static constexpr float maxSize = 2.0f;
        std::array<DelayLine, numLines> lines;
        std::array<Allpass, 8> diffusers;
        std::array<float, numLines> lengths {}, gains {}, damp {}, lfoPhase {}, lfoInc {};
        float sampleRate = 44100.0f, decay = 4.0f, sizeParam = 0.7f, dampHz = 8000.0f, mod = 0.3f;
        float dampCoeff = 0.5f, modDepth = 0.0f;
    };

    //==============================================================================
    /** Band-limited (PolyBLEP) sawtooth oscillator. */
    class SawOsc
    {
    public:
        void setFrequency (float hz, float sampleRate) noexcept { inc = juce::jlimit (0.0f, 0.45f, hz / sampleRate); }
        void setPhase (float p) noexcept { phase = p; }

        float process() noexcept
        {
            auto y = 2.0f * phase - 1.0f;
            y -= blep (phase, inc);
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
            return y;
        }

    private:
        static float blep (float t, float dt) noexcept
        {
            if (dt <= 0.0f) return 0.0f;
            if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
            if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
            return 0.0f;
        }

        float phase = 0.0f, inc = 0.0f;
    };

    /** Fast xorshift white noise in [-1, 1). */
    class Noise
    {
    public:
        explicit Noise (uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}

        float process() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) (int32_t) state * (1.0f / 2147483648.0f);
        }

    private:
        uint32_t state;
    };

    //==============================================================================
    /** Smooth saturation that keeps runaway feedback musical instead of clipping. */
    inline float softClip (float x) noexcept
    {
        if (x > 3.0f)  return 1.0f;
        if (x < -3.0f) return -1.0f;
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2); // Padé approximation of tanh
    }

    /** Equal-power pan gains, unity at centre. pan in [-1, 1]. */
    inline std::pair<float, float> panGains (float pan) noexcept
    {
        const auto angle = (pan + 1.0f) * pi * 0.25f;
        return { std::cos (angle) * juce::MathConstants<float>::sqrt2, std::sin (angle) * juce::MathConstants<float>::sqrt2 };
    }
}
