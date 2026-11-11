#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostInfo.h"

KybalionProcessor::KybalionProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "KybalionState", Params::createLayout())
{
    for (int i = 0; i < Params::numSteps; ++i)
        stepParams[(size_t) i] = apvts.getRawParameterValue (Params::Gate::step (i));

    using T = suite::SectionType;
    scenes.setup (apvts,
                  { { Params::Gate::depth, "Gate depth" }, { Params::Gate::length, "Gate length" }, { Params::Gate::filter, "Gate filter" },
                    { Params::Gate::swing, "Gate swing" }, { Params::Duck::depth, "Duck depth" } },
                  { { T::Intro, sGateDepth, -0.3f }, { T::Breakdown, sGateDepth, -0.6f }, { T::Breakdown, sGateFilter, 0.2f },
                    { T::Build, sGateLength, -0.25f }, { T::Build, sGateFilter, 0.3f },
                    { T::Drop, sGateDepth, 0.2f }, { T::Outro, sGateDepth, -0.3f },
                    { T::count, sGateDepth, 0.1f }, { T::count, sDuckDepth, 0.05f } });   // Drop 2+
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
    scenes.prepare();
    ducker.prepare (sampleRate);
    scTrigger.prepare (sampleRate);
    kick.prepare (sampleRate);
    kickLearner.prepare (sampleRate);
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
    scenes.beginBlock (host, sampleRate, numSamples);
    const auto msPerQ = 60000.0 / host.bpm;

    // ---- Gate settings ----
    const auto gateEnabled = value (gateOn) > 0.5f;
    {
        TranceGate::Settings s;
        for (int i = 0; i < numSteps; ++i)
            s.steps[(size_t) i] = stepParams[(size_t) i]->load();
        s.stepQ = Gate::rateQuarters ((int) value (Gate::rate));
        s.length = scenes.plain (sGateLength);
        s.attackMs = value (Gate::attack);
        s.releaseMs = value (Gate::release);
        s.depth = scenes.plain (sGateDepth);
        s.swing = juce::jmax (scenes.plain (sSwing), suiteSwing.load());   // Correspondence's groove, unless the gate swings more
        s.stereoOffset = (int) value (Gate::stereo);
        s.filter = scenes.plain (sGateFilter);
        s.res = value (Gate::res);
        gate.update (s);
    }

    // ---- Duck settings ----
    const auto duckEnabled = value (duckOn) > 0.5f;
    {
        Ducker::Settings s;
        s.depth = scenes.plain (sDuckDepth);
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
    const auto useSuiteKick = (int) value (Duck::source) == Duck::SuiteKick;
    const auto sharing = value (Duck::shareKick) > 0.5f && host.playing && host.hasPpq;
    const auto suiteKick = useSuiteKick ? suite::KickBus::read() : suite::KickPatternSet {};
    suiteKickAvailable.store (suiteKick.valid);
    if (! sharing) kickLearner.interrupt();
    const auto scBuffer = scActive ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const auto threshold = juce::Decibels::decibelsToGain (value (Duck::threshold));
    const auto duckRateQ = Duck::rateQuarters ((int) value (Duck::rate));
    const auto nudgeQ = value (Duck::nudge) / msPerQ;
    outGain.setTargetValue (juce::Decibels::decibelsToGain (value (output)));

    const auto haveSections = sections.refresh();
    const auto followSections = value (Params::follow) > 0.5f && haveSections && host.playing && host.hasPpq;
    const auto barQ = host.barQuarters();
    const auto fillWhen = (int) value (Params::Fill::when), fillStyle = (int) value (Params::Fill::style);
    const auto chordMode = (int) value (Params::Fill::chords);
    const auto chordQ = (double) suiteChordQ.load();
    const auto stepQNow = Params::Gate::rateQuarters ((int) value (Params::Gate::rate));
    fillNow = 0;
    int sectionNow = -1;
    bool pausedNow = false;

    auto* left = mainOut.getWritePointer (0);
    auto* right = mainOut.getNumChannels() > 1 ? mainOut.getWritePointer (1) : nullptr;
    float lastMs = 1.0e9f;

    for (int n = 0; n < numSamples; ++n)
    {
        auto l = left[n];
        auto r = right != nullptr ? right[n] : l;

        // On the kick track: learn where the kick hits (from this track's own audio, before any processing).
        int typeHere = -1;
        if ((sharing || useSuiteKick) && haveSections)
        {
            const auto sec = sections.locate (position, barQ);
            typeHere = sec.valid ? (int) sec.type : -1;
        }
        if (sharing)
            kickLearner.process (0.5f * (l + r), position, barQ, typeHere, threshold);

        if (gateEnabled)
        {
            // Fills: the last bar before a drop (and a breakdown) plays a fill instead of the pattern.
            int fill = TranceGate::NoFill;
            double fillQ = 0.0;
            if (followSections && fillWhen != Params::Fill::Off)
            {
                const auto sec = sections.locate (position, barQ);
                const auto intoDrop = sec.hasNext && sec.next == suite::SectionType::Drop;
                const auto intoBreakdown = sec.hasNext && sec.next == suite::SectionType::Breakdown && fillWhen == Params::Fill::BeforeDropsAndBreakdowns;
                if (sec.valid && (intoDrop || intoBreakdown) && position >= sec.endQ - barQ && sec.endQ - sec.startQ > barQ + 1.0e-6)
                {
                    fillQ = position - (sec.endQ - barQ);
                    if (fillStyle > 0) fill = fillStyle;
                    else if (intoBreakdown) fill = TranceGate::HalfTime;
                    else fill = (sec.index / 2) % 2 == 0 ? TranceGate::Roll : TranceGate::Gap;   // Auto: alternate into drops
                }
            }
            fillNow = fill;

            // Chord changes (from Correspondence): accent the step on each change, or restart the
            // pattern there. Changes are counted from the section start, as Correspondence writes them.
            float accent = 0.0f;
            auto gatePos = position;
            if (chordMode != Params::Fill::ChordsOff && chordQ > 0.0 && fill == TranceGate::NoFill)
            {
                double origin = 0.0;
                if (followSections)
                    if (const auto sec = sections.locate (position, barQ); sec.valid) origin = sec.startQ;
                const auto since = std::fmod (position - origin, chordQ);
                const auto into = since < 0.0 ? since + chordQ : since;
                if (chordMode == Params::Fill::ChordsRestart)
                    gatePos = into;
                else if (into < stepQNow)
                {
                    accent = 1.0f;
                    if (! inChordAccent) chordAccents.fetch_add (1);
                }
                inChordAccent = accent > 0.0f;
            }
            gate.process (gatePos, l, r, fill, fillQ, barQ, accent);
        }

        // Time since the last duck trigger: from the beat grid, or from detected kicks.
        float ms;
        float sc = 0.0f;
        if (scActive)
        {
            sc = scBuffer.getSample (0, n);
            if (scBuffer.getNumChannels() > 1) sc = 0.5f * (sc + scBuffer.getSample (1, n));
            kick.process (sc, threshold);
        }
        if (useSidechain)
        {
            ms = scTrigger.process (sc, threshold);
        }
        else if (useSuiteKick && suiteKick.valid)
        {
            // Duck on the shared kick pattern for this section (the nudge still applies).
            auto phase = std::fmod (position - nudgeQ, barQ);
            if (phase < 0.0) phase += barQ;
            ms = suiteKick.msSinceHit (typeHere, phase, barQ, msPerQ);
        }
        else
        {
            // A negative nudge ducks slightly before the beat (like lookahead, with no latency).
            auto phase = std::fmod (position - nudgeQ, duckRateQ);
            if (phase < 0.0) phase += duckRateQ;
            ms = (float) (phase * msPerQ);
        }
        // Ouroboros: no kick in breakdowns and builds, so don't duck there.
        if (followSections)
        {
            const auto sec = sections.locate (position, barQ);
            sectionNow = sec.valid ? (int) sec.type : -1;
            if (sec.valid && suite::isKicklessSection (sec.type))
            {
                ms = 1.0e9f;   // far past the release: no gain reduction
                pausedNow = true;
            }
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
    currentFill.store (fillNow);
    duckGain.store (duckEnabled ? ducker.getGain() : 1.0f);
    duckMs.store (lastMs);
    currentSection.store (haveSections ? sectionNow : -1);
    autoBreakdown.store (pausedNow);
}

KybalionProcessor::SuiteStatus KybalionProcessor::suiteStatus()
{
    SuiteStatus s;
    const auto source = (int) value (Params::Duck::source);
    if (source == Params::Duck::Sidechain) s.flags |= suite::Status::sidechainWanted;
    if (sidechainConnected.load()) s.flags |= suite::Status::sidechainConnected;
    if (value (Params::follow) > 0.5f) s.flags |= suite::Status::followsOuroboros;
    if ((int) value (Params::Fill::when) != Params::Fill::Off && value (Params::gateOn) > 0.5f) s.flags |= suite::Status::fillsOn;
    if (value (Params::Duck::shareKick) > 0.5f)
    {
        s.flags |= suite::Status::kickSource;
        if (kickLearner.patterns().valid)
            suite::KickBus::publish (kickLearner.patterns());   // keep the shared pattern fresh while stopped
    }
    s.metrics[0] = (float) source;
    float swing = 0.0f;
    for (const auto& e : suite::Registry::list())
        if (e.plugin == "Correspondence") swing = std::max (swing, e.metrics[0]);
    suiteSwing.store (swing);
    float chordQ = 0.0f;
    for (const auto& e : suite::Registry::list())
        if (e.plugin == "Correspondence" && e.metrics[1] > 0.0f) chordQ = e.metrics[1];
    suiteChordQ.store (chordQ);
    s.metrics[1] = suiteKickAvailable.load() ? 1.0f : 0.0f;
    return s;
}

void KybalionProcessor::trackNameChanged (const juce::String& name)
{
    // Auto-role: a Kybalion on a track called "Kick" shares its kick with the suite.
    const auto n = name.toLowerCase();
    if ((n.contains ("kick") || n.startsWith ("bd")) && value (Params::Duck::shareKick) < 0.5f)
    {
        auto apply = [] (KybalionProcessor& p)
        {
            p.setParam (Params::Duck::shareKick, 1.0f);
            p.setParam (Params::duckOn, 0.0f);   // the kick itself doesn't duck
        };
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            apply (*this);
        else
            juce::MessageManager::callAsync ([safe = juce::WeakReference<KybalionProcessor> (this), apply]
            {
                if (auto* p = safe.get()) apply (*p);
            });
    }
}

bool KybalionProcessor::fitToKick()
{
    if (kick.hits() < kicksNeeded)
        return false;
    // Hold through the kick's punch, then recover as its tail dies away.
    const auto length = kick.lengthMs();
    setParam (Params::Duck::hold, juce::jlimit (0.0f, 200.0f, 0.15f * length));
    setParam (Params::Duck::release, juce::jlimit (20.0f, 1000.0f, 0.85f * length));
    return true;
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

void KybalionProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    scenes.writeInto (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KybalionProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            scenes.readFrom (state);
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KybalionProcessor();
}
