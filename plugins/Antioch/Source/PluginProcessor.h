#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "ConductorBus.h"
#include "Engines.h"
#include "KeyBus.h"
#include "SectionBus.h"
#include "Scenes.h"
#include "Parameters.h"
#include "MoveLog.h"
#include "AutomationBus.h"
#include "ActivityBus.h"

class AntiochProcessor : public suite::SuiteProcessor
{
public:
    AntiochProcessor();
    ~AntiochProcessor() override { suite::Conductor::forget (getInstanceId()); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processSuiteBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getPluginState (juce::MemoryBlock& destData) override;
    void setPluginState (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    suite::SceneEngine scenes;
    /** Learning from your tweaks: a knob moved by hand in a section is offered as that section's scene. */
    void pollTick() override
    {
        scenes.watchTweaks (isMorphing());
        onLeadTrack.store (roleForTrack() == suite::roleLead);
        glueRoomTick();
        spacePass();
    }

    /** Glue room: a new Antioch on the music bus (a track named "bus", "group" or "music")
        becomes one short, shared room for everything on it: a 0.9 s small room, no echo or
        throws, and its level sets itself (the room about 18 dB under the music). The tracks'
        own Antiochs give each part its depth; this puts them in one space. */
    bool glueRoomTick();
    bool onBusTrack() const
    {
        const auto n = getTrackName().toLowerCase();
        return n.contains ("bus") || n.contains ("group") || n.contains ("music");
    }
    bool glueRoomChecked = false;

    /** Auto space level: every 4 s of the dry part playing (outside breakdowns, where the scenes
        open the space on purpose), how far the echo and reverb sit under the dry part, against
        what the track's role usually has (a lead's tail about 12 dB under it). One trim step
        (up to 1.5 dB) a pass; the next pass keeps it if the tail got closer to the target, and
        undoes it otherwise. Returns true if it moved or undid. Message thread (tests call it). */
    bool spacePass();
    static float spaceTargetFor (int role);   // tail minus dry, dB (0: leave it alone)
    static constexpr float spacePassSeconds = 4.0f;
    std::atomic<float> spaceHeard { 0.0f }, spaceTailDb { -100.0f };
    std::atomic<bool> throwsHeld { false };   // automatic throws waiting for the lead's phrase to end   // this pass: seconds heard, tail minus dry (dB)
    std::atomic<int> spaceKept { 0 }, spaceUndone { 0 };
    std::atomic<bool> onLeadTrack { false };   // on the lead itself, lead-aware space stays out of the way
    std::atomic<float> leadRoomNow { 1.0f };
    std::atomic<int> chordShift { 0 };   // semitones the resonators follow the chord (tests, UI)    // the wet gain lead-aware space is applying (tests, UI)
    float leadRoomGain = 1.0f;
    /** New song (Ouroboros): section scenes on, with the suggested offsets. */
    bool applySession (const suite::SessionStyle&, int, int) override
    {
        setParameterPlain (apvts, suite::SceneEngine::enabledId, 1.0f);
        scenes.applySuggested();
        return true;
    }

    /** Scene parameter slots (indices into `scenes`). */
    enum SceneSlot { sDry, sEcho, sVerb, sThrow, sDecay, sShimmer, sHarmonics, sEchoDamp };

    // Read by the UI.
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<int> effectiveRoot { 0 };      // root actually used (local or followed)
    std::atomic<bool> followingBus { false };  // true when the root comes from Correspondence
    std::atomic<int> busScale { 0 };
    std::atomic<float> throwGate { 0.0f };     // 0..1, lights the THROW button
    std::atomic<float> duckAmount { 0.0f };    // current reverb gain reduction 0..1
    std::atomic<bool> ouroborosFound { false };
    std::atomic<float> tensionNow { 0.0f }, reverbWidthNow { 1.0f };   // tension width (tests and UI)
    /** Role-aware setup: on a vocal, throws at the end of every sung phrase; on a lead, at the
        end of every section (into the next). */
    bool applyRoleSetup (int role, const juce::String&) override
    {
        if (role == suite::roleVocal || role == suite::roleLead || role == suite::rolePads)
            setParameterPlain (apvts, Params::autoSpace, 1.0f);   // its space sets itself
        if (role != suite::roleVocal && role != suite::roleLead) return role == suite::rolePads;
        setParameterPlain (apvts, Params::Throw::autoMode, (float) (role == suite::roleVocal ? Params::Throw::autoAtVocalEnds : Params::Throw::autoAtSectionEnds));
        setParameterPlain (apvts, Params::Throw::autoLen, role == suite::roleVocal ? 1.0f : 2.0f);   // last 1/8, or the last beat
        return true;
    }

    /** Project tidy: idle when no echo, reverb or throw reaches the output. */
    SuiteStatus suiteStatus() override
    {
        SuiteStatus s;
        auto v = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
        if (v (Params::echoLevel) < 0.001f && v (Params::verbLevel) < 0.001f && v (Params::throwLevel) < 0.001f) s.flags |= suite::Status::idle;
        return s;
    }
    std::atomic<float> pumpNow { 1.0f };          // the reverb pump's lowest gain in the last block
    std::atomic<bool> vocalMarksFound { false };  // Solve et Coagula has marked vocal bars
    float reverbPump = 1.0f;
    std::atomic<bool> spaceReset { true };
    double spaceDry = 0.0, spaceWet = 0.0;
    int64_t spaceSamples = 0;
    struct { bool pending = false; float trimBefore = 0.0f, errBefore = 0.0f; int rest = 0; } space;

private:
    juce::AudioParameterBool* boolParam (const juce::String& id);
    juce::AudioParameterChoice* choiceParam (const juce::String& id);
    juce::AudioParameterInt* intParam (const juce::String& id);
    float value (const juce::String& id) const;

    struct TapPtrs
    {
        juce::AudioParameterBool* on = nullptr;
        juce::AudioParameterChoice* division = nullptr;
        juce::AudioParameterInt* harmonic = nullptr;
        std::atomic<float>* timeMs = nullptr;
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* feedback = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
    };
    std::array<TapPtrs, Params::numTaps> tapPtrs;

    HarmonicEcho echo;
    EuphoricReverb reverb;
public:
    FreezeWash freeze;
    std::atomic<int> freezesPlayed { 0 };
private:
    int freezeSectionIndex = -1;
    double freezeLastPos = -1.0e9;
    ThrowDelay throwDelay;
    suite::EnvelopeFollower dryEnvelope;
    suite::SectionFollower sections;

    juce::SmoothedValue<float> dryGain, echoGain, verbGain, throwGain, outGain, echoSend, throwSend, verbDuck, throwDuck;
    double sampleRate = 44100.0;
    bool firstBlock = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AntiochProcessor)
};
