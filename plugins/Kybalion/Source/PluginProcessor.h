#pragma once
#include "KitBus.h"

#include "Alias.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "KickBus.h"
#include "Engines.h"
#include "SectionBus.h"
#include "DirectorBus.h"
#include "Scenes.h"
#include "StyleLog.h"
#include "FixBus.h"
#include "KeyBus.h"
#include "Music.h"
#include "AutopilotBus.h"
#include "LabelReadyBus.h"
#include "MixLatch.h"
#include <map>

class KybalionProcessor : public suite::SuiteProcessor
{
public:
    KybalionProcessor();
    ~KybalionProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getPluginState (juce::MemoryBlock& destData) override;
    void setPluginState (const void* data, int sizeInBytes) override;

    /** Message thread: load a gate pattern or ducking preset into the parameters. */
    void applyGatePattern (int index);
    void applyDuckPreset (int index);
    void randomiseGate (juce::Random&);

    juce::AudioProcessorValueTreeState apvts;
    suite::SceneEngine scenes;
    /** Learning from your tweaks: a knob moved by hand in a section is offered as that section's scene. */
    void pollTick() override { scenes.watchTweaks (isMorphing()); kickBassTick(); mixTick(); }
    /** The mix fixes here: the pump's depth per section (deeper in drops, lighter in breakdowns),
        and fit to the kick by itself once a sidechained kick has been heard. Each latched once
        Ouroboros' checks ask for it, while its autopilot switch is on; saved with the project. */
    void mixTick();
    std::atomic<bool> pumpPerSection { false }, phasePerSection { false };
    bool autoFitted = false;
    suite::MixLatch mixLatch { apvts };
    /** Kick and bass phase per section: the low end against the kick in each kind of section (with the knob's
        polarity), and the sections where it cancels flipped on their own (cross-faded, no click). */
    std::array<double, 6> secMS {}, secMM {}, secSS {};
    std::array<std::atomic<bool>, 6> sectionFlip {};
    float polGain = 1.0f;
    static float pumpScale (int sectionType) noexcept
    {
        switch (sectionType) { case 4: return 1.1f; case 2: return 0.6f; case 3: return 0.85f; case 0: case 5: return 0.8f; default: return 1.0f; }
    }

    /** Kick and bass: where the kick's body sits against the key ("tune it up a semitone to the
        root"), and with Duck focus on Auto, whether to duck only around the kick's note (when
        most of this track's low end lives elsewhere, so it keeps sustaining) or the whole sound.
        Message thread; logs to What changed when either changes. */
    void kickBassTick();
    static juce::String kickTuning (float bodyHz, int keyRoot, bool haveKey);
    /** Auto focus: duck around the kick's note when less than half this track's low end is there. */
    static bool focusFor (float shareOnKickNote, bool wasFocused) { return wasFocused ? shareOnKickNote < 0.6f : shareOnKickNote < 0.4f; }
    std::atomic<bool> focusAuto { false };
    std::atomic<float> kickBodyHz { 0.0f }, shareOnKickNote { -1.0f };
    juce::String tuningText;   // message thread (UI)
    suite::Svf kickNoteBand;
    float kickNoteBandHz = 0.0f;
    double kickNotePower = 0.0, lowPower = 0.0;
    /** New song (Ouroboros): section scenes on, with the suggested offsets. */
    bool applySession (const suite::SessionStyle&, int, int) override
    {
        setParameterPlain (apvts, suite::SceneEngine::enabledId, 1.0f);
        scenes.applySuggested();
        return true;
    }
    enum SceneSlot { sGateDepth, sGateLength, sGateFilter, sSwing, sDuckDepth };

    // Read by the UI.
    std::atomic<double> currentBpm { 138.0 };
    std::atomic<int> gateStep { 0 };
    std::atomic<int> currentFill { 0 };   // TranceGate::Fill playing now (0 = none)
    std::atomic<float> duckGain { 1.0f };
    std::atomic<float> duckMs { 1.0e9f };      // time since the last duck trigger
    std::atomic<bool> sidechainConnected { false };
    std::atomic<int> currentSection { -1 };      // SectionType from Ouroboros, -1 if none
    std::atomic<bool> autoBreakdown { false };   // ducking paused because of the section

    /** The kick on the sidechain (whenever one is connected): length and pitch. */
    KickAnalyser kick;
    /** Sets duck hold and release from the measured kick. False until a few kicks were heard. */
    bool fitToKick();
    static constexpr int kicksNeeded = 3;

    /** On the kick track: learns the kick pattern and shares it (Share kick). */
    suite::KickLearner kickLearner;
    /** On the kick track: its own kick's length and body pitch, shared with the pattern (Azoth tunes its impact to it). */
    KickAnalyser ownKick;
    std::atomic<bool> suiteKickAvailable { false };   // a shared pattern is being received
    SuiteStatus suiteStatus() override;
    std::atomic<float> suiteSwing { 0.0f };
    std::atomic<float> gateDepthNow { 0.0f };   // the gate's depth in use (after the energy curve)
    std::atomic<float> suiteChordQ { 0.0f };   // Correspondence's chord length in quarters (0 = not running)
    std::atomic<int> chordAccents { 0 };       // chord changes accented (for tests and the UI)
    bool inChordAccent = false;
    std::atomic<float> dynamicDepth { 1.0f };

    /** Kick and bass phase: with the kick on the sidechain, how this track's low end lines up
        with it while the kick sounds (-1 = cancelling, +1 = adding up). Flip polarity fixes it. */
    std::atomic<float> lowCorrelation { 0.0f };
    std::atomic<bool> phaseMeasured { false };
    double phaseMS = 0.0, phaseMM = 0.0, phaseSS = 0.0;
    float phaseLowM = 0.0f, phaseLowS = 0.0f, phaseEnv = 0.0f;   // the dynamic gate's depth scale now (tests and UI)   // Correspondence's groove (the gate swings with it)

    /** Your own gate patterns: saved as files, offered in every project. */
    static juce::File userPatternFolder()
    {
        return suite::Alias::file ("Gate patterns");
    }
    bool saveGatePattern (const juce::File& folder, const juce::String& name) const
    {
        juce::StringArray steps;
        for (int i = 0; i < Params::numSteps; ++i) steps.add (juce::String (stepParams[(size_t) i]->load(), 3));
        folder.createDirectory();
        return folder.getChildFile (juce::File::createLegalFileName (name) + ".gate").replaceWithText (steps.joinIntoString (","));
    }
    static std::vector<std::pair<juce::String, std::array<float, Params::numSteps>>> loadGatePatterns (const juce::File& folder)
    {
        std::vector<std::pair<juce::String, std::array<float, Params::numSteps>>> out;
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.gate"))
        {
            const auto v = juce::StringArray::fromTokens (f.loadFileAsString(), ",", "");
            if (v.size() != Params::numSteps) continue;
            std::array<float, Params::numSteps> steps {};
            for (int i = 0; i < Params::numSteps; ++i) steps[(size_t) i] = juce::jlimit (0.0f, 1.0f, v[i].getFloatValue());
            out.push_back ({ f.getFileNameWithoutExtension(), steps });
        }
        std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        return out;
    }
    /** The gate takes the song's kit rhythm (Correspondence): its chord part's or its arp's 16
        steps. Returns false if Correspondence isn't sharing one. */
    bool applyKitRhythm (bool arp)
    {
        const auto kit = suite::KitBus::read();
        if (! kit.valid) return false;
        std::array<float, Params::numSteps> steps {};
        for (int i = 0; i < Params::numSteps; ++i) steps[(size_t) i] = (arp ? kit.arp : kit.chords)[(size_t) i];
        applySteps (steps);
        return true;
    }
    void applySteps (const std::array<float, Params::numSteps>& steps)
    {
        for (int i = 0; i < Params::numSteps; ++i) setParam (Params::Gate::step (i), steps[(size_t) i]);
    }
    void trackNameChanged (const juce::String&) override;
    /** Role-aware setup: bass ducks tight to the suite kick (no gate), pads gate and duck deep,
        leads duck gently, vocals duck gently with no gate. */
    bool applyRoleSetup (int role, const juce::String& trackName) override;
    /** The learning pass (Ouroboros): fit hold and release to the kick heard, and flip the
        polarity where the low end cancels against it. Undoable. */
    std::unique_ptr<suite::FixReceiver> fixes;
    /** Ducking depth from the overlap: how loud this track's low end (below 120 Hz) is while the
        kick hits, and the duck depth that clears it. The learning pass's kick fit sets it. */
    float overlapDb() const;
    static float depthForOverlap (float db);
    suite::Svf overlapLow;
    double overlapPower = 0.0;
    int64_t overlapSamples = 0;

    /** Your gate patterns: once a song has played into its final drop with the gate on, its
        pattern goes into the style log (with the kind of track, from its name). The pattern list
        puts your most used first, and a new Kybalion starts on your favourite (for that kind of
        track once its name is known), once you've kept a pattern in two or more songs. */
    using Steps = std::array<float, Params::numSteps>;
    static juce::String stepsText (const Steps&);
    static Steps stepsFromText (const juce::String&);
    static juce::String trackKind (const juce::String& trackName);   // pads, plucks, leads, bass, vocals, other
    /** Patterns by how often you kept them (most first), for a kind of track ("" = any). */
    static std::vector<std::pair<Steps, int>> favouritePatterns (const std::vector<suite::StyleLog::Entry>&, const juce::String& kind = {});
    Steps currentSteps() const;
    void logPatternOnce();
    std::atomic<bool> reachedFinalDrop { false }, patternLogged { false };
    bool startedOnFavourite = false;
    Steps startSteps {};   // what this instance started with: replaced by a favourite only while untouched
    juce::String kind;

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float plainValue);

    TranceGate gate;
    Ducker ducker;
    SidechainTrigger scTrigger;
    suite::DirectorBus::Follower suiteDirector;   // the suite director's plan
    int lastDirectorReport = -1;
    float directorSilence = 1.0f;
    bool reentryAccent = false;
    suite::SectionFollower sections;
    std::array<std::atomic<float>*, Params::numSteps> stepParams {};
    juce::SmoothedValue<float> outGain;
    double position = 0.0, sampleRate = 44100.0;
    int fillNow = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (KybalionProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KybalionProcessor)
};
