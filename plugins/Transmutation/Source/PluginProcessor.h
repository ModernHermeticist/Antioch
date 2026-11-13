#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "FixBus.h"
#include "AutopilotBus.h"
#include "MoveLog.h"
#include "SectionBus.h"
#include "HostInfo.h"
#include "Director.h"
#include "DirectorBus.h"
#include "Engine.h"
#include "Recipes.h"
#include <atomic>
#include <map>

namespace TParams
{
    inline const juce::String spice   { "spice" };     // how much happens, subtle to wild
    inline const juce::String builds  { "builds" };    // how hard the builds work (Mentalism's "build rise" fix turns this up)
    inline const juce::String drops   { "drops" };     // how much the drops do (Mentalism's "build impact" fix turns this up)
    inline const juce::String amount  { "amount" };    // dry/wet of every move
    inline const juce::String output  { "output" };
    inline const juce::String genre   { "genre" };
    inline const juce::String role    { "role" };      // 0 = Auto (from the track's name / Prima Materia)
    inline const juce::String direct  { "director" };  // the director plans moves (off: only what you audition)

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}

class TransmutationProcessor : public suite::SuiteProcessor
{
public:
    TransmutationProcessor();
    ~TransmutationProcessor() override;

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
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getPluginState (juce::MemoryBlock& destData) override;
    void setPluginState (const void* data, int sizeInBytes) override;
    bool applyRoleSetup (int role, const juce::String& trackName) override;
    SuiteStatus suiteStatus() override;
    void pollTick() override;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<suite::FixReceiver> fixes;   // Mentalism: "build rise" and "build impact"

    // ---- The director (message thread) ----
    /** Plans again now if anything it depends on changed (tests call it directly). Returns true if the plan changed. */
    bool replan (bool force = false);
    trm::Plan getPlan() const { const juce::ScopedLock sl (planLock); return plan; }
    trm::DirectorInput lastInput() const { const juce::ScopedLock sl (planLock); return input; }
    int effectiveRole() const;
    trm::Profile hearing() const { const juce::ScopedLock sl (planLock); return profile; }

    // per-move Auto / Always / Never, locks and re-rolls (message thread; saved with the project)
    uint8_t modeOf (int id) const { const juce::ScopedLock sl (planLock); return modes[(size_t) juce::jlimit (1, trm::numEffects, id)]; }
    void setMode (int id, uint8_t mode);
    void lockMove (const trm::Move&);
    void unlockMoment (uint32_t key);
    bool isLocked (uint32_t key) const { const juce::ScopedLock sl (planLock); return locks.count (key) > 0; }
    void rerollMoment (uint32_t key);
    void rerollAll();
    /** Writes this track's plan as MIDI (a note and a marker per move). */
    bool exportPlan (const juce::File& file) const;
    /** Plays a move now, for a bar, over whatever is playing. */
    void audition (int id) { auditionRequest.store (id); }

    // ---- Read by the UI ----
    std::atomic<double> songPos { 0.0 }, bpmNow { 138.0 };
    std::atomic<bool> playingNow { false }, mapFound { false };
    std::array<std::atomic<int>, 6> activeNow {};
    std::atomic<int> runawaysCaught { 0 };
    std::atomic<float> levelHoldDb { 0.0f };
    std::atomic<int> memoryBars { 0 };

    // tests
    trm::Engine& engineForTests() { return engine; }
    void setProfileForTests (const trm::Profile& p) { const juce::ScopedLock sl (planLock); profile = p; profileLocked = true; }

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void updateProfile();
    void publishPlan();
    trm::Ctx contextFor (const trm::Move&, double posQ, const suite::SectionFollower::Position&, double barQ) const;

    trm::Engine engine;
    double sampleRate = 48000.0;
    suite::SectionFollower sections;
    suite::DirectorBus::Follower suiteAudio, suiteMsg;   // the suite director's plan (audio thread / message thread)
    float silenceGain = 1.0f;                             // the director's silences, ramped
    juce::SmoothedValue<float> outGain;

    // the plan, handed to the audio thread
    mutable juce::CriticalSection planLock;
    trm::Plan plan;
    trm::DirectorInput input;
    uint64_t inputHash = 0;
    std::array<uint8_t, trm::numEffects + 1> modes = trm::defaultModes();
    std::map<uint32_t, int> locks, rerolls;
    trm::Profile profile;
    bool profileLocked = false;
    int profileVotes = 0;
    trm::Profile candidate;
    double lastPublishMs = 0.0, lastProfileMs = 0.0;
    std::array<float, trm::numEffects + 1> signature {};
    double signatureReadMs = -1.0e9;

    static constexpr int maxAudioMoves = 260;
    juce::SpinLock audioLock;
    std::array<trm::Move, maxAudioMoves> pending {}, moves {};
    int pendingCount = 0, moveCount = 0;
    std::atomic<bool> pendingFresh { false };
    std::array<int64_t, maxAudioMoves> anchors {};
    std::atomic<int> auditionRequest { 0 };
    trm::Move auditionMove;
    int64_t auditionAnchor = -1;

    // audio-thread state
    std::atomic<uint16_t> borrowedMask { 0 };
    std::atomic<int> autoRole { 0 };   // the role from the track's name / Prima Materia (message thread; the registry allocates)
    int lastChordIndex = -1;
    int64_t chordAnchor = 0;
    double lastPosQ = -1.0;
    bool memoryArmed = true;
    double memoryEndQ = -1.0;
    float runawaySeconds = 0.0f;
    std::atomic<bool> runawayFlag { false };
    std::atomic<double> statEnergy { 0.0 }, statDiff { 0.0 }, statSide { 0.0 };
    std::atomic<int> statOnsets { 0 }, statSamples { 0 }, statHeld { 0 };
    std::atomic<float> statPitch { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransmutationProcessor)
};
