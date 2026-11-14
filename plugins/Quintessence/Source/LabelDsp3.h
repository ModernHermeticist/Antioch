#pragma once

// Quintessence's label-ready mix fixes, the third set: everything a track can do for the mix on
// its own, each with its autopilot switch (the message thread decides which apply to this track:
// its role, its name, what it measures, what Ouroboros' checks asked for).
//
// Levels: phrase trims (Ouroboros, a step per 4 bars), the hook's entry lifted, high notes evened,
// an input leveller, the chain kept loudness-neutral, a dip under each drop's impact, room for the
// riser, a lift on the final drop, settling after the drop's first 8 bars, hat rides at phrase
// ends, a dip under fills, the riser's level from the section's energy.
// Tone: a low cut from the part's lowest note, backing parts' top rolled off under the lead, side-
// only air and side-only mud cut, matched low cuts on kick and bass, pads dipping at the lead's
// note, the arp under the hook, hats under the vocal's "s", the clap under the vocal, drums low-
// and high-cut in breakdowns, a high-pass that rises through each build, dips at the old chord's
// root at each chord change, darker intros and outros, the vocal's presence ridden, FX high-passed
// and their tails darkening.
// Dynamics: drum bus parallel compression, upward compression in breakdowns, glue per section on a
// bus, an expander on noisy parts, a fast peak stage on the vocal, pad sustain in drops, snare and
// clap tails in drops, the bass's lows ducking under each kick (only its lows).
// Stereo: a pan layout for centred percussion, mono-safe widening of mono pads, width split by
// band, a correlation guard, a pan offset (Ouroboros: two parts colliding).
// Colour: bus saturation in drops, an exciter for air, a click on a dull kick, drive on the bass's
// mids only, a doubler on the vocal in drops, air on the vocal.
// Safety: inter-sample peaks trimmed at the source, DC removed.

#include "SuiteDSP.h"
#include "KickBus.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace qn
{
    class LabelDsp3
    {
    public:
        enum Fix
        {
            phraseTrim, hookLift, noteEven, inputLeveller, chainNeutral, impactDuck, riserRoom, autoLowCut, backingHighCut, sideAir, sideMud,
            matchedLowCut, padUnderLead, arpUnderHook, hatsUnderSibilance, bassLowDuck, fxUnderVocal, clapUnderVocal, breakdownDrumLowCut,
            buildHighPass, chordFollow, drumParallel, upwardBreakdown, gluePerSection, expander, vocalPeak, padSustain, panLayout, monoWiden,
            bandWidth, correlationGuard, busSatDrop, exciterAir, kickClick, bassMidDrive, dropPunch, breakdownDrumHighCut, postDropSettle,
            finalDropLift, introOutroTone, fillDuck, vocalDoubler, vocalAir, vocalPresence, hatRides, clapTail, fxHighPass, riserEnergy,
            fxTailDark, truePeak, dcBlock, numFixes
        };
        std::array<std::atomic<bool>, numFixes> on {};
        std::array<std::atomic<float>, 256> phraseTrimDb {};   // per 4-bar phrase (Ouroboros' phrase rides)
        std::atomic<float> panOffset { 0.0f };                  // -1..1 (a pan layout, or Ouroboros' collision fix)
        std::atomic<float> lowCutHz { 0.0f };                   // from the part's lowest note (message thread)
        std::atomic<int> lowestNote { -1 };                     // measured here (MIDI)
        std::atomic<float> sibilanceShare { 0.0f };             // the vocal's 6-10 kHz share (for the hats)
        std::atomic<float> truePeakTrimDb { 0.0f };

        struct Context
        {
            double ppq = 0.0, bpm = 120.0, barQ = 4.0, sectionStartQ = 0.0, sectionEndQ = 0.0;
            int sectionType = -1, role = 0, occurrence = 0, dropCount = 0;
            float buildProgress = 0.0f, energyRatio = 1.0f;
            bool playing = false, nextIsDrop = false;
            // what the rest of the suite does now (ActivityBus, the lead's pitch, the kick's pattern)
            float leadActivity = 0.0f, vocalActivity = 0.0f, riserActivity = 0.0f, sibilance = 0.0f, dropHit = 0.0f, phraseEnd = 0.0f;
            float vocalOverMusicDb = 99.0f, leadHz = 0.0f;
            const suite::KickPatternSet* kick = nullptr;   // the kick's pattern (Kybalion), nullptr = on the beats
            float chordRootHz = 0.0f, previousChordRootHz = 0.0f, sinceChordChangeQ = 99.0f;
            // the safety round: the mix fixes' strength (Ouroboros' knob), lifts allowed by the headroom (0..1),
            // the level budget left for these fixes (dB, both ways), a build budget (another fix already takes the
            // lows out through the builds), the drum bus opening up for fills
            float strength = 1.0f, liftsAllowed = 1.0f, levelBudgetDb = 6.0f;
            bool buildBudget = false, fillOpen = false;
        };

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (auto* f : { &hp[0], &hp[1] }) f->set (40.0f, 0.707f, fs);
            for (int c = 0; c < 2; ++c)
            {
                lowSplit[(size_t) c].set (120.0f, 0.707f, fs); midSplit[(size_t) c].set (250.0f, 0.707f, fs); exciteHp[(size_t) c].set (6000.0f, 0.707f, fs);
                drumLp[(size_t) c].set (5000.0f, 0.707f, fs); drumHp[(size_t) c].set (150.0f, 0.707f, fs); fxHp[(size_t) c].set (150.0f, 0.707f, fs);
                matchedHp[(size_t) c].set (28.0f, 0.707f, fs);
            }
            sideLow.set (300.0f, 0.707f, fs); sideHigh.set (4000.0f, 0.707f, fs); sideAirF.set (8000.0f, 0.707f, fs); sideMudBp.set (300.0f, 1.0f, fs);
            pitchLp.set (900.0f, 0.707f, fs); sibBp.set (7500.0f, 1.0f, fs); presenceBp.set (3000.0f, 1.0f, fs); arpBp.set (2000.0f, 0.8f, fs); clapBp.set (3000.0f, 1.0f, fs);
            hatsBp.set (8000.0f, 1.2f, fs); topF.set (9000.0f, 0.707f, fs); leadNotch.set (440.0f, 4.0f, fs); chordNotch.set (110.0f, 4.0f, fs); widenHp.set (300.0f, 0.707f, fs);
            fxTailLp[0].set (16000.0f, 0.707f, fs); fxTailLp[1].set (16000.0f, 0.707f, fs);
            delay.assign ((size_t) (0.05 * sampleRate) + 4, 0.0f); dPos = 0;
            dcState = { 0.0f, 0.0f }; dcPrev = { 0.0f, 0.0f };
            levEnv = 0.0f; levGain = 1.0f; chainGain = 1.0f; chainIn = chainOut = 0.0;
            envFast = envSlow = 0.0f; sinceHit = 1 << 30; tailGain = 1.0f; parEnv = 0.0f; upEnv = 0.0f; glueEnv = 0.0f; expEnv = 0.0f; vocEnv = 0.0f;
            corrLR = corrLL = corrRR = 0.0; corrGain = 1.0f; zeroCross = 0; pitchWindow = 0; lastSign = false; pitchMedianHz = 0.0f;
            prevSamples = { 0.0f, 0.0f }; prevSamples2 = { 0.0f, 0.0f }; clickPhase = -1; fxEnv = 0.0f;
            lfo = 0.0f;
        }

        /** Before the chain: the input leveller (and the chain's input level, for keeping it neutral). */
        void input (float* const* ch, int chs, int n) noexcept
        {
            double p = 0.0;
            for (int c = 0; c < chs; ++c) for (int i = 0; i < n; ++i) p += (double) ch[c][i] * ch[c][i];
            chainIn = 0.95 * chainIn + 0.05 * (p / juce::jmax (1, n * chs));
            if (! on[inputLeveller].load()) { levGain = 1.0f; return; }
            const auto rms = (float) std::sqrt (p / juce::jmax (1, n * chs));
            if (rms > 1.0e-4f)
            {
                levEnv = levEnv <= 0.0f ? rms : levEnv + 0.002f * (rms - levEnv);   // the long-term level (slow)
                const auto want = juce::jlimit (0.5f, 2.0f, levEnv / juce::jmax (1.0e-5f, rms));
                levGain += 0.02f * (std::sqrt (want) - levGain);                   // half the way: a leveller, not a flattener
            }
            for (int c = 0; c < chs; ++c) juce::FloatVectorOperations::multiply (ch[c], levGain, n);
        }

        /** After the chain (before the section trims and the traits). */
        void process (float* const* ch, int chs, int n, const Context& c) noexcept
        {
            const auto role = c.role, t = c.sectionType;
            const auto qps = c.bpm / 60.0 / (double) fs;
            const auto inDrop = t == 4, inBreak = t == 2, inBuild = t == 3, inIntroOutro = t == 0 || t == 5;
            const auto isDrum = role == 1 || role == 3;
            // the chain kept loudness-neutral (its output to its input, slowly, +-3 dB)
            {
                double p = 0.0;
                for (int k = 0; k < chs; ++k) for (int i = 0; i < n; ++i) p += (double) ch[k][i] * ch[k][i];
                chainOut = 0.95 * chainOut + 0.05 * (p / juce::jmax (1, n * chs));
                if (on[chainNeutral].load() && chainOut > 1.0e-9 && chainIn > 1.0e-9)
                    chainGain += 0.01f * (juce::jlimit (0.708f, 1.413f, (float) std::sqrt (chainIn / chainOut)) - chainGain);
                else chainGain += 0.01f * (1.0f - chainGain);
            }
            // block-level gains (dB), smoothed below
            float db = 0.0f;
            const auto phraseIdx = juce::jlimit (0, 255, (int) std::floor (c.ppq / (4.0 * juce::jmax (1.0, c.barQ))));
            if (on[phraseTrim].load() && c.playing) db += phraseTrimDb[(size_t) phraseIdx].load();
            const auto inSection = c.ppq - c.sectionStartQ;
            if (on[hookLift].load() && role == 5 && inDrop && inSection < c.barQ) db += 0.5f;
            if (on[impactDuck].load() && (role == 4 || role == 5 || role == 6 || role == 7)) db -= 1.0f * c.dropHit;
            if (on[riserRoom].load() && (role == 4 || role == 5) && inBuild) db -= 1.5f * c.riserActivity;
            if (on[finalDropLift].load() && inDrop && c.dropCount >= 2 && c.occurrence == c.dropCount - 1) db += 0.5f;
            if (on[postDropSettle].load() && inDrop && inSection > 8.0 * c.barQ) db -= juce::jmin (0.5f, (float) ((inSection - 8.0 * c.barQ) / (8.0 * c.barQ)) * 0.5f);
            if (on[hatRides].load() && role == 3 && c.playing && std::fmod (juce::jmax (0.0, inSection), 8.0 * c.barQ) >= 6.0 * c.barQ) db += 0.7f;
            if (on[fillDuck].load() && (role == 4 || role == 5 || role == 7)) db -= 1.0f * c.phraseEnd;
            if (on[riserEnergy].load() && role == 7 && inBuild) db += juce::jlimit (-3.0f, 3.0f, 6.0f * (std::sqrt (juce::jmax (0.1f, c.energyRatio)) - 1.0f));
            if (on[dropPunch].load() && (role == 1 || role == 2) && inDrop && c.occurrence >= 0 && inSection < 1.0) db += 1.5f;
            db *= c.strength;
            if (db > 0.0f) db *= c.liftsAllowed;   // the headroom's short: no lifts
            const auto unclamped = db;
            db = juce::jlimit (-c.levelBudgetDb, juce::jmin (3.0f, c.levelBudgetDb), db);
            budgetHit = std::abs (db - unclamped) > 0.05f;
            lastDb = db;
            db += truePeakTrimDb.load();
            const auto targetGain = juce::Decibels::decibelsToGain (db) * chainGain;

            // per-block settings
            const auto leadOn = c.leadActivity > 0.3f, vocalOn = c.vocalActivity > 0.3f;
            const auto nameArp = isArp, nameClap = isClap, nameBus = isBus;
            if (on[padUnderLead].load() && role == 4 && c.leadHz > 60.0f && std::abs (c.leadHz - leadNotchHz) > 2.0f) { leadNotchHz = c.leadHz; leadNotch.set (juce::jlimit (60.0f, 8000.0f, c.leadHz), 4.0f, fs); }
            if (on[chordFollow].load() && role == 4 && c.previousChordRootHz > 30.0f && std::abs (c.previousChordRootHz - chordNotchHz) > 1.0f) { chordNotchHz = c.previousChordRootHz; chordNotch.set (chordNotchHz * 2.0f, 4.0f, fs); }
            const auto lc = lowCutHz.load();
            if (on[autoLowCut].load() && role != 1 && role != 2 && lc > 25.0f && std::abs (lc - hpHz) > 1.0f) { hpHz = lc; hp[0].set (lc, 0.707f, fs); hp[1].set (lc, 0.707f, fs); }
            const auto buildHp = on[buildHighPass].load() && role != 1 && role != 7 && inBuild && c.buildProgress > 0.5f;
            if (buildHp) { const auto hz = 20.0f + (c.buildBudget ? 230.0f : 460.0f) * (c.buildProgress - 0.5f); if (std::abs (hz - buildHz) > 2.0f) { buildHz = hz; buildHpf[0].set (hz, 0.707f, fs); buildHpf[1].set (hz, 0.707f, fs); } }
            // the kick's pattern: where the bass's lows duck
            const auto msPerQ = 60000.0 / juce::jmax (1.0, c.bpm);
            const auto msSinceKick = [&c, msPerQ] (double q) -> double
            {
                const auto phase = std::fmod (juce::jmax (0.0, q), c.barQ);
                if (c.kick != nullptr) { const auto ms = c.kick->msSinceHit (c.sectionType, phase, c.barQ, msPerQ); if (ms < 1.0e8f) return ms; }
                return std::fmod (phase, 1.0) * msPerQ;   // no pattern: four on the floor
            };

            for (int i = 0; i < n; ++i)
            {
                const auto q = c.ppq + qps * i;
                gain += 0.002f * (targetGain - gain);
                float l = ch[0][i], r = chs > 1 ? ch[1][i] : l;
                // DC
                if (on[dcBlock].load())
                {
                    const float in[2] = { l, r };
                    float* out[2] = { &l, &r };
                    for (int k = 0; k < 2; ++k) { const auto y = in[k] - dcPrev[(size_t) k] + 0.9995f * dcState[(size_t) k]; dcPrev[(size_t) k] = in[k]; dcState[(size_t) k] = y; *out[k] = y; }
                }
                // low cuts: matched (kick and bass), from the lowest note, drums in breakdowns, the build's rise, FX
                if (on[matchedLowCut].load() && (role == 1 || role == 2)) { l = matchedHp[0].highpass (l); r = matchedHp[1].highpass (r); }
                if (on[autoLowCut].load() && role != 1 && role != 2 && hpHz > 25.0f) { l = hp[0].highpass (l); r = hp[1].highpass (r); }
                if (on[breakdownDrumLowCut].load() && isDrum && role != 1 && inBreak) { l = drumHp[0].highpass (l); r = drumHp[1].highpass (r); }
                if (on[breakdownDrumHighCut].load() && isDrum && inBreak) { l = drumLp[0].lowpass (l); r = drumLp[1].lowpass (r); }
                if (buildHp) { l = buildHpf[0].highpass (l); r = buildHpf[1].highpass (r); }
                if (on[fxHighPass].load() && role == 7) { l = fxHp[0].highpass (l); r = fxHp[1].highpass (r); }
                // keyed dips
                const auto mono = 0.5f * (l + r);
                auto dipBand = [&] (suite::Svf& f, float amountDb)
                {
                    const auto band = f.bandpassNorm (mono);
                    const auto k = 1.0f - juce::Decibels::decibelsToGain (-amountDb * c.strength);
                    l -= k * band; r -= k * band;
                };
                if (on[padUnderLead].load() && role == 4 && leadOn && leadNotchHz > 60.0f) dipBand (leadNotch, 2.5f * c.leadActivity);
                if (on[chordFollow].load() && role == 4 && c.sinceChordChangeQ < 0.5f && chordNotchHz > 30.0f) dipBand (chordNotch, 2.0f);
                if (on[arpUnderHook].load() && nameArp && leadOn) dipBand (arpBp, 1.5f * c.leadActivity);
                if (on[hatsUnderSibilance].load() && role == 3 && c.sibilance > 0.2f) dipBand (hatsBp, 2.0f * c.sibilance);
                if (on[clapUnderVocal].load() && nameClap && vocalOn) dipBand (clapBp, 1.5f * c.vocalActivity);
                if (on[backingHighCut].load() && role == 4 && leadOn) { const auto top = topF.highpass (mono); l -= 0.2f * top; r -= 0.2f * top; }
                if (on[introOutroTone].load() && role != 1 && inIntroOutro) { const auto top = topF.highpass (mono); l -= 0.2f * top; r -= 0.2f * top; }
                if (on[vocalPresence].load() && role == 6 && c.vocalOverMusicDb < 3.0f) { const auto pres = presenceBp.bandpass (mono); const auto k = 0.19f * juce::jlimit (0.0f, 1.0f, (3.0f - c.vocalOverMusicDb) / 3.0f); l += k * pres; r += k * pres; }
                // the bass's lows duck under each kick (only its lows)
                if (on[bassLowDuck].load() && role == 2 && c.playing)
                {
                    if ((i & 15) == 0) kickMsNow = msSinceKick (q);
                    const auto duck = kickMsNow < 80.0 ? 0.5f : 1.0f;   // -6 dB for the kick's first 80 ms
                    duckGain += (duck < duckGain ? 0.05f : 0.002f) * (duck - duckGain);
                    const auto lo1 = lowSplit[0].lowpass (l), lo2 = lowSplit[1].lowpass (r);
                    l += (duckGain - 1.0f) * lo1; r += (duckGain - 1.0f) * lo2;
                }
                // FX: under the vocal, tails darkening
                if (on[fxUnderVocal].load() && role == 7) { const auto g = juce::Decibels::decibelsToGain (-2.0f * c.vocalActivity); l *= g; r *= g; }
                if (on[fxTailDark].load() && role == 7)
                {
                    const auto a = std::abs (mono);
                    fxEnv = a > fxEnv ? a : fxEnv * 0.99997f;
                    const auto bright = a > 0.5f * fxEnv ? 1.0f : 0.0f;
                    fxTone += (bright > fxTone ? 0.01f : 0.00002f) * (bright - fxTone);
                    if ((i & 63) == 0) { const auto hz = 6000.0f + 10000.0f * fxTone; fxTailLp[0].set (hz, 0.707f, fs); fxTailLp[1].set (hz, 0.707f, fs); }
                    l = fxTailLp[0].lowpass (l); r = fxTailLp[1].lowpass (r);
                }
                // dynamics: hits and tails
                const auto a = 0.5f * (std::abs (l) + std::abs (r));
                envFast += 0.15f * (a - envFast); envSlow += 0.002f * (a - envSlow);
                if (envFast > 2.0f * envSlow && envFast > 1.0e-3f && sinceHit > (int) (0.06f * fs)) { sinceHit = 0; if (on[kickClick].load() && role == 1) clickPhase = 0; } else ++sinceHit;
                {
                    float target = 1.0f;
                    if (on[clapTail].load() && nameClap && inDrop && sinceHit > (int) (0.12f * fs)) target = 0.63f;
                    if (on[padSustain].load() && role == 4 && inDrop && sinceHit > (int) (0.3f * fs)) target = 0.71f;
                    tailGain += (target < tailGain ? 0.0005f : 0.01f) * (target - tailGain);
                    l *= tailGain; r *= tailGain;
                }
                // a click on a dull kick: 2 ms at 3.5 kHz on each hit, 20 dB under its peak
                if (clickPhase >= 0)
                {
                    const auto t2 = (float) clickPhase / fs;
                    const auto v = 0.1f * envFast * std::sin (2.0f * juce::MathConstants<float>::pi * 3500.0f * t2) * (1.0f - t2 / 0.002f);
                    l += v; r += v;
                    if (++clickPhase > (int) (0.002f * fs)) clickPhase = -1;
                }
                // drum bus parallel, glue per section, upward compression in breakdowns, an expander, the vocal's peaks
                const auto fillNow = c.fillOpen && c.phraseEnd > 0.5f;   // the drums open up for the fill
                if (on[drumParallel].load() && nameBus && isDrum && ! fillNow)
                {
                    parEnv += (a > parEnv ? 0.3f : 0.0005f) * (a - parEnv);
                    const auto g = juce::jlimit (0.1f, 1.0f, 0.1f / juce::jmax (1.0e-4f, parEnv));   // a heavy squash
                    l += 0.2f * l * g; r += 0.2f * r * g;
                }
                if (on[gluePerSection].load() && nameBus && ! fillNow)
                {
                    glueEnv += (a > glueEnv ? 0.01f : 0.0003f) * (a - glueEnv);
                    const auto thr = inDrop ? 0.25f : inBreak ? 0.5f : 0.35f;
                    if (glueEnv > thr) { const auto g = std::pow (thr / glueEnv, 0.5f); l *= g; r *= g; }   // 2:1 over the section's threshold
                }
                if (on[upwardBreakdown].load() && (role == 4 || role == 5) && inBreak)
                {
                    upEnv += (a > upEnv ? 0.01f : 0.0003f) * (a - upEnv);
                    if (upEnv > 1.0e-4f && upEnv < 0.05f) { const auto g = juce::jmin (1.41f, std::pow (0.05f / upEnv, 0.25f)); l *= g; r *= g; }
                }
                if (on[expander].load())
                {
                    expEnv += (a > expEnv ? 0.05f : 0.0005f) * (a - expEnv);
                    if (expEnv < expThreshold) { const auto g = juce::jmax (0.1f, expEnv / juce::jmax (1.0e-6f, expThreshold)); l *= g; r *= g; }
                }
                if (on[vocalPeak].load() && role == 6)
                {
                    vocEnv = a > vocEnv ? a : vocEnv + 0.0004f * (a - vocEnv);
                    vocLong += 0.00002f * (a - vocLong);
                    const auto thr = juce::jmax (0.02f, 2.0f * vocLong);
                    if (vocEnv > thr) { const auto g = std::pow (thr / vocEnv, 0.75f); l *= g; r *= g; }   // 4:1 over twice its average
                }
                // colour: saturation on a bus in drops, an exciter, the bass's mids driven, the vocal's air and doubler
                if (on[busSatDrop].load() && nameBus && inDrop) { l = 0.8f * l + 0.2f * std::tanh (1.5f * l) / 1.5f; r = 0.8f * r + 0.2f * std::tanh (1.5f * r) / 1.5f; }
                if ((on[exciterAir].load() && (role == 3 || role == 4)) || (on[vocalAir].load() && role == 6))
                {
                    const auto h1 = exciteHp[0].highpass (l), h2 = exciteHp[1].highpass (r);
                    l += 0.08f * std::tanh (4.0f * h1); r += 0.08f * std::tanh (4.0f * h2);
                }
                if (on[bassMidDrive].load() && role == 2)
                {
                    const auto lo1 = midSplit[0].lowpass (l), lo2 = midSplit[1].lowpass (r);
                    const auto hi1 = l - lo1, hi2 = r - lo2;
                    l = lo1 + 0.6f * hi1 + 0.4f * std::tanh (2.5f * hi1) / 2.5f * 2.0f;
                    r = lo2 + 0.6f * hi2 + 0.4f * std::tanh (2.5f * hi2) / 2.5f * 2.0f;
                }
                if (on[vocalDoubler].load() && role == 6 && inDrop)
                {
                    delay[(size_t) dPos] = 0.5f * (l + r);
                    lfo += 0.5f / fs; if (lfo > 1.0f) lfo -= 1.0f;
                    const auto dl = (0.015f + 0.0003f * std::sin (juce::MathConstants<float>::twoPi * lfo)) * fs;
                    const auto rp = (float) dPos - dl;
                    const auto size = (int) delay.size();
                    const auto i0 = (int) std::floor (rp);
                    const auto fr = rp - (float) i0;
                    const auto v = delay[(size_t) ((i0 % size + size) % size)] * (1.0f - fr) + delay[(size_t) (((i0 + 1) % size + size) % size)] * fr;
                    l += 0.3f * v; r += 0.3f * v;
                    dPos = (dPos + 1) % size;
                }
                // stereo: band-split width, side air, side mud, mono-safe widening, the correlation guard, the pan
                if (chs > 1)
                {
                    auto m = 0.5f * (l + r), s = 0.5f * (l - r);
                    if (on[bandWidth].load() && (role == 4 || role == 5)) { const auto sl = sideLow.lowpass (s), sh = sideHigh.highpass (s); s += -0.3f * sl + 0.2f * sh; }
                    if (on[sideAir].load() && (role == 4 || role == 5)) s += 0.19f * sideAirF.highpass (s);
                    if (on[sideMud].load() && role == 4) s -= 0.2f * sideMudBp.bandpass (s);
                    if (on[monoWiden].load() && role == 4 && monoInput)
                    {
                        delay[(size_t) dPos] = m;
                        const auto size = (int) delay.size();
                        const auto d = delay[(size_t) (((dPos - (int) (0.012f * fs)) % size + size) % size)];
                        s += 0.25f * widenHp.highpass (d);   // in the side only: the mono sum is untouched
                        dPos = (dPos + 1) % size;   // (the doubler is the vocal's: never on the same track)
                    }
                    if (on[correlationGuard].load() && role != 1 && role != 2)
                    {
                        corrLR += 0.0005 * ((double) l * r - corrLR); corrLL += 0.0005 * ((double) l * l - corrLL); corrRR += 0.0005 * ((double) r * r - corrRR);
                        const auto corr = corrLL > 1.0e-9 && corrRR > 1.0e-9 ? (float) (corrLR / std::sqrt (corrLL * corrRR)) : 1.0f;
                        const auto want = corr < 0.0f ? 0.7f : corr > 0.3f ? 1.0f : corrGain;
                        corrGain += 0.0005f * (want - corrGain);
                        s *= corrGain;
                    }
                    l = m + s; r = m - s;
                    const auto pan = juce::jlimit (-1.0f, 1.0f, panOffset.load() + (on[panLayout].load() ? layoutPan : 0.0f));
                    if (std::abs (pan) > 1.0e-3f) { l *= 1.0f - juce::jmax (0.0f, pan); r *= 1.0f + juce::jmin (0.0f, pan); }
                }
                // the note-range evening (high notes down) and the level moves
                float g = gain;
                if (on[noteEven].load() && (role == 4 || role == 5) && pitchMedianHz > 0.0f && pitchNowHz > 0.0f)
                    g *= juce::Decibels::decibelsToGain (juce::jlimit (-3.0f, 0.0f, -1.5f * std::log2 (pitchNowHz / pitchMedianHz)));
                l *= g; r *= g;
                // pitch (zero crossings of the lows): the part's lowest note, and the note now
                {
                    const auto p = pitchLp.lowpass (0.5f * (l + r));
                    const auto sign = p > 0.0f;
                    if (sign != lastSign) { ++zeroCross; lastSign = sign; }
                    if (++pitchWindow >= (int) (0.05f * fs))
                    {
                        const auto hz = (float) zeroCross * 0.5f / 0.05f;
                        if (hz > 30.0f && hz < 1200.0f && envSlow > 1.0e-3f)
                        {
                            pitchNowHz = hz;
                            pitchMedianHz = pitchMedianHz <= 0.0f ? hz : pitchMedianHz * std::pow (hz / pitchMedianHz, 0.01f);
                            const auto midi = (int) std::lround (69.0f + 12.0f * std::log2 (hz / 440.0f));
                            if (lowestSeen < 0 || midi < lowestSeen) { if (++lowCount > 20) lowestSeen = midi; } else lowCount = 0;
                            lowestNote.store (lowestSeen);
                        }
                        zeroCross = 0; pitchWindow = 0;
                    }
                }
                // the vocal's "s": its 6-10 kHz share, for the hats
                if (role == 6) { const auto sb = sibBp.bandpass (0.5f * (l + r)); sibEnv += 0.01f * (std::abs (sb) - sibEnv); sibAll += 0.01f * (0.5f * (std::abs (l) + std::abs (r)) - sibAll); }
                // inter-sample peaks: the midpoint between samples (a cheap 2x look)
                if (on[truePeak].load())
                {
                    const auto midL = 0.5625f * (l + prevSamples[0]) - 0.0625f * (prevSamples2[0] + l), midR = 0.5625f * (r + prevSamples[1]) - 0.0625f * (prevSamples2[1] + r);
                    tpHold = juce::jmax (tpHold, juce::jmax (std::abs (midL), std::abs (midR)), juce::jmax (std::abs (l), std::abs (r)));
                    prevSamples2 = prevSamples; prevSamples = { l, r };
                }
                ch[0][i] = l;
                if (chs > 1) ch[1][i] = r;
            }
            if (role == 6) sibilanceShare.store (sibAll > 1.0e-5f ? juce::jlimit (0.0f, 1.0f, 4.0f * sibEnv / sibAll) : 0.0f);
        }
        /** Message thread, a few times a second: the inter-sample peak trim (a step at a time). */
        void tick()
        {
            const auto pk = tpHold; tpHold = 0.0f;
            auto trim = truePeakTrimDb.load();
            if (on[truePeak].load() && pk > 0.891f) trim = juce::jmax (-6.0f, trim - 0.5f);                 // over -1 dBFS between samples
            else if (pk < 0.7f) trim = juce::jmin (0.0f, trim + 0.1f);
            if (! on[truePeak].load()) trim = 0.0f;
            truePeakTrimDb.store (trim);
        }
        // what the message thread knows about this track
        bool isArp = false, isClap = false, isBus = false, monoInput = false;
        float expThreshold = 0.0f, layoutPan = 0.0f;
        float lastDb = 0.0f;      // this block's level move (dB, before the true-peak trim), for the level budget
        bool budgetHit = false;   // the budget held a move back this block
        float pitchHz() const noexcept { return pitchNowHz; }   // the note now (its own estimate; 0 = none)
        float pitchMedian() const noexcept { return pitchMedianHz; }

    private:
        float fs = 44100.0f, gain = 1.0f, levEnv = 0.0f, levGain = 1.0f, chainGain = 1.0f, duckGain = 1.0f;
        double chainIn = 0.0, chainOut = 0.0;
        std::array<suite::Svf, 2> hp, lowSplit, midSplit, exciteHp, drumLp, drumHp, fxHp, matchedHp, buildHpf, fxTailLp;
        suite::Svf sideLow, sideHigh, sideAirF, sideMudBp, pitchLp, sibBp, presenceBp, arpBp, clapBp, hatsBp, topF, leadNotch, chordNotch, widenHp;
        double kickMsNow = 1.0e9;
        float hpHz = 0.0f, buildHz = 0.0f, leadNotchHz = 0.0f, chordNotchHz = 0.0f;
        std::vector<float> delay;
        int dPos = 0;
        std::array<float, 2> dcState {}, dcPrev {}, prevSamples {}, prevSamples2 {};
        float envFast = 0.0f, envSlow = 0.0f, tailGain = 1.0f, parEnv = 0.0f, upEnv = 0.0f, glueEnv = 0.0f, expEnv = 0.0f, vocEnv = 0.0f, vocLong = 0.0f;
        int sinceHit = 1 << 30, clickPhase = -1;
        double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0;
        float corrGain = 1.0f, fxEnv = 0.0f, fxTone = 1.0f, lfo = 0.0f, sibEnv = 0.0f, sibAll = 0.0f, tpHold = 0.0f;
        int zeroCross = 0, pitchWindow = 0, lowestSeen = -1, lowCount = 0;
        bool lastSign = false;
        float pitchMedianHz = 0.0f, pitchNowHz = 0.0f;
    };
}
