#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "FixBus.h"
#include "Arrangement.h"
#include "ArrangementDna.h"
#include "DjPhrasing.h"
#include "StyleLog.h"
#include "Listen.h"
#include "SuiteDSP.h"
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
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Antioch Suite").getChildFile ("My style.txt");
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

    /** Mentalism's DJ phrasing fix (undoable). */
    std::unique_ptr<suite::FixReceiver> fixes;

    /** Arrangement DNA from your saved templates. */
    static arrangement::ArrangementDna learnArrangementDna()
    {
        std::vector<suite::SectionMap> maps;
        for (const auto& [name, m] : arrangement::loadUserTemplates (arrangement::userTemplateFolder())) maps.push_back (m);
        return arrangement::learnDna (maps);
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
    bool applyVersion (int kind) override
    {
        const auto m = getMap();
        if (kind == suite::clubEdit) { setMap (arrangement::clubEdit (m)); return true; }
        int drops = 0;
        for (int i = 0; i < m.count; ++i) drops += m.sections[(size_t) i].type == suite::SectionType::Drop;
        arrangement::GenSettings g;
        g.drops = juce::jlimit (1, 3, drops);
        if (kind == suite::radioEdit)
        {
            g.drops = juce::jmin (2, g.drops);
            g.totalBars = juce::jmax (64, (int) std::round (3.5 * bpm.load() / 4.0 / 8.0) * 8);
            g.djIntro = false;
            g.longBreakdown = false;
        }
        else
        {
            g.totalBars = m.totalBars + 64;
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
    static inline const juce::String snapId { "snap" };
    static int snapBars (int index) { static const int s[] = { 1, 4, 8, 16 }; return s[juce::jlimit (0, 3, index)]; }

private:
    void timerCallback() override { applyRide(); publish(); logActivity(); checkAutosave(); followSectionSnapshots(); }

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
