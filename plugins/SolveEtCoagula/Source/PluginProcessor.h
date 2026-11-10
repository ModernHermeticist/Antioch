#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Unmasker.h"
#include "Scenes.h"

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
                                     speedId { "speed" }, mixId { "mix" };

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
    enum SceneSlot { sDepth, sSensitivity, sFocus, sMix };
    std::atomic<bool> sidechainConnected { false }, vocalPresent { false };
    juce::AudioProcessorValueTreeState apvts;

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolveEtCoagulaProcessor)
};
