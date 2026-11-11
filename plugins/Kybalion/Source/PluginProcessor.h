#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "KickBus.h"
#include "Engines.h"
#include "SectionBus.h"
#include "Scenes.h"

class KybalionProcessor : public suite::SuiteProcessor
{
public:
    KybalionProcessor();
    ~KybalionProcessor() override = default;

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
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Antioch Suite").getChildFile ("Gate patterns");
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
    void applySteps (const std::array<float, Params::numSteps>& steps)
    {
        for (int i = 0; i < Params::numSteps; ++i) setParam (Params::Gate::step (i), steps[(size_t) i]);
    }
    void trackNameChanged (const juce::String&) override;

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float plainValue);

    TranceGate gate;
    Ducker ducker;
    SidechainTrigger scTrigger;
    suite::SectionFollower sections;
    std::array<std::atomic<float>*, Params::numSteps> stepParams {};
    juce::SmoothedValue<float> outGain;
    double position = 0.0, sampleRate = 44100.0;
    int fillNow = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (KybalionProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KybalionProcessor)
};
