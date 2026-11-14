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
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace suite::Autopilot
{
    /** The twelve switches you see (each fix lives in one; a fix runs when its group is on). The first
        four keep their old bits, so saved projects keep their meaning; "mixing" is now the tone and
        balance fixes left after the others took theirs. */
    enum Group : uint32_t
    {
        harshness = 1u, stability = 2u, arrangement = 4u, mixing = 8u,
        lowEnd = 16u, space = 32u, vocals = 64u, drums = 128u, width = 256u, dynamics = 512u, transitions = 1024u, behaviour = 2048u
    };
    constexpr int numGroups = 12;
    /** Every mixing group (what an old project's "Mixing off" means now). */
    constexpr uint32_t allMixing = mixing | lowEnd | space | vocals | drums | width | dynamics | transitions;
    inline Group groupAt (int i) noexcept { return (Group) (1u << juce::jlimit (0, numGroups - 1, i)); }
    inline const char* groupName (Group g) noexcept
    {
        switch (g)
        {
            case harshness:   return "Harshness";
            case stability:   return "Safety & stability";
            case arrangement: return "Arrangement & writing";
            case mixing:      return "Tone & balance";
            case lowEnd:      return "Low end (kick, bass, sub)";
            case space:       return "Space (reverb, delay, sends)";
            case vocals:      return "Vocals";
            case drums:       return "Drums & transients";
            case width:       return "Width & stereo";
            case dynamics:    return "Dynamics & loudness";
            case transitions: return "Transitions & FX";
            case behaviour:   return "How the autopilot works";
            default:          return "";
        }
    }
    inline const char* groupDetail (Group g) noexcept
    {
        switch (g)
        {
            case harshness:   return "harsh moments, spiky bands, drive on high notes, bright tails, shared resonances";
            case stability:   return "runaway feedback, peaks, DC and rumble, level wobble, clicks, failsafes";
            case arrangement: return "busy moments thinned, parts taking turns, fills, entries, the writing goals";
            case mixing:      return "tracks making room for each other: masking, resonances, tone per section";
            case lowEnd:      return "kick and bass together, sub per note, mono lows, low cuts";
            case space:       return "reverb and delay per section and role, ducked returns, pre-delay";
            case vocals:      return "the vocal on top: presence, sibilance, pops, breaths, doubles";
            case drums:       return "transients, claps, hats, cymbals, drum glue";
            case width:       return "width per section, mono safety, panning, layers spread";
            case dynamics:    return "levels, glue, punch, peaks, headroom and the budgets";
            case transitions: return "risers, impacts, sweeps and fills around the sections";
            case behaviour:   return "self-checks, judging, one fix at a time, undo, learning";
            default:          return "";
        }
    }

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
        // mix fixes (the fifth set): Quintessence's LabelDsp3 in its order, then the other plugins'
        mxPhraseTrimF, mxHookLiftF, mxNoteEvenF, mxInputLevellerF, mxChainNeutralF, mxImpactDuckF, mxRiserRoomF, mxAutoLowCutF, mxBackingHighCutF, mxSideAirF, mxSideMudF, mxMatchedLowCutF, mxPadUnderLeadF, mxArpUnderHookF, mxHatsUnderSibilanceF, mxBassLowDuckF, mxFxUnderVocalF, mxClapUnderVocalF, mxBreakdownDrumLowCutF, mxBuildHighPassF, mxChordFollowF, mxDrumParallelF, mxUpwardBreakdownF, mxGluePerSectionF, mxExpanderF, mxVocalPeakF, mxPadSustainF, mxPanLayoutF, mxMonoWidenF, mxBandWidthF, mxCorrelationGuardF, mxBusSatDropF, mxExciterAirF, mxKickClickF, mxBassMidDriveF, mxDropPunchF, mxBreakdownDrumHighCutF, mxPostDropSettleF, mxFinalDropLiftF, mxIntroOutroToneF, mxFillDuckF, mxVocalDoublerF, mxVocalAirF, mxVocalPresenceF, mxHatRidesF, mxClapTailF, mxFxHighPassF, mxRiserEnergyF, mxFxTailDarkF, mxTruePeakF, mxDcBlockF, mxPumpSectionF, mxAutoFitKickF, mxSendsSectionF, mxRolePreDelayF, mxDecaySectionF, mxDelayDivisionF, mxEchoDampF, mxVerbGateF, mxPadRoomF, mxClapVerbF, mxPresenceEaseF, mxDriveSectionF, mxDepthSectionF, mxToneBudgetF, mxArbitrationF, mxStepBudgetF, mxRollbackAreaF, mxProvenanceF, mxLowPolarityF, mxLayerRolesF, mxKickSectionF, mxLfHeadroomF,
        // the sixth set: Quintessence's LabelDsp4 in its order, its safety, Antioch's, the rest, the autopilot's own behaviours
        mx2PhaseRotateF, mx2Drop2BrightF, mx2OutroThinF, mx2BreakdownEntryF, mx2BackingVocalsF, mx2VocalProximityF, mx2BoxinessF, mx2CymbalWashF, mx2PercKeyF, mx2BandBalanceF, mx2LaterDropToneF, mx2LowMidDropsF, mx2SubPerNoteF, mx2FillOpenF, mx2LeadEnvelopeF, mx2PadSwellF, mx2KickTailTempoF, mx2GhostEvenF, mx2HaasF, mx2PanLimitF, mx2WidthDrop2F, mx2RiserWidenF, mx2SideLowsSectionF, mx2PickupLiftF, mx2BassChokeF, mx2IntroEaseF, mx2BuildTensionF, mx2PostImpactF, mx2RepeatToneF, mx2VocalVsLeadF, mx2VocalHeldDoubleF, mx2VocalAirSectionF, mx2SubVsKickF, mx2BassHarmonicsSectionF, mx2DownlifterGateF, mx2ImpactEnergyF, mx2SweepToDropF, mx2FxUnderHookF, mx2LevelBudgetF, mx2BuildBudgetF, mx2HeadroomForecastF, mx2BoundarySmoothF, mx2FailSafeF, mx2ReverbToneF, mx2DuckedDelayF, mx2VerbForVocalF, mx2VerbWidthRoleF, mx2SizeSectionF, mx2PreDelayDensityF, mx2FeedbackSectionF, mx2EchoPanRoleF, mx2VerbLowCutSectionF, mx2BreakdownLastBarF, mx2PlosiveSendF, mx2PhasePerSectionF, mx2LeadFizzF, mx2ClapLayersF, mx2SelfCheckF, mx2LearnPluginF, mx2SettleF, mx2CpuBudgetF, mx2FixOrderF, mx2ConfirmF, mx2AreaPriorityF, mx2FadeUndoF, mx2ValidPlaysF, mx2NoiseFloorF, mx2JudgeWhereF, mx2OneAtATimeF, mx2OwnershipF, mx2RenderFreezeF, mx2PlateauF, mx2MilestoneFixesF,
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
            "Label: a swell in long breakdowns",
            "Mix: phrase rides from Ouroboros (Quintessence)",
            "Mix: the hook's entry lifted",
            "Mix: high notes evened out",
            "Mix: input leveller",
            "Mix: the chain kept loudness-neutral",
            "Mix: dip under each drop's impact",
            "Mix: room for the riser in builds",
            "Mix: low cut from the part's lowest note",
            "Mix: backing parts' top under the lead",
            "Mix: air in the sides only",
            "Mix: mud cut in the sides only",
            "Mix: matched low cuts on kick and bass",
            "Mix: pads dip at the lead's note",
            "Mix: the arp under the hook",
            "Mix: hats under the vocal's s",
            "Mix: the bass's lows duck under each kick",
            "Mix: FX under the vocal",
            "Mix: the clap under the vocal",
            "Mix: drums low-cut in breakdowns",
            "Mix: a high-pass rising through each build",
            "Mix: pads dip the old chord's root at changes",
            "Mix: drum bus parallel compression",
            "Mix: upward compression in breakdowns",
            "Mix: bus glue per section",
            "Mix: expander on noisy parts",
            "Mix: the vocal's peaks caught",
            "Mix: pad sustain held back in drops",
            "Mix: a pan layout for centred percussion",
            "Mix: mono-safe widening of mono pads",
            "Mix: width split by band",
            "Mix: correlation guard",
            "Mix: bus saturation in drops",
            "Mix: exciter for air (hats, pads)",
            "Mix: a click on a dull kick",
            "Mix: drive on the bass's mids only",
            "Mix: kick and bass punch at each drop",
            "Mix: drums high-cut in breakdowns",
            "Mix: the drop settles after 8 bars",
            "Mix: the final drop lifted",
            "Mix: darker intro and outro",
            "Mix: dip under fills",
            "Mix: vocal doubler in drops",
            "Mix: air on the vocal",
            "Mix: the vocal's presence ridden",
            "Mix: hat rides into phrase ends",
            "Mix: clap tails in drops",
            "Mix: FX high-passed",
            "Mix: the riser's level from the energy",
            "Mix: FX tails darkening",
            "Mix: inter-sample peaks trimmed per track",
            "Mix: DC removed",
            "Mix: pump depth per section (Kybalion)",
            "Mix: fit to the kick automatically (Kybalion)",
            "Mix: sends per section (Antioch)",
            "Mix: pre-delay per role (Antioch)",
            "Mix: reverb decay per section (Antioch)",
            "Mix: delay division per role (Antioch)",
            "Mix: echo damping per section (Antioch)",
            "Mix: the space tightens before the drop (Antioch)",
            "Mix: pads get their own room (Antioch)",
            "Mix: clap reverb from the tempo (Antioch)",
            "Mix: presence eased before the drive (Vibration)",
            "Mix: drive per section (Vibration)",
            "Mix: unmasking depth per section (Solve et Coagula)",
            "Mix: a tone budget across tracks (Ouroboros)",
            "Mix: fixes that fight are arbitrated (Ouroboros)",
            "Mix: a step budget per play-through (Ouroboros)",
            "Mix: roll back an area since the last milestone (Ouroboros)",
            "Mix: who changed each track (Ouroboros)",
            "Mix: low-end polarity against the kick (Ouroboros)",
            "Mix: kick and hat layers lined up (Ouroboros)",
            "Mix: kick level per section (Ouroboros)",
            "Mix: low-end headroom (Ouroboros)",
            "Mix: lopsided kick and bass peaks evened (phase rotation)",
            "Mix: drop 2 a touch brighter",
            "Mix: the outro thins out at its end",
            "Mix: no drop-out into the breakdown",
            "Mix: backing vocals under the lead vocal",
            "Mix: the vocal's proximity boom cut",
            "Mix: stacked pads' boxiness cut",
            "Mix: cymbal wash tucked after each hit",
            "Mix: tonal percussion nudged into the key",
            "Mix: left/right balance per band",
            "Mix: each drop's own tone",
            "Mix: the low-mid boom in drops",
            "Mix: the sub held steady note to note",
            "Mix: the drum bus opens up for fills",
            "Mix: the lead's sustain in busy drops",
            "Mix: pads swell in breakdowns",
            "Mix: the kick's tail follows the tempo",
            "Mix: ghost notes evened",
            "Mix: left/right delays realigned (mono combing)",
            "Mix: nothing panned past 70%",
            "Mix: drop 2 a touch wider",
            "Mix: the riser widens into the drop",
            "Mix: the mono cutoff per section",
            "Mix: the fill's last beat into the drop lifted",
            "Mix: the bass cut for the last 16th before the drop",
            "Mix: new parts eased in",
            "Mix: pad drive rising through builds",
            "Mix: FX tails tucked after the drop's impact",
            "Mix: repeated sections not identical in tone",
            "Mix: the vocal or the lead leads, not both",
            "Mix: the vocal doubled on held notes",
            "Mix: more vocal air in breakdowns",
            "Mix: the bass's sub kept off the kick's body",
            "Mix: more bass harmonics in breakdowns",
            "Mix: downlifter tails gated at the next section",
            "Mix: the impact's level from the drop's energy",
            "Mix: noise sweeps peak at the drop",
            "Mix: FX dip under the hook",
            "Safety: a level budget per track (+-6 dB)",
            "Safety: a budget for the lows taken out in builds",
            "Safety: lifts held back when the headroom's short",
            "Safety: levels ramp across section changes",
            "Safety: a fix that misbehaves is switched off",
            "Mix: the reverb's tone follows its track (Antioch)",
            "Mix: the echo ducks under its own part (Antioch)",
            "Mix: the reverb steps back for the vocal (Antioch)",
            "Mix: reverb width by role (Antioch)",
            "Mix: reverb size per section (Antioch)",
            "Mix: pre-delay from note density (Antioch)",
            "Mix: echo feedback per section (Antioch)",
            "Mix: the echo's pan pattern by role (Antioch)",
            "Mix: the reverb's low cut per section (Antioch)",
            "Mix: the space opens in the breakdown's last bar (Antioch)",
            "Mix: no plosives in the reverb (Antioch)",
            "Mix: kick and bass phase per section (Kybalion)",
            "Mix: drive fizz over 12 kHz rolled off (Vibration)",
            "Mix: clap and snare layers lined up (Ouroboros)",
            "Autopilot: the plugins' fixes checked, and switched off if no better",
            "Autopilot: learn from the plugins' fixes too",
            "Autopilot: wait for the plugins to settle",
            "Autopilot: the least useful fixes sleep when the CPU is high",
            "Autopilot: tone first, then level, then stereo",
            "Autopilot: a fix kept only when two play-throughs agree",
            "Autopilot: the label's most important area first",
            "Autopilot: a fix whose gain fades is undone",
            "Autopilot: loops, solos and partial plays aren't judged",
            "Autopilot: a fix must beat each check's own wobble",
            "Autopilot: a fix judged where it acts",
            "Autopilot: a check's plugin fixes one at a time",
            "Autopilot: a knob you move is yours",
            "Autopilot: nothing changes while FL renders",
            "Autopilot: stop and report when nothing moves",
            "Autopilot: milestones carry every plugin's fixes" };
        static_assert (sizeof (n) / sizeof (n[0]) == numFeatures, "a name for every fix");
        static_assert (numFeatures <= 384, "twelve words of switches");
        return f >= 0 && f < numFeatures ? n[f] : "";
    }
    inline uint32_t baseGroup (int f)
    {
        if (f == transmuteF) return 4u;
        if (f >= mx2PhaseRotateF)
            switch (f)
            {
                case mx2CymbalWashF: case mx2LeadFizzF: return 1u;
                case mx2LevelBudgetF: case mx2BuildBudgetF: case mx2HeadroomForecastF: case mx2BoundarySmoothF: case mx2FailSafeF: case mx2SelfCheckF: case mx2LearnPluginF: case mx2SettleF: case mx2CpuBudgetF: case mx2FixOrderF: case mx2ConfirmF: case mx2AreaPriorityF: case mx2FadeUndoF: case mx2ValidPlaysF: case mx2NoiseFloorF: case mx2JudgeWhereF: case mx2OneAtATimeF: case mx2OwnershipF: case mx2RenderFreezeF: case mx2PlateauF: case mx2MilestoneFixesF: return 2u;
                default: return 8u;
            }
        if (f >= mxPhraseTrimF)
            switch (f)
            {
                case mxPresenceEaseF: return 1u;
                case mxToneBudgetF: case mxArbitrationF: case mxStepBudgetF: case mxRollbackAreaF: case mxProvenanceF: return 2u;
                default: return 8u;
            }
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

    namespace detail
    {
        /** Case-insensitive "contains" on plain C strings (no allocation: the audio thread asks). */
        inline bool has (const char* text, const char* word) noexcept
        {
            for (const char* t = text; *t != 0; ++t)
            {
                const char* a = t; const char* b = word;
                while (*a != 0 && *b != 0 && (char) std::tolower ((unsigned char) *a) == *b) { ++a; ++b; }
                if (*b == 0) return true;
            }
            return false;
        }
        inline uint32_t classify (int f) noexcept
        {
            const auto base = baseGroup (f);
            if ((f >= mx2SelfCheckF && f <= mx2MilestoneFixesF) || (f >= mxToneBudgetF && f <= mxProvenanceF)
                || f == learnKeptF || f == learnUndoneF || f == tasteF || f == regressionGuardF || f == preRenderF || f == idleSleepF)
                return behaviour;
            if (base != mixing) return base;
            const char* n = featureName (f);
            for (auto* w : { "vocal", "vox", "sibil", "de-ess", "plosive", "breath", "pop" }) if (has (n, w)) return vocals;
            for (auto* w : { "reverb", "verb", "delay", "echo", "send", "room", "pre-delay", "decay", "throw" }) if (has (n, w)) return space;
            for (auto* w : { "clap", "hat", "drum", "transient", "cymbal", "perc", "snare", "hits" }) if (has (n, w)) return drums;
            for (auto* w : { "kick", "bass", "sub", "low end", "lows", "low-end" }) if (has (n, w)) return lowEnd;
            for (auto* w : { "width", "wide", "stereo", "pan", "mono", "haas", "side", "correlation", "spread" }) if (has (n, w)) return width;
            for (auto* w : { "riser", "impact", "sweep", "downlifter", "fill", "transition", "fx " }) if (has (n, w)) return transitions;
            for (auto* w : { "level", "loud", "glue", "punch", "peak", "headroom", "dynamic", "expander", "squash", "budget", "compress" }) if (has (n, w)) return dynamics;
            return mixing;
        }
    }
    /** The group a fix belongs to (one of the twelve). */
    inline uint32_t featureGroup (int f) noexcept
    {
        static const auto table = []
        {
            std::array<uint32_t, numFeatures> t {};
            for (int i = 0; i < numFeatures; ++i) t[(size_t) i] = detail::classify (i);
            return t;
        }();
        return f >= 0 && f < numFeatures ? table[(size_t) f] : (uint32_t) mixing;
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
            std::atomic<uint32_t> featuresOff[12];           // single fixes switched off (bit per Feature)
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
            // the mix fixes, suite-wide: their strength (0..1, 1 = as designed; stored as 1 - strength so a fresh block is 1),
            // a loudness-matched A/B with every fix bypassed, how many to put to sleep for CPU, and a reset (a new number
            // each time every fix should start again)
            std::atomic<float> mixWeakness;
            std::atomic<uint32_t> mixBypassed, cpuSleep, mixResetGen;
        };
        inline Shared& shared() noexcept
        {
            static Shared* s = []
            {
                auto* m = static_cast<Shared*> (openSharedBlock ("antioch_suite_autopilot_v8", sizeof (Shared)));
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
    /** True if any of these groups is on. */
    inline bool anyOn (uint32_t groups) noexcept { return (detail::shared().disabled.load() & groups) != groups; }
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
    /** Every switch's word (from 64 on: words 2..11), as hex "a,b,c" for the project. */
    inline juce::String disabledFeaturesRest()
    {
        auto& s = detail::shared();
        juce::StringArray w;
        for (int i = 2; i < 12; ++i) w.add (juce::String::toHexString ((int) s.featuresOff[i].load()));
        return w.joinIntoString (",");
    }
    inline void setDisabledFeaturesRest (const juce::String& hex)
    {
        auto& s = detail::shared();
        const auto w = juce::StringArray::fromTokens (hex, ",", "");
        for (int i = 0; i < 10; ++i) s.featuresOff[2 + i].store (i < w.size() ? (uint32_t) w[i].getHexValue64() : 0u);
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
    inline void postLive (uint32_t id, int role, const float* bandsDb, int rank = -1) noexcept
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
        // the role in the low byte, its clash rank (0..100) above it: readers of the role alone see it unchanged
        s.liveRole[slot].store ((role & 0xff) | (juce::jlimit (0, 255, rank >= 0 ? rank : priorityOf (role) * 18) << 8));
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
            fn ((int) (s.liveRole[i].load() & 0xff), (const float*) bands);
        }
    }
    /** The same, with each track's clash rank (higher keeps the space; RoleCatalog's rank). */
    template <typename Fn> inline void forEachLiveRanked (uint32_t self, Fn&& fn) noexcept
    {
        auto& s = detail::shared();
        const auto now = detail::nowMs();
        for (int i = 0; i < maxLive; ++i)
        {
            const auto id = s.liveId[i].load();
            if (id == 0 || id == self || now - s.liveMs[i].load() > 300u) continue;
            float bands[liveBands];
            for (int b = 0; b < liveBands; ++b) bands[b] = s.liveBand[i][b].load();
            const auto packed = (int) s.liveRole[i].load();
            fn (packed & 0xff, (packed >> 8) & 0xff, (const float*) bands);
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
            fn ((int) (s.liveRole[i].load() & 0xff), (const double*) on);
        }
    }
    /** Vibration: the lead's note now (Hz). */
    inline void publishLeadPitch (float hz) noexcept { detail::shared().leadHz.store (hz); detail::shared().leadMs.store (detail::nowMs()); }
    inline float leadPitch() noexcept
    {
        auto& s = detail::shared();
        return detail::nowMs() - s.leadMs.load() > 300u ? 0.0f : s.leadHz.load();
    }

    /** The mix fixes' strength, 0..1 (Ouroboros' knob; 1 = as designed). */
    inline float mixStrength() noexcept { return 1.0f - juce::jlimit (0.0f, 1.0f, detail::shared().mixWeakness.load()); }
    inline void setMixStrength (float v) noexcept { detail::shared().mixWeakness.store (1.0f - juce::jlimit (0.0f, 1.0f, v)); }
    /** The whole-mix A/B: every mix fix bypassed (each track loudness-matched). */
    inline bool mixBypassed() noexcept { return detail::shared().mixBypassed.load() != 0; }
    inline void setMixBypassed (bool b) noexcept { detail::shared().mixBypassed.store (b ? 1u : 0u); }
    /** The CPU budget: 0 = every fix runs; each level puts the next eight least useful to sleep. */
    inline int cpuSleepLevel() noexcept { return (int) juce::jmin (6u, detail::shared().cpuSleep.load()); }
    inline void setCpuSleepLevel (int n) noexcept { detail::shared().cpuSleep.store ((uint32_t) juce::jlimit (0, 6, n)); }

    inline void reset() noexcept   // tests
    {
        auto& s = detail::shared();
        s.disabled.store (0); for (auto& w : s.featuresOff) w.store (0); s.harshAmount.store (0.0f); s.harshMs.store (0);
        for (auto& h : s.harshIds) h.store (0);
        for (auto& c : s.counters) c.store (0);
        for (int i = 0; i < maxResonances; ++i) { s.resonanceId[i].store (0); s.resonanceHz[i].store (0.0f); s.resonanceMs[i].store (0); }
        for (int i = 0; i < maxLive; ++i) { s.liveId[i].store (0); s.liveMs[i].store (0); s.liveOnsetNext[i].store (0); for (auto& o : s.liveOnset[i]) o.store (0.0); }
        s.leadHz.store (0.0f); s.leadMs.store (0);
        s.mixWeakness.store (0.0f); s.mixBypassed.store (0); s.cpuSleep.store (0);
    }
}
