#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Advisor.h"
#include "Parameters.h"
#include "Spectrum.h"
#include "Coach.h"
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
    void resetMeasurements() { resetRequested.store (true); }

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

    /** Samples at full scale since the last Reset (for the pre-export checklist). */
    std::atomic<int> clippedSamples { 0 };

    /** Fix history (fixes and rough mixes sent from here), newest last. */
    struct FixRecord { uint32_t seq = 0; suite::Fix fix = suite::Fix::None; juce::Time when; uint32_t undoSeq = 0; };
    uint32_t sendFix (suite::Fix fix)
    {
        const auto seq = suite::FixBus::send (fix);
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

    /** After a fix: forget what was measured for sections of `type`, so the coach re-checks them. */
    void forgetSections (suite::SectionType type, const suite::SectionMap& map)
    {
        for (int i = 0; i < juce::jmin (map.count, (int) statsBySection.size()); ++i)
            if (map.sections[(size_t) i].type == type)
                statsBySection[(size_t) i].clear();
    }

private:
    void timerCallback() override;
    juce::CriticalSection historyLock;
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
    std::vector<float> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MentalismProcessor)
};
