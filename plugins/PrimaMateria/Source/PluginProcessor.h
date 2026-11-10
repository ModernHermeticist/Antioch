#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "FixBus.h"
#include "Params.h"
#include "SuiteDSP.h"
#include "HostInfo.h"
#include "Analysis.h"
#include "Registry.h"
#include <optional>

class PrimaMateriaProcessor : public suite::SuiteProcessor
{
public:
    PrimaMateriaProcessor();
    ~PrimaMateriaProcessor() override = default;

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

    void applyRole (int index);
    int getRole() const { return (int) apvts.getRawParameterValue (pm::roleId)->load() - 1; }   // -1 = not set

    /** This track's loudness (LUFS, gated), measured in Drop sections (or all playback without a map). */
    float measuredLufs() const { return loudness.integrated(); }
    SuiteStatus suiteStatus() override;
    void trackNameChanged (const juce::String&) override;
    /** Rough mix: the trim that puts this track at its role's level relative to the kick (nullopt = no change). */
    std::optional<float> roughMixTrim() const;

    /** Low-end manager: section types (bits; bit 6 = Drop 2+) where a kick or bass track plays,
        from the suite registry (updated once a second). */
    std::atomic<uint32_t> lowOwners { 0 };
    std::atomic<float> managerCutNow { 0.0f };   // the cut the manager is applying now (0 = none), for the UI
    /** Which section types this track sounds in (bits; bit 6 = Drop 2+). */
    uint32_t soundingMask() const;
    void refreshLowOwners();

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<suite::FixReceiver> fixes;   // Mentalism's fix-it buttons

    // For the UI
    std::atomic<int> currentType { -1 };
    std::atomic<bool> ramping { false }, gapping { false }, mapAvailable { false };
    std::atomic<float> nowLevelDb { 0.0f }, nowLowCut { pm::lowCutOff }, nowHighCut { pm::highCutOff };

private:
    pm::Setting settingFor (int type) const;

    suite::SectionFollower sections;
    analysis::LoudnessMeter loudness;
    analysis::BandAnalyzer spectrum;   // the track's spectrum in the drops, for the masking map
    std::array<suite::Biquad, 2> carve;
    int carveBandNow = -1; float carveDepthNow = 0.0f;
    juce::SmoothedValue<float> trimGain;
    float lastTrim = 0.0f;
    std::array<suite::Svf, 2> lowCut, highCut;
    float levelDb = 0.0f, lowLog = 0.0f, highLog = 0.0f, gapGain = 1.0f;
    double sampleRate = 48000.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PrimaMateriaProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrimaMateriaProcessor)
};
