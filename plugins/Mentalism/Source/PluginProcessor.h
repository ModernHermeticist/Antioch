#pragma once

#include "Alias.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Advisor.h"
#include "Parameters.h"
#include "Spectrum.h"
#include "Coach.h"
#include "MySound.h"
#include "Masking.h"
#include "HostInfo.h"
#include "FixBus.h"
#include <thread>
#include <map>

/** The analysed shape and loudness of a reference track. */
struct ReferenceProfile
{
    juce::String name;
    float lufs = -100.0f, truePeakDb = -100.0f;
    advisor::Bands bands {};
    bool valid = false;
    /** The reference's loudest 20 s (its drop), kept for A/B at equal loudness. */
    struct Excerpt { juce::AudioBuffer<float> audio; double sampleRate = 44100.0; float lufs = -100.0f; };
    std::shared_ptr<const Excerpt> excerpt;

    /** Analyses an audio file (WAV/AIFF/FLAC/Ogg/MP3). Safe to call on any thread. */
    static ReferenceProfile analyse (const juce::File&, const std::atomic<bool>* cancel = nullptr);
};

class MentalismProcessor : public suite::SuiteProcessor, private juce::Timer
{
public:
    MentalismProcessor();
    ~MentalismProcessor() override;

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

    /** Clears integrated loudness, true peak and long-term balance (applied on the next block). */
    void resetMeasurements() { keepPassForCompare(); resetRequested.store (true); autoPasses.store (0); lastApplied.clear(); revertedFixes.clear(); }
    /** The learning pass (Ouroboros): start measuring afresh, and Finish the song at its end. */
    std::unique_ptr<suite::FixReceiver> learnReceiver;

    /** Pass compare: Reset keeps the pass you just measured (if it ran 20 s or more), and the
        next pass is compared with it: loudness (so you can hear both at equal loudness) and the
        tonal changes. */
    struct Pass { bool valid = false; float lufs = -100.0f, truePeakDb = -100.0f; advisor::Bands bands {}; };
    void keepPassForCompare();
    void setPreviousPass (const Pass& p) { const juce::ScopedLock sl (historyLock); previousPass = p; }
    static juce::String comparePasses (const Pass& before, const Pass& now);

    /** Mix report: everything Mentalism knows about this mix, as text. Saved by "Save mix
        report" in the checklist, and automatically when Finish the song completes. */
    juce::String mixReport() const;
    static juce::File& reportFolderOverride() { static juce::File f; return f; }   // tests
    static juce::File reportFolder()
    {
        if (reportFolderOverride() != juce::File()) return reportFolderOverride();
        return suite::Alias::file ("Mix reports");
    }
    juce::File saveMixReport() const;

    /** My sound: learns a loudness and tonal target from your finished tracks (in the
        background), saved for every project. */
    void learnMySound (const juce::Array<juce::File>& tracks);
    static MySound analyseMySound (const juce::Array<juce::File>& tracks, const std::atomic<bool>* cancel = nullptr);
    bool isLearningMySound() const { return learningMySound.load(); }
    MySound getMySound() const { const juce::ScopedLock sl (referenceLock); return mySound; }
    void setMySound (const MySound& m) { const juce::ScopedLock sl (referenceLock); mySound = m; }

    /** Auto-reference: analyses every track in a folder (up to 40) and loads the one closest to
        this song (tone so far, and key and tempo where the file names say them). Background thread. */
    void autoReference (const juce::File& folder);
    /** The choice itself, synchronously (tests): the profile to load and why it was chosen. */
    static std::pair<ReferenceProfile, juce::String> chooseReference (const juce::File& folder, const advisor::Bands& mix, bool haveMix,
                                                                      int root, bool minor, float bpm, const std::atomic<bool>* cancel = nullptr);
    juce::String autoReferenceStatus() const { const juce::ScopedLock sl (referenceLock); return autoRefText; }

    /** Starts analysing a reference track in the background. */
    void loadReference (const juce::File&);
    bool isLoadingReference() const { return loading.load(); }
    ReferenceProfile getReference() const;
    void setReference (const ReferenceProfile&);
    juce::String autoRefText;

    /** Everything the UI shows: current numbers, chosen targets and advice. */
    struct Report
    {
        advisor::Measurement measurement;
        float momentary, shortTerm, targetLufs, ceiling;
        juce::String targetName;
        advisor::Bands shortDiff {}, longDiff {};
        bool tonalAvailable = false;     // false when "Reference" is chosen but none is loaded
        bool arrangementAvailable = false;
        suite::SectionMap map;
        std::vector<coach::SectionStat> sections;
        coach::Result arrangement;
        float measuredSeconds = 0.0f;
        juce::StringArray advice;
        juce::String sinceLastPass;   // pass compare, once this pass has run 20 s
    };
    Report report() const;

    /** Your own coach numbers and rough-mix balance, learned from finished songs: Finish the
        song logs each finished mix's arc (breakdown contrast, build rise) and every track's level
        against the kick; the coach judges by your arc once three songs are logged. */
    void logFinishedSong (const Report&);
    /** Mix DNA: once two or more finished masters are in the style log, My sound learns from them
        (unless you chose your own tracks). True if it changed. */
    bool learnMixDna();
    /** "Sounds like me": this song against your style log and mix DNA (see LikeMe.h). */
    juce::String soundsLikeMe() const;
    /** Live mix checks, while the song plays: a track going over 0 dBFS (Prima Materia), a vocal
        buried for 4 bars (Solve et Coagula), a drop that's almost mono for 8 s. First in the suggestions. */
    juce::StringArray liveChecks() const;
    /** A/B (Compare switch, or automate it from a key): while it's on, Mentalism plays the reference's
        loudest 20 s, looped, at the loudness the mix had when you switched, so you compare sound,
        not level. Put Mentalism last on the master to hear it. */
    std::atomic<bool> abPlaying { false };
    std::atomic<float> abGainDb { 0.0f };
private:
    juce::SpinLock abLock;
    std::shared_ptr<const ReferenceProfile::Excerpt> abShared, abExcerpt;   // abExcerpt: the audio thread's copy
    double abPos = 0.0;
public:
    static constexpr float monoCorrelation = 0.97f, monoDropAfterSeconds = 8.0f;
    std::atomic<float> monoDropSeconds { 0.0f };
    std::atomic<float> hostBpm { 0.0f };
    void refreshCoachThresholds();
    coach::Thresholds getCoachThresholds() const { const juce::ScopedLock sl (historyLock); return coachLimits; }

    juce::AudioProcessorValueTreeState apvts;
    suite::AudioTap spectrumTap;

    analysis::LoudnessMeter loudness;
    analysis::TruePeakMeter truePeak;
    analysis::StereoMeter stereo;
    analysis::BandAnalyzer bands;

    /** Loudness gathered per section of the current Ouroboros map (any thread). */
    std::vector<coach::SectionStat> sectionStats() const;

    /** Versions: a radio edit is mastered for streaming, an extended mix for the club. */
    /** For Ouroboros' next-step assistant: [coach problems, checklist failures, seconds measured, ready to export]. */
    SuiteStatus suiteStatus() override;

    bool applyVersion (int kind) override
    {
        setParameterPlain (apvts, Params::loudnessTarget, kind == suite::radioEdit ? 2.0f : 0.0f);
        return true;
    }

    /** New song (Ouroboros): the loudness target for the style. */
    bool applySession (const suite::SessionStyle& style, int, int) override
    {
        setParameterPlain (apvts, Params::loudnessTarget, (float) style.loudnessTarget);
        // Mix DNA: once your finished songs taught it your tonal balance, new songs aim for it.
        if (getMySound().valid && getMySound().learnedFromSongs())
            setParameterPlain (apvts, Params::tonalTarget, (float) advisor::mySoundTonalIndex);
        return true;
    }

    /** Reference match: half the difference from the loaded reference at the low end (below
        ~150 Hz) and the top (above ~6 kHz), at most 4 dB each, sent to Lapis' tone shelves; and
        the loudness target set to match the reference. Needs a reference and 10 s of the song.
        Returns the fix's sequence number (0 if there's nothing to go on). */
    uint32_t matchReference();
    std::atomic<float> lastMatchLow { 0.0f }, lastMatchHigh { 0.0f };

    /** Samples at full scale since the last Reset (for the pre-export checklist). */
    std::atomic<int> clippedSamples { 0 };

    /** Fix history (fixes and rough mixes sent from here), newest last. */
    struct FixRecord { uint32_t seq = 0; suite::Fix fix = suite::Fix::None; juce::Time when; uint32_t undoSeq = 0; };
    uint32_t sendFix (suite::Fix fix, uint32_t data = 0)
    {
        const auto seq = data != 0 ? suite::FixBus::send (fix, data) : suite::FixBus::send (fix);
        const juce::ScopedLock sl (historyLock);
        fixHistory.push_back ({ seq, fix, juce::Time::getCurrentTime(), 0 });
        if (fixHistory.size() > 16) fixHistory.erase (fixHistory.begin());
        return seq;
    }
    uint32_t undoFix (uint32_t seq)
    {
        const juce::ScopedLock sl (historyLock);
        for (auto& r : fixHistory)
            if (r.seq == seq && r.undoSeq == 0)
                return r.undoSeq = suite::FixBus::sendUndo (seq);
        return 0;
    }
    std::vector<FixRecord> getFixHistory() const { const juce::ScopedLock sl (historyLock); return fixHistory; }

    /** Sends one of the coach's fixes and forgets what was measured for the sections it
        changes, so the coach hears them again before judging. Returns the fix's sequence. */
    uint32_t applyCoachFix (suite::Fix fix, uint32_t data, const suite::SectionMap& map);

    /** Auto-coach: after every complete play-through, applies the coach's fixes (each once
        per pass, at most `maxAutoPasses` passes), until the coach is happy. */
    static constexpr int maxAutoPasses = 3;
    int autoCoachPasses() const { return autoPasses.load(); }
    /** Fixes that check themselves: after each auto-coach pass, the next complete play-through
        shows whether its fixes helped. One whose problem is still flagged, while the arrangement
        has no fewer problems than before, is undone and not tried again. */
    struct AppliedFix { suite::Fix fix; uint32_t data, seq; };
    std::vector<AppliedFix> lastApplied;
    int problemsBeforeLastPass = 0;
    std::vector<std::pair<suite::Fix, uint32_t>> revertedFixes;
    juce::StringArray verifyLastPass (const coach::Result& now);
    juce::String lastVerdict;   // the last check, for the UI and tests
    juce::String autoCoachStatus() const { const juce::ScopedLock sl (historyLock); return autoStatus; }

    /** Finish the song: gain staging, DJ intro/outro, reference tone, then (once the drops have
        been heard again) the rough mix, then the auto-coach until the arc is right. */
    enum class FinishStage { Idle, WaitingForDrop, Coaching, Done };
    juce::StringArray finishSong();
    FinishStage finishStage() const { return finishState.load(); }
    juce::String finishStatus() const { const juce::ScopedLock sl (historyLock); return finishText; }
    /** The timer's work (tests call it directly). */
    void runAutomation();

    /** Masking watch: every 30 s of playback the clashes are checked again; one that has lasted
        three checks in a row (a minute and a half) becomes a suggestion. Nothing is changed. */
    static constexpr int maskingChecksToSuggest = 3;
    void watchMasking (const std::vector<masking::Clash>& clashes);
    std::map<juce::String, int> autoCarves;   // masking resolved automatically: carves sent per clash (at most 3, each deeper)
    juce::StringArray maskingSuggestions() const { const juce::ScopedLock sl (historyLock); return persistentMasking; }

    /** After a fix: forget what was measured for sections of `type`, so the coach re-checks them. */
    void forgetSections (suite::SectionType type, const suite::SectionMap& map)
    {
        for (int i = 0; i < juce::jmin (map.count, (int) statsBySection.size()); ++i)
            if (map.sections[(size_t) i].type == type)
                statsBySection[(size_t) i].clear();
    }

    void forgetSection (int index)
    {
        if (juce::isPositiveAndBelow (index, (int) statsBySection.size())) statsBySection[(size_t) index].clear();
    }

private:
    void timerCallback() override;
    juce::CriticalSection historyLock;
    std::atomic<int> autoPasses { 0 };
    juce::String autoStatus, finishText;
    std::atomic<FinishStage> finishState { FinishStage::Idle };
    float finishMeasuredAt = 0.0f, lastMaskingCheckAt = 0.0f;
    std::map<juce::String, int> maskingCounts;
    juce::StringArray persistentMasking;
    Pass previousPass;
    coach::Thresholds coachLimits;
    std::vector<FixRecord> fixHistory;

    struct AtomicStat
    {
        std::atomic<double> power { 0.0 }, first { 0.0 }, second { 0.0 }, brightFirst { 0.0 }, brightSecond { 0.0 };
        std::atomic<int> blocks { 0 }, firstBlocks { 0 }, secondBlocks { 0 };
        void clear() { power = 0.0; first = 0.0; second = 0.0; brightFirst = 0.0; brightSecond = 0.0; blocks = 0; firstBlocks = 0; secondBlocks = 0; }
    };
    std::array<AtomicStat, suite::SectionMap::maxSections> statsBySection;
    suite::SectionFollower sections;
    suite::SectionMap statsMap;      // audio thread: the map the stats belong to
    uint32_t lastSubBlock = 0;
    double sampleRate = 48000.0;

    std::atomic<bool> resetRequested { false }, loading { false }, cancelLoad { false };
    std::thread loader;
    mutable juce::CriticalSection referenceLock;
    ReferenceProfile reference;
    MySound mySound;
    std::atomic<bool> learningMySound { false };
    std::thread mySoundLoader;
    std::vector<float> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalismProcessor)
};
