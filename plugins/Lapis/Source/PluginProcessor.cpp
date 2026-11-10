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
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::toneLowId, 1 }, "Low Tone", NormalisableRange<float> (-6.0f, 6.0f), 0.0f, db));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { LapisProcessor::toneHighId, 1 }, "High Tone", NormalisableRange<float> (-6.0f, 6.0f), 0.0f, db));
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
    for (auto& g : gainForTarget) g.store (std::numeric_limits<float>::quiet_NaN());
    // Mentalism's "Match reference": nudge the tone shelves by the amounts it sends.
    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix, uint32_t data) -> std::vector<suite::FixReceiver::Change>
    {
        if (fix != suite::Fix::ReferenceTone) return {};
        float lowDb = 0.0f, highDb = 0.0f;
        suite::unpackTone (data, lowDb, highDb);
        const auto low = value (toneLowId), high = value (toneHighId);
        return { { toneLowId, juce::jlimit (-6.0f, 6.0f, low + lowDb) }, { toneHighId, juce::jlimit (-6.0f, 6.0f, high + highDb) } };
    });
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
    for (auto* f : { &lowShelf[0], &lowShelf[1], &highShelf[0], &highShelf[1] }) f->reset();
    shownLow = shownHigh = 1000.0f;   // recalculate the shelves for this sample rate
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

    // A different target (a new version): remember this target's gain, recall the new one's.
    if (! std::isnan (lastTarget) && std::abs (target - lastTarget) > 0.01f)
    {
        gainForTarget[(size_t) targetSlot (lastTarget)].store (learnedGainDb.load());
        const auto known = gainForTarget[(size_t) targetSlot (target)].load();
        learnedGainDb.store (std::isnan (known) ? juce::jlimit (-12.0f, 24.0f, learnedGainDb.load() + (target - lastTarget)) : known);
        settledUpdates = 0;
        runningMax = -100.0f;
    }
    lastTarget = target;
    effectiveCeiling.store (ceilingDb);
    followingMentalism.store (follow);

    const auto ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    limiter.setCeiling (ceiling);
    limiter.setRelease (value (releaseId));

    const auto mode = (int) value (modeId);
    const auto wantedGainDb = mode == Manual ? value (manualGainId) : learnedGainDb.load();
    const auto clip = value (clipId);
    const auto clipThreshold = ceiling;

    // Tone shelves (only recalculated when they change; bypassed at 0 dB).
    const auto toneLow = value (toneLowId), toneHigh = value (toneHighId);
    if (std::abs (toneLow - shownLow) > 1.0e-3f || std::abs (toneHigh - shownHigh) > 1.0e-3f)
    {
        for (auto& f : lowShelf) f.setLowShelf (100.0f, toneLow, (float) sampleRate);
        for (auto& f : highShelf) f.setHighShelf (8000.0f, toneHigh, (float) sampleRate);
        shownLow = toneLow; shownHigh = toneHigh;
    }
    const auto toneOn = std::abs (toneLow) > 0.01f || std::abs (toneHigh) > 0.01f;

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);
    float maxGr = 0.0f;

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        // Smooth gain changes (they come in small steps from the learning loop).
        appliedGainDb += 0.0005f * (wantedGainDb - appliedGainDb);
        const auto g = juce::Decibels::decibelsToGain (appliedGainDb);
        auto l = L[n] * g, r = R[n] * g;
        if (toneOn)
        {
            l = highShelf[0].process (lowShelf[0].process (l));
            r = highShelf[1].process (lowShelf[1].process (r));
        }

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
    juce::StringArray perTarget;
    for (int i = 0; i < numTargetSlots; ++i)
        if (const auto g = gainForTarget[(size_t) i].load(); ! std::isnan (g))
            perTarget.add (juce::String (i) + ":" + juce::String (g, 2));
    state.setProperty ("gainPerTarget", perTarget.joinIntoString (";"), nullptr);
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
            for (auto& g : gainForTarget) g.store (std::numeric_limits<float>::quiet_NaN());
            for (const auto& item : juce::StringArray::fromTokens (state["gainPerTarget"].toString(), ";", ""))
                if (const auto k = item.upToFirstOccurrenceOf (":", false, false).getIntValue(); juce::isPositiveAndBelow (k, numTargetSlots))
                    gainForTarget[(size_t) k].store (item.fromFirstOccurrenceOf (":", false, false).getFloatValue());
            lastTarget = std::numeric_limits<float>::quiet_NaN();
            appliedGainDb = learnedGainDb.load();
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LapisProcessor();
}
