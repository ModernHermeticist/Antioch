#pragma once

// Measurement core for Mentalism. Used both on live audio (audio thread writes,
// UI reads the atomics) and offline on a reference track (single thread).
//
//  - LoudnessMeter: ITU-R BS.1770-4 / EBU R128 momentary, short-term and gated
//    integrated loudness (K-weighting, 400 ms blocks with 75% overlap).
//  - TruePeakMeter: 4x oversampled peak (BS.1770 annex 2 style interpolation).
//  - StereoMeter: correlation (full band and below 120 Hz) and mono-sum loss.
//  - BandAnalyzer: octave-band balance (31.5 Hz .. 16 kHz), short-term and long-term.

#include "SuiteDSP.h"
#include <atomic>
#include <array>
#include <cmath>

namespace analysis
{
    constexpr int numBands = 10;
    inline const std::array<float, numBands>& bandCentres()
    {
        static const std::array<float, numBands> c { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
        return c;
    }

    inline float powerToLufs (double power) { return power > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (power)) : -100.0f; }

    //==============================================================================
    struct BiquadD
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0; }
    };

    /** The two-stage K-weighting filter from BS.1770, designed for any sample rate. */
    struct KWeighting
    {
        BiquadD shelf, highpass;

        void prepare (double fs)
        {
            {   // Stage 1: high shelf (+4 dB above ~1.7 kHz)
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
                const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
                shelf.b1 = 2.0 * (K * K - Vh) / a0;
                shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
                shelf.a1 = 2.0 * (K * K - 1.0) / a0;
                shelf.a2 = (1.0 - K / Q + K * K) / a0;
            }
            {   // Stage 2: high-pass (RLB weighting)
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
                const double a0 = 1.0 + K / Q + K * K;
                highpass.b0 = 1.0; highpass.b1 = -2.0; highpass.b2 = 1.0;
                highpass.a1 = 2.0 * (K * K - 1.0) / a0;
                highpass.a2 = (1.0 - K / Q + K * K) / a0;
            }
            shelf.reset();
            highpass.reset();
        }

        double process (double x) noexcept { return highpass.process (shelf.process (x)); }
    };

    //==============================================================================
    class LoudnessMeter
    {
    public:
        static constexpr int histogramBins = 751;   // -70.0 .. +5.0 LUFS in 0.1 LU steps

        void prepare (double fs)
        {
            for (auto& k : weighting) k.prepare (fs);
            subBlockLength = juce::jmax (1, (int) std::round (fs * 0.1));
            reset();
        }

        void reset()
        {
            subSum = 0.0; subCount = 0; subIndex = 0; subsFilled = 0;
            subPowers.fill (0.0);
            for (auto& h : histogram) h.store (0, std::memory_order_relaxed);
            momentary.store (-100.0f);
            shortTerm.store (-100.0f);
        }

        void process (float left, float right) noexcept
        {
            const auto l = weighting[0].process (left), r = weighting[1].process (right);
            subSum += l * l + r * r;

            if (++subCount < subBlockLength)
                return;

            // A 100 ms sub-block is complete.
            subPowers[(size_t) subIndex] = subSum / subCount;
            subIndex = (subIndex + 1) % (int) subPowers.size();
            subsFilled = std::min (subsFilled + 1, (int) subPowers.size());
            subSum = 0.0; subCount = 0;

            const auto m = meanOfLast (4);
            lastMomentaryPower = m;
            ++subBlocks;
            momentary.store (powerToLufs (m));
            if (subsFilled >= 30)
                shortTerm.store (powerToLufs (meanOfLast (30)));

            // Every 100 ms a new overlapping 400 ms gating block exists.
            if (subsFilled >= 4)
            {
                const auto lufs = powerToLufs (m);
                if (lufs > -70.0f)
                {
                    const auto bin = juce::jlimit (0, histogramBins - 1, (int) std::lround ((lufs + 70.0f) * 10.0f));
                    histogram[(size_t) bin].fetch_add (1, std::memory_order_relaxed);
                }
            }
        }

        /** Gated integrated loudness (absolute -70 LUFS gate, relative -10 LU gate). */
        float integrated() const
        {
            auto powerOf = [] (int bin) { return std::pow (10.0, ((bin / 10.0 - 70.0) + 0.691) / 10.0); };

            double sum = 0.0; uint64_t count = 0;
            for (int i = 0; i < histogramBins; ++i)
            {
                const auto n = histogram[(size_t) i].load (std::memory_order_relaxed);
                sum += n * powerOf (i); count += n;
            }
            if (count == 0)
                return -100.0f;

            const auto relativeGate = powerToLufs (sum / (double) count) - 10.0f;
            sum = 0.0; count = 0;
            for (int i = 0; i < histogramBins; ++i)
            {
                if (i / 10.0f - 70.0f < relativeGate) continue;
                const auto n = histogram[(size_t) i].load (std::memory_order_relaxed);
                sum += n * powerOf (i); count += n;
            }
            return count == 0 ? -100.0f : powerToLufs (sum / (double) count);
        }

        std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f };

        /** Audio thread: counts completed 100 ms sub-blocks, so callers can act on each new
            momentary (400 ms) measurement; lastMomentaryPower is its mean square. */
        uint32_t subBlockCount() const noexcept { return subBlocks; }
        double lastMomentaryPower = 0.0;

    private:
        double meanOfLast (int n) const noexcept
        {
            double s = 0.0;
            for (int k = 1; k <= n; ++k)
                s += subPowers[(size_t) ((subIndex - k + (int) subPowers.size()) % (int) subPowers.size())];
            return s / n;
        }

        std::array<KWeighting, 2> weighting;
        std::array<double, 30> subPowers {};
        std::array<std::atomic<uint32_t>, histogramBins> histogram {};
        double subSum = 0.0;
        int subCount = 0, subBlockLength = 4800, subIndex = 0, subsFilled = 0;
        uint32_t subBlocks = 0;
    };

    //==============================================================================
    class TruePeakMeter
    {
    public:
        static constexpr int factor = 4, tapsPerPhase = 12;

        TruePeakMeter()
        {
            // Hann-windowed sinc interpolator, normalised so each phase has unity DC gain.
            constexpr int total = factor * tapsPerPhase;
            for (int p = 0; p < factor; ++p)
            {
                double sum = 0.0;
                for (int k = 0; k < tapsPerPhase; ++k)
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

        void reset() { for (auto& h : history) h.fill (0.0f); peak.store (0.0f); }

        void process (float left, float right) noexcept
        {
            const float in[2] = { left, right };
            auto maxNow = 0.0f;
            for (size_t c = 0; c < 2; ++c)
            {
                auto& h = history[c];
                std::move_backward (h.begin(), h.end() - 1, h.end());
                h[0] = in[c];
                for (size_t p = 0; p < (size_t) factor; ++p)
                {
                    float y = 0.0f;
                    for (size_t k = 0; k < (size_t) tapsPerPhase; ++k)
                        y += h[k] * coeffs[p][k];
                    maxNow = std::max (maxNow, std::abs (y));
                }
            }
            if (maxNow > peak.load (std::memory_order_relaxed))
                peak.store (maxNow, std::memory_order_relaxed);
        }

        float peakDb() const { return juce::Decibels::gainToDecibels (peak.load(), -100.0f); }

        std::atomic<float> peak { 0.0f };

    private:
        std::array<std::array<float, tapsPerPhase>, factor> coeffs {};
        std::array<std::array<float, tapsPerPhase>, 2> history {};
    };

    //==============================================================================
    class StereoMeter
    {
    public:
        void prepare (double fs)
        {
            coeff = 1.0 - std::exp (-1.0 / (0.3 * fs)); // 300 ms integration
            lowL.set (120.0f, (float) fs);
            lowR.set (120.0f, (float) fs);
            lowL.reset(); lowR.reset();
            full = low = {};
        }

        void process (float l, float r) noexcept
        {
            float ll, lh, rl, rh;
            lowL.process (l, ll, lh);
            lowR.process (r, rl, rh);
            full.add (l, r, coeff);
            low.add (ll, rl, coeff);
            correlation.store (full.correlation());
            lowCorrelation.store (low.correlation());
            monoLossDb.store (full.monoLossDb());
        }

        std::atomic<float> correlation { 1.0f }, lowCorrelation { 1.0f }, monoLossDb { 0.0f };

    private:
        struct Sums
        {
            double lr = 0, ll = 0, rr = 0;
            void add (double l, double r, double c) noexcept
            {
                lr += c * (l * r - lr); ll += c * (l * l - ll); rr += c * (r * r - rr);
            }
            float correlation() const noexcept
            {
                const auto d = std::sqrt (ll * rr);
                return d < 1.0e-12 ? 1.0f : (float) juce::jlimit (-1.0, 1.0, lr / d);
            }
            float monoLossDb() const noexcept
            {
                // Energy of the mono sum relative to the average channel energy.
                const auto stereo = 0.5 * (ll + rr), mono = 0.25 * (ll + rr + 2.0 * lr);
                return stereo < 1.0e-12 ? 0.0f : (float) (10.0 * std::log10 (std::max (1.0e-12, mono / stereo)));
            }
        };

        suite::Crossover lowL, lowR;
        Sums full, low;
        double coeff = 0.001;
    };

    //==============================================================================
    class BandAnalyzer
    {
    public:
        void prepare (double fs)
        {
            for (int b = 0; b < numBands; ++b)
            {
                filters[(size_t) b].set (bandCentres()[(size_t) b], 1.41f, (float) fs);
                filters[(size_t) b].reset();
            }
            subLength = juce::jmax (1, (int) std::round (fs * 0.1));
            shortCoeff = 1.0 - std::exp (-0.1 / 3.0); // ~3 s memory, updated every 100 ms
            reset();
        }

        void reset()
        {
            subPower.fill (0.0); subCount = 0;
            shortPower.fill (0.0); longSum.fill (0.0); longBlocks = 0;
            for (auto& a : shortDb) a.store (-100.0f);
            for (auto& a : longDb) a.store (-100.0f);
            longSeconds.store (0.0f);
        }

        void process (float l, float r) noexcept
        {
            const auto m = 0.5f * (l + r);
            for (size_t b = 0; b < (size_t) numBands; ++b)
            {
                const auto y = filters[b].bandpass (m);
                subPower[b] += (double) y * y;
            }
            totalPower += (double) m * m;

            if (++subCount < subLength)
                return;

            const auto audible = totalPower / subCount > 1.0e-6; // ignore silence (below -60 dBFS)
            for (size_t b = 0; b < (size_t) numBands; ++b)
            {
                const auto p = subPower[b] / subCount;
                shortPower[b] += shortCoeff * (p - shortPower[b]);
                shortDb[b].store ((float) (10.0 * std::log10 (shortPower[b] + 1.0e-12)));
                if (audible) longSum[b] += p;
                subPower[b] = 0.0;
            }
            if (audible)
            {
                ++longBlocks;
                for (size_t b = 0; b < (size_t) numBands; ++b)
                    longDb[b].store ((float) (10.0 * std::log10 (longSum[b] / (double) longBlocks + 1.0e-12)));
                longSeconds.store ((float) longBlocks * 0.1f);
            }
            subCount = 0;
            totalPower = 0.0;
        }

        std::array<std::atomic<float>, numBands> shortDb {}, longDb {};
        std::atomic<float> longSeconds { 0.0f };

    private:
        std::array<suite::Svf, numBands> filters;
        std::array<double, numBands> subPower {}, shortPower {}, longSum {};
        double shortCoeff = 0.03, totalPower = 0.0;
        int subCount = 0, subLength = 4800;
        uint64_t longBlocks = 0;
    };
}
