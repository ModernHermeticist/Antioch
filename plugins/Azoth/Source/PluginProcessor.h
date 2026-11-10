#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Voices.h"
#include "SectionBus.h"
#include "HostInfo.h"

class AzothProcessor : public juce::AudioProcessor
{
public:
    AzothProcessor();
    ~AzothProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Parameters
    static inline const juce::String followId { "follow" }, levelId { "level" }, toneId { "tone" },
        impactId { "impact" }, crashId { "crash" }, swellId { "swell" }, downId { "downlifter" }, rollId { "roll" },
        swellLenId { "swelllen" }, rollStartId { "rollstart" }, rollPitchId { "rollpitch" }, gapId { "gap" }, grooveCrashId { "groovecrash" };
    static double swellQuarters (int i) { static const double q[] = { 1.0, 2.0, 4.0, 8.0 }; return q[juce::jlimit (0, 3, i)]; }
    static double gapQuarters (int i)   { static const double q[] = { 0.0, 0.5, 1.0 }; return q[juce::jlimit (0, 2, i)]; }

    enum Audition { aImpact = 1, aCrash = 2, aDown = 4, aSnare = 8 };
    void audition (int which) { pendingAudition.fetch_or (which); }

    juce::AudioProcessorValueTreeState apvts;

    // For the UI and tests
    std::atomic<int> eventsFired { 0 }, snareHits { 0 };
    std::atomic<bool> mapAvailable { false };
    std::atomic<int> nextType { -1 };
    std::atomic<float> barsToNext { 0.0f };

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void enterSection (suite::SectionType type, suite::SectionType previous, double bpm, double barQ, float rootHz);

    suite::SectionFollower sections;
    azoth::Impact impact;
    azoth::Crash crash;
    azoth::Downlifter downlifter;
    std::array<azoth::Snare, 3> snares;
    int nextSnare = 0;
    suite::Noise swellNoise { 0x51E11u };
    suite::Svf swellFilter;

    int previousIndex = -1;
    suite::SectionType previousType = suite::SectionType::Intro;
    double previousPos = 0.0;
    double rollPos = -1.0e9;   // previous sample position, for snare grid crossings
    std::atomic<int> pendingAudition { 0 };
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AzothProcessor)
};
