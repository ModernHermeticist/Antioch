#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
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
    enum SceneSlot { sGateDepth, sGateLength, sGateFilter, sSwing, sDuckDepth };

    // Read by the UI.
    std::atomic<double> currentBpm { 138.0 };
    std::atomic<int> gateStep { 0 };
    std::atomic<float> duckGain { 1.0f };
    std::atomic<float> duckMs { 1.0e9f };      // time since the last duck trigger
    std::atomic<bool> sidechainConnected { false };
    std::atomic<int> currentSection { -1 };      // SectionType from Ouroboros, -1 if none
    std::atomic<bool> autoBreakdown { false };   // ducking paused because of the section

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KybalionProcessor)
};
