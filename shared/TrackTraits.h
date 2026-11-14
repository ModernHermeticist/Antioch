#pragma once

// What every track is like, measured where it plays (each Quintessence) and shared for the
// label-ready checks: how hot it comes in (clip gain), how fast it comes back after a duck
// (sidechain recovery), how much stereo it has under 150 Hz, how long its tails ring (reverb
// size), how spiky and how wide it is (depth), its level in each section of the song (automation
// left behind), and a short low-rate copy of its waveform, stamped with the song position, so two
// layers of one part can be checked for phase against each other.
// Audio thread: TraitsMeter. Message thread: publish / list. Shared memory, seqlock per slot.

#include "SharedMemory.h"
#include "SuiteDSP.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <vector>

namespace suite::TrackTraits
{
    constexpr int maxSlots = 64, maxSections = 32, ringSize = 2048;
    constexpr double ringRate = 4000.0;   // the waveform copy's sample rate (Hz)

    struct Traits
    {
        uint32_t id = 0;
        int role = 0;
        char track[48] {};
        float inPeakDb = -100.0f, inRmsDb = -100.0f;       // what comes in (before Quintessence)
        float outRmsDb = -100.0f, sideDb = -100.0f;        // what goes out, and its side
        float lowMidDb = -100.0f, lowSideDb = -100.0f;     // under 150 Hz: the middle and the side
        float crestDb = 0.0f;                              // peak over RMS (spiky = in front)
        float decayMs = 0.0f;                              // how long a hit takes to fall 20 dB (x3 = RT60), 0 = unknown
        int decayCount = 0;
        float recoveryMs = 0.0f;                           // after a duck, back within 1 dB (median), 0 = never ducked
        int duckCount = 0;
        std::array<float, maxSections> sectionDb {};       // its level in each section of the map (-100 = not heard there)
        std::array<float, maxSections> sectionPeakDb {};   // its loudest sample in each section
        std::array<float, maxSections> sectionDbOffline {}; // the same, measured while FL rendered (offline), -100 = never rendered
        float movementDb = 0.0f;                           // how much its tone changes from one tenth of a second to the next (dB)
        int movementCount = 0;
        float noiseFloorDb = -100.0f;                      // its quietest tenths of a second while it plays (10th percentile)
        float hitSpreadDb = 0.0f;                          // how far its hits' peaks spread (90th - 10th percentile)
        float subsonicDb = -100.0f;                        // under 20 Hz against its level (dB)
        float cpuPercent = 0.0f;                           // the plugin's share of the audio thread's time (%)
        int hitCount = 0;
        // the waveform copy (mono, 250..1500 Hz, 4 kHz), newest sample at ring[(ringPos - 1) % ringSize]
        std::array<float, ringSize> ring {};
        int ringPos = 0;
        double ringPpq = -1.0, ringBpm = 120.0;            // the song position of the newest sample
        bool valid() const { return id != 0; }
        juce::String trackName() const { return juce::String::fromUTF8 (track); }
        /** The copy as a time-ordered vector (oldest first). */
        std::vector<float> waveform() const
        {
            std::vector<float> w ((size_t) ringSize);
            for (int i = 0; i < ringSize; ++i) w[(size_t) i] = ring[(size_t) ((ringPos + i) % ringSize)];
            return w;
        }
    };

    namespace detail
    {
        struct Slot { std::atomic<uint32_t> seq, stamp; Traits t; };
        struct Shared { Slot slots[maxSlots]; };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_tracktraits_v3", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t nowSeconds() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<seconds> (steady_clock::now().time_since_epoch()).count() + 1;
        }
    }

    inline void publish (const Traits& t) noexcept
    {
        if (t.id == 0) return;
        auto& s = detail::shared();
        int slot = -1;
        const auto now = detail::nowSeconds();
        for (int i = 0; i < maxSlots && slot < 0; ++i) if (s.slots[i].t.id == t.id && s.slots[i].stamp.load() != 0) slot = i;
        for (int i = 0; i < maxSlots && slot < 0; ++i) if (s.slots[i].stamp.load() == 0 || now - s.slots[i].stamp.load() > 30) slot = i;
        if (slot < 0) return;
        auto& sl = s.slots[slot];
        sl.seq.fetch_add (1);
        sl.t = t;
        sl.seq.fetch_add (1);
        sl.stamp.store (now);
    }
    inline void withdraw (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        for (auto& sl : s.slots) if (sl.t.id == id) sl.stamp.store (0);
    }
    inline std::vector<Traits> list (uint32_t maxAgeSeconds = 10)
    {
        std::vector<Traits> out;
        auto& s = detail::shared();
        const auto now = detail::nowSeconds();
        for (auto& sl : s.slots)
        {
            const auto st = sl.stamp.load();
            if (st == 0 || now - st > maxAgeSeconds) continue;
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto before = sl.seq.load();
                if ((before & 1u) != 0) continue;
                Traits t = sl.t;
                if (sl.seq.load() != before) continue;
                if (t.id != 0) out.push_back (t);
                break;
            }
        }
        return out;
    }
    inline void reset() noexcept { for (auto& sl : detail::shared().slots) { sl.stamp.store (0); sl.t.id = 0; } }   // tests

    //==============================================================================
    /** Two layers of one part played together: their correlation as they are, and the best one
        within +-3 ms (a layer late or early). Aligned by the song position of their copies. */
    struct LayerPhase { bool valid = false; float correlation = 0.0f, bestCorrelation = 0.0f; float lagMs = 0.0f; };
    inline LayerPhase layerPhase (const Traits& a, const Traits& b)
    {
        LayerPhase out;
        if (a.ringPpq < 0.0 || b.ringPpq < 0.0 || a.ringBpm <= 0.0) return out;
        const auto shiftSamples = (a.ringPpq - b.ringPpq) * 60.0 / a.ringBpm * ringRate;   // a is this much newer
        if (std::abs (shiftSamples) > ringSize / 2) return out;
        const auto wa = a.waveform(), wb = b.waveform();
        const auto shift = (int) std::lround (shiftSamples);
        auto corrAt = [&] (int lag)
        {
            // a[i] lines up with b[i - shift + lag]
            double ab = 0.0, aa = 0.0, bb = 0.0;
            for (int i = 0; i < ringSize; ++i)
            {
                const auto j = i + shift + lag;   // b's index of the same moment (b older by shift)
                if (j < 0 || j >= ringSize) continue;
                const auto x = (double) wa[(size_t) i], y = (double) wb[(size_t) j];
                ab += x * y; aa += x * x; bb += y * y;
            }
            return aa > 1.0e-9 && bb > 1.0e-9 ? (float) (ab / std::sqrt (aa * bb)) : 0.0f;
        };
        // energy in both at all?
        double ea = 0.0, eb = 0.0;
        for (auto v : wa) ea += (double) v * v;
        for (auto v : wb) eb += (double) v * v;
        if (ea < 1.0e-6 || eb < 1.0e-6) return out;
        out.valid = true;
        out.correlation = corrAt (0);
        out.bestCorrelation = out.correlation;
        const auto maxLag = (int) (0.003 * ringRate);
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            const auto c = corrAt (lag);
            if (c > out.bestCorrelation) { out.bestCorrelation = c; out.lagMs = (float) (lag * 1000.0 / ringRate); }
        }
        return out;
    }

    //==============================================================================
    /** Audio-thread meter (one per Quintessence). Feed it what comes in (input) and what goes out
        (output); the message thread takes a snapshot about once a second. */
    class TraitsMeter
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            lowL.set (150.0f, 0.707f, fs); lowR.set (150.0f, 0.707f, fs);
            band.set (600.0f, 0.7f, fs);
            highBand.set (3000.0f, 0.707f, fs);
            subL.set (20.0f, 0.707f, fs);
            windowLength = juce::jmax (1, (int) (0.1 * sampleRate));
            decim = juce::jmax (1, (int) std::lround (sampleRate / ringRate));
            fastA = 1.0f - std::exp (-1.0f / (0.005f * fs));
            refA = 1.0f - std::exp (-1.0f / (0.25f * fs));
            reset();
        }
        void reset() noexcept
        {
            inPeak = 0.0f; inPow = 0.0; inN = 0;
            outPow = sidePow = lowMidPow = lowSidePow = 0.0; outN = 0; peakOut = 0.0f;
            envFast = envRef = 0.0f;
            sectionPow.fill (0.0); sectionN.fill (0); sectionPeak.fill (0.0f); sectionPowOff.fill (0.0); sectionNOff.fill (0);
            nDecays.store (0); nRecoveries.store (0); nHits.store (0);
            winPow = winLow = winHigh = 0.0; winN = 0; prevWin = { -200.0f, -200.0f, -200.0f }; moveSum = 0.0; moveN = 0; floorHist.fill (0);
            inHit = false; ducking = false; hitPeak = 0.0f; hitSamples = duckSamples = 0;
            ring.fill (0.0f); ringPos = 0; decimAcc = 0.0f; decimCount = 0; ringPpq = -1.0;
        }
        void input (const float* l, const float* r, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const auto a = std::abs (l[i]), b = r != nullptr ? std::abs (r[i]) : a;
                inPeak = std::max (inPeak, std::max (a, b));
                inPow += 0.5 * ((double) l[i] * l[i] + (r != nullptr ? (double) r[i] * r[i] : (double) l[i] * l[i]));
            }
            inN += n;
        }
        /** What goes out, with the song position at the block's start (ppq < 0 = not playing) and
            the section index there (-1 = none). */
        void output (const float* l, const float* r, int n, double ppq, double bpm, int sectionIndex, bool offline = false) noexcept
        {
            const auto qPerSample = bpm / 60.0 / (double) fs;
            // a new play-through: measure afresh (the waveform copy carries on)
            if (ppq >= 0.0 && ! wasPlaying) { inPeak = 0.0f; inPow = 0.0; inN = 0; outPow = sidePow = lowMidPow = lowSidePow = 0.0; outN = 0; peakOut = 0.0f;
                                              sectionPow.fill (0.0); sectionN.fill (0); sectionPeak.fill (0.0f); nDecays.store (0); nRecoveries.store (0); nHits.store (0);
                                              moveSum = 0.0; moveN = 0; floorHist.fill (0); }
            wasPlaying = ppq >= 0.0;
            for (int i = 0; i < n; ++i)
            {
                const auto L = l[i], R = r != nullptr ? r[i] : l[i];
                const auto mid = 0.5f * (L + R), side = 0.5f * (L - R);
                outPow += (double) mid * mid; sidePow += (double) side * side;
                const auto lo1 = lowL.lowpass (L), lo2 = lowR.lowpass (R);
                { const auto sub = subL.lowpass (0.5f * (L + R)); subPow += (double) sub * sub; }
                const auto lm = 0.5f * (lo1 + lo2), ls = 0.5f * (lo1 - lo2);
                lowMidPow += (double) lm * lm; lowSidePow += (double) ls * ls;
                const auto a = std::abs (mid);
                peakOut = std::max (peakOut, a);
                if (sectionIndex >= 0 && sectionIndex < maxSections)
                {
                    if (offline) { sectionPowOff[(size_t) sectionIndex] += (double) mid * mid; ++sectionNOff[(size_t) sectionIndex]; }
                    else
                    {
                        sectionPow[(size_t) sectionIndex] += (double) mid * mid; ++sectionN[(size_t) sectionIndex];
                        sectionPeak[(size_t) sectionIndex] = std::max (sectionPeak[(size_t) sectionIndex], std::max (std::abs (L), std::abs (R)));
                    }
                }
                // tenths of a second: the tone's movement and the noise floor
                {
                    const auto hi = highBand.highpass (mid);
                    winPow += (double) mid * mid; winLow += (double) lm * lm; winHigh += (double) hi * hi;
                    if (++winN >= windowLength)
                    {
                        auto db = [this] (double p) { return (float) (10.0 * std::log10 (p / winN + 1.0e-12)); };
                        const auto all = db (winPow);
                        if (all > -70.0f && ppq >= 0.0)
                        {
                            const std::array<float, 3> now { db (winLow), db (std::max (1.0e-12, winPow - winLow - winHigh)), db (winHigh) };
                            if (prevWin[0] > -199.0f)
                            {
                                float d = 0.0f;
                                for (size_t k = 0; k < 3; ++k) d += std::abs ((now[k] - all) - (prevWin[k] - prevAll));
                                moveSum += d / 3.0f; ++moveN;
                            }
                            prevWin = now; prevAll = all;
                            ++floorHist[(size_t) juce::jlimit (0, 199, (int) std::lround (-2.0f * all))];
                        }
                        else prevWin = { -200.0f, -200.0f, -200.0f };
                        winPow = winLow = winHigh = 0.0; winN = 0;
                    }
                }
                // envelopes (fast 5 ms, reference 250 ms)
                envFast += fastA * (a - envFast);
                envRef += refA * (a - envRef);
                trackDecay();
                trackDuck();
                // the waveform copy
                decimAcc += band.bandpass (mid);
                if (++decimCount >= decim)
                {
                    ring[(size_t) ringPos] = decimAcc / (float) decim;
                    ringPos = (ringPos + 1) % ringSize;
                    decimAcc = 0.0f; decimCount = 0;
                    if (ppq >= 0.0) ringPpq = ppq + (double) i * qPerSample;
                }
            }
            outN += n;
            ringBpm = bpm;
            if (ppq < 0.0) ringPpq = -1.0;
        }
        /** Message thread: what's been measured so far. */
        Traits snapshot (uint32_t id, int role, const juce::String& trackName) const
        {
            Traits t;
            t.id = id; t.role = role;
            trackName.copyToUTF8 (t.track, sizeof (t.track) - 1);
            auto db = [] (double p, int count) { return count > 0 ? (float) (10.0 * std::log10 (p / count + 1.0e-12)) : -100.0f; };
            t.inPeakDb = inN > 0 ? juce::Decibels::gainToDecibels (inPeak, -100.0f) : -100.0f;
            t.inRmsDb = db (inPow, inN);
            t.outRmsDb = db (outPow, outN); t.sideDb = db (sidePow, outN);
            t.lowMidDb = db (lowMidPow, outN); t.lowSideDb = db (lowSidePow, outN);
            t.subsonicDb = outN > 0 && t.outRmsDb > -90.0f ? db (subPow, outN) - t.outRmsDb : -100.0f;
            t.cpuPercent = cpu.load();
            t.crestDb = outN > 0 && t.outRmsDb > -90.0f ? juce::Decibels::gainToDecibels (peakOut, -100.0f) - t.outRmsDb : 0.0f;
            auto median = [] (const std::array<float, maxEvents>& a, int count)
            {
                std::vector<float> v (a.begin(), a.begin() + juce::jlimit (0, maxEvents, count));
                if (v.empty()) return 0.0f;
                std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
                return v[v.size() / 2];
            };
            t.decayMs = median (decays, nDecays.load()); t.decayCount = nDecays.load();
            t.recoveryMs = median (recoveries, nRecoveries.load()); t.duckCount = nRecoveries.load();
            for (int s = 0; s < maxSections; ++s)
            {
                t.sectionDb[(size_t) s] = sectionN[(size_t) s] > (int) fs ? db (sectionPow[(size_t) s], sectionN[(size_t) s]) : -100.0f;
                t.sectionPeakDb[(size_t) s] = sectionN[(size_t) s] > 0 ? juce::Decibels::gainToDecibels (sectionPeak[(size_t) s], -100.0f) : -100.0f;
                t.sectionDbOffline[(size_t) s] = sectionNOff[(size_t) s] > (int) fs ? db (sectionPowOff[(size_t) s], sectionNOff[(size_t) s]) : -100.0f;
            }
            t.movementCount = moveN; t.movementDb = moveN > 0 ? (float) (moveSum / moveN) : 0.0f;
            {
                int total = 0; for (auto c : floorHist) total += c;
                if (total >= 20)
                {
                    int acc = 0;
                    for (int b = 199; b >= 0; --b) { acc += floorHist[(size_t) b]; if (acc >= total / 10) { t.noiseFloorDb = -0.5f * (float) b; break; } }
                }
            }
            {
                const auto k = juce::jlimit (0, maxEvents, nHits.load());
                t.hitCount = k;
                if (k >= 8)
                {
                    std::vector<float> h (hits.begin(), hits.begin() + k);
                    std::sort (h.begin(), h.end());
                    t.hitSpreadDb = h[(size_t) (k * 9 / 10)] - h[(size_t) (k / 10)];
                }
            }
            t.ring = ring; t.ringPos = ringPos; t.ringPpq = ringPpq; t.ringBpm = ringBpm;
            return t;
        }
        float sampleRate() const noexcept { return fs; }

    private:
        // A hit: the fast envelope well over the reference. How long it takes to fall 20 dB under its
        // peak, if nothing else hits first.
        void trackDecay() noexcept
        {
            if (! inHit)
            {
                if (envFast > 2.0f * envRef && envFast > 1.0e-3f) { inHit = true; hitPeak = envFast; hitSamples = 0; }
                return;
            }
            ++hitSamples;
            if (envFast > hitPeak) { hitPeak = envFast; hitSamples = 0; }
            if (envFast < 0.1f * hitPeak || hitSamples > (int) (3.0f * fs))
                if (const auto k = nHits.load(); k < maxEvents) { hits[(size_t) k] = juce::Decibels::gainToDecibels (hitPeak, -100.0f); nHits.store (k + 1); }
            if (envFast < 0.1f * hitPeak)
            {
                if (const auto k = nDecays.load(); k < maxEvents) { decays[(size_t) k] = 1000.0f * (float) hitSamples / fs; nDecays.store (k + 1); }
                inHit = false;
            }
            else if (hitSamples > (int) (3.0f * fs)) inHit = false;   // held notes: not a decay
        }
        // A duck: the level falls 4 dB under its usual level quickly, then comes back to within 1 dB.
        void trackDuck() noexcept
        {
            if (envRef < 1.0e-3f) { ducking = false; return; }
            if (! ducking)
            {
                // a fall from its usual level to well under it within 30 ms
                sinceHigh = envFast >= 0.9f * envRef ? 0 : sinceHigh + 1;
                if (envFast < 0.63f * envRef && sinceHigh < (int) (0.03f * fs)) { ducking = true; duckSamples = sinceHigh; duckRef = envRef; }
            }
            else
            {
                ++duckSamples;
                if (envFast >= 0.89f * duckRef)
                {
                    if (const auto k = nRecoveries.load(); duckSamples > (int) (0.01f * fs) && k < maxEvents) { recoveries[(size_t) k] = 1000.0f * (float) duckSamples / fs; nRecoveries.store (k + 1); }
                    ducking = false;
                }
                else if (duckSamples > (int) (1.0f * fs)) ducking = false;   // a section change, not a duck
            }
        }

        float fs = 44100.0f, fastA = 0.1f, refA = 0.01f;
        Svf lowL, lowR, band, highBand, subL;
        double subPow = 0.0;
    public:
        std::atomic<float> cpu { 0.0f };   // set by the plugin (its share of the audio thread's time, %)
    private:
        float inPeak = 0.0f, peakOut = 0.0f;
        double inPow = 0.0, outPow = 0.0, sidePow = 0.0, lowMidPow = 0.0, lowSidePow = 0.0;
        int inN = 0, outN = 0;
        float envFast = 0.0f, envRef = 0.0f;
        std::array<double, maxSections> sectionPow {};
        std::array<int, maxSections> sectionN {}, sectionNOff {};
        std::array<double, maxSections> sectionPowOff {};
        std::array<float, maxSections> sectionPeak {};
        std::array<float, 256> hits {};
        std::atomic<int> nHits { 0 };
        double winPow = 0.0, winLow = 0.0, winHigh = 0.0, moveSum = 0.0;
        int winN = 0, moveN = 0, windowLength = 4410;
        std::array<float, 3> prevWin { -200.0f, -200.0f, -200.0f };
        float prevAll = 0.0f;
        std::array<int, 200> floorHist {};
        static constexpr int maxEvents = 256;
        std::array<float, maxEvents> decays {}, recoveries {};
        std::atomic<int> nDecays { 0 }, nRecoveries { 0 };
        bool inHit = false, ducking = false;
        float hitPeak = 0.0f, duckRef = 0.0f;
        int hitSamples = 0, duckSamples = 0, sinceHigh = 1 << 20;
        std::array<float, ringSize> ring {};
        int ringPos = 0, decim = 11, decimCount = 0;
        float decimAcc = 0.0f;
        double ringPpq = -1.0, ringBpm = 120.0;
        bool wasPlaying = false;
    };
}
