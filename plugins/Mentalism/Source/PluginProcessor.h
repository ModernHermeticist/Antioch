#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Advisor.h"
#include "Parameters.h"
#include "Spectrum.h"
#include <thread>

/** The analysed shape and loudness of a reference track. */
struct ReferenceProfile
{
    juce::String name;
    float lufs = -100.0f, truePeakDb = -100.0f;
    advisor::Bands bands {};
    bool valid = false;

    /** Analyses an audio file (WAV/AIFF/FLAC/Ogg/MP3). Safe to call on any thread. */
    static ReferenceProfile analyse (const juce::File&, const std::atomic<bool>* cancel = nullptr);
};

class MentalismProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    MentalismProcessor();
    ~MentalismProcessor() override;

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

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Clears integrated loudness, true peak and long-term balance (applied on the next block). */
    void resetMeasurements() { resetRequested.store (true); }

    /** Starts analysing a reference track in the background. */
    void loadReference (const juce::File&);
    bool isLoadingReference() const { return loading.load(); }
    ReferenceProfile getReference() const;
    void setReference (const ReferenceProfile&);

    /** Everything the UI shows: current numbers, chosen targets and advice. */
    struct Report
    {
        advisor::Measurement measurement;
        float momentary, shortTerm, targetLufs, ceiling;
        juce::String targetName;
        advisor::Bands shortDiff {}, longDiff {};
        bool tonalAvailable = false;     // false when "Reference" is chosen but none is loaded
        float measuredSeconds = 0.0f;
        juce::StringArray advice;
    };
    Report report() const;

    juce::AudioProcessorValueTreeState apvts;
    suite::AudioTap spectrumTap;

    analysis::LoudnessMeter loudness;
    analysis::TruePeakMeter truePeak;
    analysis::StereoMeter stereo;
    analysis::BandAnalyzer bands;

private:
    void timerCallback() override;

    std::atomic<bool> resetRequested { false }, loading { false }, cancelLoad { false };
    std::thread loader;
    mutable juce::CriticalSection referenceLock;
    ReferenceProfile reference;
    std::vector<float> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalismProcessor)
};
