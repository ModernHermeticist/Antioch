#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "FixBus.h"
#include "Arrangement.h"
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
                                     intensityId { "intensity" }, liveId { "live" };

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
        then back to A. Every suite plugin captures each slot in order. */
    uint32_t makeVersions()
    {
        using suite::SnapshotAction;
        suite::SnapshotBus::send (SnapshotAction::Capture, 0);
        suite::SnapshotBus::send (SnapshotAction::ApplyVersion, suite::radioEdit);
        suite::SnapshotBus::send (SnapshotAction::Capture, 1);
        suite::SnapshotBus::send (SnapshotAction::Recall, 0);
        suite::SnapshotBus::send (SnapshotAction::ApplyVersion, suite::extendedMix);
        suite::SnapshotBus::send (SnapshotAction::Capture, 2);
        const auto last = suite::SnapshotBus::send (SnapshotAction::Recall, 0);
        pollSnapshots();
        return last;
    }

    /** New song: builds this style's map (Arrange), sets its key change, and asks the rest of
        the suite to set up (key, progression, loudness target, scenes). Returns the message's
        sequence number, for counting the plugins that answered. */
    uint32_t startSession (int styleIndex, int root, int scale)
    {
        const auto& style = suite::sessionStyles()[(size_t) juce::jlimit (0, (int) suite::sessionStyles().size() - 1, styleIndex)];
        arrangement::GenSettings g;
        g.totalBars = suite::sessionBars (style);
        g.drops = style.drops;
        g.djIntro = style.djIntro;
        g.longBreakdown = style.longBreakdown;
        setParameterPlain (apvts, liftId, (float) style.finalLift);
        setMap (arrangement::generate (g));
        return suite::SessionBus::send ({ styleIndex, root, scale });
    }
    static inline const juce::String snapId { "snap" };
    static int snapBars (int index) { static const int s[] = { 1, 4, 8, 16 }; return s[juce::jlimit (0, 3, index)]; }

private:
    void timerCallback() override { publish(); logActivity(); }

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
