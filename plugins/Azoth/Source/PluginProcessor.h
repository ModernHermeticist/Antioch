#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "FixBus.h"
#include "Voices.h"
#include "SectionBus.h"
#include "HostInfo.h"
#include "ReverseSwell.h"

class AzothProcessor : public suite::SuiteProcessor, private juce::Timer
{
public:
    AzothProcessor();
    ~AzothProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getPluginState (juce::MemoryBlock& destData) override;
    SuiteStatus suiteStatus() override
    {
        SuiteStatus s;
        if (value (followId) > 0.5f) s.flags |= suite::Status::followsOuroboros;
        if (activeLatency.load() > 0) s.flags |= suite::Status::highLatency;   // reverse swells: 3 s lookahead
        return s;
    }
    void setPluginState (const void* data, int sizeInBytes) override;

    // Parameters
    static inline const juce::String followId { "follow" }, levelId { "level" }, toneId { "tone" },
        impactId { "impact" }, crashId { "crash" }, swellId { "swell" }, downId { "downlifter" }, rollId { "roll" },
        swellLenId { "swelllen" }, rollStartId { "rollstart" }, rollPitchId { "rollpitch" }, gapId { "gap" }, grooveCrashId { "groovecrash" },
        laterDropsId { "laterdrops" }, riserId { "riser" }, riserRangeId { "riserrange" }, trackRiserId { "trackriser" }, reverseOnId { "rev_on" }, reverseLevelId { "rev_level" }, reverseTargetId { "rev_target" }, reverseLengthId { "rev_length" },
        dropFxId { "dropfx" }, dropFxLenId { "dropfxlen" }, buildShapeId { "buildshape" }, buildFilterId { "buildfilter" },
        preLiftId { "prelift" }, tuneSamplesId { "tunesamples" }, tagWhereId { "tagwhere" },
        riserChordsId { "riserchords" }, rollFromId { "rollfrom" }, rollToId { "rollto" }, rollCurveId { "rollcurve" }, rollVelId { "rollvel" },
        rollPitchSpeedId { "rollpitchspeed" }, impactTuneId { "impacttune" };
    enum RollCurve { rcSteps, rcSmooth, rcLate };
    /** The snare roll's note length (quarters) at progress `p` through it. */
    static double rollRate (int from, int to, int curve, double p)
    {
        const auto startRate = from == 0 ? 1.0 : 0.5;
        const auto endRate = to == 0 ? 0.25 : to == 1 ? 0.125 : 0.0625;
        const auto pc = curve == rcLate ? p * p : p;
        if (curve == rcSmooth) return startRate * std::pow (endRate / startRate, pc);
        const auto stages = (int) std::lround (std::log2 (startRate / endRate));
        const auto k = juce::jmin (stages, (int) std::floor (pc * (stages + 1)));
        return startRate / std::exp2 ((double) k);
    }
    /** How far through the roll's pitch rise it is (0..1): with the build (steady), or in step with
        the roll's speed, so the pitch jumps each time the roll speeds up. */
    static double rollPitchAmount (int from, int to, int curve, double p, bool followSpeed)
    {
        if (! followSpeed) return p;
        const auto startRate = from == 0 ? 1.0 : 0.5;
        const auto endRate = to == 0 ? 0.25 : to == 1 ? 0.125 : 0.0625;
        return juce::jlimit (0.0, 1.0, std::log2 (startRate / rollRate (from, to, curve, p)) / std::log2 (startRate / endRate));
    }
    /** The impact's pitch: the song's root, or the kick's own body pitch (shared by a Kybalion on
        the kick track), folded into 35-90 Hz, so the impact lands on the kick instead of against it. */
    static float impactPitch (float rootHz, float kickHz, bool followKick)
    {
        if (! followKick || kickHz < 25.0f || kickHz > 400.0f) return rootHz;
        auto hz = kickHz;
        while (hz > 90.0f) hz *= 0.5f;
        while (hz < 35.0f) hz *= 2.0f;
        return hz;
    }
    std::atomic<float> lastImpactHz { 0.0f };
    enum DropFx { dfOff, dfTapeStop, dfStutter };
    enum BuildShape { bsSmooth, bsStepped, bsLateSurge };
    static double dropFxQuarters (int i) { static const double q[] = { 0.5, 1.0, 2.0 }; return q[juce::jlimit (0, 2, i)]; }
    /** Build progress (0..1) bent by the Build shape: smooth, in four stages, or saving most of the rise for the end.
        (The snare roll already moves in stages, so Stepped leaves it alone.) */
    static float shapeProgress (int shape, float p)
    {
        if (p < 0.0f) return p;
        if (shape == bsStepped) return juce::jmin (1.0f, (std::floor (p * 4.0f) + 1.0f) / 4.0f);   // four stages, each a step up
        if (shape == bsLateSurge) return p * p;
        return p;
    }

    /** Your own samples in place of the synthesised impact, crash and downlifter (message thread). */
    enum SampleSlot { slotImpact, slotCrash, slotDown, slotTag, numSampleSlots };
    enum TagWhere { tagOff, tagFirstDrop, tagEveryDrop };
    std::atomic<int> tagsPlayed { 0 };
    bool loadSample (int slot, const juce::File&);
    void clearSample (int slot);
    juce::String sampleName (int slot) const;

    /** Sound design presets: every Azoth setting and your samples, as a file for any project. */
    static juce::File& presetFolderOverride() { static juce::File f; return f; }   // tests
    static juce::File presetFolder()
    {
        if (presetFolderOverride() != juce::File()) return presetFolderOverride();
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Antioch Suite").getChildFile ("Azoth presets");
    }
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File&);
    static juce::Array<juce::File> presetFiles();
    float samplePitch (int slot) const;
    static constexpr double lookaheadSeconds = 3.0;
    static double reverseLengthQuarters (int i) { static const double q[] = { 2.0, 4.0, 8.0 }; return q[juce::jlimit (0, 2, i)]; }

    /** Message thread: applies the Reverse swells switch to the reported latency (called on a timer). */
    void updateLatency();
    int getActiveLatency() const { return activeLatency.load(); }
    int64_t reverseSamplesPlayed() const { return swell.playedSamples.load(); }
    static double swellQuarters (int i) { static const double q[] = { 1.0, 2.0, 4.0, 8.0 }; return q[juce::jlimit (0, 3, i)]; }
    static double gapQuarters (int i)   { static const double q[] = { 0.0, 0.5, 1.0 }; return q[juce::jlimit (0, 2, i)]; }

    enum Audition { aImpact = 1, aCrash = 2, aDown = 4, aSnare = 8 };
    void audition (int which) { pendingAudition.fetch_or (which); }

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<suite::FixReceiver> fixes;   // Mentalism's fix-it buttons

    // For the UI and tests
    std::atomic<int> eventsFired { 0 }, snareHits { 0 };
    std::atomic<float> rollPitchNow { 0.0f };   // semitones the last roll hit was raised
    std::atomic<float> lastImpactLevel { 0.0f };
    std::atomic<bool> mapAvailable { false };
    std::atomic<float> lastRise { -1.0f };   // riser progress, for tests and the UI
    std::atomic<int> nextType { -1 };
    std::atomic<float> barsToNext { 0.0f };
    std::atomic<int> dropFxRuns { 0 };   // tape stops / stutters played

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    double tuning (int slot, float rootHz) const;   // playback ratio that puts a sample on the song's key
    void enterSection (suite::SectionType type, suite::SectionType previous, double bpm, double barQ, float rootHz, bool laterDrop);

    suite::SectionFollower sections;
    azoth::Impact impact;
    azoth::Crash crash;
    azoth::Downlifter downlifter;
    std::array<azoth::Snare, 3> snares;
    std::array<azoth::SamplePlayer, numSampleSlots> samplePlayers;
    std::vector<std::shared_ptr<const azoth::Sample>> sampleKeep;   // never freed on the audio thread
    std::array<juce::String, numSampleSlots> samplePaths;

    // Drop entrance FX on this track's audio: a short history to replay from.
    std::array<std::vector<float>, 2> history;
    int historyPos = 0;
    double tapeRead = 0.0;
    int64_t stutterCount = 0;
    int stutterStart = 0;
    bool dropFxActive = false;
    std::array<suite::Svf, 2> buildFilter;
    float buildFilterHz = 0.0f;
    int buildFilterCounter = 0;
    azoth::Riser riser;
    std::array<suite::PitchShifter, 2> trackShift;   // the track's own audio, pitched up through builds
    float trackRiseMix = 0.0f, liftMix = 0.0f;
    float energyGain = 1.0f, riseGain = 1.0f;   // from Ouroboros' energy curve
    int trackShiftCounter = 0;
    int nextSnare = 0;
    suite::Noise swellNoise { 0x51E11u };
    suite::Svf swellFilter;

    int previousIndex = -1;
    suite::SectionType previousType = suite::SectionType::Intro;
    double previousPos = 0.0;
    double rollPos = -1.0e9;   // previous sample position, for snare grid crossings
    std::atomic<int> pendingAudition { 0 };

    // Reverse swells: fixed lookahead (when on), dry delay, and the swell engine.
    void timerCallback() override { updateLatency(); }
    ReverseSwell swell;
    std::array<std::vector<float>, 2> dryDelay;
    int dryDelayPos = 0;
    std::atomic<int> activeLatency { 0 };
    int64_t inputIndex = 0;
    double previousInputPos = -1.0e9;
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AzothProcessor)
};
