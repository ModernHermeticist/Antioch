#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Limiter.h"
#include "Analysis.h"
#include "TargetBus.h"

class LapisProcessor : public suite::SuiteProcessor
{
public:
    LapisProcessor();
    ~LapisProcessor() override = default;

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

    // Parameters
    static inline const juce::String targetId { "target" }, modeId { "mode" }, ceilingId { "ceiling" }, clipId { "clip" },
                                     releaseId { "release" }, manualGainId { "manualgain" };
    enum Mode { Auto = 0, Hold, Manual };
    static constexpr int followMentalism = 0;
    static float targetLufsFor (int index) { static const float t[] = { -8.0f, -8.0f, -6.0f, -14.0f }; return t[juce::jlimit (0, 3, index)]; }
    static float ceilingFor (int index) { static const float c[] = { -0.3f, -0.3f, -0.3f, -1.0f }; return c[juce::jlimit (0, 3, index)]; }

    /** Loud sections are aimed this far above the integrated target: breakdowns pull the
        song's integrated loudness down by roughly this much in typical trance arrangements. */
    static constexpr float loudSectionOffset = 1.0f;

    void resetLearning() { resetRequested.store (true); }

    // For the UI
    std::atomic<float> gainDb { 0.0f }, gainReductionDb { 0.0f }, maxReductionDb { 0.0f };
    std::atomic<float> outShortTerm { -100.0f }, effectiveTarget { -8.0f }, effectiveCeiling { -0.3f };
    std::atomic<bool> followingMentalism { false }, limitedByReduction { false }, settled { false };
    analysis::TruePeakMeter outputPeak;

    juce::AudioProcessorValueTreeState apvts;

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    LookaheadLimiter limiter;
    analysis::LoudnessMeter loudness;
    std::atomic<float> learnedGainDb { 0.0f };
    std::atomic<bool> resetRequested { false };
    float runningMax = -100.0f, appliedGainDb = 0.0f;
    std::array<float, 10> stHistory { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
    int stHistoryPos = 0;   // short-term loudness over the last second (100 ms steps)
    int samplesSinceUpdate = 0, updateInterval = 4800, settledUpdates = 0;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LapisProcessor)
};
