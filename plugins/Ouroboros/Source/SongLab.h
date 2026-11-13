#pragma once

// The song measured bar by bar, the same way for your mix (Ouroboros on the master, while it
// plays) and for reference tracks (decoded from a file): loudness, the tone in ten octave bands,
// the side (stereo) energy and how many onsets (new events). Then summed per section of the map.

#include "Analysis.h"
#include "SectionBus.h"
#include "SuiteDSP.h"
#include "Listen.h"
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
        Bands band {};
        float bandDb (int b) const { return band[(size_t) juce::jlimit (0, numBands - 1, b)]; }
    };

    /** Sums for one bar while it plays (power), turned into a Bar at the end. */
    struct Accumulator
    {
        double full = 0.0, side = 0.0, low = 0.0;
        std::array<double, numBands> band {};
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
            envFast = envSlow = lowEnv = 0.0f;
            fastA = 1.0f - std::exp (-1.0f / (0.005f * fs)); slowA = 1.0f - std::exp (-1.0f / (0.12f * fs));
            lowA = 1.0f - std::exp (-1.0f / (0.002f * fs)); lowR = 1.0f - std::exp (-1.0f / (0.04f * fs));
            holdOff = 0; armed = true;
        }
        void process (float l, float r, Accumulator& acc) noexcept
        {
            const auto mono = 0.5f * (l + r), side = 0.5f * (l - r);
            ++acc.samples;
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
        }
    private:
        float fs = 44100.0f, envFast = 0.0f, envSlow = 0.0f, fastA = 0.1f, slowA = 0.01f, lowEnv = 0.0f, lowA = 0.1f, lowR = 0.01f;
        int holdOff = 0;
        bool armed = true;
        std::array<suite::Svf, numBands> filters;
        suite::Svf lowBand;
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
}
