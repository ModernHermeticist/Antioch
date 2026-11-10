#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SuiteDSP.h"
#include "Spectrum.h"
#include "Scenes.h"

class VibrationProcessor : public suite::SuiteProcessor
{
public:
    VibrationProcessor();
    ~VibrationProcessor() override = default;

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

    void applyPreset (int index);

    /** Settings after the Euphoria macro is added (what is actually applied). */
    struct Effective { float air, presence, mud, drive, mbDepth, width; };
    Effective effective() const;

    juce::AudioProcessorValueTreeState apvts;
    suite::SceneEngine scenes;
    enum SceneSlot { sEuphoria, sAir, sDrive, sMultiband, sWidth };
    suite::AudioTap inputTap, outputTap;
    std::atomic<float> autoGainDb { 0.0f };

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    struct Band
    {
        suite::EnvelopeFollower env;
        float attackMs, releaseMs;
    };

    // EQ, per channel
    std::array<suite::Svf, 2> lowCutFilters;
    std::array<suite::Biquad, 2> mudEq, presenceEq, airEq;

    // Saturation (2x oversampled so the drive doesn't alias)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // Multiband: per channel, split at 250 Hz then 3 kHz; the low band gets the
    // 3 kHz allpass so all three bands stay phase-aligned when summed.
    std::array<suite::Crossover, 2> splitLow, splitHigh, alignLow;
    std::array<Band, 3> bands {};

    // Width: side signal split so everything below "Mono Below" is mono.
    suite::Crossover sideSplit, midAlign;

    // Auto gain
    float inPower = 0.0f, outPower = 0.0f, powerCoeff = 0.001f, autoGain = 1.0f, autoGainCoeff = 0.0005f;
    juce::SmoothedValue<float> outGain;
    std::vector<float> monoScratch;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VibrationProcessor)
};
