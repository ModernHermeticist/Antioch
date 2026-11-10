#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Arrangement.h"
#include "Listen.h"
#include "SuiteDSP.h"

class OuroborosProcessor : public suite::SuiteProcessor, private juce::Timer
{
public:
    OuroborosProcessor();
    ~OuroborosProcessor() override;

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

    /** Message thread: read/replace the arrangement (published to the suite immediately). */
    suite::SectionMap getMap() const;
    SuiteStatus suiteStatus() override
    {
        SuiteStatus s;
        if (getMap().count > 0) s.flags |= suite::Status::hasMap;
        return s;
    }
    void setMap (const suite::SectionMap&);

    /** Starts a section of `type` at the playhead's bar, snapped to the Snap setting. */
    void markAtPlayhead (suite::SectionType type);

    /** Publishes (or withdraws) the map. Called on a timer and after every edit. */
    void publish();

    // ---- Listen: measure the mix bar by bar while playing, then propose a map ----
    std::atomic<bool> listening { false };
    void clearListening() { resetFeatures.store (true); }
    std::vector<listen::BarFeature> features() const;
    int barsHeard() const;
    suite::SectionMap proposeFromListening() const { return listen::propose (features()); }

    juce::AudioProcessorValueTreeState apvts;

    // Transport, written by the audio thread, read by the UI.
    std::atomic<double> ppq { 0.0 }, bpm { 138.0 }, barQuarters { 4.0 };
    std::atomic<bool> playing { false };

    static inline const juce::String broadcastId { "broadcast" };
    static inline const juce::String snapId { "snap" };
    static int snapBars (int index) { static const int s[] = { 1, 4, 8, 16 }; return s[juce::jlimit (0, 3, index)]; }

private:
    void timerCallback() override { publish(); }

    static constexpr int maxListenBars = 1024;
    struct BarData
    {
        std::atomic<float> flux { 0.0f }, high { 0.0f }, full { 0.0f };
        std::atomic<int> samples { 0 };
    };
    std::array<BarData, maxListenBars> barData;
    std::atomic<bool> resetFeatures { false };
    suite::Svf lowBand, highBand;
    float lowEnv = 0.0f, envAttack = 0.1f, envRelease = 0.001f;
    double sampleRate = 44100.0;

    mutable juce::CriticalSection lock;
    suite::SectionMap map;
    bool wasBroadcasting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OuroborosProcessor)
};
