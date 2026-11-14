#pragma once

// Quintessence: an EQ and a three-band compressor that manage themselves.
//
// Every few seconds of music it measures the track (ten octave bands after the EQ, and the
// dynamics of three bands before and after the compressor) and makes at most one move towards
// what the track's role usually sounds like: a band 1-1.5 dB at a time (never past +-4 dB), a
// low cut where the part has no job, a compressor band in or out. On the next pass it checks
// the move: if the sound didn't get closer to the target, the move is undone and that spot is
// left alone for a while. Mentalism's masking, mud and harshness fixes for this track come
// here instead of to Prima Materia, with dips that open only while the vocal or lead plays.
// Touch a band and it's yours: it locks, and the automation leaves it alone.

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "SuiteDSP.h"
#include "AutopilotBus.h"
#include "FixBus.h"
#include "SectionBus.h"
#include "HostInfo.h"
#include "Parameters.h"
#include "Targets.h"
#include "Resonance.h"
#include "SpireTips.h"
#include "ConductorBus.h"
#include "AutopilotBus.h"
#include "ReferenceBus.h"
#include "AutomationBus.h"
#include "KickBus.h"
#include "ChordBus.h"
#include "ActivityBus.h"
#include "Music.h"
#include "Registry.h"
#include "TrackTraits.h"
#include "LabelDsp.h"
#include "LabelDsp2.h"
#include "LabelDsp3.h"
#include "LabelDsp4.h"
#include "MixLatch.h"
#include "LabelReadyBus.h"
#include "LiveMix.h"
#include "DirectorStage.h"
#include "Premaster.h"
#include "PeakBus.h"
#include "KeyBus.h"

class QuintessenceProcessor : public suite::SuiteProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    QuintessenceProcessor();
    ~QuintessenceProcessor() override;

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
    SuiteStatus suiteStatus() override;
    bool morphsWithSnapshots() const override { return false; }   // it manages its own settings
    void pollTick() override
    {
        applyLocks();
        autoPass();
        labelDsp3.tick();
        if (++haasTick % 10 == 0) labelDsp4.tick();
        // where the master's peaks come from: this track's last 2 s
        const auto pk = peakHold.exchange (0.0f);
        peakWindow[(size_t) (peakTick++ % peakWindow.size())] = pk;
        float recent = 0.0f;
        for (auto v : peakWindow) recent = juce::jmax (recent, v);
        if (recent > 1.0e-5f) suite::PeakBus::post (getInstanceId(), getTrackName(), roleShown.load(), juce::Decibels::gainToDecibels (recent), peakAtQ.load());
        // what this track is like, for the label-ready checks (about once a second)
        if (++traitsTick % 5 == 0)
        {
            auto t = traits.snapshot (getInstanceId(), effectiveRole(), getTrackName());
            t.lowestNote = labelDsp3.lowestNote.load();
            t.haasMs = labelDsp4.haasMs.load();
            suite::TrackTraits::publish (t);
            latchLabelFixes (t);
        }
    }
    /** The label-ready fixes that live here: switched on (and kept) where Ouroboros' checks found
        the problem on this track, while their autopilot switch is on. */
    qn::LabelDsp labelDsp;
    uint32_t labelLatched = 0;   // 1 pad motion, 2 breakdown low cut, 4 input staging (saved with the project)
    void latchLabelFixes (const suite::TrackTraits::Traits& t)
    {
        namespace AP = suite::Autopilot;
        const auto role = effectiveRole();
        auto latch = [&] (AP::Feature f, uint32_t bit, bool applies, const char* what)
        {
            if ((labelLatched & bit) == 0 && applies && AP::on (f) && suite::LabelReadyBus::needed ((int) f))
            {
                labelLatched |= bit;
                apvts.state.setProperty ("labelLatched", (int) labelLatched, nullptr);
                addLog (juce::String ("Label-ready: ") + what);
            }
        };
        latch (AP::padMotionF, 1u, role == 4 && t.movementCount >= 20 && t.movementDb < 0.8f, "a slow filter movement over each phrase (this pad never changed)");
        latch (AP::breakdownLowCutF, 2u, role != 0 && role != 1 && role != 2 && t.lowMidDb > t.outRmsDb - 6.0f, "a low cut in the breakdowns (lows left to the bass)");
        latch (AP::inputStageF, 4u, t.inRmsDb > -90.0f && std::abs (t.inRmsDb + 18.0f) > 8.0f, "the input staged to -18 dBFS RMS (the level going out unchanged)");
        latch (AP::motionF, 8u, (role == 3 || role == 5 || role == 7) && t.movementCount >= 20 && t.movementDb < 1.0f, "slow movement over each phrase (this part never changed)");
        latch (AP::kickTailF, 16u, role == 1 && t.decayCount >= 3, "the kick's tail shortened in the drops");
        latch (AP::subHandoverF, 32u, role == 2, "the sub handed over through the builds");
        latch (AP::chokeF, 64u, role == 3 || role == 7, "cut for the last 16th before each drop");
        latch (AP::subsonicF, 128u, t.subsonicDb > -40.0f, "a subsonic filter (energy under 20 Hz)");
        latch (AP::driveF, 256u, role == 5, "gentle drive for harmonics (the lead didn't cut through)");
        labelDsp.padMotion.store (labelDsp.padMotion.load() || ((labelLatched & 8u) != 0 && AP::on (AP::motionF)));
        labelDsp2.kickTail.store ((labelLatched & 16u) != 0 && AP::on (AP::kickTailF));
        labelDsp2.subHandover.store ((labelLatched & 32u) != 0 && AP::on (AP::subHandoverF));
        labelDsp2.choke.store ((labelLatched & 64u) != 0 && AP::on (AP::chokeF));
        labelDsp2.subsonic.store ((labelLatched & 128u) != 0 && AP::on (AP::subsonicF));
        labelDsp2.drive.store ((labelLatched & 256u) != 0 && AP::on (AP::driveF));
        bool anyTone = false, anyWidth = false;
        for (auto& row : labelDsp2.toneDb) for (auto& v : row) anyTone = anyTone || std::abs (v.load()) > 0.01f;
        for (auto& v : labelDsp2.widthOffset) anyWidth = anyWidth || std::abs (v.load()) > 0.001f;
        labelDsp2.sectionTone.store (anyTone && AP::on (AP::sectionToneF));
        labelDsp2.sectionWidth.store (anyWidth && AP::on (AP::sectionWidthF));
        labelDsp.padMotion.store ((labelLatched & 1u) != 0 && AP::on (AP::padMotionF));
        labelDsp.breakdownLowCut.store ((labelLatched & 2u) != 0 && AP::on (AP::breakdownLowCutF));
        labelDsp.inputStage.store ((labelLatched & 4u) != 0 && AP::on (AP::inputStageF));
        latchMixFixes (t);
        if (t.inRmsDb > -90.0f && labelDsp.inputStage.load())
            labelDsp.stageDb.store (labelDsp.stageDb.load() + 0.2f * (juce::jlimit (-12.0f, 12.0f, -18.0f - (t.inRmsDb + labelDsp.stageDb.load())) ));
    }
    /** What this track is like (clip gain, recovery, stereo lows, tails, levels per section...). */
    suite::TrackTraits::TraitsMeter traits;
    int traitsTick = 0, haasTick = 0;
    /** The label-ready fixes for this track: its polarity flipped (a layer cancelling another), and
        its level in each kind of section (the lead kept on top, a section matched to the label).
        Saved with the project. */
    std::atomic<bool> polarityFlipped { false };
    std::array<std::atomic<float>, 6> sectionTrimDb {};
    std::function<void()> flipPolarity (uint32_t data);
    std::function<void()> punchStep (uint32_t data);
    std::function<void()> vocalDelay (uint32_t data);
    std::function<void()> sectionTone (uint32_t data);
    std::function<void()> sectionWidthStep (uint32_t data);
    qn::LabelDsp2 labelDsp2;
    // The mix fixes (LabelDsp3): each latched once Ouroboros' checks ask for it and it applies to
    // this track (its role, its name, what it measures); saved with the project.
    qn::LabelDsp3 labelDsp3;
    qn::LabelDsp4 labelDsp4;
    uint64_t mixLatched = 0;   // (the old way of keeping them: carried over into mixLatch once)
    suite::MixLatch mixLatch { apvts };
    bool mixApplies4 (int fix, int role, const juce::String& name, const suite::TrackTraits::Traits& t) const;
    // safety: the fail-safe (a misbehaving fix off for this song), hear what the fixes remove, the A/B's level match
    juce::AudioBuffer<float> dryCopy;
    std::atomic<bool> failsafeOff { false }, failsafeTripped { false }, deltaListen { false }, mixBudgetHit { false };
    std::atomic<float> mixLevelDb { 0.0f };
    bool mixLog = false;
    float bypassMatch = 1.0f, matchGainNow = 1.0f;
    bool mixApplies (int fix, int role, const juce::String& name, const suite::TrackTraits::Traits& t) const;
    void latchMixFixes (const suite::TrackTraits::Traits& t);
    std::function<void()> phraseTrimStep (uint32_t data);
    std::function<void()> trackPan (uint32_t data);
    std::function<void()> sectionTrim (uint32_t data);
    void storeLabelFixes();
    void loadLabelFixes();
    float trimGain = 1.0f;   // audio thread: the section trim in use (ramped)

    //==============================================================================
    static constexpr double passSeconds = 3.0;   // music heard per pass
    static constexpr double firstPassSeconds = 1.5;   // ... for the first few passes, to settle a new track quickly
    double passSecondsNow() const { return passes.load() < 6 ? firstPassSeconds : passSeconds; }

    /** Message thread: one pass if enough music has been heard since the last (the poller calls
        it; tests call it directly). Returns true if it made or undid a move. */
    bool autoPass();
    /** Message thread: turns manual edits into locks (the poller calls it; tests too). */
    void applyLocks();
    /** Forgets the automatic tone and dynamics moves (carves and your own bands stay). */
    void relearn();
    /** The role in use: the Role setting, or the track's (from Prima Materia or the name). */
    int effectiveRole() const;

    juce::StringArray moveLog() const { const juce::ScopedLock sl (logLock); return log; }
    /** What to change in Spire for this track (from the last pass). */
    juce::StringArray spireTips() const { const juce::ScopedLock sl (logLock); return tipsNow; }
    juce::String spireStart() const;
    /** Ringing peaks found in the last pass (Hz). */
    std::vector<float> lastResonances() const { const juce::ScopedLock sl (logLock); return resonancesNow; }

    struct Measurement
    {
        std::array<float, 10> bandsDb {};
        std::array<float, qn::numMbBands> p50Pre {}, p95Pre {}, p50Post {}, p95Post {};
        std::array<int, qn::numMbBands> windowsPost {};
        float seconds = 0.0f;
        float midDb = -100.0f, sideDb = -100.0f;
        std::array<float, 4> sibSwing {}, sibPeak {};   // a vocal's 4.5 / 6 / 7.5 / 9 kHz bands: how far their loud moments stand out (before de-essing)
        float sibPostSwing = 0.0f;          // ... the de-esser's band after it
        float punchDb = 0.0f;               // how far the attacks stand over the body (fast over slow follower, dB, average of each 50 ms window's most; 0 = nothing heard)
        float glueGrDb = 0.0f, glueLevelDb = -100.0f;   // the glue's average gain reduction, and the level it sees
        float sideMinusMid() const { return midDb > -90.0f ? sideDb - midDb : -100.0f; }
        int group = 0;   // 0: anywhere but a breakdown, 1: a breakdown (it keeps a separate correction there)
        float swingPost (int b) const { return p95Post[(size_t) b] - p50Post[(size_t) b]; }
    };
    /** The measurement so far this pass (any thread). */
    Measurement measurement() const;
    /** Mean absolute distance from the role's tone, over the bands that matter (dB); -1 if none. */
    float toneError (const Measurement&, int role) const;
    /** The measured shape minus the target, per band (0 where it doesn't matter). */
    std::array<float, 10> toneDiff (const Measurement&, int role) const;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<suite::FixReceiver> fixes;

    std::array<std::atomic<float>, qn::numMbBands> grNow {};          // gain reduction, dB (UI)
    std::array<std::atomic<float>, 10> shownDiff {};                  // measured minus target (UI)
    std::atomic<int> roleShown { 0 }, groupShown { 0 }, learnedShown { 0 };

    /** The tone it steers towards for a role: your own (from the songs you finished, once there are
        3 or more in the style log), or the built-in one. */
    const std::array<float, 10>& shapeFor (int role) const;
    int learnedSongs (int role) const { return learnedCount[(size_t) juce::jlimit (0, 7, role)]; }
    /** Reads the style log's per-role tone of your finished songs. */
    void loadLearned();
    std::atomic<int> passes { 0 }, kept { 0 }, undone { 0 };

private:
    void parameterChanged (const juce::String& id, float) override;
    void write (const juce::String& id, float plain);
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void addLog (const juce::String& line);
    bool eqMove (const Measurement&, int role);
    bool widthMove (const Measurement&, int role);
    bool resonanceMove (const Measurement&, int role);
    bool punchMove (const Measurement&, int role);
    bool glueMove (const Measurement&, int role);
    bool deessMove (const Measurement&, int role);
    bool budgetMove (const Measurement&, int role);
    void keyMove (int role);       // the autopilot: a pitched FX note out of the song's key is notched
    void returnsMove();            // ... and an effect return gets its cuts
    bool returnsDone = false, monoCounted = false, drumBusCounted = false;
    float inPeakWin = 0.0f;
    double inPowWin = 0.0;
    int inWinCount = 0;
public:
    std::atomic<float> inCrestDb { -100.0f };   // the crest of what comes in (peak over RMS, 400 ms windows, averaged)
private:
    float needNow (const Measurement&, int role) const;
    std::function<void()> masterTone (uint32_t data);
    int budgetDone = -1;
    void drainSpectrum();
    bool mbMove (const Measurement&, int role);
    int slotFor (int band, bool forCarve) const;
    std::function<void()> carve (uint32_t data);
    uint32_t primaMateriaIdHere() const;
    void requestReset() { resetRequested.store (true); }

    //==============================================================================
    // Audio thread
    double sr = 48000.0;
    double tempoNow = -1.0;
    std::array<suite::Biquad, 2> chordDip, riserDip, keyNotch;
    float keyNotchSet = 0.0f;
    float chordDipHz = 0.0f, riserDipDb = 0.0f;
    suite::SectionFollower follower;
    int passGroup = -1;
    std::atomic<int> measureGroup { 0 };
    std::array<suite::Svf, 4> lowCuts, highCuts;   // two per channel: 24 dB/oct
    float lowCutNow = -1.0f, highCutNow = -1.0f;
    qn::DirectorStage directorStage;
    float headroomGain = 1.0f;
    suite::DirectorBus::Follower suiteDirector, suiteDirectorIdle;   // (idle: never refreshed, so the stage ramps back to neutral)
public:
    std::atomic<float> directorPresence { 0.0f };   // the director's room-making dip now (dB)
    std::atomic<float> headroomTrimNow { 0.0f };    // the premaster headroom trim on this track now (dB)
    std::atomic<float> peakHold { 0.0f };           // the output's peak since the last tick (audio thread)
    std::atomic<double> peakAtQ { 0.0 };            // where it was (song position)
    std::array<float, 8> peakWindow {};
    int peakTick = 0;
private:
    std::array<std::array<suite::Biquad, 2>, qn::numSlots> peaks;
    std::array<std::array<float, 3>, qn::numSlots> peakNow {};   // freq, gain, q in use
    // The multiband: a stereo Linkwitz-Riley tree (6 splits, and each lower band phase-matched to
    // the splits above it so the bands add back up flat), run only while a band compresses; and a
    // cheap mono split that measures the bands all the time (for the automatic dynamics)
    static constexpr int numComps = qn::numCross * (qn::numCross - 1) / 2;
    std::array<std::array<suite::Crossover, qn::numCross>, 2> splits;
    std::array<std::array<suite::Crossover, numComps>, 2> comps;
    std::array<suite::Crossover, qn::numCross> measureSplits;
    enum MbState { mbOff, mbWarming, mbOn, mbFadingOut };
    int mbState = mbOff, mbWarm = 0;
    float mbFade = 0.0f, mbFadeStep = 0.001f;
public:
    std::atomic<bool> multibandRunning { false };   // tests and the UI: the tree is in the signal path
private:
    std::array<suite::EnvelopeFollower, qn::numMbBands> env;
    std::array<float, qn::numMbBands> makeupDb {};
    float makeupCoeff = 0.0f;

    // Measurement (audio thread writes; message thread reads and asks for resets)
    std::array<suite::Svf, 10> analysers;
    std::array<double, 10> winBand {}, passBand {};
    double winTotal = 0.0;
    int winCount = 0, winLength = 4800, passWindows = 0, settle = 0;
    std::array<double, qn::numMbBands> mbWinPre {}, mbWinPost {};
    int mbWinCount = 0, mbWinLength = 2400;
    static constexpr int histBins = 200;   // half-dB bins from 0 down to -100 dBFS
    std::array<std::array<std::atomic<int>, histBins>, qn::numMbBands> histPre {}, histPost {};
    std::array<std::atomic<float>, 10> passBandDb {};
    std::atomic<int> passWindowsShared { 0 };
    double winMid = 0.0, winSide = 0.0, passMid = 0.0, passSide = 0.0;
    std::atomic<float> passMidDb { -100.0f }, passSideDb { -100.0f };
    // De-esser, transient shaper and glue (audio thread)
    std::array<suite::Svf, 2> deessBand;
    std::array<suite::Svf, 4> sibAnalysers;
    // Autopilot: the harsh-moment dip and the stereo-collapse ride
    std::array<suite::Svf, 2> harshFilter;
    int harshBandNow = -1;
    float harshDipDb = 0.0f, dipAttack = 0.01f, dipRelease = 0.001f, widthRide = 1.0f, rideAttack = 0.01f, rideRelease = 0.0002f;
    double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0, corrCoeff = 0.001;
    bool dipping = false, riding = false;
public:
    std::atomic<float> widthRideNow { 1.0f };   // (tests, UI)
    std::atomic<float> keyNotchHz { 0.0f };     // an out-of-key FX note found (0 = none)
    std::atomic<float> fxShiftSemis { 0.0f };   // ... and the re-pitch that puts it in the key
    std::array<suite::PitchShifter, 2> fxShift;
    float fxShiftSet = 0.0f;
    double abInPow = 0.0, abOutPow = 0.0;       // loudness in and out (3 s), for the equal-loudness bypass
    qn::LiveMix live;                           // the autopilot's live mixing moves
private:
    suite::Svf sibPostAnalyser;
    suite::EnvelopeFollower deessEnv, fastEnv, slowEnv, glueEnv, outFast, outSlow;
    float attackMax = 0.0f;
    float deessFreqNow = -1.0f, deessSlow = 0.0f, deessSlowCoeff = 0.0f, glueMakeup = 0.0f, glueReleaseNow = -1.0f;
    std::array<double, 4> sibWin {};
    double sibPostWin = 0.0, crestPow = 0.0, punchSum = 0.0, glueGrSum = 0.0, glueLevelSum = 0.0;
    float crestPeak = 0.0f;
    int punchCount = 0;
    int64_t glueCount = 0;
    std::array<std::array<std::atomic<int>, histBins>, 4> histSib {};
    std::array<std::atomic<int>, histBins> histSibPost {};
    std::atomic<float> passPunchDb { 0.0f }, passGlueGr { 0.0f }, passGlueLevel { -100.0f };
public:
    std::atomic<float> deessNow { 0.0f };   // the de-esser's reduction now (dB, UI)
    std::atomic<float> buildNow { 0.0f };   // how far through a build (0 outside one)
    std::function<void()> narrower (uint32_t data);
    std::function<void()> lessSquash();
    inline static const juce::Identifier sideOffsetKey { "side_offset" }, swingOffsetKey { "swing_offset" };
    void setOffset (const juce::Identifier& id, float v);
public:
    /** The problem watcher's lasting changes to the targets: the side this many dB lower (narrower),
        the multiband letting this many dB more swing through (the drops breathe). */
    float sideOffset() const { return (float) apvts.state.getProperty (sideOffsetKey, 0.0f); }
    float dynamicsError (const Measurement&, int role) const;
    static void migrateBands (juce::ValueTree& state);
    float swingOffset() const { return (float) apvts.state.getProperty (swingOffsetKey, 0.0f); }
    // premaster-safe: the bus glue never holds more than 2 dB on average (mastering does the rest)
    float glueTarget() const { return juce::jlimit (0.5f, 2.0f, qn::targets::glueTargetDb - 0.25f * swingOffset()); }
    mutable std::atomic<float> needShown { 0.0f };   // what it asked the conductor with (dB)
    std::atomic<bool> bypassedNow { false }, kickKeyed { false };   // (tests, UI)
    std::atomic<float> chordHzNow { 0.0f }, riserDipNow { 0.0f }, sideNow { -100.0f };
private:
    std::vector<float> fifoData, blockMono;
    juce::AbstractFifo fifo { 1 << 18 };
    std::atomic<bool> resetRequested { true };
    void resetMeasurement() noexcept;

    //==============================================================================
    // Message thread
    struct Pending
    {
        enum Kind { none, eqSlot, lowCut, mb, width, resonance, punch, glue, deess, budget } kind = none;
        int index = -1, band = -1;
        std::vector<std::pair<juce::String, float>> before;
        float errBefore = 0.0f;
        juce::String what;
        float extra = 0.0f;
    } pending;
    std::array<int, 10> toneCooldown {};
    std::array<int, qn::numMbBands> mbCooldown {};
    bool lowCutTried = false;
    int nextFirst = 0;   // which kind of move gets the first chance this pass (they take turns)
    int widthCooldown = 0, punchCooldown = 0, deessCooldown = 0;
    qn::ResonanceFinder resonances;
    struct Candidate { float hz; int passes; float prominence; float seconds; };
    std::vector<Candidate> candidates, noteCandidates;   // ringing peaks; and held notes (for an FX's key check)
    std::vector<std::pair<float, int>> resonanceCooldown;   // Hz, passes left
    juce::StringArray tipsNow;
    std::vector<float> resonancesNow;
    std::array<float, 10> longBands {};
    bool haveBands = false;
    std::array<std::array<float, 10>, 8> learned {};
    std::array<int, 8> learnedCount {};
    mutable std::array<float, 10> withReference {};
public:
    mutable std::atomic<bool> referenceShown { false };   // steering with Mentalism's reference
private:
    std::atomic<bool> autoWriting { false };
    std::atomic<uint32_t> pendingLocks { 0 };
    juce::CriticalSection logLock;
    juce::StringArray log;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuintessenceProcessor)
};
