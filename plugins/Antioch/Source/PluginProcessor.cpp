#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostInfo.h"
#include "Music.h"

namespace
{
    constexpr double maxDelaySeconds = 6.0;
}

AntiochProcessor::AntiochProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AntiochState", Params::createLayout())
{
    using namespace Params;
    for (int t = 0; t < numTaps; ++t)
    {
        auto& p = tapPtrs[(size_t) t];
        p.on       = boolParam (tapId (t, Tap::on));
        p.division = choiceParam (tapId (t, Tap::division));
        p.harmonic = intParam (tapId (t, Tap::harmonic));
        p.timeMs   = apvts.getRawParameterValue (tapId (t, Tap::timeMs));
        p.pitch    = apvts.getRawParameterValue (tapId (t, Tap::pitch));
        p.feedback = apvts.getRawParameterValue (tapId (t, Tap::feedback));
        p.level    = apvts.getRawParameterValue (tapId (t, Tap::level));
        p.pan      = apvts.getRawParameterValue (tapId (t, Tap::pan));
        jassert (p.on != nullptr && p.division != nullptr && p.harmonic != nullptr);
    }
}

juce::AudioParameterBool* AntiochProcessor::boolParam (const juce::String& id)
{
    return dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id));
}

juce::AudioParameterChoice* AntiochProcessor::choiceParam (const juce::String& id)
{
    return dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id));
}

juce::AudioParameterInt* AntiochProcessor::intParam (const juce::String& id)
{
    return dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (id));
}

float AntiochProcessor::value (const juce::String& id) const
{
    return apvts.getRawParameterValue (id)->load();
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
    echo.prepare (sampleRate, maxDelaySeconds);
    reverb.prepare (sampleRate);
    throwDelay.prepare (sampleRate, maxDelaySeconds);
    dryEnvelope.reset();

    for (auto* s : { &dryGain, &echoGain, &verbGain, &throwGain, &outGain, &echoSend, &throwSend, &verbDuck, &throwDuck })
        s->reset (sampleRate, 0.03);

    firstBlock = true;
}

void AntiochProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    using namespace Params;

    const auto numSamples = buffer.getNumSamples();
    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto host = suite::HostInfo::read (getPlayHead(), currentBpm.load());
    currentBpm.store (host.bpm);
    const auto sr = (float) sampleRate;

    // ---- Key: follow Correspondence if it's broadcasting, else use local key ----
    auto root = choiceParam (rootNote)->getIndex();
    const auto bus = suite::KeyBus::read();
    const auto follow = boolParam (followKey)->get() && bus.valid;
    if (follow)
        root = bus.root;
    effectiveRoot.store (root);
    followingBus.store (follow);
    busScale.store (bus.scale);

    const auto rootHz = suite::music::midiToHz ((float) (12 * (intParam (rootOct)->get() + 1) + root));
    const auto synced = boolParam (Params::sync)->get();

    // ---- Echo ----
    {
        HarmonicEcho::Settings s;
        s.resonance = value (resonance);
        s.blend     = value (resMix);
        s.dampHz    = value (damping);
        s.lowCutHz  = value (lowCut);

        for (int t = 0; t < numTaps; ++t)
        {
            auto& ts = s.taps[(size_t) t];
            const auto& p = tapPtrs[(size_t) t];
            const auto seconds = synced ? suite::music::divisionQuarters (p.division->getIndex()) * 60.0 / host.bpm
                                        : (double) p.timeMs->load() / 1000.0;
            ts.delaySamples = (float) (juce::jlimit (0.001, maxDelaySeconds, seconds) * sampleRate);
            ts.on          = p.on->get();
            ts.semitones   = p.pitch->load();
            ts.resonatorHz = rootHz * (float) p.harmonic->get();
            ts.feedback    = p.feedback->load();
            ts.level       = p.level->load();
            ts.pan         = p.pan->load();
        }
        echo.update (s);
    }

    // ---- Reverb ----
    {
        EuphoricReverb::Settings s;
        s.decay = value (Verb::decay);
        s.size = value (Verb::size);
        s.predelayMs = value (Verb::predelay);
        s.dampHz = value (Verb::damping);
        s.lowCutHz = value (Verb::lowCut);
        s.highCutHz = value (Verb::highCut);
        s.shimmer = value (Verb::shimmer);
        s.shimmerSemitones = Verb::shimmerSemitones (choiceParam (Verb::shimInt)->getIndex());
        s.width = value (Verb::width);
        s.modulation = value (Verb::mod);
        reverb.update (s);
    }
    dryEnvelope.set (2.0f, value (Verb::duckRel), sr);

    // ---- Throw ----
    {
        ThrowDelay::Settings s;
        s.delaySamples = (float) (suite::music::divisionQuarters (choiceParam (Throw::division)->getIndex()) * 60.0 / host.bpm * sampleRate);
        s.feedback = value (Throw::feedback);
        s.pingPong = value (Throw::pingPong);
        s.lowCutHz = value (Throw::lowCut);
        s.highCutHz = value (Throw::highCut);
        s.drive = value (Throw::drive);
        throwDelay.update (s);
    }

    const auto manualThrow = boolParam (Throw::on)->get();
    const auto autoIndex = choiceParam (Throw::autoMode)->getIndex();
    const auto autoBars = Throw::autoBars (autoIndex);
    const auto autoOn = autoBars > 0 && host.playing && host.hasPpq;
    const auto haveSections = sections.refresh();
    ouroborosFound.store (haveSections);
    const auto sectionThrows = autoIndex == Throw::autoAtSectionEnds && haveSections && host.playing && host.hasPpq;
    const auto barQ = host.barQuarters();
    const auto phraseQ = autoBars * host.barQuarters();
    const auto lengthQ = Throw::autoLengthQuarters (choiceParam (Throw::autoLen)->getIndex());
    const auto windowQ = autoOn ? juce::jmin (phraseQ, lengthQ) : lengthQ;
    const auto qPerSample = host.quartersPerSample (sampleRate);

    // ---- Levels ----
    const auto out = juce::Decibels::decibelsToGain (value (output));
    dryGain.setTargetValue (value (dry));
    echoGain.setTargetValue (value (echoLevel));
    verbGain.setTargetValue (value (verbLevel));
    throwGain.setTargetValue (value (throwLevel));
    outGain.setTargetValue (out);
    echoSend.setTargetValue (value (echoToVerb));
    throwSend.setTargetValue (value (Throw::toVerb));
    verbDuck.setTargetValue (value (Verb::duck));
    throwDuck.setTargetValue (value (Throw::duck));

    if (firstBlock)
    {
        for (auto* s : { &dryGain, &echoGain, &verbGain, &throwGain, &outGain, &echoSend, &throwSend, &verbDuck, &throwDuck })
            s->setCurrentAndTargetValue (s->getTargetValue());
        firstBlock = false;
    }

    auto* left  = buffer.getWritePointer (0);
    auto* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto* inRight = numIn > 1 ? buffer.getReadPointer (1) : nullptr;
    float maxDuck = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto inL = left[n];
        const auto inR = inRight != nullptr ? inRight[n] : inL;
        const auto mono = 0.5f * (inL + inR);

        // Ducking amount follows the dry signal so tails bloom in the gaps.
        const auto env = juce::jmin (1.0f, dryEnvelope.process (mono) * 3.0f);
        const auto vDuck = 1.0f - 0.9f * verbDuck.getNextValue() * env;
        const auto tDuck = 1.0f - 0.9f * throwDuck.getNextValue() * env;
        maxDuck = juce::jmax (maxDuck, 1.0f - vDuck);

        // Echo
        float eL, eR;
        echo.process (mono, eL, eR);
        const auto eg = echoGain.getNextValue();
        eL *= eg; eR *= eg;

        // Throw (manual button/automation or automatic phrase-end window)
        bool gateOpen = manualThrow;
        if (autoOn && ! gateOpen)
        {
            auto pos = std::fmod (host.ppq + qPerSample * n, phraseQ);
            if (pos < 0.0) pos += phraseQ;
            gateOpen = pos >= phraseQ - windowQ;
        }
        if (sectionThrows && ! gateOpen)
        {
            // Throw the last part of every section (into the next one), as marked in Ouroboros.
            const auto posQ = host.ppq + qPerSample * n;
            const auto sec = sections.locate (posQ, barQ);
            gateOpen = sec.valid && posQ >= sec.endQ - juce::jmin (windowQ, sec.endQ - sec.startQ);
        }
        float tL, tR;
        throwDelay.process (inL, inR, gateOpen, tL, tR);
        const auto tg = throwGain.getNextValue() * tDuck;
        tL *= tg; tR *= tg;

        // Reverb gets the dry signal plus sends from the echo and throw sections.
        const auto es = echoSend.getNextValue(), ts = throwSend.getNextValue();
        float rL, rR;
        reverb.process (inL + es * eL + ts * tL, inR + es * eR + ts * tR, vDuck, rL, rR);
        const auto vg = verbGain.getNextValue();

        const auto d = dryGain.getNextValue();
        const auto o = outGain.getNextValue();
        const auto oL = (inL * d + eL + tL + rL * vg) * o;
        const auto oR = (inR * d + eR + tR + rR * vg) * o;

        if (right != nullptr)
        {
            left[n] = oL;
            right[n] = oR;
        }
        else
        {
            left[n] = 0.5f * (oL + oR);
        }
    }

    throwGate.store (throwDelay.gateLevel());
    duckAmount.store (maxDuck);
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
