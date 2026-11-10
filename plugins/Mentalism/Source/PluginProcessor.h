#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Advisor.h"
#include "Parameters.h"
#include "Spectrum.h"
#include "Coach.h"
#include "MySound.h"
#include "HostInfo.h"
#include <thread>

/** The analysed shape and loudness of a reference track. */
struct ReferenceProfile
{
    juce::String name;
    float lufs = -100.0f, truePeakDb = -100.0f;
    advisor::Bands bands {};
    bool valid = false;

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
    void resetMeasurements() { resetRequested.store (true); autoPasses.store (0); }

    /** My sound: learns a loudness and tonal target from your finished tracks (in the
        background), saved for every project. */
    void learnMySound (const juce::Array<juce::File>& tracks);
    static MySound analyseMySound (const juce::Array<juce::File>& tracks, const std::atomic<bool>* cancel = nullptr);
    bool isLearningMySound() const { return learningMySound.load(); }
    MySound getMySound() const { const juce::ScopedLock sl (referenceLock); return mySound; }
    void setMySound (const MySound& m) { const juce::ScopedLock sl (referenceLock); mySound = m; }

    /** Starts analysing a reference track in the background. */
    void loadReference (const juce::File&);
    bool isLoadingReference() const { return loading.load(); }
    ReferenceProfile getReference() const;
    void setReference (const ReferenceProfile&);

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
    };
    Report report() const;

    juce::AudioProcessorValueTreeState apvts;
    suite::AudioTap spectrumTap;

    analysis::LoudnessMeter loudness;
    analysis::TruePeakMeter truePeak;
    analysis::StereoMeter stereo;
    analysis::BandAnalyzer bands;

    /** Loudness gathered per section of the current Ouroboros map (any thread). */
    std::vector<coach::SectionStat> sectionStats() const;

    /** Versions: a radio edit is mastered for streaming, an extended mix for the club. */
    bool applyVersion (int kind) override
    {
        setParameterPlain (apvts, Params::loudnessTarget, kind == suite::radioEdit ? 2.0f : 0.0f);
        return true;
    }

    /** New song (Ouroboros): the loudness target for the style. */
    bool applySession (const suite::SessionStyle& style, int, int) override
    {
        setParameterPlain (apvts, Params::loudnessTarget, (float) style.loudnessTarget);
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
    juce::String autoCoachStatus() const { const juce::ScopedLock sl (historyLock); return autoStatus; }

    /** Finish the song: gain staging, DJ intro/outro, reference tone, then (once the drops have
        been heard again) the rough mix, then the auto-coach until the arc is right. */
    enum class FinishStage { Idle, WaitingForDrop, Coaching, Done };
    juce::StringArray finishSong();
    FinishStage finishStage() const { return finishState.load(); }
    juce::String finishStatus() const { const juce::ScopedLock sl (historyLock); return finishText; }
    /** The timer's work (tests call it directly). */
    void runAutomation();

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
    float finishMeasuredAt = 0.0f;
    std::vector<FixRecord> fixHistory;

    struct AtomicStat
    {
        std::atomic<double> power { 0.0 }, first { 0.0 }, second { 0.0 };
        std::atomic<int> blocks { 0 }, firstBlocks { 0 }, secondBlocks { 0 };
        void clear() { power = 0.0; first = 0.0; second = 0.0; blocks = 0; firstBlocks = 0; secondBlocks = 0; }
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
