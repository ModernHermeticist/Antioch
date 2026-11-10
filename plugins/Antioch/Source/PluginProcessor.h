#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Engines.h"
#include "KeyBus.h"
#include "SectionBus.h"
#include "Scenes.h"
#include "Parameters.h"

class AntiochProcessor : public suite::SuiteProcessor
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
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getPluginState (juce::MemoryBlock& destData) override;
    void setPluginState (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    suite::SceneEngine scenes;
    /** New song (Ouroboros): section scenes on, with the suggested offsets. */
    bool applySession (const suite::SessionStyle&, int, int) override
    {
        setParameterPlain (apvts, suite::SceneEngine::enabledId, 1.0f);
        scenes.applySuggested();
        return true;
    }

    /** Scene parameter slots (indices into `scenes`). */
    enum SceneSlot { sDry, sEcho, sVerb, sThrow, sDecay, sShimmer, sHarmonics, sEchoDamp };

    // Read by the UI.
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<int> effectiveRoot { 0 };      // root actually used (local or followed)
    std::atomic<bool> followingBus { false };  // true when the root comes from Correspondence
    std::atomic<int> busScale { 0 };
    std::atomic<float> throwGate { 0.0f };     // 0..1, lights the THROW button
    std::atomic<float> duckAmount { 0.0f };    // current reverb gain reduction 0..1
    std::atomic<bool> ouroborosFound { false };

private:
    juce::AudioParameterBool* boolParam (const juce::String& id);
    juce::AudioParameterChoice* choiceParam (const juce::String& id);
    juce::AudioParameterInt* intParam (const juce::String& id);
    float value (const juce::String& id) const;

    struct TapPtrs
    {
        juce::AudioParameterBool* on = nullptr;
        juce::AudioParameterChoice* division = nullptr;
        juce::AudioParameterInt* harmonic = nullptr;
        std::atomic<float>* timeMs = nullptr;
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* feedback = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
    };
    std::array<TapPtrs, Params::numTaps> tapPtrs;

    HarmonicEcho echo;
    EuphoricReverb reverb;
    ThrowDelay throwDelay;
    suite::EnvelopeFollower dryEnvelope;
    suite::SectionFollower sections;

    juce::SmoothedValue<float> dryGain, echoGain, verbGain, throwGain, outGain, echoSend, throwSend, verbDuck, throwDuck;
    double sampleRate = 44100.0;
    bool firstBlock = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AntiochProcessor)
};
