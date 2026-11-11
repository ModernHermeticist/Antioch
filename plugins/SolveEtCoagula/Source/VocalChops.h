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
    enum Pattern { Stutter, Offbeat, ThreeThreeTwo, Shuffle, Words, numPatterns };   // Words: the bar's sung words, one per 8th, in order

    /** Slice played on each 1/16 step (-1 = rest). */
    static const std::array<int, steps>& pattern (int p)
    {
        static const std::array<std::array<int, steps>, numPatterns> table {{
            { 0, 0, 0, 0,  4, 4, 4, 4,  8, 8, 8, 8,  12, 12, 12, 12 },        // stutter: each beat's first slice, repeated
            { -1, -1, 2, -1,  -1, -1, 6, -1,  -1, -1, 10, -1,  -1, -1, 14, -1 }, // off-beat 8ths
            { 0, -1, -1, 0,  -1, -1, 0, -1,  8, -1, -1, 8,  -1, -1, 8, -1 },     // 3-3-2
            { 0, 1, 0, 1,  4, 5, 4, 5,  8, 9, 8, 9,  12, 14, 12, 15 },           // shuffled
            { 0, 0, 0, 0,  4, 4, 4, 4,  8, 8, 8, 8,  12, 12, 12, 12 },        // (words: played by onset; this is the fallback)
        }};
        return table[(size_t) juce::jlimit (0, numPatterns - 1, p)];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const auto maxBar = (size_t) (4.5 * sampleRate);   // one bar at 53 BPM and up
        for (auto& b : bars) b.assign (maxBar, 0.0f);
        writing = 0; captured = -1; capturedLength = 0;
        onsetCount = { 0, 0 }; fast = slow = 0.0f; lastOnset = -100000;
        fastCoeff = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
        slowCoeff = 1.0f - std::exp (-1.0f / (0.08f * (float) sampleRate));
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
        // Word onsets: the vocal jumping well over its recent level, at least 80 ms after the last
        const auto a = std::abs (vocal);
        fast += fastCoeff * (a - fast);
        slow += slowCoeff * (a - slow);
        auto& count = onsetCount[(size_t) writing];
        if (fast > 2.0f * slow + 0.005f && fast > 0.02f && (int) i - lastOnset > (int) (0.08 * sr) && count < maxOnsets && i < b.size())
        {
            onsets[(size_t) writing][(size_t) count++] = juce::jmax (0, (int) i - (int) (0.005 * sr));   // just before the rise
            lastOnset = (int) i;
        }
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
        onsetCount[(size_t) writing] = 0;
        lastOnset = -100000;
        barPower = 0.0; barCount = 0; lastPosInBar = -1.0;
    }

    bool ready() const noexcept { return captured >= 0; }
    int wordsHeard() const noexcept { return captured >= 0 ? onsetCount[(size_t) captured] : 0; }

    /** A position in the bar as if the swung 16ths were straight (off-16ths `swing` of a 16th late). */
    static double unswing (double posInBar, double barQ, double swing) noexcept
    {
        if (swing <= 0.0) return posInBar;
        const auto six = barQ / steps, pairLen = 2.0 * six;
        const auto pair = std::floor (posInBar / pairLen) * pairLen, bp = posInBar - pair, off = six * (1.0 + swing);
        return pair + (bp < off ? bp * six / off : six + (bp - off) * six / (pairLen - off));
    }

    /** One output sample of chops at `posInBar`; `swing` (0..0.6) follows the suite's groove. */
    float play (double posInBar, double barQ, double qPerSample, int patternIndex, bool octaveUp, double swing = 0.0) const noexcept
    {
        if (captured < 0) return 0.0f;
        const auto fadeLen = 0.002 * sr;
        const auto count = onsetCount[(size_t) captured];
        if (patternIndex == Words && count > 0)
        {
            // One sung word per 8th, in the order they were sung
            const auto eighthQ = barQ / 8.0;
            const auto step8 = juce::jlimit (0, 7, (int) (posInBar / eighthQ));
            const auto start = onsets[(size_t) captured][(size_t) (step8 % count)];
            const auto into = (posInBar - step8 * eighthQ) / qPerSample;
            const auto gateEnd = 0.85 * eighthQ / qPerSample;
            const auto env = into > gateEnd ? 0.0 : juce::jmin (1.0, into / fadeLen, (gateEnd - into) / fadeLen);
            const auto idx = juce::jlimit (0, capturedLength - 1, start + (int) (into * (octaveUp ? 2.0 : 1.0)));
            return bars[(size_t) captured][(size_t) idx] * (float) env;
        }
        if (patternIndex == Words) patternIndex = Stutter;   // no words found: the plain chop
        posInBar = unswing (posInBar, barQ, swing);
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
    static constexpr int maxOnsets = 16;
    std::array<std::array<int, maxOnsets>, 2> onsets {};
    std::array<int, 2> onsetCount { 0, 0 };
    float fast = 0.0f, slow = 0.0f, fastCoeff = 0.01f, slowCoeff = 0.001f;
    int lastOnset = -100000;
    std::array<std::vector<float>, 2> bars;
    int writing = 0, captured = -1, capturedLength = 0;
    double sr = 48000.0, lastPosInBar = -1.0, barPower = 0.0;
    int barCount = 0;
};
