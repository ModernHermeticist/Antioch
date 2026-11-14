#pragma once

// Quintessence's mix fixes, the fourth set (each with its autopilot switch, latched per track by
// MixLatch where Ouroboros' checks ask and it applies):
// Peaks: lopsided kick and bass waveforms evened by phase rotation (an all-pass: no compression).
// Level and arrangement: drop 2 brighter and wider, the outro thinning at its end, no drop-out into
// the breakdown, backing vocals under the lead vocal, the lead's sustain shorter in busy drops,
// pads swelling in breakdowns, ghost notes evened, the fill's last beat lifted into the drop, the
// bass cut for the last 16th before it (never a full silence), new parts eased in, pad drive
// rising through builds, FX tails tucked after the impact, repeated sections not identical, the
// vocal or the lead leading (not both), the impact's level from the drop's energy, sweeps peaking
// at the drop, FX dipping under the hook, downlifter tails gated at the next section.
// Tone: the vocal's proximity boom, stacked pads' boxiness, cymbal wash after each hit, tonal
// percussion nudged into the key, the low-mid boom in drops, more vocal air in breakdowns, more
// bass harmonics in breakdowns, the bass's sub kept off the kick's body.
// Dynamics: the sub held steady note to note.
// Stereo: left/right balance per band, left/right delays realigned (mono combing), nothing panned
// past 70%, the riser widening into the drop, the mono cutoff per section, the vocal doubled on
// held notes.
// (Each drop's own tone and the kick's tail following the tempo live in LabelDsp2; the drum bus
// opening for fills in LabelDsp3: their switches are here so they latch like the rest.)

#include "LabelDsp3.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace qn
{
    class LabelDsp4
    {
    public:
        enum Fix
        {
            phaseRotate, drop2Bright, outroThin, breakdownEntry, backingVocals, vocalProximity, boxiness, cymbalWash, percKey, bandBalance,
            laterDropTone, lowMidDrops, subPerNote, fillOpen, leadEnvelope, padSwell, kickTailTempo, ghostEven, haas, panLimit, widthDrop2,
            riserWiden, sideLowsSection, pickupLift, bassChoke, introEase, buildTension, postImpact, repeatTone, vocalVsLead, vocalHeldDouble,
            vocalAirSection, subVsKick, bassHarmonicsSection, downlifterGate, impactEnergy, sweepToDrop, fxUnderHook, numFixes
        };
        std::array<std::atomic<bool>, numFixes> on {};
        // set by the message thread
        std::atomic<float> backingOffsetDb { 0.0f };   // a backing vocal: how far under the lead vocal it should come (dB, <= 0)
        std::atomic<int> keyRoot { -1 };               // the song's key (for tonal percussion)
        std::atomic<uint32_t> scaleMask { 0 };         // pitch classes in the key (bit per semitone from C)
        // measured here, for the traits and the checks
        std::atomic<float> haasMs { 0.0f };

        struct Extra
        {
            int nextType = -1;
            float kickBodyHz = 0.0f;   // the kick's body pitch (Kybalion), 0 = unknown
            float pitchHz = 0.0f;      // this track's note now (LabelDsp3's estimate)
        };

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (int c = 0; c < 2; ++c)
            {
                allpassA[(size_t) c].set (150.0f, 0.7f, fs); allpassB[(size_t) c].set (400.0f, 0.7f, fs);
                shelf[(size_t) c].set (6000.0f, 0.707f, fs); airShelf[(size_t) c].set (10000.0f, 0.707f, fs);
                cymbalHp[(size_t) c].set (6000.0f, 0.707f, fs); lowMid[(size_t) c].set (150.0f, 1.2f, fs);
                subLp[(size_t) c].set (80.0f, 0.707f, fs); harmLp[(size_t) c].set (120.0f, 0.707f, fs); harmBp[(size_t) c].set (400.0f, 0.7f, fs);
                kickNotch[(size_t) c].set (55.0f, 6.0f, fs); hookBand[(size_t) c].set (2000.0f, 0.7f, fs);
                midBand[(size_t) c].set (1000.0f, 0.4f, fs); topBand[(size_t) c].set (7000.0f, 0.707f, fs);
            }
            proxBp.set (200.0f, 1.2f, fs); boxBp.set (450.0f, 1.0f, fs); sideLow.set (120.0f, 0.707f, fs); doubleHp.set (400.0f, 0.707f, fs);
            percDip.set (440.0f, 6.0f, fs); percLift.set (440.0f, 6.0f, fs); pitchLp.set (900.0f, 0.707f, fs);
            delayLine.assign (2 * ((size_t) (0.035 * sampleRate) + 8), 0.0f); dPos = 0; doubleLine.assign ((size_t) (0.03 * sampleRate) + 8, 0.0f); dPos2 = 0;
            haasL.assign (ringLen, 0.0f); haasR.assign (ringLen, 0.0f); haasPos = 0; decim = juce::jmax (1, (int) std::round (fs / 11025.0f));
            gain = 1.0f; envFast = envSlow = 0.0f; sinceHit = 1 << 30; silentFor = 0; easeLeft = 0; subEnv = subAvg = 0.0f;
            proxEnv = allEnv = boxEnv = 0.0f; hitPeak = 0.0f; hitMedian = 0.0f; ghostGain = 1.0f; tail = 1.0f;
            balPow = {}; lrPowL = lrPowR = 0.0; delayL = delayR = 0; heldFor = 0; lastPitch = 0.0f;
            percHzNow = 0.0f; zc = 0; zcWin = 0; lastSign = false;
        }

        /** After LabelDsp3. `budgetLeftDb`: the level budget left for this set (dB), `liftsAllowed` 0..1. */
        void process (float* const* ch, int chs, int n, const LabelDsp3::Context& c, const Extra& x, float budgetLeftDb = 6.0f) noexcept
        {
            const auto role = c.role, t = c.sectionType;
            const auto inDrop = t == 4, inBreak = t == 2, inBuild = t == 3;
            const auto beatQ = 1.0, barQ = juce::jmax (1.0, c.barQ);
            const auto inSection = c.ppq - c.sectionStartQ, toEnd = c.sectionEndQ - c.ppq;
            const auto qps = c.bpm / 60.0 / (double) fs;
            const auto msPerQ = 60000.0 / juce::jmax (1.0, c.bpm);
            // block-level gain (dB)
            float db = 0.0f;
            auto is = [this] (Fix f) { return on[(size_t) f].load(); };
            if (is (outroThin) && t == 5 && c.playing && toEnd < 16.0 * barQ && (role == 4 || role == 5 || role == 7 || (role == 3 && toEnd < 8.0 * barQ)))
                db -= 2.0f * (float) juce::jlimit (0.0, 1.0, 1.0 - toEnd / (16.0 * barQ));
            if (is (breakdownEntry) && inBreak && inSection >= 0.0 && inSection < barQ && (role == 4 || role == 5 || role == 6)) db += 1.5f * (float) (1.0 - inSection / barQ);
            if (is (backingVocals) && role == 6) db += juce::jlimit (-9.0f, 0.0f, backingOffsetDb.load());
            if (is (pickupLift) && (role == 1 || role == 3) && inBuild && c.nextIsDrop && toEnd < beatQ && toEnd > 0.25) db += 1.5f;
            if (is (postImpact) && role == 7 && inDrop && inSection >= beatQ && inSection < 3.0 * beatQ) db -= 3.0f;
            if (is (vocalVsLead) && role == 6 && inDrop && c.leadActivity > 0.3f) db -= 1.5f;     // the hook: the lead leads in the drops
            if (is (vocalVsLead) && role == 5 && ! inDrop && c.vocalActivity > 0.3f) db -= 1.5f;  // elsewhere the vocal does
            if (is (impactEnergy) && role == 7 && inDrop && inSection >= 0.0 && inSection < beatQ) db += juce::jlimit (-3.0f, 3.0f, 6.0f * (std::sqrt (juce::jmax (0.1f, c.energyRatio)) - 1.0f));
            if (is (sweepToDrop) && role == 7 && inBuild && c.nextIsDrop) db += 2.0f * c.buildProgress * c.buildProgress - 1.0f;
            db *= c.strength;
            if (db > 0.0f) db *= c.liftsAllowed;
            const auto unclamped = db;
            db = juce::jlimit (-juce::jmax (0.0f, budgetLeftDb), juce::jmax (0.0f, juce::jmin (3.0f, budgetLeftDb)), db);
            budgetHit = std::abs (db - unclamped) > 0.05f;
            lastDb = db;
            const auto target = juce::Decibels::decibelsToGain (db);
            // per-block decisions
            const auto bassChokeNow = is (bassChoke) && role == 2 && c.nextIsDrop && c.playing && toEnd > 0.0 && toEnd < 0.25;
            const auto riser = is (riserWiden) && role == 7 ? (inBuild ? 1.0f + c.buildProgress : inDrop ? 1.0f - 0.8f * c.dropHit : 1.0f) : 1.0f;
            const auto width2 = is (widthDrop2) && (role == 4 || role == 5) && inDrop && c.occurrence >= 1 ? 1.1f : 1.0f;
            const auto outroSide = is (outroThin) && t == 5 && toEnd < 16.0 * barQ ? 1.0f - 0.5f * (float) juce::jlimit (0.0, 1.0, 1.0 - toEnd / (16.0 * barQ)) : 1.0f;
            const auto sideCutHz = inDrop ? 150.0f : inBreak ? 80.0f : 120.0f;
            if (is (sideLowsSection) && std::abs (sideCutHz - sideHz) > 1.0f) { sideHz = sideCutHz; sideLow.set (sideHz, 0.707f, fs); }
            float topShelfDb = 0.0f;
            if (is (drop2Bright) && inDrop && c.occurrence >= 1 && role != 1 && role != 2) topShelfDb += 0.5f;
            if (is (repeatTone) && (t == 1 || t == 2) && c.occurrence >= 1) topShelfDb += (c.occurrence % 2 == 1) ? 0.5f : -0.5f;
            if (is (backingVocals) && role == 6 && backingOffsetDb.load() < -0.5f) topShelfDb -= 2.0f;
            const auto shelfK = juce::Decibels::decibelsToGain (topShelfDb * c.strength) - 1.0f;
            const auto airK = is (vocalAirSection) && role == 6 && inBreak ? (juce::Decibels::decibelsToGain (1.5f * c.strength) - 1.0f) : 0.0f;
            const auto sideBacking = is (backingVocals) && role == 6 && backingOffsetDb.load() < -0.5f ? 1.2f : 1.0f;
            const auto subNote = is (subVsKick) && role == 2 && x.kickBodyHz > 30.0f && x.pitchHz > 20.0f && std::abs (x.pitchHz - x.kickBodyHz) < 6.0f;
            if (subNote && std::abs (x.kickBodyHz - notchHz) > 0.5f) { notchHz = x.kickBodyHz; for (auto& f : kickNotch) f.set (notchHz, 6.0f, fs); }
            // tonal percussion: the nearest note in the key
            const auto percOn = is (percKey) && role == 3 && keyRoot.load() >= 0 && percHzNow > 60.0f;
            if (percOn && (i0Pending || std::abs (percHzNow - percAt) > 2.0f))
            {
                i0Pending = false; percAt = percHzNow;
                const auto midi = 69.0f + 12.0f * std::log2 (percHzNow / 440.0f);
                const auto near = (int) std::lround (midi);
                int best = near; float bestD = 99.0f;
                for (int d = -2; d <= 2; ++d)
                {
                    const auto pc = ((near + d) % 12 + 12) % 12;
                    if ((scaleMask.load() & (1u << pc)) != 0 && std::abs ((float) (near + d) - midi) < bestD) { bestD = std::abs ((float) (near + d) - midi); best = near + d; }
                }
                percOffKey = std::abs ((float) best - midi) > 0.3f;
                percDip.set (percHzNow, 6.0f, fs);
                percLift.set (440.0f * std::pow (2.0f, ((float) best - 69.0f) / 12.0f), 6.0f, fs);
            }
            const auto kickBeat = [&c, msPerQ] (double q)
            {
                const auto phase = std::fmod (juce::jmax (0.0, q), c.barQ);
                if (c.kick != nullptr) { const auto ms = c.kick->msSinceHit (c.sectionType, phase, c.barQ, msPerQ); if (ms < 1.0e8f) return (double) ms; }
                return std::fmod (phase, 1.0) * msPerQ;
            };

            for (int i = 0; i < n; ++i)
            {
                const auto q = c.ppq + qps * i;
                float l = ch[0][i], r = chs > 1 ? ch[1][i] : l;
                gain += 0.002f * (target - gain);
                // the haas ring (decimated), for the left/right delay estimate
                if (++decimCount >= decim) { decimCount = 0; haasL[(size_t) haasPos] = l; haasR[(size_t) haasPos] = r; haasPos = (haasPos + 1) % ringLen; }
                // left/right realigned: the earlier side delayed by the measured lag
                if (is (haas) && (delayL > 0 || delayR > 0) && chs > 1)
                {
                    const auto size = (int) delayLine.size() / 2;
                    delayLine[(size_t) (2 * dPos)] = l; delayLine[(size_t) (2 * dPos + 1)] = r;
                    if (delayL > 0) l = delayLine[(size_t) (2 * (((dPos - delayL) % size + size) % size))];
                    if (delayR > 0) r = delayLine[(size_t) (2 * (((dPos - delayR) % size + size) % size) + 1)];
                    dPos = (dPos + 1) % size;
                }
                // phase rotation: two all-passes (lopsided peaks evened, nothing compressed)
                if (is (phaseRotate))
                {
                    l = l - 2.0f * allpassA[0].bandpassNorm (l); r = r - 2.0f * allpassA[1].bandpassNorm (r);
                    l = l - 2.0f * allpassB[0].bandpassNorm (l); r = r - 2.0f * allpassB[1].bandpassNorm (r);
                }
                const auto mono = 0.5f * (l + r);
                const auto a = 0.5f * (std::abs (l) + std::abs (r));
                envFast += 0.15f * (a - envFast); envSlow += 0.002f * (a - envSlow);
                const auto hit = envFast > 2.0f * envSlow && envFast > 1.0e-3f && sinceHit > (int) (0.06f * fs);
                if (hit) { sinceHit = 0; hitPeak = 0.0f; } else ++sinceHit;
                if (sinceHit < (int) (0.005f * fs)) hitPeak = juce::jmax (hitPeak, a);
                if (sinceHit == (int) (0.005f * fs) && hitPeak > 0.0f) hitMedian = hitMedian <= 0.0f ? hitPeak : hitMedian * std::pow (hitPeak / hitMedian, 0.05f);
                // new parts eased in: two bars of silence, then the first bar from -3 dB
                if (a < 1.0e-4f) ++silentFor;
                else
                {
                    if (is (introEase) && silentFor > (int) (2.0 * barQ * 60.0 / c.bpm * fs) && (t == 0 || t == 1 || t == 3)) easeLeft = (int) (barQ * 60.0 / c.bpm * fs);
                    silentFor = 0;
                }
                float g = gain;
                if (easeLeft > 0) { g *= juce::Decibels::decibelsToGain (-3.0f * (float) easeLeft / (float) juce::jmax (1.0, barQ * 60.0 / c.bpm * fs)); --easeLeft; }
                // envelopes on hits: the lead's sustain in busy drops, pads swelling in breakdowns, ghost notes evened
                {
                    float want = 1.0f;
                    if (is (leadEnvelope) && role == 5 && inDrop && c.energyRatio >= 1.0f && sinceHit > (int) (0.2f * fs)) want = 0.71f;
                    if (is (padSwell) && role == 4 && inBreak && sinceHit < (int) (0.15f * fs)) want = juce::Decibels::decibelsToGain (-4.0f * (1.0f - (float) sinceHit / (0.15f * fs)));
                    tail += (want < tail ? 0.0005f : 0.005f) * (want - tail);
                    g *= tail;
                    if (is (ghostEven) && role == 3 && hitMedian > 1.0e-4f && sinceHit < (int) (0.1f * fs) && hitPeak > 0.0f && hitPeak < 0.35f * hitMedian)
                        ghostGain += 0.01f * (juce::jmin (1.41f, 0.35f * hitMedian / hitPeak) - ghostGain);
                    else ghostGain += 0.002f * (1.0f - ghostGain);
                    g *= ghostGain;
                }
                if (bassChokeNow) g *= 0.1f;   // -20 dB: the bass steps out, the drop doesn't go silent
                // the sub held steady note to note (in the drops)
                if (is (subPerNote) && role == 2 && inDrop)
                {
                    const auto s1 = subLp[0].lowpass (l), s2 = subLp[1].lowpass (r);
                    const auto sa = 0.5f * (std::abs (s1) + std::abs (s2));
                    subEnv += 0.003f * (sa - subEnv); subAvg += 0.00002f * (sa - subAvg);
                    if (subEnv > 1.0e-4f && subAvg > 1.0e-4f)
                    {
                        const auto sg = juce::jlimit (0.71f, 1.41f, std::sqrt (subAvg / subEnv));
                        l += (sg - 1.0f) * s1; r += (sg - 1.0f) * s2;
                    }
                }
                // tone
                if (std::abs (shelfK) > 1.0e-4f) { l += shelfK * shelf[0].highpass (l); r += shelfK * shelf[1].highpass (r); }
                if (airK > 0.0f) { l += airK * airShelf[0].highpass (l); r += airK * airShelf[1].highpass (r); }
                if (is (vocalProximity) && role == 6)
                {
                    const auto pb = proxBp.bandpassNorm (mono);
                    proxEnv += 0.002f * (std::abs (pb) - proxEnv); allEnv += 0.002f * (a - allEnv);
                    const auto over = allEnv > 1.0e-5f ? juce::Decibels::gainToDecibels (proxEnv / allEnv) + 6.0f : -99.0f;
                    if (over > 0.0f) { const auto k = 1.0f - juce::Decibels::decibelsToGain (-juce::jmin (3.0f, over) * c.strength); l -= k * pb; r -= k * pb; }
                }
                if (is (boxiness) && role == 4)
                {
                    const auto bb = boxBp.bandpassNorm (mono);
                    boxEnv += 0.001f * (std::abs (bb) - boxEnv); allEnv += 0.001f * (a - allEnv);
                    const auto over = allEnv > 1.0e-5f ? juce::Decibels::gainToDecibels (boxEnv / allEnv) + 8.0f : -99.0f;
                    if (over > 0.0f) { const auto k = 1.0f - juce::Decibels::decibelsToGain (-juce::jmin (3.0f, over) * c.strength); l -= k * bb; r -= k * bb; }
                }
                if (is (cymbalWash) && role == 3 && inDrop && sinceHit > (int) (0.08f * fs)) { const auto k = 0.29f * c.strength; l -= k * cymbalHp[0].highpass (l); r -= k * cymbalHp[1].highpass (r); }
                if (is (lowMidDrops) && inDrop && (role == 2 || role == 4 || role == 5)) { const auto k = 0.16f * c.strength; l -= k * lowMid[0].bandpassNorm (l); r -= k * lowMid[1].bandpassNorm (r); }
                if (percOn && percOffKey) { const auto d = percDip.bandpassNorm (mono), u = percLift.bandpassNorm (mono); l += -0.29f * d + 0.19f * u; r += -0.29f * d + 0.19f * u; }
                if (is (bassHarmonicsSection) && role == 2 && inBreak)
                {
                    const auto h1 = harmBp[0].bandpassNorm (std::tanh (3.0f * harmLp[0].lowpass (l))), h2 = harmBp[1].bandpassNorm (std::tanh (3.0f * harmLp[1].lowpass (r)));
                    l += 0.15f * c.strength * h1; r += 0.15f * c.strength * h2;
                }
                if (subNote && kickBeat (q) < 100.0) { l -= 0.5f * kickNotch[0].bandpassNorm (l); r -= 0.5f * kickNotch[1].bandpassNorm (r); }
                if (is (fxUnderHook) && role == 7 && inDrop && c.leadActivity > 0.3f) { const auto k = 0.16f * c.strength; l -= k * hookBand[0].bandpassNorm (l); r -= k * hookBand[1].bandpassNorm (r); }
                if (is (buildTension) && role == 4 && inBuild)
                {
                    const auto amt = 0.3f * c.buildProgress * c.strength;
                    l = (1.0f - amt) * l + amt * std::tanh (1.8f * l) / 1.8f; r = (1.0f - amt) * r + amt * std::tanh (1.8f * r) / 1.8f;
                }
                // downlifter tails gated at a new section: no attack in its first 16th, the rest of its first beat at -12 dB
                if (is (downlifterGate) && role == 7 && c.playing && inSection >= 0.0)
                {
                    const auto inS = inSection + qps * i;
                    if (inS < 0.25 && hit) attackedHere = true;
                    if (inS < 0.01) attackedHere = false;
                    if (inS >= 0.25 && inS < 1.0 && ! attackedHere) g *= 0.25f;
                }
                // stereo
                if (chs > 1)
                {
                    auto m = 0.5f * (l + r), s = 0.5f * (l - r);
                    s *= riser * width2 * outroSide * sideBacking;
                    if (is (sideLowsSection) && role != 1 && role != 2) s -= sideLow.lowpass (s);
                    if (is (vocalHeldDouble) && role == 6 && inDrop && heldFor > (int) (0.3f * fs))
                    {
                        const auto size = (int) doubleLine.size();
                        doubleLine[(size_t) dPos2] = m;
                        const auto d = doubleLine[(size_t) (((dPos2 - (int) (0.012f * fs)) % size + size) % size)];
                        s += 0.2f * c.strength * doubleHp.highpass (d);
                    }
                    if (is (vocalHeldDouble)) dPos2 = (dPos2 + 1) % (int) doubleLine.size();
                    l = m + s; r = m - s;
                    // left/right balance per band (the mids and the top), and nothing panned past 70%
                    if (is (bandBalance) && role != 1 && role != 2)
                    {
                        float* sides[2] = { &l, &r };
                        for (int b = 0; b < 2; ++b)
                        {
                            auto& fl = b == 0 ? midBand : topBand;
                            const auto bl = b == 0 ? fl[0].bandpassNorm (l) : fl[0].highpass (l), br = b == 0 ? fl[1].bandpassNorm (r) : fl[1].highpass (r);
                            balPow[(size_t) b][0] += 0.0002 * ((double) bl * bl - balPow[(size_t) b][0]);
                            balPow[(size_t) b][1] += 0.0002 * ((double) br * br - balPow[(size_t) b][1]);
                            if (balPow[(size_t) b][0] > 1.0e-10 && balPow[(size_t) b][1] > 1.0e-10)
                            {
                                const auto lean = (float) (10.0 * std::log10 (balPow[(size_t) b][0] / balPow[(size_t) b][1]));
                                if (std::abs (lean) > 1.5f)
                                {
                                    const auto k = 1.0f - juce::Decibels::decibelsToGain (-juce::jmin (3.0f, 0.5f * std::abs (lean)) * c.strength);
                                    if (lean > 0.0f) *sides[0] -= k * bl; else *sides[1] -= k * br;
                                }
                            }
                        }
                    }
                    if (is (panLimit))
                    {
                        lrPowL += 0.0002 * ((double) l * l - lrPowL); lrPowR += 0.0002 * ((double) r * r - lrPowR);
                        if (lrPowL + lrPowR > 1.0e-10)   // (one side silent counts as fully panned)
                        {
                            const auto bal = (float) (10.0 * std::log10 ((lrPowL + 1.0e-14) / (lrPowR + 1.0e-14)));
                            if (std::abs (bal) > 10.0f)   // past about 70%: some of the loud side into the quiet one
                            {
                                const auto k = 0.25f * juce::jmin (1.0f, (std::abs (bal) - 10.0f) / 10.0f + 0.3f) * c.strength;
                                if (bal > 0.0f) r += k * l; else l += k * r;
                            }
                        }
                    }
                }
                l *= g; r *= g;
                // the note's steadiness (the vocal's held notes) and tonal percussion's pitch
                if (x.pitchHz > 0.0f)
                {
                    if (lastPitch > 0.0f && std::abs (std::log2 (x.pitchHz / lastPitch)) < 0.03f) ++heldFor; else heldFor = 0;
                    if ((i & 255) == 0) lastPitch = x.pitchHz;
                }
                if (role == 3)
                {
                    const auto p = pitchLp.lowpass (mono);
                    const auto sign = p > 0.0f;
                    if (sign != lastSign) { ++zc; lastSign = sign; }
                    if (++zcWin >= (int) (0.03f * fs))
                    {
                        if (envSlow > 1.0e-3f) { const auto hz = (float) zc * 0.5f / 0.03f; percHzNow = percHzNow <= 0.0f ? hz : percHzNow + 0.1f * (hz - percHzNow); }
                        zc = 0; zcWin = 0;
                    }
                }
                ch[0][i] = l;
                if (chs > 1) ch[1][i] = r;
            }
        }

        /** Message thread, every second or two: the delay between left and right (cross-correlation over +-30 ms). */
        void tick()
        {
            const auto rate = fs / (float) decim;
            const auto maxLag = juce::jmin (ringLen / 4, (int) (0.03f * rate));
            std::vector<float> L (haasL), R (haasR);
            double e0 = 0.0, eL = 0.0, eR = 0.0;
            for (int k = 0; k < ringLen; ++k) { e0 += (double) L[(size_t) k] * R[(size_t) k]; eL += (double) L[(size_t) k] * L[(size_t) k]; eR += (double) R[(size_t) k] * R[(size_t) k]; }
            if (eL < 1.0e-6 || eR < 1.0e-6) { haasMs.store (0.0f); return; }
            const auto norm = std::sqrt (eL * eR);
            const auto c0 = (float) (e0 / norm);
            float best = c0; int bestLag = 0;
            for (int lag = -maxLag; lag <= maxLag; ++lag)
            {
                if (lag == 0) continue;
                double acc = 0.0;
                for (int k = maxLag; k < ringLen - maxLag; ++k) acc += (double) L[(size_t) k] * R[(size_t) (k + lag)];
                const auto cc = (float) (acc / norm);
                if (cc > best) { best = cc; bestLag = lag; }
            }
            // a clear delay: the delayed copy matches much better than the two as they are
            const auto ms = best > c0 + 0.3f && std::abs (bestLag) >= 1 ? 1000.0f * (float) bestLag / rate : 0.0f;
            haasMs.store (ms);
            const auto samples = (int) std::lround (std::abs (ms) * 0.001f * fs);
            // R lags L (lag > 0): delay L to meet it; L lags R: delay R
            const auto limit = (int) delayLine.size() / 2 - 2;
            delayL = ms > 0.0f ? juce::jmin (limit, samples) : 0;
            delayR = ms < 0.0f ? juce::jmin (limit, samples) : 0;
        }

        float lastDb = 0.0f;
        bool budgetHit = false;

    private:
        static constexpr int ringLen = 4096;
        float fs = 44100.0f, gain = 1.0f, sideHz = 0.0f, notchHz = 0.0f, percAt = 0.0f;
        std::array<suite::Svf, 2> allpassA, allpassB, shelf, airShelf, cymbalHp, lowMid, subLp, harmLp, harmBp, kickNotch, hookBand, midBand, topBand;
        suite::Svf proxBp, boxBp, sideLow, doubleHp, percDip, percLift, pitchLp;
        std::vector<float> delayLine, doubleLine, haasL, haasR;
        int dPos = 0, dPos2 = 0, haasPos = 0, decim = 4, decimCount = 0, delayL = 0, delayR = 0;
        float envFast = 0.0f, envSlow = 0.0f, hitPeak = 0.0f, hitMedian = 0.0f, ghostGain = 1.0f, tail = 1.0f;
        int sinceHit = 1 << 30, silentFor = 0, easeLeft = 0, heldFor = 0;
        float subEnv = 0.0f, subAvg = 0.0f, proxEnv = 0.0f, allEnv = 0.0f, boxEnv = 0.0f, lastPitch = 0.0f;
        std::array<std::array<double, 2>, 2> balPow {};
        double lrPowL = 0.0, lrPowR = 0.0;
        bool attackedHere = false, percOffKey = false, i0Pending = true, lastSign = false;
        float percHzNow = 0.0f;
        int zc = 0, zcWin = 0;
    };
}
