#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Arrangement.h"

class OuroborosProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    OuroborosProcessor();
    ~OuroborosProcessor() override;

    void prepareToPlay (double, int) override {}
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

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Message thread: read/replace the arrangement (published to the suite immediately). */
    suite::SectionMap getMap() const;
    void setMap (const suite::SectionMap&);

    /** Starts a section of `type` at the playhead's bar, snapped to the Snap setting. */
    void markAtPlayhead (suite::SectionType type);

    /** Publishes (or withdraws) the map. Called on a timer and after every edit. */
    void publish();

    juce::AudioProcessorValueTreeState apvts;

    // Transport, written by the audio thread, read by the UI.
    std::atomic<double> ppq { 0.0 }, bpm { 138.0 }, barQuarters { 4.0 };
    std::atomic<bool> playing { false };

    static inline const juce::String broadcastId { "broadcast" };
    static inline const juce::String snapId { "snap" };
    static int snapBars (int index) { static const int s[] = { 1, 4, 8, 16 }; return s[juce::jlimit (0, 3, index)]; }

private:
    void timerCallback() override { publish(); }

    mutable juce::CriticalSection lock;
    suite::SectionMap map;
    bool wasBroadcasting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OuroborosProcessor)
};
