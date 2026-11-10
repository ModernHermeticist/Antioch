#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace dsp
{
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

        int capacity() const noexcept { return size - 4; }

    private:
        float at (int index) const noexcept { return buffer[(size_t) (index & mask)]; }

        std::vector<float> buffer;
        int size = 0, mask = 0, writePos = 0;
    };

    //==============================================================================
    /** Two-head crossfading delay-line pitch shifter (granular, ~50 ms grains).
        Crossfades to a clean bypass when the interval is unison so unshifted
        taps keep their exact timing and don't pick up comb colouration. */
    class PitchShifter
    {
    public:
        void prepare (double sampleRate)
        {
            window = (float) (sampleRate * 0.05);
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
            const auto g1 = std::sin (juce::MathConstants<float>::pi * phase);
            const auto g2 = std::sin (juce::MathConstants<float>::pi * phase2);

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
    /** One-pole TPT filter giving both lowpass and highpass outputs. */
    class OnePole
    {
    public:
        void setCutoff (float hz, float sampleRate) noexcept
        {
            const auto g = std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, sampleRate * 0.49f) / sampleRate);
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

    /** Smooth saturation that keeps runaway feedback musical instead of clipping. */
    inline float softClip (float x) noexcept
    {
        if (x > 3.0f)  return 1.0f;
        if (x < -3.0f) return -1.0f;
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2); // Padé approximation of tanh
    }
}
