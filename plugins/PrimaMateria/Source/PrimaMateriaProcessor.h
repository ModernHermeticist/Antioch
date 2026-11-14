#pragma once

#include "RoleCatalog.h"
#include "Alias.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "DirectorBus.h"
#include "AutopilotBus.h"
#include "SuiteDSP.h"
#include "FixBus.h"
#include "Params.h"
#include "SuiteDSP.h"
#include "HostInfo.h"
#include "Analysis.h"
#include "Registry.h"
#include "PlanBus.h"
#include "AutomationBus.h"
#include "KickBus.h"
#include <optional>

class PrimaMateriaProcessor : public suite::SuiteProcessor
{
public:
    PrimaMateriaProcessor();
    ~PrimaMateriaProcessor() override { suite::PlanBus::withdraw (getInstanceId()); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Prima Materia"; }   // a module inside another plugin: keeps its own name for the suite
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getPluginState (juce::MemoryBlock& destData) override;
    void setPluginState (const void* data, int sizeInBytes) override;

    void applyRole (int index);
    /** A detailed role (RoleCatalog.h): its broad role's settings, then its own sections, cuts and ducking. */
    void applySubRole (int role);
    int getSubRole() const noexcept { return subRoleNow.load(); }
    std::atomic<int> subRoleNow { -1 };
    /** Inside Quintessence: the role comes from there (the role box only shows it). */
    bool hosted = false;
    int getRole() const { return (int) apvts.getRawParameterValue (pm::roleId)->load() - 1; }   // -1 = not set

    /** This track's loudness (LUFS, gated), measured in Drop sections (or all playback without a map). */
    float measuredLufs() const { return loudness.integrated(); }

    //==============================================================================
    // Prima Materia never changes the track's level, mutes it or filters it: it works out the
    // section-by-section moves and suggests them (the fader, mutes, filters, the gap before drops,
    // the low end, carves, the vocal's room), for you to make in FL's mixer - or exported as fader
    // automation. The track passes through untouched, apart from the autopilot's DC and rumble filter.
    /** Hear the suggestions (momentary; not saved with the project). Tests use it to check the plan. */
    std::atomic<bool> previewPlan { false };
    std::atomic<double> lastBpm { 138.0 };
    /** The suggested mixer moves, in plain words, one per line. */
    juce::StringArray planLines() const;
    /** The suggested fader level (dB, before trim and channel gain) in section type t; offDb = muted. */
    float planLevelDb (int type) const { return settingFor (type).levelDb; }
    /** Writes "<track> - mixer moves.mid" (CC 7 fader moves on channel 1 following the Ouroboros
        map; 100 = unchanged, 0 = muted) and a .txt of the suggestions into `folder`. Returns the
        MIDI file, or an empty File if there's no map. */
    juce::File exportPlan (const juce::File& folder) const;
    /** The CC 7 value for a fader change of `db` (100 = unchanged; FL's linked fader at 100%). */
    /** The plan as numbers (shared with Ouroboros' Mixer page through the plan bus). */
    suite::PlanBus::Plan makePlan() const;
    void pollTick() override
    {
        // once a second: the plan only changes with the map and its settings (and it now runs on every Quintessence)
        if (planTick++ % 5 != 0) return;
        if (getRole() > 0) suite::PlanBus::publish (makePlan()); else suite::PlanBus::withdraw (getInstanceId());
    }
    int planTick = 0;
    static int faderCc (float db) { return suite::PlanBus::faderCc (db); }
    SuiteStatus suiteStatus() override;

    /** Your own role settings: "Save as my <role>" keeps this track's section levels, cuts,
        ramp, gap, breath and pump for its role; every time that role is applied (by hand or
        from the track name) your settings come after the built-in ones. */
    static juce::File& roleFolderOverride() { static juce::File f; return f; }   // tests
    static juce::File userRoleFolder()
    {
        if (roleFolderOverride() != juce::File()) return roleFolderOverride();
        return suite::Alias::file ("Roles");
    }
    static juce::StringArray myRoleParamIds();
    bool saveMyRole();
    static bool hasMyRole (int role);
    bool joinsChains() const override { return false; }
    /** The recipe name of a role's signature chain (the suite plugins on a track of that role). */
    static juce::String chainName (int role) { return "Chain " + juce::String (pm::roles()[(size_t) juce::jlimit (0, (int) pm::roles().size() - 1, role)].name); }
    static void forgetMyRole (int role);

    /** Auto-role from the sound: for a track with no role yet (and a name that didn't say),
        once 8 s of it has been heard the role is guessed from its spectrum and how punchy it
        is (crest factor): kick, bass, hats, pads or lead. Returns -1 when unsure. */
    /** The rough mix's level for a role against the kick: yours (from finished songs) or built in. */
    static float roughMixOffsetFor (int role);
    static int roleFromSound (const std::array<float, analysis::numBands>& bandsDb, float crestDb);
    float averageCrestDb() const { const auto n = crestCount.load(); return n > 0 ? crestSum.load() / (float) n : 0.0f; }
    std::atomic<bool> roleGuessed { false };
    std::atomic<bool> clippedRecently { false };
    std::atomic<bool> learnReset { false };
    std::atomic<bool> busyPart { false };   // an arp or pluck (by its name): thins out more while the vocal sings
    std::atomic<float> riderDbNow { 0.0f };   // the vocal rider's gain now (tests, UI)
    float rideDb = 0.0f;   // the learning pass started: measure afresh   // over 0 dBFS since the last status (live mix check)
    void guessRoleFromSound();

    /** Club edit: DJ-friendly intro and outro: pads, leads and vocals out, the kick in. */
    bool applyVersion (int kind) override
    {
        if (kind != suite::clubEdit) return false;
        const auto role = getRole();
        bool changed = false;
        for (auto t : { suite::SectionType::Intro, suite::SectionType::Outro })
        {
            const auto id = pm::levelId ((int) t);
            const auto level = apvts.getRawParameterValue (id)->load();
            if ((role == 4 || role == 5 || role == 6) && level > pm::offDb + 0.5f) { setParameterPlain (apvts, id, pm::offDb); changed = true; }
            if (role == 1 && level <= pm::offDb + 0.5f) { setParameterPlain (apvts, id, 0.0f); changed = true; }
        }
        return changed;
    }
    void trackNameChanged (const juce::String&) override;
    /** Rough mix: the trim that puts this track at its role's level relative to the kick (nullopt = no change). */
    std::optional<float> roughMixTrim() const;

    /** Low-end manager: section types (bits; bit 6 = Drop 2+) where a kick or bass track plays,
        from the suite registry (updated once a second). */
    std::atomic<uint32_t> lowOwners { 0 };
    std::atomic<float> managerCutNow { 0.0f };   // the cut the manager is applying now (0 = none), for the UI
    /** Which section types this track sounds in (bits; bit 6 = Drop 2+). */
    uint32_t soundingMask() const;
    void refreshLowOwners();

    /** Gain staging: this track's input level (RMS dBFS while playing, before Input Gain), and the
        input gain that puts it at -18 dBFS. */
    float inputLevelDb() const
    {
        const auto n = inputCount.load();
        return n > 0 ? juce::Decibels::gainToDecibels ((float) std::sqrt (inputPower.load() / (double) n), -100.0f) : -100.0f;
    }
    /** Continuous gain staging (message thread, every status update): while it's on, every
        10 s the input gain moves to put the input at -18 dBFS RMS, if it's more than 1.5 dB off. */
    void autoStage();
    int autoStageTicks = 0;
    std::atomic<int> autoStageMoves { 0 };
    std::optional<float> gainStageTarget() const
    {
        const auto level = inputLevelDb();
        if (level <= -80.0f) return std::nullopt;
        return juce::jlimit (-24.0f, 24.0f, -18.0f - level);
    }

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<suite::FixReceiver> fixes;   // Mentalism's fix-it buttons

    /** Arc match (Mentalism): a level change for one section of the map, kept with the
        section's start bar so it only applies while the map still has that section there. */
    struct ArcTrim { std::atomic<int> bar { -1 }; std::atomic<float> db { 0.0f }; };
    std::array<ArcTrim, suite::SectionMap::maxSections> arcTrims;
    float arcTrimFor (int index, int startBar) const
    {
        if (! juce::isPositiveAndBelow (index, (int) arcTrims.size())) return 0.0f;
        const auto& t = arcTrims[(size_t) index];
        return t.bar.load() == startBar ? t.db.load() : 0.0f;
    }

    // For the UI
    std::atomic<int> currentType { -1 };
    std::atomic<bool> ramping { false }, gapping { false }, mapAvailable { false };
    std::atomic<float> nowLevelDb { 0.0f }, nowLowCut { pm::lowCutOff }, nowHighCut { pm::highCutOff };

public:
    std::atomic<int> peaksCaught { 0 };   // (tests)
private:
    // Autopilot (stability): DC / subsonic filter, peak guard, wobble rider, click-free switches
    std::array<suite::Svf, 2> subsonic, passSubsonic;   // passSubsonic: on the untouched output
    juce::AudioBuffer<float> dry;                        // the track as it came in (it goes out like that)
    float guardGain = 1.0f, guardRelease = 0.001f, normalPeak = 0.0f, peakWarm = 0.0f, peakBlock = 0.0f, wobbleDb = 0.0f, wobbleWarm = 0.0f, wobbleSmooth = 0.001f, lastTargetDb = 0.0f;
    double wobbleShort = 0.0, wobbleLong = 0.0, wobbleShortCoeff = 0.001, wobbleLongCoeff = 0.0001;
    bool guarding = false, wobbling = false;
    int overSamples = 0;

private:
    pm::Setting settingFor (int type) const;

    suite::DirectorBus::Follower suiteDirector;   // the suite director's silences (suggested)
    suite::SectionFollower sections;
    analysis::LoudnessMeter loudness;
    analysis::BandAnalyzer spectrum;
    std::atomic<float> crestSum { 0.0f };
    std::atomic<int> crestCount { 0 };
    float windowPeak = 0.0f;
    double windowPower = 0.0;
    int windowSamples = 0;
    std::atomic<double> inputPower { 0.0 };
    std::atomic<int64_t> inputCount { 0 };
    float pumpGain = 1.0f, vocalGain = 1.0f;   // the track's spectrum in the drops, for the masking map
    std::array<suite::Biquad, 2> carve;
    int carveBandNow = -1; float carveDepthNow = 0.0f;
    juce::SmoothedValue<float> trimGain;
    float lastTrim = 0.0f, lastInputGain = 0.0f;
    std::atomic<float> measuredAtTrim { 0.0f };   // the trim the current loudness reading was measured at
    std::array<suite::Svf, 2> lowCut, highCut;
    suite::Svf monoSide;   // mono lows
    float monoHzNow = 0.0f;
    float levelDb = 0.0f, lowLog = 0.0f, highLog = 0.0f, gapGain = 1.0f;
    double sampleRate = 48000.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PrimaMateriaProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrimaMateriaProcessor)
};
