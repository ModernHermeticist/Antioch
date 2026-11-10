#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostInfo.h"

KybalionProcessor::KybalionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "KybalionState", Params::createLayout())
{
    for (int i = 0; i < Params::numSteps; ++i)
        stepParams[(size_t) i] = apvts.getRawParameterValue (Params::Gate::step (i));
}

bool KybalionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (in != out && in != juce::AudioChannelSet::mono())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void KybalionProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    gate.prepare (sampleRate);
    ducker.prepare (sampleRate);
    scTrigger.prepare (sampleRate);
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (Params::output)));
    position = 0.0;
}

void KybalionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    using namespace Params;

    auto mainIn  = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    const auto numSamples = buffer.getNumSamples();

    if (mainOut.getNumChannels() > mainIn.getNumChannels())
        for (int ch = mainIn.getNumChannels(); ch < mainOut.getNumChannels(); ++ch)
            mainOut.copyFrom (ch, 0, mainIn, 0, 0, numSamples); // mono in -> stereo out

    // ---- Transport ----
    const auto host = suite::HostInfo::read (getPlayHead(), currentBpm.load());
    currentBpm.store (host.bpm);
    if (host.playing && host.hasPpq)
        position = host.ppq;
    const auto qPerSample = host.quartersPerSample (sampleRate);
    const auto msPerQ = 60000.0 / host.bpm;

    // ---- Gate settings ----
    const auto gateEnabled = value (gateOn) > 0.5f;
    {
        TranceGate::Settings s;
        for (int i = 0; i < numSteps; ++i)
            s.steps[(size_t) i] = stepParams[(size_t) i]->load();
        s.stepQ = Gate::rateQuarters ((int) value (Gate::rate));
        s.length = value (Gate::length);
        s.attackMs = value (Gate::attack);
        s.releaseMs = value (Gate::release);
        s.depth = value (Gate::depth);
        s.swing = value (Gate::swing);
        s.stereoOffset = (int) value (Gate::stereo);
        s.filter = value (Gate::filter);
        s.res = value (Gate::res);
        gate.update (s);
    }

    // ---- Duck settings ----
    const auto duckEnabled = value (duckOn) > 0.5f;
    {
        Ducker::Settings s;
        s.depth = value (Duck::depth);
        s.attackMs = value (Duck::attack);
        s.holdMs = value (Duck::hold);
        s.releaseMs = value (Duck::release);
        s.curve = value (Duck::curve);
        s.splitHz = value (Duck::split);
        s.breakdown = value (Duck::breakdown) > 0.5f;
        ducker.update (s);
    }

    auto* scBus = getBus (true, 1);
    const auto scActive = scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0;
    sidechainConnected.store (scActive);
    const auto useSidechain = (int) value (Duck::source) == Duck::Sidechain && scActive;
    const auto scBuffer = scActive ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const auto threshold = juce::Decibels::decibelsToGain (value (Duck::threshold));
    const auto duckRateQ = Duck::rateQuarters ((int) value (Duck::rate));
    const auto nudgeQ = value (Duck::nudge) / msPerQ;
    outGain.setTargetValue (juce::Decibels::decibelsToGain (value (output)));

    auto* left = mainOut.getWritePointer (0);
    auto* right = mainOut.getNumChannels() > 1 ? mainOut.getWritePointer (1) : nullptr;
    float lastMs = 1.0e9f;

    for (int n = 0; n < numSamples; ++n)
    {
        auto l = left[n];
        auto r = right != nullptr ? right[n] : l;

        if (gateEnabled)
            gate.process (position, l, r);

        // Time since the last duck trigger: from the beat grid, or from detected kicks.
        float ms;
        if (useSidechain)
        {
            auto sc = scBuffer.getSample (0, n);
            if (scBuffer.getNumChannels() > 1) sc = 0.5f * (sc + scBuffer.getSample (1, n));
            ms = scTrigger.process (sc, threshold);
        }
        else
        {
            // A negative nudge ducks slightly before the beat (like lookahead, with no latency).
            auto phase = std::fmod (position - nudgeQ, duckRateQ);
            if (phase < 0.0) phase += duckRateQ;
            ms = (float) (phase * msPerQ);
        }
        lastMs = ms;

        if (duckEnabled)
            ducker.process (ms, l, r);

        const auto o = outGain.getNextValue();
        left[n] = l * o;
        if (right != nullptr) right[n] = r * o;

        position += qPerSample;
    }

    gateStep.store (gate.getCurrentStep());
    duckGain.store (duckEnabled ? ducker.getGain() : 1.0f);
    duckMs.store (lastMs);
}

void KybalionProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void KybalionProcessor::applyGatePattern (int index)
{
    const auto& patterns = Params::Gate::patterns();
    if (! juce::isPositiveAndBelow (index, (int) patterns.size()))
        return;
    for (int i = 0; i < Params::numSteps; ++i)
        setParam (Params::Gate::step (i), patterns[(size_t) index].steps[(size_t) i]);
}

void KybalionProcessor::randomiseGate (juce::Random& rng)
{
    // Musical randomness: downbeats stay on, other steps are on ~60% of the time with varied levels.
    for (int i = 0; i < Params::numSteps; ++i)
    {
        const auto on = i % 4 == 0 || rng.nextFloat() < 0.6f;
        setParam (Params::Gate::step (i), on ? (i % 4 == 0 ? 1.0f : 0.5f + 0.5f * rng.nextFloat()) : 0.0f);
    }
}

void KybalionProcessor::applyDuckPreset (int index)
{
    const auto& presets = Params::Duck::presets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;
    const auto& p = presets[(size_t) index];
    setParam (Params::Duck::depth, p.depth);
    setParam (Params::Duck::attack, p.attack);
    setParam (Params::Duck::hold, p.hold);
    setParam (Params::Duck::release, p.release);
    setParam (Params::Duck::curve, p.curve);
    setParam (Params::Duck::split, p.split);
}

juce::AudioProcessorEditor* KybalionProcessor::createEditor()
{
    return new KybalionEditor (*this);
}

void KybalionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KybalionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KybalionProcessor();
}
