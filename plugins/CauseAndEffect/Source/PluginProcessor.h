#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FixBus.h"
#include "BuildClock.h"
#include "Parameters.h"
#include "SuiteDSP.h"
#include "SectionBus.h"

class CauseEffectProcessor : public juce::AudioProcessor
{
public:
    CauseEffectProcessor();
    ~CauseEffectProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 15.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<suite::FixReceiver> fixes;   // Mentalism's fix-it buttons

    // Read by the UI.
    std::atomic<float> buildLevel { 0.0f };    // curved build amount actually applied
    std::atomic<double> progress { -1.0 };     // timed build progress, -1 if none
    std::atomic<double> lengthBars { 16.0 };
    std::atomic<int> dropCount { 0 };          // increments at every drop
    std::atomic<int> effectiveRoot { 9 };
    std::atomic<bool> followingBus { false };
    std::atomic<int> busScale { 0 };
    std::atomic<bool> sectionBuild { false };   // the current build comes from an Ouroboros Build section
    std::atomic<bool> ouroborosFound { false };

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    int choice (const juce::String& id) const { return (int) value (id); }

    BuildClock clock;
    suite::SectionFollower sections;
    bool inSectionBuild = false;
    double sectionBuildEndQ = 0.0;
    suite::Svf hpL, hpR, noiseBpL, noiseBpR, riserLpL, riserLpR, washHpL, washHpR;
    suite::FdnReverb washVerb;
    suite::Noise noiseL { 0x1234567u }, noiseR { 0x7654321u };
    std::array<suite::SawOsc, 4> saws;

    juce::SmoothedValue<float> manual, outGain;
    float build = 0.0f, buildSmoothing = 0.01f;
    float stutterGain = 1.0f, stutterSmoothing = 0.01f;
    float tailGain = 1.0f, tailStep = 0.001f;
    bool tailFading = false;
    bool manualArmed = false;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CauseEffectProcessor)
};
