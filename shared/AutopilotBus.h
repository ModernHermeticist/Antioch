#pragma once

// The autopilot: what the suite manages by itself, moment to moment, so the tune can come out of
// the sounds and effects you choose. Four groups, each with one switch (Ouroboros' Autopilot page):
//   harshness   - harsh moments on the master, spiky bands, drive on high notes, bright tails,
//                 resonances several tracks share
//   stability   - runaway feedback and filter resonance, one-off peaks, DC and subsonics, very low
//                 bass notes, level wobble, stereo collapse, clicks at section changes
//   arrangement - the density manager (too many parts at once) and turn-taking between melodic parts
//   mixing      - tracks making room for each other moment to moment: masking (with the lead's notes),
//                 the low end's owner, transients on the kick, out-of-key FX, mono lows, even bass
//                 notes and drum hits, harmonics for small speakers, moving resonances, the noise
//                 floor, effect returns, a correction per kind of section, reverb making room
// The bus also carries the live signals the plugins share for it (the master's harsh moment, the
// resonances each track hears) and a count of everything done, for the health readout.

#include "SharedMemory.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace suite::Autopilot
{
    enum Group : uint32_t { harshness = 1u, stability = 2u, arrangement = 4u, mixing = 8u };

    /** Each automatic fix, with its own switch inside its group (it runs when both are on). */
    enum Feature : int
    {
        // harshness
        harshDipF, spikeTamerF, driveEaseF, tailDarkenF, sharedResonanceF,
        // stability
        runawayGuardF, filterRingingF, dcFilterF, lowBassFoldF, stereoRideF, levelMatchF, driveLevelF,
        // arrangement
        densityF, turnTakingF, hangoverTrimF,
        // mixing
        maskingF, liftF, lowOwnerF, kickTransientsF, monoLowsF, bassNotesF, harmonicsF, sootheF, drumHitsF, noiseFloorF,
        effectReturnsF, sectionEqF, reverbRoomF, noteRoomF, tempoSpaceF, kickBassDipF, layerSpreadF, kickTuneF, drumBusGlueF,
        fxInKeyF, subBeforeKickF, vocalPopsF, vocalBreathsF, clickGuardF, balanceF, wetMonoSafeF, humF, deessSpaceF,
        cleanEndingF, layerAlignF, equalBypassF,
        // arrangement (added later: kept at the end so saved switches keep their places)
        transmuteF,
        headroomF,          // mixing: keep the premaster's -6 dBFS headroom (every Quintessence down together)
        // label-ready (each acts where Ouroboros' checks find the problem; LabelAutoFix.h)
        kickShapeF, leadTimbreF, padMotionF, clapTransientF, hygieneF, sectionFxF, phraseFillsF, arrangeEntriesF, preDropCurveF,
        loopVariationF, vocalTimingF, vocalClarityF, sibilanceF, vocalSteadyF, throwsF, squashF, peakTrimF, stressFixF,
        bassClearKickF, bassInKeyF, breakdownLowCutF, reverbLowsF, monoDoublesF, hatsToneF, harshAttackF, fxUnderLeadF,
        tightSwingF, accentsF, ghostLiftF, bassRootsF, dropTensionF, melodyClimbF, voicingRoomF, idleSleepF, inputStageF,
        preDelayF, widthBuildF, contrastF,
        // label-ready, the fourth set
        roleTimbreF, journeyF, sectionWidthF, phraseDynamicsF, kickBassPairF, earCandySynthF, goalBreakdownF, goalDrop2F, goalCounterF, goalBassF, goalTeaserF, transitionStyleF, harshExposureF, learnKeptF, learnUndoneF, tasteF, regressionGuardF, preRenderF, subsonicF, pacingF, subPhaseF, bassLengthF, keyMonoF, kickTailF, subHandoverF, echoTighterF, reverbDuckF, motionF, airF, riserTopF, chokeF, grooveLockF, arpOffHookF, chordRhythmF, peaksLateF, fillsPickupsF, sectionEdgesF, finalPeakF, breakdownBedF, driveF, macroRidesF, layerSplitF, subFillF, spaceSceneF, sectionToneF, depthF, counterLaterF, callResponseF, vocalHarmonyF, reExportF, outroGrooveF, breakdownSwellF,
        numFeatures
    };
    inline const char* featureName (int f)
    {
        static const char* n[] = {
            "Dip the tracks causing a harsh moment", "Spike tamer on bright parts", "Ease the lead's drive on high notes", "Darken tails while it's harsh", "Share resonance cuts between tracks",
            "Catch runaway echo and shimmer (Antioch)", "Catch a runaway effect (Transmutation)", "DC and rumble filter (Prima Materia)", "Fold bass notes under C1 up (exports)",
            "Narrow an image that cancels in mono", "Hold the space to the input's level (Antioch)", "Feed the drive a steady level (Vibration)",
            "Thin parts when too many play (exports)", "Chops and candy take turns with the hook (exports)", "Trim notes ringing into a clashing chord (exports)",
            "Dip under more important parts", "Lift a masked lead or vocal", "Leave the lows to the kick and bass", "Soften attacks landing on the kick", "Mono lows (under 120 Hz)",
            "Even out bass notes", "Harmonics for small speakers (bass)", "Soothe ringing bands", "Even out drum hits", "Pull down the noise floor",
            "Cuts on effect returns", "Own EQ for grooves, intro and outro", "Reverb pumps and steps back (kick, lead, vocal)", "Reverb dips at the lead's note", "Reverb timed to the tempo",
            "Bass dips at the kick's note", "Spread layers left and right", "Tune the kick to the key", "Glue on a drum bus",
            "Out-of-key FX re-pitched", "Clear the sub before each kick", "Cut vocal pops", "Lower vocal breaths", "Smooth clip starts and ends", "Re-centre a leaning track", "Keep the reverb mono-safe",
            "Remove hum", "De-ess the space", "Fade the space after the last bar", "Line up layered parts", "Equal loudness for the A/B bypass",
            "Creative moves at transitions (Transmutation)", "Keep the premaster's headroom (-6 dBFS)",
            "Label: shape the kick (click, tail, sub)", "Label: the lead's timbre", "Label: movement on static pads", "Label: clap and snare attacks",
            "Label: noise and aliasing in samples", "Label: mark every section change", "Label: fills at phrase ends", "Label: the intro builds, the outro mirrors it (Correspondence)",
            "Label: a curve into each drop", "Label: vary repeated 8-bar loops (Correspondence)", "Label: early vocal delayed onto the grid", "Label: room for the vocal at 2-4 kHz",
            "Label: sibilance", "Label: the vocal steady between sections", "Label: throws at the vocal's phrase ends (Antioch)", "Label: let squashed sections breathe",
            "Label: trim the track behind the loudest peak", "Label: fixes from the mastering stress test",
            "Label: bass notes stop before the kick (Correspondence)", "Label: bass notes in key (Correspondence)", "Label: no rumble in the breakdowns",
            "Label: no lows in the reverbs", "Label: doubles that survive mono", "Label: the hats against the label", "Label: soften biting attacks",
            "Label: noise FX under the lead", "Label: one swing for every part (Correspondence)", "Label: accents line up (Correspondence)",
            "Label: ghost notes heard (Correspondence)", "Label: the bass on the chords' roots (Correspondence)", "Label: tension into each drop (Correspondence)",
            "Label: the melody climbs through the song (Correspondence)", "Label: chords leave the lead room (Correspondence)", "Label: idle plugins sleep",
            "Label: stage each track's input (Quintessence)", "Label: reverb pre-delay from the tempo (Antioch)", "Label: width opens into the drop",
            "Label: drops against breakdowns like the label",
            "Label: each part's tone where it's exposed",
            "Label: the loudness journey section by section",
            "Label: width section by section",
            "Label: every phrase breathes",
            "Label: kick and bass paired (Kybalion)",
            "Label: ear-candy one-shots where nothing new happens",
            "Label: the breakdown's emotion (Correspondence)",
            "Label: drop 2 differs from drop 1 (Correspondence)",
            "Label: the counter-line's space (Correspondence)",
            "Label: the drop's bass fits the chords (Correspondence)",
            "Label: the hook teased early (Correspondence)",
            "Label: the director's moves in the label's style",
            "Label: harshness over the whole song",
            "Label: learn from the fixes you keep",
            "Label: learn from the fixes you undo",
            "Label: your taste on top of the label's tone",
            "Label: undo a change that breaks a signed-off area",
            "Label: check everything when FL renders",
            "Label: subsonic filter (Quintessence)",
            "Label: slow down when fixes keep failing",
            "Label: sub phase bar by bar (Kybalion)",
            "Label: bass note lengths per section (Correspondence)",
            "Label: mono lows follow the key",
            "Label: shorter kick tail in the drops (Quintessence)",
            "Label: the sub hands over through builds (Quintessence)",
            "Label: fewer echoes when it's busy (Antioch)",
            "Label: the reverb steps back for the lead (Antioch)",
            "Label: movement on parts that never change",
            "Label: air on top",
            "Label: the riser peaks under the drop",
            "Label: hats and FX cut before the drop (Quintessence)",
            "Label: the bass locks with the kit (Correspondence)",
            "Label: the arp leaves the hook's rhythm (Correspondence)",
            "Label: chord rhythm per section (Correspondence)",
            "Label: melodic peaks late in the phrase (Correspondence)",
            "Label: fills and pickups (Correspondence)",
            "Label: section openings and endings",
            "Label: the final drop is the peak (Correspondence)",
            "Label: a bed in thin breakdowns (Correspondence)",
            "Label: drive where the lead doesn't cut (Quintessence)",
            "Label: macro rides in the exports (Correspondence)",
            "Label: layers in one octave split",
            "Label: sub under a thin bass",
            "Label: drier drops, more space in breakdowns",
            "Label: each section's tone (Quintessence)",
            "Label: the pads behind the lead",
            "Label: the counter-melody from drop 2 (Correspondence)",
            "Label: call and response in the hook (Correspondence)",
            "Label: vocal harmonies written (Solve et Coagula)",
            "Label: re-export the parts after a writing fix (Correspondence)",
            "Label: the outro keeps the groove (Correspondence)",
            "Label: a swell in long breakdowns" };
        static_assert (sizeof (n) / sizeof (n[0]) == numFeatures, "a name for every fix");
        static_assert (numFeatures <= 160, "five words of switches");
        return f >= 0 && f < numFeatures ? n[f] : "";
    }
    inline uint32_t featureGroup (int f)
    {
        if (f == transmuteF) return 4u;
        if (f >= kickShapeF)
            switch (f)
            {
                case sectionFxF: case phraseFillsF: case arrangeEntriesF: case preDropCurveF: case loopVariationF: case tightSwingF: case accentsF:
                case ghostLiftF: case bassRootsF: case dropTensionF: case melodyClimbF: case voicingRoomF: case bassClearKickF: case bassInKeyF: case contrastF:
                    return 4u;
                case harshAttackF: case harshExposureF: case airF: return 1u;
                case earCandySynthF: case goalBreakdownF: case goalDrop2F: case goalCounterF: case goalBassF: case goalTeaserF: case transitionStyleF: case bassLengthF: case grooveLockF: case arpOffHookF: case chordRhythmF: case peaksLateF: case fillsPickupsF: case sectionEdgesF: case finalPeakF: case breakdownBedF: case macroRidesF: case counterLaterF: case callResponseF: case reExportF: case outroGrooveF: case breakdownSwellF:
                    return 4u;
                case learnKeptF: case learnUndoneF: case tasteF: case regressionGuardF: case preRenderF: case subsonicF: case pacingF:
                    return 2u;
                case idleSleepF: case inputStageF: return 2u;
                default: return 8u;
            }
        return f <= sharedResonanceF ? 1u : f <= driveLevelF ? 2u : f <= hangoverTrimF ? 4u : 8u;
    }

    enum Counter
    {
        harshDips, spikesTamed, driveEased, tailsDamped, sharedResonances,
        runawaysCaught, peaksCaught, lowNotesFolded, wobbleRidden, widthRidden, clicksSmoothed,
        partsThinned, turnsTaken,
        masksDipped, partsLifted, lowEndCleared, transientsSoftened, outOfKeyNotched, bassNotesEvened, harmonicsAdded,
        resonancesSoothed, hitsEvened, tailsQuietened, returnsCleaned, sectionCorrections, reverbMadeRoom, monoLowsKept,
        kickBassDips, layersSpread, kickTuned, drumBusGlued,
        levelMatched, subTailsCleared, plosivesCut, breathsLowered, clicksGuarded, fxRepitched, balanceCentred, wetNarrowed,
        humRemoved, spaceDeessed, driveLevelled, hangoversTrimmed, endingsFaded, layersAligned,
        numCounters
    };
    inline const char* counterName (int c)
    {
        static const char* n[] = { "harsh moments dipped", "spiky bands tamed", "drive eased on high notes", "tails darkened", "shared resonances",
                                   "runaways caught", "peaks caught", "low bass notes folded up", "level wobbles ridden", "stereo collapses ridden",
                                   "section-change clicks smoothed", "parts thinned (density)", "turns taken (melodic parts)",
                                   "masking dips (making room)", "lead / vocal lifted through a busy moment", "low end left to the kick and bass",
                                   "transients softened on the kick", "out-of-key FX notes cut", "bass notes evened out", "harmonics added for small speakers",
                                   "moving resonances soothed", "drum hits evened out", "quiet tails cleaned (noise floor)", "effect returns cleaned",
                                   "groove / intro / outro corrections (EQ per section)", "reverb making room (kick, lead, vocal)", "tracks with mono lows",
                                   "bass making room for the kick's note", "layers spread apart", "kicks tuned to the key", "drum buses glued",
                                   "effects held to the input's level", "bass sub cleared before kicks", "vocal pops cut", "vocal breaths lowered",
                                   "clicks at clip starts and ends smoothed", "FX re-pitched into the key", "tracks re-centred (left/right)", "reverb kept mono-safe",
                                   "hum removed", "the space de-essed", "drive fed a steady level", "notes trimmed at chord changes",
                                   "song endings faded clean", "layered parts lined up" };
        return c >= 0 && c < numCounters ? n[c] : "";
    }

    constexpr int maxHarshIds = 4, maxResonances = 16, maxLive = 32, liveBands = 10;

    namespace detail
    {
        struct Shared
        {
            std::atomic<uint32_t> disabled;                 // groups switched off
            std::atomic<uint32_t> featuresOff[5];           // single fixes switched off (bit per Feature)
            std::atomic<float> harshAmount;                 // 0..1: how harsh the master is right now
            std::atomic<int32_t> harshBand;                 // octave band of the harshness (6 = 2 kHz, 7 = 4 kHz, 8 = 8 kHz)
            std::atomic<uint32_t> harshIds[maxHarshIds];    // the tracks causing it (low 12 bits of their Quintessence's id)
            std::atomic<uint32_t> harshMs;
            std::atomic<uint32_t> counters[numCounters];
            std::atomic<float> resonanceHz[maxResonances];  // the resonances each Quintessence hears now
            std::atomic<uint32_t> resonanceId[maxResonances], resonanceMs[maxResonances];
            // What each Quintessence plays right now: its role and ten octave bands (dB), so tracks
            // can make room for each other; and the lead's note (Vibration)
            std::atomic<uint32_t> liveId[maxLive], liveMs[maxLive];
            std::atomic<int32_t> liveRole[maxLive];
            std::atomic<float> liveBand[maxLive][liveBands];
            std::atomic<double> liveOnset[maxLive][16];   // each track's last 16 attacks (song position, quarters)
            std::atomic<uint32_t> liveOnsetNext[maxLive];
            std::atomic<float> leadHz;
            std::atomic<uint32_t> leadMs;
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_autopilot_v6", sizeof (Shared)));
                return m != nullptr ? m : new Shared {};
            }();
            return *s;
        }
        inline uint32_t hash32 (uint32_t x) noexcept { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
        inline uint32_t nowMs() noexcept
        {
            using namespace std::chrono;
            return (uint32_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
        }
    }

    inline bool on (Group g) noexcept { return (detail::shared().disabled.load() & g) == 0; }
    inline void set (Group g, bool enabled) noexcept
    {
        auto& d = detail::shared().disabled;
        if (enabled) d.fetch_and (~(uint32_t) g); else d.fetch_or ((uint32_t) g);
    }

    /** One fix: on when its group is on and it hasn't been switched off itself. */
    inline bool on (Feature f) noexcept
    {
        auto& s = detail::shared();
        return (s.disabled.load() & featureGroup (f)) == 0 && (s.featuresOff[f / 32].load() & (1u << (f % 32))) == 0;
    }
    inline bool switchedOn (Feature f) noexcept { return (detail::shared().featuresOff[f / 32].load() & (1u << (f % 32))) == 0; }
    inline void set (Feature f, bool enabled) noexcept
    {
        auto& w = detail::shared().featuresOff[f / 32];
        if (enabled) w.fetch_and (~(1u << (f % 32))); else w.fetch_or (1u << (f % 32));
    }
    inline uint64_t disabledFeatures() noexcept { auto& s = detail::shared(); return (uint64_t) s.featuresOff[0].load() | ((uint64_t) s.featuresOff[1].load() << 32); }
    inline void setDisabledFeatures (uint64_t m) noexcept { auto& s = detail::shared(); s.featuresOff[0].store ((uint32_t) m); s.featuresOff[1].store ((uint32_t) (m >> 32)); }
    /** Switches from 64 on (the label-ready ones). */
    inline uint32_t disabledFeaturesHigh() noexcept { return detail::shared().featuresOff[2].load(); }
    inline void setDisabledFeaturesHigh (uint32_t m) noexcept { detail::shared().featuresOff[2].store (m); }
    /** Every switch's word (from 64 on: words 2..4), as hex "a,b,c" for the project. */
    inline juce::String disabledFeaturesRest()
    {
        auto& s = detail::shared();
        return juce::String::toHexString ((int) s.featuresOff[2].load()) + "," + juce::String::toHexString ((int) s.featuresOff[3].load()) + "," + juce::String::toHexString ((int) s.featuresOff[4].load());
    }
    inline void setDisabledFeaturesRest (const juce::String& hex)
    {
        auto& s = detail::shared();
        const auto w = juce::StringArray::fromTokens (hex, ",", "");
        for (int i = 0; i < 3; ++i) s.featuresOff[2 + i].store (i < w.size() ? (uint32_t) w[i].getHexValue64() : 0u);
    }

    inline uint32_t disabledGroups() noexcept { return detail::shared().disabled.load(); }   // saved with Ouroboros
    inline void setDisabledGroups (uint32_t mask) noexcept { detail::shared().disabled.store (mask & 15u); }

    inline void count (Counter c, uint32_t n = 1) noexcept { detail::shared().counters[c].fetch_add (n); }
    inline uint32_t countOf (Counter c) noexcept { return detail::shared().counters[c].load(); }

    /** Mentalism: the master is harsh right now (amount 0..1, the band, the tracks causing it). */
    inline void publishHarsh (float amount, int band, const uint32_t* ids, int n) noexcept
    {
        auto& s = detail::shared();
        for (int i = 0; i < maxHarshIds; ++i) s.harshIds[i].store (i < n ? (ids[i] & 0xfffu) | 0x1000u : 0u);
        s.harshBand.store (band);
        s.harshAmount.store (amount);
        s.harshMs.store (detail::nowMs());
    }
    /** How harsh the master is now (0 if it isn't, or the news is older than half a second). */
    inline float harshNow (int* band = nullptr) noexcept
    {
        auto& s = detail::shared();
        if (detail::nowMs() - s.harshMs.load() > 500u) return 0.0f;
        if (band != nullptr) *band = s.harshBand.load();
        return s.harshAmount.load();
    }
    /** ...and whether this track (its Quintessence's id) is one of the causes. */
    inline bool harshCulprit (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        for (auto& h : s.harshIds) if (h.load() == ((id & 0xfffu) | 0x1000u)) return true;
        return false;
    }

    /** Quintessence: the resonance this track hears now (0 = none). Posts expire after 20 s. */
    inline void postResonance (uint32_t id, float hz) noexcept
    {
        auto& s = detail::shared();
        int slot = -1, oldest = 0;
        for (int i = 0; i < maxResonances; ++i)
        {
            if (s.resonanceId[i].load() == id) { slot = i; break; }
            if (s.resonanceMs[i].load() < s.resonanceMs[oldest].load()) oldest = i;
        }
        if (slot < 0) slot = oldest;
        s.resonanceId[slot].store (id);
        s.resonanceHz[slot].store (hz);
        s.resonanceMs[slot].store (detail::nowMs());
    }
    /** How many other tracks hear a resonance within a sixth of an octave of `hz`. */
    /** Every resonance a track has posted in the last 20 s (Hz). */
    inline std::vector<float> resonancesNow()
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        std::vector<float> out;
        for (int i = 0; i < maxResonances; ++i)
            if (const auto hz = s.resonanceHz[i].load(); hz > 0.0f && now - s.resonanceMs[i].load() <= 20000u) out.push_back (hz);
        return out;
    }
    inline int othersRingingAt (uint32_t id, float hz) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        int n = 0;
        for (int i = 0; i < maxResonances; ++i)
        {
            const auto other = s.resonanceHz[i].load();
            if (s.resonanceId[i].load() == id || other <= 0.0f || hz <= 0.0f || now - s.resonanceMs[i].load() > 20000u) continue;
            if (std::abs (std::log2 (other / hz)) < 1.0f / 6.0f) ++n;
        }
        return n;
    }

    /** Who goes first when two parts want the same space (role index as in Quintessence: 1 kick,
        2 bass, 3 hats/perc, 4 pads, 5 lead, 6 vocal, 7 FX, 8 bus). 0 = takes no part. */
    inline int priorityOf (int role) noexcept
    {
        switch (role) { case 6: return 5; case 5: return 4; case 1: case 2: return 3; case 4: return 2; case 3: case 7: return 1; default: return 0; }
    }

    /** Quintessence, every block: what this track plays now. */
    inline void postLive (uint32_t id, int role, const float* bandsDb) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        int slot = -1, oldest = 0;
        for (int i = 0; i < maxLive; ++i)
        {
            if (s.liveId[i].load() == id) { slot = i; break; }
            if (s.liveMs[i].load() < s.liveMs[oldest].load()) oldest = i;
        }
        if (slot < 0)
        {
            slot = oldest;
            for (auto& o : s.liveOnset[slot]) o.store (0.0);
        }
        for (int b = 0; b < liveBands; ++b) s.liveBand[slot][b].store (bandsDb[b]);
        s.liveRole[slot].store (role);
        s.liveId[slot].store (id);
        s.liveMs[slot].store (now);
    }
    inline void withdrawLive (uint32_t id) noexcept
    {
        auto& s = detail::shared();
        for (int i = 0; i < maxLive; ++i) if (s.liveId[i].load() == id) { s.liveId[i].store (0); s.liveMs[i].store (0); }
    }
    /** Calls fn (role, bandsDb[10]) for every other track heard in the last 300 ms. */
    template <typename Fn> inline void forEachLive (uint32_t self, Fn&& fn) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        for (int i = 0; i < maxLive; ++i)
        {
            const auto id = s.liveId[i].load();
            if (id == 0 || id == self || now - s.liveMs[i].load() > 300u) continue;
            float bands[liveBands];
            for (int b = 0; b < liveBands; ++b) bands[b] = s.liveBand[i][b].load();
            fn ((int) s.liveRole[i].load(), (const float*) bands);
        }
    }
    /** Quintessence: an attack at song position `ppq` on this track (for lining up layered parts). */
    inline void postOnset (uint32_t id, double ppq) noexcept
    {
        auto& s = detail::shared();
        for (int i = 0; i < maxLive; ++i)
            if (s.liveId[i].load() == id)
            {
                const auto k = s.liveOnsetNext[i].fetch_add (1) % 16u;
                s.liveOnset[i][k].store (ppq);
                return;
            }
    }
    /** Calls fn (role, onsets[16]) for every other track heard in the last 300 ms (0 = no onset). */
    template <typename Fn> inline void forEachLiveOnsets (uint32_t self, Fn&& fn) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        for (int i = 0; i < maxLive; ++i)
        {
            const auto id = s.liveId[i].load();
            if (id == 0 || id == self || now - s.liveMs[i].load() > 300u) continue;
            double on[16];
            for (int k = 0; k < 16; ++k) on[k] = s.liveOnset[i][k].load();
            fn ((int) s.liveRole[i].load(), (const double*) on);
        }
    }
    /** Vibration: the lead's note now (Hz). */
    inline void publishLeadPitch (float hz) noexcept { detail::shared().leadHz.store (hz); detail::shared().leadMs.store (detail::nowMs()); }
    inline float leadPitch() noexcept
    {
        auto& s = detail::shared();
        return detail::nowMs() - s.leadMs.load() > 300u ? 0.0f : s.leadHz.load();
    }

    inline void reset() noexcept   // tests
    {
        auto& s = detail::shared();
        s.disabled.store (0); s.featuresOff[0].store (0); s.featuresOff[1].store (0); s.featuresOff[2].store (0); s.featuresOff[3].store (0); s.featuresOff[4].store (0); s.harshAmount.store (0.0f); s.harshMs.store (0);
        for (auto& h : s.harshIds) h.store (0);
        for (auto& c : s.counters) c.store (0);
        for (int i = 0; i < maxResonances; ++i) { s.resonanceId[i].store (0); s.resonanceHz[i].store (0.0f); s.resonanceMs[i].store (0); }
        for (int i = 0; i < maxLive; ++i) { s.liveId[i].store (0); s.liveMs[i].store (0); s.liveOnsetNext[i].store (0); for (auto& o : s.liveOnset[i]) o.store (0.0); }
        s.leadHz.store (0.0f); s.leadMs.store (0);
    }
}
