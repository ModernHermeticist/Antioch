#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Unmasker.h"
#include "Scenes.h"
#include "VocalChops.h"
#include "VocalMelody.h"
#include "Alias.h"
#include "WritingBus.h"
#include "KeyBus.h"
#include "VocalBus.h"
#include "SuiteDSP.h"
#include "Registry.h"

class SolveEtCoagulaProcessor : public suite::SuiteProcessor
{
public:
    SolveEtCoagulaProcessor();
    ~SolveEtCoagulaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processSuiteBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&);
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

    static inline const juce::String depthId { "depth" }, sensitivityId { "sensitivity" }, focusId { "focus" },
                                     speedId { "speed" }, mixId { "mix" },
                                     chopsOnId { "chops_on" }, chopsWhereId { "chops_where" }, chopsPatternId { "chops_pattern" },
                                     chopsLevelId { "chops_level" }, chopsOctaveId { "chops_octave" };
    enum ChopsWhere { inBuilds, inDrops, inBuildsAndDrops, inBreakdowns };

    /** Vocal chops (from the sidechain vocal), for the UI and tests. */
    VocalChops chops;
    std::atomic<bool> chopsReady { false }, chopsPlaying { false };

    /** Sensitivity 0..100% maps to the vocal level where cutting starts (-30 .. -60 dB). */
    static float thresholdFor (float sensitivityPercent) { return -30.0f - 0.3f * sensitivityPercent; }

    Unmasker unmasker;
    SuiteStatus suiteStatus() override
    {
        SuiteStatus s;
        s.flags = suite::Status::sidechainWanted | (sidechainConnected.load() ? suite::Status::sidechainConnected : 0u);
        if (! sidechainConnected.load() && apvts.getRawParameterValue (chopsOnId)->load() < 0.5f) s.flags |= suite::Status::idle;   // nothing to do
        if (vocalBuried.load()) s.flags |= suite::Status::vocalBuried;
        return s;
    }
    suite::SceneEngine scenes;
    /** Learning from your tweaks: a knob moved by hand in a section is offered as that section's scene. */
    void pollTick() override
    {
        scenes.watchTweaks (isMorphing());
        collectMelody();
        float swing = 0.0f;
        for (const auto& e : suite::Registry::list())
            if (e.plugin == "Correspondence") swing = std::max (swing, e.metrics[0]);
        grooveSwing.store (swing);
    }

    /** Vocal melody to MIDI: what the sidechain vocal sings, as notes (message thread). */
    VocalMelody melody;
    std::atomic<float> grooveSwing { 0.0f };   // Correspondence's groove: the chops swing with it
    std::atomic<double> songBpm { 120.0 };
    void collectMelody()
    {
        const auto key = suite::KeyBus::read();
        melody.collect (key.valid ? key.root : -1, key.valid ? key.scale : 0);
        // ...and tell the suite what the vocal sings, so Correspondence writes around it
        if (melody.notes().size() != publishedNotes)
        {
            std::vector<suite::WritingBus::SungNote> sung;
            for (const auto& n : melody.notes()) sung.push_back ({ n.pitch, n.start, n.length });
            suite::WritingBus::publishSung (sung);
            publishedNotes = melody.notes().size();
        }
    }
    size_t publishedNotes = 0;
    /** Writes "Vocal melody.mid" and (with a key) "Vocal harmony.mid", a third above, into `folder`. */
    juce::String saveVocalMidi (const juce::File& folder)
    {
        collectMelody();
        const auto& notes = melody.notes();
        if (notes.empty()) return "Nothing heard yet: play the song with the vocal on the sidechain.";
        folder.createDirectory();
        auto write = [&] (const std::vector<VocalMelody::Note>& n, const juce::String& name)
        {
            const auto file = folder.getChildFile (name + ".mid");
            file.deleteFile();
            juce::FileOutputStream out (file);
            return out.openedOk() && VocalMelody::toMidi (n, songBpm.load(), name).writeTo (out);
        };
        const auto key = suite::KeyBus::read();
        const auto harmony = VocalMelody::harmony (notes, key.valid ? key.root : -1, key.valid ? key.scale : 0);
        auto ok = write (notes, "Vocal melody");
        if (! harmony.empty()) ok = write (harmony, "Vocal harmony") && ok;
        ok = write (VocalMelody::doubles (notes, songBpm.load()), "Vocal double (guide)") && ok;   // 8
        const auto below = VocalMelody::harmonyBelow (notes, key.valid ? key.root : -1, key.valid ? key.scale : 0);
        if (! below.empty()) ok = write (below, "Vocal harmony below") && ok;
        return ok ? juce::String ((int) notes.size()) + " notes written to " + folder.getFullPathName()
                        + (harmony.empty() ? juce::String (" (no key set: no harmony)") : juce::String (", with harmonies a third above and a sixth below"))
                        + ", and a guide for a double (each note 10-25 ms off, like a second take)"
                  : juce::String ("Couldn't write to ") + folder.getFullPathName();
    }
    static juce::File vocalMidiFolder() { return suite::Alias::file ("Vocal MIDI"); }
    /** 7: the vocal's tuning and timing as heard so far, in words (a check: nothing is moved). */
    juce::String checkVocal()
    {
        collectMelody();
        const auto& notes = melody.notes();
        if (notes.empty()) return "Nothing heard yet: play the song with the vocal on the sidechain.";
        const auto issues = VocalMelody::check (notes, songBpm.load());
        if (issues.empty()) return juce::String ((int) notes.size()) + " notes heard: all within 25 cents and 30 ms of the grid.";
        juce::StringArray lines { juce::String ((int) issues.size()) + " of " + juce::String ((int) notes.size()) + " notes to look at (tune or nudge them in your editor):" };
        for (size_t i = 0; i < issues.size() && i < 40; ++i) lines.add (issues[i].text);
        if (issues.size() > 40) lines.add ("... and " + juce::String ((int) issues.size() - 40) + " more");
        return lines.joinIntoString ("\n");
    }
    /** New song (Ouroboros): section scenes on, with the suggested offsets. */
    bool applySession (const suite::SessionStyle&, int, int) override
    {
        setParameterPlain (apvts, suite::SceneEngine::enabledId, 1.0f);
        scenes.applySuggested();
        return true;
    }
    enum SceneSlot { sDepth, sSensitivity, sFocus, sMix };
    std::atomic<bool> sidechainConnected { false }, vocalPresent { false };
    /** Live mix check (Mentalism): in the presence range (1-4 kHz) the music has been more than
        `buriedDb` above the vocal (as it arrives on the sidechain) for 4 sung bars in a row. */
    static constexpr float buriedDb = 9.0f;
    std::atomic<bool> vocalBuried { false };
    std::atomic<int> buriedBars { 0 };
    juce::AudioProcessorValueTreeState apvts;

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    double sampleRate = 48000.0;
    suite::SectionFollower sections;
    bool inChopSection = false;
    int vocalBar = -1, vocalBlocks = 0, barBlocks = 0;   // for the VocalBus: which bars the vocal sings in
    std::array<suite::Svf, 4> presence;   // vocal HP, vocal LP, music HP, music LP: 1-4 kHz
    double vocalPresence = 0.0, musicPresence = 0.0;
    double shortVocal = 0.0, shortMusic = 0.0, shortCoeff = 0.0001;
public:
    std::atomic<float> vocalOverMusicDb { -100.0f };   // tests, UI
private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolveEtCoagulaProcessor)
};
