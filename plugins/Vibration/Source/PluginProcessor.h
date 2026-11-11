#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "SuiteDSP.h"
#include "Spectrum.h"
#include "Scenes.h"
#include "Fingerprint.h"

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
    /** Role-aware setup: the preset for the track (supersaw lead, pluck, arp, pad, vocal chop). */
    bool applyRoleSetup (int role, const juce::String& trackName) override
    {
        int preset = -1;
        if (suite::isPluckName (trackName)) preset = 1;
        else if (suite::isArpName (trackName)) preset = 4;
        else if (role == suite::roleLead) preset = 0;
        else if (role == suite::rolePads) preset = 2;
        else if (role == suite::roleVocal) preset = 3;
        if (preset < 0) return false;
        applyPreset (preset);
        return true;
    }

    /** Lead fingerprint: measured on this plugin's output (needs ~5 s of the lead playing). */
    analysis::BandAnalyzer outBands;
    bool fingerprintReady() const { return outBands.longSeconds.load() >= 5.0f; }
    fingerprint::Shape measuredShape() const;
    /** Saves this lead's fingerprint as `name` in `dir` (message thread). */
    bool saveFingerprint (const juce::File& dir, const juce::String& name);
    /** Recalls a fingerprint's character and, once the lead has been heard, moves the EQ
        towards its tone. Returns a line saying what changed. */
    juce::String matchFingerprint (const fingerprint::Fingerprint&);
    void restartFingerprintMeasure() { outBands.reset(); }

    /** Settings after the Euphoria macro is added (what is actually applied). */
    struct Effective { float air, presence, mud, drive, mbDepth, width; };
    Effective effective() const;

    juce::AudioProcessorValueTreeState apvts;
    suite::SceneEngine scenes;
    /** Learning from your tweaks: a knob moved by hand in a section is offered as that section's scene. */
    void pollTick() override { scenes.watchTweaks (isMorphing()); }
    /** New song (Ouroboros): section scenes on, with the suggested offsets. */
    bool applySession (const suite::SessionStyle&, int, int) override
    {
        setParameterPlain (apvts, suite::SceneEngine::enabledId, 1.0f);
        scenes.applySuggested();
        const auto drop = (int) suite::SectionType::Drop;
        scenes.setOffset (drop, sEuphoria, scenes.getOffset (drop, sEuphoria) + sessionMood().dropEuphoria);
        return true;
    }
    enum SceneSlot { sEuphoria, sAir, sDrive, sMultiband, sWidth };
    suite::AudioTap inputTap, outputTap;
    suite::SectionFollower chordSections;
    std::vector<float> pitchRing;
    int pitchWrite = 0;
    float pitchAverageSemis = 0.0f;
    bool pitchAverageSet = false;
    std::atomic<float> autoGainDb { 0.0f };

    /** Tension width: wider on chords that pull away from home (Correspondence's progression,
        counted from each section's start). Pitch-tracking brightness: Air and Presence rise as
        the lead goes above its usual pitch and fall below it. */
    std::atomic<float> tensionNow { 0.0f }, pitchHz { 0.0f }, pitchOffset { 0.0f };
    void updateTension (const suite::HostInfo& host);
    void trackPitch (const float* mono, int n);

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
