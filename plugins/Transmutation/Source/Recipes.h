#pragma once

// How each of the 118 moves drives the engine at a point in time. Every move playing now adds
// its wishes to one Mod: levels multiply, cuts take the stronger, sends and mixes the larger,
// pitch adds, and the buffer player / gate go to the move that started last.

#include "Effects.h"
#include "Engine.h"
#include "Music.h"
#include <cmath>

namespace trm
{
    /** One planned move on this track. */
    struct Move
    {
        float startQ = 0.0f, endQ = 0.0f;   // song position, quarter notes
        int effect = 0;
        float amount = 1.0f;                // how far it goes (spice, story arc, vocal priority)
        uint32_t seed = 0;
        uint32_t key = 0;                   // the moment it belongs to (lock and re-roll)
        uint32_t slot = 0;                  // the kind of moment (a Slot bit)
        uint8_t flags = 0;                  // see MoveFlags
    };
    enum MoveFlags : uint8_t { mfLocked = 1, mfAlways = 2, mfAnswer = 4, mfMotif = 8, mfAudition = 16, mfSignature = 32, mfSuite = 64 };   // mfSuite: the suite director asked for it

    /** The song around a move, now. */
    struct Ctx
    {
        double t = 0.0, q = 0.0, lenQ = 1.0, posQ = 0.0, barQ = 4.0, samplesPerQ = 24000.0;
        float amt = 1.0f;
        int role = 0;
        int sectionType = 0;                 // suite::SectionType
        double sectionProgress = 0.0;
        bool breakPeak = false;              // in the last 2 bars of a breakdown
        float energy = 0.6f;                 // the energy curve here (0..1), smoothed toward the next section
        double phraseQ = 0.0, phraseLenQ = 32.0, chordQ = 0.0;
        bool chordTension = false, chordMinor = true;
        int third = 3, fifth = 7, scale = 0, finalLift = 0;
        float rootHz = 55.0f, leadHz = 0.0f, pitchHz = 0.0f;
        uint16_t ownOnsets = 0, borrowedOnsets = 0, kickMask = 0;
        int memLen = 0;
        int64_t anchor = 0, chordAnchor = 0;
        bool vocalNow = false, nextIsDrop = false;
        float held = 0.0f;
    };

    namespace detail
    {
        inline float smooth (double x) { const auto c = (float) juce::jlimit (0.0, 1.0, x); return c * c * (3.0f - 2.0f * c); }
        inline float hashUnit (uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return (float) (h & 0xffffu) / 65535.0f; }
        inline float rollRate (double t) { return t < 0.5 ? 0.5f : t < 0.75 ? 0.25f : t < 0.9 ? 0.125f : 0.0625f; }
        inline void lowerLp (Mod& m, float hz) { m.lp = juce::jmin (m.lp, juce::jlimit (60.0f, 20000.0f, hz)); }
        inline void raiseHp (Mod& m, float hz) { m.hp = juce::jmax (m.hp, juce::jlimit (20.0f, 4000.0f, hz)); }
        inline void play (Mod& m, int mode, const Ctx& c, double a = 0.0, double b = 0.0, float mix = 1.0f, bool add = false)
        {
            m.play = mode; m.playA = a; m.playB = b; m.playMix = juce::jmax (m.playAdd == add ? m.playMix : 0.0f, mix);
            m.playAnchor = c.anchor; m.playAdd = add;
        }
        inline void grains (Mod& m, int mode, int64_t anchor, float mix, float sizeMs, float rate, float spread, bool add)
        {
            m.grainMode = mode; m.grainAnchor = anchor; m.grainMix = juce::jmax (m.grainMix, mix);
            m.grainSizeMs = sizeMs; m.grainRate = rate; m.grainSpread = spread; m.grainAdd = add;
        }
        inline void gate (Mod& m, int kind, float depth, float rateQ = 0.25f)
        {
            m.gateKind = kind; m.gateDepth = juce::jmax (m.gateDepth, juce::jlimit (0.0f, 1.0f, depth)); m.gateRateQ = rateQ;
        }
        inline void echo (Mod& m, float send, float timeQ, float fb, float lp = 7000.0f, float hp = 200.0f)
        {
            m.dSend = juce::jmax (m.dSend, send); m.dTimeQ = timeQ; m.dFb = juce::jmax (m.dFb, fb); m.dLp = lp; m.dHp = hp;
        }
        inline void room (Mod& m, float send, float decay, float size, float damp = 8000.0f)
        {
            m.rSend = juce::jmax (m.rSend, send); m.rDecay = decay; m.rSize = size; m.rDamp = damp;
        }
        /** In and out over `fadeQ` at the move's ends. */
        inline float ends (const Ctx& c, double fadeQ)
        {
            return (float) juce::jlimit (0.0, 1.0, juce::jmin (c.q / fadeQ, (c.lenQ - c.q) / fadeQ));
        }
    }

    inline void applyRecipe (const Move& mv, const Ctx& c, Mod& m)
    {
        using namespace detail;
        const auto t = c.t, q = c.q;
        const auto a = juce::jlimit (0.0f, 1.6f, c.amt);
        const auto spq = c.samplesPerQ;
        const auto bar = c.barQ;
        const auto lastBar = juce::jlimit (0.0, 1.0, (q - (c.lenQ - bar)) / bar);
        using ST = int;
        constexpr ST tIntro = 0, tGroove = 1, tBreak = 2, tBuild = 3, tDrop = 4, tOutro = 5;
        juce::ignoreUnused (tIntro, tOutro, tGroove);

        switch (mv.effect)
        {
            // ======== A. Builds, drops and breakdowns ========
            case 1:
            {
                const auto p = (float) (t * t);
                switch (c.role)
                {
                    case 1: raiseHp (m, 20.0f + 130.0f * smooth ((t - 0.75) * 4.0) * a); break;
                    case 2: raiseHp (m, 20.0f * std::pow (10.0f, p * a)); m.gain *= 1.0f - 0.5f * a * smooth (lastBar * 2.0); break;
                    case 4: lowerLp (m, 1800.0f * std::pow (11.0f, (float) t)); m.width *= 1.0f + 0.6f * (float) t * a; room (m, 0.25f * (float) t * a, 2.5f, 0.7f); break;
                    case 5: case 6: echo (m, 0.45f * (float) t * a, 0.75f, 0.35f + 0.3f * (float) t); raiseHp (m, 20.0f * std::pow (15.0f, p)); break;
                    case 3: gate (m, gateStraight, 0.6f * (float) t * a, rollRate (t)); raiseHp (m, 20.0f * std::pow (30.0f, p * a)); break;
                    default: raiseHp (m, 20.0f * std::pow (40.0f, p * a)); room (m, 0.4f * p * a, 3.0f, 0.8f); m.pitch += 3.0f * p * a; break;
                }
                break;
            }
            case 2:
            {
                static const float start[8] = { 0.3f, 0.75f, 0.5f, 0.1f, 0.25f, 0.4f, 0.6f, 0.0f };
                static const float top[8] = { 500.0f, 200.0f, 350.0f, 1500.0f, 700.0f, 400.0f, 250.0f, 1000.0f };
                const auto r = juce::jlimit (0, 7, c.role);
                const auto u = juce::jlimit (0.0, 1.0, (t - start[r]) / (1.0 - start[r]));
                raiseHp (m, 20.0f * std::pow (top[r] / 20.0f, (float) std::pow (u, 1.5) * juce::jmin (1.0f, a)));
                break;
            }
            case 3: m.shepard = juce::jmax (m.shepard, 0.6f * a * (mv.slot == sBuildEnd ? 1.0f : smooth ((t - 0.5) * 2.0))); break;
            case 4:
            {
                play (m, playTapeStop, c, 0.0, std::pow (1.0 - t, 1.5));
                lowerLp (m, 20000.0f * std::pow (0.04f, (float) t));
                if (t > 0.9) m.gain *= (float) (1.0 - (t - 0.9) * 10.0);
                break;
            }
            case 5: m.gain = 0.0f; break;
            case 6: m.width *= 1.0f + 1.2f * a * (float) ((1.0 - t) * (1.0 - t)); m.haas = juce::jmax (m.haas, 12.0f * (float) (1.0 - t) * a); break;
            case 31: m.gain *= 1.0f - 0.45f * a * (float) std::pow (t, 1.5); lowerLp (m, 20000.0f * std::pow (0.1f, (float) t * a)); break;
            case 32: play (m, playRewind, c, 0.0, 2.5, t < 0.85 ? 1.0f : (float) ((1.0 - t) / 0.15)); lowerLp (m, 6000.0f); break;
            case 33: m.squeeze = juce::jmax (m.squeeze, 0.9f * a * (float) t); break;
            case 34:
                if (c.role == 1 || c.role == 2) m.gain = 0.0f;
                else { lowerLp (m, 900.0f); room (m, 0.4f, 4.0f, 0.9f); m.gain *= 0.6f; m.width *= 1.3f; }
                break;
            case 35: gate (m, gateStraight, 0.5f * a * juce::jmin (1.0f, (float) t * 1.5f), rollRate (t)); break;
            case 36: m.harmSemis[0] = 12.0f * (float) (t * t); m.harmMix[0] = juce::jmax (m.harmMix[0], 0.45f * a * (float) t); lowerLp (m, 1500.0f * std::pow (13.0f, (float) t)); break;
            case 37: echo (m, q < 1.0 ? 0.6f * a : 0.0f, 0.75f, 0.55f, 3500.0f); break;
            case 38:
                if (q < 1.0) play (m, playReverse, c, spq);
                m.gain *= 0.2f + 0.8f * (float) t;
                room (m, 0.4f * (float) (1.0 - t), 3.0f, 0.8f);
                break;
            case 39: m.drive = juce::jmax (m.drive, 0.25f * a); m.width *= 1.12f; m.air += 2.0f * a; break;
            case 40:
                grains (m, grainLive, 0, 0.8f * (float) t * a, 120.0f, 25.0f, 0.3f, false);
                echo (m, 0.35f * (float) t, 0.75f, 0.45f);
                m.dropout = juce::jmax (m.dropout, 0.35f * (float) t); m.playSeed = mv.seed;
                lowerLp (m, 20000.0f * std::pow (0.15f, (float) t));
                break;

            // ======== B. Rhythm and glitch ========
            case 7:
            {
                const auto rate = c.lenQ > 2.0 ? (t < 0.25 ? 1.0f : rollRate ((t - 0.25) / 0.75)) : rollRate (t);
                play (m, playRepeat, c, rate * spq);
                break;
            }
            case 8: play (m, playReverse, c, c.lenQ * spq); break;
            case 9:
                if (c.role == 3) { m.gateKind = gateEuclid; m.euclidK = 3 + 2 * (int) (mv.seed % 3u); m.gateDepth = juce::jmax (m.gateDepth, 0.55f * a); }
                else { m.gateKind = gatePoly; m.polyQ = (mv.seed & 1u) ? 0.75f : 1.5f; m.gateDepth = juce::jmax (m.gateDepth, 0.5f * a); }
                break;
            case 10: play (m, playShuffle, c, 0.25 * spq, (double) (c.kickMask | 0x8000u)); m.playSeed = mv.seed; break;
            case 11: play (m, playHalftime, c, 0.0, 0.0, t < 0.95 ? 1.0f : (float) ((1.0 - t) * 20.0)); break;
            case 41: play (m, playSwing, c, 0.6 * t * a); break;
            case 42: m.gateKind = gateMask; m.gateMask = c.ownOnsets != 0 ? c.ownOnsets : (uint16_t) 0x5555; m.gateDepth = juce::jmax (m.gateDepth, 0.8f * a); break;
            case 43: play (m, playShuffle, c, 0.25 * spq, 0.0); m.playSeed = mv.seed; break;
            case 44: m.crush = juce::jmax (m.crush, 0.45f * a); m.crushOffbeat = true; break;
            case 45: echo (m, 0.15f * a, 1.0f / 3.0f, 0.3f, 3000.0f); break;
            case 46: m.dropout = juce::jmax (m.dropout, 0.3f * a); m.playSeed = mv.seed; break;
            case 47: m.kickDuck = juce::jmax (m.kickDuck, 0.4f * a); break;
            case 48: gate (m, gateAccel, 0.6f * a * juce::jmin (1.0f, 2.0f * (float) t), 0.5f * std::pow (0.125f, (float) t)); break;

            // ======== C. Pitch and harmony ========
            case 12: echo (m, q < 1.0 ? 0.45f * a : 0.0f, 0.5f, 0.6f, 6000.0f); m.dPitch = (float) c.third; break;
            case 13:
                m.harmSemis[0] = (float) c.third; m.harmSemis[1] = (float) c.fifth;
                m.harmMix[0] = juce::jmax (m.harmMix[0], 0.3f * a * ends (c, bar)); m.harmMix[1] = juce::jmax (m.harmMix[1], 0.25f * a * ends (c, bar));
                m.harmDelayQ = 0.75f; m.harmToDelay = false;
                echo (m, 0.12f * a, 0.75f, 0.35f);
                break;
            case 14:
                if (c.role != 2) m.octDown = juce::jmax (m.octDown, 0.35f * a);
                m.octUp = juce::jmax (m.octUp, 0.3f * a); m.bloomHeld = true;
                break;
            case 15: m.combMix = juce::jmax (m.combMix, 0.4f * a * ends (c, 1.0)); break;
            case 16: m.fshift = c.rootHz * 2.0f * (1.0f + std::floor ((float) t * 3.0f)); m.fshiftMix = juce::jmax (m.fshiftMix, 0.55f * a * (float) t); break;
            case 49: m.harmSemis[1] = (float) c.third; m.harmMix[1] = juce::jmax (m.harmMix[1], 0.3f * a * ends (c, bar)); break;
            case 50: m.pitch += -12.0f * smooth (t) * smooth (t); m.gain *= 1.0f - 0.7f * (float) t; break;
            case 51:
            {
                auto target = 2.0f;
                if (c.leadHz > 30.0f)
                {
                    const auto note = 69.0f + 12.0f * std::log2 (c.leadHz / 440.0f);
                    const auto rootPc = (float) ((((int) std::round (12.0f * std::log2 (c.rootHz / 440.0f)) + 69 + c.finalLift) % 12 + 12) % 12);
                    auto up = std::fmod (rootPc - std::fmod (note, 12.0f) + 24.0f, 12.0f);
                    if (up < 1.0f) up += 12.0f;
                    target = up;
                }
                m.pitch += target * (float) (t * t);
                break;
            }
            case 52: m.detune = juce::jmax (m.detune, a * (float) t); break;
            case 53: m.pitch += (float) c.finalLift * smooth (t); break;
            case 54: m.drone = juce::jmax (m.drone, 0.7f * a * ends (c, bar)); break;
            case 55:
                m.harmSemis[0] = (float) c.third; m.harmSemis[1] = (float) c.fifth; m.harmSemis[2] = 12.0f;
                for (auto& h : m.harmMix) h = juce::jmax (h, 0.28f * a * ends (c, 1.0));
                m.harmDelayQ = 0.25f;
                room (m, 0.15f * a, 2.5f, 0.7f);
                break;
            case 56: room (m, 0.3f * a, 5.0f, 0.85f); m.shimmer = juce::jmax (m.shimmer, 0.5f * a * ends (c, 1.0)); break;
            case 57: m.pitch += -1.0f * (float) (1.0 - t); break;

            // ======== D. Space and texture ========
            case 17:
                if (c.memLen > 0)
                {
                    const auto cycle = std::fmod (q, 4.0 * bar);
                    const auto on = cycle >= 2.0 * bar && cycle < 3.0 * bar;
                    const auto frag = (double) ((mv.seed + (uint32_t) (q / (4.0 * bar))) % 4u) * bar * spq;
                    if (on) play (m, playMemory, c, frag, bar * spq, 0.4f * a, true);
                    room (m, 0.25f * a, 3.5f, 0.85f);
                }
                break;
            case 18: lowerLp (m, 300.0f * std::pow (60.0f, (float) t)); m.wow = juce::jmax (m.wow, 0.6f * (float) (1.0 - t) * a); m.lpQ = juce::jmax (m.lpQ, 1.2f); break;
            case 19: grains (m, grainFrozen, c.anchor, 0.45f * a * ends (c, bar), 180.0f, 20.0f, 0.05f, true); room (m, 0.2f, 4.0f, 0.85f); break;
            case 20: echo (m, 0.45f * a, 1.0f, 0.4f); m.dRev = juce::jmax (m.dRev, 1.0f); break;
            case 21: grains (m, grainLive, 0, 0.85f * a * (float) t, 70.0f, 40.0f, 0.8f, false); break;
            case 22: m.vowel = 1.0f - c.energy; m.vowelMix = juce::jmax (m.vowelMix, 0.5f * a * ends (c, 1.0)); break;
            case 23: case 61:
            {
                const auto s = c.sectionType;
                if (mv.effect == 23)
                {
                    if (s == tDrop) room (m, 0.08f * a, 0.8f, 0.3f);
                    else if (s == tBreak) room (m, 0.3f * a, 5.0f, 0.9f);
                    else if (s == tBuild) room (m, (0.1f + 0.25f * (float) c.sectionProgress) * a, 2.5f, 0.6f);
                    else room (m, 0.12f * a, 1.6f, 0.5f);
                }
                else
                {
                    if (s == tDrop) room (m, 0.08f * a, 0.6f, 0.2f, 7000.0f);
                    else if (s == tBreak) { room (m, 0.25f * a, 4.5f, 0.85f, 6000.0f); if (c.breakPeak) m.shimmer = juce::jmax (m.shimmer, 0.4f * a); }
                    else if (s == tBuild) room (m, 0.15f * a, 2.5f, 0.6f);
                    else room (m, 0.12f * a, 1.4f, 0.35f, 9000.0f);
                }
                break;
            }
            case 24:
                m.doppler = juce::jmax (m.doppler, a); m.dopplerPos = (float) t;
                m.pan += (2.0f * (float) t - 1.0f) * 0.9f * juce::jmin (1.0f, a);
                lowerLp (m, 20000.0f * (0.2f + 0.8f * std::sin (suite::pi * (float) t)));
                m.gain *= 0.6f + 0.4f * std::sin (suite::pi * (float) t);
                break;
            case 25: m.rotateDepth = juce::jmax (m.rotateDepth, 0.7f * a * ends (c, 1.0)); m.rotateHz = 0.25f * std::pow (8.0f, (float) t); break;
            case 26: m.wow = juce::jmax (m.wow, 0.3f * a); m.drive = juce::jmax (m.drive, 0.15f * a); lowerLp (m, 9000.0f); break;
            case 58: lowerLp (m, 3500.0f + 16500.0f * (1.0f - juce::jmin (1.0f, a))); room (m, 0.3f * a, 3.0f, 0.8f); m.gain *= 1.0f - 0.15f * juce::jmin (1.0f, a); m.width *= 0.9f; break;
            case 59: grains (m, grainChord, c.chordAnchor, 0.4f * a * std::exp (-(float) c.chordQ / 0.5f), 150.0f, 25.0f, 0.0f, true); break;
            case 60: room (m, 0.3f * a, 10.0f, 1.0f, 2000.0f); break;
            case 62: echo (m, 0.5f * a, 0.75f, 0.5f + 0.42f * (float) t, 2500.0f + 2000.0f * (float) (1.0 - t), 400.0f); break;
            case 63: m.phone = juce::jmax (m.phone, a * (t < 0.75 ? 1.0f : (float) ((1.0 - t) * 4.0))); break;
            case 64: m.width *= 0.85f + 0.55f * c.held * juce::jmin (1.0f, a); break;
            case 65:
                m.sendScale *= c.vocalNow ? 0.4f : 1.25f;
                if (! c.vocalNow) { m.rSend = juce::jmax (m.rSend, 0.08f * a); }
                break;
            case 66: m.air += 3.0f * a * (float) (mv.slot == sBuildBody ? smooth ((t - 0.75) * 4.0) : t); break;
            case 67: m.sag = juce::jmax (m.sag, a * (float) t); lowerLp (m, 20000.0f * std::pow (0.15f, (float) t)); m.wow = juce::jmax (m.wow, 0.3f * a); break;

            // ======== E. Expression ========
            case 27: m.vib = juce::jmax (m.vib, 0.18f * a); m.vibHeld = true; m.bright += 1.5f * c.held * a; break;
            case 28: m.keyTrack = true; m.keyTrackAmt = juce::jmax (m.keyTrackAmt, 0.5f * a); break;
            case 29: m.drive = juce::jmax (m.drive, 0.6f * a * (float) t); m.bright += 2.0f * (float) t; break;
            case 30: gate (m, gateStraight, 0.7f * a * juce::jmin (1.0f, (float) t * 1.5f), rollRate (t)); break;
            case 68:
            {
                const auto ph = juce::jlimit (0.0, 1.0, c.phraseQ / juce::jmax (1.0, c.phraseLenQ));
                m.gain *= 1.0f - 0.12f * juce::jmin (1.0f, a) * (1.0f - std::sin (suite::pi * (float) ph));
                break;
            }
            case 69:
            {
                const auto inBar = std::fmod (c.posQ, bar);
                if (inBar < 0.5) { m.bright += 2.0f * a * (float) (1.0 - inBar / 0.5); m.drive = juce::jmax (m.drive, 0.08f * a); }
                break;
            }
            case 70:
            {
                const auto r = hashUnit ((uint32_t) (int64_t) std::floor (c.posQ * 4.0) * 2654435761u ^ mv.seed);
                m.bright += (r - 0.5f) * 2.0f * a;
                m.gain *= 1.0f + (hashUnit ((uint32_t) (int64_t) std::floor (c.posQ * 4.0) ^ 0x9e3779b9u ^ mv.seed) - 0.5f) * 0.06f * a;
                break;
            }
            case 71: m.bright += (c.energy - 0.6f) * 5.0f * a; lowerLp (m, 4000.0f + 16000.0f * juce::jlimit (0.0f, 1.0f, c.energy + (1.0f - juce::jmin (1.0f, a)))); break;
            case 72: m.velColour = juce::jmax (m.velColour, a); m.drive = juce::jmax (m.drive, 0.05f * a); break;
            case 73:
            {
                const auto half = (int) std::floor (c.phraseQ / juce::jmax (1.0, c.phraseLenQ * 0.5));
                m.pan += 0.35f * juce::jmin (1.0f, a) * ((half & 1) ? 1.0f : -1.0f);
                break;
            }
            case 74: lowerLp (m, 20000.0f + juce::jmin (1.0f, a) * ((1500.0f + 14000.0f * std::exp (-(float) c.chordQ / 0.4f)) - 20000.0f)); m.lpQ = juce::jmax (m.lpQ, 1.1f); break;
            case 75: m.width *= c.chordTension ? 1.0f + 0.35f * a : c.chordMinor ? 1.0f - 0.15f * a : 1.0f; break;

            // ======== F. No limits ========
            case 76:
                if (c.memLen > 0) play (m, playMemory, c, (double) (mv.seed % 4u) * bar * spq, bar * spq, 0.35f * a * ends (c, bar), true);
                m.playSeed = 1u;
                room (m, 0.5f * a, 5.0f, 0.9f);
                break;
            case 77: m.detune = juce::jmax (m.detune, a); m.width *= 1.5f; m.haas = juce::jmax (m.haas, 15.0f); break;
            case 78: m.shapeRole = 5; m.shapeMix = juce::jmax (m.shapeMix, a * (float) t); break;
            case 79: m.shapeRole = 6; m.shapeMix = juce::jmax (m.shapeMix, 0.8f * a); break;
            case 80: m.gateKind = gateMask; m.gateMask = c.borrowedOnsets != 0 ? c.borrowedOnsets : (uint16_t) 0x5555; m.gateDepth = juce::jmax (m.gateDepth, 0.85f * a); break;
            case 81:
                if (q < 0.5) room (m, 0.5f, 3.0f, 0.85f);
                else m.gain = 0.0f;
                break;
            case 82: grains (m, grainStretch, c.anchor, 1.0f, 100.0f, 35.0f, 0.0f, false); m.grainSpeed = 0.5f; break;
            case 83: m.overtone = (float) t; m.overtoneMix = juce::jmax (m.overtoneMix, 0.35f * a); break;
            case 84: grains (m, grainLive, 0, a * (float) t, 220.0f, 45.0f, 0.3f, false); room (m, 0.3f * (float) t, 3.0f, 0.8f); break;
            case 85: room (m, 0.25f * a, 2.0f, 0.6f); m.rGate = juce::jmax (m.rGate, 0.8f); break;
            case 86: room (m, 0.25f * a, 3.0f, 0.7f); m.shimmer = juce::jmax (m.shimmer, 0.5f * a); m.rDuck = juce::jmax (m.rDuck, 0.9f); break;
            case 87:
                play (m, playTapeLoop, c, 0.0, 1.0 - 0.03 * t, ends (c, 0.5));
                lowerLp (m, 20000.0f * std::pow (0.2f, (float) t)); m.crush = juce::jmax (m.crush, 0.2f * (float) t); m.wow = juce::jmax (m.wow, 0.2f);
                break;
            case 88:
            {
                static const float delayQ[8] = { 1.5f, 0.0f, 1.5f, 0.0f, 1.0f, 2.0f, 2.5f, 3.0f };
                const auto local = (q - delayQ[juce::jlimit (0, 7, c.role)]) / 1.5;
                if (local >= 0.0 && local <= 1.5)
                {
                    const auto bump = (float) std::exp (-std::pow (local - 0.3, 2.0) / 0.08);
                    m.air += 4.0f * a * bump; m.width *= 1.0f + 0.5f * a * bump; m.bright += 2.0f * a * bump;
                }
                break;
            }
            case 89:
                if (c.role == 4) raiseHp (m, 1000.0f);
                else if (c.role == 5 || c.role == 6) lowerLp (m, 700.0f);
                else if (c.role == 3) lowerLp (m, 1500.0f);
                else raiseHp (m, 600.0f);
                break;
            case 90:
                if (c.memLen > 0) play (m, playMemShuffle, c, 0.25 * spq);
                else play (m, playShuffle, c, 0.25 * spq, 0.0);
                m.playSeed = mv.seed;
                break;

            // ======== H. Transitions ========
            case 99: grains (m, grainFrozen, c.anchor, 0.45f * a * (float) (1.0 - t), 200.0f, 20.0f, 0.03f, true); break;
            case 100: play (m, playRepeat, c, 0.25 * spq); m.air += 2.0f * a; break;
            case 101: m.gain = 0.0f; break;
            case 102: echo (m, q < c.lenQ - bar ? 0.5f * a : 0.0f, 0.75f, 0.5f); break;
            case 103: lowerLp (m, 500.0f * std::pow (40.0f, (float) t)); m.lpQ = juce::jmax (m.lpQ, 1.6f); break;

            // ======== I. Per role ========
            case 104:
                if (mv.slot == sDropBody) m.tailGate = juce::jmax (m.tailGate, 0.6f * a);
                else room (m, 0.12f * a, 1.2f, 0.4f, 2500.0f);
                break;
            case 105:
                if (mv.slot == sPhraseEnd) m.pitch += 12.0f * (t < 0.95 ? 1.0f : 0.0f);
                else if (mv.slot == sFinalDrop) { m.drive = juce::jmax (m.drive, 0.35f * a); m.lpQ = juce::jmax (m.lpQ, 1.0f); m.vowel = 0.2f; m.vowelMix = juce::jmax (m.vowelMix, 0.25f * a); }
                else raiseHp (m, 20.0f + 100.0f * (float) t * a);
                break;
            case 106:
                if (mv.slot == sBuildBody) room (m, (0.1f + 0.2f * (float) t) * a, 1.0f + (float) t, 0.4f);
                else if (mv.slot == sBuildEnd) { if (q >= c.lenQ - 2.0) play (m, playRepeat, c, 0.125 * spq); }
                else m.gain *= 0.25f;
                break;
            case 107:
                if (mv.slot == sBreakBody) lowerLp (m, 2500.0f + 2000.0f * std::sin ((float) (c.posQ / 16.0) * juce::MathConstants<float>::twoPi));
                else if (mv.slot == sDropBody) gate (m, gateStraight, 0.7f * a, 0.25f);
                else m.gain *= 0.75f + 0.25f * (float) juce::jmin (1.0, c.chordQ);
                break;
            case 108:
                if (mv.slot == sBuildBody) lowerLp (m, 800.0f * std::pow (25.0f, std::floor ((float) t * 4.0f) / 4.0f + 0.25f * (float) (1.0 - juce::jmin (1.0f, a))));
                else echo (m, 0.4f * a, 0.75f, 0.85f);
                break;
            case 109:
                if (mv.slot == sBreakPeak) grains (m, grainFrozen, c.anchor, 0.6f * a * ends (c, 0.5), 160.0f, 22.0f, 0.0f, true);
                else if (q >= c.lenQ - 1.0) { room (m, 0.6f * a, 3.5f, 0.85f); echo (m, 0.4f * a, 0.75f, 0.4f); }
                break;
            case 110:
                if (mv.slot == sBuildEnd) { if (q >= c.lenQ - 1.0) play (m, playReverse, c, spq); m.gain *= (float) (t * t); }
                else if (mv.slot == sDropStart) { grains (m, grainStretch, c.anchor, 0.6f * a * (float) (1.0 - t), 140.0f, 25.0f, 0.0f, true); m.grainSpeed = 0.25f; }
                else { m.doppler = juce::jmax (m.doppler, a); m.dopplerPos = (float) t; m.pan += (2.0f * (float) t - 1.0f) * 0.9f; }
                break;

            // ======== J. Physics ========
            case 111: echo (m, q < 1.0 ? 0.35f * a : 0.0f, 0.75f, 0.55f, 6000.0f * std::pow (0.3f, (float) t)); m.dPitch = -1.0f; break;
            case 112: echo (m, 0.25f * a, 0.5f + (1.0f - c.energy), 0.4f); break;
            case 113: m.weather = juce::jmax (m.weather, 0.8f * a * ends (c, 2.0 * bar)); break;
            case 114: if (c.leadHz > 30.0f) { m.peakHz = juce::jlimit (100.0f, 12000.0f, c.leadHz * 2.0f); m.peakDb = juce::jmax (m.peakDb, 5.0f * a); } break;
            case 115: echo (m, q < 2.0 ? 0.4f * a : 0.0f, 0.5f, 0.6f); m.dPitch = 7.0f; break;
            case 116: gate (m, gateStraight, 0.3f + 0.5f * a * (float) t, t < 0.5 ? 0.25f : 0.125f); break;
            case 117:
            {
                const auto ph = std::fmod (c.posQ, 8.0 * bar) / (8.0 * bar);
                m.gain *= 1.0f - 0.06f * juce::jmin (1.0f, a) * (0.5f - 0.5f * (float) std::cos (ph * juce::MathConstants<double>::twoPi));
                break;
            }
            case 118:
                if (c.memLen > 0)
                {
                    const auto kind = mv.seed % 3u;
                    play (m, playMemory, c, 0.0, bar * spq, 0.6f * a * ends (c, 0.25), true);
                    m.playSeed = kind == 1u ? 1u : 0u;
                    if (kind == 2u) m.haas = juce::jmax (m.haas, 18.0f);
                }
                break;
            default: break;
        }
    }
}
