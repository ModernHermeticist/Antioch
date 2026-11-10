#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Parameters.h"
#include "PreviewSynth.h"

class CorrespondenceProcessor : public suite::SuiteProcessor, private juce::Timer
{
public:
    CorrespondenceProcessor();
    ~CorrespondenceProcessor() override;

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
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getPluginState (juce::MemoryBlock& destData) override;
    void setPluginState (const void* data, int sizeInBytes) override;

    /** Publishes (or withdraws) the key on the suite KeyBus. Called on a timer. */
    void publishKey();

    /** Loads a preset into the parameters (message thread). */
    void applyPreset (int index);
    /** New song (Ouroboros): the key, a progression for the style, and the key broadcast on. */
    bool applySession (const suite::SessionStyle& style, int root, int scale) override
    {
        applyPreset (style.progression);
        setParameterPlain (apvts, Params::rootNote, (float) juce::jlimit (0, 11, root));
        setParameterPlain (apvts, Params::scale, (float) scale);   // the chosen key wins over the preset's scale
        setParameterPlain (apvts, Params::broadcast, 1.0f);
        return true;
    }

    /** Learns a melody motif from the first two bars of a MIDI file (message thread). Returns the note count. */
    /** Export all: a song folder inside `parent` with every clip, plus "<song> - all parts.mid"
        (song parts on separate tracks, sections as markers) when Ouroboros has a map. Returns the folder. */
    juce::File exportEverything (const juce::File& parent) const;

    int learnMotif (const juce::File&);
    void forgetMotif();
    std::vector<chords::Note> getMotif() const;
    chords::MelodySettings melodySettings() const;   // parameters + learned motif

    /** Writes the chords/bass/arp clip to a .mid file and returns it. */
    juce::File exportClip (const juce::String& kind, const juce::File& directory) const;

    juce::AudioProcessorValueTreeState apvts;
    PreviewSynth preview;
    std::atomic<double> currentBpm { 138.0 };
    std::atomic<bool> adoptedDetectedKey { false };   // the current key came from Emerald Tablet

private:
    void timerCallback() override { publishKey(); }

    bool wasBroadcasting = false;
    mutable juce::CriticalSection motifLock;
    std::vector<chords::Note> motif;
    uint32_t lastSuggestion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CorrespondenceProcessor)
};
