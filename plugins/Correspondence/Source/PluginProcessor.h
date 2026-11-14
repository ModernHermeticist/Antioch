#pragma once

#include "EmeraldTabletProcessor.h"   // merged into Correspondence: runs inside it (a module)
#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Parameters.h"
#include "PreviewSynth.h"
#include "Glide.h"
#include "Identity.h"
#include "FixBus.h"
#include "Harmony.h"
#include "Clash.h"
#include "VocalAware.h"
#include "MoveLog.h"
#include <map>
#include <optional>
#include "Alias.h"
#include "SongBus.h"
#include "LabelReadyBus.h"
#include "AutopilotBus.h"
#include "WriteTools.h"
#include "ContextPlayer.h"

class CorrespondenceProcessor : public suite::SuiteProcessor, private juce::Timer
{
public:
    CorrespondenceProcessor();
    ~CorrespondenceProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { releaseModules(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    /** Emerald Tablet, merged into Correspondence: it runs here, after Correspondence's own processing. */
    EmeraldTabletProcessor& emerald() const { return *static_cast<EmeraldTabletProcessor*> (module ("Emerald Tablet")); }

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
        // Your progression DNA: unless the mood asks for its own, the progression you use most
        // (in two songs or more) comes first.
        if (mood.progression < 0)
            if (const auto mine = identity::favouriteProgressions (suite::StyleLog::read()); ! mine.empty() && mine.front().second >= 2)
                applyNumerals (mine.front().first);
        setParameterPlain (apvts, Params::rootNote, (float) juce::jlimit (0, 11, root));
        setParameterPlain (apvts, Params::scale, (float) scale);   // the chosen key wins over the preset's scale
        setParameterPlain (apvts, Params::broadcast, 1.0f);
        // The pattern library follows the session: its style and mood, and a fresh set of families
        setParameterPlain (apvts, Params::libStyle, (float) libraryStyleFor (style));
        setParameterPlain (apvts, Params::libMood, (float) libraryMoodFor (sessionMoodIndex(), style));
        setParameterPlain (apvts, Params::libSeed, (float) juce::Random::getSystemRandom().nextInt ({ 1, 10000 }));
        return true;
    }
    /** The library style for a session style, and the mood for a session mood ("As the style": the style's own). */
    static int libraryStyleFor (const suite::SessionStyle& style)
    {
        const juce::String n (style.name);
        using S = patterns::Style;
        return (int) (n.containsIgnoreCase ("tech") ? S::Tech : n.containsIgnoreCase ("progressive") ? S::Progressive
                    : n.containsIgnoreCase ("vocal") ? S::Vocal : n.containsIgnoreCase ("psy") ? S::Psy
                    : n.containsIgnoreCase ("orchestral") ? S::Orchestral : n.containsIgnoreCase ("classic") ? S::Classic : S::Uplifting);
    }
    static int libraryMoodFor (int sessionMood, const suite::SessionStyle& style)
    {
        using M = patterns::Mood;
        switch (sessionMood)
        {
            case 1: return (int) M::Euphoric;
            case 2: return (int) M::Hopeful;
            case 3: return (int) M::Melancholic;
            case 4: return (int) M::Dark;
            default: break;
        }
        const juce::String n (style.name);
        return (int) (n.containsIgnoreCase ("tech") ? M::Driving : n.containsIgnoreCase ("progressive") ? M::Dreamy
                    : n.containsIgnoreCase ("vocal") ? M::Melancholic : n.containsIgnoreCase ("anthem") ? M::Epic : M::Euphoric);
    }

    /** Learns a melody motif from the first two bars of a MIDI file (message thread). Returns the note count. */
    /** Export all: a song folder inside `parent` with every clip, plus "<song> - all parts.mid"
        (song parts on separate tracks, sections as markers) when Ouroboros has a map. Returns the folder. */
    juce::File exportEverything (const juce::File& parent) const;
    mutable std::map<juce::String, juce::int64> lastExportHash;   // what each part was at the last export (the mix re-learns the parts that change)
    mutable std::atomic<uint32_t> lastRelearnMask { 0 };
    mutable std::atomic<int> directedChanges { 0 };   // notes the suite director's plan changed in the last clip
    mutable std::atomic<int> movedForVocal { 0 };   // notes moved off the vocal's held notes in the last clip
    /** Song pack for a collaborator (a vocalist): a folder with the song sheet, the chords,
        bass and lead guide MIDI, the section markers and a note on key, range and structure. */
    juce::File exportSongPack (const juce::File& parent) const;
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
    void forgetDna() { const juce::ScopedLock sl (motifLock); dna = {}; bassDna = {}; }
    chords::BassDna getBassDna() const { const juce::ScopedLock sl (motifLock); return bassDna; }
    /** The parameters' settings plus what was learned (your bassline). */
    chords::Settings currentSettings() const
    {
        auto s = Params::readSettings (apvts);
        s.bassDna = getBassDna();
        return s;
    }
    /** Song patterns for exports, with the vocal's bars from Solve et Coagula. */
    chords::SongPatterns currentSongPatterns() const;
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
        st.metrics[1] = (float) Params::quartersPerChord ((int) apvts.getRawParameterValue (Params::perChord)->load());   // chord length (Kybalion's gate follows the changes)
        return st;
    }
    void forgetMotif();
    /** Sets the progression from roman numerals ("i-VI-III-VII"), in the current key. */
    bool applyNumerals (const juce::String& numerals);
    /** Harmony explorer: colour ideas for the current progression; applying one keeps the
        progression before it, for Undo. */
    std::vector<harmony::Idea> harmonyIdeas() const { return harmony::ideas (currentSettings()); }
    void applySlots (const chords::Settings& s);
    bool applyHarmonyIdea (int index);
    bool undoHarmonyIdea();
    std::optional<chords::Settings> beforeIdea;

    //==============================================================================
    // The writing tools (WriteTools.h; the Write menu)
    /** Locks: bars of a clip ("Song Lead"...) kept through every regenerate (saved with the project). */
    chords::tools::Lock lockOf (const juce::String& kind) const { return chords::tools::lockFromText (apvts.state.getProperty ("lock_" + kind.replaceCharacter (' ', '_')).toString()); }
    void setLock (const juce::String& kind, const chords::tools::Lock& l)
    {
        const juce::Identifier key ("lock_" + kind.replaceCharacter (' ', '_'));
        if (l.empty()) apvts.state.removeProperty (key, nullptr); else apvts.state.setProperty (key, chords::tools::toText (l), nullptr);
    }
    void lockBars (const juce::String& kind, int fromBar, int toBar);
    void unlockBars (const juce::String& kind, int fromBar, int toBar);
    /** A clip as it would export now (with its locks). */
    std::vector<chords::Note> clipNow (const juce::String& kind) const;
    /** Regenerate: new seeds (the closest of eight to your taste once it knows you), the locked bars
        kept; `onlyThis` locks every other song part first. Each regenerate is a take. */
    juce::String regenerate (const juce::String& kind, bool onlyThis);
    mutable std::map<juce::String, chords::tools::TakeHistory> takes;
    bool takeBack (const juce::String& kind);
    bool takeForward (const juce::String& kind);
    void applyTake (const juce::String& kind, const chords::tools::Take& t);
    bool keepBarsFrom (const juce::String& kind, int takeIndex, int fromBar, int toBar);
    void rateTake (const juce::String& kind, bool liked);
    chords::tools::Taste taste;
    static juce::File& tasteFileOverride() { static juce::File f; return f; }   // tests
    static juce::File tasteFile();
    void loadTaste();
    void saveTaste() const;
    /** Hear it in context: the part played in time with FL under the song (empty kind: off). */
    chords::ContextPlayer contextPlayer;
    juce::String contextKind;
    void playInContext (const juce::String& kind);
    /** Switches saved with the project: one motif for the whole song, the shared rhythm, the reference's feel. */
    bool option (const char* key) const { return (bool) apvts.state.getProperty (key, false); }
    void setOption (const char* key, bool on) { apvts.state.setProperty (key, on, nullptr); }
    /** The hook the tools work on: the motif (learned, hummed, from the bank) or the first drop's first two bars. */
    std::vector<chords::Note> currentHook() const;
    chords::tools::HookReport hookReport() const { return chords::tools::hookDoctor (currentHook(), currentSettings()); }
    bool applyHookEdit (const chords::tools::HookEdit& e);
    /** Hum a hook: listen to this track's input; finishing turns it into the motif. */
    chords::HumCapture hum;
    std::vector<float> humAudio;
    juce::String humNote;
    void startHum();
    juce::String finishHum();
    /** Topline from lyrics: three candidates; one saved as MIDI with its lyrics. */
    std::vector<chords::tools::Topline> toplines (const juce::String& lyric, int bars) const;
    juce::File exportTopline (const chords::tools::Topline& t, const juce::File& dir, const juce::String& name) const;
    /** Originality: the hook against your hook bank and the library's leads. */
    std::vector<chords::tools::NearRepeat> originality() const;
    /** The emotional arc and the trance harmony moves (Undo: the variation and harmony Undos). */
    bool applyArc (const chords::tools::ArcPlan& p);
    bool applyMove (const chords::tools::Move& m);
    /** Feel from a reference: analyse an audio file; on from then (saved). */
    juce::String loadFeel (const juce::File& audio);
    chords::tools::Feel feel() const;
    /** Variation engine: alternatives for a kind of section; applying one keeps what was there, for Undo. */
    std::vector<harmony::Variation> sectionVariations (suite::SectionType type) const { return harmony::variations (currentSongPatterns(), type); }
    bool applyVariation (suite::SectionType type, int index);
    /** The song's parts for Ouroboros' label-ready checks: the hook (the first drop's lead, 8 bars),
        the bass, the counter-line under the hook, the chord roots per kind of section, the key. */
    suite::SongBus::Song songForLabel (const suite::SectionMap& map, const chords::SongPatterns& sp) const;
    suite::SongBus::Song songForLabel (const suite::SectionMap& map) const { return songForLabel (map, currentSongPatterns()); }
    /** Write the hook stronger: new Melodia hooks for the drop (other seeds and rhythms) scored as
        hooks (SongBus::hookScore), the best three that beat today's; `current` is today's score.
        Applying one sets the melody's seed and style (and Melodia over the library's hook); Undo
        (the variation Undo) puts them back. */
    struct StrongerHook { juce::String name; float score = 0.0f; int seed = 7, style = 1; bool libLead = false; };
    std::vector<StrongerHook> strongerHooks (float& current) const;
    bool applyStrongerHook (const StrongerHook&);
    /** Write for the label: options for one goal, each scored the way the label-ready checks
        would see it, best first (up to three that beat today's); applying one sets its parameters
        (the variation Undo puts them back). */
    enum class LabelGoal { BreakdownEmotion, Drop2Different, CounterSpace, BassFit, IntroTeaser };
    struct LabelOption { juce::String name; float score = 0.0f; std::vector<std::pair<juce::String, float>> params; };
    std::vector<LabelOption> labelOptions (LabelGoal goal, float& current) const;
    float labelScore (LabelGoal goal, const suite::SectionMap& map, const chords::SongPatterns& sp) const;
    bool applyLabelOption (const LabelOption&);
    /** Label-ready writing fixes, latched for this song when Ouroboros' checks ask for them (and
        their autopilot switch is on); saved with the project. The part clean-ups act while latched
        and switched on; the rest set a setting once (yours to change back). */
    uint32_t labelLatched = 0;
    int labelTick = 0;
    void latchLabelWriting();
    /** The map the song is written against: Ouroboros' when it shares one, else a standard trance form. */
    static suite::SectionMap songMapOrDefault();
    /** Clash check: parts in the song clips that sit in the same register, each with a fix. */
    std::vector<clash::Clash> clashes() const;
    bool fixClash (const clash::Clash&);
    bool undoVariation();
    std::vector<std::pair<juce::String, float>> beforeVariation;
    /** Hook bank: keeps the motif (or the melody's first two bars) as a hook for every project. */
    bool saveHook (const juce::String& name);
    /** Uses a hook from the bank as Melodia's motif. */
    void useHook (const std::vector<chords::Note>& notes);
    std::vector<chords::Note> getMotif() const;
    chords::MelodySettings melodySettings() const;   // parameters + learned motif

    /** Writes the chords/bass/arp clip to a .mid file and returns it. */
    juce::File exportClip (const juce::String& kind, const juce::File& directory) const;
    /** A clip's notes, grooved and humanised as exported (`available` false: nothing to export). */
    std::vector<chords::Note> clipNotes (const juce::String& kind, std::vector<std::pair<double, bool>>& pianoPedal, bool& available) const;
    /** FL piano roll scripts for the clips (Tools > Scripting in the piano roll), in FL's scripts folder. */
    juce::Array<juce::File> sendToPianoRoll() const;

    juce::AudioProcessorValueTreeState apvts;
    PreviewSynth preview;
    std::atomic<double> currentBpm { 138.0 };
    std::atomic<bool> adoptedDetectedKey { false };   // the current key came from Emerald Tablet

    /** Song draft (Ouroboros): a moment after the new song is set up, Export all into
        <suite folder>/Drafts, so the whole song's MIDI is ready to drag in. */
    std::unique_ptr<suite::FixReceiver> fixes;
    static juce::File& draftFolderOverride() { static juce::File f; return f; }   // tests
    static juce::File draftFolder() { return draftFolderOverride() != juce::File() ? draftFolderOverride() : suite::Alias::file ("Drafts"); }
    std::atomic<double> draftAt { 0.0 };
    juce::File lastDraft;
    void exportDraftIfDue (bool now = false)
    {
        const auto at = draftAt.load();
        if (at <= 0.0 || (! now && juce::Time::getMillisecondCounterHiRes() < at)) return;
        draftAt.store (0.0);
        draftFolder().createDirectory();
        lastDraft = exportEverything (draftFolder());
    }

private:
    void timerCallback() override
    {
        { suite::Watchdog::Scope w (getName(), "key"); publishKey(); }
        { suite::Watchdog::Scope w (getName(), "song draft export"); exportDraftIfDue(); }
        if (hum.armed.load())
        {
            hum.drain (humAudio);
            if ((double) humAudio.size() > hum.rate() * 30.0) finishHum();   // 30 seconds at most
        }
    }

    bool wasBroadcasting = false;
    int64_t hookRestsHash = 0;
    juce::uint32 hookRestsMs = 0;
    std::vector<std::pair<float, float>> hookRests;   // where the song's hook rests (shared with Transmutation)
    mutable juce::CriticalSection motifLock;
    std::vector<chords::Note> motif;
    chords::MelodyDna dna;
    chords::BassDna bassDna;
    uint32_t lastSuggestion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CorrespondenceProcessor)
};
