#pragma once

#include "Alias.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "FixBus.h"
#include "Arrangement.h"
#include "ArrangementDna.h"
#include "DjPhrasing.h"
#include "StyleLog.h"
#include "Listen.h"
#include "SuiteDSP.h"
#include "DirectorHost.h"
#include "LabelReadyHost.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <optional>
#include <thread>

class OuroborosProcessor : public suite::SuiteProcessor, private juce::Timer
{
public:
    OuroborosProcessor();
    ~OuroborosProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }   // live mode: play sections from MIDI notes
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

    /** Message thread: read/replace the arrangement (published to the suite immediately). */
    suite::SectionMap getMap() const;
    SuiteStatus suiteStatus() override
    {
        SuiteStatus s;
        if (getMap().count > 0) s.flags |= suite::Status::hasMap;
        return s;
    }
    void setMap (const suite::SectionMap&);
    /** Moments, one per line: "33 impact", "64 stop", "96 throw" (bar, then what). Returns how many were kept. */
    int setMomentsText (const juce::String&);
    juce::String momentsText() const;
    std::vector<suite::Moment> moments;

    /** Starts a section of `type` at the playhead's bar, snapped to the Snap setting. */
    void markAtPlayhead (suite::SectionType type);

    /** Publishes (or withdraws) the map. Called on a timer and after every edit. */
    void publish();

    // ---- Listen: measure the mix bar by bar while playing, then propose a map ----
    std::atomic<bool> listening { false };
    void clearListening() { resetFeatures.store (true); }
    std::vector<listen::BarFeature> features() const;
    int barsHeard() const;
    suite::SectionMap proposeFromListening() const { return listen::propose (features()); }

    /** Reference structure: decodes an audio file in the background, measures it bar by bar at
        `bpm` and proposes its section map. Poll takeReferenceProposal() for the result. */
    void analyseReference (const juce::File& file, double referenceBpm);
    /** Blocking version (tests): returns the proposal, count 0 if the file couldn't be read. */
    static suite::SectionMap proposeFromFile (const juce::File& file, double referenceBpm);
    /** The file measured bar by bar (empty if it couldn't be read). */
    static std::vector<listen::BarFeature> measureFile (const juce::File& file, double referenceBpm);
    std::optional<suite::SectionMap> takeReferenceProposal();
    std::atomic<bool> analysingReference { false };
    juce::String referenceName() const { const juce::ScopedLock sl (lock); return lastReferenceName; }

    juce::AudioProcessorValueTreeState apvts;

    // Transport, written by the audio thread, read by the UI.
    std::atomic<double> ppq { 0.0 }, bpm { 138.0 }, barQuarters { 4.0 };
    std::atomic<bool> playing { false };
    std::thread referenceThread;
    juce::CriticalSection notesLock;
    juce::StringArray notes;
    uint32_t seenFix = suite::FixBus::latest(), seenSnapshot = suite::SnapshotBus::latest(),
             seenSession = suite::SessionBus::latest(), seenRecipe = suite::RecipeBus::latest();
    std::optional<suite::SectionMap> referenceResult;
    juce::String lastReferenceName;
    suite::SectionFollower morphFollower;

    static inline const juce::String broadcastId { "broadcast" }, liftId { "finallift" }, finalSnapId { "finalsnap" }, dropGapId { "dropgap" },
                                     intensityId { "intensity" }, liveId { "live" }, entranceId { "entrance" }, rideId { "ride" }, autosaveId { "autosave" };
    /** Autosave: every few minutes of work (only after the song has played since the last one),
        the whole suite is saved as a recipe named "Autosave <date time>"; the last 12 are kept. */
    static int autosaveMinutes (int choice) { static const int m[] = { 0, 5, 10, 20 }; return m[juce::jlimit (0, 3, choice)]; }
    static inline const juce::String autosavePrefix { "Autosave " };
    static constexpr int autosavesKept = 12;
    juce::String autosaveNow();
    void checkAutosave();
    static juce::StringArray autosaves();          // newest first
    static void pruneAutosaves (int keep = autosavesKept);
    std::atomic<bool> playedSinceAutosave { false };
    juce::Time lastAutosave { juce::Time::getCurrentTime() };

    /** Style report: your keys, tempos, progressions, lengths, structures and loudness from the
        style log, plus your usual structure from your templates. Also saved as "My style.txt". */
    static juce::String styleReport();
    static juce::File styleReportFile()
    {
        return suite::Alias::file ("My style.txt");
    }
    /** Intensity ride: the controller that moves Intensity live (and FL records it as automation). */
    static int rideController (int choice) { static const int cc[] = { 0, 1, 2, 11, 16 }; return cc[juce::jlimit (0, 4, choice)]; }
    /** A controller value (0..127) as Intensity: 0 -> 0%, 64 -> 100%, 127 -> 200%. */
    static float rideIntensity (int value)
    {
        return value <= 64 ? 100.0f * (float) value / 64.0f : 100.0f + 100.0f * (float) (value - 64) / 63.0f;
    }
    /** Moves Intensity to the last controller value received (message thread; the timer calls it). */
    void applyRide();
    std::atomic<int> pendingRide { -1 };

    /** Snapshot per section: a snapshot slot (A-D) for each section type, recalled across the
        suite as the song enters a section of that type (Ouroboros keeps its own map). */
    static juce::String sectionSnapId (int type) { return "snap_sec_" + juce::String (type); }
    void followSectionSnapshots();
    int lastSnapSection = -1;
    std::atomic<int> sectionSnapsSent { 0 };

    /** Render plan: what to export, how loud, in what order (from the versions made). */
    juce::String renderPlan() const;
    /** The delivery package for the label: a folder with the render plan, the mastering brief,
        the file list for the label profile, the section markers and cue sheet, the director's
        plan and moves, and Mentalism's premaster report. Returns the folder. */
    juce::File writeDeliveryPackage (const juce::File& parent, int profileIndex, const juce::String& artist = {}, const juce::String& title = {});
    int deliveryProfile() const { return (int) apvts.state.getProperty ("deliveryProfile", 0); }
    void setDeliveryProfile (int i) { apvts.state.setProperty ("deliveryProfile", i, nullptr); }
    /** One-click finish: the learning pass starts (play the song once), Mentalism mixes it (Mix
        it), Correspondence exports every part, and the render plan follows. Returns what was set
        going, as a report to read while the song plays. */
    juce::String finishEverything()
    {
        const auto learn = startLearningPass();
        const auto mix = suite::FixBus::send (suite::Fix::MixItAll);
        const auto exported = suite::FixBus::send (suite::Fix::ExportAll);
        suite::MoveLog::add (getName(), getTrackName(), "Finish everything: the learning pass, Mix it and Export all, started together", suite::MoveLog::fix);
        juce::StringArray report;
        report.add ("Finish everything, started:");
        report.add (juce::String ("1. The learning pass") + (learn != 0 ? ": play the song once from the top; the suite sets itself up as it hears it." : " couldn't start (no section map yet)."));
        report.add ("2. Mix it (Mentalism): gain staging now, the rough mix once the drop is heard, then the arc, masking, mud and harshness.");
        report.add ("3. Export all (Correspondence): every part as MIDI, into the Exports folder.");
        report.add ("Every move shows in What changed? as it happens. When the song has played through, render it:");
        report.add ("");
        report.add (renderPlan());
        juce::ignoreUnused (mix, exported);
        return report.joinIntoString ("\n");
    }

    /** Mentalism's DJ phrasing fix (undoable). */
    std::unique_ptr<suite::FixReceiver> fixes;

    /** Arrangement DNA from your saved templates. */
    static arrangement::ArrangementDna learnArrangementDna()
    {
        std::vector<suite::SectionMap> maps;
        for (const auto& [name, m] : arrangement::loadUserTemplates (arrangement::userTemplateFolder())) maps.push_back (m);
        return arrangement::learnDna (maps);
    }
    /** Arrange from the loop: with an 8- or 16-bar loop playing and every track's Prima Materia given
        a role, lays out a full song (your usual structure if you have one, else a classic layout) and
        has every Prima Materia set which sections its track plays in from its role. Undo in Mentalism's History. */
    uint32_t arrangeFromLoop (int totalBars = 0)
    {
        const auto dna = learnArrangementDna();
        if (dna.valid) setMap (arrangement::generateLike (dna, totalBars));
        else
        {
            arrangement::GenSettings g;
            if (totalBars > 0) g.totalBars = totalBars;
            setMap (arrangement::generate (g));
        }
        addNote (juce::String ("Arranged the loop into a song") + (dna.valid ? " (your usual structure)" : ""));
        return suite::FixBus::send (suite::Fix::ArrangeFromLoop);
    }
    /** Writes the section markers as a MIDI file (drag or import into FL) plus a .txt cue sheet next to it. */
    bool exportMarkers (const juce::File& midiFile) const;

    /** Live mode: the note that plays each section type (C Intro, D Groove, E Breakdown, F Build, G Drop, A Outro). */
    static int liveTypeForNote (int note)
    {
        switch (((note % 12) + 12) % 12) { case 0: return 0; case 2: return 1; case 4: return 2; case 5: return 3; case 7: return 4; case 9: return 5; default: return -1; }
    }
    /** Live mode: a section played at `ppq` starts on the next bar line (the one after, if that's under a fifth of a bar away). */
    static double liveStartFor (double ppq, double barQ)
    {
        auto start = std::ceil (ppq / barQ - 1.0e-9) * barQ;
        if (start - ppq < 0.2 * barQ && start - ppq > 1.0e-6) start += barQ;
        return start;
    }
    std::atomic<int> liveType { -1 }, livePrevType { -1 }, liveIndex { 0 };

    /** Session notes: what the suite's automation did in this project (newest last, saved with it). */
    juce::StringArray getNotes() const { const juce::ScopedLock sl (notesLock); return notes; }
    void addNote (const juce::String& text)
    {
        const juce::ScopedLock sl (notesLock);
        notes.add (juce::Time::getCurrentTime().formatted ("%d %b %H:%M  ") + text);
        while (notes.size() > 200) notes.remove (0);
    }
    /** Reads new fixes, snapshot commands, sessions and recipes from the suite and notes them. */
    void logActivity();
    std::atomic<double> liveStartQ { 0.0 };
    bool morphsWithSnapshots() const override { return false; }   // Ouroboros sends the morph
    /** How far the suite morphs into the final-drop snapshot at this position (0..1). */
    static float finalMorphAmount (const suite::SectionFollower::Position& pos, const suite::SectionMap& map, double ppq) noexcept
    {
        const auto last = map.lastDropIndex();
        if (! pos.valid || last < 0) return 0.0f;
        if (pos.index >= last) return 1.0f;
        if (pos.index == last - 1 && pos.type == suite::SectionType::Build)
            return (float) juce::jlimit (0.0, 1.0, pos.progress (ppq));
        return 0.0f;
    }
    std::atomic<float> morphAmount { 0.0f };

    /** Versions: a radio edit (about 3:30, short intro and breakdown, at most two drops) or an
        extended mix (64 bars longer, DJ intro and outro) of the current map. */
    /** Your version lengths: when you open the render plan (the versions as you kept them), the
        radio edit's length and how much longer the extended mix is go into the style log; Make
        versions then uses your usual ones (two or more) instead of 3:30 and +64 bars. */
    struct VersionLengths { float radioSeconds = 0.0f; int extendedExtraBars = 0; };
    static VersionLengths usualVersionLengths (const std::vector<suite::StyleLog::Entry>& entries)
    {
        std::vector<float> radio, extra;
        for (const auto& e : entries)
        {
            if (e.kind != "versions") continue;
            if (e.get ("radioseconds").getFloatValue() > 0.0f) radio.push_back (e.get ("radioseconds").getFloatValue());
            if (e.get ("extendedextra").getIntValue() > 0) extra.push_back ((float) e.get ("extendedextra").getIntValue());
        }
        VersionLengths v;
        if (radio.size() >= 2) v.radioSeconds = suite::StyleLog::median (radio);
        if (extra.size() >= 2) v.extendedExtraBars = juce::jmax (8, (int) std::round (suite::StyleLog::median (extra) / 8.0f) * 8);
        return v;
    }
    /** The map saved in a snapshot slot (count 0 if none). */
    suite::SectionMap snapshotMap (int slot) const;
    /** Logs the kept versions' lengths (once per session; the render plan calls it). */
    bool logVersionLengths();
    bool versionLengthsLogged = false;

    bool applyVersion (int kind) override
    {
        const auto m = getMap();
        if (kind == suite::clubEdit) { setMap (arrangement::clubEdit (m)); return true; }
        int drops = 0;
        for (int i = 0; i < m.count; ++i) drops += m.sections[(size_t) i].type == suite::SectionType::Drop;
        arrangement::GenSettings g;
        g.drops = juce::jlimit (1, 3, drops);
        const auto yours = usualVersionLengths (suite::StyleLog::read());
        if (kind == suite::radioEdit)
        {
            g.drops = juce::jmin (2, g.drops);
            const auto minutes = yours.radioSeconds > 0.0f ? yours.radioSeconds / 60.0 : 3.5;   // your usual radio edit, or 3:30
            g.totalBars = juce::jmax (64, (int) std::round (minutes * bpm.load() / 4.0 / 8.0) * 8);
            g.djIntro = false;
            g.longBreakdown = false;
        }
        else
        {
            g.totalBars = m.totalBars + (yours.extendedExtraBars > 0 ? yours.extendedExtraBars : 64);
            g.djIntro = true;
            g.longBreakdown = true;
        }
        setMap (arrangement::generate (g));
        return true;
    }

    /** Makes the versions in one go: A = the current song, B = radio edit, C = extended mix,
        D = club edit (32-bar beat-only intro and outro), then back to A. Every suite plugin captures each slot in order. */
    uint32_t makeVersions()
    {
        using suite::SnapshotAction;
        suite::SnapshotBus::send (SnapshotAction::Capture, 0);
        suite::SnapshotBus::send (SnapshotAction::ApplyVersion, suite::radioEdit);
        suite::SnapshotBus::send (SnapshotAction::Capture, 1);
        suite::SnapshotBus::send (SnapshotAction::Recall, 0);
        suite::SnapshotBus::send (SnapshotAction::ApplyVersion, suite::extendedMix);
        suite::SnapshotBus::send (SnapshotAction::Capture, 2);
        suite::SnapshotBus::send (SnapshotAction::Recall, 0);
        suite::SnapshotBus::send (SnapshotAction::ApplyVersion, suite::clubEdit);
        suite::SnapshotBus::send (SnapshotAction::Capture, 3);
        const auto last = suite::SnapshotBus::send (SnapshotAction::Recall, 0);
        pollSnapshots();
        return last;
    }

    /** New song: builds this style's map (Arrange), sets its key change, and asks the rest of
        the suite to set up (key, progression, loudness target, scenes). Returns the message's
        sequence number, for counting the plugins that answered. */
    uint32_t startSession (int styleIndex, int root, int scale, int moodIndex = 0, bool fromSignature = false)
    {
        // Your signature first (every plugin's own sound), then the song's style and key on top.
        // Ouroboros keeps its new map: it skips the signature itself.
        if (fromSignature && suite::Recipes::list().contains (suite::Recipes::signature))
        {
            suite::RecipeBus::send (suite::RecipeAction::Load, suite::Recipes::signature);
            ignorePendingRecipes();
        }
        const auto& mood = suite::sessionMood (moodIndex);
        const auto& style = suite::sessionStyles()[(size_t) juce::jlimit (0, (int) suite::sessionStyles().size() - 1, styleIndex)];
        arrangement::GenSettings g;
        g.totalBars = suite::sessionBars (style);
        g.drops = style.drops;
        g.djIntro = style.djIntro;
        g.longBreakdown = style.longBreakdown;
        setParameterPlain (apvts, liftId, (float) style.finalLift);
        setParameterPlain (apvts, intensityId, (float) mood.intensity);
        auto m = arrangement::generate (g);
        if (mood.breakdownEnergy >= 0)
            for (int i = 0; i < m.count; ++i)
                if (m.sections[(size_t) i].type == suite::SectionType::Breakdown) m.sections[(size_t) i].energy = mood.breakdownEnergy;
        setMap (m);
        return suite::SessionBus::send ({ styleIndex, root, scale, moodIndex });
    }
    /** Song draft: New song (map, key, chords, every plugin set up), then Correspondence writes the
        whole song's MIDI into Drafts and every plugin saves the setup as a recipe ("Draft ..."). */
    uint32_t draftSong (int styleIndex, int root, int scale, int moodIndex = 0, bool fromSignature = false)
    {
        const auto seq = startSession (styleIndex, root, scale, moodIndex, fromSignature);
        draftDueAt = juce::Time::getMillisecondCounterHiRes() + 1500.0;   // once the suite has set up the new song
        return seq;
    }
    double draftDueAt = 0.0;

    /** Learning pass: one play-through and the suite sets itself up. Every plugin starts
        measuring afresh; once the song has been heard (80% of its bars, up to the last), Kybalion
        fits its ducking to the kick and flips where the low end cancels, and Mentalism runs Finish
        the song (gain staging, DJ check, rough mix, coaching). Undo puts the Kybalion moves back;
        Mentalism's History has the rest. */
    uint32_t startLearningPass()
    {
        for (auto& w : heardBars) w.store (0);
        learning.store (true);
        learnSeqs.clear();
        addNote ("Learning pass: play the whole song once");
        return suite::FixBus::send (suite::Fix::LearnStart);
    }
    int learnBarsHeard() const
    {
        int n = 0;
        for (const auto& w : heardBars) { auto v = w.load(); while (v) { n += (int) (v & 1u); v >>= 1; } }
        return n;
    }
    /** Ends the pass once the song has been heard (tests: `force`). True when it ended. */
    bool continueLearningPass (bool force = false)
    {
        if (! learning.load()) return false;
        const auto m = getMap();
        const auto atEnd = m.totalBars > 0 && lastHeardBar.load() >= m.totalBars && learnBarsHeard() * 10 >= m.totalBars * 8;
        if (! force && ! atEnd) return false;
        learning.store (false);
        learnSeqs = { suite::FixBus::send (suite::Fix::KickFit), suite::FixBus::send (suite::Fix::PhaseFlip) };
        suite::FixBus::send (suite::Fix::FinishSong);
        addNote ("Learning pass done: ducking fitted to the kick, phase checked, Finish the song started in Mentalism");
        return true;
    }
    int undoLearningPass()
    {
        int n = 0;
        for (auto seq : learnSeqs) if (seq != 0) { suite::FixBus::sendUndo (seq); ++n; }
        learnSeqs.clear();
        if (n > 0) addNote ("Undid the learning pass's Kybalion moves (Mentalism's History has the rest)");
        return n;
    }
    std::atomic<bool> learning { false };
    std::array<std::atomic<uint32_t>, 32> heardBars {};
    std::atomic<int> lastHeardBar { 0 };
    std::vector<uint32_t> learnSeqs;
    juce::String lastDraftRecipe;
    void continueDraft (bool now = false)
    {
        if (draftDueAt <= 0.0 || (! now && juce::Time::getMillisecondCounterHiRes() < draftDueAt)) return;
        draftDueAt = 0.0;
        suite::FixBus::send (suite::Fix::DraftSong);
        lastDraftRecipe = "Draft " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M");
        suite::RecipeBus::send (suite::RecipeAction::Save, lastDraftRecipe);
        addNote ("Drafted the song: MIDI in Drafts, setup saved as the recipe '" + lastDraftRecipe + "'");
    }
    // ---- Label-ready: the song measured as it plays, scored against the label (LabelReadyHost.h) ----
    lab::LabelReadyHost labelReady;
    lab::Analyser songAnalyser;          // audio thread
    lab::Accumulator songBar;            // audio thread: the bar being heard
    int songBarIndex = -1;               // audio thread
    int labelTicks = 0;
    void tickLabelReady()
    {
        ++labelTicks;
        if (playing.load() && labelTicks % 5 == 0) labelReady.sampleTracks (getMap(), ppq.load(), barQuarters.load(), bpm.load());
        if (labelTicks % 40 == 0)
        {
            labelReady.live.expectedSamples = (float) (sampleRate * barQuarters.load() * 60.0 / juce::jmax (1.0, bpm.load()));
            labelReady.update (getMap(), bpm.load(), barQuarters.load(), director.havePlan ? &director.plan : nullptr);
        }
    }

    // ---- The suite director: one plan for every transition (DirectorHost.h) ----
    ouro::DirectorHost director;
    /** Re-plans now (after an edit) and publishes. True if the plan changed. */
    bool replan()
    {
        directorDirty = false;
        const auto changed = director.update (getMap(), bpm.load(), barQuarters.load());
        directorPublished.store (director.havePlan);
        return changed;
    }
    std::atomic<bool> directorDirty { true };
    std::atomic<bool> directorPublished { false };   // the audio thread keeps the plan alive (an offline render runs faster than the timer)
    int directorTicks = 0, lastSampledBar = -1;
    void tickDirector()
    {
        if (++directorTicks % 5 == 0 && playing.load())   // the rehearsal pass: how loud and busy each bar is
        {
            const auto bar = (int) std::floor (ppq.load() / juce::jmax (1.0, barQuarters.load()) + 1.0e-9) + 1;
            director.sampleLive (bar);
            lastSampledBar = bar;
        }
        if (auto r = takeReferenceStyle(); r.valid) { director.reference = r; directorDirty = true; }
        if (directorDirty.load() || directorTicks % 20 == 0) replan();
    }
    suite::director::ReferenceStyle takeReferenceStyle() { const juce::ScopedLock sl (lock); auto r = referenceStyleResult; referenceStyleResult = {}; return r; }
    suite::director::ReferenceStyle referenceStyleResult;

    static inline const juce::String snapId { "snap" };
    static int snapBars (int index) { static const int s[] = { 1, 4, 8, 16 }; return s[juce::jlimit (0, 3, index)]; }

private:
    void timerCallback() override { applyRide(); publish(); logActivity(); checkAutosave(); followSectionSnapshots(); continueDraft(); continueLearningPass(); tickDirector(); tickLabelReady(); }

    static constexpr int maxListenBars = 1024;
    struct BarData
    {
        std::atomic<float> flux { 0.0f }, high { 0.0f }, full { 0.0f };
        std::atomic<int> samples { 0 };
    };
    std::array<BarData, maxListenBars> barData;
    std::atomic<bool> resetFeatures { false };
    suite::Svf lowBand, highBand;
    float lowEnv = 0.0f, envAttack = 0.1f, envRelease = 0.001f;
    double sampleRate = 44100.0;

    mutable juce::CriticalSection lock;
    suite::SectionMap map;
    bool wasBroadcasting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OuroborosProcessor)
};
