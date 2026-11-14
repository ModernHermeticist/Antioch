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

    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix) -> std::vector<suite::FixReceiver::Change>
    {
        std::vector<suite::FixReceiver::Change> fit;
        if (fix == suite::Fix::KickFit && kick.hits() >= kicksNeeded)
            fit = { { Params::Duck::hold, juce::jlimit (0.0f, 200.0f, 0.15f * kick.lengthMs()) },
                    { Params::Duck::release, juce::jlimit (20.0f, 1000.0f, 0.85f * kick.lengthMs()) } };
        if (fix == suite::Fix::KickFit && overlapDb() > -90.0f)
            fit.push_back ({ Params::Duck::depth, depthForOverlap (overlapDb()) });   // as deep as the collision needs
        if (fix == suite::Fix::KickFit) return fit;
        if (fix == suite::Fix::MorePump && value (Params::duckOn) > 0.5f)   // mood: more pump
            return { { Params::Duck::depth, juce::jmin (1.0f, value (Params::Duck::depth) + 0.1f) } };
        if (fix == suite::Fix::PhaseFlip && phaseMeasured.load() && lowCorrelation.load() < -0.3f)
            return { { Params::Duck::flip, value (Params::Duck::flip) > 0.5f ? 0.0f : 1.0f } };
        return {};
    });
    fixes->setCustom ([this] (suite::Fix fix, uint32_t data) -> std::function<void()>
    {
        uint32_t low; int feature;
        suite::unpackMixFix (data, low, feature);
        if (fix == suite::Fix::MixFixOff && low == (getInstanceId() & 0xfffu)) return mixLatch.switchOff (feature);
        if (fix == suite::Fix::MixFixReset) { autoFitted = false; return mixLatch.resetAll(); }
        return {};
    });

    // Your favourite gate pattern, for a new Kybalion (a saved project's own pattern replaces it as it loads).
    const auto favourites = favouritePatterns (suite::StyleLog::read());
    if (! favourites.empty() && favourites.front().second >= 2)
    {
        for (int i = 0; i < Params::numSteps; ++i)
            if (auto* p = apvts.getParameter (Params::Gate::step (i))) p->setValueNotifyingHost (p->convertTo0to1 (favourites.front().first[(size_t) i]));
        startedOnFavourite = true;
    }
    startSteps = currentSteps();
}

juce::String KybalionProcessor::stepsText (const Steps& s)
{
    juce::StringArray v;
    for (auto x : s) v.add (juce::String (x, 2));
    return v.joinIntoString (",");
}

KybalionProcessor::Steps KybalionProcessor::stepsFromText (const juce::String& text)
{
    Steps s {};
    const auto v = juce::StringArray::fromTokens (text, ",", "");
    for (int i = 0; i < Params::numSteps && i < v.size(); ++i) s[(size_t) i] = juce::jlimit (0.0f, 1.0f, v[i].getFloatValue());
    return s;
}

juce::String KybalionProcessor::trackKind (const juce::String& trackName)
{
    const auto n = trackName.toLowerCase();
    auto any = [&n] (std::initializer_list<const char*> words) { for (auto* w : words) if (n.contains (w)) return true; return false; };
    if (any ({ "vox", "vocal", "voc", "acapella" })) return "vocals";
    if (any ({ "bass", "sub" })) return "bass";
    if (any ({ "pluck", "stab", "arp" })) return "plucks";
    if (any ({ "lead", "saw", "synth" })) return "leads";
    if (any ({ "pad", "chord", "string", "keys", "piano" })) return "pads";
    return n.isEmpty() ? juce::String() : juce::String ("other");
}

std::vector<std::pair<KybalionProcessor::Steps, int>> KybalionProcessor::favouritePatterns (const std::vector<suite::StyleLog::Entry>& entries, const juce::String& kind)
{
    std::map<juce::String, int> counts;
    for (const auto& e : entries)
        if (e.kind == "gate" && e.get ("steps").isNotEmpty() && (kind.isEmpty() || e.get ("track") == kind))
            ++counts[e.get ("steps")];
    std::vector<std::pair<juce::String, int>> sorted (counts.begin(), counts.end());
    std::stable_sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::pair<Steps, int>> out;
    for (const auto& [text, n] : sorted) out.push_back ({ stepsFromText (text), n });
    return out;
}

KybalionProcessor::Steps KybalionProcessor::currentSteps() const
{
    Steps s {};
    for (int i = 0; i < Params::numSteps; ++i) s[(size_t) i] = value (Params::Gate::step (i));
    return s;
}

void KybalionProcessor::logPatternOnce()
{
    if (! reachedFinalDrop.load() || value (Params::gateOn) < 0.5f || patternLogged.exchange (true)) return;
    suite::StyleLog::add ("gate", { { "steps", stepsText (currentSteps()) }, { "track", kind } });
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
    ownKick.prepare (sampleRate);
    overlapLow.set (120.0f, 0.707f, (float) sampleRate);
    overlapLow.reset();
    overlapPower = 0.0;
    overlapSamples = 0;
    outGain.reset (sampleRate, 0.03);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (Params::output)));
    position = 0.0;
}

void KybalionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Level-neutral scenes: measure what comes in, process, then bring the level back to the drop's (when it's on).
    const auto n = buffer.getNumSamples();
    const auto inPower = suite::SceneEngine::blockPower (buffer, n);
    processSuiteBlock (buffer, midi);
    const auto g = scenes.levelKeepGain (inPower, suite::SceneEngine::blockPower (buffer, n));
    if (std::abs (g - 1.0f) > 1.0e-4f) buffer.applyGain (0, n, g);
}

void KybalionProcessor::processSuiteBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
        // The energy curve (Ouroboros): where it's drawn up the gate cuts deeper, where it's drawn down, softer
        if (sections.refresh() && host.hasPpq)
            if (const auto at = sections.locate (host.ppq, host.barQuarters()); at.valid)
                s.depth = juce::jlimit (0.0f, 1.0f, s.depth * juce::jlimit (0.6f, 1.3f, at.energyRatio));
        gateDepthNow.store (s.depth);
        // Dynamic gate: through each drop the gate starts at 60% of its depth and reaches full
        // depth by the end, so the drop keeps intensifying; elsewhere it follows the energy curve.
        if (value (Gate::dynamic) > 0.5f && value (Params::follow) > 0.5f && host.playing && host.hasPpq && sections.refresh())
            if (const auto sec = sections.locate (host.ppq, host.barQuarters()); sec.valid)
            {
                const auto scale = sec.type == suite::SectionType::Drop ? 0.6f + 0.4f * (float) juce::jlimit (0.0, 1.0, sec.progress (host.ppq))
                                                                         : juce::jlimit (0.3f, 1.0f, sec.energyRatio);
                s.depth *= scale;
                dynamicDepth.store (scale);
            }
        s.swing = juce::jmax (scenes.plain (sSwing), suiteSwing.load());   // Correspondence's groove, unless the gate swings more
        s.stereoOffset = (int) value (Gate::stereo);
        s.panMode = (int) value (Gate::pan);
        s.filter = scenes.plain (sGateFilter);
        s.res = value (Gate::res);
        gate.update (s);
    }

    // ---- Duck settings ----
    const auto duckEnabled = value (duckOn) > 0.5f;
    {
        Ducker::Settings s;
        s.depth = scenes.plain (sDuckDepth);
        if (pumpPerSection.load() && host.hasPpq && sections.refresh())
            if (const auto sec = sections.locate (host.ppq, host.barQuarters()); sec.valid)
                s.depth = juce::jlimit (0.0f, 1.0f, s.depth * pumpScale ((int) sec.type));
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
    // Kick and bass: duck around the kick's note, if chosen (or Auto decided so)
    const auto body = suiteKick.valid ? suiteKick.bodyHz : 0.0f;
    kickBodyHz.store (body);
    const auto focusMode = (int) value (Duck::focus);
    ducker.setFocus (body > 25.0f && (focusMode == Duck::FocusKickNote || (focusMode == Duck::FocusAuto && focusAuto.load())) ? body : 0.0f);
    if (body > 25.0f && std::abs (body - kickNoteBandHz) > 0.5f) { kickNoteBand.set (body, 1.2f, (float) getSampleRate()); kickNoteBand.reset(); kickNoteBandHz = body; kickNotePower = lowPower = 0.0; }
    if (! sharing) kickLearner.interrupt();
    const auto scBuffer = scActive ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const auto threshold = juce::Decibels::decibelsToGain (value (Duck::threshold));
    const auto duckRateQ = Duck::rateQuarters ((int) value (Duck::rate));
    const auto nudgeQ = value (Duck::nudge) / msPerQ;
    outGain.setTargetValue (juce::Decibels::decibelsToGain (value (output)));

    const auto haveSections = sections.refresh();
    const auto followSections = value (Params::follow) > 0.5f && haveSections && host.playing && host.hasPpq;
    if (haveSections && host.playing && host.hasPpq)
        if (const auto sec = sections.locate (host.ppq, host.barQuarters()); sec.valid && sec.type == suite::SectionType::Drop
                                                                             && sec.index == sections.getMap().lastDropIndex())
            reachedFinalDrop.store (true);   // the song was played to its final drop: the pattern was kept
    const auto barQ = host.barQuarters();
    const auto fillWhen = (int) value (Params::Fill::when), fillStyle = (int) value (Params::Fill::style);
    const auto chordMode = (int) value (Params::Fill::chords);
    const auto chordQ = (double) suiteChordQ.load();
    const auto stepQNow = Params::Gate::rateQuarters ((int) value (Params::Gate::rate));
    fillNow = 0;
    // The suite director: when its plan is live, the fills are its call (and its silences close the gate)
    const auto directorLive = suiteDirector.refreshFor (getInstanceId()) && host.playing && host.hasPpq;
    const auto silenceStep = (float) (1.0 / (0.003 * sampleRate));
    int dirIdx = -1, dirSilence = -1;
    int sectionNow = -1;
    bool pausedNow = false;

    auto* left = mainOut.getWritePointer (0);
    auto* right = mainOut.getNumChannels() > 1 ? mainOut.getWritePointer (1) : nullptr;
    float lastMs = 1.0e9f;

    // Kick and bass phase (sidechain kick): the low end of this track against the kick, while it sounds.
    const auto flip = value (Params::Duck::flip) > 0.5f;
    int phaseType = -1;
    if (host.hasPpq && sections.refresh())
        if (const auto sec = sections.locate (host.ppq, host.barQuarters()); sec.valid) phaseType = juce::jlimit (0, 5, (int) sec.type);
    const auto sectionFlipNow = phasePerSection.load() && phaseType >= 0 && sectionFlip[(size_t) phaseType].load();
    const auto polTarget = (flip != sectionFlipNow) ? -1.0f : 1.0f;
    if (scActive)
    {
        const auto lowCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 120.0f / (float) sampleRate);
        for (int n = 0; n < numSamples; ++n)
        {
            auto m = 0.5f * (left[n] + (right != nullptr ? right[n] : left[n]));
            if (flip) m = -m;
            auto k = scBuffer.getSample (0, n);
            if (scBuffer.getNumChannels() > 1) k = 0.5f * (k + scBuffer.getSample (1, n));
            phaseLowM += lowCoeff * (m - phaseLowM);
            phaseLowS += lowCoeff * (k - phaseLowS);
            phaseEnv = std::max (std::abs (phaseLowS), phaseEnv * 0.9995f);
            if (phaseEnv > 0.02f)
            {
                constexpr double decay = 0.99997;   // a few seconds of memory
                phaseMS = phaseMS * decay + (double) phaseLowM * phaseLowS;
                phaseMM = phaseMM * decay + (double) phaseLowM * phaseLowM;
                phaseSS = phaseSS * decay + (double) phaseLowS * phaseLowS;
                if (phaseType >= 0)
                {
                    secMS[(size_t) phaseType] = secMS[(size_t) phaseType] * decay + (double) phaseLowM * phaseLowS;
                    secMM[(size_t) phaseType] = secMM[(size_t) phaseType] * decay + (double) phaseLowM * phaseLowM;
                    secSS[(size_t) phaseType] = secSS[(size_t) phaseType] * decay + (double) phaseLowS * phaseLowS;
                }
            }
        }
        if (phaseMM > 1.0e-3 && phaseSS > 1.0e-3)
        {
            lowCorrelation.store ((float) (phaseMS / std::sqrt (phaseMM * phaseSS)));
            phaseMeasured.store (true);
        }
    }

    for (int n = 0; n < numSamples; ++n)
    {
        polGain += 0.005f * (polTarget - polGain);   // a polarity change cross-fades over a few ms
        auto l = polGain * left[n];
        auto r = polGain * (right != nullptr ? right[n] : left[n]);
        const auto lowIn = overlapLow.lowpass (0.5f * (l + r));   // this track's low end, before anything (ducking depth fit)
        if (kickNoteBandHz > 0.0f)
        {
            const auto onNote = kickNoteBand.bandpass (0.5f * (l + r)) / 1.2f;
            kickNotePower += (double) onNote * onNote;
            lowPower += (double) lowIn * lowIn;
        }

        // On the kick track: learn where the kick hits (from this track's own audio, before any processing).
        int typeHere = -1;
        if ((sharing || useSuiteKick) && haveSections)
        {
            const auto sec = sections.locate (position, barQ);
            typeHere = sec.valid ? (int) sec.type : -1;
        }
        if (sharing)
        {
            kickLearner.process (0.5f * (l + r), position, barQ, typeHere, threshold);
            ownKick.process (0.5f * (l + r), threshold);
            if (n == 0) kickLearner.setBodyHz (ownKick.hits() >= kicksNeeded ? ownKick.pitchHz() : 0.0f);
        }

        if (gateEnabled)
        {
            // Fills: the last bar before a drop (and a breakdown) plays a fill instead of the pattern.
            int fill = TranceGate::NoFill;
            double fillQ = 0.0;
            if (directorLive)
            {
                namespace dr = suite::director;
                if ((n & 31) == 0) dirIdx = suiteDirector.actingAt (position, dr::pKybalion);
                if (dirIdx >= 0)
                {
                    const auto& m = suiteDirector[dirIdx];
                    const auto g = m.gesture[dr::pKybalion];
                    fill = g == dr::gKyRoll ? TranceGate::Roll : g == dr::gKyGap ? TranceGate::Gap : g == dr::gKyStutter ? TranceGate::Stutter : TranceGate::HalfTime;
                    fillQ = g == dr::gKyHalfTime ? position - m.atQ : position - (m.atQ - barQ);
                    if (dirIdx != lastDirectorReport) { suite::DirectorBus::reportActed (suiteDirector.hash(), dirIdx, dr::pKybalion); lastDirectorReport = dirIdx; }
                }
            }
            else if (followSections && fillWhen != Params::Fill::Off)
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
            if (directorLive && fill == TranceGate::NoFill)
            {
                // the handoff's last leg: Kybalion re-enters on the one, its first step wide open
                if ((n & 31) == 0)
                {
                    reentryAccent = false;
                    if (const auto at = suiteDirector.momentNear (position, 0.0, stepQNow, suite::director::kIntoDrop); at >= 0)
                        for (const auto& leg : suiteDirector[at].legs)
                            if (leg.player == suite::director::pKybalion && leg.gesture == suite::director::gNone) reentryAccent = true;
                }
                if (reentryAccent) accent = juce::jmax (accent, 1.0f);
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
        // Ducking depth from the overlap: this track's low end while the kick hits (its first 60 ms).
        if (ms < 60.0f && (useSidechain || (useSuiteKick && suiteKick.valid)))
        {
            overlapPower += (double) lowIn * lowIn;
            ++overlapSamples;
        }

        if (duckEnabled)
            ducker.process (ms, l, r);

        // the director's silences: the gate closes (3 ms), unless this is the kick in a kick-only bar
        if (directorLive && (n & 31) == 0)
        {
            dirSilence = suiteDirector.silenceAt (position);
            if (dirSilence >= 0 && suiteDirector[dirSilence].silence == suite::director::gSiKickOnly && sharing) dirSilence = -1;
        }
        directorSilence += juce::jlimit (-silenceStep, silenceStep, (directorLive && dirSilence >= 0 ? 0.0f : 1.0f) - directorSilence);
        if (directorSilence < 0.999f) { l *= directorSilence; r *= directorSilence; }

        const auto o = outGain.getNextValue();
        left[n] = l * o;
        if (right != nullptr) right[n] = r * o;

        position += qPerSample;
    }

    if (lowPower > 1.0e-6) shareOnKickNote.store ((float) juce::jmin (1.0, kickNotePower / lowPower));
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
    logPatternOnce();
    const auto source = (int) value (Params::Duck::source);
    if (source == Params::Duck::Sidechain) s.flags |= suite::Status::sidechainWanted;
    if (sidechainConnected.load()) s.flags |= suite::Status::sidechainConnected;
    if (value (Params::follow) > 0.5f) s.flags |= suite::Status::followsOuroboros;
    if ((int) value (Params::Fill::when) != Params::Fill::Off && value (Params::gateOn) > 0.5f) s.flags |= suite::Status::fillsOn;
    if (value (Params::gateOn) < 0.5f && value (Params::duckOn) < 0.5f) s.flags |= suite::Status::idle;   // both halves off
    if (value (Params::Duck::shareKick) > 0.5f)
    {
        s.flags |= suite::Status::kickSource;
        if (kickLearner.patterns().valid)
        {
            suite::KickBus::publish (kickLearner.patterns());   // keep the shared pattern fresh while stopped
        }
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

juce::String KybalionProcessor::kickTuning (float bodyHz, int keyRoot, bool haveKey)
{
    if (bodyHz < 25.0f) return {};
    const auto midi = 69.0f + 12.0f * std::log2 (bodyHz / 440.0f);
    const auto note = juce::roundToInt (midi);
    const auto cents = juce::roundToInt ((midi - (float) note) * 100.0f);
    const auto& names = suite::music::noteNames();
    juce::String text = "Kick body " + juce::String (juce::roundToInt (bodyHz)) + " Hz (" + names[((note % 12) + 12) % 12] + juce::String (note / 12 - 1)
                        + (cents != 0 ? juce::String (cents > 0 ? " +" : " ") + juce::String (cents) + " cents" : juce::String()) + ")";
    if (! haveKey) return text + ".";
    const auto pc = ((note - keyRoot) % 12 + 12) % 12;
    auto shortest = [] (int semis) { semis = ((semis % 12) + 12) % 12; return semis > 6 ? semis - 12 : semis; };
    const auto toRoot = shortest (-pc), toFifth = shortest (7 - pc);
    const auto signed_ = [] (int s) { return (s > 0 ? "+" : "") + juce::String (s) + (std::abs (s) == 1 ? " semitone" : " semitones"); };
    if (toRoot == 0) return text + ": on the key's root (" + names[keyRoot] + "), in tune with the bass.";
    if (toFifth == 0) return text + ": on the key's fifth (" + names[(keyRoot + 7) % 12] + "), in tune with the bass.";
    return text + ": between the key's notes. Tune the kick " + signed_ (toRoot) + " to the root (" + names[keyRoot] + ")"
           + (std::abs (toFifth) < std::abs (toRoot) + 2 ? ", or " + signed_ (toFifth) + " to the fifth (" + names[(keyRoot + 7) % 12] + ")" : juce::String())
           + ", so its body rings with the bass, not against it.";
}

void KybalionProcessor::kickBassTick()
{
    const auto body = kickBodyHz.load();
    const auto key = suite::KeyBus::read();
    const auto tuning = kickTuning (body, key.root, key.valid);
    if (tuning.isNotEmpty() && tuning != tuningText)
    {
        tuningText = tuning;
        suite::MoveLog::add (getName(), getTrackName(), tuning);
    }
    if ((int) value (Params::Duck::focus) != Params::Duck::FocusAuto || body < 25.0f) return;
    const auto share = shareOnKickNote.load();
    if (share < 0.0f) return;
    const auto was = focusAuto.load();
    const auto now = focusFor (share, was);
    if (now == was) return;
    focusAuto.store (now);
    const auto pct = juce::String (juce::roundToInt (share * 100.0f)) + "%";
    suite::MoveLog::add (getName(), getTrackName(), now ? "Ducking only around the kick's note (" + juce::String (juce::roundToInt (body)) + " Hz): " + pct
                                                          + " of this track's low end is there, so the rest keeps sustaining through the kick"
                                                        : "Ducking the whole sound again: " + pct + " of this track's low end sits on the kick's note");
}

bool KybalionProcessor::applyRoleSetup (int role, const juce::String&)
{
    int preset = -1;
    bool gate = false;
    switch (role)
    {
        case suite::roleBass:  preset = 0; gate = false; break;
        case suite::rolePads:  preset = 1; gate = true;  break;
        case suite::roleLead:  preset = 2; gate = false; break;
        case suite::roleVocal: preset = 2; gate = false; break;
        default: return false;   // the kick shares itself (by name); hats and FX are left alone
    }
    applyDuckPreset (preset);
    setParam (Params::duckOn, 1.0f);
    if (role == suite::roleBass) setParam (Params::Duck::focus, (float) Params::Duck::FocusAuto);   // it decides from what it hears
    setParam (Params::Duck::source, (float) Params::Duck::SuiteKick);
    setParam (Params::gateOn, gate ? 1.0f : 0.0f);
    if (gate && ! startedOnFavourite) applyGatePattern (1);   // Trance classic, unless your favourite is already there
    return true;
}

void KybalionProcessor::trackNameChanged (const juce::String& name)
{
    // Your favourite pattern for this kind of track, while the gate is still as it started.
    kind = trackKind (name);
    if (kind.isNotEmpty())
    {
        const auto favourites = favouritePatterns (suite::StyleLog::read(), kind);
        if (! favourites.empty() && favourites.front().second >= 2 && currentSteps() == startSteps && favourites.front().first != startSteps)
        {
            auto steps = favourites.front().first;
            auto apply = [steps] (KybalionProcessor& p) { p.applySteps (steps); p.startSteps = steps; };
            if (juce::MessageManager::getInstance()->isThisTheMessageThread()) apply (*this);
            else juce::MessageManager::callAsync ([safe = juce::WeakReference<KybalionProcessor> (this), apply] { if (auto* p = safe.get()) apply (*p); });
        }
    }

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

float KybalionProcessor::overlapDb() const
{
    return overlapSamples > (int64_t) (0.5 * getSampleRate()) ? juce::Decibels::gainToDecibels ((float) std::sqrt (overlapPower / (double) overlapSamples), -100.0f) : -100.0f;
}

float KybalionProcessor::depthForOverlap (float db)
{
    // Strong low end under the kick (-18 dBFS RMS or more) ducks almost fully; little (-42 or less) just a touch.
    return juce::jlimit (0.4f, 0.95f, 0.4f + (db + 42.0f) / 24.0f * 0.55f);
}

void KybalionProcessor::mixTick()
{
    namespace AP = suite::Autopilot;
    if (! mixLatch.log) mixLatch.log = [this] (const juce::String& s) { suite::MoveLog::add ("Kybalion", getTrackName(), s); };
    // the fixes kept the old way (a property each) carried over once
    if ((bool) apvts.state.getProperty ("mixPumpSection", false)) { mixLatch.force (AP::mxPumpSectionF); apvts.state.removeProperty ("mixPumpSection", nullptr); }
    if ((bool) apvts.state.getProperty ("mixAutoFit", false)) { mixLatch.force (AP::mxAutoFitKickF); apvts.state.removeProperty ("mixAutoFit", nullptr); }
    mixLatch.beginTick (isNonRealtime());
    pumpPerSection.store (mixLatch.update (AP::mxPumpSectionF, true));
    if (mixLatch.update (AP::mxAutoFitKickF, true) && ! autoFitted && sidechainConnected.load() && kick.hits() >= kicksNeeded
        && ! mixLatch.owned (Params::Duck::hold) && ! mixLatch.owned (Params::Duck::release))
        autoFitted = fitToKick();
    phasePerSection.store (mixLatch.update (AP::mx2PhasePerSectionF, sidechainConnected.load()));
    for (size_t t = 0; t < 6; ++t)
        if (secMM[t] > 1.0e-3 && secSS[t] > 1.0e-3)
        {
            const auto corr = secMS[t] / std::sqrt (secMM[t] * secSS[t]);
            if (corr < -0.3 && ! sectionFlip[t].load()) { sectionFlip[t].store (true); if (phasePerSection.load()) suite::MoveLog::add ("Kybalion", getTrackName(), "Polarity flipped in the " + juce::String (suite::sectionName ((suite::SectionType) t)).toLowerCase() + "s (the low end cancelled the kick there)"); }
            else if (corr > 0.3 && sectionFlip[t].load()) sectionFlip[t].store (false);
        }
    mixLatch.watchOwnership();
    mixLatch.publish (getInstanceId(), "Kybalion", getTrackName());
}

bool KybalionProcessor::fitToKick()
{
    if (kick.hits() < kicksNeeded)
        return false;
    // Hold through the kick's punch, then recover as its tail dies away.
    const auto length = kick.lengthMs();
    mixLatch.setParam (Params::Duck::hold, juce::jlimit (0.0f, 200.0f, 0.15f * length));
    mixLatch.setParam (Params::Duck::release, juce::jlimit (20.0f, 1000.0f, 0.85f * length));
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
