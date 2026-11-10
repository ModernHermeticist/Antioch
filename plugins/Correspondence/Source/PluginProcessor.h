#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Parameters.h"
#include "PreviewSynth.h"
#include "Glide.h"

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
        const auto& mood = sessionMood();
        applyPreset (mood.progression >= 0 ? mood.progression : style.progression);
        setParameterPlain (apvts, Params::rootNote, (float) juce::jlimit (0, 11, root));
        setParameterPlain (apvts, Params::scale, (float) scale);   // the chosen key wins over the preset's scale
        setParameterPlain (apvts, Params::broadcast, 1.0f);
        return true;
    }

    /** Learns a melody motif from the first two bars of a MIDI file (message thread). Returns the note count. */
    /** Export all: a song folder inside `parent` with every clip, plus "<song> - all parts.mid"
        (song parts on separate tracks, sections as markers) when Ouroboros has a map. Returns the folder. */
    juce::File exportEverything (const juce::File& parent) const;
    /** Glide and scoops for exported leads, from the Lead expression setting. */
    chords::LeadExpression leadExpression() const
    {
        const auto i = (int) apvts.getRawParameterValue (Params::leadExpression)->load();
        chords::LeadExpression x;
        x.glide = i >= 1;
        x.scoops = i >= 2;
        x.bendRange = i == 3 ? 12 : 2;
        return x;
    }

    int learnMotif (const juce::File&);

    /** Reads every note of a MIDI file (all tracks), in quarter notes. */
    static std::vector<chords::Note> readMidiNotes (const juce::File&);
    /** Dropped MIDI: several clips teach Melodia your melodic DNA; one chord clip becomes the
        progression (and key); one melodic clip becomes the motif. Returns what happened, for the UI. */
    juce::String learnFromFiles (const juce::StringArray& paths);
    /** Sets key, chords and chord length from a detected progression. */
    void applyProgression (const chords::ProgressionGuess&);
    void forgetDna() { const juce::ScopedLock sl (motifLock); dna = {}; }
    chords::MelodyDna getDna() const { const juce::ScopedLock sl (motifLock); return dna; }

    /** Song info for exports (stored with the project). */
    juce::String getSongTitle() const { return apvts.state["songTitle"].toString(); }
    juce::String getArtist() const { return apvts.state["artist"].toString(); }
    void setSongInfo (const juce::String& title, const juce::String& artist)
    {
        apvts.state.setProperty ("songTitle", title, nullptr);
        apvts.state.setProperty ("artist", artist, nullptr);
    }
    /** Shares the groove with the suite (Kybalion's gate swings with it). */
    SuiteStatus suiteStatus() override
    {
        SuiteStatus st;
        st.metrics[0] = Params::grooveSwing ((int) apvts.getRawParameterValue (Params::groove)->load());
        return st;
    }
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
    chords::MelodyDna dna;
    uint32_t lastSuggestion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CorrespondenceProcessor)
};
