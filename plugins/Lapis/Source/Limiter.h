#pragma once

// Lookahead true-peak limiter.
//
// Peaks are detected on a 4x oversampled estimate of the waveform (so inter-sample
// peaks count), the required gain is taken as a moving minimum over the lookahead
// window and then box-smoothed over the same window. That combination guarantees
// the gain has fully reached its target by the time the peak leaves the delay line,
// with no overshoot and a smooth (click-free) attack. Release is a one-pole.

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

class TruePeakInterpolator
{
public:
    static constexpr int factor = 4, taps = 12;
    static constexpr int delay = 6;   // approx. group delay in input samples

    TruePeakInterpolator()
    {
        constexpr int total = factor * taps;
        for (int p = 0; p < factor; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                const auto n = k * factor + p;
                const auto t = (n - (total - 1) / 2.0) / factor;
                const auto sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                const auto w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (n + 0.5) / total);
                coeffs[(size_t) p][(size_t) k] = (float) (sinc * w);
                sum += sinc * w;
            }
            for (auto& c : coeffs[(size_t) p]) c = (float) (c / sum);
        }
    }

    void reset() { history.fill (0.0f); }

    /** Pushes a sample; returns the largest absolute value of the 4 interpolated points. */
    float process (float x) noexcept
    {
        std::move_backward (history.begin(), history.end() - 1, history.end());
        history[0] = x;
        float peak = 0.0f;
        for (size_t p = 0; p < (size_t) factor; ++p)
        {
            float y = 0.0f;
            for (size_t k = 0; k < (size_t) taps; ++k) y += history[k] * coeffs[p][k];
            peak = std::max (peak, std::abs (y));
        }
        return peak;
    }

private:
    std::array<std::array<float, taps>, factor> coeffs {};
    std::array<float, taps> history {};
};

class LookaheadLimiter
{
public:
    void prepare (double sampleRate, float lookaheadMs = 1.5f)
    {
        sr = (float) sampleRate;
        window = juce::jmax (2, (int) std::round (lookaheadMs * 0.001f * sr));
        latency = window - 1 + TruePeakInterpolator::delay;

        for (auto& d : delayLines) d.assign ((size_t) latency, 0.0f);   // ring of N = delay of N samples
        delayPos = 0;
        minValues.assign ((size_t) window + 1, 1.0f);
        minIndex.assign ((size_t) window + 1, 0);
        minHead = minTail = 0;
        boxValues.assign ((size_t) window, 1.0f);
        boxSum = (double) window;
        boxPos = 0;
        counter = 0;
        gain = 1.0f;
        for (auto& i : interpolators) i.reset();
        setRelease (80.0f);
    }

    void setCeiling (float linear) noexcept { ceiling = linear; }
    void setRelease (float ms) noexcept { release = 1.0f - std::exp (-1.0f / (juce::jmax (1.0f, ms) * 0.001f * sr)); }
    int getLatency() const noexcept { return latency; }
    float getGain() const noexcept { return gain; }

    void process (float& left, float& right) noexcept
    {
        // 1. Required gain from the true-peak estimate of the incoming audio.
        const auto peak = std::max (interpolators[0].process (left), interpolators[1].process (right));
        const auto required = peak > ceiling ? ceiling / peak : 1.0f;

        // 2. Moving minimum over the lookahead window (monotonic queue).
        const auto cap = (int) minValues.size();
        while (minHead != minTail && minValues[(size_t) ((minTail - 1 + cap) % cap)] >= required)
            minTail = (minTail - 1 + cap) % cap;
        minValues[(size_t) minTail] = required;
        minIndex[(size_t) minTail] = counter;
        minTail = (minTail + 1) % cap;
        while (minIndex[(size_t) minHead] <= counter - window)
            minHead = (minHead + 1) % cap;
        const auto windowMin = minValues[(size_t) minHead];
        ++counter;

        // 3. Box-smooth over the same window: reaches windowMin exactly when the peak is output.
        boxSum += windowMin - boxValues[(size_t) boxPos];
        boxValues[(size_t) boxPos] = windowMin;
        boxPos = (boxPos + 1) % window;
        const auto target = (float) (boxSum / window);

        // 4. Instant down (already smoothed), one-pole release up.
        gain = target < gain ? target : gain + release * (target - gain);

        // 5. Delay the audio so the gain lines up with the peaks it was computed for.
        float* ch[2] = { &left, &right };
        for (size_t c = 0; c < 2; ++c)
        {
            auto& d = delayLines[c];
            const auto delayed = d[(size_t) delayPos];
            d[(size_t) delayPos] = *ch[c];
            *ch[c] = delayed * gain;
        }
        delayPos = (delayPos + 1) % (int) delayLines[0].size();
    }

private:
    std::array<TruePeakInterpolator, 2> interpolators;
    std::array<std::vector<float>, 2> delayLines;
    std::vector<float> minValues, boxValues;
    std::vector<int64_t> minIndex;
    int minHead = 0, minTail = 0, boxPos = 0, delayPos = 0, window = 72, latency = 77;
    int64_t counter = 0;
    double boxSum = 0.0;
    float sr = 48000.0f, ceiling = 0.966f, release = 0.001f, gain = 1.0f;
};
