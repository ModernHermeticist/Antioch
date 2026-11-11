#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Unmasker.h"
#include "Scenes.h"
#include "VocalChops.h"
#include "VocalBus.h"

class SolveEtCoagulaProcessor : public suite::SuiteProcessor
{
public:
    SolveEtCoagulaProcessor();
    ~SolveEtCoagulaProcessor() override = default;

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

    static inline const juce::String depthId { "depth" }, sensitivityId { "sensitivity" }, focusId { "focus" },
                                     speedId { "speed" }, mixId { "mix" },
                                     chopsOnId { "chops_on" }, chopsWhereId { "chops_where" }, chopsPatternId { "chops_pattern" },
                                     chopsLevelId { "chops_level" }, chopsOctaveId { "chops_octave" };
    enum ChopsWhere { inBuilds, inDrops, inBuildsAndDrops, inBreakdowns };

    /** Vocal chops (from the sidechain vocal), for the UI and tests. */
    VocalChops chops;
    std::atomic<bool> chopsReady { false }, chopsPlaying { false };

    /** Sensitivity 0..100% maps to the vocal level where cutting starts (-30 .. -60 dB). */
    static float thresholdFor (float sensitivityPercent) { return -30.0f - 0.3f * sensitivityPercent; }

    Unmasker unmasker;
    SuiteStatus suiteStatus() override
    {
        SuiteStatus s;
        s.flags = suite::Status::sidechainWanted | (sidechainConnected.load() ? suite::Status::sidechainConnected : 0u);
        return s;
    }
    suite::SceneEngine scenes;
    /** New song (Ouroboros): section scenes on, with the suggested offsets. */
    bool applySession (const suite::SessionStyle&, int, int) override
    {
        setParameterPlain (apvts, suite::SceneEngine::enabledId, 1.0f);
        scenes.applySuggested();
        return true;
    }
    enum SceneSlot { sDepth, sSensitivity, sFocus, sMix };
    std::atomic<bool> sidechainConnected { false }, vocalPresent { false };
    juce::AudioProcessorValueTreeState apvts;

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    double sampleRate = 48000.0;
    suite::SectionFollower sections;
    bool inChopSection = false;
    int vocalBar = -1, vocalBlocks = 0, barBlocks = 0;   // for the VocalBus: which bars the vocal sings in

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolveEtCoagulaProcessor)
};
