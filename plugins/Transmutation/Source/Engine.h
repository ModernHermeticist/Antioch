#pragma once

// Transmutation's engine: one chain of stages every move drives. The processor sets the targets
// (a Mod, built from every move playing now by Recipes.h) every 32 samples; the engine glides to
// them and plays the stages that are in use. Allocation-free after prepare().

#include "SuiteDSP.h"
#include "Analysis.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace trm
{
    enum PlayMode { playNone, playRepeat, playReverse, playTapeStop, playRewind, playHalftime, playShuffle, playSwing,
                    playMemory, playTapeLoop, playMemShuffle };
    enum GateKind { gateNone, gateStraight, gateEuclid, gateMask, gatePoly, gateAccel, gateKick };
    enum GrainMode { grainFrozen, grainStretch, grainLive, grainMemory, grainChord };

    /** What the moves ask the stages for. Neutral values change nothing. */
    struct Mod
    {
        // level and dynamics
        float gain = 1.0f, gateDepth = 0.0f, squeeze = 0.0f, drive = 0.0f, crush = 0.0f, dropout = 0.0f, kickDuck = 0.0f, tailGate = 0.0f;
        int gateKind = gateNone; float gateRateQ = 0.25f; uint16_t gateMask = 0xffff; int euclidK = 5; float polyQ = 0.75f;
        bool crushOffbeat = false;
        // buffer player
        int play = playNone; float playMix = 0.0f; bool playAdd = false; double playA = 0.0, playB = 0.0; uint32_t playSeed = 0; int64_t playAnchor = 0;
        // grains
        float grainMix = 0.0f, grainSizeMs = 90.0f, grainRate = 30.0f, grainSpread = 0.0f, grainPitch = 0.0f, grainSpeed = 0.5f;
        int grainMode = grainLive; int64_t grainAnchor = 0; bool grainAdd = false;
        // pitch
        bool vibHeld = false; float velColour = 0.0f, sendScale = 1.0f;
        float pitch = 0.0f, vib = 0.0f, wow = 0.0f, sag = 0.0f, doppler = 0.0f, dopplerPos = 0.0f;
        std::array<float, 3> harmMix {}, harmSemis {};
        float harmDelayQ = 0.0f; bool harmToDelay = false;
        float octDown = 0.0f, octUp = 0.0f; bool bloomHeld = false;
        float detune = 0.0f, fshift = 0.0f, fshiftMix = 0.0f, shepard = 0.0f;
        // filters
        float hp = 20.0f, lp = 20000.0f, lpQ = 0.707f, vowel = 0.0f, vowelMix = 0.0f, phone = 0.0f, combMix = 0.0f;
        float overtone = 0.0f, overtoneMix = 0.0f, peakHz = 1000.0f, peakDb = 0.0f, bright = 0.0f, air = 0.0f, shapeMix = 0.0f;
        int shapeRole = 0;
        bool keyTrack = false; float keyTrackAmt = 0.0f;
        // stereo
        float width = 1.0f, pan = 0.0f, rotateHz = 0.0f, rotateDepth = 0.0f, haas = 0.0f;
        // delay
        float dSend = 0.0f, dTimeQ = 0.75f, dFb = 0.35f, dPitch = 0.0f, dLp = 7000.0f, dHp = 200.0f, dRev = 0.0f;
        // reverb
        float rSend = 0.0f, rDecay = 2.0f, rSize = 0.5f, rDamp = 8000.0f, shimmer = 0.0f, rFreeze = 0.0f, rGate = 0.0f, rDuck = 0.0f;
        // additive layers
        float drone = 0.0f, weather = 0.0f;
    };

    /** The song around the engine now (set every 32 samples by the processor). */
    struct Env
    {
        double posQ = 0.0, barQ = 4.0, samplesPerQ = 24000.0;
        bool playing = false;
        float rootHz = 55.0f;                 // the section's root, low (octave 1)
        std::array<float, 3> chordHz { 220.0f, 277.2f, 329.6f };   // the chord's notes (octave 3-4)
        float kickSinceMs = 1.0e9f, kickUntilMs = 1.0e9f;
        float leadHz = 0.0f;
        std::array<float, 10> shapeDb {};    // another track's live tone (bands, relative to its mean)
        float endFade = 1.0f;                 // a clean ending past the song's last bar
        bool chordChanged = false;
        float amount = 1.0f;                  // dry/wet of the whole plugin
    };

    //==============================================================================
    /** Hilbert pair (two allpass chains, 90 degrees apart over ~20 Hz-20 kHz) for frequency shifting. */
    class Hilbert
    {
    public:
        void reset() { for (auto& s : st) s = {}; prev = 0.0f; }
        void process (float x, float& re, float& im) noexcept
        {
            static constexpr float a1[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            static constexpr float a2[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            auto r = x, i = x;
            for (int k = 0; k < 4; ++k) r = st[(size_t) k].run (r, a1[k]);
            for (int k = 0; k < 4; ++k) i = st[(size_t) k + 4].run (i, a2[k]);
            re = prev; prev = r;   // one sample behind: the chains then sit 90 degrees apart
            im = i;
        }
    private:
        struct Section
        {
            float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            float run (float x, float a) noexcept
            {
                const auto y = a * a * (x + y2) - x2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                return y;
            }
        };
        std::array<Section, 8> st {};
        float prev = 0.0f;
    };

    /** Mono feedback-free resonator on a frequency (a peak), for the chord resonator and weather. */
    struct Resonators
    {
        std::array<suite::CombResonator, 3> combs;
        void prepare (double sr) { for (auto& c : combs) { c.prepare (sr); c.setFeedback (0.93f); c.setDamping (0.35f); } }
        void reset() { for (auto& c : combs) c.reset(); }
        void tune (const std::array<float, 3>& hz, float fb) { for (size_t i = 0; i < 3; ++i) { combs[i].setFrequency (hz[i]); combs[i].setFeedback (fb); } }
        float process (float x) noexcept { return (combs[0].process (x) + combs[1].process (x) + combs[2].process (x)) * 0.33f; }
    };

    //==============================================================================
    class Engine
    {
    public:
        static constexpr int subBlock = 32;
        static constexpr int ringBits = 20, ringSize = 1 << ringBits, ringMask = ringSize - 1;

        void prepare (double newSampleRate)
        {
            sr = newSampleRate;
            const auto fs = (float) sr;
            ringL.assign ((size_t) ringSize, 0.0f); ringR.assign ((size_t) ringSize, 0.0f);
            memCapacity = (int) (sr * 16.0);
            memL.assign ((size_t) memCapacity, 0.0f); memR.assign ((size_t) memCapacity, 0.0f);
            memLen = 0;
            for (auto* p : { &pitchL, &pitchR, &detL, &detR, &octD, &octU, &shepA, &shepB, &fbPitchL, &fbPitchR, &shimmerShift })
                p->prepare (sr, 0.05f);
            for (auto& h : harm) h.prepare (sr, 0.05f);
            for (auto& d : harmDelay) d.prepare ((int) (sr * 2.5));
            tapeL.prepare ((int) (sr * 0.12)); tapeR.prepare ((int) (sr * 0.12));
            haasLine.prepare ((int) (sr * 0.03));
            dlyL.prepare ((int) (sr * 4.5)); dlyR.prepare ((int) (sr * 4.5));
            verb.prepare (sr);
            combs.prepare (sr); weatherCombs.prepare (sr);
            onsetFast.set (1.0f, 30.0f, fs); onsetSlow.set (20.0f, 250.0f, fs);
            env.set (3.0f, 80.0f, fs); slowEnv.set (300.0f, 300.0f, fs);
            pitchLp.setCutoff (900.0f, fs);
            memLpL.setCutoff (2500.0f, fs); memLpR.setCutoff (2500.0f, fs);
            levelCoeff = 1.0 - std::exp (-1.0 / (0.3 * sr));
            levelCoeffFast = 1.0 - std::exp (-1.0 / (0.05 * sr));
            levelAttack = 1.0f - std::exp (-1.0f / (0.02f * fs));
            levelRelease = 1.0f - std::exp (-1.0f / (0.2f * fs));
            reset();
        }

        void reset()
        {
            std::fill (ringL.begin(), ringL.end(), 0.0f); std::fill (ringR.begin(), ringR.end(), 0.0f);
            for (auto* p : { &pitchL, &pitchR, &detL, &detR, &octD, &octU, &shepA, &shepB, &fbPitchL, &fbPitchR, &shimmerShift }) p->reset();
            for (auto& h : harm) h.reset();
            for (auto& d : harmDelay) d.reset();
            tapeL.reset(); tapeR.reset(); haasLine.reset(); dlyL.reset(); dlyR.reset(); verb.reset();
            combs.reset(); weatherCombs.reset(); hilbert.reset();
            for (auto* f : { &hpL, &hpR, &lpL, &lpR, &phHpL, &phHpR, &phLpL, &phLpR, &otL, &otR, &windBp, &droneLp, &dHpL, &dHpR, &dLpL, &dLpR })
                f->reset();
            for (auto& f : vowelL) f.reset();
            for (auto& f : vowelR) f.reset();
            for (auto& b : shapeL) b.reset();
            for (auto& b : shapeR) b.reset();
            peakL.reset(); peakR.reset(); brL.reset(); brR.reset(); airL.reset(); airR.reset();
            for (auto& g : grains) g.active = false;
            cur = Mod {}; tgt = Mod {};
            absPos = 0; gainSm = 1.0f; echoIdle = verbIdle = 1 << 30; levelIn = levelOut = levelInFast = levelOutFast = 0.0; levelGain = 1.0f;
            playLastAnchor = -1; playPos = 0.0; loopPhase = 0.0; swingD = 0.0;
            accelPhase = 0.0; rotPhase = 0.0; vibPhase = 0.0; wowPhase = 0.0; fsPhase = 0.0; shepPhase = 0.0; dronePhase = 0.0;
            heldSamples = 0; held = 0.0f; sinceOnset = 1 << 30; onsetCount = 0;
            onsetMaskNow = onsetMaskLast = 0; lastBar = -1; tailEnv = 1.0f; dRevPhase = 0.0;
            lastCoeff = Mod {}; coeffInit = false;
            crushHoldL = crushHoldR = 0.0f; crushCount = 0;
            memRecording = false;
        }

        Mod tgt;
        const Mod& current() const noexcept { return cur; }
        int64_t now() const noexcept { return absPos; }

        // ---- Drop memory: the first drop, recorded for hook memories, mirror drops and self-remixes ----
        void startMemory() noexcept { memRecording = true; memWrite = 0; }
        void stopMemory() noexcept { if (memRecording) { memRecording = false; memLen = memWrite; } }
        bool memoryRecording() const noexcept { return memRecording; }
        int memoryLength() const noexcept { return memLen; }

        // ---- What the engine hears (for the director's audio profile) ----
        struct Stats { double energy = 0, diff = 0, side = 0; int onsets = 0, samples = 0, heldSamples = 0; float pitchHz = 0; };
        Stats takeStats() noexcept { auto s = stats; s.pitchHz = pitchHz; stats = {}; return s; }
        uint16_t lastBarOnsets() const noexcept { return onsetMaskLast; }
        float heldAmount() const noexcept { return held; }
        float pitchEstimate() const noexcept { return pitchHz; }
        /** Decimated mono input for the pitchedness check (message thread reads it). */
        static constexpr int decimSize = 4096;
        std::array<float, decimSize> decim {};
        std::atomic<int> decimWrite { 0 };

        float lastInPeak = 0.0f, lastOutPeak = 0.0f;
        float levelHoldDb() const noexcept { return juce::Decibels::gainToDecibels (levelGain); }
        void clearTails() noexcept { dlyL.reset(); dlyR.reset(); verb.reset(); for (auto& g : grains) g.active = false; }

        //==============================================================================
        /** Processes up to subBlock samples in place with the current targets. */
        void process (float* L, float* R, int n, const Env& e) noexcept
        {
            glide (e);
            const auto fs = (float) sr;
            const auto& m = cur;
            updateCoefficients (e);
            const auto qPerSample = 1.0 / e.samplesPerQ;
            const auto barSamples = e.barQ * e.samplesPerQ;

            for (int i = 0; i < n; ++i)
            {
                const auto posQ = e.posQ + i * qPerSample;
                const auto inL = L[i], inR = R[i];
                // ---- record and listen ----
                ringL[(size_t) (absPos & ringMask)] = inL;
                ringR[(size_t) (absPos & ringMask)] = inR;
                if (memRecording)
                {
                    if (memWrite < memCapacity) { memL[(size_t) memWrite] = inL; memR[(size_t) memWrite] = inR; ++memWrite; }
                    else stopMemory();
                }
                listen (inL, inR, posQ, e.barQ);

                float xL = inL, xR = inR;
                addVerbL = addVerbR = 0.0f;

                // ---- buffer player ----
                if (m.playMix > 0.001f && m.play != playNone)
                {
                    float pL, pR;
                    player (pL, pR, posQ, e, barSamples);
                    if (m.playAdd)
                    {
                        // memories: added under the part, distant (darker) and into the reverb
                        pL = memLpL.lowpass (pL); pR = memLpR.lowpass (pR);
                        addVerbL = m.playMix * pL; addVerbR = m.playMix * pR;
                        xL += m.playMix * pL; xR += m.playMix * pR;
                    }
                    else
                    {
                        xL += m.playMix * (pL - xL);
                        xR += m.playMix * (pR - xR);
                    }
                }
                else if (m.play == playNone)
                {
                    playLastAnchor = -1;
                }

                // ---- grains ----
                if (m.grainMix > 0.001f)
                {
                    float gL, gR;
                    grainsProcess (gL, gR);
                    if (m.grainAdd) { xL += m.grainMix * gL; xR += m.grainMix * gR; addVerbL += 0.5f * m.grainMix * gL; addVerbR += 0.5f * m.grainMix * gR; }
                    else { xL += m.grainMix * (gL - xL); xR += m.grainMix * (gR - xR); }
                }

                // ---- pitch: the whole part, harmonies, octaves, detune, the Shepard rise ----
                const auto mono = 0.5f * (xL + xR);
                xL = pitchL.process (xL); xR = pitchR.process (xR);
                float add = 0.0f, toDelay = 0.0f;
                for (size_t h = 0; h < 3; ++h)
                    if (m.harmMix[h] > 0.001f)
                    {
                        auto v = harm[h].process (mono);
                        if (m.harmDelayQ > 0.0f)
                        {
                            harmDelay[h].push (v);
                            v = harmDelay[h].read ((float) juce::jlimit (1.0, sr * 2.4, m.harmDelayQ * e.samplesPerQ * (double) (h + 1)));
                        }
                        (m.harmToDelay ? toDelay : add) += m.harmMix[h] * v;
                    }
                if (m.octDown > 0.001f || m.octUp > 0.001f)
                {
                    const auto bloom = m.bloomHeld ? held : 1.0f;
                    add += bloom * (m.octDown * octD.process (mono) + 0.6f * m.octUp * octU.process (mono));
                }
                if (m.detune > 0.001f)
                {
                    const auto dl = detL.process (xL), dr = detR.process (xR);
                    xL = 0.6f * xL + 0.55f * dl; xR = 0.6f * xR + 0.55f * dr;
                }
                if (m.shepard > 0.001f)
                {
                    shepPhase += 1.0 / (2.0 * barSamples);
                    shepPhase -= std::floor (shepPhase);
                    const auto pa = (float) shepPhase, pb = std::fmod (pa + 0.5f, 1.0f);
                    shepA.setSemitones (0.02f + 12.0f * pa); shepB.setSemitones (0.02f + 12.0f * pb);
                    const auto ga = std::sin (suite::pi * pa), gb = std::sin (suite::pi * pb);
                    add += m.shepard * 0.7f * (ga * ga * shepA.process (mono) + gb * gb * shepB.process (mono));
                }
                if (m.fshiftMix > 0.001f)
                {
                    float re, im;
                    hilbert.process (mono, re, im);
                    fsPhase += m.fshift / sr;
                    fsPhase -= std::floor (fsPhase);
                    const auto ph = (float) (fsPhase * juce::MathConstants<double>::twoPi);
                    const auto shifted = re * std::cos (ph) - im * std::sin (ph);
                    xL += m.fshiftMix * (shifted - xL);
                    xR += m.fshiftMix * (shifted - xR);
                }
                xL += add; xR += add;

                // ---- filters ----
                if (m.hp > 21.0f) { xL = hpL.highpass (xL); xR = hpR.highpass (xR); } else { hpL.highpass (xL); hpR.highpass (xR); }
                const auto lpOn = m.lp < 19500.0f || m.keyTrack;
                if (lpOn) { xL = lpL.lowpass (xL); xR = lpR.lowpass (xR); } else { lpL.lowpass (xL); lpR.lowpass (xR); }
                if (m.vowelMix > 0.001f)
                {
                    float vL = 0.0f, vR = 0.0f;
                    static constexpr float fg[3] = { 1.0f, 0.6f, 0.3f };
                    for (size_t k = 0; k < 3; ++k) { vL += fg[k] * vowelL[k].bandpass (xL); vR += fg[k] * vowelR[k].bandpass (xR); }
                    xL += m.vowelMix * (vL * 0.35f - xL); xR += m.vowelMix * (vR * 0.35f - xR);
                }
                if (m.phone > 0.001f)
                {
                    const auto pL = std::tanh (3.0f * phLpL.lowpass (phHpL.highpass (xL))), pR = std::tanh (3.0f * phLpR.lowpass (phHpR.highpass (xR)));
                    xL += m.phone * (pL * 0.6f - xL); xR += m.phone * (pR * 0.6f - xR);
                }
                if (m.combMix > 0.001f)
                {
                    const auto c = combs.process (0.5f * (xL + xR)) * 0.5f;
                    xL += m.combMix * c; xR += m.combMix * c;
                }
                if (m.overtoneMix > 0.001f) { xL += m.overtoneMix * 3.0f * otL.bandpass (xL); xR += m.overtoneMix * 3.0f * otR.bandpass (xR); }
                if (std::abs (m.peakDb) > 0.05f) { xL = peakL.process (xL); xR = peakR.process (xR); }
                if (std::abs (lastCoeff.bright) > 0.05f) { xL = brL.process (xL); xR = brR.process (xR); }
                if (std::abs (m.air) > 0.05f) { xL = airL.process (xL); xR = airR.process (xR); }
                if (m.shapeMix > 0.001f)
                    for (size_t b = 0; b < shapeL.size(); ++b) { xL = shapeL[b].process (xL); xR = shapeR[b].process (xR); }

                // ---- colour: drive and crush ----
                if (m.drive > 0.001f)
                {
                    const auto k = 1.0f + 7.0f * m.drive;
                    xL = std::tanh (k * xL) / std::sqrt (k); xR = std::tanh (k * xR) / std::sqrt (k);
                }
                if (m.crush > 0.001f && (! m.crushOffbeat || std::fmod (posQ, 1.0) >= 0.5))
                {
                    const auto hold = 1 + (int) (m.crush * 14.0f);
                    if (++crushCount >= hold) { crushCount = 0; crushHoldL = xL; crushHoldR = xR; }
                    const auto steps = std::exp2 (14.0f - 10.0f * m.crush);
                    xL = std::round (crushHoldL * steps) / steps; xR = std::round (crushHoldR * steps) / steps;
                }

                // ---- tape: wow, flutter, vibrato, sag, doppler (one modulated delay) ----
                if (m.wow > 0.001f || m.vib > 0.001f || m.sag > 0.001f || m.doppler > 0.001f)
                {
                    vibPhase += 5.5 / sr; vibPhase -= std::floor (vibPhase);
                    wowPhase += 0.55 / sr; wowPhase -= std::floor (wowPhase);
                    const auto tw = (float) (wowPhase * juce::MathConstants<double>::twoPi), tv = (float) (vibPhase * juce::MathConstants<double>::twoPi);
                    const auto vibA = (m.vibHeld ? held : 1.0f) * m.vib * 0.0087f * fs / (juce::MathConstants<float>::twoPi * 5.5f) * 1.2f;
                    const auto d = 2.0f + vibA * (1.0f + std::sin (tv))
                                 + m.wow * (0.0025f * fs * (1.0f + std::sin (tw)) + 0.0002f * fs * (1.0f + std::sin (tw * 11.3f)))
                                 + m.sag * 0.025f * fs
                                 + m.doppler * 0.02f * fs * std::abs (2.0f * m.dopplerPos - 1.0f);
                    tapeL.push (xL); tapeR.push (xR);
                    xL = tapeL.read (d); xR = tapeR.read (d);
                }
                else
                {
                    tapeL.push (xL); tapeR.push (xR);
                }

                // ---- gain: level, gates, squeeze, kick shape, tail gate, dropouts ----
                auto g = m.gain * gateGain (posQ, e) * squeezeGain (xL, xR);
                if (m.kickDuck > 0.001f)
                {
                    const auto since = e.kickSinceMs + (float) (i * 1000.0 / sr), until = e.kickUntilMs - (float) (i * 1000.0 / sr);
                    const auto spq = (float) (e.samplesPerQ * 1000.0 / sr);
                    auto duck = since < 1.0e8f ? std::exp (-since / (0.22f * spq)) : 0.0f;
                    if (until < 4.0f) duck = juce::jmax (duck, 1.0f - until / 4.0f);
                    g *= 1.0f - m.kickDuck * duck;
                }
                if (m.tailGate > 0.001f)
                {
                    const auto holdS = 0.12 * e.samplesPerQ;
                    tailEnv += ((double) sinceOnset > holdS ? (1.0f - m.tailGate) - tailEnv : 1.0f - tailEnv) * 0.004f;
                    g *= tailEnv;
                }
                if (m.dropout > 0.001f)
                {
                    const auto cell = (int64_t) std::floor (posQ * 8.0);
                    const auto inBar = std::fmod (posQ, e.barQ);
                    const auto h = (uint32_t) (cell * 2654435761u) ^ m.playSeed;
                    if (inBar >= 0.125 && (float) ((h >> 8) % 1000u) < m.dropout * 1000.0f) g = 0.0f;
                }
                gainSm += (g - gainSm) * (g < gainSm ? 0.08f : 0.04f);
                xL *= gainSm; xR *= gainSm;

                // ---- stereo ----
                if (std::abs (m.width - 1.0f) > 0.01f)
                {
                    const auto mid = 0.5f * (xL + xR), side = 0.5f * (xL - xR) * m.width;
                    xL = mid + side; xR = mid - side;
                }
                if (m.haas > 0.05f) { haasLine.push (xR); xR = haasLine.read (m.haas * 0.001f * fs); } else haasLine.push (xR);
                auto pan = m.pan;
                if (m.rotateDepth > 0.001f)
                {
                    rotPhase += m.rotateHz / sr; rotPhase -= std::floor (rotPhase);
                    pan += m.rotateDepth * (float) std::sin (rotPhase * juce::MathConstants<double>::twoPi);
                }
                if (std::abs (pan) > 0.005f)
                {
                    pan = juce::jlimit (-1.0f, 1.0f, pan);
                    xL *= juce::jmin (1.0f, 1.0f - pan); xR *= juce::jmin (1.0f, 1.0f + pan);
                }

                // ---- echo ----
                float wetL = 0.0f, wetR = 0.0f;
                // the echo sleeps once nothing goes in and its tail has died away (most of the time)
                const auto echoFed = m.dSend > 0.0005f || std::abs (toDelay) > 1.0e-7f;
                echoIdle = echoFed || std::abs (lastEchoL) + std::abs (lastEchoR) > 1.0e-5f ? 0 : juce::jmin (echoIdle + 1, 1 << 30);
                if (echoIdle == (int) (sr * 0.25)) { dlyL.reset(); dlyR.reset(); lastEchoL = lastEchoR = 0.0f; }
                if (echoIdle < (int) (sr * 0.25))
                {
                    const auto D = (float) juce::jlimit (32.0, sr * 4.4, (double) m.dTimeQ * e.samplesPerQ);
                    auto oL = dlyL.read (D), oR = dlyR.read (D * 1.003f);
                    if (m.dRev > 0.001f)
                    {
                        // reverse heads: two windows reading backwards through the last D samples
                        dRevPhase += 1.0 / D; dRevPhase -= std::floor (dRevPhase);
                        const auto p1 = (float) dRevPhase, p2 = std::fmod (p1 + 0.5f, 1.0f);
                        const auto w1 = std::sin (suite::pi * p1), w2 = std::sin (suite::pi * p2);
                        const auto rL = w1 * w1 * dlyL.read (1.0f + 2.0f * D * p1) + w2 * w2 * dlyL.read (1.0f + 2.0f * D * p2);
                        const auto rR = w1 * w1 * dlyR.read (1.0f + 2.0f * D * p1) + w2 * w2 * dlyR.read (1.0f + 2.0f * D * p2);
                        oL += m.dRev * (rL - oL); oR += m.dRev * (rR - oR);
                    }
                    auto fL = dLpL.lowpass (dHpL.highpass (oL)), fR = dLpR.lowpass (dHpR.highpass (oR));
                    if (std::abs (m.dPitch) > 0.01f) { fL = fbPitchL.process (fL); fR = fbPitchR.process (fR); }
                    const auto sendIn = m.dSend * m.sendScale * 0.5f * (xL + xR) + toDelay;
                    dlyL.push (std::tanh (sendIn + m.dFb * fL));
                    dlyR.push (std::tanh (sendIn + m.dFb * fR));
                    wetL += oL * 0.8f; wetR += oR * 0.8f;
                    lastEchoL = oL; lastEchoR = oR;
                }

                // ---- reverb (with shimmer, freeze, stutter and kick ducking on the return) ----
                const auto verbFed = m.rSend > 0.0005f || m.shimmer > 0.0005f || std::abs (addVerbL) + std::abs (addVerbR) > 1.0e-7f;
                verbIdle = verbFed || m.rFreeze > 0.001f || std::abs (lastVerbL) + std::abs (lastVerbR) > 1.0e-5f ? 0 : juce::jmin (verbIdle + 1, 1 << 30);
                if (verbIdle == (int) (sr * 0.25)) { verb.reset(); lastVerbL = lastVerbR = 0.0f; }
                if (verbIdle < (int) (sr * 0.25))
                {
                    auto sendL = (m.rSend * m.sendScale * xL + addVerbL) * (1.0f - m.rFreeze), sendR = (m.rSend * m.sendScale * xR + addVerbR) * (1.0f - m.rFreeze);
                    if (m.shimmer > 0.001f)
                    {
                        const auto s = shimmerShift.process (0.5f * (lastVerbL + lastVerbR)) * m.shimmer * 0.45f;
                        sendL += s; sendR += s;
                    }
                    float vL, vR;
                    verb.process (sendL, sendR, vL, vR);
                    lastVerbL = vL; lastVerbR = vR;
                    auto ret = 1.0f;
                    if (m.rGate > 0.001f) ret *= 1.0f - m.rGate * (std::fmod (posQ * 4.0, 1.0) < 0.5 ? 0.0f : 1.0f);
                    if (m.rDuck > 0.001f)
                    {
                        const auto since = e.kickSinceMs + (float) (i * 1000.0 / sr);
                        ret *= 1.0f - m.rDuck * (since < 1.0e8f ? std::exp (-since / 90.0f) : 0.0f);
                    }
                    retSm += (ret - retSm) * 0.01f;
                    wetL += vL * retSm; wetR += vR * retSm;
                }

                // ---- additive layers: the drone and the weather ----
                if (m.drone > 0.001f)
                {
                    dronePhase += e.rootHz / sr; dronePhase -= std::floor (dronePhase);
                    const auto ph = (float) dronePhase;
                    const auto s = std::sin (ph * juce::MathConstants<float>::twoPi) + 0.25f * droneLp.lowpass (2.0f * std::fmod (ph * 2.0f, 1.0f) - 1.0f);
                    wetL += m.drone * 0.09f * s; wetR += m.drone * 0.09f * s;
                }
                if (m.weather > 0.001f)
                {
                    const auto nz = noise.process();
                    const auto rain = weatherCombs.process (nz * 0.05f);
                    const auto wind = windBp.bandpass (nz) * 0.3f;
                    wetL += m.weather * (rain + wind * 0.7f) * 0.25f;
                    wetR += m.weather * (rain * 0.8f + wind) * 0.25f;
                }

                xL += wetL * e.endFade; xR += wetR * e.endFade;

                // ---- level-matched: changes the sound, never the level (only ever turns down) ----
                {
                    const auto inP = 0.5 * ((double) inL * inL + (double) inR * inR), outP = 0.5 * ((double) xL * xL + (double) xR * xR);
                    levelIn += levelCoeff * (inP - levelIn);
                    levelOut += levelCoeff * (outP - levelOut);
                    levelInFast += levelCoeffFast * (inP - levelInFast);
                    levelOutFast += levelCoeffFast * (outP - levelOutFast);
                    const auto allowed = juce::jmax (levelIn, 0.004);
                    auto target = levelOut > allowed * 1.0233 ? (float) std::sqrt (allowed / levelOut) : 1.0f;
                    // a sudden pile-up (moves stacking after a gap) is caught in 50 ms, not 300: the fast
                    // output may only go 1 dB over the louder of the input's fast and slow levels
                    const auto allowedFast = juce::jmax (allowed, levelInFast) * 1.26;
                    if (levelOutFast > allowedFast) target = juce::jmin (target, (float) std::sqrt (allowedFast / levelOutFast));
                    levelGain += (target < levelGain ? levelAttack : levelRelease) * (target - levelGain);
                    if (levelGain > 0.9999f && target >= 1.0f) levelGain = 1.0f;
                    xL *= levelGain; xR *= levelGain;
                }

                // ---- amount ----
                L[i] = inL + e.amount * (xL - inL);
                R[i] = inR + e.amount * (xR - inR);
                if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { L[i] = R[i] = 0.0f; clearTails(); }
                lastInPeak = juce::jmax (lastInPeak, std::abs (inL), std::abs (inR));
                lastOutPeak = juce::jmax (lastOutPeak, std::abs (L[i]), std::abs (R[i]));
                ++absPos;
            }
        }

    private:
        //==============================================================================
        static float toward (float c, float t, float a) noexcept { return c + a * (t - c); }

        void glide (const Env&) noexcept
        {
            constexpr float F = 0.35f, M = 0.09f, S = 0.025f;
            auto& c = cur; const auto& t = tgt;
            c.gain = toward (c.gain, t.gain, F); c.gateDepth = toward (c.gateDepth, t.gateDepth, F);
            c.squeeze = toward (c.squeeze, t.squeeze, M); c.drive = toward (c.drive, t.drive, M); c.crush = toward (c.crush, t.crush, M);
            c.dropout = t.dropout; c.kickDuck = toward (c.kickDuck, t.kickDuck, M); c.tailGate = toward (c.tailGate, t.tailGate, M);
            c.gateKind = t.gateKind; c.gateRateQ = t.gateRateQ; c.gateMask = t.gateMask; c.euclidK = t.euclidK; c.polyQ = t.polyQ; c.crushOffbeat = t.crushOffbeat;
            c.playMix = toward (c.playMix, t.playMix, F); c.playAdd = t.playAdd || (c.playAdd && c.playMix > 0.002f && t.play == playNone);
            if (t.play != playNone || c.playMix < 0.002f)
            {
                c.play = t.play; c.playA = t.playA; c.playB = t.playB; c.playSeed = t.playSeed; c.playAnchor = t.playAnchor;
            }
            c.grainMix = toward (c.grainMix, t.grainMix, M); c.grainSizeMs = t.grainSizeMs; c.grainRate = t.grainRate;
            c.grainSpread = t.grainSpread; c.grainPitch = t.grainPitch; c.grainSpeed = t.grainSpeed;
            if (t.grainMix > 0.0f || c.grainMix < 0.002f) { c.grainMode = t.grainMode; c.grainAnchor = t.grainAnchor; c.grainAdd = t.grainAdd; }
            c.vibHeld = t.vibHeld; c.velColour = toward (c.velColour, t.velColour, M); c.sendScale = toward (c.sendScale, t.sendScale, S);
            c.pitch = toward (c.pitch, t.pitch, M); c.vib = toward (c.vib, t.vib, M); c.wow = toward (c.wow, t.wow, M);
            c.sag = toward (c.sag, t.sag, S); c.doppler = toward (c.doppler, t.doppler, S); c.dopplerPos = t.dopplerPos;
            for (size_t h = 0; h < 3; ++h) { c.harmMix[h] = toward (c.harmMix[h], t.harmMix[h], M); if (t.harmMix[h] > 0.0f) c.harmSemis[h] = t.harmSemis[h]; }
            c.harmDelayQ = t.harmDelayQ; c.harmToDelay = t.harmToDelay;
            c.octDown = toward (c.octDown, t.octDown, M); c.octUp = toward (c.octUp, t.octUp, M); c.bloomHeld = t.bloomHeld;
            c.detune = toward (c.detune, t.detune, M); c.fshift = toward (c.fshift, t.fshift, M); c.fshiftMix = toward (c.fshiftMix, t.fshiftMix, M);
            c.shepard = toward (c.shepard, t.shepard, M);
            c.hp = toward (c.hp, t.hp, M); c.lp = std::exp (toward (std::log (c.lp), std::log (t.lp), M)); c.lpQ = toward (c.lpQ, t.lpQ, M);
            c.vowel = toward (c.vowel, t.vowel, M); c.vowelMix = toward (c.vowelMix, t.vowelMix, M); c.phone = toward (c.phone, t.phone, F);
            c.combMix = toward (c.combMix, t.combMix, M); c.overtone = t.overtone; c.overtoneMix = toward (c.overtoneMix, t.overtoneMix, M);
            c.peakHz = toward (c.peakHz, t.peakHz, M); c.peakDb = toward (c.peakDb, t.peakDb, M);
            c.bright = toward (c.bright, t.bright, M); c.air = toward (c.air, t.air, M);
            c.shapeMix = toward (c.shapeMix, t.shapeMix, S); c.shapeRole = t.shapeRole;
            c.keyTrack = t.keyTrack; c.keyTrackAmt = toward (c.keyTrackAmt, t.keyTrackAmt, M);
            c.width = toward (c.width, t.width, M); c.pan = toward (c.pan, t.pan, M); c.rotateHz = toward (c.rotateHz, t.rotateHz, S);
            c.rotateDepth = toward (c.rotateDepth, t.rotateDepth, M); c.haas = toward (c.haas, t.haas, S);
            c.dSend = toward (c.dSend, t.dSend, M); c.dTimeQ = toward (c.dTimeQ, t.dTimeQ, 0.004f); c.dFb = toward (c.dFb, t.dFb, M);
            c.dPitch = t.dPitch; c.dLp = toward (c.dLp, t.dLp, M); c.dHp = toward (c.dHp, t.dHp, M); c.dRev = toward (c.dRev, t.dRev, M);
            c.rSend = toward (c.rSend, t.rSend, M); c.rDecay = toward (c.rDecay, t.rDecay, S); c.rSize = toward (c.rSize, t.rSize, S);
            c.rDamp = toward (c.rDamp, t.rDamp, S); c.shimmer = toward (c.shimmer, t.shimmer, M); c.rFreeze = toward (c.rFreeze, t.rFreeze, M);
            c.rGate = toward (c.rGate, t.rGate, M); c.rDuck = toward (c.rDuck, t.rDuck, M);
            c.drone = toward (c.drone, t.drone, S); c.weather = toward (c.weather, t.weather, S);
            // the delay's time: a long way at once is a jump (moves start on their own time), a little is elastic
            if (std::abs (t.dTimeQ - c.dTimeQ) > 0.6f && c.dSend < 0.02f) c.dTimeQ = t.dTimeQ;
        }

        static bool changed (float a, float b, float rel = 0.01f) noexcept { return std::abs (a - b) > rel * juce::jmax (1.0f, std::abs (b)); }

        void updateCoefficients (const Env& e) noexcept
        {
            const auto fs = (float) sr;
            const auto& m = cur;
            auto& o = lastCoeff;
            const auto first = ! coeffInit;
            coeffInit = true;
            if (first || changed (m.hp, o.hp)) { hpL.set (m.hp, 0.707f, fs); hpR.set (m.hp, 0.707f, fs); o.hp = m.hp; }
            auto lp = m.lp;
            if (m.keyTrack && pitchHz > 40.0f) lp = juce::jmin (lp, juce::jlimit (300.0f, 18000.0f, pitchHz * (2.0f + 10.0f * (1.0f - m.keyTrackAmt))));
            if (first || changed (lp, o.lp) || changed (m.lpQ, o.lpQ)) { lpL.set (juce::jlimit (40.0f, 0.45f * fs, lp), m.lpQ, fs); lpR.set (juce::jlimit (40.0f, 0.45f * fs, lp), m.lpQ, fs); o.lp = lp; o.lpQ = m.lpQ; }
            if (m.vowelMix > 0.001f && (first || changed (m.vowel, o.vowel, 0.002f)))
            {
                // a -> o -> u as the vowel goes 0 -> 1
                static constexpr float f[3][3] = { { 800, 1150, 2900 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const auto v = juce::jlimit (0.0f, 1.0f, m.vowel) * 2.0f;
                const auto a = juce::jmin (1, (int) v), b = juce::jmin (2, a + 1);
                const auto fr = v - (float) a;
                for (size_t k = 0; k < 3; ++k)
                {
                    const auto hz = f[a][k] + fr * (f[b][k] - f[a][k]);
                    vowelL[k].set (hz, 7.0f, fs); vowelR[k].set (hz, 7.0f, fs);
                }
                o.vowel = m.vowel;
            }
            if (first) { phHpL.set (450.0f, 0.8f, fs); phHpR.set (450.0f, 0.8f, fs); phLpL.set (2800.0f, 0.9f, fs); phLpR.set (2800.0f, 0.9f, fs); }
            if (m.overtoneMix > 0.001f)
            {
                const auto hz = juce::jlimit (30.0f, 0.4f * fs, e.rootHz * 2.0f * (1.0f + std::floor (m.overtone * 15.0f)));
                if (first || changed (hz, o.overtone)) { otL.set (hz, 14.0f, fs); otR.set (hz, 14.0f, fs); o.overtone = hz; }
            }
            if (std::abs (m.peakDb) > 0.05f && (first || changed (m.peakHz, o.peakHz) || changed (m.peakDb, o.peakDb)))
            { peakL.setPeak (m.peakHz, 2.5f, m.peakDb, fs); peakR.setPeak (m.peakHz, 2.5f, m.peakDb, fs); o.peakHz = m.peakHz; o.peakDb = m.peakDb; }
            auto bright = m.bright;
            if (m.velColour > 0.001f) bright += m.velColour * juce::jlimit (-4.0f, 4.0f, 6.0f * (env.value() / (slowEnv.value() + 1.0e-5f) - 1.0f));
            if (first || changed (bright, o.bright, 0.02f)) { brL.setHighShelf (3000.0f, bright, fs); brR.setHighShelf (3000.0f, bright, fs); o.bright = bright; }
            if (first || changed (m.air, o.air, 0.02f)) { airL.setHighShelf (10000.0f, m.air, fs); airR.setHighShelf (10000.0f, m.air, fs); o.air = m.air; }
            if (m.shapeMix > 0.001f)
            {
                const auto& c = ::analysis::bandCentres();
                for (size_t b = 0; b < shapeL.size(); ++b)
                {
                    const auto db = juce::jlimit (-9.0f, 9.0f, e.shapeDb[b] * m.shapeMix);
                    shapeL[b].setPeak (c[b], 1.4f, db, fs); shapeR[b].setPeak (c[b], 1.4f, db, fs);
                }
            }
            if (m.combMix > 0.001f || first) combs.tune (e.chordHz, 0.93f);
            if (m.weather > 0.001f || first)
            {
                weatherCombs.tune ({ e.chordHz[0] * 2.0f, e.chordHz[1] * 2.0f, e.chordHz[2] * 2.0f }, 0.985f);
                windPhase += subBlock / sr * 0.07; windPhase -= std::floor (windPhase);
                windBp.set (300.0f + 1200.0f * (0.5f + 0.5f * (float) std::sin (windPhase * juce::MathConstants<double>::twoPi)), 1.5f, fs);
            }
            if (first) droneLp.set (300.0f, 0.7f, fs);
            // pitch voices
            pitchL.setSemitones (m.pitch); pitchR.setSemitones (m.pitch);
            for (size_t h = 0; h < 3; ++h) harm[h].setSemitones (m.harmMix[h] > 0.0005f ? m.harmSemis[h] : 0.0f);
            octD.setSemitones (m.octDown > 0.0005f ? -12.0f : 0.0f); octU.setSemitones (m.octUp > 0.0005f ? 12.0f : 0.0f);
            const auto cents = 0.04f + 0.25f * m.detune;
            detL.setSemitones (m.detune > 0.0005f ? cents : 0.0f); detR.setSemitones (m.detune > 0.0005f ? -cents : 0.0f);
            if (m.shepard <= 0.001f) { shepA.setSemitones (0.0f); shepB.setSemitones (0.0f); }
            fbPitchL.setSemitones (m.dPitch); fbPitchR.setSemitones (m.dPitch);
            shimmerShift.setSemitones (m.shimmer > 0.0005f ? 12.0f : 0.0f);
            if (first || changed (m.dLp, o.dLp) || changed (m.dHp, o.dHp))
            { dLpL.set (m.dLp, 0.707f, fs); dLpR.set (m.dLp, 0.707f, fs); dHpL.set (m.dHp, 0.707f, fs); dHpR.set (m.dHp, 0.707f, fs); o.dLp = m.dLp; o.dHp = m.dHp; }
            verb.setParameters (m.rFreeze > 0.5f ? 60.0f : juce::jlimit (0.3f, 20.0f, m.rDecay), juce::jlimit (0.05f, 1.0f, m.rSize), juce::jlimit (800.0f, 16000.0f, m.rDamp), 0.4f);
        }

        /** Every listener the director's audio profile and the moves need, on the dry input. */
        void listen (float inL, float inR, double posQ, double barQ) noexcept
        {
            const auto mono = 0.5f * (inL + inR);
            const auto fast = onsetFast.process (mono), slow = onsetSlow.process (mono);
            ++sinceOnset;
            if (fast > 1.8f * slow + 1.0e-4f && fast > 0.003f && sinceOnset > (int) (0.06 * sr))
            {
                sinceOnset = 0;
                ++stats.onsets;
                const auto bar = (int) std::floor (posQ / barQ);
                const auto sixteenth = juce::jlimit (0, 15, (int) (std::fmod (posQ, barQ) / 0.25));
                if (bar != lastBar) { onsetMaskLast = onsetMaskNow; onsetMaskNow = 0; lastBar = bar; }
                onsetMaskNow |= (uint16_t) (1u << sixteenth);
            }
            // held: a note sounding steadily for half a beat or more
            const auto steady = slow > 0.003f && fast > 0.6f * slow;
            heldSamples = steady ? heldSamples + 1 : 0;
            held += ((heldSamples > (int) (0.25 * sr) ? 1.0f : 0.0f) - held) * 0.0005f;
            stats.energy += (double) mono * mono;
            const auto d = mono - lastMono; lastMono = mono;
            stats.diff += (double) d * d;
            const auto side = 0.5f * (inL - inR);
            stats.side += (double) side * side;
            ++stats.samples;
            if (steady) ++stats.heldSamples;
            // a cheap pitch: positive zero crossings of the low-passed input
            const auto lpx = pitchLp.lowpass (mono);
            ++zcCount;
            if (lastLp <= 0.0f && lpx > 0.0f && slow > 0.003f)
            {
                if (zcCount > 8 && zcCount < 2000) pitchHz += ((float) sr / (float) zcCount - pitchHz) * 0.05f;
                zcCount = 0;
            }
            lastLp = lpx;
            // decimated input for the pitchedness check
            decimAcc += mono; ++decimN;
            if (decimN == 4)
            {
                const auto w = decimWrite.load (std::memory_order_relaxed);
                decim[(size_t) (w & (decimSize - 1))] = decimAcc * 0.25f;
                decimWrite.store (w + 1, std::memory_order_release);
                decimAcc = 0.0f; decimN = 0;
            }
        }

        float squeezeGain (float xL, float xR) noexcept
        {
            const auto peak = juce::jmax (std::abs (xL), std::abs (xR));
            const auto e = env.process (peak);
            const auto ref = slowEnv.process (peak) + 1.0e-5f;
            if (cur.squeeze < 0.001f || e <= ref) return 1.0f;
            const auto ratio = 1.0f + 9.0f * cur.squeeze;
            return std::pow (e / ref, -(1.0f - 1.0f / ratio)) * (1.0f + 0.5f * cur.squeeze);
        }

        float gateGain (double posQ, const Env& e) noexcept
        {
            const auto& m = cur;
            if (m.gateKind == gateNone || m.gateDepth < 0.001f) return 1.0f;
            bool open = true;
            switch (m.gateKind)
            {
                case gateStraight: open = std::fmod (posQ, (double) m.gateRateQ) < 0.5 * m.gateRateQ; break;
                case gatePoly:     open = std::fmod (posQ, (double) m.polyQ) < 0.5 * m.polyQ; break;
                case gateAccel:
                    accelPhase += 1.0 / (e.samplesPerQ * juce::jmax (0.02f, m.gateRateQ));
                    accelPhase -= std::floor (accelPhase);
                    open = accelPhase < 0.5;
                    break;
                case gateEuclid:
                {
                    const auto step = (int) (std::fmod (posQ, e.barQ) / 0.25) % 16;
                    const auto k = juce::jlimit (1, 15, m.euclidK);
                    open = ((step * k) % 16) < k;   // Bresenham: k hits spread over 16 steps
                    open = open && std::fmod (posQ, 0.25) < 0.17;
                    break;
                }
                case gateMask:
                {
                    const auto step = (int) (std::fmod (posQ, e.barQ) / 0.25) % 16;
                    open = (m.gateMask >> step) & 1u;
                    open = open && std::fmod (posQ, 0.25) < 0.2;
                    break;
                }
                case gateKick: open = e.kickSinceMs > 60.0f; break;
                default: break;
            }
            return open ? 1.0f : 1.0f - m.gateDepth;
        }

        float ringRead (const std::vector<float>& ring, double pos) const noexcept
        {
            pos = juce::jmin (pos, (double) absPos);   // (the sample written this instant at most)
            const auto i = (int64_t) std::floor (pos);
            const auto f = (float) (pos - (double) i);
            const auto a = ring[(size_t) (i & ringMask)], b = ring[(size_t) ((i + 1) & ringMask)];
            return a + f * (b - a);
        }
        float memRead (const std::vector<float>& mem, double pos) const noexcept
        {
            if (memLen <= 1) return 0.0f;
            pos = std::fmod (pos, (double) memLen);
            if (pos < 0.0) pos += memLen;
            const auto i = (int) pos;
            const auto f = (float) (pos - i);
            return mem[(size_t) i] + f * (mem[(size_t) ((i + 1) % memLen)] - mem[(size_t) i]);
        }
        static float edgeFade (double offset, double len, double fadeLen) noexcept
        {
            return (float) juce::jlimit (0.0, 1.0, juce::jmin (offset / fadeLen, (len - offset) / fadeLen));
        }

        void player (float& oL, float& oR, double posQ, const Env& e, double barSamples) noexcept
        {
            const auto& m = cur;
            const auto nowD = (double) absPos;
            const auto anchor = (double) m.playAnchor;
            const auto since = juce::jmax (0.0, nowD - anchor);
            const auto fade = 0.002 * sr;
            if (m.playAnchor != playLastAnchor)
            {
                playLastAnchor = m.playAnchor;
                playPos = anchor; loopPhase = 0.0; swingD = 0.0;
            }
            double pos = nowD;
            float g = 1.0f;
            bool mem = false;
            switch (m.play)
            {
                case playRepeat:
                {
                    const auto L = juce::jmax (64.0, m.playA);
                    const auto off = std::fmod (since, L);
                    pos = anchor + off;
                    g = edgeFade (off, L, fade);
                    break;
                }
                case playReverse:   pos = anchor - since - 1.0; g = edgeFade (since, m.playA > 0.0 ? m.playA : 1.0e9, fade); break;
                case playTapeStop:
                {
                    playPos += juce::jlimit (0.0, 1.0, m.playB);
                    pos = playPos;
                    break;
                }
                case playRewind:    playPos -= juce::jmax (1.0, m.playB); pos = playPos; break;
                case playHalftime:  pos = anchor + since * 0.5; break;
                case playShuffle:
                {
                    const auto L = juce::jmax (64.0, m.playA);
                    const auto k = (int) (since / L);
                    const auto slices = juce::jmax (1, (int) std::round (barSamples / L));
                    auto src = k % slices;
                    const auto keep = (uint32_t) m.playB;   // slices that stay put (kicks, the last one)
                    if (src != slices - 1 && ((keep >> (src & 31)) & 1u) == 0u)
                        src = (int) ((uint32_t) (src * 7 + 3) * 2654435761u ^ m.playSeed) % juce::jmax (1, slices - 1);
                    const auto off = since - k * L;
                    pos = anchor - barSamples + src * L + off;
                    g = edgeFade (off, L, fade);
                    break;
                }
                case playSwing:
                {
                    const auto sixteenth = 0.25 * e.samplesPerQ;
                    const auto second = std::fmod (posQ, 0.5) >= 0.25;
                    swingD += ((second ? m.playA * sixteenth * 0.33 : 0.0) - swingD) * 0.002;
                    pos = nowD - 1.0 - swingD;
                    break;
                }
                case playMemory:
                {
                    mem = true;
                    const auto L = juce::jmax (256.0, m.playB);
                    const auto off = std::fmod (since, L);
                    const auto reverse = (m.playSeed & 1u) != 0u;
                    pos = m.playA + (reverse ? L - off : off);
                    g = edgeFade (off, L, 0.01 * sr);
                    break;
                }
                case playMemShuffle:
                {
                    mem = true;
                    const auto L = juce::jmax (64.0, m.playA);
                    const auto k = (int) (since / L);
                    const auto slices = juce::jmax (1, (int) (memLen / L));
                    const auto src = (int) (((uint32_t) (k * 13 + 5) * 2654435761u ^ m.playSeed) % (uint32_t) slices);
                    const auto off = since - k * L;
                    pos = src * L + off;
                    g = edgeFade (off, L, fade);
                    break;
                }
                case playTapeLoop:
                {
                    const auto L = juce::jmax (256.0, barSamples);
                    loopPhase += juce::jlimit (0.5, 1.0, m.playB);
                    const auto off = std::fmod (loopPhase, L);
                    pos = anchor - L + off;
                    g = edgeFade (off, L, 0.005 * sr);
                    break;
                }
                default: break;
            }
            if (mem && memLen <= 0) { oL = oR = 0.0f; return; }
            oL = g * (mem ? memRead (memL, pos) : ringRead (ringL, pos));
            oR = g * (mem ? memRead (memR, pos) : ringRead (ringR, pos));
        }

        //==============================================================================
        struct Grain { double pos = 0, inc = 1; int len = 0, age = 0; float gl = 1, gr = 1; bool mem = false, active = false; };
        void grainsProcess (float& oL, float& oR) noexcept
        {
            const auto& m = cur;
            const auto size = juce::jlimit (0.01, 1.0, (double) m.grainSizeMs * 0.001) * sr;
            spawnAcc += m.grainRate / sr;
            while (spawnAcc >= 1.0)
            {
                spawnAcc -= 1.0;
                for (auto& g : grains)
                    if (! g.active)
                    {
                        const auto jit = rng.nextDouble() * 0.03 * sr;
                        const auto semis = m.grainPitch + m.grainSpread * (rng.nextFloat() * 2.0f - 1.0f);
                        g.inc = std::exp2 (semis / 12.0);
                        g.len = (int) size;
                        g.age = 0;
                        g.mem = m.grainMode == grainMemory;
                        const auto span = size * juce::jmax (1.0, g.inc);
                        switch (m.grainMode)
                        {
                            case grainFrozen: case grainChord: g.pos = (double) m.grainAnchor - span - jit; break;
                            case grainStretch: g.pos = (double) m.grainAnchor + ((double) absPos - (double) m.grainAnchor) * m.grainSpeed - span - jit; break;
                            case grainMemory: g.pos = (double) m.grainAnchor + rng.nextDouble() * juce::jmax (1.0, (double) memLen - span); break;
                            default: g.pos = (double) absPos - span - jit; break;
                        }
                        const auto p = rng.nextFloat() * 0.8f - 0.4f;
                        g.gl = 1.0f - juce::jmax (0.0f, p); g.gr = 1.0f + juce::jmin (0.0f, p);
                        g.active = true;
                        break;
                    }
            }
            float sL = 0.0f, sR = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const auto w = std::sin (suite::pi * (float) g.age / (float) g.len);
                const auto gain = w * w;
                const auto l = g.mem ? memRead (memL, g.pos) : ringRead (ringL, g.pos);
                const auto r = g.mem ? memRead (memR, g.pos) : ringRead (ringR, g.pos);
                sL += gain * g.gl * l; sR += gain * g.gr * r;
                g.pos += g.inc;
                if (++g.age >= g.len) g.active = false;
            }
            const auto overlap = juce::jmax (1.0f, m.grainRate * (float) (size / sr));
            const auto norm = 1.0f / std::sqrt (overlap) * 1.2f;
            oL = sL * norm; oR = sR * norm;
        }

        //==============================================================================
        double sr = 48000.0;
        Mod cur, lastCoeff;
        bool coeffInit = false;
        int64_t absPos = 0;
        std::vector<float> ringL, ringR, memL, memR;
        int memCapacity = 0, memLen = 0, memWrite = 0;
        bool memRecording = false;

        suite::PitchShifter pitchL, pitchR, detL, detR, octD, octU, shepA, shepB, fbPitchL, fbPitchR, shimmerShift;
        std::array<suite::PitchShifter, 3> harm;
        std::array<suite::DelayLine, 3> harmDelay;
        suite::DelayLine tapeL, tapeR, haasLine, dlyL, dlyR;
        suite::FdnReverb verb;
        Resonators combs, weatherCombs;
        Hilbert hilbert;
        suite::Svf hpL, hpR, lpL, lpR, phHpL, phHpR, phLpL, phLpR, otL, otR, windBp, droneLp, dHpL, dHpR, dLpL, dLpR;
        std::array<suite::Svf, 3> vowelL, vowelR;
        suite::Biquad peakL, peakR, brL, brR, airL, airR;
        std::array<suite::Biquad, 10> shapeL, shapeR;
        suite::Noise noise { 0x51a7e5u };
        suite::EnvelopeFollower onsetFast, onsetSlow, env, slowEnv;
        suite::OnePole pitchLp;
        std::array<Grain, 24> grains {};
        juce::Random rng { 0x7a11 };
        double spawnAcc = 0.0;

        suite::OnePole memLpL, memLpR;
        float addVerbL = 0.0f, addVerbR = 0.0f;
        int echoIdle = 1 << 30, verbIdle = 1 << 30;
        float lastEchoL = 0.0f, lastEchoR = 0.0f;
        float gainSm = 1.0f, retSm = 1.0f, lastVerbL = 0.0f, lastVerbR = 0.0f, tailEnv = 1.0f;
        double levelIn = 0.0, levelOut = 0.0, levelInFast = 0.0, levelOutFast = 0.0, levelCoeffFast = 0.0;
        float levelGain = 1.0f;
        double levelCoeff = 1.0 / (0.3 * 48000.0);
        float levelAttack = 0.001f, levelRelease = 0.0001f;
        int64_t playLastAnchor = -1;
        double playPos = 0.0, loopPhase = 0.0, swingD = 0.0;
        double accelPhase = 0.0, rotPhase = 0.0, vibPhase = 0.0, wowPhase = 0.0, fsPhase = 0.0, shepPhase = 0.0, dronePhase = 0.0, windPhase = 0.0, dRevPhase = 0.0;
        float crushHoldL = 0.0f, crushHoldR = 0.0f;
        int crushCount = 0;

        // listening
        Stats stats;
        int heldSamples = 0, sinceOnset = 1 << 30, onsetCount = 0, zcCount = 0, decimN = 0, lastBar = -1;
        float held = 0.0f, pitchHz = 0.0f, lastMono = 0.0f, lastLp = 0.0f, decimAcc = 0.0f;
        uint16_t onsetMaskNow = 0, onsetMaskLast = 0;
    };
}
