#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "KeyDetector.h"
#include "KeyBus.h"

class EmeraldTabletProcessor : public suite::SuiteProcessor, private juce::Timer
{
public:
    EmeraldTabletProcessor();
    ~EmeraldTabletProcessor() override;

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

    static inline const juce::String autoSendId { "autosend" }, broadcastAloneId { "broadcastalone" };
    static constexpr float minSeconds = 12.0f, stableSeconds = 8.0f, minConfidence = 0.5f;

    /** Message thread (called on a timer): sends a stable, confident key to the suite. */
    void update();
    void sendNow();
    void resetDetection() { resetRequested.store (true); }

    static int scaleIndexFor (bool minor) { return minor ? 0 : 1; }   // suite scales: 0 = Minor, 1 = Major

    KeyDetector detector;
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> lastSent { -1 };
    std::atomic<bool> broadcastingAlone { false };

private:
    void timerCallback() override { update(); }

    std::atomic<bool> resetRequested { false };
    int stableCandidate = -1;
    float stableSince = 0.0f;
    std::vector<float> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmeraldTabletProcessor)
};
