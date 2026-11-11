// Offline tests for Lapis. Usage: LapisTests [output-dir]

#include "PluginProcessor.h"
#include "TestUtils.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<LapisProcessor> make (int targetIndex = 1)
    {
        suite::TargetBus::withdraw();
        auto p = std::make_unique<LapisProcessor>();
        set (*p, LapisProcessor::targetId, (float) targetIndex);
        prepare (*p);
        return p;
    }

    /** Pinkish noise with a kick-like pulse at `levelDb` (roughly dBFS RMS). */
    InputFn music (std::function<float (double)> levelDbAt, uint32_t seed = 1)
    {
        auto rng = std::make_shared<juce::Random> ((juce::int64) seed);
        auto lp = std::make_shared<float> (0.0f);
        return [=] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                *lp += 0.1f * (rng->nextFloat() * 2.0f - 1.0f - *lp);
                const auto beat = std::fmod (t, 60.0 / 138.0);
                const auto kick = (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * beat) * std::exp (-beat * 14.0));
                const auto v = juce::Decibels::decibelsToGain (levelDbAt (t)) * (2.5f * *lp + 0.8f * kick);
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
        };
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    section ("Loudness per version");
    {
        auto p = make (1);   // club, -8 LUFS
        render (*p, 0.05, {});
        set (*p, LapisProcessor::targetId, 3.0f);   // streaming, -14 LUFS: not learned yet
        render (*p, 0.05, {});
        check (std::abs (p->getLearnedGainDb() + 6.0f) < 0.01f, "a new target starts 6 dB down from the club gain");
        p->resetLearning();
        render (*p, 0.05, {});
        set (*p, LapisProcessor::targetId, 1.0f);
        render (*p, 0.05, {});
        check (std::abs (p->getLearnedGainDb()) < 0.01f, "back to the club target: its own gain, not a guess");
        set (*p, LapisProcessor::targetId, 3.0f);
        render (*p, 0.05, {});
        check (std::abs (p->getLearnedGainDb()) < 0.01f && ! std::isnan (p->rememberedGainFor (-8.0f)), "and the streaming gain is remembered too");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make (1);
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (q->rememberedGainFor (-14.0f)) < 0.01f && std::abs (q->rememberedGainFor (-8.0f)) < 0.01f, "remembered with the project");
    }

    section ("Master fingerprint");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LapisFingerprints");
        dir.deleteRecursively();
        auto white = [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            juce::Random rng (pos + 3);
            for (int i = 0; i < b.getNumSamples(); ++i) { const auto v = 0.1f * (rng.nextFloat() * 2.0f - 1.0f); b.setSample (0, i, v); b.setSample (1, i, v); }
        };
        auto p = make (1);
        set (*p, LapisProcessor::clipId, 0.8f);
        render (*p, 3.0, white);
        check (! p->saveFingerprint (dir, "Bright"), "needs about 10 s of the master first");
        render (*p, 8.0, white);
        check (p->saveFingerprint (dir, "Bright") && LapisProcessor::loadFingerprints (dir).size() == 1, "saves a master fingerprint");

        auto q = make (1);
        render (*q, 11.0, music ([] (double) { return -20.0f; }));   // a much darker master
        const auto line = q->matchFingerprint (LapisProcessor::loadFingerprints (dir)[0]);
        check (std::abs (q->apvts.getRawParameterValue (LapisProcessor::clipId)->load() - 0.8f) < 0.01f, "matching recalls the chain (clip 80%)");
        check (q->apvts.getRawParameterValue (LapisProcessor::toneHighId)->load() > 1.0f && q->apvts.getRawParameterValue (LapisProcessor::toneLowId)->load() < -0.5f,
               "and moves the tone towards the stored master: brighter, less low end (" + line + ")");
        dir.deleteRecursively();
    }

    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Limiter");
    {
        auto p = make();
        set (*p, LapisProcessor::modeId, (float) LapisProcessor::Manual);
        set (*p, LapisProcessor::clipId, 0.0f);
        juce::AudioBuffer<float> audio;
        render (*p, 0.1, [] (juce::AudioBuffer<float>& b, int64_t pos) { if (pos == 0) { b.setSample (0, 100, 0.1f); b.setSample (1, 100, 0.1f); } }, &audio);
        int peakAt = 0; float peak = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (std::abs (audio.getSample (0, i)) > peak) { peak = std::abs (audio.getSample (0, i)); peakAt = i; }
        check (peakAt - 100 == p->getLatencySamples() && std::abs (peak - 0.1f) < 1.0e-4f,
               "below the ceiling: untouched, delayed by the reported " + juce::String (p->getLatencySamples()) + " samples");

        auto q = make();
        set (*q, LapisProcessor::modeId, (float) LapisProcessor::Manual);
        set (*q, LapisProcessor::manualGainId, 18.0f);
        set (*q, LapisProcessor::ceilingId, -1.0f);
        juce::Random rng (3);
        render (*q, 5.0, [&] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
        }, &audio);
        const auto samplePeak = juce::Decibels::gainToDecibels (peakBetween (audio, 0.0, 5.0));
        const auto tp = q->outputPeak.peakDb();
        check (samplePeak <= -1.0f + 0.01f, "sample peaks never pass the -1 dB ceiling (" + juce::String (samplePeak, 2) + ")");
        check (tp <= -1.0f + 0.3f, "true peak held near the ceiling with +18 dB of drive (" + juce::String (tp, 2) + " dBTP)");
        check (q->maxReductionDb.load() > 10.0f, "and it really was limiting hard (" + juce::String (q->maxReductionDb.load(), 1) + " dB)");
    }

    section ("Auto gain finds the target");
    {
        auto p = make (1);   // Club: -8 LUFS integrated, loud sections at -7
        juce::AudioBuffer<float> audio;
        render (*p, 60.0, music ([] (double) { return -14.0f; }), &audio);
        const auto st = p->outShortTerm.load();
        check (std::abs (st - (-7.0f)) < 1.0f, "loud material lands at the loud-section target (" + juce::String (st, 2) + " LUFS, gain +"
                                                     + juce::String (p->gainDb.load(), 1) + " dB)");
        check (p->settled.load(), "reports 'settled'");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("lapis_auto.wav"), audio);

        // Hold: the gain stays put even if the input gets louder.
        set (*p, LapisProcessor::modeId, (float) LapisProcessor::Hold);
        const auto held = p->gainDb.load();
        render (*p, 10.0, music ([] (double) { return -8.0f; }));
        check (std::abs (p->gainDb.load() - held) < 0.05f, "Hold keeps the learned gain");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make (1);
        q->setStateInformation (state.getData(), (int) state.getSize());
        set (*q, LapisProcessor::modeId, (float) LapisProcessor::Hold);
        render (*q, 0.5, {});
        check (std::abs (q->gainDb.load() - held) < 0.2f, "learned gain is saved with the project");
    }

    section ("Breakdowns are not pushed up");
    {
        auto p = make (1);
        // 30 s drop, 30 s breakdown 10 dB quieter, 30 s drop.
        const auto level = [] (double t) { return t >= 30.0 && t < 60.0 ? -24.0f : -14.0f; };
        float gainAfterDrop = -100.0f, gainAfterBreakdown = -100.0f;
        render (*p, 90.0, music (level, 5), nullptr, nullptr, [&] (int64_t pos)
        {
            const auto t = (double) pos / sampleRate;
            if (gainAfterDrop < -99.0f && t >= 29.9) gainAfterDrop = p->gainDb.load();
            if (gainAfterBreakdown < -99.0f && t >= 59.9) gainAfterBreakdown = p->gainDb.load();
        });
        check (gainAfterDrop > 5.0f && gainAfterDrop < 20.0f, "the drop needed real gain, well inside the range (+" + juce::String (gainAfterDrop, 1) + " dB)");
        check (std::abs (gainAfterBreakdown - gainAfterDrop) < 0.5f, "gain unchanged through a 10 dB quieter breakdown ("
                                                                         + juce::String (gainAfterDrop, 1) + " -> " + juce::String (gainAfterBreakdown, 1) + " dB)");
    }

    section ("Tone shelves (reference match)");
    {
        suite::FixBus::reset();
        auto p = make (1);
        set (*p, LapisProcessor::modeId, (float) LapisProcessor::Manual);
        set (*p, LapisProcessor::manualGainId, 0.0f);
        auto levelAt = [&p] (float hz)
        {
            juce::AudioBuffer<float> out;
            render (*p, 1.0, [hz] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto v = 0.05f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            }, &out);
            return juce::Decibels::gainToDecibels (rmsBetween (out, 0.5, 1.0));
        };
        const auto low0 = levelAt (40.0f), high0 = levelAt (14000.0f), mid0 = levelAt (1000.0f);
        const auto seq = suite::FixBus::send (suite::Fix::ReferenceTone, suite::packTone (2.0f, -1.5f));
        p->fixes->poll();
        check (std::abs (p->apvts.getRawParameterValue (LapisProcessor::toneLowId)->load() - 2.0f) < 0.01f
                   && std::abs (p->apvts.getRawParameterValue (LapisProcessor::toneHighId)->load() + 1.5f) < 0.01f, "Mentalism's amounts land on the shelves");
        const auto low1 = levelAt (40.0f), high1 = levelAt (14000.0f), mid1 = levelAt (1000.0f);
        check (std::abs ((low1 - low0) - 2.0f) < 0.4f && std::abs ((high1 - high0) + 1.5f) < 0.4f && std::abs (mid1 - mid0) < 0.3f,
               "40 Hz up " + juce::String (low1 - low0, 2) + " dB, 14 kHz down " + juce::String (high0 - high1, 2) + " dB, 1 kHz untouched");
        suite::FixBus::sendUndo (seq);
        p->fixes->poll();
        check (std::abs (p->apvts.getRawParameterValue (LapisProcessor::toneLowId)->load()) < 0.01f, "undo flattens them again");
    }

    section ("Follows Mentalism's target");
    {
        auto p = make (LapisProcessor::followMentalism);
        suite::TargetBus::publish (-14.0f, -1.0f);
        render (*p, 0.05, {});
        check (p->followingMentalism.load() && std::abs (p->effectiveTarget.load() + 14.0f) < 0.05f
                   && std::abs (p->effectiveCeiling.load() + 1.0f) < 0.01f, "uses Mentalism's -14 LUFS / -1 dBTP");
        suite::TargetBus::withdraw();
        render (*p, 0.05, {});
        check (! p->followingMentalism.load() && std::abs (p->effectiveTarget.load() + 8.0f) < 0.05f, "falls back to the club target without Mentalism");
    }

    if (outDir != juce::File())
    {
        auto p = make (1);
        render (*p, 8.0, music ([] (double) { return -20.0f; }));
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("lapis.png"));
        editor.reset();
    }

    return finish();
}
