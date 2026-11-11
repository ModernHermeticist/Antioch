#pragma once

#include "Alias.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "SuiteProcessor.h"
#include "Limiter.h"
#include "Analysis.h"
#include "TargetBus.h"
#include "FixBus.h"
#include "SuiteDSP.h"
#include "HostInfo.h"
#include <map>

class LapisProcessor : public suite::SuiteProcessor
{
public:
    LapisProcessor();
    ~LapisProcessor() override = default;

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

    // Parameters
    static inline const juce::String targetId { "target" }, modeId { "mode" }, ceilingId { "ceiling" }, clipId { "clip" },
                                     releaseId { "release" }, manualGainId { "manualgain" },
                                     toneLowId { "tone_low" }, toneHighId { "tone_high" },
                                     bitDepthId { "bitdepth" };   // 24-bit (no dither) or 16-bit (TPDF dither)   // gentle shelves (100 Hz / 8 kHz); Mentalism's reference match sets them
    enum Mode { Auto = 0, Hold, Manual };
    static constexpr int followMentalism = 0;
    static float targetLufsFor (int index) { static const float t[] = { -8.0f, -8.0f, -6.0f, -14.0f }; return t[juce::jlimit (0, 3, index)]; }
    static float ceilingFor (int index) { static const float c[] = { -0.3f, -0.3f, -0.3f, -1.0f }; return c[juce::jlimit (0, 3, index)]; }

    /** Loud sections are aimed this far above the integrated target: breakdowns pull the
        song's integrated loudness down by roughly this much in typical trance arrangements. */
    static constexpr float loudSectionOffset = 1.0f;

    void resetLearning() { resetRequested.store (true); }
    float getLearnedGainDb() const { return learnedGainDb.load(); }

    /** Auto-loudness per version: the gain learned for each loudness target is remembered (in
        half-LU steps from -24 to -4 LUFS), so switching between the club and streaming versions
        lands straight on the right gain. An unknown target starts from the current gain moved by
        the difference in target. */
    static int targetSlot (float lufs) { return juce::jlimit (0, numTargetSlots - 1, (int) std::lround ((lufs + 24.0f) * 2.0f)); }
    static constexpr int numTargetSlots = 41;
    float rememberedGainFor (float lufs) const { return gainForTarget[(size_t) targetSlot (lufs)].load(); }

    /** Versions: when not following Mentalism, a radio edit aims at streaming, an extended mix at the club. */
    bool applyVersion (int kind) override
    {
        if ((int) apvts.getRawParameterValue (targetId)->load() == followMentalism) return false;   // Mentalism decides
        setParameterPlain (apvts, targetId, kind == suite::radioEdit ? 3.0f : 1.0f);   // extended mix and club edit: club
        resetLearning();
        return true;
    }

    /** Master fingerprint: your master chain (target, ceiling, clip, release, tone) and the
        tonal shape of the finished master, kept as a file. Matching recalls the chain and moves
        the tone shelves half-way towards the stored shape (after ~10 s of the new song). */
    struct MasterFingerprint
    {
        juce::String name;
        std::array<float, analysis::numBands> shape {};
        std::map<juce::String, float> settings;
    };
    analysis::BandAnalyzer outBands;
    static juce::File fingerprintFolder()
    {
        return suite::Alias::file ("Master fingerprints");
    }
    bool fingerprintReady() const { return outBands.longSeconds.load() >= 10.0f; }
    std::array<float, analysis::numBands> measuredShape() const;
    bool saveFingerprint (const juce::File& dir, const juce::String& name);
    static std::vector<MasterFingerprint> loadFingerprints (const juce::File& dir);
    juce::String matchFingerprint (const MasterFingerprint&);

    // For the UI
    std::atomic<float> gainDb { 0.0f }, gainReductionDb { 0.0f }, maxReductionDb { 0.0f };
    std::atomic<float> outShortTerm { -100.0f }, effectiveTarget { -8.0f }, effectiveCeiling { -0.3f };
    std::atomic<bool> followingMentalism { false }, limitedByReduction { false }, settled { false };
    analysis::TruePeakMeter outputPeak;

    /** Export notes: every play-through of 30 s or more (an FL export plays the song through too)
        is measured on its own: integrated loudness, true peak, length, the most gain reduction.
        When it stops, a note goes into <suite folder>/Export notes with the target and the
        dither used, and whether it hit them. */
    struct PassResult { float lufs = -100.0f, truePeakDb = -100.0f, maxGrDb = 0.0f, seconds = 0.0f, targetLufs = -8.0f, ceilingDb = -0.3f; bool dithered = false; };
    static juce::String exportNotes (const PassResult&);
    static juce::File& notesFolderOverride() { static juce::File f; return f; }   // tests
    static juce::File notesFolder() { return notesFolderOverride() != juce::File() ? notesFolderOverride() : suite::Alias::file ("Export notes"); }
    juce::File writeExportNotes();   // message thread; the newest finished pass, if any
    SuiteStatus suiteStatus() override { writeExportNotes(); return {}; }
    std::atomic<bool> passReady { false };
    juce::File lastNotes;

    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<suite::FixReceiver> fixes;   // Mentalism's reference match

private:
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    LookaheadLimiter limiter;
    std::array<suite::Biquad, 2> lowShelf, highShelf;
    float shownLow = 1000.0f, shownHigh = 1000.0f;
    analysis::LoudnessMeter loudness;
    std::atomic<float> learnedGainDb { 0.0f };
    std::array<std::atomic<float>, numTargetSlots> gainForTarget;   // NaN = not learned yet
    float lastTarget = std::numeric_limits<float>::quiet_NaN();
    std::atomic<bool> resetRequested { false };
    float runningMax = -100.0f, appliedGainDb = 0.0f;
    std::array<float, 10> stHistory { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
    int stHistoryPos = 0;   // short-term loudness over the last second (100 ms steps)
    int samplesSinceUpdate = 0, updateInterval = 4800, settledUpdates = 0;
    double sampleRate = 44100.0;

    // The current play-through, for the export notes.
    analysis::LoudnessMeter passLoudness;
    analysis::TruePeakMeter passPeak;
    bool wasPlaying = false;
    int64_t passSamples = 0;
    float passMaxGr = 0.0f;
    PassResult finished;   // written on the audio thread before passReady is set
    juce::Random ditherRng { 0x5eed };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LapisProcessor)
};
