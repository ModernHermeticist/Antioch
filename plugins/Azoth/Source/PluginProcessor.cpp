#include "PluginProcessor.h"
#include "ActivityBus.h"
#include "PluginEditor.h"
#include "KeyBus.h"
#include "KickBus.h"
#include "Music.h"
#include "ChordBus.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        using Attr = AudioParameterFloatAttributes;
        auto percent = Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
        AudioProcessorValueTreeState::ParameterLayout l;
        auto id = [] (const String& s) { return ParameterID { s, 1 }; };

        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::followId), "Follow Ouroboros", true));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::levelId), "FX Level", NormalisableRange<float> (-24.0f, 6.0f), -6.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::toneId), "Tone", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::impactId), "Impact", NormalisableRange<float> (0.0f, 1.0f), 0.8f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::crashId), "Crash", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::swellId), "Swell", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::downId), "Downlifter", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::rollId), "Snare Roll", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::swellLenId), "Swell Length", StringArray { "1 beat", "2 beats", "1 bar", "2 bars" }, 1));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::riserId), "Tonal Riser", NormalisableRange<float> (0.0f, 1.0f), 0.4f, percent));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::riserRangeId), "Riser Range", StringArray { "1 octave", "2 octaves" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::trackRiserId), "Track Riser", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::rollStartId), "Roll and Riser Start", StringArray { "Whole build", "Second half", "Last 4 bars" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::rollPitchId), "Roll Pitch Rise", NormalisableRange<float> (0.0f, 12.0f, 1.0f), 7.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return "+" + String (roundToInt (v)) + " st"; })));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::gapId), "Gap Before Drop", StringArray { "None", "1/2 beat", "1 beat" }, 0));
        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::grooveCrashId), "Crash Into Groove", true));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::laterDropsId), "Bigger Later Drops", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::reverseOnId), "Reverse Swells (3 s lookahead)", false));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::reverseLevelId), "Reverse Swell Level", NormalisableRange<float> (0.0f, 1.0f), 0.7f, percent));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::reverseTargetId), "Reverse Swell Into", StringArray { "Drops", "Every section" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::reverseLengthId), "Reverse Swell Length", StringArray { "1/2 bar", "1 bar", "2 bars" }, 1));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::dropFxId), "Drop Entrance FX", StringArray { "Off", "Tape stop", "Stutter" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::dropFxLenId), "Drop Entrance FX Length", StringArray { "1/2 beat", "1 beat", "2 beats" }, 1));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::buildShapeId), "Build Shape", StringArray { "Smooth", "Stepped", "Late surge" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::buildFilterId), "Build Filter", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::preLiftId), "Key Change Pre-lift", true));
        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::tuneSamplesId), "Tune My Samples to the Key", true));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::tagWhereId), "Producer Tag", StringArray { "Off", "First drop", "Every drop" }, 1));
        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::riserChordsId), "Riser Follows the Chords", true));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::rollFromId), "Roll From", StringArray { "1/4", "1/8" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::rollToId), "Roll To", StringArray { "1/16", "1/32", "1/64" }, 1));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::rollCurveId), "Roll Speed-up", StringArray { "Steps", "Smooth", "Late" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::rollVelId), "Roll Start Velocity", NormalisableRange<float> (0.1f, 1.0f), 0.35f, percent));
        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::rollPitchSpeedId), "Roll Pitch Follows Speed", true));
        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::impactTuneId), "Impact Tuned to the Kick", true));
        return l;
    }

    const juce::Identifier sampleProperty (int slot) { return juce::Identifier ("sample" + juce::String (slot)); }
}

AzothProcessor::AzothProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AzothState", layout())
{
    startTimerHz (4);

    // Mentalism's fix-it buttons: more swell and roll into a flat build; bigger later drops.
    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix) -> std::vector<suite::FixReceiver::Change>
    {
        using R = suite::FixReceiver;
        if (fix == suite::Fix::BuildRise) return { R::nudge (apvts, swellId, 0.1f), R::nudge (apvts, rollId, 0.1f) };
        if (fix == suite::Fix::LaterDropBigger) return { R::nudge (apvts, laterDropsId, 0.25f) };
        return {};
    });
    // Session health (Ouroboros): missing samples go back to the synthesised sounds (undo puts the paths back).
    fixes->setCustom ([this] (suite::Fix fix, uint32_t) -> std::function<void()>
    {
        if (fix != suite::Fix::SessionHealth || missingSamples() == 0) return {};
        std::vector<std::pair<int, juce::String>> forgot;
        for (int slot = 0; slot < numSampleSlots; ++slot)
        {
            const auto path = apvts.state[sampleProperty (slot)].toString();
            if (path.isNotEmpty() && ! juce::File (path).existsAsFile()) { forgot.push_back ({ slot, path }); clearSample (slot); }
        }
        return [this, forgot] { for (const auto& [slot, path] : forgot) apvts.state.setProperty (sampleProperty (slot), path, nullptr); };
    });

    // Your transitions: a new Azoth starts from the style you usually keep.
    const auto usual = usualTransitions (suite::StyleLog::read());
    for (const auto& [id, v] : usual)
        if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    usedYourTransitions = ! usual.empty();
}

std::map<juce::String, float> AzothProcessor::usualTransitions (const std::vector<suite::StyleLog::Entry>& entries)
{
    std::map<juce::String, std::map<int, int>> counts;
    int songs = 0;
    for (const auto& e : entries)
    {
        if (e.kind != "transitions") continue;
        ++songs;
        for (const auto& id : transitionParamIds())
            if (e.get (id).isNotEmpty()) ++counts[id][e.get (id).getIntValue()];
    }
    std::map<juce::String, float> out;
    if (songs < 2) return out;
    for (const auto& [id, values] : counts)
    {
        int best = 0, bestCount = 0;
        for (const auto& [v, c] : values) if (c > bestCount) { best = v; bestCount = c; }
        out[id] = (float) best;
    }
    return out;
}

void AzothProcessor::logTransitionsOnce()
{
    if (! reachedFinalDrop.load() || transitionsLogged.exchange (true)) return;
    std::map<juce::String, juce::String> fields;
    for (const auto& id : transitionParamIds())
        fields[id] = juce::String (juce::roundToInt (apvts.getRawParameterValue (id)->load()));
    suite::StyleLog::add ("transitions", fields);
}

void AzothProcessor::updateLatency()
{
    const auto on = apvts.getRawParameterValue (reverseOnId)->load() > 0.5f;
    const auto wanted = on ? (int) std::round (lookaheadSeconds * sampleRate) : 0;
    if (wanted != activeLatency.load())
    {
        activeLatency.store (wanted);
        setLatencySamples (wanted);   // the host re-aligns this track
    }
}

bool AzothProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && (layouts.getMainInputChannelSet() == out || layouts.getMainInputChannelSet().isDisabled());
}

void AzothProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    const auto lookahead = (int) std::round (lookaheadSeconds * sampleRate);
    swell.prepare (sampleRate, lookahead);
    for (auto& d : dryDelay) d.assign ((size_t) lookahead, 0.0f);
    dryDelayPos = 0;
    inputIndex = 0;
    previousInputPos = -1.0e9;
    activeLatency.store (-1);   // force a refresh
    updateLatency();
    const auto sr = (float) sampleRate;
    impact.prepare (sr);
    crash.prepare (sr);
    downlifter.prepare (sr);
    for (auto& s : snares) s.prepare (sr);
    for (auto& s : samplePlayers) s.prepare (sr);
    for (auto& h : history) h.assign ((size_t) (4.0 * sampleRate) + 1, 0.0f);
    historyPos = 0;
    dropFxActive = false;
    for (auto& f : buildFilter) f.reset();
    buildFilterHz = 0.0f;
    buildFilterCounter = 0;
    riser.prepare (sr);
    for (auto& t : trackShift) t.prepare (sampleRate, 0.06f);
    trackRiseMix = 0.0f;
    liftMix = 0.0f;
    swellFilter.reset();
    previousIndex = -1;
    rollPos = -1.0e9;
}

void AzothProcessor::enterSection (suite::SectionType type, suite::SectionType previous, double bpm, double barQ, float rootHz, bool laterDrop, float impactScale, float downScale)
{
    using T = suite::SectionType;
    if (type == T::Drop)
    {
        // Second and later drops hit harder (up to +50% at full "Bigger later drops").
        if (impactScale <= 0.0f) return;   // the director gave this drop to someone else
        const auto boost = (laterDrop ? 1.0f + 0.5f * value (laterDropsId) : 1.0f) * impactScale;
        if (samplePlayers[slotImpact].loaded()) samplePlayers[slotImpact].trigger (value (impactId) * boost, tuning (slotImpact, rootHz));
        else
        {
            const auto hz = impactPitch (rootHz, value (impactTuneId) > 0.5f ? suite::KickBus::read().bodyHz : 0.0f, true);
            lastImpactHz.store (hz);
            impact.trigger (hz, value (impactId) * boost);
        }
        const auto where = (int) value (tagWhereId);
        if (samplePlayers[slotTag].loaded() && (where == tagEveryDrop || (where == tagFirstDrop && ! laterDrop)))
        {
            samplePlayers[slotTag].trigger (0.8f, tuning (slotTag, rootHz));   // your tag, level-matched (and tuned if it's pitched)
            tagsPlayed.fetch_add (1);
        }
        if (samplePlayers[slotCrash].loaded()) samplePlayers[slotCrash].trigger (value (crashId) * boost, tuning (slotCrash, rootHz));
        else crash.trigger (value (crashId) * boost, value (toneId));
        lastImpactLevel.store (value (impactId) * boost);
    }
    else if (type == T::Breakdown)
    {
        if (downScale <= 0.0f) return;
        const auto twoBars = (float) (2.0 * barQ * 60.0 / bpm);
        if (samplePlayers[slotDown].loaded()) samplePlayers[slotDown].trigger (value (downId) * downScale, tuning (slotDown, rootHz));
        else downlifter.trigger (twoBars, rootHz * 4.0f, value (downId) * downScale);
    }
    else if (type == T::Groove && previous == T::Intro && value (grooveCrashId) > 0.5f)
    {
        if (samplePlayers[slotCrash].loaded()) samplePlayers[slotCrash].trigger (0.6f * value (crashId));
        else crash.trigger (0.6f * value (crashId), value (toneId));
    }
    else
    {
        return;   // nothing plays when entering this section
    }
    eventsFired.fetch_add (1);
}

void AzothProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    using T = suite::SectionType;

    const auto host = suite::HostInfo::read (getPlayHead());
    const auto latency = juce::jmax (0, activeLatency.load());
    const auto haveMap = sections.refresh();
    mapAvailable.store (haveMap);
    const auto follow = value (followId) > 0.5f && haveMap && host.playing && host.hasPpq;
    const auto barQ = host.barQuarters();
    const auto qPerSample = host.quartersPerSample (sampleRate);
    // With the lookahead on, what we output is `latency` samples behind what we receive,
    // so the synth FX are generated for that earlier position.
    const auto latencyQ = latency * qPerSample;
    const auto reverseOn = latency > 0;
    const auto reverseLevel = value (reverseLevelId);
    const auto reverseEvery = (int) value (reverseTargetId) == 1;
    const auto captureSamples = (int) std::round (1.0 / qPerSample);   // one beat of the section
    const auto swellSamples = (int) std::round (reverseLengthQuarters ((int) value (reverseLengthId)) / qPerSample);

    // Sub elements land on the song's root (octave 1), from Correspondence/Emerald Tablet; A if unknown.
    const auto key = suite::KeyBus::read();
    const auto rootHz = suite::music::midiToHz ((float) (24 + (key.valid ? key.root : 9)));

    const auto sr = (float) sampleRate;
    const auto level = juce::Decibels::decibelsToGain (value (levelId));
    const auto swellLevel = value (swellId), rollLevel = value (rollId);
    const auto swellQ = swellQuarters ((int) value (swellLenId));
    const auto gapQ = gapQuarters ((int) value (gapId));
    const auto rollStart = (int) value (rollStartId);
    const auto rollPitch = value (rollPitchId);
    const auto riserLevel = value (riserId), trackRiser = value (trackRiserId);
    const auto riserRange = (int) value (riserRangeId) == 1 ? 24.0f : 12.0f;
    const auto riserRootHz = rootHz * 4.0f;   // octave 3
    const auto dropFx = (int) value (dropFxId);
    auto dropFxNow = dropFx;
    // The suite director: which of Azoth's gestures each moment gets (when its plan is live)
    namespace dr = suite::director;
    const auto directorLive = suiteDirector.refresh() && follow;
    float dirRiser = 1.0f, dirRoll = 1.0f, dirSwell = 1.0f;
    double dirGapQ = gapQ;
    const auto dropFxQ = dropFxQuarters ((int) value (dropFxLenId));
    const auto buildShape = (int) value (buildShapeId);
    const auto buildFilterAmount = value (buildFilterId);
    const auto preLift = value (preLiftId) > 0.5f;
    const auto rollFrom = (int) value (rollFromId), rollTo = (int) value (rollToId), rollCurve = (int) value (rollCurveId);
    const auto rollVel = value (rollVelId);
    const auto rollPitchSpeed = value (rollPitchSpeedId) > 0.5f;
    suite::Progression progression;
    const auto followChords = value (riserChordsId) > 0.5f && suite::ChordBus::read (progression);
    const auto beatSamples = 1.0 / juce::jmax (1.0e-9, qPerSample);

    // Auditions from the UI play immediately.
    if (const auto a = pendingAudition.exchange (0))
    {
        if (a & aImpact) { if (samplePlayers[slotImpact].loaded()) samplePlayers[slotImpact].trigger (juce::jmax (0.5f, value (impactId)));
                           else impact.trigger (rootHz, juce::jmax (0.5f, value (impactId))); }
        if (a & aCrash)  { if (samplePlayers[slotCrash].loaded()) samplePlayers[slotCrash].trigger (juce::jmax (0.5f, value (crashId)));
                           else crash.trigger (juce::jmax (0.5f, value (crashId)), value (toneId)); }
        if ((a & aDown) && samplePlayers[slotDown].loaded()) samplePlayers[slotDown].trigger (juce::jmax (0.5f, value (downId)));
        else if (a & aDown) downlifter.trigger ((float) (2.0 * barQ * 60.0 / host.bpm), rootHz * 4.0f, juce::jmax (0.5f, value (downId)));
        if (a & aSnare)  snares[0].trigger (juce::jmax (0.5f, rollLevel), 1.0f);
    }

    // Status for the UI: what's next and when.
    if (follow)
    {
        const auto now = sections.locate (host.ppq - latencyQ, barQ);
        nextType.store (now.valid && now.hasNext ? (int) now.next : -1);
        barsToNext.store (now.valid ? (float) ((now.endQ - (host.ppq - latencyQ)) / barQ) : 0.0f);
    }
    else
    {
        nextType.store (-1);
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    float riserActivity = 0.0f;   // how far into a riser or swell (Quintessence on the pads and leads makes room for it)
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        float fx = 0.0f;
        float rise = -1.0f;   // progress through this build's riser, or -1
        float riseKey = 1.0f; // final-drop key change: the riser aims for the key of the drop it leads into
        float dropP = -1.0f;  // progress through the drop entrance FX, or -1
        float liftSemis = -1.0f;   // key-change pre-lift: semitones this track is pitched up now, or -1
        float chordRatio = 1.0f;   // the riser climbs through the chord roots (from Correspondence)
        bool dropSilent = false;

        if (follow)
        {
            const auto pos = host.ppq + qPerSample * n - latencyQ;
            const auto sec = sections.locate (pos, barQ);
            const auto jumped = previousIndex >= 0 && (pos < previousPos - 1.0e-6 || pos > previousPos + 0.5);

            // Entering a new section while playing forward fires its transition.
            if (sec.valid && previousIndex >= 0 && ! jumped && sec.index != previousIndex)
            {
                energyGain = juce::jlimit (0.0f, 3.0f, sec.energyRatio * sections.getMap().intensityScale());   // energy curve and Intensity
                float impactScale = 1.0f, downScale = 1.0f;
                if (directorLive)
                {
                    const auto at = suiteDirector.momentNear (sec.startQ, 0.05, 0.05, dr::kIntoDrop | dr::kIntoBreak | dr::kIntoOther | dr::kPin);
                    const auto g = at >= 0 ? suiteDirector[at].gesture[dr::pAzoth] : (uint8_t) dr::gNone;
                    const auto part = at >= 0 && suiteDirector[at].hero == dr::pAzoth ? 1.0f : 0.6f;
                    impactScale = g == dr::gAzImpact ? part : 0.0f;
                    downScale = g == dr::gAzDown ? part : 0.0f;
                    if (g == dr::gAzImpact || g == dr::gAzDown) suite::DirectorBus::reportActed (suiteDirector.hash(), at, dr::pAzoth);
                }
                enterSection (sec.type, previousType, host.bpm, barQ, rootHz * std::exp2 ((float) sec.keyShift / 12.0f), sec.isLaterDrop(), impactScale, downScale);
                if (sec.type == T::Drop && sec.index == sections.getMap().lastDropIndex()) reachedFinalDrop.store (true);
            }
            // An impact moment (Ouroboros): the impact and crash on its bar, as on a drop.
            if (! jumped && previousIndex >= 0 && sections.getMap().momentCount > 0)
            {
                const auto bar = (int) std::floor (pos / barQ + 1.0e-9) + 1, prevBar = (int) std::floor (previousPos / barQ + 1.0e-9) + 1;
                if (bar != prevBar && sections.getMap().hasMoment (bar, suite::momentImpact))
                {
                    if (samplePlayers[slotImpact].loaded()) samplePlayers[slotImpact].trigger (value (impactId), tuning (slotImpact, rootHz));
                    else impact.trigger (impactPitch (rootHz, value (impactTuneId) > 0.5f ? suite::KickBus::read().bodyHz : 0.0f, true), juce::jmax (0.5f, value (impactId)));
                    if (samplePlayers[slotCrash].loaded()) samplePlayers[slotCrash].trigger (value (crashId), tuning (slotCrash, rootHz));
                    else crash.trigger (value (crashId), value (toneId));
                    momentImpacts.fetch_add (1);
                }
            }
            if (sec.valid) riseGain = juce::jlimit (0.0f, 3.0f, (sec.type == T::Build ? sec.nextEnergyRatio : 1.0f) * sections.getMap().intensityScale());
            if (sec.valid) riseKey = std::exp2 ((float) sec.nextKeyShift / 12.0f);
            previousIndex = sec.valid ? sec.index : -1;
            if (sec.valid) previousType = sec.type;
            previousPos = pos;
            const auto rollPrevPos = rollPos;
            rollPos = pos;

            if (sec.valid && directorLive && sec.index != directorSection)
            {
                // what the director wants from Azoth at the end of this section
                directorSection = sec.index;
                const auto at = suiteDirector.momentNear (sec.endQ, 0.05, 0.05, dr::kIntoDrop | dr::kIntoBuild | dr::kIntoBreak | dr::kIntoOther | dr::kBreakPeak);
                const auto g = at >= 0 ? suiteDirector[at].gesture[dr::pAzoth] : (uint8_t) dr::gNone;
                const auto part = at >= 0 && suiteDirector[at].hero == dr::pAzoth ? 1.0f : 0.6f;
                dirRiserNow = g == dr::gAzRiser ? part : 0.0f;
                dirRollNow = g == dr::gAzRoll ? part : 0.0f;
                dirSwellNow = g == dr::gAzSwell ? part : 0.0f;
                dirTapeNow = g == dr::gAzTapeStop;
                dirGapNow = at >= 0 && suiteDirector[at].silence != dr::gNone && suiteDirector[at].silence != dr::gSiFakeDrop
                              ? (double) (suiteDirector[at].atQ - suiteDirector[at].silenceStartQ) : 0.0;
                if (g != dr::gNone && g != dr::gAzImpact && g != dr::gAzDown) suite::DirectorBus::reportActed (suiteDirector.hash(), at, dr::pAzoth);
            }
            if (directorLive)
            {
                dirRiser = dirRiserNow; dirRoll = dirRollNow; dirSwell = dirSwellNow; dirGapQ = dirGapNow;
                dropFxNow = dirTapeNow ? (int) dfTapeStop : (int) dfOff;
            }
            if (sec.valid)
            {
                const auto end = sec.endQ - (sec.hasNext && sec.next == T::Drop ? dirGapQ : 0.0);

                // Risers across the Build (same start as the roll), stopping for the gap.
                if (sec.type == T::Build && followChords)
                    chordRatio = std::exp2 ((float) progression.chordRoots[(size_t) progression.chordAt (pos - sec.startQ)] / 12.0f);
                if (sec.type == T::Build)
                {
                    const auto startQ = rollStart == 0 ? sec.startQ
                                      : rollStart == 1 ? 0.5 * (sec.startQ + sec.endQ)
                                                       : juce::jmax (sec.startQ, sec.endQ - 4.0 * barQ);
                    if (pos >= startQ && pos < end)
                        rise = shapeProgress (buildShape, (float) ((pos - startQ) / juce::jmax (1.0e-6, end - startQ)));
                }

                // Key-change pre-lift: over the last beat before the drop that changes key, this
                // track glides up to the new key, announcing the lift.
                if (preLift && sec.hasNext && sec.next == T::Drop && sec.nextKeyShift != sec.keyShift && pos >= end - 1.0 && pos < end)
                    liftSemis = (float) (sec.nextKeyShift - sec.keyShift) * (float) ((pos - (end - 1.0)) / 1.0);

                // Drop entrance: tape stop or stutter on this track's audio just before the drop.
                if (dropFxNow != dfOff && sec.hasNext && sec.next == T::Drop)
                {
                    if (pos >= end - dropFxQ && pos < end)
                        dropP = (float) ((pos - (end - dropFxQ)) / dropFxQ);
                    else if (dropFxNow == dfTapeStop && pos >= end && pos < sec.endQ)
                        dropSilent = true;   // the tape has stopped: silence through the gap
                }

                // Snare roll through the Build, accelerating in steps and rising in pitch.
                if (sec.type == T::Build && rollLevel * dirRoll > 0.0f)
                {
                    const auto startQ = rollStart == 0 ? sec.startQ
                                      : rollStart == 1 ? 0.5 * (sec.startQ + sec.endQ)
                                                       : juce::jmax (sec.startQ, sec.endQ - 4.0 * barQ);
                    if (pos >= startQ && pos < end)
                    {
                        const auto p = (double) shapeProgress (buildShape == bsStepped ? bsSmooth : buildShape,
                                                               (float) ((pos - startQ) / juce::jmax (1.0e-6, end - startQ)));
                        const auto rate = rollRate (rollFrom, rollTo, rollCurve, p);
                        // A hit whenever playback crosses a grid line at the current rate (16ths and slower
                        // follow the suite's groove: the off-16ths land late by its swing).
                        const auto swing = rate >= 0.25 - 1.0e-9 ? (double) grooveSwing.load() : 0.0;
                        const auto w = unswing (pos, swing), wPrev = unswing (rollPrevPos, swing);
                        const auto crossed = rollPrevPos < pos && pos - rollPrevPos < 0.5
                                             && (int64_t) std::floor (w / rate) != (int64_t) std::floor (wPrev / rate);
                        if (crossed)
                        {
                            snares[(size_t) nextSnare].trigger (rollLevel * dirRoll * (rollVel + (1.0f - rollVel) * (float) p),
                                                                std::exp2 (rollPitch * (float) rollPitchAmount (rollFrom, rollTo, rollCurve, p, rollPitchSpeed) / 12.0f));
                            rollPitchNow.store (rollPitch * (float) rollPitchAmount (rollFrom, rollTo, rollCurve, p, rollPitchSpeed));
                            nextSnare = (nextSnare + 1) % (int) snares.size();
                            snareHits.fetch_add (1);
                        }
                    }
                }

                // Swell: rising, opening noise into every Drop.
                if (swellLevel * dirSwell > 0.0f && sec.hasNext && sec.next == T::Drop)
                {
                    // Into a later drop, the swell is longer (up to twice as long).
                    bool dropBefore = false;
                    for (int k = 0; k <= sec.index; ++k)
                        if (sections.getMap().sections[(size_t) k].type == T::Drop) dropBefore = true;
                    const auto thisSwellQ = dropBefore ? swellQ * (1.0 + value (laterDropsId)) : swellQ;
                    const auto s = (pos - (end - thisSwellQ)) / thisSwellQ;
                    if (s >= 0.0 && s < 1.0)
                    {
                        const auto x = (float) s;
                        swellFilter.set (400.0f * std::pow (30.0f, x), 0.7f, sr);
                        fx += swellFilter.lowpass (swellNoise.process()) * x * x * swellLevel * dirSwell * 0.6f;
                        riserActivity = juce::jmax (riserActivity, swellLevel > 0.0f ? x : 0.0f);
                    }
                }
            }
        }
        else
        {
            previousIndex = -1;
        }

        fx += (impact.process() + crash.process() + downlifter.process()) * energyGain;
        for (auto& sp : samplePlayers) fx += sp.process() * energyGain;
        fx += riser.process (rise, riserRootHz * riseKey * chordRatio, riserRange, riserLevel * dirRiser) * riseGain;
        if (rise >= 0.0f && riserLevel > 0.0f) riserActivity = juce::jmax (riserActivity, rise);
        for (auto& s : snares) fx += s.process() * riseGain;

        fx *= level;

        if (reverseOn)
        {
            // Remember the incoming audio, and start a swell one beat after a target section begins.
            const auto inL = left[n], inR = right != nullptr ? right[n] : left[n];
            swell.pushInput (inL, inR);
            if (follow)
            {
                const auto inputPos = host.ppq + qPerSample * n;
                if (inputPos < previousInputPos - 1.0e-6 || inputPos > previousInputPos + 0.5)
                    swell.cancel();   // jumped: whatever was scheduled no longer lines up
                const auto sec = sections.locate (inputPos, barQ);
                const auto captureEnd = sec.startQ + 1.0;
                if (sec.valid && sec.index > 0 && (reverseEvery || sec.type == T::Drop)
                    && previousInputPos < captureEnd && inputPos >= captureEnd && inputPos - previousInputPos < 0.5 && ! swell.busy())
                {
                    const auto boundaryIndex = inputIndex - (int64_t) std::llround ((inputPos - sec.startQ) / qPerSample);
                    swell.start (inputIndex, boundaryIndex, captureSamples, swellSamples, reverseLevel);
                }
                previousInputPos = inputPos;
            }
            else
            {
                swell.cancel();
                previousInputPos = -1.0e9;
            }

            // The output runs `latency` samples behind: delayed dry audio plus the swell for that moment.
            float sl, sr2;
            swell.output (inputIndex - latency, sl, sr2);
            ++inputIndex;
            for (size_t c = 0; c < 2; ++c)
            {
                auto& d = dryDelay[c];
                const auto delayed = d[(size_t) dryDelayPos];
                d[(size_t) dryDelayPos] = c == 0 ? inL : inR;
                if (c == 0) left[n] = delayed + sl;
                else if (right != nullptr) right[n] = delayed + sr2;
            }
            dryDelayPos = (dryDelayPos + 1) % (int) dryDelay[0].size();
            if (dryDelayPos >= latency) dryDelayPos = 0;   // the delay line is exactly `latency` long
        }

        // Track riser: this track's own audio, pitched up to an octave across the build and faded in.
        {
            const auto target = rise >= 0.0f ? trackRiser * rise * rise : 0.0f;
            trackRiseMix += 0.002f * (target - trackRiseMix);
            if (trackShiftCounter-- <= 0)
            {
                trackShiftCounter = 32;
                for (auto& t : trackShift) t.setSemitones (liftSemis >= 0.0f ? liftSemis : rise >= 0.0f ? 12.0f * rise : 0.0f);
            }
            const auto shiftedL = trackShift[0].process (left[n]);
            const auto shiftedR = trackShift[1].process (right != nullptr ? right[n] : left[n]);
            // The pre-lift replaces the track with its pitched copy (a quick crossfade either side).
            liftMix += (liftSemis >= 0.0f ? 0.01f : 0.004f) * ((liftSemis >= 0.0f ? 1.0f : 0.0f) - liftMix);
            if (liftMix > 1.0e-4f)
            {
                left[n] += liftMix * (shiftedL - left[n]);
                if (right != nullptr) right[n] += liftMix * (shiftedR - right[n]);
            }
            else if (trackRiseMix > 1.0e-5f)
            {
                left[n] += shiftedL * trackRiseMix;
                if (right != nullptr) right[n] += shiftedR * trackRiseMix;
            }
        }

        // Build filter: this track's audio thins out (high-pass up to 1 kHz) across the build.
        {
            if (buildFilterCounter-- <= 0)
            {
                buildFilterCounter = 32;
                buildFilterHz = rise >= 0.0f ? 20.0f * std::pow (50.0f, buildFilterAmount * rise) : 0.0f;
                if (buildFilterHz > 21.0f)
                    for (auto& f : buildFilter) f.set (buildFilterHz, 0.7f, sr);
                else
                    for (auto& f : buildFilter) f.reset();
            }
            if (buildFilterHz > 21.0f)
            {
                left[n] = buildFilter[0].highpass (left[n]);
                if (right != nullptr) right[n] = buildFilter[1].highpass (right[n]);
            }
        }

        // Drop entrance FX, replaying this track's recent audio.
        {
            const auto size = (int) history[0].size();
            history[0][(size_t) historyPos] = left[n];
            history[1][(size_t) historyPos] = right != nullptr ? right[n] : left[n];
            if (dropP >= 0.0f)
            {
                if (! dropFxActive)
                {
                    dropFxActive = true;
                    tapeRead = historyPos;
                    stutterStart = historyPos;
                    stutterCount = 0;
                    dropFxRuns.fetch_add (1);
                }
                float outL = 0.0f, outR = 0.0f;
                if (dropFxNow == dfTapeStop)
                {
                    // The tape slows to a halt: read speed falls from 1 to 0, pitch with it.
                    const auto i0 = (int) tapeRead, i1 = (i0 + 1) % size;
                    const auto f = (float) (tapeRead - i0);
                    const auto fade = juce::jmin (1.0f, (1.0f - dropP) * 20.0f);
                    outL = (history[0][(size_t) i0] + f * (history[0][(size_t) i1] - history[0][(size_t) i0])) * fade;
                    outR = (history[1][(size_t) i0] + f * (history[1][(size_t) i1] - history[1][(size_t) i0])) * fade;
                    tapeRead += 1.0 - dropP;
                    if (tapeRead >= size) tapeRead -= size;
                }
                else
                {
                    // Stutter: loop the moment before, in 1/8 notes, then 1/16, then 1/32.
                    const auto slice = juce::jmax (64, (int) (beatSamples * (dropP < 0.5f ? 0.5 : dropP < 0.75f ? 0.25 : 0.125)));
                    const auto k = (int) (stutterCount % slice);
                    const auto idx = ((stutterStart - slice + 1 + k) % size + size) % size;
                    const auto env = juce::jmin (1.0f, (float) k / 64.0f, (float) (slice - k) / 64.0f);
                    outL = history[0][(size_t) idx] * env;
                    outR = history[1][(size_t) idx] * env;
                    ++stutterCount;
                }
                left[n] = outL;
                if (right != nullptr) right[n] = outR;
            }
            else
            {
                if (dropSilent)
                {
                    left[n] = 0.0f;
                    if (right != nullptr) right[n] = 0.0f;
                }
                else
                {
                    dropFxActive = false;
                }
            }
            historyPos = (historyPos + 1) % size;
        }

        left[n] += fx;
        if (right != nullptr) right[n] += fx;
        lastRise.store (rise);
    }
    if (riserActivity > 0.0f) suite::ActivityBus::write (suite::ActivityBus::riser, riserActivity);


    if (reverseOn)
        swell.renderStep (buffer.getNumSamples());   // render the next part of any pending swell
}

juce::AudioProcessorEditor* AzothProcessor::createEditor()
{
    return new AzothEditor (*this);
}

void AzothProcessor::getPluginState (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AzothProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            for (int slot = 0; slot < numSampleSlots; ++slot)
            {
                const auto path = apvts.state[sampleProperty (slot)].toString();
                if (path.isNotEmpty()) loadSample (slot, juce::File (path));
                else clearSample (slot);
            }
        }
}

bool AzothProcessor::loadSample (int slot, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (slot, (int) numSampleSlots)) return false;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 2) return false;

    auto sample = std::make_shared<azoth::Sample>();
    const auto length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (20.0 * reader->sampleRate));
    juce::AudioBuffer<float> raw ((int) juce::jmax (1u, reader->numChannels), length);
    reader->read (&raw, 0, length, 0, true, true);
    sample->mono.setSize (1, length);
    sample->mono.clear();
    for (int c = 0; c < raw.getNumChannels(); ++c)
        sample->mono.addFrom (0, 0, raw, c, 0, length, 1.0f / (float) raw.getNumChannels());
    sample->rate = reader->sampleRate;
    sample->path = file.getFullPathName();
    sample->analyse();   // level match, and its pitch for tuning to the key

    sampleKeep.push_back (sample);
    samplePlayers[(size_t) slot].setSample (sample);
    samplePaths[(size_t) slot] = sample->path;
    apvts.state.setProperty (sampleProperty (slot), sample->path, nullptr);
    return true;
}

bool AzothProcessor::savePreset (const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    presetFolder().createDirectory();
    return presetFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".azoth").replaceWithText (apvts.copyState().toXmlString());
}

bool AzothProcessor::loadPreset (const juce::File& file)
{
    const auto tree = juce::ValueTree::fromXml (file.loadFileAsString());
    if (! tree.hasType (apvts.state.getType())) return false;
    apvts.replaceState (tree);
    for (int slot = 0; slot < numSampleSlots; ++slot)
    {
        const auto path = apvts.state[sampleProperty (slot)].toString();
        if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSample (slot, juce::File (path));
        else clearSample (slot);
    }
    return true;
}

juce::Array<juce::File> AzothProcessor::presetFiles()
{
    auto files = presetFolder().findChildFiles (juce::File::findFiles, false, "*.azoth");
    files.sort();
    return files;
}

double AzothProcessor::tuning (int slot, float rootHz) const
{
    if (value (tuneSamplesId) < 0.5f) return 1.0;
    return azoth::tuneRatio (samplePlayers[(size_t) slot].pitch(), rootHz);
}

float AzothProcessor::samplePitch (int slot) const
{
    return juce::isPositiveAndBelow (slot, (int) numSampleSlots) ? samplePlayers[(size_t) slot].pitch() : 0.0f;
}

int AzothProcessor::missingSamples() const
{
    int n = 0;
    for (int slot = 0; slot < numSampleSlots; ++slot)
    {
        const auto path = apvts.state[sampleProperty (slot)].toString();
        n += path.isNotEmpty() && ! juce::File (path).existsAsFile();
    }
    return n;
}

void AzothProcessor::clearSample (int slot)
{
    if (! juce::isPositiveAndBelow (slot, (int) numSampleSlots)) return;
    samplePlayers[(size_t) slot].setSample (nullptr);
    samplePaths[(size_t) slot].clear();
    apvts.state.removeProperty (sampleProperty (slot), nullptr);
}

juce::String AzothProcessor::sampleName (int slot) const
{
    return juce::isPositiveAndBelow (slot, (int) numSampleSlots) ? juce::File (samplePaths[(size_t) slot]).getFileNameWithoutExtension() : juce::String();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AzothProcessor();
}
