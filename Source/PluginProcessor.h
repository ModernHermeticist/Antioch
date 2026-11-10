#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "DSP.h"
#include "Parameters.h"

class AntiochProcessor : public juce::AudioProcessor
{
public:
    AntiochProcessor();
    ~AntiochProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Host tempo as last seen by the audio thread (for the UI). */
    std::atomic<double> currentBpm { 120.0 };

private:
    struct TapParams
    {
        juce::AudioParameterBool* on = nullptr;
        juce::AudioParameterChoice* division = nullptr;
        std::atomic<float>* timeMs = nullptr;
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* feedback = nullptr;
        juce::AudioParameterInt* harmonic = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
    };

    struct Tap
    {
        dsp::DelayLine delay;
        dsp::PitchShifter shifter;
        dsp::CombResonator comb;
        dsp::OnePole damp, lowCut;
        float delaySamples = 0.0f;
        juce::SmoothedValue<float> gainL, gainR, feedback, enabled;
    };

    float targetDelaySamples (int tapIndex, bool synced, double bpm) const;

    std::array<TapParams, Params::numTaps> tapParams;
    std::array<Tap, Params::numTaps> taps;

    juce::AudioParameterBool* syncParam = nullptr;
    juce::AudioParameterChoice* rootParam = nullptr;
    juce::AudioParameterInt* octaveParam = nullptr;
    std::atomic<float>* resonanceParam = nullptr;
    std::atomic<float>* resMixParam = nullptr;
    std::atomic<float>* dampParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;

    juce::SmoothedValue<float> dryGain, wetGain, resMix;
    double sampleRate = 44100.0;
    bool firstBlock = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AntiochProcessor)
};
