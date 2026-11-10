#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr double maxDelaySeconds = 6.0;
    constexpr double delayGlideSeconds = 0.08; // tape-style glide when times change

    template <typename T>
    T* getParam (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        auto* p = dynamic_cast<T*> (state.getParameter (id));
        jassert (p != nullptr);
        return p;
    }
}

AntiochProcessor::AntiochProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AntiochState", Params::createLayout())
{
    syncParam      = getParam<juce::AudioParameterBool> (apvts, Params::sync);
    rootParam      = getParam<juce::AudioParameterChoice> (apvts, Params::rootNote);
    octaveParam    = getParam<juce::AudioParameterInt> (apvts, Params::rootOct);
    resonanceParam = apvts.getRawParameterValue (Params::resonance);
    resMixParam    = apvts.getRawParameterValue (Params::resMix);
    dampParam      = apvts.getRawParameterValue (Params::damping);
    lowCutParam    = apvts.getRawParameterValue (Params::lowCut);
    mixParam       = apvts.getRawParameterValue (Params::mix);
    outputParam    = apvts.getRawParameterValue (Params::output);

    for (int t = 0; t < Params::numTaps; ++t)
    {
        auto& p = tapParams[(size_t) t];
        p.on       = getParam<juce::AudioParameterBool> (apvts, Params::tapId (t, Params::Tap::on));
        p.division = getParam<juce::AudioParameterChoice> (apvts, Params::tapId (t, Params::Tap::division));
        p.timeMs   = apvts.getRawParameterValue (Params::tapId (t, Params::Tap::timeMs));
        p.pitch    = apvts.getRawParameterValue (Params::tapId (t, Params::Tap::pitch));
        p.feedback = apvts.getRawParameterValue (Params::tapId (t, Params::Tap::feedback));
        p.harmonic = getParam<juce::AudioParameterInt> (apvts, Params::tapId (t, Params::Tap::harmonic));
        p.level    = apvts.getRawParameterValue (Params::tapId (t, Params::Tap::level));
        p.pan      = apvts.getRawParameterValue (Params::tapId (t, Params::Tap::pan));
    }
}

bool AntiochProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out || in == juce::AudioChannelSet::mono();
}

void AntiochProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    const auto maxDelay = (int) std::ceil (maxDelaySeconds * sampleRate) + (int) (0.1 * sampleRate);

    for (auto& tap : taps)
    {
        tap.delay.prepare (maxDelay);
        tap.shifter.prepare (sampleRate);
        tap.comb.prepare (sampleRate);
        tap.damp.reset();
        tap.lowCut.reset();

        for (auto* s : { &tap.gainL, &tap.gainR, &tap.feedback, &tap.enabled })
            s->reset (sampleRate, 0.03);
    }

    for (auto* s : { &dryGain, &wetGain, &resMix })
        s->reset (sampleRate, 0.03);

    firstBlock = true;
}

float AntiochProcessor::targetDelaySamples (int t, bool synced, double bpm) const
{
    const auto& p = tapParams[(size_t) t];
    const auto seconds = synced ? Params::divisionInQuarterNotes (p.division->getIndex()) * 60.0 / bpm
                                : (double) p.timeMs->load() / 1000.0;

    return (float) (juce::jlimit (0.001, maxDelaySeconds, seconds) * sampleRate);
}

void AntiochProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    // ---- Tempo ----
    double bpm = currentBpm.load();
    if (auto* host = getPlayHead())
        if (auto position = host->getPosition())
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;
    bpm = juce::jlimit (20.0, 999.0, bpm);
    currentBpm.store (bpm);

    // ---- Block-rate parameter updates ----
    const auto synced = syncParam->get();
    const auto sr = (float) sampleRate;
    const auto rootMidi = 12 * (octaveParam->get() + 1) + rootParam->getIndex();
    const auto rootHz = (float) juce::MidiMessage::getMidiNoteInHertz (rootMidi);
    const auto resonance = resonanceParam->load();
    const auto dampHz = dampParam->load();
    const auto combDamp = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * dampHz / sr);

    const auto mix = mixParam->load();
    const auto outGain = juce::Decibels::decibelsToGain (outputParam->load());
    dryGain.setTargetValue (juce::jmin (1.0f, 2.0f * (1.0f - mix)) * outGain);
    wetGain.setTargetValue (juce::jmin (1.0f, 2.0f * mix) * outGain);
    resMix.setTargetValue (resMixParam->load());

    std::array<float, Params::numTaps> targetDelay {};

    for (int t = 0; t < Params::numTaps; ++t)
    {
        auto& tap = taps[(size_t) t];
        const auto& p = tapParams[(size_t) t];

        targetDelay[(size_t) t] = targetDelaySamples (t, synced, bpm);
        tap.shifter.setSemitones (p.pitch->load());
        tap.comb.setFrequency (rootHz * (float) p.harmonic->get());
        tap.comb.setFeedback (resonance);
        tap.comb.setDamping (combDamp);
        tap.damp.setCutoff (dampHz, sr);
        tap.lowCut.setCutoff (lowCutParam->load(), sr);
        tap.feedback.setTargetValue (p.feedback->load());
        tap.enabled.setTargetValue (p.on->get() ? 1.0f : 0.0f);

        // Equal-power pan, normalised to unity at centre.
        const auto angle = (p.pan->load() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const auto level = p.level->load() * juce::MathConstants<float>::sqrt2;
        tap.gainL.setTargetValue (std::cos (angle) * level);
        tap.gainR.setTargetValue (std::sin (angle) * level);

        if (firstBlock)
        {
            tap.delaySamples = targetDelay[(size_t) t];
            for (auto* s : { &tap.gainL, &tap.gainR, &tap.feedback, &tap.enabled })
                s->setCurrentAndTargetValue (s->getTargetValue());
        }
    }

    if (firstBlock)
    {
        for (auto* s : { &dryGain, &wetGain, &resMix })
            s->setCurrentAndTargetValue (s->getTargetValue());
        firstBlock = false;
    }

    const auto glide = 1.0f - std::exp (-1.0f / (float) (delayGlideSeconds * sampleRate));

    auto* left  = buffer.getWritePointer (0);
    auto* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto* inRight = numIn > 1 ? buffer.getReadPointer (1) : nullptr;

    // ---- Per-sample processing ----
    for (int n = 0; n < numSamples; ++n)
    {
        const auto inL = left[n];
        const auto inR = inRight != nullptr ? inRight[n] : inL;
        const auto monoIn = 0.5f * (inL + inR);
        const auto blend = resMix.getNextValue();

        float wetL = 0.0f, wetR = 0.0f;

        for (size_t t = 0; t < taps.size(); ++t)
        {
            auto& tap = taps[t];
            const auto enabled = tap.enabled.getNextValue();

            tap.delaySamples += glide * (targetDelay[t] - tap.delaySamples);

            // Echo -> pitch shift -> tuned resonance -> tone shaping
            auto x = tap.delay.read (tap.delaySamples - tap.shifter.latency());
            x = tap.shifter.process (x);
            x += blend * (tap.comb.process (x) - x);
            x = tap.lowCut.highpass (x);
            x = tap.damp.lowpass (x);

            // Feed the processed echo back so each repeat is shifted/tuned again.
            tap.delay.push (monoIn * enabled + dsp::softClip (x * tap.feedback.getNextValue()));

            const auto out = x * enabled;
            wetL += out * tap.gainL.getNextValue();
            wetR += out * tap.gainR.getNextValue();
        }

        const auto dry = dryGain.getNextValue();
        const auto wet = wetGain.getNextValue();

        if (right != nullptr)
        {
            left[n]  = inL * dry + wetL * wet;
            right[n] = inR * dry + wetR * wet;
        }
        else
        {
            left[n] = inL * dry + 0.5f * (wetL + wetR) * wet;
        }
    }
}

juce::AudioProcessorEditor* AntiochProcessor::createEditor()
{
    return new AntiochEditor (*this);
}

void AntiochProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AntiochProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AntiochProcessor();
}
