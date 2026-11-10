#pragma once

// Key detection: a chromagram (energy per pitch class) from an FFT of the mix,
// accumulated over time, correlated with the Krumhansl-Kessler key profiles for
// all 24 major and minor keys. Allocation-free after prepare(); the FFT runs on
// the audio thread every 4096 samples (~85 ms at 48 kHz).

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

class KeyDetector
{
public:
    static constexpr int order = 13, size = 1 << order, hop = size / 2;

    struct Candidate { int root = 0; bool minor = true; float score = -1.0f; };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        input.assign ((size_t) size, 0.0f);
        fftData.assign ((size_t) size * 2, 0.0f);
        window.assign ((size_t) size, 0.0f);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) size);

        // Map candidate fundamentals 80 Hz .. 1.2 kHz to pitch classes (bins between semitones are skipped).
        binClass.assign ((size_t) size / 2, -1);
        for (int b = 1; b < size / 2; ++b)
        {
            const auto hz = b * sr / size;
            if (hz < 80.0 || hz > 1200.0) continue;
            const auto midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const auto nearest = std::round (midi);
            if (std::abs (midi - nearest) < 0.35)
                binClass[(size_t) b] = ((int) nearest % 12 + 12) % 12;
        }
        reset();
    }

    void reset()
    {
        filled = 0;
        longChroma.fill (0.0);
        for (auto& c : chroma) c.store (0.0f);
        seconds.store (0.0f);
        best.store (-1);
        confidence.store (0.0f);
    }

    /** Audio thread: feed mono samples. */
    void process (const float* samples, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            input[(size_t) filled++] = samples[i];
            if (filled == size)
            {
                analyseFrame();
                std::copy (input.begin() + hop, input.end(), input.begin());   // 50% overlap
                filled = size - hop;
            }
        }
    }

    /** Ranked candidates from the accumulated chroma (any thread). */
    std::array<Candidate, 24> ranked() const
    {
        static const float majorProfile[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static const float minorProfile[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

        std::array<float, 12> c {};
        for (int i = 0; i < 12; ++i) c[(size_t) i] = chroma[(size_t) i].load();

        std::array<Candidate, 24> out {};
        for (int k = 0; k < 24; ++k)
        {
            const auto minor = k >= 12;
            const auto root = k % 12;
            const auto* profile = minor ? minorProfile : majorProfile;
            std::array<float, 12> rotated {};
            for (int i = 0; i < 12; ++i) rotated[(size_t) ((i + root) % 12)] = profile[i];
            out[(size_t) k] = { root, minor, pearson (c, rotated) };
        }
        std::sort (out.begin(), out.end(), [] (const Candidate& a, const Candidate& b) { return a.score > b.score; });
        return out;
    }

    std::array<std::atomic<float>, 12> chroma {};   // normalised long-term chroma (for display)
    std::atomic<float> seconds { 0.0f };            // seconds of audible music analysed
    std::atomic<int> best { -1 };                   // root + (minor ? 12 : 0), or -1
    std::atomic<float> confidence { 0.0f };         // 0..1

private:
    static float pearson (const std::array<float, 12>& a, const std::array<float, 12>& b)
    {
        float ma = 0.0f, mb = 0.0f;
        for (int i = 0; i < 12; ++i) { ma += a[(size_t) i]; mb += b[(size_t) i]; }
        ma /= 12.0f; mb /= 12.0f;
        float num = 0.0f, da = 0.0f, db = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            const auto x = a[(size_t) i] - ma, y = b[(size_t) i] - mb;
            num += x * y; da += x * x; db += y * y;
        }
        return da > 0.0f && db > 0.0f ? num / std::sqrt (da * db) : -1.0f;
    }

    void analyseFrame() noexcept
    {
        float energy = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            fftData[(size_t) i] = input[(size_t) i] * window[(size_t) i];
            energy += input[(size_t) i] * input[(size_t) i];
        }
        std::fill (fftData.begin() + size, fftData.end(), 0.0f);
        if (energy / size < 1.0e-7f)       // silence: don't learn from it
            return;

        fft.performFrequencyOnlyForwardTransform (fftData.data());

        // Pitch salience: a candidate fundamental scores the (compressed) energy of its first
        // five harmonics. Overtones of real notes (e.g. the fifth above, from the 3rd harmonic)
        // then score far less than the notes themselves, which keeps C major from looking like G.
        std::array<double, 12> frame {};
        double total = 0.0;
        for (int b = 1; b < size / 2; ++b)
            if (const auto pc = binClass[(size_t) b]; pc >= 0)
            {
                double salience = 0.0, weight = 1.0;
                for (int h = 1; h <= 5 && b * h < size / 2; ++h, weight *= 0.8)
                    salience += weight * std::log1p ((double) fftData[(size_t) (b * h)] * 10.0);
                frame[(size_t) pc] += salience;
                total += salience;
            }
        if (total <= 0.0)
            return;

        double maxValue = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            longChroma[(size_t) i] += frame[(size_t) i] / total;
            maxValue = std::max (maxValue, longChroma[(size_t) i]);
        }
        for (int i = 0; i < 12; ++i)
            chroma[(size_t) i].store ((float) (longChroma[(size_t) i] / maxValue));
        seconds.store (seconds.load() + (float) (hop / sr));

        const auto r = ranked();
        best.store (r[0].root + (r[0].minor ? 12 : 0));
        // Confidence: a strong fit that clearly beats the runner-up.
        confidence.store (juce::jlimit (0.0f, 1.0f, (r[0].score - r[1].score) / 0.08f) * juce::jlimit (0.0f, 1.0f, (r[0].score - 0.4f) / 0.4f));
    }

    juce::dsp::FFT fft { order };
    std::vector<float> input, fftData, window;
    std::vector<int> binClass;
    std::array<double, 12> longChroma {};
    int filled = 0;
    double sr = 48000.0;
};
