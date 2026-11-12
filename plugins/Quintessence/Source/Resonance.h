#pragma once

// Resonance finder: narrow ringing peaks (a filter's resonance, a supersaw's comb, a room mode
// in a sample) that stand far above the spectrum around them. Averages 4096-point spectra over
// a pass; a peak counts when it's a local maximum standing 10 dB or more above the median of
// the third of an octave either side of it (leaving out its own twelfth of an octave).
// Message thread only.

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace qn
{
    class ResonanceFinder
    {
    public:
        static constexpr int order = 12, size = 1 << order, hop = size / 2;
        struct Peak { float hz = 0.0f, prominenceDb = 0.0f; };

        ResonanceFinder() : window ((size_t) size), frame ((size_t) size * 2), power ((size_t) size / 2 + 1, 0.0)
        {
            for (int i = 0; i < size; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
        }

        void reset() { std::fill (power.begin(), power.end(), 0.0); frames = 0; pending.clear(); }

        /** Feeds mono samples (any amount); analyses each full frame. */
        void add (const float* x, int n)
        {
            pending.insert (pending.end(), x, x + n);
            size_t at = 0;
            while (pending.size() - at >= (size_t) size)
            {
                for (int i = 0; i < size; ++i) frame[(size_t) i] = pending[at + (size_t) i] * window[(size_t) i];
                std::fill (frame.begin() + size, frame.end(), 0.0f);
                fft.performFrequencyOnlyForwardTransform (frame.data());
                for (size_t b = 0; b < power.size(); ++b) power[b] += (double) frame[b] * frame[b];
                ++frames;
                at += hop;
            }
            pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) at);
        }

        int framesHeard() const { return frames; }

        /** How far the spectrum at `hz` stands above the third of an octave around it (dB). */
        float prominenceAt (float hz, double sampleRate) const
        {
            const auto db = spectrumDb();
            if (db.empty()) return 0.0f;
            const auto bin = (int) std::lround (hz / sampleRate * size);
            int best = bin;
            for (int b = binFor (hz / std::pow (2.0f, 1.0f / 48.0f), sampleRate); b <= binFor (hz * std::pow (2.0f, 1.0f / 48.0f), sampleRate); ++b)
                if (b > 0 && b < (int) db.size() && db[(size_t) b] > db[(size_t) best]) best = b;
            return prominence (db, best, sampleRate);
        }

        /** The most prominent peaks between 200 Hz and 8 kHz, strongest first. */
        std::vector<Peak> peaks (double sampleRate, float minProminenceDb = 10.0f, int maxPeaks = 3, float maxProminenceDb = 35.0f, float loHz = 200.0f) const
        {
            std::vector<Peak> out;
            const auto db = spectrumDb();
            if (db.empty()) return out;
            const auto lo = binFor (loHz, sampleRate), hi = std::min ((int) db.size() - 2, binFor (8000.0f, sampleRate));
            for (int b = lo; b <= hi; ++b)
            {
                // A local maximum within a 24th of an octave
                const auto from = binFor ((float) (b * sampleRate / size) / std::pow (2.0f, 1.0f / 24.0f), sampleRate);
                const auto to = binFor ((float) (b * sampleRate / size) * std::pow (2.0f, 1.0f / 24.0f), sampleRate);
                bool isMax = true;
                for (int k = std::max (1, from); k <= std::min ((int) db.size() - 1, to) && isMax; ++k)
                    if (k != b && db[(size_t) k] > db[(size_t) b]) isMax = false;
                if (! isMax) continue;
                // 10-35 dB proud of the sound around it: a resonance. Far more than that is a note
                // standing on its own (nothing around it), not a resonance on a sound.
                const auto p = prominence (db, b, sampleRate);
                if (p >= minProminenceDb && p <= maxProminenceDb)
                {
                    // Between bins: the top of a parabola through the peak and its neighbours
                    const auto a = db[(size_t) b - 1], c = db[(size_t) b], e = db[(size_t) b + 1];
                    const auto denom = a - 2.0f * c + e;
                    const auto offset = std::abs (denom) > 1.0e-6f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - e) / denom) : 0.0f;
                    out.push_back ({ (float) ((b + offset) * sampleRate / size), p });
                }
            }
            std::sort (out.begin(), out.end(), [] (const Peak& a, const Peak& b) { return a.prominenceDb > b.prominenceDb; });
            if ((int) out.size() > maxPeaks) out.resize ((size_t) maxPeaks);
            return out;
        }

    private:
        static int binFor (float hz, double sampleRate) { return std::max (1, (int) std::lround (hz / sampleRate * size)); }

        std::vector<float> spectrumDb() const
        {
            std::vector<float> db;
            if (frames == 0) return db;
            db.resize (power.size());
            for (size_t b = 0; b < power.size(); ++b) db[b] = (float) (10.0 * std::log10 (power[b] / frames + 1.0e-20));
            return db;
        }

        static float prominence (const std::vector<float>& db, int b, double sampleRate)
        {
            const auto hz = (float) (b * sampleRate / size);
            const auto outerLo = binFor (hz / std::pow (2.0f, 1.0f / 3.0f), sampleRate), outerHi = binFor (hz * std::pow (2.0f, 1.0f / 3.0f), sampleRate);
            const auto innerLo = binFor (hz / std::pow (2.0f, 1.0f / 12.0f), sampleRate), innerHi = binFor (hz * std::pow (2.0f, 1.0f / 12.0f), sampleRate);
            std::vector<float> around;
            for (int k = outerLo; k <= std::min ((int) db.size() - 1, outerHi); ++k)
                if (k < innerLo || k > innerHi) around.push_back (db[(size_t) k]);
            if (around.size() < 4) return 0.0f;
            std::nth_element (around.begin(), around.begin() + (std::ptrdiff_t) (around.size() / 2), around.end());
            return db[(size_t) b] - around[around.size() / 2];
        }

        juce::dsp::FFT fft { order };
        std::vector<float> window, frame;
        std::vector<double> power;
        std::vector<float> pending;
        int frames = 0;
    };
}
