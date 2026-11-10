#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        using Attr = AudioParameterFloatAttributes;
        AudioProcessorValueTreeState::ParameterLayout l;
        auto db = Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });

        l.add (std::make_unique<AudioParameterChoice> (ParameterID { LapisProcessor::targetId, 1 }, "Target",
                                                       StringArray { "Follow Mentalism", "Club (-8 LUFS)", "Loud club (-6 LUFS)", "Streaming (-14 LUFS)" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { LapisProcessor::modeId, 1 }, "Gain Mode",
                                                       StringArray { "Auto", "Hold", "Manual" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::ceilingId, 1 }, "Ceiling (own target)",
                                                      NormalisableRange<float> (-3.0f, 0.0f), -0.3f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dBTP"; })));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::clipId, 1 }, "Clip", NormalisableRange<float> (0.0f, 1.0f), 0.3f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));
        NormalisableRange<float> rel (10.0f, 500.0f);
        rel.setSkewForCentre (80.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::releaseId, 1 }, "Release", rel, 80.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::manualGainId, 1 }, "Manual Gain",
                                                      NormalisableRange<float> (-6.0f, 24.0f), 0.0f, db));
        return l;
    }
}

LapisProcessor::LapisProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LapisState", layout())
{
}

bool LapisProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void LapisProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    limiter.prepare (sampleRate);
    setLatencySamples (limiter.getLatency());
    loudness.prepare (sampleRate);
    outputPeak.reset();
    updateInterval = juce::jmax (1, (int) (sampleRate * 0.1));
    samplesSinceUpdate = 0;
    appliedGainDb = 0.0f;
}

void LapisProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (resetRequested.exchange (false))
    {
        learnedGainDb.store (0.0f);
        runningMax = -100.0f;
        settledUpdates = 0;
        stHistory.fill (-100.0f);
        maxReductionDb.store (0.0f);
        outputPeak.reset();
        loudness.reset();
    }

    // ---- Target: from Mentalism if it's running and chosen, else our own ----
    const auto targetIndex = (int) value (targetId);
    const auto shared = suite::TargetBus::read();
    const auto follow = targetIndex == followMentalism && shared.valid;
    const auto target = follow ? shared.lufs : targetLufsFor (targetIndex);
    const auto ceilingDb = follow ? shared.ceilingDbtp : value (ceilingId);
    effectiveTarget.store (target);
    effectiveCeiling.store (ceilingDb);
    followingMentalism.store (follow);

    const auto ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    limiter.setCeiling (ceiling);
    limiter.setRelease (value (releaseId));

    const auto mode = (int) value (modeId);
    const auto wantedGainDb = mode == Manual ? value (manualGainId) : learnedGainDb.load();
    const auto clip = value (clipId);
    const auto clipThreshold = ceiling;

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    float maxGr = 0.0f;

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        // Smooth gain changes (they come in small steps from the learning loop).
        appliedGainDb += 0.0005f * (wantedGainDb - appliedGainDb);
        const auto g = juce::Decibels::decibelsToGain (appliedGainDb);
        auto l = L[n] * g, r = R[n] * g;

        // Soft clipper: shaves the sharpest peaks so the limiter works less hard.
        if (clip > 0.0f)
        {
            l += clip * (clipThreshold * std::tanh (l / clipThreshold) - l);
            r += clip * (clipThreshold * std::tanh (r / clipThreshold) - r);
        }

        limiter.process (l, r);

        // Final safety: never exceed the ceiling by more than rounding.
        L[n] = juce::jlimit (-ceiling, ceiling, l);
        R[n] = juce::jlimit (-ceiling, ceiling, r);

        loudness.process (L[n], R[n]);
        outputPeak.process (L[n], R[n]);
        maxGr = std::max (maxGr, -juce::Decibels::gainToDecibels (limiter.getGain()));

        // ---- Learning loop, every 100 ms: aim loud sections at target + offset ----
        if (++samplesSinceUpdate >= updateInterval)
        {
            samplesSinceUpdate = 0;
            const auto st = loudness.shortTerm.load();
            runningMax = std::max (runningMax - 0.005f, st);     // forgets very slowly (~0.5 LU per 10 s)

            // Only learn from steady loud material: within 1 LU of the loudest so far and not
            // falling. (Short-term loudness takes ~3 s to fall at the start of a breakdown;
            // learning during that slide would push the gain up.)
            const auto secondAgo = stHistory[(size_t) stHistoryPos];
            stHistory[(size_t) stHistoryPos] = st;
            stHistoryPos = (stHistoryPos + 1) % (int) stHistory.size();
            const auto falling = st < secondAgo - 0.3f;
            const auto loud = st > -50.0f && st > runningMax - 1.0f && ! falling;
            const auto heavyLimiting = gainReductionDb.load() > 8.0f;
            limitedByReduction.store (heavyLimiting);

            if (mode == Auto && loud)
            {
                const auto error = (target + loudSectionOffset) - st;
                auto step = juce::jlimit (-0.2f, 0.2f, error * 0.08f);
                if (heavyLimiting && step > 0.0f) step = 0.0f;   // don't crush the master to chase loudness
                learnedGainDb.store (juce::jlimit (-12.0f, 24.0f, learnedGainDb.load() + step));
                settledUpdates = std::abs (error) < 0.5f ? settledUpdates + 1 : 0;
            }
            settled.store (settledUpdates > 100);   // ~10 s of loud material on target
        }
    }

    gainDb.store (appliedGainDb);
    gainReductionDb.store (maxGr);
    if (maxGr > maxReductionDb.load()) maxReductionDb.store (maxGr);
    outShortTerm.store (loudness.shortTerm.load());
}

juce::AudioProcessorEditor* LapisProcessor::createEditor()
{
    return new LapisEditor (*this);
}

void LapisProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("learnedGain", learnedGainDb.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LapisProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            learnedGainDb.store ((float) state.getProperty ("learnedGain", 0.0f));
            appliedGainDb = learnedGainDb.load();
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LapisProcessor();
}
