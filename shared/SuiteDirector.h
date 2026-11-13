#pragma once

// The suite director: one plan, owned by Ouroboros, for every transition in the song and every
// plugin's part in it. Each moment (a section change, a breakdown's peak, a phrase end, a moment
// you pin) gets one hero (the plugin that leads it, with its gesture), quiet supporters, a handoff
// sentence where it suits (Azoth's riser ends into Transmutation's snap, then Kybalion's gate
// re-enters on the one), the director's own silence, and the mix around it: the other tracks make
// room, the limiter eases, the space goes dry then wide, the low end clears for the hit.
//
// Pure and deterministic: the same inputs give the same plan. Ouroboros runs it and publishes the
// result (DirectorBus.h); every plugin plays its part and falls back to its own behaviour when no
// plan is published.

#include "SectionBus.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace suite::director
{
    //==============================================================================
    // Players: who can lead or support a moment
    enum Player : uint8_t { pTransmute, pKybalion, pAntioch, pAzoth, pCorrespondence, pScene, pSilence, numPlayers, pNone = 255 };
    inline const char* playerName (int p)
    {
        static const char* n[] = { "Transmutation", "Kybalion", "Antioch", "Azoth", "Correspondence", "Scenes", "Silence" };
        return p >= 0 && p < numPlayers ? n[p] : "nobody";
    }
    /** The plugin that has to be in the session for a player to act (Scenes and Silence: always). */
    inline const char* pluginOf (int p)
    {
        static const char* n[] = { "Transmutation", "Kybalion", "Antioch", "Azoth", "Correspondence", "", "" };
        return p >= 0 && p < numPlayers ? n[p] : "";
    }

    inline const char* roleName (int r)
    {
        static const char* n[] = { "Any track", "Kick", "Bass", "Hats", "Pads", "Lead", "Vocal", "FX" };
        return r >= 0 && r <= 7 ? n[r] : "";
    }

    // Moment kinds (bits)
    enum Kind : uint16_t
    {
        kIntoDrop = 1, kIntoBreak = 2, kIntoBuild = 4, kIntoOther = 8, kBreakPeak = 16, kBreakHint = 32,
        kPhrase = 64, kFinalEnd = 128, kPin = 256, kAny = 0x1ff
    };
    inline const char* kindName (int k)
    {
        switch (k)
        {
            case kIntoDrop: return "into the drop"; case kIntoBreak: return "into the breakdown"; case kIntoBuild: return "into the build";
            case kIntoOther: return "section change"; case kBreakPeak: return "breakdown peak"; case kBreakHint: return "breakdown hint";
            case kPhrase: return "phrase end"; case kFinalEnd: return "the ending"; case kPin: return "your pin"; default: return "";
        }
    }

    //==============================================================================
    // Gestures: what a player does at a moment
    enum Gesture : uint8_t
    {
        gNone,
        gTmRepeat, gTmReverse, gTmPitch, gTmFilter, gTmSpace, gTmGrain, gTmStereo, gTmTape,
        gKyRoll, gKyGap, gKyHalfTime, gKyStutter,
        gAnThrow, gAnWash, gAnFreeze,
        gAzImpact, gAzRiser, gAzSwell, gAzRoll, gAzDown, gAzTapeStop,
        gCoPickup, gCoRiserArp, gCoBassOut, gCoStab, gCoHint,
        gScSlam, gScStagger, gScFilter,
        gSiSnap, gSiBreath, gSiKickOnly, gSiFakeDrop,
        numGestures
    };

    // Moods (per section) and drop characters
    enum Mood : uint8_t { moodCalm, moodHope, moodDrive, moodTension, moodRelease, moodNostalgia, moodTriumph, moodDark, numMoods };
    inline const char* moodName (int m)
    {
        static const char* n[] = { "calm", "hope", "drive", "tension", "release", "nostalgia", "triumph", "dark" };
        return m >= 0 && m < numMoods ? n[m] : "";
    }
    enum Character : uint8_t { chStatement, chEuphoric, chDark, chStripped, chHuge, numCharacters };
    inline const char* characterName (int c)
    {
        static const char* n[] = { "Statement", "Euphoric", "Dark", "Stripped", "Huge" };
        return c >= 0 && c < numCharacters ? n[c] : "";
    }
    inline const char* characterWhat (int c)
    {
        static const char* n[] = { "clean and hook-first: the drop as written", "wide, bright and spacious", "filtered, pitched and gritty",
                                   "bare: kick, bass and the hook, the rest arriving late", "everything: widest, loudest, every plugin dressed up" };
        return c >= 0 && c < numCharacters ? n[c] : "";
    }

    enum Genre : uint8_t { genreAuto, genreUplifting, genreTech, genrePsy, genreProgressive, genreOrchestral, numGenres };
    inline const char* genreName (int g)
    {
        static const char* n[] = { "Auto (from the tempo)", "Uplifting", "Tech trance", "Psy", "Progressive", "Orchestral" };
        return g >= 0 && g < numGenres ? n[g] : "";
    }
    inline int genreFromTempo (double bpm) { return bpm >= 142.0 ? genrePsy : bpm >= 136.0 ? genreUplifting : bpm >= 130.0 ? genreTech : genreProgressive; }

    enum Story : uint8_t { storyRestrained, storyClassic, storyFestival, storyExperimental, numStories };
    inline const char* storyName (int s)
    {
        static const char* n[] = { "Restrained", "Classic", "Festival", "Experimental" };
        return s >= 0 && s < numStories ? n[s] : "";
    }

    enum Harmony : uint8_t { hNone, hBorrowed, hPedal, hKeyPrep, hSus, numHarmony };
    inline const char* harmonyName (int h)
    {
        static const char* n[] = { "", "a borrowed chord into it (bVI-bVII)", "a pedal under the build", "a V pickup into the new key", "a sus4 resolving on the one" };
        return h >= 0 && h < numHarmony ? n[h] : "";
    }
    enum TempoMove : uint8_t { tNone, tHalftime, tRamp, numTempo };
    inline const char* tempoName (int t)
    {
        static const char* n[] = { "", "a half-time feel for its first bars", "the tempo eases 2% through it and climbs back in the build" };
        return t >= 0 && t < numTempo ? n[t] : "";
    }
    enum Edit : uint8_t { eNone, eBassOut, eArpOut, ePadsLate, eDrumsOut, numEdits };
    inline const char* editName (int e)
    {
        static const char* n[] = { "", "the bass drops out", "the arp rests", "the pads come in late", "the drums drop out" };
        return e >= 0 && e < numEdits ? n[e] : "";
    }

    // Requests (mood words and chat): each with an optional scope
    enum Word : uint16_t
    {
        wDarker = 1, wBrighter = 2, wHarder = 4, wSofter = 8, wLessGlitch = 16, wMoreGlitch = 32, wMoreSpace = 64, wDrier = 128,
        wMoreSilence = 256, wLessSilence = 512, wSimpler = 1024, wBusier = 2048, wWider = 4096
    };
    inline const char* wordName (uint16_t w)
    {
        switch (w)
        {
            case wDarker: return "darker"; case wBrighter: return "brighter"; case wHarder: return "hit harder"; case wSofter: return "softer";
            case wLessGlitch: return "less glitch"; case wMoreGlitch: return "more glitch"; case wMoreSpace: return "more space"; case wDrier: return "drier";
            case wMoreSilence: return "more silence"; case wLessSilence: return "less silence"; case wSimpler: return "simpler"; case wBusier: return "busier";
            case wWider: return "wider"; default: return "";
        }
    }
    struct Request
    {
        uint16_t words = 0;
        int sectionType = -1;   // -1 = everywhere, else a SectionType
        int dropOrdinal = -1;   // with sectionType Drop: which drop (-1 = every drop; 99 = the last)
        bool operator== (const Request& o) const { return words == o.words && sectionType == o.sectionType && dropOrdinal == o.dropOrdinal; }
    };

    enum Intent : uint8_t { iAuto, iCalmer, iMore, iNoMoves, numIntents };
    inline const char* intentName (int i)
    {
        static const char* n[] = { "Auto", "Calmer", "More", "No moves" };
        return i >= 0 && i < numIntents ? n[i] : "";
    }

    // Moment flags
    enum Flag : uint16_t
    {
        fLocked = 1, fFrozen = 2, fPinned = 4, fMotif = 8, fRecall = 16, fSurprise = 32, fUserHero = 64, fVocal = 128,
        fAudio = 256, fSignature = 512, fLearned = 1024, fTrimmed = 2048, fSafe = 4096
    };

    struct GestureInfo
    {
        Gesture id;
        Player player;
        const char* name;
        const char* what;
        uint8_t size;         // 1 small, 2 medium, 3 big
        uint16_t kinds;       // where it can go
        bool safe;            // low risk (filters, levels, space): kept in safe mode
        float weight;
        uint8_t moods;        // bit per Mood it suits
        uint8_t genres;       // bit per Genre (1..5) that loves it
        uint8_t characters;   // bit per Character it dresses
    };

    namespace detail
    {
        constexpr uint8_t M (std::initializer_list<int> ms) { uint8_t b = 0; for (auto m : ms) b = (uint8_t) (b | (1u << m)); return b; }
    }

    inline const std::array<GestureInfo, numGestures>& gestures()
    {
        using detail::M;
        constexpr uint16_t trans = kIntoDrop | kIntoBreak | kIntoBuild | kIntoOther | kBreakPeak | kPhrase | kPin;
        static const std::array<GestureInfo, numGestures> g { {
            { gNone, pNone, "rest", "", 0, 0, true, 0.0f, 0, 0, 0 },
            { gTmRepeat, pTransmute, "Beat-repeat roll", "the track's last beats repeat, faster and faster", 2, kIntoDrop | kIntoBuild | kPhrase | kBreakPeak | kPin, false, 1.0f,
              M ({ moodTension, moodDrive, moodDark }), (uint8_t) (2 | 4 | 8), M ({ chHuge, chDark }) },
            { gTmReverse, pTransmute, "Reverse swell", "the track runs backwards into the hit", 2, kIntoDrop | kIntoBreak | kBreakPeak | kPhrase | kPin, false, 1.0f,
              M ({ moodHope, moodNostalgia, moodRelease }), (uint8_t) (2 | 32), M ({ chEuphoric, chStatement }) },
            { gTmPitch, pTransmute, "Pitch dive", "the track bends down (or up) into the change", 2, kIntoDrop | kIntoBreak | kPhrase | kPin, false, 0.8f,
              M ({ moodTension, moodDark }), (uint8_t) (4 | 8), M ({ chDark }) },
            { gTmFilter, pTransmute, "Filter sweep", "a filter closes and opens across the change", 1, trans, true, 1.1f,
              M ({ moodCalm, moodHope, moodDrive, moodTension }), (uint8_t) (16 | 2 | 8), M ({ chStripped, chStatement }) },
            { gTmSpace, pTransmute, "Space throw", "the track's last note thrown into reverb", 1, trans, true, 1.0f,
              M ({ moodHope, moodNostalgia, moodCalm, moodRelease }), (uint8_t) (2 | 16 | 32), M ({ chEuphoric }) },
            { gTmGrain, pTransmute, "Granular smear", "the sound breaks into grains", 2, kIntoDrop | kIntoBreak | kBreakPeak | kPhrase | kPin, false, 0.7f,
              M ({ moodTension, moodDark, moodNostalgia }), (uint8_t) 8, M ({ chDark }) },
            { gTmStereo, pTransmute, "Stereo spin", "the image spins and narrows into the hit", 1, kIntoDrop | kIntoBuild | kPhrase | kPin, false, 0.8f,
              M ({ moodDrive, moodHope }), (uint8_t) (2 | 8), M ({ chEuphoric, chHuge }) },
            { gTmTape, pTransmute, "Tape stop", "the track slows to a stop before the hit", 3, kIntoDrop | kIntoBreak | kPin, false, 0.9f,
              M ({ moodTension, moodDark, moodDrive }), (uint8_t) (4 | 8), M ({ chHuge, chDark }) },

            { gKyRoll, pKybalion, "Gate roll", "the trance gate rolls 16ths, then 32nds, into the one", 2, kIntoDrop | kIntoBuild | kPhrase | kPin, false, 1.1f,
              M ({ moodTension, moodDrive, moodTriumph }), (uint8_t) (2 | 4 | 8), M ({ chHuge, chStatement }) },
            { gKyGap, pKybalion, "Gate gap", "the gate shuts for the last beats: a breath before the hit", 1, kIntoDrop | kIntoBreak | kPhrase | kPin, true, 1.0f,
              M ({ moodTension, moodRelease }), (uint8_t) (2 | 16), M ({ chStatement, chStripped }) },
            { gKyHalfTime, pKybalion, "Gate half-time", "the gate drops to half speed", 1, kIntoBreak | kIntoOther | kPhrase | kPin, true, 0.9f,
              M ({ moodCalm, moodNostalgia }), (uint8_t) (2 | 8), M ({ chStripped }) },
            { gKyStutter, pKybalion, "Gate stutter", "the gate stutters on the last beat", 2, kIntoDrop | kIntoBuild | kPhrase | kPin, false, 0.8f,
              M ({ moodTension, moodDrive, moodDark }), (uint8_t) (4 | 2), M ({ chDark, chHuge }) },

            { gAnThrow, pAntioch, "Echo throw", "the last word or note thrown into the echo", 1, trans, true, 1.2f,
              M ({ moodHope, moodRelease, moodNostalgia, moodCalm }), (uint8_t) (1 | 16 | 8), M ({ chEuphoric, chStatement }) },
            { gAnWash, pAntioch, "Reverb wash", "the space opens wide as the breakdown begins", 2, kIntoBreak | kBreakHint | kPin, true, 1.1f,
              M ({ moodNostalgia, moodCalm, moodHope }), (uint8_t) (1 | 16 | 8), M ({ chEuphoric }) },
            { gAnFreeze, pAntioch, "Reverb freeze", "the space holds its last chord under the change", 2, kIntoBreak | kBreakPeak | kFinalEnd | kPin, true, 0.7f,
              M ({ moodNostalgia, moodCalm, moodHope }), (uint8_t) (1 | 16), M ({ chEuphoric }) },

            { gAzImpact, pAzoth, "Impact", "a tuned impact and crash on the one", 2, kIntoDrop | kIntoOther | kPin, true, 1.2f,
              M ({ moodRelease, moodTriumph, moodDrive }), (uint8_t) (1 | 2 | 16), M ({ chHuge, chStatement, chEuphoric }) },
            { gAzRiser, pAzoth, "Riser", "a riser climbs through the build", 2, kIntoDrop | kIntoBuild | kPin, true, 1.1f,
              M ({ moodTension, moodHope, moodTriumph }), (uint8_t) (1 | 16 | 2), M ({ chHuge, chEuphoric }) },
            { gAzSwell, pAzoth, "Noise swell", "noise opens up into the hit", 1, kIntoDrop | kBreakPeak | kIntoBuild | kPin, true, 1.0f,
              M ({ moodTension, moodHope }), (uint8_t) (1 | 8 | 2), M ({ chStatement, chStripped, chEuphoric }) },
            { gAzRoll, pAzoth, "Snare roll", "a snare roll speeds up into the drop", 2, kIntoDrop | kIntoBuild | kPin, false, 1.0f,
              M ({ moodTension, moodTriumph, moodDrive }), (uint8_t) (1 | 16), M ({ chHuge, chStatement }) },
            { gAzDown, pAzoth, "Downlifter", "a falling sweep into the breakdown", 1, kIntoBreak | kIntoOther | kFinalEnd | kPin, true, 1.0f,
              M ({ moodCalm, moodNostalgia, moodRelease }), (uint8_t) (1 | 8 | 16), M ({ chEuphoric, chStripped }) },
            { gAzTapeStop, pAzoth, "Drop tape stop", "Azoth's track grinds to a stop before the drop", 3, kIntoDrop | kPin, false, 0.6f,
              M ({ moodTension, moodDark }), (uint8_t) (2 | 4), M ({ chDark, chHuge }) },

            { gCoPickup, pCorrespondence, "Pickup fill", "the lead plays a pickup into the one (written into the MIDI)", 1, kIntoDrop | kIntoBuild | kPhrase | kPin, true, 0.9f,
              M ({ moodHope, moodTriumph, moodRelease }), (uint8_t) (1 | 16 | 8), M ({ chStatement, chEuphoric }) },
            { gCoRiserArp, pCorrespondence, "Riser arp", "an arp climbing an octave through the last bars", 2, kIntoDrop | kIntoBuild | kPin, true, 0.8f,
              M ({ moodTension, moodHope, moodTriumph }), (uint8_t) (1 | 2), M ({ chHuge, chEuphoric }) },
            { gCoBassOut, pCorrespondence, "Bass drop-out", "the bass rests for the last bar, so the drop's bass lands", 1, kIntoDrop | kPhrase | kPin, true, 1.0f,
              M ({ moodTension, moodRelease, moodDrive }), (uint8_t) (2 | 4 | 8), M ({ chStripped, chStatement, chHuge }) },
            { gCoStab, pCorrespondence, "Chord stab", "a chord stab on the one", 1, kIntoDrop | kIntoOther | kPin, true, 0.8f,
              M ({ moodTriumph, moodRelease, moodDrive }), (uint8_t) (1 | 16), M ({ chHuge, chStatement }) },
            { gCoHint, pCorrespondence, "Hook hint", "the hook's first notes, alone, in the breakdown", 1, kBreakHint | kPin, true, 1.0f,
              M ({ moodHope, moodNostalgia }), (uint8_t) (1 | 16 | 8), M ({ chEuphoric, chStatement }) },

            { gScSlam, pScene, "Slam", "everything changes on the one", 1, kIntoDrop | kIntoOther | kIntoBreak, true, 1.0f,
              M ({ moodRelease, moodTriumph, moodDrive }), (uint8_t) (1 | 2 | 4), M ({ chHuge, chStatement }) },
            { gScStagger, pScene, "Staggered entrance", "kick, then bass, then the rest, a beat apart", 1, kIntoDrop, true, 0.7f,
              M ({ moodDrive, moodTension }), (uint8_t) (2 | 8), M ({ chStripped }) },
            { gScFilter, pScene, "Filter entrance", "the drop arrives behind a filter that opens over a bar", 1, kIntoDrop | kIntoOther, true, 0.6f,
              M ({ moodHope, moodDark }), (uint8_t) (8 | 4), M ({ chDark, chStripped }) },

            { gSiSnap, pSilence, "Snap", "half a beat of silence before the one", 1, kIntoDrop | kIntoBreak | kPin, false, 1.0f,
              M ({ moodTension, moodRelease }), (uint8_t) (1 | 2 | 4), M ({ chHuge, chStatement }) },
            { gSiBreath, pSilence, "Breath", "a whole beat of silence before the one", 2, kIntoDrop | kPin, false, 0.8f,
              M ({ moodTension, moodRelease, moodTriumph }), (uint8_t) (1 | 16), M ({ chHuge, chEuphoric }) },
            { gSiKickOnly, pSilence, "Kick-only bar", "only the kick plays the last bar", 2, kIntoDrop | kPin, false, 0.6f,
              M ({ moodDrive, moodTension }), (uint8_t) (2 | 4), M ({ chStripped, chDark }) },
            { gSiFakeDrop, pSilence, "Fake drop", "the drop starts, stops after a beat, and comes back a bar later", 3, kIntoDrop, false, 0.35f,
              M ({ moodTension, moodTriumph }), (uint8_t) (1 | 2), M ({ chHuge }) },
        } };
        return g;
    }
    inline const GestureInfo& gesture (int id) { return gestures()[(size_t) juce::jlimit (0, (int) numGestures - 1, id)]; }
    inline juce::Colour playerColour (int p)
    {
        static const juce::uint32 c[] = { 0xffe8a33d, 0xff5ec8b8, 0xff9a7be8, 0xffe8615a, 0xff7fb86a, 0xffd8c27d, 0xff8a8f98 };
        return p >= 0 && p < numPlayers ? juce::Colour (c[p]) : juce::Colours::grey;
    }

    //==============================================================================
    /** A handoff: one leg of the sentence (who, what, when). */
    struct Leg { uint8_t player = pNone, gesture = gNone; float atQ = 0.0f; };

    /** One moment of the plan: trivially copyable (it goes into shared memory as is). */
    struct Moment
    {
        uint32_t key = 0;
        uint16_t kind = 0, flags = 0;
        int16_t section = -1;        // the section it leads into (or is in, for peaks, hints and phrases)
        int8_t dropOrdinal = -1;     // the drop it leads into or belongs to
        uint8_t hero = pNone, heroGesture = gNone, heroRole = 0;   // heroRole: the track role the hero plays on (0 = any)
        std::array<uint8_t, numPlayers> gesture {};                // every player's gesture here (0 = rests)
        std::array<Leg, 3> legs {};                                // the handoff sentence (empty legs: player pNone)
        float atQ = 0.0f, startQ = 0.0f, endQ = 0.0f;              // the hit, and the window the moment takes
        uint8_t silence = gNone;
        float silenceStartQ = 0.0f, silenceEndQ = 0.0f;
        uint8_t cutTails = 0, mood = 0, character = 0, harmony = hNone, tempo = tNone, edit = eNone;
        float editStartQ = 0.0f, editEndQ = 0.0f;
        float budget = 0.0f, spent = 0.0f, tension = 0.0f, amount = 1.0f, confidence = 1.0f;
        // the mix around the moment
        // evenDb: a level ride over the first 2 bars after the change, evening out a jump Mentalism measured (premaster: no limiter anywhere)
        float premixDb = 0.0f, hookDipDb = 0.0f, lowClearBeats = 0.0f, evenDb = 0.0f, buildHoldDb = 0.0f;
        float dryBefore = 0.0f, narrowBefore = 0.0f, wideOnHit = 0.0f;

        bool acts (int p) const noexcept { return p >= 0 && p < numPlayers && gesture[(size_t) p] != gNone; }
        bool isHero (int p) const noexcept { return hero == p; }
        bool supports (int p) const noexcept { return acts (p) && hero != p; }
        int supporters() const noexcept { int n = 0; for (int p = 0; p < numPlayers; ++p) n += supports (p) ? 1 : 0; return n; }
        bool same (const Moment& o) const noexcept
        {
            return hero == o.hero && heroGesture == o.heroGesture && gesture == o.gesture && silence == o.silence && harmony == o.harmony
                   && tempo == o.tempo && edit == o.edit && std::abs (atQ - o.atQ) < 1.0e-4f;
        }
    };

    /** A pin: this player leads at this bar (gesture 0 = the director's choice for that player). */
    struct Pin { int bar = 0; uint8_t player = pNone, gesture = gNone; bool operator== (const Pin& o) const { return bar == o.bar && player == o.player && gesture == o.gesture; } };
    /** A gesture you taught it: the moment you shaped, kept as a recipe for that kind of moment. */
    struct Learned { uint16_t kind = 0; Moment recipe; juce::String name; };

    /** What Mentalism measured in one section (LUFS; first and second half). */
    struct SectionFeedback { float lufs = -100.0f, first = -100.0f, second = -100.0f; bool heard = false; };
    /** What a reference track does at its transitions. */
    struct ReferenceStyle { bool valid = false; float movesPerMinute = 0.0f, silenceShare = 0.0f, bigShare = 0.0f; juce::String name; };

    struct Input
    {
        SectionMap map;
        double bpm = 138.0, barQ = 4.0;
        int story = storyClassic, genre = genreAuto;
        bool safe = false;
        bool silencesBeforeDrops = false;            // the Snap and the Breath (full silence before a drop): off unless you switch them on
        float bigger = 0.0f;                         // -2..2: the "bigger / smaller" knob
        uint32_t seed = 0;
        int candidates = 6;
        uint8_t present = 0x7f;                      // bit per Player: the plugins in the session
        std::map<uint32_t, int> rerolls;             // moment key -> re-roll count
        std::map<uint32_t, std::pair<uint8_t, uint8_t>> locks;   // moment key -> hero, gesture
        std::map<uint32_t, Moment> frozen;           // moment key -> the whole moment, kept as is (freeze, templates, hand edits)
        std::map<uint32_t, std::array<uint8_t, numPlayers>> playerLocks;   // moment key -> gestures kept per player (0 = the director's choice)
        std::vector<uint8_t> intent;                 // per section (Intent)
        std::vector<float> sectionSpice;             // per section (0..2, 1 = as the story says)
        std::vector<std::pair<int, int>> noMoves;    // bar ranges (inclusive) the director leaves alone
        std::vector<Pin> pins;
        std::map<uint32_t, int> heroRole;            // moment key -> the track role you chose to lead it
        std::vector<int> sectionMood;                // per section: -1 = the director's, else a Mood
        std::vector<Request> requests;
        std::array<float, numGestures> taste {};     // your ratings (sum of +1 / -1)
        std::vector<Learned> learned;
        // what the song actually sounds like
        std::vector<float> barLoudness, barDensity;  // per bar (dB; 0..1), from a rehearsal pass
        std::vector<SectionFeedback> feedback;       // per section, from Mentalism
        ReferenceStyle reference;
        std::vector<std::pair<double, double>> vocal;     // sung phrases (quarters)
        std::vector<std::pair<double, double>> hookRests; // where the hook rests (quarters)
    };

    struct Score
    {
        float total = 0.0f, energyFit = 0.0f, contrast = 0.0f, variety = 0.0f, clutter = 0.0f, taste = 0.0f, reference = 0.0f, audio = 0.0f;
        juce::String text() const
        {
            auto f = [] (float v) { return juce::String (v, 2); };
            return "score " + f (total) + " (energy fit " + f (energyFit) + ", contrast " + f (contrast) + ", variety " + f (variety)
                   + ", clutter " + f (clutter) + ", taste " + f (taste) + ", reference " + f (reference) + ", audio " + f (audio) + ")";
        }
    };

    struct Lint { int severity = 1; int moment = -1; juce::String text; };   // severity 2 = a problem, 1 = a note

    struct BreakdownStory { int section = -1; float emptyQ = 0, hintQ = 0, swellQ = 0, peakQ = 0, endQ = 0; };

    struct StructureAdvice { bool any = false; SectionMap suggested; juce::StringArray reasons; };

    struct Plan
    {
        std::vector<Moment> moments;
        std::vector<juce::StringArray> why;          // per moment: the reasons
        std::vector<float> tension, energy;          // per bar
        std::vector<uint8_t> mood;                   // per section
        std::vector<uint8_t> character;              // per drop
        std::vector<BreakdownStory> breakdowns;
        int motif = gNone;                           // the gesture that comes back, bigger, in the final drop
        int genre = genreUplifting, story = storyClassic;
        Score score;
        int candidatesTried = 0;
        std::vector<Lint> lints;
        StructureAdvice structure;
        uint32_t seed = 0, hash = 0;
        bool safe = false;

        int indexOf (uint32_t key) const { for (size_t i = 0; i < moments.size(); ++i) if (moments[i].key == key) return (int) i; return -1; }
    };

    //==============================================================================
    namespace detail
    {
        inline uint32_t mix (uint32_t a, uint32_t b) noexcept
        {
            auto h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
            h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
            return h;
        }
        inline float unit (uint32_t h) noexcept { return (float) (mix (h, 0x51ed27u) & 0xffffffu) / 16777216.0f; }

        struct StoryShape { float spice, silence, handoffs, surprises, phrases, fakeDrop; int maxSupport; };
        inline StoryShape storyShape (int s)
        {
            switch (s)
            {
                case storyRestrained:   return { 0.6f, 0.15f, 0.15f, 0.0f, 0.15f, 0.0f, 1 };
                case storyFestival:     return { 1.3f, 0.7f, 0.7f, 2.0f, 0.5f, 0.5f, 3 };
                case storyExperimental: return { 1.5f, 0.6f, 0.6f, 4.0f, 0.6f, 0.35f, 3 };
                case storyClassic: default: return { 1.0f, 0.45f, 0.45f, 1.0f, 0.3f, 0.15f, 2 };
            }
        }
        inline float genreWeight (int genre, const GestureInfo& g)
        {
            if (genre <= genreAuto || genre >= numGenres) return 1.0f;
            return (g.genres & (1u << (genre - 1))) != 0 ? 1.6f : 0.8f;
        }
        inline bool glitchy (int id) { return id == gTmRepeat || id == gTmGrain || id == gTmTape || id == gKyStutter || id == gAzTapeStop || id == gTmPitch; }
        inline bool spacey (int id) { return id == gTmSpace || id == gAnThrow || id == gAnWash || id == gAnFreeze || id == gTmReverse; }
        inline bool bright (int id) { return id == gAzRiser || id == gAzSwell || id == gTmStereo || id == gCoRiserArp || id == gAzImpact || id == gTmReverse; }
        inline bool dark (int id) { return id == gTmPitch || id == gTmGrain || id == gTmFilter || id == gScFilter || id == gTmTape || id == gAzTapeStop; }
        inline bool silenceGesture (int id) { return id >= gSiSnap && id <= gSiFakeDrop; }

        /** The section types' usual moods. */
        inline uint8_t defaultMood (const SectionMap& m, int i)
        {
            using T = SectionType;
            const auto t = m.sections[(size_t) i].type;
            const auto e = m.energyOf (i);
            switch (t)
            {
                case T::Intro:     return moodCalm;
                case T::Groove:    return e > 0.65f ? moodDrive : moodHope;
                case T::Breakdown: return e < 0.2f ? moodNostalgia : moodHope;
                case T::Build:     return moodTension;
                case T::Drop:      return i == m.lastDropIndex() ? moodTriumph : (e < 0.85f ? moodDrive : moodRelease);
                case T::Outro:     return moodNostalgia;
                case T::count: default: return moodCalm;
            }
        }
    }

    /** Words heard in a sentence ("make the second drop hit harder", "less glitch in the breakdown"). */
    inline Request parseRequest (const juce::String& text)
    {
        Request r;
        const auto t = " " + text.toLowerCase().replaceCharacters (",.!?;:", "      ") + " ";
        auto has = [&t] (const char* w) { return t.contains (w); };
        if (has ("darker") || has (" dark ") || has ("moodier")) r.words |= wDarker;
        if (has ("brighter") || has (" bright ") || has ("lighter") || has ("happier")) r.words |= wBrighter;
        if (has ("harder") || has ("bigger") || has ("hit ") || has ("punch") || has ("more impact") || has ("louder")) r.words |= wHarder;
        if (has ("softer") || has ("gentler") || has ("smaller") || has ("subtle") || has ("calmer")) r.words |= wSofter;
        if (has ("less glitch") || has ("no glitch") || has ("fewer glitch") || has ("less stutter") || has ("cleaner")) r.words |= wLessGlitch;
        else if (has ("glitch") || has ("stutter") || has ("chop")) r.words |= wMoreGlitch;
        if (has ("more space") || has ("more reverb") || has ("spacious") || has ("bigger room") || has ("wetter")) r.words |= wMoreSpace;
        if (has ("drier") || has ("dryer") || has ("less reverb") || has ("tighter")) r.words |= wDrier;
        if (has ("less silence") || has ("no silence") || has ("no gap") || has ("no snap") || has ("fewer gaps")) r.words |= wLessSilence;
        else if (has ("silence") || has (" gap") || has (" snap") || has ("pause") || has ("breath")) r.words |= wMoreSilence;
        if (has ("simpler") || has ("less going on") || has ("less busy") || has ("fewer moves") || has ("minimal")) r.words |= wSimpler;
        if (has ("busier") || has ("more going on") || has ("more moves") || has ("more fx") || has ("crazier") || has ("wild")) r.words |= wBusier;
        if (has ("wider") || has ("more stereo")) r.words |= wWider;
        using T = SectionType;
        static const std::pair<const char*, int> ordinals[] = { { "first", 0 }, { "1st", 0 }, { "second", 1 }, { "2nd", 1 }, { "third", 2 }, { "3rd", 2 },
                                                                 { "final", 99 }, { "last", 99 } };
        if (has ("drop"))
        {
            r.sectionType = (int) T::Drop;
            for (const auto& [w, o] : ordinals) if (has (w)) { r.dropOrdinal = o; break; }
        }
        else if (has ("breakdown") || has ("break ")) r.sectionType = (int) T::Breakdown;
        else if (has ("build") || has ("riser")) r.sectionType = (int) T::Build;
        else if (has ("intro")) r.sectionType = (int) T::Intro;
        else if (has ("outro") || has ("ending")) r.sectionType = (int) T::Outro;
        else if (has ("groove")) r.sectionType = (int) T::Groove;
        return r;
    }
    inline juce::String describe (const Request& r)
    {
        juce::StringArray w;
        for (uint16_t b = 1; b != 0 && b <= wWider; b = (uint16_t) (b << 1)) if (r.words & b) w.add (wordName (b));
        juce::String where = "everywhere";
        if (r.sectionType == (int) SectionType::Drop)
            where = r.dropOrdinal == 99 ? "the final drop" : r.dropOrdinal >= 0 ? "drop " + juce::String (r.dropOrdinal + 1) : "every drop";
        else if (r.sectionType >= 0) where = "the " + juce::String (sectionName ((SectionType) r.sectionType)).toLowerCase();
        return (w.isEmpty() ? juce::String ("(no words I know)") : w.joinIntoString (", ")) + ": " + where;
    }

    //==============================================================================
    /** The window a player's gesture takes around a moment (quarters), scaled to the tempo. */
    inline std::pair<double, double> windowOf (const Moment& m, int player, int gestureId, double barQ, double bpm)
    {
        const auto at = (double) m.atQ;
        const auto scale = juce::jlimit (0.75, 1.35, bpm / 138.0);   // faster songs: gestures take more beats
        const auto beats = [&] (double b) { return b * scale; };
        const auto silenceStart = m.silence != gNone ? (double) m.silenceStartQ : at;
        switch (gestureId)
        {
            case gTmRepeat: case gTmPitch: case gTmReverse: return { silenceStart - beats (2.0), silenceStart };
            case gTmTape: case gAzTapeStop: return { silenceStart - beats (1.0), silenceStart };
            case gTmFilter: case gTmGrain: case gTmStereo: return { silenceStart - barQ, silenceStart };
            case gTmSpace: case gAnThrow: return { silenceStart - 1.0, at + barQ };
            case gKyRoll: return { at - barQ, silenceStart };
            case gKyGap: return { at - beats (1.0), at };
            case gKyStutter: return { silenceStart - 1.0, silenceStart };
            case gKyHalfTime: return { at, at + 2.0 * barQ };
            case gAnWash: return { at, at + 4.0 * barQ };
            case gAnFreeze: return { at - 1.0, at + 2.0 * barQ };
            case gAzImpact: case gCoStab: case gScSlam: return { at, at + 1.0 };
            case gAzRiser: case gCoRiserArp: return { at - juce::jmin (8.0, 4.0 * scale) * barQ, silenceStart };
            case gAzSwell: return { at - 2.0 * barQ, silenceStart };
            case gAzRoll: return { at - 4.0 * barQ, silenceStart };
            case gAzDown: return { at, at + 2.0 * barQ };
            case gCoPickup: return { silenceStart - 1.0, at };
            case gCoBassOut: return { at - barQ, at };
            case gCoHint: return { at, at + 2.0 * barQ };
            case gScStagger: return { at, at + 2.0 };
            case gScFilter: return { at, at + barQ };
            default: juce::ignoreUnused (player); return { (double) m.startQ, (double) m.endQ };
        }
    }

    //==============================================================================
    namespace detail
    {
        struct Slot { uint16_t kind; double atQ; int section; int dropOrdinal; bool keyLift; bool lastDrop; };

        inline uint32_t keyOf (uint16_t kind, double atQ) { return mix ((uint32_t) kind, (uint32_t) std::lround (atQ * 4.0)) | 1u; }

        inline std::vector<Slot> slotsOf (const Input& in)
        {
            using T = SectionType;
            std::vector<Slot> out;
            const auto& m = in.map;
            const auto bar = in.barQ;
            std::vector<int> dropOrd ((size_t) juce::jmax (0, m.count), -1);
            int drops = 0;
            for (int i = 0; i < m.count; ++i) if (m.sections[(size_t) i].type == T::Drop) dropOrd[(size_t) i] = drops++;
            auto ordinalFrom = [&] (int i) { for (int k = i; k < m.count; ++k) if (dropOrd[(size_t) k] >= 0) return dropOrd[(size_t) k]; return -1; };
            const auto lastDrop = m.lastDropIndex();
            for (int i = 0; i < m.count; ++i)
            {
                const auto type = m.sections[(size_t) i].type;
                const auto S = (m.sections[(size_t) i].startBar - 1) * bar, E = (m.endBar (i) - 1) * bar;
                if (i > 0)
                {
                    const auto kind = type == T::Drop ? kIntoDrop : type == T::Breakdown ? kIntoBreak : type == T::Build ? kIntoBuild : kIntoOther;
                    out.push_back ({ (uint16_t) kind, S, i, ordinalFrom (i), m.keyShiftOf (i) > m.keyShiftOf (i - 1), i == lastDrop });
                }
                if (type == T::Breakdown && E - S >= 8.0 * bar)
                {
                    out.push_back ({ kBreakHint, S + std::floor ((E - S) / bar * 0.25) * bar, i, ordinalFrom (i), false, false });
                    if (i + 1 < m.count) out.push_back ({ kBreakPeak, E - 2.0 * bar, i, ordinalFrom (i), false, false });
                }
                if (type == T::Drop || type == T::Groove || type == T::Breakdown)
                {
                    const auto phrase = std::fmod ((E - S) / bar, 8.0) < 0.01 ? 8.0 : 4.0;   // real phrase lengths: 8 bars, else 4
                    for (auto P = S + 2.0 * phrase * bar; P < E - phrase * bar + 1.0e-6; P += 2.0 * phrase * bar)
                        out.push_back ({ kPhrase, P, i, ordinalFrom (i), false, false });
                }
            }
            if (m.count > 0) out.push_back ({ kFinalEnd, m.totalBars * bar, m.count - 1, -1, false, false });
            for (const auto& p : in.pins)
            {
                const auto q = (p.bar - 1) * bar;
                if (std::any_of (out.begin(), out.end(), [&] (const Slot& s) { return std::abs (s.atQ - q) < 0.01; })) continue;
                int sec = 0;
                for (int i = 0; i < m.count; ++i) if ((m.sections[(size_t) i].startBar - 1) * bar <= q + 1.0e-6) sec = i;
                out.push_back ({ kPin, q, sec, ordinalFrom (sec), false, false });
            }
            std::stable_sort (out.begin(), out.end(), [] (const Slot& a, const Slot& b) { return a.atQ < b.atQ; });
            return out;
        }

        inline std::vector<float> tensionCurve (const SectionMap& m, double)
        {
            using T = SectionType;
            std::vector<float> t ((size_t) juce::jmax (0, m.totalBars), 0.2f);
            for (int i = 0; i < m.count; ++i)
            {
                const auto s = m.sections[(size_t) i].startBar - 1, e = juce::jmin (m.totalBars, m.endBar (i) - 1);
                const auto len = juce::jmax (1, e - s);
                const auto type = m.sections[(size_t) i].type;
                for (int b = s; b < e; ++b)
                {
                    const auto x = (float) (b - s + 1) / (float) len;
                    float v = 0.2f;
                    switch (type)
                    {
                        case T::Intro: v = 0.15f + 0.15f * x; break;
                        case T::Groove: v = 0.3f + 0.15f * x; break;
                        case T::Breakdown: v = 0.25f + 0.4f * x * x; break;
                        case T::Build: v = 0.45f + 0.55f * x * x; break;
                        case T::Drop: v = 0.35f + 0.1f * x; break;
                        case T::Outro: v = 0.3f - 0.2f * x; break;
                        case T::count: default: break;
                    }
                    // a ripple at each 8-bar phrase's end
                    if (((b - s + 1) % 8) == 0) v += 0.08f;
                    t[(size_t) b] = juce::jlimit (0.0f, 1.0f, v);
                }
            }
            return t;
        }

        /** Everything the requests say about a section (words), scoped. */
        inline uint16_t wordsFor (const Input& in, int section, int dropOrdinal, int drops)
        {
            uint16_t w = 0;
            if (section < 0 || section >= in.map.count) return w;
            const auto type = (int) in.map.sections[(size_t) section].type;
            for (const auto& r : in.requests)
            {
                if (r.sectionType >= 0 && r.sectionType != type) continue;
                if (r.sectionType == (int) SectionType::Drop && r.dropOrdinal >= 0)
                {
                    const auto want = r.dropOrdinal == 99 ? drops - 1 : r.dropOrdinal;
                    if (want != dropOrdinal) continue;
                }
                w = (uint16_t) (w | r.words);
            }
            return w;
        }

        struct Context
        {
            const Input* in = nullptr;
            int genre = genreUplifting, drops = 0;
            StoryShape story {};
            std::vector<uint8_t> mood, character;
            std::vector<float> tension;
            int motif = gNone;
        };

        inline bool inNoMoveZone (const Input& in, double q)
        {
            const auto bar = (int) std::floor (q / in.barQ + 1.0e-9) + 1;
            for (const auto& [a, b] : in.noMoves) if (bar >= a && bar <= b) return true;
            return false;
        }
        inline bool vocalOver (const Input& in, double a, double b)
        {
            for (const auto& [s, e] : in.vocal) if (s < b && e > a) return true;
            return false;
        }
        inline float barValue (const std::vector<float>& v, double q, double barQ, float missing)
        {
            const auto b = (int) std::floor (q / barQ - 1.0e-9);
            return b >= 0 && b < (int) v.size() ? v[(size_t) b] : missing;
        }

        /** One whole plan for one roll of the dice. */
        inline Plan planOnce (const Input& in, const Context& cx, const std::vector<Slot>& slots, uint32_t seed)
        {
            using T = SectionType;
            Plan plan;
            plan.genre = cx.genre;
            plan.story = in.story;
            plan.mood = cx.mood;
            plan.character = cx.character;
            plan.tension = cx.tension;
            plan.motif = cx.motif;
            plan.safe = in.safe;
            plan.seed = seed;
            const auto& m = in.map;
            const auto bar = in.barQ;
            const auto biggerScale = std::pow (1.25f, juce::jlimit (-2.0f, 2.0f, in.bigger));
            std::map<int, double> lastUsed;   // gesture -> where it was last heard (fatigue memory)
            int surprisesLeft = (int) std::round (cx.story.surprises);
            int lastHero = pNone, fakeDropsLeft = cx.story.fakeDrop > 0.0f ? 1 : 0;
            const auto fakeRoll = unit (mix (seed, 0xfa4eu));

            // audio: what Mentalism heard (drops that don't land, sagging drops)
            auto feedbackOf = [&] (int i) -> const SectionFeedback* { return i >= 0 && i < (int) in.feedback.size() && in.feedback[(size_t) i].heard ? &in.feedback[(size_t) i] : nullptr; };

            for (const auto& s : slots)
            {
                Moment mo;
                mo.kind = s.kind;
                mo.key = keyOf (s.kind, s.atQ);
                mo.section = (int16_t) s.section;
                mo.dropOrdinal = (int8_t) s.dropOrdinal;
                mo.atQ = (float) s.atQ;
                const auto sectionType = s.section >= 0 && s.section < m.count ? m.sections[(size_t) s.section].type : T::Groove;
                mo.mood = s.section >= 0 && s.section < (int) cx.mood.size() ? cx.mood[(size_t) s.section] : moodCalm;
                mo.character = s.dropOrdinal >= 0 && s.dropOrdinal < (int) cx.character.size() ? cx.character[(size_t) s.dropOrdinal] : chStatement;
                mo.tension = barValue (cx.tension, s.atQ, bar, 0.5f);
                juce::StringArray why;
                why.add (juce::String (kindName (s.kind)) + " at bar " + juce::String ((int) std::lround (s.atQ / bar) + 1) + ": the mood is "
                         + moodName (mo.mood) + (s.dropOrdinal >= 0 && (s.kind & (kIntoDrop | kPhrase)) && sectionType == T::Drop
                                                   ? juce::String (", drop ") + juce::String (s.dropOrdinal + 1) + " is " + characterName (mo.character) : juce::String()));

                // frozen moments (freeze, a template, your own edit) are kept exactly
                if (const auto it = in.frozen.find (mo.key); it != in.frozen.end())
                {
                    auto f = it->second;
                    f.key = mo.key; f.atQ = mo.atQ; f.kind = mo.kind; f.section = mo.section; f.dropOrdinal = mo.dropOrdinal;
                    f.flags = (uint16_t) (f.flags | fFrozen);
                    why.add ("Frozen: kept exactly as you left it.");
                    for (int p = 0; p < numPlayers; ++p) if (f.gesture[(size_t) p] != gNone) lastUsed[f.gesture[(size_t) p]] = s.atQ;
                    if (f.hero != pNone) lastHero = f.hero;
                    plan.moments.push_back (f);
                    plan.why.push_back (why);
                    continue;
                }

                const auto words = wordsFor (in, s.section, s.dropOrdinal, cx.drops);
                const auto intent = s.section >= 0 && s.section < (int) in.intent.size() ? in.intent[(size_t) s.section] : (uint8_t) iAuto;
                const auto secSpice = s.section >= 0 && s.section < (int) in.sectionSpice.size() ? juce::jlimit (0.0f, 2.0f, in.sectionSpice[(size_t) s.section]) : 1.0f;
                const auto noMoves = intent == iNoMoves || inNoMoveZone (in, s.atQ) || (s.kind == kFinalEnd && in.story == storyRestrained);

                // ---- the energy budget ----
                float base = 0.0f;
                switch (s.kind)
                {
                    case kIntoDrop: base = 3.0f; break;     case kIntoBreak: base = 2.0f; break;   case kIntoBuild: base = 1.5f; break;
                    case kIntoOther: base = 1.2f; break;    case kBreakPeak: base = 1.6f; break;   case kBreakHint: base = 1.2f; break;
                    case kPhrase: base = 0.9f; break;       case kFinalEnd: base = 1.2f; break;    case kPin: base = 2.5f; break;
                    default: break;
                }
                auto budget = base * cx.story.spice * secSpice * biggerScale * (0.7f + 0.6f * mo.tension);
                if (s.kind == kIntoDrop && cx.drops > 1) budget *= s.dropOrdinal == 0 ? 0.85f : s.dropOrdinal == cx.drops - 1 ? 1.3f : 1.0f;
                if (intent == iCalmer) budget *= 0.55f;
                if (intent == iMore) budget *= 1.45f;
                if (words & wHarder) budget *= 1.35f;
                if (words & (wSofter | wSimpler)) budget *= 0.65f;
                if (words & wBusier) budget *= 1.3f;
                // audio: busy bars get fewer moves, thin ones more; a drop that didn't land gets more
                if (! in.barDensity.empty())
                {
                    const auto d = barValue (in.barDensity, s.atQ - 0.5, bar, 0.5f);
                    if (d > 0.75f) { budget *= 0.75f; mo.flags |= fAudio; why.add ("The bars before it are busy (heard): fewer moves."); }
                    else if (d < 0.3f) { budget *= 1.15f; mo.flags |= fAudio; why.add ("The bars before it are thin (heard): room for more."); }
                }
                bool notLanding = false;
                if (s.kind == kIntoDrop && s.section > 0)
                    if (const auto* a = feedbackOf (s.section - 1); a != nullptr)
                        if (const auto* d = feedbackOf (s.section); d != nullptr && d->first - a->second < 2.0f)
                        {
                            notLanding = true;
                            budget *= 1.3f;
                            mo.flags |= fAudio;
                            why.add ("Mentalism heard this drop arrive only " + juce::String (d->first - a->second, 1) + " LU above the build: it gets more contrast.");
                        }
                if (s.kind == kPhrase && sectionType == T::Drop)
                    if (const auto* d = feedbackOf (s.section); d != nullptr && d->second < d->first - 1.0f)
                    {
                        budget *= 1.4f;
                        mo.flags |= fAudio;
                        why.add ("The drop sags in its second half (" + juce::String (d->first - d->second, 1) + " LU): a lift here.");
                    }
                if (noMoves) budget = 0.0f;
                if (in.safe) budget = juce::jmin (budget, 2.0f);
                mo.budget = budget;

                // ---- candidates: who leads ----
                const auto reroll = (uint32_t) [&] { const auto it = in.rerolls.find (mo.key); return it != in.rerolls.end() ? it->second : 0; }();
                auto h = mix (mix (seed, mo.key), reroll * 7919u + 1u);
                const auto vocalHere = vocalOver (in, s.atQ - 2.0, s.atQ + 0.5);
                if (vocalHere) mo.flags |= fVocal;

                auto weightOf = [&] (const GestureInfo& g) -> float
                {
                    if (g.player == pNone || (g.kinds & s.kind) == 0) return 0.0f;
                    if (g.player < numPlayers && (in.present & (1u << g.player)) == 0) return 0.0f;
                    if (in.safe && ! g.safe) return 0.0f;
                    if ((float) g.size > budget + 0.01f) return 0.0f;
                    if (silenceGesture (g.id) && g.id != gSiFakeDrop && (words & wLessSilence)) return 0.0f;
                    if ((g.id == gSiSnap || g.id == gSiBreath) && ! in.silencesBeforeDrops) return 0.0f;   // no full silence before a drop unless you allow it
                    auto w = g.weight * genreWeight (cx.genre, g);
                    if (g.moods & (1u << mo.mood)) w *= 1.6f;
                    if ((s.kind & (kIntoDrop | kPhrase)) && sectionType == T::Drop && (g.characters & (1u << mo.character))) w *= 1.5f;
                    if (s.kind == kIntoDrop && s.keyLift && (g.id == gCoPickup || g.id == gAzRiser || g.id == gTmPitch)) w *= 1.6f;
                    w *= 1.0f + 0.15f * juce::jlimit (-5.0f, 8.0f, in.taste[(size_t) g.id]);
                    if (in.taste[(size_t) g.id] <= -3.0f) w *= 0.2f;
                    // fatigue: a gesture heard in the last 32 bars waits (unless it's the motif)
                    if (const auto it = lastUsed.find (g.id); it != lastUsed.end() && s.atQ - it->second < 32.0 * bar && g.id != cx.motif) w *= 0.15f;
                    if (g.player == lastHero) w *= 0.6f;
                    if (words & wLessGlitch) { if (glitchy (g.id)) w *= 0.1f; }
                    if (words & wMoreGlitch) { if (glitchy (g.id)) w *= 2.2f; }
                    if (words & wMoreSpace) { if (spacey (g.id)) w *= 2.0f; }
                    if (words & wDrier) { if (spacey (g.id)) w *= 0.25f; }
                    if (words & wDarker) { w *= dark (g.id) ? 2.0f : bright (g.id) ? 0.5f : 1.0f; }
                    if (words & wBrighter) { w *= bright (g.id) ? 2.0f : dark (g.id) ? 0.5f : 1.0f; }
                    if (words & wHarder) { w *= g.size >= 2 ? 1.6f : 0.8f; }
                    if (words & wMoreSilence) { if (silenceGesture (g.id)) w *= 2.5f; }
                    if (notLanding && (silenceGesture (g.id) || g.size >= 2)) w *= 1.5f;
                    // the vocal sings here: gestures that cover it step back (Antioch's throw of its last word is welcome)
                    if (vocalHere && g.id != gAnThrow && (g.player == pTransmute || g.id == gKyStutter) && g.size >= 2) w *= 0.3f;
                    if (s.kind == kIntoDrop && silenceGesture (g.id)) w *= 0.5f + cx.story.silence;
                    if (g.id == gSiFakeDrop && (fakeDropsLeft <= 0 || s.dropOrdinal <= 0 || s.lastDrop || fakeRoll > cx.story.fakeDrop)) return 0.0f;
                    return juce::jmax (0.0f, w);
                };
                auto pick = [&] (std::vector<std::pair<int, float>>& cands, uint32_t hh) -> int
                {
                    float total = 0.0f;
                    for (const auto& c : cands) total += c.second;
                    if (total <= 0.0f) return gNone;
                    auto r = unit (hh) * total;
                    for (const auto& [id, w] : cands) { if (r < w) return id; r -= w; }
                    return cands.back().first;
                };

                // phrase ends only sometimes get a move (over-automation guard)
                if (s.kind == kPhrase && ! (mo.flags & fAudio) && unit (mix (h, 0x9a5eu)) > cx.story.phrases * secSpice * biggerScale * (intent == iMore ? 1.6f : 1.0f)
                    && in.locks.count (mo.key) == 0)
                    budget = 0.0f;

                // silence first (it's the director's own instrument): into drops and breakdowns
                int silenceId = gNone;
                if (budget > 0.0f && (s.kind & (kIntoDrop | kIntoBreak)))
                {
                    std::vector<std::pair<int, float>> sc;
                    for (const auto& g : gestures()) if (silenceGesture (g.id)) if (const auto w = weightOf (g); w > 0.0f) sc.push_back ({ g.id, w });
                    const auto chance = (words & wMoreSilence) ? 0.95f : (words & wLessSilence) ? 0.0f
                                      : juce::jlimit (0.0f, 0.95f, cx.story.silence * (s.kind == kIntoDrop ? 1.0f : 0.45f) * (notLanding ? 1.6f : 1.0f));
                    if (! sc.empty() && unit (mix (h, 0x5113u)) < chance) silenceId = pick (sc, mix (h, 0x5114u));
                }

                // the hero
                int heroId = gNone;
                if (const auto it = in.locks.find (mo.key); it != in.locks.end() && it->second.second != gNone)
                {
                    heroId = it->second.second;
                    mo.flags |= fLocked;
                    why.add (juce::String ("Locked: ") + gesture (heroId).name + " stays.");
                }
                for (const auto& p : in.pins)
                    if (std::abs ((p.bar - 1) * bar - s.atQ) < 0.01 && p.player < numPlayers)
                    {
                        if (p.gesture != gNone) heroId = p.gesture;
                        else
                        {
                            std::vector<std::pair<int, float>> pc;
                            for (const auto& g : gestures()) if (g.player == p.player && (g.kinds & (s.kind | kPin))) pc.push_back ({ g.id, juce::jmax (0.05f, weightOf (g)) });
                            heroId = pick (pc, mix (h, 0x917u));
                        }
                        mo.flags |= fPinned;
                        why.add (juce::String ("Pinned: ") + playerName (p.player) + " leads here.");
                        if (budget < (float) gesture (heroId).size) budget = (float) gesture (heroId).size;
                    }
                // a gesture you taught it for this kind of moment
                const Learned* learned = nullptr;
                if (heroId == gNone && budget > 0.0f)
                    for (const auto& l : in.learned)
                        if ((l.kind & s.kind) != 0 && unit (mix (h, 0x1ea7u + (uint32_t) l.recipe.heroGesture)) < 0.45f) { learned = &l; break; }
                if (learned != nullptr)
                {
                    const auto keep = mo;
                    mo = learned->recipe;
                    mo.key = keep.key; mo.kind = keep.kind; mo.section = keep.section; mo.dropOrdinal = keep.dropOrdinal; mo.atQ = keep.atQ;
                    mo.mood = keep.mood; mo.character = keep.character; mo.tension = keep.tension; mo.budget = keep.budget;
                    const auto shift = keep.atQ - learned->recipe.atQ;
                    for (auto& l : mo.legs) if (l.player != pNone) l.atQ += shift;
                    mo.silenceStartQ += shift; mo.silenceEndQ += shift; mo.editStartQ += shift; mo.editEndQ += shift;
                    mo.flags = (uint16_t) ((keep.flags & (fVocal | fAudio)) | fLearned);
                    heroId = mo.heroGesture;
                    silenceId = mo.silence;
                    why.add ("Taught: your \"" + learned->name + "\" for this kind of moment.");
                }
                // the motif: introduced at the first drop, recalled bigger at the last
                if (heroId == gNone && budget > 0.0f && cx.motif != gNone && s.kind == kIntoDrop && cx.drops > 1
                    && (s.dropOrdinal == 0 || s.lastDrop) && weightOf (gesture (cx.motif)) > 0.0f)
                {
                    heroId = cx.motif;
                    mo.flags |= s.dropOrdinal == 0 ? fMotif : fRecall;
                    why.add (s.dropOrdinal == 0 ? juce::String ("The song's motif: ") + gesture (heroId).name + ", heard first here, comes back bigger in the final drop."
                                                : juce::String ("Recall: the first drop's ") + gesture (heroId).name + ", bigger.");
                }
                if (heroId == gNone && budget > 0.0f)
                {
                    std::vector<std::pair<int, float>> cands;
                    for (const auto& g : gestures())
                    {
                        if (silenceGesture (g.id) || g.player == pScene) continue;
                        auto w = weightOf (g);
                        if (w <= 0.0f) continue;
                        // the surprise budget: rare moves get a lift while it lasts
                        if (surprisesLeft > 0 && g.weight < 0.85f) w *= 1.8f;
                        cands.push_back ({ g.id, w });
                    }
                    heroId = pick (cands, mix (h, 0x4e70u));
                    if (heroId != gNone && surprisesLeft > 0 && gesture (heroId).weight < 0.85f) { --surprisesLeft; mo.flags |= fSurprise; why.add ("A surprise (the story allows a few)."); }
                    if (heroId != gNone && in.taste[(size_t) heroId] >= 2.0f) { mo.flags |= fSignature; why.add ("One of your signature moves (you rated it up)."); }
                }
                if (learned == nullptr)
                {
                    if (heroId != gNone)
                    {
                        mo.hero = gesture (heroId).player;
                        mo.heroGesture = (uint8_t) heroId;
                        mo.gesture[(size_t) mo.hero] = (uint8_t) heroId;
                        why.add (juce::String (playerName (mo.hero)) + " leads: " + gesture (heroId).name + " (" + gesture (heroId).what + ").");
                    }
                    else if (budget <= 0.0f)
                        why.add (noMoves ? "You asked for no moves here." : s.kind == kPhrase ? "A phrase end left plain, so the big moments stand out." : "No move here.");
                    if (silenceId != gNone)
                    {
                        mo.silence = (uint8_t) silenceId;
                        mo.gesture[pSilence] = (uint8_t) silenceId;
                        why.add (juce::String ("The director's silence: ") + gesture (silenceId).name + " (" + gesture (silenceId).what + ").");
                        if (silenceId == gSiFakeDrop) --fakeDropsLeft;
                    }
                    auto spent = (float) gesture (heroId).size + (silenceId != gNone ? 0.5f * (float) gesture (silenceId).size : 0.0f);

                    // ---- supporters: quiet, inside what's left of the budget ----
                    int supportCount = 0;
                    const auto maxSupport = juce::jmin (cx.story.maxSupport, (words & wSimpler) ? 0 : (words & wBusier) ? 3 : 3);
                    for (int round = 0; round < 3 && heroId != gNone; ++round)
                    {
                        if (supportCount >= maxSupport || spent + 0.5f > budget + 0.01f) break;
                        std::vector<std::pair<int, float>> sc;
                        for (const auto& g : gestures())
                        {
                            if (g.player == pNone || g.player == pSilence || g.player == pScene || mo.acts (g.player) || g.size > 2) continue;
                            auto w = weightOf (g);
                            if (w <= 0.0f) continue;
                            if (g.size == 1) w *= 1.5f;
                            sc.push_back ({ g.id, w });
                        }
                        const auto sid = pick (sc, mix (h, 0x5u + (uint32_t) round * 31u));
                        if (sid == gNone) break;
                        const auto cost = 0.5f * (float) gesture (sid).size;
                        if (spent + cost > budget + 0.01f) break;
                        mo.gesture[(size_t) gesture (sid).player] = (uint8_t) sid;
                        spent += cost;
                        ++supportCount;
                    }
                    if (supportCount > 0)
                    {
                        juce::StringArray sup;
                        for (int p = 0; p < numPlayers; ++p) if (mo.supports (p) && p != pSilence) sup.add (juce::String (playerName (p)) + " (" + gesture (mo.gesture[(size_t) p]).name + ")");
                        if (! sup.isEmpty()) why.add ("Quietly supporting: " + sup.joinIntoString (", ") + ".");
                    }
                    // the entrance (scenes) for every section change with a hero: slam, staggered or through a filter
                    if (s.kind & (kIntoDrop | kIntoOther | kIntoBreak))
                    {
                        int scene = gScSlam;
                        if (s.kind == kIntoDrop) scene = mo.character == chStripped ? gScStagger : mo.character == chDark ? gScFilter : gScSlam;
                        if (s.kind == kIntoOther && mo.mood == moodHope && unit (mix (h, 0x5ceu)) < 0.3f) scene = gScFilter;
                        if (in.safe && ! gesture (scene).safe) scene = gScSlam;
                        mo.gesture[pScene] = (uint8_t) scene;
                    }
                    mo.spent = spent;

                    // ---- the handoff sentence (into drops with room for it) ----
                    if (s.kind == kIntoDrop && heroId != gNone && budget >= 2.5f && unit (mix (h, 0x4a4du)) < cx.story.handoffs * biggerScale)
                    {
                        // a lead-in, then the snap, then the re-entry on the one
                        uint8_t leadIn = mo.gesture[pAzoth] == gAzRiser || mo.gesture[pAzoth] == gAzRoll ? (uint8_t) pAzoth
                                       : mo.gesture[pKybalion] == gKyRoll ? (uint8_t) pKybalion : (uint8_t) mo.hero;
                        mo.legs[0] = { leadIn, mo.gesture[(size_t) leadIn], (float) (s.atQ - bar) };
                        const auto snapPlayer = mo.silence != gNone ? (uint8_t) pSilence : mo.acts (pTransmute) && mo.hero != leadIn ? (uint8_t) pTransmute : (uint8_t) pNone;
                        if (snapPlayer != pNone) mo.legs[1] = { snapPlayer, mo.gesture[(size_t) snapPlayer], (float) (s.atQ - 1.0) };
                        const auto reentry = (in.present & (1u << pKybalion)) && mo.gesture[pKybalion] != gKyRoll ? (uint8_t) pKybalion
                                           : mo.acts (pAzoth) && mo.gesture[pAzoth] == gAzImpact ? (uint8_t) pAzoth : (uint8_t) pScene;
                        mo.legs[2] = { reentry, reentry == pKybalion ? (uint8_t) gNone : mo.gesture[(size_t) reentry], (float) s.atQ };
                        juce::StringArray said;
                        for (const auto& l : mo.legs)
                            if (l.player != pNone)
                                said.add (juce::String (playerName (l.player)) + (l.gesture != gNone ? juce::String (" ") + gesture (l.gesture).name : juce::String (" re-enters on the one")));
                        why.add ("One sentence, not three noises: " + said.joinIntoString (", then ") + ".");
                    }
                }

                // ---- players you locked keep their gesture here (the rest re-rolled around them) ----
                if (const auto it = in.playerLocks.find (mo.key); it != in.playerLocks.end())
                {
                    for (int p = 0; p < numPlayers; ++p)
                    {
                        const auto g = it->second[(size_t) p];
                        if (g == gNone || g >= numGestures || gesture (g).player != p) continue;
                        if (mo.gesture[(size_t) p] != g) mo.spent += 0.5f * (float) gesture (g).size;
                        mo.gesture[(size_t) p] = g;
                        if (p == pSilence) mo.silence = g;
                        else if (mo.hero == pNone || mo.hero == p) { mo.hero = (uint8_t) p; mo.heroGesture = g; }
                    }
                    mo.flags |= fLocked;
                    why.add ("Some players locked by you: kept as they were.");
                }

                // ---- the windows ----
                if (mo.silence != gNone)
                {
                    const auto beats = mo.silence == gSiSnap ? 0.5 : mo.silence == gSiBreath ? 1.0 : mo.silence == gSiKickOnly ? bar : 1.0;
                    if (mo.silence == gSiFakeDrop) { mo.silenceStartQ = (float) (s.atQ + 1.0); mo.silenceEndQ = (float) (s.atQ + bar); }
                    else { mo.silenceStartQ = (float) (s.atQ - beats); mo.silenceEndQ = (float) s.atQ; }
                }
                double lo = s.atQ, hi = s.atQ;
                for (int p = 0; p < numPlayers; ++p)
                    if (mo.acts (p) && p != pSilence)
                    {
                        const auto w = windowOf (mo, p, mo.gesture[(size_t) p], bar, in.bpm);
                        lo = juce::jmin (lo, w.first); hi = juce::jmax (hi, w.second);
                    }
                if (mo.silence != gNone) { lo = juce::jmin (lo, (double) mo.silenceStartQ); hi = juce::jmax (hi, (double) mo.silenceEndQ); }
                mo.startQ = (float) lo; mo.endQ = (float) juce::jmax (hi, lo + 0.25);

                // ---- placement around the vocal and the hook: a phrase-end hero moves into the hook's rest ----
                if (s.kind == kPhrase && mo.hero != pNone && ! in.hookRests.empty())
                {
                    double best = 1.0e9, shift = 0.0;
                    for (const auto& [rs, re] : in.hookRests)
                    {
                        const auto d = re - s.atQ;
                        if (std::abs (d) <= 1.0 + 1.0e-6 && std::abs (d) < std::abs (best) && re - rs >= 0.5) { best = d; shift = d; }
                    }
                    if (best < 1.0e8 && std::abs (shift) > 1.0e-6)
                    {
                        mo.startQ += (float) shift; mo.endQ += (float) shift;
                        why.add ("Moved " + juce::String (shift, 2) + " beats into the hook's rest.");
                    }
                }
                if (vocalHere && mo.hero != pNone) why.add ("The vocal sings here: gestures that would cover it stepped back.");

                // ---- harmony, tempo and arrangement edits (Correspondence writes them) ----
                if (s.kind == kIntoDrop && budget > 0.0f && (in.present & (1u << pCorrespondence)) && ! in.safe)
                {
                    const auto r = unit (mix (h, 0x4a12u));
                    if (s.keyLift) mo.harmony = hKeyPrep;
                    else if ((mo.mood == moodRelease || mo.mood == moodTriumph || mo.character == chEuphoric) && r < 0.45f * cx.story.spice) mo.harmony = hBorrowed;
                    else if (r < 0.25f * cx.story.spice) mo.harmony = hSus;
                    const auto prevBuild = s.section > 0 && m.sections[(size_t) s.section - 1].type == T::Build;
                    if (prevBuild && mo.harmony == hNone && unit (mix (h, 0x9ed4u)) < 0.35f) mo.harmony = hPedal;
                    if (mo.gesture[pCorrespondence] == gCoBassOut || (mo.character == chStripped && r > 0.5f))
                    {
                        mo.edit = eBassOut; mo.editStartQ = (float) (s.atQ - bar); mo.editEndQ = (float) s.atQ;
                    }
                    else if (mo.character == chStripped)
                    {
                        mo.edit = ePadsLate; mo.editStartQ = (float) s.atQ; mo.editEndQ = (float) (s.atQ + 4.0 * bar);
                    }
                    if (mo.harmony != hNone) why.add (juce::String ("Harmony: ") + harmonyName (mo.harmony) + ".");
                }
                if (s.kind == kIntoBreak && budget > 0.0f && ! in.safe)
                {
                    const auto r = unit (mix (h, 0x7e30u));
                    if (in.story == storyExperimental && r < 0.4f) mo.tempo = tRamp;
                    else if (cx.genre != genrePsy && r < 0.3f * cx.story.spice) mo.tempo = tHalftime;
                    if (s.section > 0 && m.sections[(size_t) s.section - 1].type == T::Drop && unit (mix (h, 0xa4bu)) < 0.3f * cx.story.spice && (in.present & (1u << pCorrespondence)))
                    {
                        mo.edit = eArpOut; mo.editStartQ = (float) (s.atQ - 4.0 * bar); mo.editEndQ = (float) s.atQ;
                    }
                    if (mo.tempo != tNone) why.add (juce::String ("Tempo: ") + tempoName (mo.tempo) + ".");
                }
                if (mo.edit != eNone) why.add (juce::String ("Arrangement: ") + editName (mo.edit) + " (bars " + juce::String ((int) std::lround (mo.editStartQ / bar) + 1)
                                                + "-" + juce::String ((int) std::lround (mo.editEndQ / bar)) + ").");

                // ---- the mix around it ----
                const auto hit = (s.kind & (kIntoDrop | kPin)) != 0;
                if (mo.hero != pNone)
                {
                    mo.premixDb = -juce::jlimit (0.5f, 2.5f, 0.6f * (float) gesture (mo.heroGesture).size + (mo.character == chHuge ? 0.5f : 0.0f));
                    mo.heroRole = (uint8_t) [&] { const auto it = in.heroRole.find (mo.key); return it != in.heroRole.end() ? it->second : 0; }();
                    if (mo.heroRole != 0) { mo.flags |= fUserHero; why.add ("You chose the track that leads here."); }
                }
                if (s.kind == kIntoDrop)
                {
                    const auto c = mo.character;
                    mo.buildHoldDb = notLanding ? 1.5f : c == chHuge ? 1.2f : c == chStripped ? 0.4f : 0.8f;
                    mo.dryBefore = c == chEuphoric ? 0.4f : 0.75f;
                    mo.narrowBefore = c == chStripped ? 0.2f : 0.5f;
                    mo.wideOnHit = c == chHuge || c == chEuphoric ? 0.6f : c == chStripped ? 0.1f : 0.35f;
                    mo.hookDipDb = c == chStatement || c == chEuphoric ? -1.5f : -1.0f;
                    mo.lowClearBeats = mo.acts (pAzoth) && mo.gesture[pAzoth] == gAzImpact ? 1.0f : mo.silence != gNone ? 0.5f : 0.0f;
                    mo.cutTails = 1;
                    if (words & wDrier) mo.dryBefore = juce::jmin (1.0f, mo.dryBefore + 0.25f);
                    if (words & wMoreSpace) mo.dryBefore *= 0.5f;
                    if (words & wWider) mo.wideOnHit = juce::jmin (1.0f, mo.wideOnHit + 0.3f);
                    if (words & wHarder) mo.buildHoldDb += 0.4f;
                }
                else if (s.kind == kIntoBreak) { mo.cutTails = mo.gesture[pAntioch] == gAnWash || mo.gesture[pAntioch] == gAnFreeze ? 0 : 1; }
                // transition loudness: a jump Mentalism measured at a change that isn't a drop is evened out
                if ((s.kind & (kIntoOther | kIntoBuild | kIntoBreak)) && s.section > 0)
                    if (const auto* a = feedbackOf (s.section - 1); a != nullptr)
                        if (const auto* d = feedbackOf (s.section); d != nullptr && d->first - a->second > 3.0f)
                        {
                            mo.evenDb = -juce::jmin (3.0f, d->first - a->second - 2.0f);
                            mo.flags |= fAudio;
                            why.add ("Mentalism heard a " + juce::String (d->first - a->second, 1) + " LU jump here: the first 2 bars ride " + juce::String (mo.evenDb, 1) + " dB to even it out.");
                        }
                else if (hit) { mo.wideOnHit = 0.2f; }
                if (in.safe)
                {
                    mo.flags |= fSafe;
                    mo.premixDb = juce::jmax (mo.premixDb, -1.0f);
                    mo.buildHoldDb = juce::jmin (mo.buildHoldDb, 0.5f);
                    mo.harmony = hNone; mo.tempo = tNone; mo.edit = eNone;
                }

                // ---- confidence ----
                float conf = 0.8f;
                if (mo.flags & (fLocked | fPinned | fFrozen | fLearned)) conf = 1.0f;
                if (mo.flags & fAudio) conf += 0.1f;
                if (in.barDensity.empty() && in.feedback.empty()) conf -= 0.15f;
                if (vocalHere && mo.hero == pTransmute) conf -= 0.25f;
                if (mo.flags & fSurprise) conf -= 0.2f;
                if (mo.hero != pNone && in.taste[(size_t) mo.heroGesture] < 0.0f) conf -= 0.2f;
                mo.confidence = juce::jlimit (0.05f, 1.0f, conf);

                // memory
                for (int p = 0; p < numPlayers; ++p) if (mo.gesture[(size_t) p] != gNone) lastUsed[mo.gesture[(size_t) p]] = s.atQ;
                if (mo.hero != pNone) lastHero = mo.hero;
                plan.moments.push_back (mo);
                plan.why.push_back (why);
            }

            // ---- the pile-up guard: heroes closer than two bars apart, the smaller one steps back ----
            int lastLed = -1;
            for (size_t i = 0; i < plan.moments.size(); ++i)
            {
                if (plan.moments[i].hero == pNone) continue;
                if (lastLed < 0 || plan.moments[i].atQ - plan.moments[(size_t) lastLed].atQ >= 2.0 * bar - 1.0e-3) { lastLed = (int) i; continue; }
                auto& a = plan.moments[(size_t) lastLed];
                auto& b = plan.moments[i];
                const auto keepA = (a.flags & (fLocked | fPinned | fFrozen)) != 0, keepB = (b.flags & (fLocked | fPinned | fFrozen)) != 0;
                if (keepA && keepB) { lastLed = (int) i; continue; }
                const auto aSmaller = gesture (a.heroGesture).size < gesture (b.heroGesture).size || (a.kind & (kPhrase | kBreakHint)) != 0;
                const auto loserIsA = keepB || (! keepA && aSmaller);
                auto& loser = loserIsA ? a : b;
                const auto idx = loserIsA ? (size_t) lastLed : i;
                plan.why[idx].add (juce::String ("Stepped back: ") + gesture (loser.heroGesture).name + " was too close to the next moment (pile-up guard).");
                for (int p = 0; p < numPlayers; ++p) if (p != pScene && p != pSilence) loser.gesture[(size_t) p] = gNone;   // its supporters step back with it
                loser.hero = pNone; loser.heroGesture = gNone; loser.spent = 0.0f;
                loser.flags |= fTrimmed;
                loser.legs = {};
                if (loserIsA) lastLed = (int) i;
            }
            return plan;
        }

        inline Score scoreOf (const Input& in, const Plan& p)
        {
            Score s;
            if (p.moments.empty()) return s;
            std::array<int, numGestures> used {};
            int heroes = 0, bigHeroes = 0, silences = 0;
            float fit = 0.0f, clutter = 0.0f, taste = 0.0f, contrast = 0.0f, audio = 0.0f;
            const Moment* lastDrop = nullptr;
            for (const auto& m : p.moments)
            {
                const auto want = m.tension * (m.kind == kIntoDrop ? 1.0f : 0.6f);
                const auto got = juce::jlimit (0.0f, 1.0f, m.spent / 4.0f);
                fit -= (want - got) * (want - got);
                if (m.supporters() > 2) clutter -= 0.3f * (float) (m.supporters() - 2);
                if (m.spent > m.budget + 0.01f) clutter -= 0.5f;
                if (m.hero != pNone)
                {
                    ++heroes;
                    ++used[(size_t) m.heroGesture];
                    taste += 0.1f * in.taste[(size_t) m.heroGesture];
                    if (gesture (m.heroGesture).size >= 3) ++bigHeroes;
                    if (! in.barDensity.empty())
                    {
                        const auto d = barValue (in.barDensity, m.atQ - 0.5, in.barQ, 0.5f);
                        audio += gesture (m.heroGesture).size >= 2 ? (0.5f - d) : 0.0f;
                    }
                }
                if (m.silence != gNone) ++silences;
                if (m.kind == kIntoDrop)
                {
                    if (lastDrop != nullptr)
                        contrast += (lastDrop->heroGesture != m.heroGesture ? 0.5f : -0.3f) + (lastDrop->hero != m.hero ? 0.3f : 0.0f) + (lastDrop->silence != m.silence ? 0.2f : 0.0f);
                    if (m.hero == pNone && m.silence == gNone) contrast -= 1.0f;   // a drop with no contrast
                    lastDrop = &m;
                }
            }
            int distinct = 0;
            for (auto u : used) distinct += u > 0 ? 1 : 0;
            s.energyFit = fit / (float) p.moments.size() * 4.0f;
            s.contrast = contrast;
            s.variety = heroes > 0 ? (float) distinct / (float) heroes : 0.0f;
            s.clutter = clutter;
            s.taste = taste;
            s.audio = audio;
            if (in.reference.valid && in.bpm > 0.0)
            {
                const auto minutes = (double) in.map.totalBars * in.barQ / in.bpm;
                const auto perMinute = minutes > 0.0 ? (float) ((double) heroes / minutes) : 0.0f;
                s.reference = -0.4f * std::abs (perMinute - in.reference.movesPerMinute)
                              - 2.0f * std::abs ((heroes > 0 ? (float) silences / (float) heroes : 0.0f) - in.reference.silenceShare)
                              - 2.0f * std::abs ((heroes > 0 ? (float) bigHeroes / (float) heroes : 0.0f) - in.reference.bigShare);
            }
            s.total = s.energyFit + s.contrast + s.variety + s.clutter + s.taste + s.reference + s.audio;
            return s;
        }

        inline uint32_t hashOf (const Plan& p)
        {
            uint32_t h = 0x811c9dc5u;
            for (const auto& m : p.moments)
            {
                h = mix (h, m.key);
                for (auto g : m.gesture) h = mix (h, g);
                h = mix (h, (uint32_t) m.harmony | ((uint32_t) m.tempo << 8) | ((uint32_t) m.edit << 16));
            }
            return h;
        }
    }

    //==============================================================================
    /** The lint: problems the plan has before it plays (two heroes at once, a gesture under a sung
        word, a key clash, a drop with no contrast, a long section with no moves at all), plus the
        collision report (one plugin asked to do two things at once). */
    inline std::vector<Lint> lint (const Plan& p, const Input& in)
    {
        std::vector<Lint> out;
        const auto bar = in.barQ;
        auto barOf = [&] (float q) { return juce::String ((int) std::floor (q / bar + 1.0e-6) + 1); };
        for (size_t i = 0; i < p.moments.size(); ++i)
        {
            const auto& a = p.moments[i];
            if (a.hero != pNone)
                for (size_t k = i + 1; k < p.moments.size(); ++k)
                {
                    const auto& b = p.moments[k];
                    if (b.startQ >= a.endQ) break;
                    if (b.hero != pNone && b.hero == a.hero)
                        out.push_back ({ 2, (int) k, juce::String ("Collision at bar ") + barOf (b.atQ) + ": " + playerName (b.hero) + " would play two gestures at once." });
                    else if (b.hero != pNone && std::abs (b.atQ - a.atQ) < (float) bar)
                        out.push_back ({ 2, (int) k, "Two heroes at once at bar " + barOf (b.atQ) + ": " + playerName (a.hero) + " and " + playerName (b.hero) + "." });
                }
            if (a.hero != pNone && a.hero != pAntioch && gesture (a.heroGesture).size >= 2)
            {
                const auto w = windowOf (a, a.hero, a.heroGesture, bar, in.bpm);
                if (detail::vocalOver (in, w.first, w.second))
                    out.push_back ({ 1, (int) i, juce::String ("Bar ") + barOf (a.atQ) + ": " + gesture (a.heroGesture).name + " lands under a sung word (it plays smaller)." });
            }
            if (a.harmony == hBorrowed && in.map.finalLift != 0 && a.dropOrdinal >= 0 && a.section >= in.map.lastDropIndex())
                out.push_back ({ 2, (int) i, "Key clash at bar " + barOf (a.atQ) + ": a borrowed chord and the key change on the same drop." });
            if (a.kind == kIntoDrop && a.hero == pNone && a.silence == gNone)
                out.push_back ({ 2, (int) i, "Drop at bar " + barOf (a.atQ) + " has no contrast: nothing leads into it." });
            if (a.supporters() > 3)
                out.push_back ({ 1, (int) i, "Bar " + barOf (a.atQ) + " is busy: " + juce::String (a.supporters()) + " supporters." });
        }
        for (int s = 0; s < in.map.count; ++s)
        {
            const auto lenBars = in.map.endBar (s) - in.map.sections[(size_t) s].startBar;
            if (lenBars < 16 || in.map.sections[(size_t) s].type == SectionType::Intro) continue;
            const auto S = (in.map.sections[(size_t) s].startBar - 1) * bar, E = (in.map.endBar (s) - 1) * bar;
            const auto any = std::any_of (p.moments.begin(), p.moments.end(), [&] (const Moment& m) { return m.hero != pNone && m.atQ > S + 0.01 && m.atQ <= E + 0.01; });
            const auto intent = s < (int) in.intent.size() ? in.intent[(size_t) s] : (uint8_t) iAuto;
            if (! any && intent != iNoMoves)
                out.push_back ({ 1, -1, juce::String ("The ") + juce::String (sectionName (in.map.sections[(size_t) s].type)).toLowerCase() + " at bar "
                                        + juce::String (in.map.sections[(size_t) s].startBar) + " has no moves at all (" + juce::String (lenBars) + " bars)." });
        }
        if (p.moments.size() > 128) out.push_back ({ 2, -1, juce::String ((int) p.moments.size()) + " moments: only the first 128 reach the plugins (a long song: fewer phrase moves)" });
        if (in.map.totalBars > 512) out.push_back ({ 1, -1, "The song is over 512 bars: the tension shown stops at bar 512 in the plugins" });
        std::stable_sort (out.begin(), out.end(), [] (const Lint& a, const Lint& b) { return a.severity > b.severity; });
        return out;
    }

    /** The structure: section lengths, the drop count and the key change, suggested with the moves. */
    inline StructureAdvice adviseStructure (const Input& in)
    {
        using T = SectionType;
        StructureAdvice a;
        a.suggested = in.map;
        auto& m = a.suggested;
        int drops = 0;
        for (int i = 0; i < m.count; ++i) drops += m.sections[(size_t) i].type == T::Drop ? 1 : 0;
        for (int i = 0; i < m.count; ++i)
        {
            const auto len = m.endBar (i) - m.sections[(size_t) i].startBar;
            const auto type = m.sections[(size_t) i].type;
            const auto want = type == T::Build ? (in.story == storyFestival ? 16 : 8) : type == T::Breakdown ? 16 : 0;
            if (want > 0 && len < want && i + 1 < m.count)
            {
                const auto extra = want - len;
                for (int k = i + 1; k < m.count; ++k) m.sections[(size_t) k].startBar += extra;
                m.totalBars += extra;
                for (int k = 0; k < m.momentCount; ++k) if (m.moments[(size_t) k].bar >= m.sections[(size_t) i].startBar + len) m.moments[(size_t) k].bar += extra;
                a.reasons.add (juce::String (sectionName (type)) + " at bar " + juce::String (m.sections[(size_t) i].startBar) + ": " + juce::String (len) + " bars is short for "
                               + (type == T::Build ? "a build to climb" : "the breakdown's story (empty, hint, swell, peak)") + "; " + juce::String (want) + " bars.");
            }
        }
        if (drops == 1 && in.story >= storyClassic && m.totalBars >= 96)
            a.reasons.add ("One drop in a " + juce::String (m.totalBars) + "-bar song: a second drop (after a breakdown) gives the story somewhere to go.");
        if (drops >= 2 && m.finalLift == 0 && (in.story == storyFestival || in.story == storyExperimental))
        {
            m.finalLift = in.story == storyFestival ? 2 : 1;
            a.reasons.add ("A key change into the final drop (+" + juce::String (m.finalLift) + "): the " + storyName (in.story) + " story lifts the last drop.");
        }
        a.any = ! a.reasons.isEmpty();
        return a;
    }

    /** The plan: several candidates, scored, the best kept. */
    inline Plan makePlan (const Input& in)
    {
        using T = SectionType;
        detail::Context cx;
        cx.in = &in;
        cx.genre = in.genre == genreAuto ? genreFromTempo (in.bpm) : in.genre;
        cx.story = detail::storyShape (in.story);
        const auto& m = in.map;
        for (int i = 0; i < m.count; ++i) cx.drops += m.sections[(size_t) i].type == T::Drop ? 1 : 0;
        // moods per section (yours where you set one; the words shade them)
        for (int i = 0; i < m.count; ++i)
        {
            auto mood = i < (int) in.sectionMood.size() && in.sectionMood[(size_t) i] >= 0 ? (uint8_t) in.sectionMood[(size_t) i] : detail::defaultMood (m, i);
            int dropOrd = -1, k = 0;
            for (int j = 0; j <= i; ++j) if (m.sections[(size_t) j].type == T::Drop) { if (j == i) dropOrd = k; ++k; }
            const auto w = detail::wordsFor (in, i, dropOrd, cx.drops);
            if ((w & wDarker) && (i >= (int) in.sectionMood.size() || in.sectionMood[(size_t) i] < 0)) mood = m.sections[(size_t) i].type == T::Build ? (uint8_t) moodTension : (uint8_t) moodDark;
            cx.mood.push_back (mood);
        }
        // drop identities: a statement first, a twist in the middle, everything at the end
        for (int d = 0; d < cx.drops; ++d)
        {
            uint8_t c = chStatement;
            const auto r = detail::unit (detail::mix (in.seed, 0xc4a0u + (uint32_t) d));
            if (cx.drops == 1) c = in.story >= storyFestival ? chHuge : chStatement;
            else if (d == 0) c = in.story == storyExperimental && r < 0.4f ? chStripped : chStatement;
            else if (d == cx.drops - 1) c = in.story == storyRestrained ? chEuphoric : chHuge;
            else c = r < 0.4f ? chDark : r < 0.7f ? chStripped : chEuphoric;
            int idx = -1, k = 0;
            for (int i = 0; i < m.count; ++i) if (m.sections[(size_t) i].type == T::Drop) { if (k == d) idx = i; ++k; }
            const auto w = detail::wordsFor (in, idx, d, cx.drops);
            if (w & wDarker) c = chDark;
            if ((w & wHarder) && c != chDark) c = chHuge;
            if (w & wSimpler) c = chStripped;
            if (idx >= 0 && idx < (int) cx.mood.size() && in.genre == genrePsy && c == chEuphoric) c = chDark;
            cx.character.push_back (c);
        }
        cx.tension = detail::tensionCurve (m, in.barQ);
        // the motif: one gesture family the song keeps coming back to
        if (cx.drops > 1 && in.story != storyRestrained)
        {
            static const int motifs[] = { gTmRepeat, gTmReverse, gKyRoll, gAzRiser, gTmTape, gAnThrow };
            for (int t = 0; t < 6; ++t)
            {
                const auto id = motifs[(detail::mix (in.seed, 0x30f1u) + (uint32_t) t) % 6u];
                const auto& g = gesture (id);
                if ((in.present & (1u << g.player)) == 0 || (in.safe && ! g.safe) || in.taste[(size_t) id] <= -2.0f) continue;
                cx.motif = id;
                break;
            }
        }

        const auto slots = detail::slotsOf (in);
        Plan best;
        bool have = false;
        const auto tries = juce::jlimit (1, 16, in.candidates);
        for (int c = 0; c < tries; ++c)
        {
            auto p = detail::planOnce (in, cx, slots, detail::mix (in.seed, (uint32_t) c * 0x2545u + 7u));
            p.score = detail::scoreOf (in, p);
            if (! have || p.score.total > best.score.total + 1.0e-6f) { best = std::move (p); have = true; }
        }
        best.candidatesTried = tries;
        best.seed = in.seed;
        // the energy curve (per bar) and the breakdowns' stories
        best.energy.assign ((size_t) juce::jmax (0, m.totalBars), 0.0f);
        for (int i = 0; i < m.count; ++i)
            for (int b = m.sections[(size_t) i].startBar - 1; b < juce::jmin (m.totalBars, m.endBar (i) - 1); ++b) best.energy[(size_t) b] = m.energyOf (i);
        for (int i = 0; i < m.count; ++i)
            if (m.sections[(size_t) i].type == T::Breakdown)
            {
                BreakdownStory b;
                b.section = i;
                const auto S = (m.sections[(size_t) i].startBar - 1) * in.barQ, E = (m.endBar (i) - 1) * in.barQ, len = E - S;
                b.emptyQ = (float) S; b.hintQ = (float) (S + std::floor (len / in.barQ * 0.25) * in.barQ); b.swellQ = (float) (S + std::floor (len / in.barQ * 0.5) * in.barQ);
                b.peakQ = (float) juce::jmax (S, E - 2.0 * in.barQ); b.endQ = (float) E;
                best.breakdowns.push_back (b);
            }
        best.lints = lint (best, in);
        best.structure = adviseStructure (in);
        best.hash = detail::hashOf (best);
        return best;
    }

    //==============================================================================
    /** The plan in words. */
    inline juce::String summary (const Plan& p, const SectionMap& m, double barQ)
    {
        juce::StringArray lines;
        std::array<int, numPlayers> led {};
        int silences = 0, heroes = 0;
        for (const auto& mo : p.moments) { if (mo.hero != pNone) { ++led[mo.hero]; ++heroes; } if (mo.silence != gNone) ++silences; }
        juce::StringArray who;
        for (int pl = 0; pl < numPlayers; ++pl) if (led[(size_t) pl] > 0) who.add (juce::String (led[(size_t) pl]) + " " + playerName (pl));
        lines.add (juce::String (storyName (p.story)) + " story, " + genreName (p.genre) + (p.safe ? ", safe mode" : "") + ": " + juce::String ((int) p.moments.size())
                   + " moments, " + juce::String (heroes) + " led (" + (who.isEmpty() ? juce::String ("none") : who.joinIntoString (", ")) + "), "
                   + juce::String (silences) + " planned silences.");
        for (size_t d = 0; d < p.character.size(); ++d)
        {
            const Moment* into = nullptr;
            for (const auto& mo : p.moments) if (mo.kind == kIntoDrop && mo.dropOrdinal == (int) d) { into = &mo; break; }
            juce::String line = "Drop " + juce::String ((int) d + 1) + " is " + characterName (p.character[d]) + " (" + characterWhat (p.character[d]) + ")";
            if (into != nullptr)
            {
                if (into->hero != pNone) line << "; " << playerName (into->hero) << " leads it in with " << juce::String (gesture (into->heroGesture).name).toLowerCase();
                if (into->silence != gNone) line << ", " << juce::String (gesture (into->silence).name).toLowerCase() << " before the one";
                if (into->harmony != hNone) line << ", " << harmonyName (into->harmony);
                if (into->flags & fRecall) line << " (the motif, recalled)";
            }
            lines.add (line + ".");
        }
        for (const auto& b : p.breakdowns)
            lines.add ("The breakdown at bar " + juce::String ((int) std::lround (b.emptyQ / barQ) + 1) + ": empty, a hint at bar " + juce::String ((int) std::lround (b.hintQ / barQ) + 1)
                       + ", a swell from bar " + juce::String ((int) std::lround (b.swellQ / barQ) + 1) + ", its peak at bar " + juce::String ((int) std::lround (b.peakQ / barQ) + 1) + ".");
        if (p.motif != gNone) lines.add (juce::String ("Motif: ") + gesture (p.motif).name + ", small in the first drop, bigger in the last.");
        juce::ignoreUnused (m);
        lines.add ("Chosen from " + juce::String (p.candidatesTried) + " candidate plans: " + p.score.text() + ".");
        return lines.joinIntoString ("\n");
    }

    /** What changed between two plans, moment by moment. */
    inline juce::StringArray diff (const Plan& before, const Plan& after, double barQ)
    {
        juce::StringArray out;
        auto barOf = [barQ] (float q) { return juce::String ((int) std::floor (q / barQ + 1.0e-6) + 1); };
        auto heroText = [] (const Moment& m) { return m.hero != pNone ? juce::String (playerName (m.hero)) + " " + gesture (m.heroGesture).name : juce::String ("nobody"); };
        for (const auto& a : after.moments)
        {
            const auto i = before.indexOf (a.key);
            if (i < 0) { out.add ("Bar " + barOf (a.atQ) + ": new moment (" + heroText (a) + ")"); continue; }
            const auto& b = before.moments[(size_t) i];
            if (b.same (a)) continue;
            juce::StringArray what;
            if (b.hero != a.hero || b.heroGesture != a.heroGesture) what.add (heroText (b) + " -> " + heroText (a));
            if (b.silence != a.silence) what.add (juce::String ("silence ") + (b.silence ? gesture (b.silence).name : "none") + " -> " + (a.silence ? gesture (a.silence).name : "none"));
            if (b.gesture != a.gesture && what.isEmpty()) what.add ("supporters changed");
            if (b.harmony != a.harmony) what.add ("harmony changed");
            if (b.edit != a.edit) what.add ("arrangement edit changed");
            if (b.tempo != a.tempo) what.add ("tempo move changed");
            out.add ("Bar " + barOf (a.atQ) + ": " + what.joinIntoString ("; "));
        }
        for (const auto& b : before.moments) if (after.indexOf (b.key) < 0) out.add ("Bar " + barOf (b.atQ) + ": moment gone");
        return out;
    }

    /** Explain any second: which plugins act at `q` and why. */
    inline juce::StringArray explain (const Plan& p, const SectionMap& m, double q, double barQ, double bpm)
    {
        juce::StringArray out;
        const auto bar = (int) std::floor (q / barQ + 1.0e-9) + 1;
        int sec = -1;
        for (int i = 0; i < m.count; ++i) if (m.sections[(size_t) i].startBar <= bar) sec = i;
        if (sec >= 0)
            out.add ("Bar " + juce::String (bar) + ": " + sectionName (m.sections[(size_t) sec].type) + ", mood " + moodName (sec < (int) p.mood.size() ? p.mood[(size_t) sec] : 0)
                     + ", tension " + juce::String (bar - 1 < (int) p.tension.size() && bar >= 1 ? p.tension[(size_t) bar - 1] : 0.0f, 2));
        bool any = false;
        for (size_t i = 0; i < p.moments.size(); ++i)
        {
            const auto& mo = p.moments[i];
            if (q < mo.startQ - 1.0e-6 || q > mo.endQ + 1.0e-6) continue;
            any = true;
            for (int pl = 0; pl < numPlayers; ++pl)
            {
                if (! mo.acts (pl)) continue;
                const auto w = windowOf (mo, pl, mo.gesture[(size_t) pl], barQ, bpm);
                const auto now = pl == pSilence ? (q >= mo.silenceStartQ && q < mo.silenceEndQ) : (q >= w.first && q < w.second);
                out.add (juce::String (now ? "  now: " : "  around: ") + playerName (pl) + (mo.hero == pl ? " (hero) " : " (support) ") + gesture (mo.gesture[(size_t) pl]).name);
            }
            if (i < p.why.size()) for (const auto& w : p.why[i]) out.add ("  why: " + w);
        }
        if (! any) out.add ("No transition here: every plugin plays its section as set.");
        return out;
    }

    /** The legend: every gesture, by player. */
    inline juce::StringArray legend()
    {
        juce::StringArray out;
        for (int pl = 0; pl < numPlayers; ++pl)
        {
            juce::StringArray names;
            for (const auto& g : gestures()) if (g.player == pl) names.add (juce::String (g.name) + (g.safe ? "" : "*"));
            out.add (juce::String (playerName (pl)) + ": " + names.joinIntoString (", "));
        }
        out.add ("* not used in safe mode");
        return out;
    }

    /** Your taste, in words: what you rated up and down. */
    inline juce::String tasteReport (const std::array<float, numGestures>& taste)
    {
        std::vector<std::pair<float, int>> v;
        for (int i = 1; i < numGestures; ++i) if (std::abs (taste[(size_t) i]) > 0.01f) v.push_back ({ taste[(size_t) i], i });
        if (v.empty()) return "No ratings yet: rate a moment up or down in the inspector.";
        std::sort (v.begin(), v.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
        juce::StringArray up, down;
        for (const auto& [s, id] : v) (s > 0 ? up : down).add (juce::String (gesture (id).name) + " (" + (s > 0 ? "+" : "") + juce::String ((int) s) + ")");
        juce::String out;
        if (! up.isEmpty()) out << "You like: " << up.joinIntoString (", ") << ".\n";
        if (! down.isEmpty()) out << "You avoid: " << down.joinIntoString (", ") << ".\n";
        juce::StringArray sig;
        for (const auto& [s, id] : v) if (s >= 2.0f) sig.add (gesture (id).name);
        if (! sig.isEmpty()) out << "Your signature moves (used more, suite-wide): " << sig.joinIntoString (", ") << ".";
        return out.trimEnd();
    }

    //==============================================================================
    // Saving: moments, requests and pins as text (state and profile files)
    inline juce::String momentToString (const Moment& m)
    {
        juce::StringArray f;
        f.add (juce::String (m.key)); f.add (juce::String (m.kind)); f.add (juce::String (m.flags)); f.add (juce::String (m.section)); f.add (juce::String (m.dropOrdinal));
        f.add (juce::String (m.hero)); f.add (juce::String (m.heroGesture)); f.add (juce::String (m.heroRole));
        juce::StringArray g; for (auto x : m.gesture) g.add (juce::String (x)); f.add (g.joinIntoString ("."));
        juce::StringArray l; for (const auto& x : m.legs) l.add (juce::String (x.player) + "/" + juce::String (x.gesture) + "/" + juce::String (x.atQ)); f.add (l.joinIntoString ("."));
        for (auto v : { m.atQ, m.startQ, m.endQ }) f.add (juce::String (v));
        f.add (juce::String (m.silence)); f.add (juce::String (m.silenceStartQ)); f.add (juce::String (m.silenceEndQ));
        for (auto v : { m.cutTails, m.mood, m.character, m.harmony, m.tempo, m.edit }) f.add (juce::String (v));
        for (auto v : { m.editStartQ, m.editEndQ, m.budget, m.spent, m.tension, m.amount, m.confidence, m.premixDb, m.hookDipDb, m.lowClearBeats,
                        m.evenDb, m.buildHoldDb, m.dryBefore, m.narrowBefore, m.wideOnHit }) f.add (juce::String (v));
        return f.joinIntoString (",");
    }
    inline bool momentFromString (const juce::String& text, Moment& m)
    {
        const auto f = juce::StringArray::fromTokens (text, ",", "");
        if (f.size() < 37) return false;
        int i = 0;
        m.key = (uint32_t) f[i++].getLargeIntValue(); m.kind = (uint16_t) f[i++].getIntValue(); m.flags = (uint16_t) f[i++].getIntValue();
        m.section = (int16_t) f[i++].getIntValue(); m.dropOrdinal = (int8_t) f[i++].getIntValue();
        m.hero = (uint8_t) f[i++].getIntValue(); m.heroGesture = (uint8_t) f[i++].getIntValue(); m.heroRole = (uint8_t) f[i++].getIntValue();
        const auto g = juce::StringArray::fromTokens (f[i++], ".", "");
        for (int k = 0; k < numPlayers && k < g.size(); ++k) m.gesture[(size_t) k] = (uint8_t) juce::jlimit (0, (int) numGestures - 1, g[k].getIntValue());
        const auto l = juce::StringArray::fromTokens (f[i++], ".", "");
        for (int k = 0; k < 3 && k < l.size(); ++k)
        {
            const auto x = juce::StringArray::fromTokens (l[k], "/", "");
            if (x.size() >= 3) m.legs[(size_t) k] = { (uint8_t) x[0].getIntValue(), (uint8_t) x[1].getIntValue(), x[2].getFloatValue() };
        }
        m.atQ = f[i++].getFloatValue(); m.startQ = f[i++].getFloatValue(); m.endQ = f[i++].getFloatValue();
        m.silence = (uint8_t) f[i++].getIntValue(); m.silenceStartQ = f[i++].getFloatValue(); m.silenceEndQ = f[i++].getFloatValue();
        for (auto* v : { &m.cutTails, &m.mood, &m.character, &m.harmony, &m.tempo, &m.edit }) *v = (uint8_t) f[i++].getIntValue();
        for (auto* v : { &m.editStartQ, &m.editEndQ, &m.budget, &m.spent, &m.tension, &m.amount, &m.confidence, &m.premixDb, &m.hookDipDb, &m.lowClearBeats,
                         &m.evenDb, &m.buildHoldDb, &m.dryBefore, &m.narrowBefore, &m.wideOnHit }) *v = f[i++].getFloatValue();
        if (m.hero != pNone && m.hero >= numPlayers) m.hero = pNone;
        return true;
    }
}
