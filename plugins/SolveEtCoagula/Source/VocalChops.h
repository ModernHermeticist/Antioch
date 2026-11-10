#pragma once

// Vocal chops: Solve et Coagula already hears the vocal (on its sidechain). Chops keeps the
// last bar of vocal it heard outside the chop sections, cuts it into sixteen 1/16 slices
// and plays them back in a rhythm pattern in the sections you choose, on top of the music
// bus. Bar-aligned and allocation-free on the audio thread (two bar buffers swap).

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

class VocalChops
{
public:
    static constexpr int steps = 16;
    enum Pattern { Stutter, Offbeat, ThreeThreeTwo, Shuffle, numPatterns };

    /** Slice played on each 1/16 step (-1 = rest). */
    static const std::array<int, steps>& pattern (int p)
    {
        static const std::array<std::array<int, steps>, numPatterns> table {{
            { 0, 0, 0, 0,  4, 4, 4, 4,  8, 8, 8, 8,  12, 12, 12, 12 },        // stutter: each beat's first slice, repeated
            { -1, -1, 2, -1,  -1, -1, 6, -1,  -1, -1, 10, -1,  -1, -1, 14, -1 }, // off-beat 8ths
            { 0, -1, -1, 0,  -1, -1, 0, -1,  8, -1, -1, 8,  -1, -1, 8, -1 },     // 3-3-2
            { 0, 1, 0, 1,  4, 5, 4, 5,  8, 9, 8, 9,  12, 14, 12, 15 },           // shuffled
        }};
        return table[(size_t) juce::jlimit (0, numPatterns - 1, p)];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const auto maxBar = (size_t) (4.5 * sampleRate);   // one bar at 53 BPM and up
        for (auto& b : bars) b.assign (maxBar, 0.0f);
        writing = 0; captured = -1; capturedLength = 0;
        lastPosInBar = -1.0; barPower = 0.0; barCount = 0;
    }

    /** Feed one sample of vocal at its place in the bar (outside chop sections). */
    void listen (float vocal, double posInBar, double barQ, double qPerSample)
    {
        if (posInBar < lastPosInBar) finishBar (barQ, qPerSample);   // a new bar began
        lastPosInBar = posInBar;
        auto& b = bars[(size_t) writing];
        const auto i = (size_t) (posInBar / qPerSample);
        if (i < b.size()) b[i] = vocal;
        barPower += (double) vocal * vocal;
        ++barCount;
    }

    /** Ends the bar being listened to (e.g. on entering a chop section). */
    void finishBar (double barQ, double qPerSample)
    {
        const auto rms = barCount > 0 ? std::sqrt (barPower / barCount) : 0.0;
        if (rms > 0.01 && barCount > (int) (0.75 * barQ / qPerSample))   // a (nearly) whole bar with vocal in it (about -40 dBFS)
        {
            captured = writing;
            capturedLength = juce::jmin ((int) bars[(size_t) writing].size(), (int) (barQ / qPerSample));
            writing = 1 - writing;
        }
        std::fill (bars[(size_t) writing].begin(), bars[(size_t) writing].end(), 0.0f);
        barPower = 0.0; barCount = 0; lastPosInBar = -1.0;
    }

    bool ready() const noexcept { return captured >= 0; }

    /** One output sample of chops at `posInBar`. */
    float play (double posInBar, double barQ, double qPerSample, int patternIndex, bool octaveUp) const noexcept
    {
        if (captured < 0) return 0.0f;
        const auto stepQ = barQ / steps;
        const auto step = juce::jlimit (0, steps - 1, (int) (posInBar / stepQ));
        const auto slice = pattern (patternIndex)[(size_t) step];
        if (slice < 0) return 0.0f;
        const auto sliceLen = juce::jmax (1, capturedLength / steps);
        const auto into = (posInBar - step * stepQ) / qPerSample;          // samples into the step
        const auto stepLen = stepQ / qPerSample;
        auto read = into * (octaveUp ? 2.0 : 1.0);
        read = std::fmod (read, (double) sliceLen);
        const auto idx = juce::jlimit (0, capturedLength - 1, slice * sliceLen + (int) read);
        // 2 ms fades, and a gate at 85% of the step for a crisp chop.
        const auto fade = 0.002 * sr;
        const auto gateEnd = 0.85 * stepLen;
        const auto env = into > gateEnd ? 0.0 : juce::jmin (1.0, into / fade, (gateEnd - into) / fade);
        return bars[(size_t) captured][(size_t) idx] * (float) env;
    }

private:
    std::array<std::vector<float>, 2> bars;
    int writing = 0, captured = -1, capturedLength = 0;
    double sr = 48000.0, lastPosInBar = -1.0, barPower = 0.0;
    int barCount = 0;
};
