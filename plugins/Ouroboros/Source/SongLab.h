#pragma once

// The song measured bar by bar, the same way for your mix (Ouroboros on the master, while it
// plays) and for reference tracks (decoded from a file): loudness, the tone in ten octave bands,
// the side (stereo) energy and how many onsets (new events). Then summed per section of the map.

#include "Analysis.h"
#include "SectionBus.h"
#include "SuiteDSP.h"
#include "Listen.h"
#include "Music.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <cmath>
#include <vector>

namespace lab
{
    constexpr int numBands = analysis::numBands;   // 31.5 Hz .. 16 kHz octaves
    using Bands = std::array<float, numBands>;

    struct Bar
    {
        bool heard = false;
        float db = -100.0f, sideDb = -100.0f, onsets = 0.0f, lowFlux = 0.0f;
        float peakDb = -100.0f, leanDb = 0.0f;   // its loudest sample; left over right (dB)
        float asymDb = 0.0f;                     // its positive peak against its negative one (dB: lopsided peaks)
        float lowCorr = 1.0f; int lowCorrCount = 0;   // the correlation of left and right under 120 Hz (1 = in phase), counted when there's low end
        Bands band {};
        std::array<float, 12> chroma {};   // the pitch classes' share of the bar's tonal energy (C = 0), sums to 1 (or all 0)
        float bandDb (int b) const { return band[(size_t) juce::jlimit (0, numBands - 1, b)]; }
    };

    /** Sums for one bar while it plays (power), turned into a Bar at the end. */
    struct Accumulator
    {
        double full = 0.0, side = 0.0, low = 0.0, left = 0.0, right = 0.0, lowLR = 0.0, lowLL = 0.0, lowRR = 0.0;
        float peak = 0.0f, posPeak = 0.0f, negPeak = 0.0f;
        std::array<double, numBands> band {};
        std::array<double, 12> chroma {};
        int samples = 0, onsets = 0;
        Bar finish() const
        {
            Bar b;
            if (samples <= 0) return b;
            auto db = [this] (double p) { return (float) (10.0 * std::log10 (p / samples + 1.0e-12)); };
            b.heard = true;
            b.db = db (full); b.sideDb = db (side);
            for (int i = 0; i < numBands; ++i) b.band[(size_t) i] = db (band[(size_t) i]);
            b.onsets = (float) onsets;
            b.lowFlux = (float) low;
            b.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
            if (posPeak > 1.0e-5f && negPeak > 1.0e-5f) b.asymDb = std::abs (juce::Decibels::gainToDecibels (posPeak / negPeak));
            if (lowLL > 1.0e-6 * samples && lowRR > 1.0e-6 * samples) { b.lowCorr = (float) (lowLR / std::sqrt (lowLL * lowRR)); b.lowCorrCount = 1; }
            b.leanDb = left > 1.0e-12 && right > 1.0e-12 ? (float) (10.0 * std::log10 (left / right)) : 0.0f;
            double c = 0.0;
            for (auto v : chroma) c += v;
            if (c > 1.0e-12) for (int k = 0; k < 12; ++k) b.chroma[(size_t) k] = (float) (chroma[(size_t) k] / c);
            return b;
        }
    };

    /** Per-sample analysis (one instance per thread). */
    class Analyser
    {
    public:
        void prepare (double sr)
        {
            fs = (float) sr;
            for (int b = 0; b < numBands; ++b) filters[(size_t) b].set (analysis::bandCentres()[(size_t) b], 1.41f, fs);
            lowBand.set (100.0f, 0.707f, fs);
            lowLeft.set (120.0f, 0.707f, fs); lowRight.set (120.0f, 0.707f, fs);
            envFast = envSlow = lowEnv = 0.0f;
            fastA = 1.0f - std::exp (-1.0f / (0.005f * fs)); slowA = 1.0f - std::exp (-1.0f / (0.12f * fs));
            lowA = 1.0f - std::exp (-1.0f / (0.002f * fs)); lowR = 1.0f - std::exp (-1.0f / (0.04f * fs));
            holdOff = 0; armed = true;
            // the chroma: 4x decimated (about 11 kHz), 2048-point frames, 65 Hz .. 2 kHz
            decimFactor = juce::jmax (1, (int) std::lround (sr / 11025.0));
            antiAlias.set (juce::jmin (2500.0f, 0.4f * fs / (float) decimFactor), 0.707f, fs);
            frameFill = 0; decimCount = 0; decimSum = 0.0f;
            const auto rate = (double) fs / decimFactor;
            for (int k = 0; k < fftSize / 2; ++k)
            {
                const auto hz = (double) k * rate / fftSize;
                binClass[(size_t) k] = hz >= 65.0 && hz <= 2000.0 ? ((int) std::lround (12.0 * std::log2 (hz / 440.0)) + 9 + 1200) % 12 : -1;
            }
            for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
        }
        static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
        /** A small in-place radix-2 FFT (real input in re, im zeroed). */
        static void fft (std::array<float, fftSize>& re, std::array<float, fftSize>& im) noexcept
        {
            for (int i = 1, j = 0; i < fftSize; ++i)
            {
                int bit = fftSize >> 1;
                for (; (j & bit) != 0; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
            }
            for (int len = 2; len <= fftSize; len <<= 1)
            {
                const auto ang = -2.0 * juce::MathConstants<double>::pi / len;
                const auto wr0 = (float) std::cos (ang), wi0 = (float) std::sin (ang);
                for (int i = 0; i < fftSize; i += len)
                {
                    float wr = 1.0f, wi = 0.0f;
                    for (int k = 0; k < len / 2; ++k)
                    {
                        const auto a = (size_t) (i + k), b = (size_t) (i + k + len / 2);
                        const auto xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                        re[b] = re[a] - xr; im[b] = im[a] - xi;
                        re[a] += xr; im[a] += xi;
                        const auto nwr = wr * wr0 - wi * wi0;
                        wi = wr * wi0 + wi * wr0; wr = nwr;
                    }
                }
            }
        }
        void process (float l, float r, Accumulator& acc) noexcept
        {
            const auto mono = 0.5f * (l + r), side = 0.5f * (l - r);
            ++acc.samples;
            acc.left += (double) l * l; acc.right += (double) r * r;
            { const auto a = lowLeft.lowpass (l), b = lowRight.lowpass (r); acc.lowLR += (double) a * b; acc.lowLL += (double) a * a; acc.lowRR += (double) b * b; }
            acc.peak = std::max (acc.peak, std::max (std::abs (l), std::abs (r)));
            acc.posPeak = std::max (acc.posPeak, 0.5f * (l + r)); acc.negPeak = std::max (acc.negPeak, -0.5f * (l + r));
            acc.full += (double) mono * mono;
            acc.side += (double) side * side;
            for (int b = 0; b < numBands; ++b) { const auto y = filters[(size_t) b].bandpass (mono); acc.band[(size_t) b] += (double) y * y; }
            // onsets: the fast envelope jumping over the slow one
            const auto a = std::abs (mono);
            envFast += fastA * (a - envFast);
            envSlow += slowA * (a - envSlow);
            // counted once per hit: it has to fall back near the slow envelope before the next one
            if (holdOff > 0) --holdOff;
            else if (armed && envFast > 1.8f * envSlow + 1.0e-4f) { ++acc.onsets; holdOff = (int) (0.05f * fs); armed = false; }
            else if (! armed && envFast < 1.1f * envSlow) armed = true;
            const auto lo = std::abs (lowBand.lowpass (mono)), before = lowEnv;
            lowEnv += (lo > lowEnv ? lowA : lowR) * (lo - lowEnv);
            acc.low += std::max (0.0f, lowEnv - before);
            // the chroma
            decimSum += antiAlias.lowpass (mono);
            if (++decimCount >= decimFactor)
            {
                frame[(size_t) frameFill++] = decimSum / (float) decimFactor;
                decimSum = 0.0f; decimCount = 0;
                if (frameFill >= fftSize)
                {
                    frameFill = 0;
                    for (int i = 0; i < fftSize; ++i) { re[(size_t) i] = frame[(size_t) i] * window[(size_t) i]; im[(size_t) i] = 0.0f; }
                    fft (re, im);
                    for (int k = 1; k < fftSize / 2; ++k)
                        if (const auto c = binClass[(size_t) k]; c >= 0)
                            acc.chroma[(size_t) c] += (double) (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
                }
            }
        }
    private:
        float fs = 44100.0f, envFast = 0.0f, envSlow = 0.0f, fastA = 0.1f, slowA = 0.01f, lowEnv = 0.0f, lowA = 0.1f, lowR = 0.01f;
        int holdOff = 0;
        bool armed = true;
        std::array<suite::Svf, numBands> filters;
        suite::Svf lowBand, antiAlias, lowLeft, lowRight;
        int decimFactor = 4, decimCount = 0, frameFill = 0;
        float decimSum = 0.0f;
        std::array<float, fftSize> frame {}, re {}, im {}, window {};
        std::array<int, fftSize / 2> binClass {};
    };

    /** A whole file, bar by bar at `bpm` (up to 15 minutes). */
    inline std::vector<Bar> analyseFile (const juce::File& file, double bpm, double& seconds)
    {
        std::vector<Bar> out;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        seconds = 0.0;
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0) return out;
        const auto length = std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 15.0 * 60.0));
        seconds = (double) length / reader->sampleRate;
        const auto barSamples = reader->sampleRate * 4.0 * 60.0 / juce::jlimit (60.0, 200.0, bpm);
        Analyser an;
        an.prepare (reader->sampleRate);
        Accumulator acc;
        juce::AudioBuffer<float> buf (2, 32768);
        int barIndex = 0;
        for (juce::int64 pos = 0; pos < length; pos += buf.getNumSamples())
        {
            const auto n = (int) std::min<juce::int64> (buf.getNumSamples(), length - pos);
            reader->read (&buf, 0, n, pos, true, true);
            const auto* L = buf.getReadPointer (0);
            const auto* R = buf.getReadPointer (reader->numChannels > 1 ? 1 : 0);
            for (int i = 0; i < n; ++i)
            {
                const auto bar = (int) std::floor ((double) (pos + i) / barSamples);
                if (bar != barIndex) { out.push_back (acc.finish()); acc = {}; barIndex = bar; }
                an.process (L[i], R[i], acc);
            }
        }
        out.push_back (acc.finish());
        return out;
    }

    /** The Listen features (for proposing a reference's map). */
    inline std::vector<listen::BarFeature> features (const std::vector<Bar>& bars)
    {
        std::vector<listen::BarFeature> f;
        for (const auto& b : bars)
        {
            listen::BarFeature x;
            x.heard = b.heard; x.fullDb = b.db; x.kickFlux = b.lowFlux;
            double hi = 0.0;
            for (int k = 7; k < numBands; ++k) hi += std::pow (10.0, b.band[(size_t) k] / 10.0);
            x.highDb = (float) (10.0 * std::log10 (hi + 1.0e-12));
            f.push_back (x);
        }
        return f;
    }

    /** One section of a song, summed. */
    struct SectionStat
    {
        suite::SectionType type = suite::SectionType::Groove;
        int index = -1, startBar = 1, bars = 0, heardBars = 0;
        float db = -100.0f, sideDb = -100.0f, onsetsPerBar = 0.0f;
        float firstDb = -100.0f, lastDb = -100.0f;   // its first and last two bars
        Bands band {};
        bool heard() const { return heardBars >= juce::jmax (1, bars / 2); }
        float width() const { return sideDb - db; }   // the side under the middle (dB): higher = wider
    };

    inline float powerMeanDb (const std::vector<float>& dbs)
    {
        if (dbs.empty()) return -100.0f;
        double p = 0.0;
        for (auto d : dbs) p += std::pow (10.0, d / 10.0);
        return (float) (10.0 * std::log10 (p / (double) dbs.size() + 1.0e-12));
    }

    inline std::vector<SectionStat> sections (const std::vector<Bar>& bars, const suite::SectionMap& map)
    {
        std::vector<SectionStat> out;
        for (int i = 0; i < map.count; ++i)
        {
            SectionStat s;
            s.index = i; s.type = map.sections[(size_t) i].type;
            s.startBar = map.sections[(size_t) i].startBar;
            const auto from = s.startBar - 1, to = map.endBar (i) - 1;
            s.bars = juce::jmax (0, to - from);
            std::vector<float> db, side, first, last;
            std::array<std::vector<float>, numBands> band;
            float onsets = 0.0f;
            for (int b = from; b < to; ++b)
            {
                if (b < 0 || b >= (int) bars.size() || ! bars[(size_t) b].heard) continue;
                const auto& x = bars[(size_t) b];
                ++s.heardBars;
                db.push_back (x.db); side.push_back (x.sideDb); onsets += x.onsets;
                for (int k = 0; k < numBands; ++k) band[(size_t) k].push_back (x.band[(size_t) k]);
                if (b < from + 2) first.push_back (x.db);
                if (b >= to - 2) last.push_back (x.db);
            }
            s.db = powerMeanDb (db); s.sideDb = powerMeanDb (side);
            s.firstDb = powerMeanDb (first); s.lastDb = powerMeanDb (last);
            for (int k = 0; k < numBands; ++k) s.band[(size_t) k] = powerMeanDb (band[(size_t) k]);
            s.onsetsPerBar = s.heardBars > 0 ? onsets / (float) s.heardBars : 0.0f;
            out.push_back (s);
        }
        return out;
    }

    /** A tone shape: each band relative to the band average (so level doesn't matter). */
    inline Bands shape (const Bands& b)
    {
        float mean = 0.0f;
        int n = 0;
        for (int k = 1; k < numBands - 1; ++k) { mean += b[(size_t) k]; ++n; }
        mean /= (float) juce::jmax (1, n);
        Bands out {};
        for (int k = 0; k < numBands; ++k) out[(size_t) k] = b[(size_t) k] - mean;
        return out;
    }
    inline float distance (const Bands& a, const Bands& b)   // rms difference of two shapes (dB)
    {
        const auto sa = shape (a), sb = shape (b);
        double d = 0.0;
        for (int k = 1; k < numBands - 1; ++k) d += (double) (sa[(size_t) k] - sb[(size_t) k]) * (sa[(size_t) k] - sb[(size_t) k]);
        return (float) std::sqrt (d / (numBands - 2));
    }

    //==============================================================================
    // Chords from the chroma: each bar's root (the best major or minor triad), a section's
    // progression, and a key-independent print of it (the steps between the roots).
    inline int barRoot (const Bar& b, float* strength = nullptr)
    {
        float total = 0.0f;
        for (auto v : b.chroma) total += v;
        if (total < 0.5f) return -1;
        int best = -1; float bestScore = 0.0f;
        for (int r = 0; r < 12; ++r)
            for (int third : { 3, 4 })
            {
                const auto sc = b.chroma[(size_t) r] + 0.7f * b.chroma[(size_t) ((r + third) % 12)] + 0.8f * b.chroma[(size_t) ((r + 7) % 12)];
                if (sc > bestScore) { bestScore = sc; best = r; }
            }
        if (strength != nullptr) *strength = bestScore;
        return bestScore > 0.35f ? best : -1;
    }
    /** The chord roots of bars [from, to), repeats merged (up to 8). */
    inline std::vector<int> progression (const std::vector<Bar>& bars, int from, int to)
    {
        std::vector<int> roots;
        for (int b = juce::jmax (0, from); b < juce::jmin ((int) bars.size(), to) && roots.size() < 8; ++b)
        {
            const auto r = barRoot (bars[(size_t) b]);
            if (r >= 0 && (roots.empty() || roots.back() != r)) roots.push_back (r);
        }
        // a loop that comes back to its first chord: drop the repeat
        if (roots.size() > 2 && roots.back() == roots.front()) roots.pop_back();
        return roots;
    }
    /** The steps between successive roots, around the loop ("8,7,7,2" for i-VI-III-VII). */
    inline juce::String progressionPrint (const std::vector<int>& roots)
    {
        if (roots.size() < 2) return {};
        juce::StringArray steps;
        for (size_t i = 0; i < roots.size(); ++i) steps.add (juce::String (((roots[(i + 1) % roots.size()] - roots[i]) % 12 + 12) % 12));
        return steps.joinIntoString (",");
    }
    /** How alike two progression prints are (0..1), allowing the loop to start anywhere. */
    inline float progressionSimilarity (const juce::String& a, const juce::String& b)
    {
        const auto x = juce::StringArray::fromTokens (a, ",", ""), y = juce::StringArray::fromTokens (b, ",", "");
        if (x.size() < 2 || y.size() < 2) return 0.0f;
        float best = 0.0f;
        const auto n = juce::jmax (x.size(), y.size());
        for (int rot = 0; rot < y.size(); ++rot)
        {
            int same = 0;
            for (int i = 0; i < juce::jmin (x.size(), y.size()); ++i) same += x[i] == y[(i + rot) % y.size()] ? 1 : 0;
            best = juce::jmax (best, (float) same / (float) n);
        }
        return best;
    }
}
