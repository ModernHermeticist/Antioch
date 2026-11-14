#pragma once

// Quintessence's live mixing (the autopilot's Mixing group): moves made moment to moment, while a
// clash lasts, on top of the slow, checked EQ and dynamics. None of them changes the track's
// overall level.
//   - Masking: where a more important part (vocal > lead > kick and bass > pads > hats and FX)
//     plays in the same band at the same time, this track dips there (up to 3 dB, two bands).
//     Pads and FX follow the lead's note instead (Vibration's pitch): a narrow dip at the note
//     and its octave, so the lead cuts through without the pad getting thin.
//   - The important part helped: a lead or vocal that's masked lifts its presence 1.5 dB.
//   - The low end's owner: with a kick or bass playing, other parts' energy under 70 Hz goes.
//   - Transients on the kick: a pad, lead, hat or FX attack landing on a kick hit is softened.
//   - Mono lows: the stereo part under 120 Hz is removed (the low end is the same in both speakers).
//   - Even bass notes: each bass note's low end is learned and the ones that boom or vanish evened.
//   - Bass on small speakers: a bass with little at 250-500 Hz (12 dB under its fundamentals) gets harmonics there.
//   - Moving resonances: in pads, leads and FX, a third-octave band (1-8 kHz) ringing far over its
//     neighbours dips while it rings, wherever the note takes it.
//   - Even drum hits: on kicks and percussion, a hit far louder than the ones around it is evened.
//   - The noise floor: under -60 dBFS the track is gently expanded down (tails, hiss).
//   - Kick and bass by note: the bass dips 6 dB at the kick's body pitch for the 60 ms after each hit.
//   - Layers spread: with two or more pads (or leads) playing, each sits a little left or right.
//   - The bass's sub (under 80 Hz) clears for the 6 ms before each kick and 3 ms into it.
//   - The vocal: plosive pops (sudden low bursts) cut for a moment; breaths between phrases 6 dB down.
//   - Click guard: a clip starting abruptly out of silence gets a 1 ms fade-in (not kicks or percussion).
//   - Hum: a steady 50 or 60 Hz hum (heard in the track's quiet moments) and its harmonics notched.
//   - Clip ends: a clip stopping abruptly into silence gets a 1 ms tail instead of a step (no latency).
//   - Layered parts in time: two tracks of the same role hitting together a few ms apart (two kick
//     layers, a layered lead): the earlier one waits for the later (up to 12 ms; no latency).
//   - Left/right: a track leaning to one side (over 1.5 dB, not a wide stereo sound) is re-centred, 3 dB at most.
//   - The kick in key: its body (under 150 Hz) is re-pitched down to the nearest note of the key
//     (the root or fifth first, 2 semitones at most); the click is left alone. Only ever down,
//     so it needs no look-ahead.

#include "RoleCatalog.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include "SuiteDSP.h"
#include "Analysis.h"
#include "AutopilotBus.h"
#include "Music.h"
#include "KeyBus.h"
#include <vector>

namespace qn
{
    class LiveMix
    {
    public:
        struct Context
        {
            int role = 0;              // 1 kick, 2 bass, 3 hats/perc, 4 pads, 5 lead, 6 vocal, 7 FX, 8 bus
            int rank = 0;              // who gives way in a clash (RoleCatalog's rank: the detailed role's, else the broad role's)
            uint32_t id = 0;
            bool mixing = true, stability = true;
            float kickMs = 1.0e9f;     // since the shared kick's last hit, at the block start
            float msPerSample = 0.0f;
            float kickBodyHz = 0.0f;   // the kick's body pitch (Kybalion), for the bass's dip
            float kickNextMs = 1.0e9f; // until the shared kick's next hit, at the block start
            double ppq = -1.0;         // song position at the block start (-1: not playing)
            double samplesPerQ = 0.0;
            bool keyValid = false;     // the song's key (kick tuning)
            int keyRoot = 0, keyScale = 0;
        };

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (int b = 0; b < analysis::numBands; ++b) { analysers[(size_t) b].set (analysis::bandCentres()[(size_t) b], 1.4f, fs); analysers[(size_t) b].reset(); }
            bandPow.fill (0.0);
            bandsDb.fill (-100.0f);
            powCoeff = 1.0 - std::exp (-1.0 / (0.08 * sampleRate));
            for (auto& s : maskEq) for (auto& f : s) f.reset();
            maskGain = { 0.0f, 0.0f }; maskSet = { { { 0, 0, 0 }, { 0, 0, 0 } } };
            for (auto& f : liftEq) f.reset();
            liftGain = 0.0f; liftSet = 0.0f;
            for (auto& f : lowSplit) { f.set (70.0f, 0.707f, fs); f.reset(); }
            lowMix = 0.0f;
            sideLow.set (120.0f, 0.707f, fs); sideLow.reset(); monoHz = 120.0f;
            for (auto& f : bassLp) { f.set (250.0f, 0.707f, fs); f.reset(); }
            pitchLp.set (300.0f, 0.707f, fs); pitchLp.reset();
            noteDb.fill (-100.0f); noteSeen.fill (0);
            noteGain = 1.0f; noteTarget = 1.0f; lastSign = false; sinceCross = 0; periods = 0; lastNote = -1; stableCount = 0; bassPow = 0.0;
            harmLp.set (120.0f, 0.707f, fs); harmLp.reset();
            harmBp.set (350.0f, 0.7f, fs); harmBp.reset();
            harmAmt = 0.0f; harmOn = false; fundSlow = upperSlow = -100.0f;
            for (int k = 0; k < numSoothe; ++k)
            {
                const auto hz = 1000.0f * std::pow (2.0f, (float) k / 3.0f);
                sootheDet[(size_t) k].set (hz, 4.3f, fs); sootheDet[(size_t) k].reset();
                for (auto& f : sootheApply[(size_t) k]) { f.set (hz, 4.3f, fs); f.reset(); }
                sootheEnv[(size_t) k].set (2.0f, 60.0f, fs); sootheEnv[(size_t) k].reset();
            }
            sootheGain.fill (1.0f);
            sootheP.fill (0.0); totalPow = 0.0; sootheUsual.fill (0.0f); sootheSeeded.fill (false); sootheDeepest.store (0.0f);
            sootheSlow = 1.0f - std::exp (-1.0f / (3.0f * fs));
            sootheCoeff = 1.0 - std::exp (-1.0 / (0.03 * sampleRate));
            fastEnv.set (0.5f, 30.0f, fs); slowEnv.set (20.0f, 200.0f, fs); fastEnv.reset(); slowEnv.reset();
            clashGain = hitGain = expGain = 1.0f;
            hitHold = hitMeasure = 0; hitPeak = 0.0f; hitAvgDb = -100.0f; hitsSeen = 0; hitTarget = 1.0f; inHit = false;
            expPow = 0.0; expHold = 0; wasLoud = false;
            a1ms = 1.0f - std::exp (-1.0f / (0.001f * fs));
            a5ms = 1.0f - std::exp (-1.0f / (0.005f * fs));
            a20ms = 1.0f - std::exp (-1.0f / (0.02f * fs));
            a40ms = 1.0f - std::exp (-1.0f / (0.04f * fs));
            a80ms = 1.0f - std::exp (-1.0f / (0.08f * fs));
            a200ms = 1.0f - std::exp (-1.0f / (0.2f * fs));
            expCoeff = 1.0 - std::exp (-1.0 / (0.03 * sampleRate));
            for (auto& f : kickDip) f.reset();
            kickDipHz = 0.0f;
            panNow = 0.0f;
            for (auto& f : tuneSplit) { f.set (150.0f, 0.707f, fs); f.reset(); }
            tuneLp.set (150.0f, 0.707f, fs); tuneLp.reset();
            for (auto& b : tuneBuf) b.assign ((size_t) (fs * 1.0f) + 8, 0.0f);
            tuneWrite = 0; tuneRead = 0.0; tuneOnsetAt = -1; tuneRatio = 1.0; tuneSince = 1 << 30; tuneFade = 0;
            tuneFast.set (0.3f, 20.0f, fs); tuneSlow.set (15.0f, 150.0f, fs); tuneFast.reset(); tuneSlow.reset();
            tuneZc = 0; tuneCrossings = 0; tuneLastSign = false; tunePeriodSum = 0.0; tunePeriods = 0; tuneBodyHz = 0.0f;
            for (auto& f : subSplit) { f.set (80.0f, 0.707f, fs); f.reset(); }
            subGain = 1.0f; kickNextNow = 1.0e9f;
            for (auto& f : popSplit) { f.set (150.0f, 0.707f, fs); f.reset(); }
            popDet.set (150.0f, 0.707f, fs); popDet.reset(); breathHp.set (3000.0f, 0.707f, fs); breathHp.reset();
            popFast.set (1.0f, 30.0f, fs); popSlow.set (30.0f, 200.0f, fs); popFast.reset(); popSlow.reset();
            popHold = 0; popAmt = 0.0f; vocPeak = 0.0; vocFast = 0.0; vocHf = 0.0; breathGain = 1.0f;
            quietCount = 0; fadeLeft = 0; fadeLen = juce::jmax (1, (int) (0.001f * fs));
            balL = balR = balLR = 0.0; balGain = { 1.0f, 1.0f };
            balCoeff = 1.0 - std::exp (-1.0 / (2.0 * sampleRate));
            fastCoeff = 1.0 - std::exp (-1.0 / (0.02 * sampleRate));
            peakRelease = 1.0 - std::exp (-1.0 / (3.0 * sampleRate));
            for (int h = 0; h < 2; ++h) { humDet[(size_t) h].set (h == 0 ? 50.0f : 60.0f, 25.0f, fs); humDet[(size_t) h].reset(); }
            humBroad.set (60.0f, 0.5f, fs); humBroad.reset();
            humPow = { 0.0, 0.0 }; humBroadPow = 0.0; humTotal = 0.0; humScore = 0.0f; humHz = 0.0f; humSet = 0.0f;
            for (auto& ch : humNotch) for (auto& f : ch) f.reset();
            lastOut = { 0.0f, 0.0f }; tailVal = { 0.0f, 0.0f }; tailLeft = 0;
            for (auto& d : alignLine) { d.prepare ((int) (0.02 * sampleRate) + 8); d.reset(); }
            alignSamples = 0.0f; alignTarget = 0.0f; sampleInBlock = 0; lastOnsetPpq = -1.0e9; alignCheck = 0;
            onsetFast.set (0.5f, 30.0f, fs); onsetSlow.set (20.0f, 200.0f, fs); onsetFast.reset(); onsetSlow.reset();
            counted = {};
        }

        /** Block start: reads what the other tracks play now and sets this block's moves. */
        void beginBlock (const Context& c, int numSamples)
        {
            ctx = c;
            for (int f = 0; f < suite::Autopilot::numFeatures; ++f) useF[(size_t) f] = c.mixing && suite::Autopilot::on ((suite::Autopilot::Feature) f);
            // label-ready: the mono lows' split follows the key (twice the bass's root, 90-150 Hz)
            {
                auto want = 120.0f;
                if (use (suite::Autopilot::keyMonoF))
                    if (const auto key = suite::KeyBus::read (8); key.valid)
                    {
                        auto hz = 440.0f * std::pow (2.0f, (float) (key.root + 28 - 69) / 12.0f) * 2.0f;   // E1..D#2, an octave up
                        while (hz < 90.0f) hz *= 2.0f;
                        while (hz > 150.0f) hz *= 0.5f;
                        want = hz;
                    }
                if (std::abs (want - monoHz) > 1.0f) { monoHz = want; sideLow.set (monoHz, 0.707f, fs); }
            }
            const auto mine = c.rank > 0 ? c.rank : suite::roles::legacyRank (c.role);
            const auto blockA = [&] (float tau) { return 1.0f - std::exp (-(float) numSamples / (tau * fs)); };

            std::array<std::array<float, 3>, 2> want { { { 0, 0, 0 }, { 0, 0, 0 } } };   // hz, gain dB, q
            float lift = 0.0f, owner = 0.0f;
            if (c.mixing && mine > 0)
            {
                std::array<float, analysis::numBands> need {};
                bool masked = false, lowOwned = false;
                suite::Autopilot::forEachLiveRanked (c.id, [&] (int role, int theirs, const float* other)
                {
                    if (theirs > mine)
                        for (int b = 3; b <= 8; ++b)
                            if (other[b] > -45.0f && bandsDb[(size_t) b] > -60.0f)
                                need[(size_t) b] = juce::jmax (need[(size_t) b], bandsDb[(size_t) b] - (other[b] - 6.0f));
                    if (theirs < mine && theirs > 0 && (c.role == 5 || c.role == 6))
                        for (int b = 5; b <= 7; ++b)
                            if (bandsDb[(size_t) b] > -50.0f && other[b] > bandsDb[(size_t) b] - 3.0f) masked = true;
                    if ((role == 1 || role == 2) && juce::jmax (other[0], other[1]) > -40.0f) lowOwned = true;
                });
                const auto leadHz = suite::Autopilot::leadPitch();
                if ((c.role == 4 || c.role == 7) && leadHz > 150.0f && leadHz < 2500.0f)
                {
                    // Room for the lead's note itself (and its octave)
                    want[0] = { leadHz, -3.0f, 6.0f };
                    want[1] = { leadHz * 2.0f, -2.0f, 6.0f };
                }
                else
                {
                    int first = -1, second = -1;
                    for (int b = 3; b <= 8; ++b)
                        if (need[(size_t) b] > 0.5f)
                        {
                            if (first < 0 || need[(size_t) b] > need[(size_t) first]) { second = first; first = b; }
                            else if (second < 0 || need[(size_t) b] > need[(size_t) second]) second = b;
                        }
                    for (int k = 0; k < 2; ++k)
                        if (const auto b = k == 0 ? first : second; b >= 0)
                            want[(size_t) k] = { analysis::bandCentres()[(size_t) b], -juce::jmin (3.0f, 0.5f * need[(size_t) b]), 1.0f };
                }
                if (masked) lift = 1.5f;
                if (lowOwned && c.role != 1 && c.role != 2 && c.role != 8 && juce::jmax (bandsDb[0], bandsDb[1]) > -55.0f) owner = 1.0f;
            }
            if (! use (suite::Autopilot::maskingF)) want = { { { 0, 0, 0 }, { 0, 0, 0 } } };
            if (! use (suite::Autopilot::liftF)) lift = 0.0f;
            if (! use (suite::Autopilot::lowOwnerF)) owner = 0.0f;
            for (int k = 0; k < 2; ++k)
            {
                auto& g = maskGain[(size_t) k];
                const auto target = want[(size_t) k][1];
                if (want[(size_t) k][0] > 0.0f) maskSet[(size_t) k][0] = want[(size_t) k][0], maskSet[(size_t) k][2] = want[(size_t) k][2];
                g += (target < g ? blockA (0.03f) : blockA (0.3f)) * (target - g);
                if (maskSet[(size_t) k][0] > 0.0f && std::abs (g - maskSet[(size_t) k][1]) > 0.05f)
                {
                    for (auto& f : maskEq[(size_t) k]) f.setPeak (maskSet[(size_t) k][0], maskSet[(size_t) k][2], g, fs);
                    maskSet[(size_t) k][1] = g;
                }
            }
            maskNow.store (juce::jmin (maskGain[0], maskGain[1]));
            once (maskGain[0] < -1.0f || maskGain[1] < -1.0f, 0, suite::Autopilot::masksDipped);
            liftGain += (lift > liftGain ? blockA (0.05f) : blockA (0.4f)) * (lift - liftGain);
            if (std::abs (liftGain - liftSet) > 0.05f) { for (auto& f : liftEq) f.setPeak (2500.0f, 1.0f, liftGain, fs); liftSet = liftGain; }
            liftNow.store (liftGain);
            once (liftGain > 1.0f, 1, suite::Autopilot::partsLifted);
            lowMix += blockA (0.2f) * (owner - lowMix);
            lowOwnerNow.store (lowMix);
            once (lowMix > 0.5f, 2, suite::Autopilot::lowEndCleared);

            // Bass on small speakers: the fundamentals' band against 250-500 Hz, over seconds
            if (use (suite::Autopilot::harmonicsF) && c.role == 2 && juce::jmax (bandsDb[0], bandsDb[1], bandsDb[2]) > -50.0f)
            {
                fundSlow += blockA (2.0f) * (juce::jmax (bandsDb[1], bandsDb[2]) - fundSlow);
                upperSlow += blockA (2.0f) * (juce::jmax (bandsDb[3], bandsDb[4]) - upperSlow);
            }
            // (with a margin: the harmonics it adds raise the upper band themselves)
            if (use (suite::Autopilot::harmonicsF) && c.role == 2 && fundSlow > -50.0f && upperSlow < fundSlow - 12.0f) harmOn = true;
            if (! use (suite::Autopilot::harmonicsF) || c.role != 2 || fundSlow < -55.0f || upperSlow > fundSlow - 6.0f) harmOn = false;
            const auto harmTarget = harmOn ? 1.0f : 0.0f;
            harmAmt += blockA (1.0f) * (harmTarget - harmAmt);
            harmonicsNow.store (harmAmt);
            once (harmAmt > 0.5f, 3, suite::Autopilot::harmonicsAdded);
            kickMsNow = c.kickMs;
            kickNextNow = c.kickNextMs;
            sampleInBlock = 0;
            // Hum: settle the notches on the found frequency and its harmonics
            if (std::abs (humHz - humSet) > 0.1f)
            {
                for (int k = 0; k < numHum; ++k)
                    for (auto& f : humNotch[(size_t) k]) f.setPeak (humHz > 0.0f ? humHz * (float) (k + 1) : 1000.0f, 30.0f, humHz > 0.0f ? -30.0f : 0.0f, fs);
                humSet = humHz;
            }
            // Layered parts: every half second, compare this track's attacks with the same role's others
            if (use (suite::Autopilot::layerAlignF) && c.ppq >= 0.0 && c.samplesPerQ > 0.0 && ++alignCheck % juce::jmax (1, (int) (0.5 * fs / juce::jmax (1, numSamples))) == 0)
            {
                const auto msPerQ = c.samplesPerQ / fs * 1000.0;
                std::array<double, 16> mine {};
                auto& s = suite::Autopilot::detail::shared();
                for (int i = 0; i < suite::Autopilot::maxLive; ++i)
                    if (s.liveId[i].load() == c.id) for (int k = 0; k < 16; ++k) mine[(size_t) k] = s.liveOnset[i][k].load();
                float best = 0.0f;
                suite::Autopilot::forEachLiveOnsets (c.id, [&] (int role, const double* other)
                {
                    if (role != c.role) return;
                    std::vector<double> diffs;
                    for (const auto m : mine)
                    {
                        if (m <= 0.0) continue;
                        double nearest = 1.0e9;
                        for (int k = 0; k < 16; ++k)
                            if (other[k] > 0.0 && std::abs (other[k] - m) * msPerQ < std::abs (nearest)) nearest = (other[k] - m) * msPerQ;
                        if (std::abs (nearest) < 15.0) diffs.push_back (nearest);
                    }
                    if (diffs.size() < 8) return;
                    std::sort (diffs.begin(), diffs.end());
                    const auto median = diffs[diffs.size() / 2];
                    double spread = 0.0;
                    for (const auto d : diffs) spread += std::abs (d - median);
                    spread /= (double) diffs.size();
                    if (median > 0.4 && median < 12.0 && spread < 0.5) best = juce::jmax (best, (float) median);   // the other hits later: wait for it
                });
                alignTarget = best * (float) fs / 1000.0f;
            }
            if (! use (suite::Autopilot::layerAlignF)) alignTarget = 0.0f;

            // Kick and bass by note
            if (use (suite::Autopilot::kickBassDipF) && c.role == 2 && c.kickBodyHz > 25.0f && std::abs (c.kickBodyHz - kickDipHz) > 0.5f)
            {
                for (auto& f : kickDip) f.set (c.kickBodyHz, 4.0f, fs);
                kickDipHz = c.kickBodyHz;
            }
            if (c.role != 2 || c.kickBodyHz <= 25.0f || ! use (suite::Autopilot::kickBassDipF)) kickDipHz = 0.0f;

            // Layers spread: another part with this role playing now
            float panTarget = 0.0f;
            if (use (suite::Autopilot::layerSpreadF) && (c.role == 4 || c.role == 5))
            {
                int same = 0;
                suite::Autopilot::forEachLive (c.id, [&] (int role, const float* other)
                {
                    if (role == c.role && juce::jmax (other[4], other[5], other[6]) > -50.0f) ++same;
                });
                if (same > 0 && juce::jmax (bandsDb[4], bandsDb[5], bandsDb[6]) > -50.0f)
                    panTarget = ((suite::Autopilot::detail::hash32 (c.id) & 1u) != 0 ? 0.15f : -0.15f);
            }
            panNow += blockA (0.5f) * (panTarget - panNow);
            spreadNow.store (panNow);
            once (std::abs (panNow) > 0.08f, 8, suite::Autopilot::layersSpread);

            // The kick in key: from the body's measured pitch, the nearest key note below it
            tuneRatio = 1.0;
            if (use (suite::Autopilot::kickTuneF) && c.role == 1 && c.keyValid && tuneBodyHz > 30.0f)
            {
                const auto& steps = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, c.keyScale)].steps;
                const auto midi = 69.0 + 12.0 * std::log2 (tuneBodyHz / 440.0);
                double best = 1.0e9;
                for (int down = 0; down <= 200; ++down)   // hundredths of a semitone, downwards only
                {
                    const auto m = midi - down / 100.0;
                    const auto rounded = std::round (m);
                    if (std::abs (m - rounded) > 0.005) continue;
                    const auto pc = (((int) rounded - c.keyRoot) % 12 + 12) % 12;
                    if (std::find (steps.begin(), steps.end(), pc) == steps.end()) continue;
                    const auto preferred = pc == 0 || pc == 7;
                    const auto cost = down / 100.0 - (preferred ? 0.75 : 0.0);   // the root or fifth, if it's not much further
                    if (cost < best) { best = cost; tuneRatio = std::pow (2.0, -(down / 100.0) / 12.0); }
                }
                if (tuneRatio > 0.9913) tuneRatio = 1.0;   // within 15 cents: in tune already
            }
            tuneRatioNow.store ((float) tuneRatio);
            once (tuneRatio < 0.999, 9, suite::Autopilot::kickTuned);
        }

        /** One stereo sample (r == l for mono tracks). */
        void process (float& l, float& r, bool stereo) noexcept
        {
            const auto mix = ctx.mixing;
            // What this track plays, before these moves (for the others, and to judge its own)
            {
                const auto mono = 0.5f * (l + r);
                for (int b = 0; b < analysis::numBands; ++b)
                {
                    const auto y = analysers[(size_t) b].bandpass (mono) / 1.4f;
                    bandPow[(size_t) b] += powCoeff * ((double) y * y - bandPow[(size_t) b]);
                }
            }
            float x[2] = { l, r };
            // Layered parts: the attacks as they come in (before any delay, so lining up doesn't
            // undo itself), shared with the other tracks
            {
                const auto a = 0.5f * (std::abs (x[0]) + std::abs (x[1]));
                const auto af = onsetFast.process (a), as = onsetSlow.process (a);
                if (af > 2.0f * as && af > 1.0e-3f && ctx.ppq >= 0.0 && ctx.samplesPerQ > 0.0 && ctx.id != 0)
                {
                    const auto at = ctx.ppq + (double) sampleInBlock / ctx.samplesPerQ;
                    if ((at - lastOnsetPpq) * ctx.samplesPerQ > 0.05 * fs) { suite::Autopilot::postOnset (ctx.id, at); lastOnsetPpq = at; }
                }
                ++sampleInBlock;
            }
            // Layered parts: this one waits for the later layer (the delay glides, so it never clicks)
            {
                alignSamples += (alignTarget > alignSamples ? 1.0f : -1.0f) * juce::jmin (std::abs (alignTarget - alignSamples), 0.02f);
                for (int c = 0; c < 2; ++c)
                {
                    alignLine[(size_t) c].push (x[c]);
                    if (alignSamples > 0.05f) x[c] = alignLine[(size_t) c].read (1.0f + alignSamples);
                }
                alignNowMs.store (alignSamples / fs * 1000.0f);
                once (alignSamples / fs * 1000.0f > 0.4f, 14, suite::Autopilot::layersAligned);
            }
            if (use (suite::Autopilot::humF)) hum (x);
            if (use (suite::Autopilot::clickGuardF) && ctx.role != 1 && ctx.role != 3) clickGuard (x);
            if (use (suite::Autopilot::balanceF) && ctx.role > 0 && stereo) balance (x);
            if (mix)
            {
                // Layers spread (before the mono lows, so the low end stays centred)
                if (std::abs (panNow) > 0.001f && stereo) { x[0] *= 1.0f - panNow; x[1] *= 1.0f + panNow; }
                // Mono lows
                if (stereo && use (suite::Autopilot::monoLowsF))
                {
                    const auto mid = 0.5f * (x[0] + x[1]);
                    auto side = 0.5f * (x[0] - x[1]);
                    side = sideLow.highpass (side);
                    x[0] = mid + side; x[1] = mid - side;
                }
                for (int c = 0; c < 2; ++c)
                {
                    auto v = x[c];
                    if (lowMix > 0.001f) { const auto hp = lowSplit[(size_t) c].highpass (v); v += (hp - v) * lowMix; }
                    else lowSplit[(size_t) c].highpass (v);
                    if (ctx.role == 2 && use (suite::Autopilot::bassNotesF))
                    {
                        if (std::abs (noteGain - 1.0f) > 0.002f) v += bassLp[(size_t) c].lowpass (v) * (noteGain - 1.0f);
                        else bassLp[(size_t) c].lowpass (v);
                    }
                    for (int k = 0; k < 2; ++k) if (maskGain[(size_t) k] < -0.05f) v = maskEq[(size_t) k][(size_t) c].process (v);
                    if (liftGain > 0.05f) v = liftEq[(size_t) c].process (v);
                    x[c] = v;
                }
                if (ctx.role == 2 && use (suite::Autopilot::bassNotesF)) bassNotes (0.5f * (l + r));
                if (ctx.role == 2 && kickDipHz > 0.0f)
                {
                    const auto depth = kickMsNow < 200.0f ? 0.5f * std::exp (-kickMsNow / 60.0f) : 0.0f;   // up to 6 dB, gone in ~150 ms
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto band = kickDip[(size_t) c].bandpass (x[c]) / 4.0f;
                        x[c] -= band * depth;
                    }
                    once (depth > 0.25f, 10, suite::Autopilot::kickBassDips);
                }
                if (ctx.role == 1 && use (suite::Autopilot::kickTuneF)) tuneKick (x);
                if (ctx.role == 2 && use (suite::Autopilot::subBeforeKickF))
                {
                    // The sub clears just before each kick (and its first few ms)
                    kickNextNow -= ctx.msPerSample;
                    const auto clear = kickNextNow < 6.0f || kickMsNow < 3.0f;
                    const auto target = clear ? 0.25f : 1.0f;
                    subGain += (target < subGain ? a1ms : a20ms) * (target - subGain);
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto sub = subSplit[(size_t) c].lowpass (x[c]);
                        if (subGain < 0.999f) x[c] += sub * (subGain - 1.0f);
                    }
                    once (subGain < 0.5f, 11, suite::Autopilot::subTailsCleared);
                }
                if (ctx.role == 6) vocalCare (x);
                if (ctx.role == 2 && harmAmt > 0.01f)
                {
                    const auto h = harmBp.bandpass (std::tanh (4.0f * harmLp.lowpass (0.5f * (x[0] + x[1])))) * 0.2f * harmAmt;
                    x[0] += h; x[1] += h;
                }
                if ((ctx.role == 4 || ctx.role == 5 || ctx.role == 7) && use (suite::Autopilot::sootheF)) soothe (x);
            }

            // Envelopes for attacks, hits and the noise floor
            const auto a = 0.5f * (std::abs (x[0]) + std::abs (x[1]));
            const auto f = fastEnv.process (a), sl = slowEnv.process (a);
            const auto attack = f > 2.0f * sl && f > 1.0e-3f;

            float gain = 1.0f;
            if (mix)
            {
                // Transients landing on the kick
                kickMsNow += ctx.msPerSample;
                const auto clashTarget = use (suite::Autopilot::kickTransientsF) && (ctx.role == 3 || ctx.role == 4 || ctx.role == 5 || ctx.role == 7) && kickMsNow < 25.0f && attack ? 0.708f : 1.0f;
                clashGain += (clashTarget < clashGain ? a1ms : a40ms) * (clashTarget - clashGain);
                if (clashTarget >= 1.0f && clashGain > 0.9999f) clashGain = 1.0f;
                once (clashGain < 0.85f, 4, suite::Autopilot::transientsSoftened);
                gain *= clashGain;
                if ((ctx.role == 1 || ctx.role == 3) && use (suite::Autopilot::drumHitsF)) gain *= hits (attack, a);
                if (ctx.role != 8 && use (suite::Autopilot::noiseFloorF)) gain *= floorGain (x);
            }
            l = x[0] * gain;
            r = x[1] * gain;
        }

        /** Block end: tell the others what this track plays. */
        void endBlock()
        {
            for (int b = 0; b < analysis::numBands; ++b) bandsDb[(size_t) b] = (float) (10.0 * std::log10 (bandPow[(size_t) b] + 1.0e-12));
            if (ctx.role > 0 && ctx.id != 0) suite::Autopilot::postLive (ctx.id, ctx.role, bandsDb.data(), ctx.rank);
        }

        void forget (uint32_t id) { suite::Autopilot::withdrawLive (id); }

        // Readouts (tests, UI)
        std::atomic<float> maskNow { 0.0f }, liftNow { 0.0f }, lowOwnerNow { 0.0f }, harmonicsNow { 0.0f }, noteGainDb { 0.0f }, sootheNow { 0.0f },
                           hitNowDb { 0.0f }, floorNowDb { 0.0f }, sootheDeepest { 0.0f };
        const std::array<float, analysis::numBands>& bands() const { return bandsDb; }

    private:
        void once (bool on, int which, suite::Autopilot::Counter counter)
        {
            if (on && ! counted[(size_t) which]) { counted[(size_t) which] = true; suite::Autopilot::count (counter); }
            if (! on) counted[(size_t) which] = false;
        }

        // Even bass notes: pitch from zero crossings of the low end, a level per note
        void bassNotes (float in) noexcept
        {
            const auto lp = pitchLp.lowpass (in);
            bassPow += 0.001 * ((double) lp * lp - bassPow);
            ++sinceCross;
            const auto sign = lastSign ? lp > -0.002f : lp > 0.002f;
            if (sign && ! lastSign)
            {
                const auto hz = fs / (float) juce::jmax (1, sinceCross);
                sinceCross = 0;
                const auto note = hz > 25.0f && hz < 300.0f ? (int) std::lround (69.0f + 12.0f * std::log2 (hz / 440.0f)) : -1;
                stableCount = note >= 0 && note == lastNote ? stableCount + 1 : 0;
                lastNote = note;
                const auto levelDb = (float) (10.0 * std::log10 (bassPow + 1.0e-12));
                if (note >= 0 && note < 128 && stableCount >= 3 && levelDb > -50.0f)
                {
                    auto& d = noteDb[(size_t) note];
                    d = noteSeen[(size_t) note] == 0 ? levelDb : d + 0.05f * (levelDb - d);
                    noteSeen[(size_t) note] = juce::jmin (noteSeen[(size_t) note] + 1, 100000);
                    float sum = 0.0f; int known = 0;
                    for (int k = 0; k < 128; ++k) if (noteSeen[(size_t) k] >= 20) { sum += noteDb[(size_t) k]; ++known; }
                    // Evens towards the average of the notes heard, by the note's level as learned (not its envelope now)
                    const auto targetDb = known >= 2 && noteSeen[(size_t) note] >= 20 ? juce::jlimit (-4.0f, 3.0f, 0.7f * (sum / (float) known - d)) : 0.0f;
                    noteTarget = juce::Decibels::decibelsToGain (targetDb);
                    noteGainDb.store (targetDb);
                    once (std::abs (targetDb) > 1.0f, 5, suite::Autopilot::bassNotesEvened);
                }
            }
            lastSign = sign;
            noteGain += a20ms * (noteTarget - noteGain);
        }

        // Moving resonances: a third-octave band far over its neighbours, in a sound with a spread
        // (a pure tone is left alone), dips while it rings
        void soothe (float* x) noexcept
        {
            const auto mono = 0.5f * (x[0] + x[1]);
            totalPow += sootheCoeff * ((double) mono * mono - totalPow);
            std::array<double, numSoothe> p {};
            for (int k = 0; k < numSoothe; ++k)
            {
                const auto y = sootheDet[(size_t) k].bandpass (mono) / 4.3f;
                sootheP[(size_t) k] += sootheCoeff * ((double) y * y - sootheP[(size_t) k]);
                p[(size_t) k] = sootheP[(size_t) k];
            }
            float deepest = 0.0f;
            for (int k = 2; k < numSoothe - 2; ++k)
            {
                float target = 1.0f;
                const auto pk = p[(size_t) k];
                // A band standing out from the bands two thirds of an octave either side by more than it
                // usually does (its own long average): a resonance ringing as a note passes through. A
                // steady sound - a held note, a chord - is the music, and is left alone.
                const auto tonal = (p[(size_t) k - 1] + pk + p[(size_t) k + 1]) / (1.4 * totalPow + 1.0e-14);
                if (pk > 1.0e-8 && tonal < 0.5)
                {
                    const auto excess = (float) (10.0 * std::log10 (pk / (0.5 * (p[(size_t) k - 2] + p[(size_t) k + 2]) + 1.0e-14)));
                    auto& usual = sootheUsual[(size_t) k];
                    if (! sootheSeeded[(size_t) k]) { usual = excess; sootheSeeded[(size_t) k] = true; }
                    usual += sootheSlow * (excess - usual);
                    const auto over = excess - juce::jmax (usual, 0.0f) - 3.0f;
                    if (excess > 5.0f && over > 0.0f) target = juce::Decibels::decibelsToGain (-juce::jmin (4.0f, 0.5f * over));
                }
                auto& g = sootheGain[(size_t) k];
                g += (target < g ? a5ms : a80ms) * (target - g);
                if (g < 0.995f)
                    for (int c = 0; c < 2; ++c) x[c] -= sootheApply[(size_t) k][(size_t) c].bandpass (x[c]) / 4.3f * (1.0f - g);
                else
                    for (int c = 0; c < 2; ++c) sootheApply[(size_t) k][(size_t) c].bandpass (x[c]);
                deepest = juce::jmin (deepest, juce::Decibels::gainToDecibels (g));
            }
            sootheNow.store (deepest);
            if (deepest < sootheDeepest.load()) sootheDeepest.store (deepest);
            once (deepest < -1.5f, 6, suite::Autopilot::resonancesSoothed);
        }

        // The kick in key: each hit's body (under 150 Hz) is read back from the hit's start at the
        // tuning ratio (slower = lower), the click above it untouched; between hits, 5 ms crossfades
        void tuneKick (float* x) noexcept
        {
            const auto mono = 0.5f * (x[0] + x[1]);
            const auto body = tuneLp.lowpass (mono);
            const auto fast = tuneFast.process (std::abs (body)), slow = tuneSlow.process (std::abs (body));
            ++tuneSince;
            const auto size = (int) tuneBuf[0].size();
            float lows[2];
            for (int c = 0; c < 2; ++c) { lows[c] = tuneSplit[(size_t) c].lowpass (x[c]); tuneBuf[(size_t) c][(size_t) tuneWrite] = lows[c]; }
            if (fast > 2.0f * slow && fast > 0.01f && tuneSince > (int) (0.1f * fs))
            {
                tuneSince = 0; tuneOnsetAt = tuneWrite; tuneRead = 0.0; tuneFade = (int) (0.005f * fs);
                tuneCrossings = 0; tunePeriodSum = 0.0; tunePeriods = 0; tuneZc = 0;
            }
            // Measure the body's pitch 15-80 ms into the hit
            if (tuneSince > (int) (0.015f * fs) && tuneSince < (int) (0.08f * fs))
            {
                ++tuneZc;
                const auto sign = tuneLastSign ? body > -0.001f : body > 0.001f;
                if (sign && ! tuneLastSign)
                {
                    if (tuneCrossings > 0) { tunePeriodSum += tuneZc; ++tunePeriods; }
                    ++tuneCrossings; tuneZc = 0;
                }
                tuneLastSign = sign;
            }
            else if (tuneSince == (int) (0.08f * fs) && tunePeriods >= 2)
            {
                const auto hz = (float) (fs * tunePeriods / tunePeriodSum);
                if (hz > 30.0f && hz < 150.0f) tuneBodyHz = tuneBodyHz <= 0.0f ? hz : tuneBodyHz + 0.3f * (hz - tuneBodyHz);
                kickBodyNow.store (tuneBodyHz);
            }
            if (tuneRatio < 0.999 && tuneOnsetAt >= 0 && tuneSince < size - 8)
            {
                const auto pos = tuneOnsetAt + tuneRead;
                const auto i0 = (int) std::floor (pos), i1 = i0 + 1;
                const auto frac = (float) (pos - i0);
                for (int c = 0; c < 2; ++c)
                {
                    const auto& b = tuneBuf[(size_t) c];
                    const auto shifted = b[(size_t) (i0 % size)] * (1.0f - frac) + b[(size_t) (i1 % size)] * frac;
                    const auto mixIn = tuneFade > 0 ? (float) tuneFade / (0.005f * fs) : 0.0f;   // fade from the old hit's read into this one
                    x[c] += (shifted - lows[c]) * (1.0f - mixIn);
                }
                tuneRead += tuneRatio;
            }
            if (tuneFade > 0) --tuneFade;
            tuneWrite = (tuneWrite + 1) % size;
        }

        // Click guard: out of 2 ms of digital silence straight into a loud sample: a 1 ms fade-in
        void clickGuard (float* x) noexcept
        {
            const auto level = juce::jmax (std::abs (x[0]), std::abs (x[1]));
            // A clip that stops dead: instead of the step to silence, a 1 ms tail from where it was.
            // Only true digital silence counts: a high note passes close to zero every few samples
            // (an 8 kHz sine every 3 at 48 kHz), and those near-zeros aren't a clip ending.
            if (level == 0.0f)
            {
                if (tailLeft == 0 && juce::jmax (std::abs (lastOut[0]), std::abs (lastOut[1])) > 0.05f)
                {
                    tailLeft = fadeLen; tailVal = lastOut;
                    suite::Autopilot::count (suite::Autopilot::clicksGuarded);
                }
                if (tailLeft > 0)
                {
                    const auto g = (float) tailLeft / (float) fadeLen;
                    x[0] = tailVal[0] * g; x[1] = tailVal[1] * g;
                    --tailLeft;
                }
                lastOut = { x[0], x[1] };
                ++quietCount;
                return;
            }
            tailLeft = 0;
            if (quietCount > (int) (0.002f * fs) && level > 0.05f && fadeLeft == 0)
            {
                fadeLeft = fadeLen;
                suite::Autopilot::count (suite::Autopilot::clicksGuarded);
            }
            quietCount = 0;
            if (fadeLeft > 0)
            {
                const auto g = 1.0f - (float) fadeLeft / (float) fadeLen;
                x[0] *= g; x[1] *= g;
                --fadeLeft;
            }
            lastOut = { x[0], x[1] };
        }

        // Hum: in the track's quiet moments (-40 dB and below, but not silence), a 50 or 60 Hz line
        // holding most of the energy for a second in all is mains hum; it and 3 harmonics are notched
        void hum (float* x) noexcept
        {
            const auto mono = 0.5f * (x[0] + x[1]);
            humTotal += 0.001 * ((double) mono * mono - humTotal);
            for (int h = 0; h < 2; ++h) { const auto y = humDet[(size_t) h].bandpass (mono) / 25.0f; humPow[(size_t) h] += 0.001 * ((double) y * y - humPow[(size_t) h]); }
            if (humHz <= 0.0f && humTotal > 1.0e-10 && humTotal < 1.0e-4)
            {
                const auto h = humPow[0] > humPow[1] ? 0 : 1;
                if (humPow[(size_t) h] > 0.5 * humTotal) humScore += 1.0f / fs;
                if (humScore > 1.0f)
                {
                    humHz = h == 0 ? 50.0f : 60.0f;
                    suite::Autopilot::count (suite::Autopilot::humRemoved);
                }
            }
            if (humSet > 0.0f)
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < numHum; ++k) x[c] = humNotch[(size_t) k][(size_t) c].process (x[c]);
            humNow.store (humHz);
        }

        // Left/right: re-centre a track that leans one way (slowly; not a wide, uncorrelated sound)
        void balance (float* x) noexcept
        {
            balL += balCoeff * ((double) x[0] * x[0] - balL);
            balR += balCoeff * ((double) x[1] * x[1] - balR);
            balLR += balCoeff * ((double) x[0] * x[1] - balLR);
            std::array<float, 2> target { 1.0f, 1.0f };
            if (balL > 1.0e-7 && balR > 1.0e-7 && balLR / std::sqrt (balL * balR) > 0.3)
            {
                const auto lean = (float) (10.0 * std::log10 (balL / balR));   // + = left louder
                if (std::abs (lean) > 1.5f)
                {
                    const auto fix = juce::jlimit (-3.0f, 3.0f, -0.5f * lean);
                    target = { juce::Decibels::decibelsToGain (0.5f * fix), juce::Decibels::decibelsToGain (-0.5f * fix) };
                }
            }
            for (int c = 0; c < 2; ++c)
            {
                balGain[(size_t) c] += (float) balCoeff * 4.0f * (target[(size_t) c] - balGain[(size_t) c]);
                x[c] *= balGain[(size_t) c];
            }
            balanceNow.store (juce::Decibels::gainToDecibels (balGain[0] / balGain[1]));
            once (std::abs (balanceNow.load()) > 1.0f, 12, suite::Autopilot::balanceCentred);
        }

        // The vocal: plosive pops and breaths
        void vocalCare (float* x) noexcept
        {
            const auto mono = 0.5f * (x[0] + x[1]);
            const auto low = popDet.lowpass (mono);
            const auto lf = popFast.process (std::abs (low)), ls = popSlow.process (std::abs (low));
            const auto broad = std::abs (mono);
            if (use (suite::Autopilot::vocalPopsF) && lf > 3.0f * ls && lf > 0.05f && std::abs (low) > 0.6f * broad + 1.0e-4f && popHold == 0)
            {
                popHold = (int) (0.03f * fs);
                suite::Autopilot::count (suite::Autopilot::plosivesCut);
            }
            const auto popTarget = popHold > 0 ? 1.0f : 0.0f;
            if (popHold > 0) --popHold;
            popAmt += (popTarget > popAmt ? a1ms : a20ms) * (popTarget - popAmt);
            for (int c = 0; c < 2; ++c)
            {
                const auto hp = popSplit[(size_t) c].highpass (x[c]);   // (always run: the filter stays warm)
                if (popAmt > 0.001f) x[c] += (hp - x[c]) * popAmt;
            }
            popNow.store (popAmt);
            // Breaths: well under the phrases (20-50 dB), and mostly air (over 3 kHz)
            const auto p2 = (double) mono * mono;
            vocFast += fastCoeff * (p2 - vocFast);
            vocPeak = vocFast > vocPeak ? vocFast : vocPeak + peakRelease * (vocFast - vocPeak);
            const auto hf = breathHp.highpass (mono);
            vocHf += fastCoeff * ((double) hf * hf - vocHf);
            const auto breath = use (suite::Autopilot::vocalBreathsF) && vocPeak > 1.0e-5 && vocFast < vocPeak * 0.01 && vocFast > vocPeak * 1.0e-5 && vocHf > 0.4 * vocFast;
            const auto bTarget = breath ? 0.5f : 1.0f;
            breathGain += (bTarget < breathGain ? a20ms : a40ms) * (bTarget - breathGain);
            x[0] *= breathGain; x[1] *= breathGain;
            breathNow.store (breathGain);
            once (breathGain < 0.7f, 13, suite::Autopilot::breathsLowered);
        }

        // Even drum hits: a hit 3 dB or more over the recent ones is brought most of the way back
        float hits (bool attack, float a) noexcept
        {
            if (hitHold > 0) --hitHold;
            if (attack && ! inHit && hitHold == 0) { inHit = true; hitMeasure = (int) (0.003f * fs); hitPeak = 0.0f; }
            if (inHit)
            {
                hitPeak = juce::jmax (hitPeak, a);
                if (--hitMeasure <= 0)
                {
                    inHit = false;
                    hitHold = (int) (0.06f * fs);
                    const auto db = juce::Decibels::gainToDecibels (hitPeak, -100.0f);
                    float cut = 0.0f;
                    if (hitsSeen >= 4 && db > hitAvgDb + 3.0f) cut = juce::jmin (6.0f, 0.7f * (db - hitAvgDb - 1.0f));
                    hitAvgDb = hitsSeen == 0 ? db : hitAvgDb + 0.15f * (juce::jmin (db, hitAvgDb + 3.0f) - hitAvgDb);
                    ++hitsSeen;
                    hitTarget = juce::Decibels::decibelsToGain (-cut);
                    hitNowDb.store (-cut);
                    if (cut > 1.0f) suite::Autopilot::count (suite::Autopilot::hitsEvened);
                }
            }
            if (hitHold == 0 && ! inHit) hitTarget = 1.0f;
            hitGain += (hitTarget < hitGain ? a1ms : a40ms) * (hitTarget - hitGain);
            if (hitTarget >= 1.0f && hitGain > 0.9999f) hitGain = 1.0f;
            return hitGain;
        }

        // The noise floor: under -60 dBFS, 1:2 downward expansion (20 dB at most), after 100 ms
        float floorGain (const float* x) noexcept
        {
            expPow += expCoeff * (0.5 * ((double) x[0] * x[0] + (double) x[1] * x[1]) - expPow);
            const auto db = (float) (10.0 * std::log10 (expPow + 1.0e-14));
            float target = 1.0f;
            if (db > -60.0f) { expHold = (int) (0.1f * fs); wasLoud = true; }
            else if (expHold > 0) --expHold;
            else target = juce::Decibels::decibelsToGain (juce::jmax (-20.0f, db + 60.0f));
            expGain += (target < expGain ? a200ms : a5ms) * (target - expGain);
            if (target >= 1.0f && expGain > 0.9999f) expGain = 1.0f;   // fully open: exactly untouched
            floorNowDb.store (juce::Decibels::gainToDecibels (expGain));
            const auto quiet = expGain < 0.7f;
            if (quiet && wasLoud && ! counted[7]) { counted[7] = true; suite::Autopilot::count (suite::Autopilot::tailsQuietened); }
            if (! quiet) counted[7] = false;
            if (quiet) wasLoud = false;
            return expGain;
        }

        static constexpr int numSoothe = 10;   // 1 kHz to 8 kHz in thirds of an octave
        float fs = 48000.0f;
        Context ctx;
        std::array<suite::Svf, analysis::numBands> analysers;
        std::array<double, analysis::numBands> bandPow {};
        std::array<float, analysis::numBands> bandsDb {};
        double powCoeff = 0.001;
        std::array<std::array<suite::Biquad, 2>, 2> maskEq;
        std::array<float, 2> maskGain {};
        std::array<std::array<float, 3>, 2> maskSet {};   // hz, gain set, q
        std::array<suite::Biquad, 2> liftEq;
        float liftGain = 0.0f, liftSet = 0.0f;
        std::array<suite::Svf, 2> lowSplit;
        float lowMix = 0.0f;
        suite::Svf sideLow;
        std::array<suite::Svf, 2> bassLp;
        suite::Svf pitchLp;
        std::array<float, 128> noteDb {};
        std::array<int, 128> noteSeen {};
        float noteGain = 1.0f, noteTarget = 1.0f;
        bool lastSign = false;
        int sinceCross = 0, periods = 0, lastNote = -1, stableCount = 0;
        double bassPow = 0.0;
        suite::Svf harmLp, harmBp;
        bool harmOn = false;
        float harmAmt = 0.0f, fundSlow = -100.0f, upperSlow = -100.0f;
        std::array<suite::Svf, numSoothe> sootheDet;
        std::array<std::array<suite::Svf, 2>, numSoothe> sootheApply;
        std::array<suite::EnvelopeFollower, numSoothe> sootheEnv;
        std::array<double, numSoothe> sootheP {};
        std::array<float, numSoothe> sootheUsual {};
        std::array<bool, numSoothe> sootheSeeded {};
        float sootheSlow = 1.0e-5f;
        double totalPow = 0.0, sootheCoeff = 0.001;
        std::array<float, numSoothe> sootheGain {};
        suite::EnvelopeFollower fastEnv, slowEnv;
        float clashGain = 1.0f, hitGain = 1.0f, expGain = 1.0f, kickMsNow = 1.0e9f;
        int hitHold = 0, hitMeasure = 0, hitsSeen = 0;
        float hitPeak = 0.0f, hitAvgDb = -100.0f, hitTarget = 1.0f;
        bool inHit = false;
        double expPow = 0.0, expCoeff = 0.001;
        int expHold = 0;
        bool wasLoud = false;
        float a1ms = 0.0f, a5ms = 0.0f, a20ms = 0.0f, a40ms = 0.0f, a80ms = 0.0f, a200ms = 0.0f;
        std::array<bool, 16> counted {};
        std::array<bool, suite::Autopilot::numFeatures> useF {};
        float monoHz = 120.0f;
    public:
        float monoLowsHz() const noexcept { return monoHz; }
    private:
        bool use (suite::Autopilot::Feature f) const noexcept { return useF[(size_t) f]; }
        std::array<suite::Svf, 2> subSplit, popSplit;
        float subGain = 1.0f, kickNextNow = 1.0e9f;
        suite::Svf popDet, breathHp;
        suite::EnvelopeFollower popFast, popSlow;
        int popHold = 0, quietCount = 0, fadeLeft = 0, fadeLen = 48;
        float popAmt = 0.0f, breathGain = 1.0f;
        double vocPeak = 0.0, vocFast = 0.0, vocHf = 0.0, balL = 0.0, balR = 0.0, balLR = 0.0, balCoeff = 1.0e-5, fastCoeff = 0.001, peakRelease = 1.0e-5;
        std::array<float, 2> balGain { 1.0f, 1.0f };
        static constexpr int numHum = 4;
        std::array<suite::Svf, 2> humDet;
        suite::Svf humBroad;
        std::array<double, 2> humPow {};
        double humBroadPow = 0.0, humTotal = 0.0;
        float humScore = 0.0f, humHz = 0.0f, humSet = 0.0f;
        std::array<std::array<suite::Biquad, 2>, numHum> humNotch;
        std::array<float, 2> lastOut {}, tailVal {};
        int tailLeft = 0;
        std::array<suite::DelayLine, 2> alignLine;
        float alignSamples = 0.0f, alignTarget = 0.0f;
        int sampleInBlock = 0, alignCheck = 0;
        double lastOnsetPpq = -1.0e9;
        suite::EnvelopeFollower onsetFast, onsetSlow;
        std::array<suite::Svf, 2> kickDip;
        float kickDipHz = 0.0f, panNow = 0.0f;
        std::array<suite::Svf, 2> tuneSplit;
        suite::Svf tuneLp;
        std::array<std::vector<float>, 2> tuneBuf;
        int tuneWrite = 0, tuneOnsetAt = -1, tuneSince = 1 << 30, tuneFade = 0, tuneZc = 0, tuneCrossings = 0, tunePeriods = 0;
        double tuneRead = 0.0, tuneRatio = 1.0, tunePeriodSum = 0.0;
        bool tuneLastSign = false;
        float tuneBodyHz = 0.0f;
        suite::EnvelopeFollower tuneFast, tuneSlow;
    public:
        std::atomic<float> spreadNow { 0.0f }, tuneRatioNow { 1.0f }, kickBodyNow { 0.0f }, balanceNow { 0.0f }, popNow { 0.0f }, breathNow { 1.0f },
                           humNow { 0.0f }, alignNowMs { 0.0f };
    };
}
