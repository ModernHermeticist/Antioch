#include "PluginProcessor.h"
#include "PluginEditor.h"

PrimaMateriaProcessor::PrimaMateriaProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PrimaMateriaState", pm::layout())
{
    // Mentalism's fix-it buttons: nudge this track's section levels (muted sections stay muted).
    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix, uint32_t data) -> std::vector<suite::FixReceiver::Change>
    {
        using T = suite::SectionType;
        auto level = [this] (int column) { return apvts.getRawParameterValue (pm::levelId (column))->load(); };
        auto muted = [] (float db) { return db <= pm::offDb + 0.5f; };
        auto shift = [&] (T type, float db) -> std::vector<suite::FixReceiver::Change>
        {
            const auto v = level ((int) type);
            if (muted (v)) return {};
            return { { pm::levelId ((int) type), juce::jlimit (pm::offDb + 1.0f, 6.0f, v + db) } };
        };

        switch (fix)
        {
            case suite::Fix::BreakdownQuieter: return shift (T::Breakdown, -2.0f);
            case suite::Fix::BreakdownLouder:  return shift (T::Breakdown, 2.0f);
            case suite::Fix::IntroQuieter:     return shift (T::Intro, -2.0f);
            case suite::Fix::LaterDropBigger:
            {
                const auto drop = (int) T::Drop;
                if (apvts.getRawParameterValue (pm::laterOnId)->load() > 0.5f)
                {
                    const auto v = level (pm::laterDrop);
                    if (muted (v)) return {};
                    return { { pm::levelId (pm::laterDrop), juce::jmin (6.0f, v + 1.5f) } };
                }
                if (muted (level (drop))) return {};
                // Start the Drop 2+ column from the drop's settings, 1.5 dB up.
                return { { pm::lowCutId (pm::laterDrop), apvts.getRawParameterValue (pm::lowCutId (drop))->load() },
                         { pm::highCutId (pm::laterDrop), apvts.getRawParameterValue (pm::highCutId (drop))->load() },
                         { pm::levelId (pm::laterDrop), juce::jmin (6.0f, level (drop) + 1.5f) },
                         { pm::laterOnId, 1.0f } };
            }
            case suite::Fix::DjIntroOutro:
            {
                // DJ-friendly: melodic parts out of the intro and outro, so it mixes in and out on the beat.
                const auto role = getRole();
                if (role != 4 && role != 5 && role != 6) return {};
                std::vector<suite::FixReceiver::Change> changes;
                for (auto t : { T::Intro, T::Outro })
                    if (! muted (level ((int) t))) changes.push_back ({ pm::levelId ((int) t), pm::offDb });
                return changes;
            }
            case suite::Fix::GainStage:
            {
                if (const auto g = gainStageTarget()) return { { pm::inputGainId, *g } };
                return {};
            }
            case suite::Fix::Carve:
            {
                if ((data & 0xfffu) != (getInstanceId() & 0xfffu)) return {};   // meant for another track
                const auto band = (int) ((data >> 12) & 0xf);
                const auto current = (int) apvts.getRawParameterValue (pm::carveBandId)->load() - 1;
                const auto depth = apvts.getRawParameterValue (pm::carveDepthId)->load();
                return { { pm::carveBandId, (float) (band + 1) },
                         { pm::carveDepthId, current == band ? juce::jmax (-9.0f, depth - 1.5f) : -3.0f } };   // again: deeper
            }
            case suite::Fix::RoughMix:
            {
                if (const auto t = roughMixTrim()) return { { pm::trimId, *t } };
                return {};
            }
            case suite::Fix::BuildRise:
            case suite::Fix::None:
            default: return {};
        }
    });

    // Arc match: one section of the map up or down, on every track that plays in it.
    fixes->setCustom ([this] (suite::Fix fix, uint32_t data) -> std::function<void()>
    {
        suite::SectionMap map;
        if (fix != suite::Fix::ArcMatch || ! suite::SectionBus::read (map)) return {};
        int index = 0;
        float db = 0.0f;
        suite::unpackArc (data, index, db);
        if (index >= map.count || index >= (int) arcTrims.size()) return {};
        const auto bar = map.sections[(size_t) index].startBar;
        if (apvts.getRawParameterValue (pm::levelId ((int) map.sections[(size_t) index].type))->load() <= pm::offDb + 0.5f) return {};   // silent here anyway
        auto& t = arcTrims[(size_t) index];
        const auto before = arcTrimFor (index, bar);
        const auto after = juce::jlimit (-12.0f, 12.0f, before + db);
        t.db.store (after);
        t.bar.store (bar);
        return [this, index, bar, before, after]
        {
            auto& tr = arcTrims[(size_t) index];
            if (tr.bar.load() == bar && std::abs (tr.db.load() - after) < 1.0e-4f) tr.db.store (before);
        };
    });
}

bool PrimaMateriaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && layouts.getMainInputChannelSet() == out;
}

void PrimaMateriaProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto& f : lowCut) f.reset();
    for (auto& f : highCut) f.reset();
    levelDb = 0.0f;
    lowLog = std::log2 (pm::lowCutOff);
    highLog = std::log2 (pm::highCutOff);
    gapGain = 1.0f;
    loudness.prepare (sampleRate);
    spectrum.prepare (sampleRate);
    for (auto& c : carve) c.reset();
    carveBandNow = -1;
    lastTrim = apvts.getRawParameterValue (pm::trimId)->load();
    trimGain.reset (sampleRate, 0.05);
    trimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (lastTrim));
}

pm::Setting PrimaMateriaProcessor::settingFor (int type) const
{
    auto v = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    return { v (pm::levelId (type)), v (pm::lowCutId (type)), v (pm::highCutId (type)) };
}

void PrimaMateriaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    using T = suite::SectionType;

    const auto host = suite::HostInfo::read (getPlayHead());
    const auto haveMap = sections.refresh();
    mapAvailable.store (haveMap);
    const auto follow = apvts.getRawParameterValue (pm::followId)->load() > 0.5f && haveMap && host.playing && host.hasPpq;
    const auto ramp = apvts.getRawParameterValue (pm::rampId)->load() > 0.5f;
    const auto gapQ = pm::gapQuarters ((int) apvts.getRawParameterValue (pm::gapId)->load());
    const auto morphQ = pm::morphQuarters ((int) apvts.getRawParameterValue (pm::morphId)->load());
    const auto qPerSample = host.quartersPerSample (sampleRate);
    const auto barQ = host.barQuarters();
    const auto sr = (float) sampleRate;

    // Glide speed: a one-pole whose time constant is a third of the Morph time.
    const auto tauSamples = morphQ > 0.0 ? (float) (morphQ / qPerSample / 3.0) : 0.0f;
    const auto glide = tauSamples > 1.0f ? 1.0f - std::exp (-1.0f / tauSamples) : 1.0f;
    const auto gapStep = 1.0f / (0.003f * sr);   // 3 ms fade in/out of the gap

    // Carve (the masking fix): a peaking dip in one octave band.
    const auto carveBand = (int) apvts.getRawParameterValue (pm::carveBandId)->load() - 1;
    const auto carveDepth = apvts.getRawParameterValue (pm::carveDepthId)->load();
    if (carveBand != carveBandNow || std::abs (carveDepth - carveDepthNow) > 1.0e-3f)
    {
        if (carveBand >= 0)
            for (auto& c : carve) c.setPeak (analysis::bandCentres()[(size_t) carveBand], 1.4f, carveDepth, sr);
        carveBandNow = carveBand; carveDepthNow = carveDepth;
    }
    const auto carving = carveBand >= 0 && carveDepth < -0.05f;

    // Trim (the rough mix sets it). A new trim makes the old loudness reading stale.
    const auto trim = apvts.getRawParameterValue (pm::trimId)->load();
    if (std::abs (trim - lastTrim) > 0.05f) { loudness.reset(); lastTrim = trim; }
    const auto inputGainDb = apvts.getRawParameterValue (pm::inputGainId)->load();
    if (std::abs (inputGainDb - lastInputGain) > 0.05f) { loudness.reset(); lastInputGain = inputGainDb; }   // gain staging: measure again
    trimGain.setTargetValue (juce::Decibels::decibelsToGain (trim));

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    int typeNow = -1;
    bool rampNow = false, gapNow = false, laterNow = false;
    const auto isKick = getRole() == 1;   // a kick-only bar before drops spares the kick
    const auto role = getRole();
    const auto breath = apvts.getRawParameterValue (pm::breathId)->load();
    const auto answer = (int) apvts.getRawParameterValue (pm::answerId)->load();
    const auto pump = apvts.getRawParameterValue (pm::pumpId)->load();
    const auto inputGain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (pm::inputGainId)->load());
    const auto entrance = sections.getMap().dropEntrance;
    const auto kickPattern = pump > 0.0f ? suite::KickBus::read() : suite::KickPatternSet {};
    const auto msPerQ = 60000.0 / host.bpm;
    const auto pumpRelease = 1.0f - std::exp (-1.0f / (0.001f * 3.0f * sr));   // 3 ms smoothing
    double blockPower = 0.0;
    const auto managerCut = apvts.getRawParameterValue (pm::lowManagerId)->load() > 0.5f ? pm::lowEndCut (getRole()) : 0.0f;
    const auto owners = lowOwners.load();
    float cutNow = 0.0f;

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        pm::Setting target;   // unity when not following
        bool inGap = false, laterThis = false;
        int typeThis = -1;
        float energyDb = 0.0f, arcDb = 0.0f;
        double posHere = host.ppq + qPerSample * n;

        // Gain staging: measure, then apply Input Gain before everything else.
        if (host.playing)
        {
            const auto l0 = left[n], r0 = right != nullptr ? right[n] : left[n];
            blockPower += 0.5 * ((double) l0 * l0 + (double) r0 * r0);
        }
        left[n] *= inputGain;
        if (right != nullptr) right[n] *= inputGain;

        if (follow)
        {
            const auto pos = host.ppq + qPerSample * n;
            const auto sec = sections.locate (pos, barQ);
            if (sec.valid)
            {
                typeNow = typeThis = (int) sec.type;
                laterThis = sec.isLaterDrop();
                energyDb = 12.0f * (sec.energy - suite::defaultEnergy (sec.type));   // drawn energy: up to +-12 dB, 0 when untouched
                if (sec.index < sections.getMap().count)
                    arcDb = arcTrimFor (sec.index, sections.getMap().sections[(size_t) sec.index].startBar);
                const auto useLater = sec.isLaterDrop() && apvts.getRawParameterValue (pm::laterOnId)->load() > 0.5f;
                target = settingFor (useLater ? pm::laterDrop : typeNow);
                laterNow = laterNow || useLater;

                // Builds ramp from the previous section's settings to the Build's own.
                if (ramp && sec.type == T::Build && sec.index > 0)
                {
                    const auto from = settingFor ((int) sections.getMap().sections[(size_t) sec.index - 1].type);
                    const auto p = (float) juce::jlimit (0.0, 1.0, sec.progress (pos));
                    const auto fromLevel = juce::jmax (from.levelDb, -40.0f), toLevel = juce::jmax (target.levelDb, -40.0f);
                    target.levelDb = target.levelDb <= pm::offDb + 0.05f && p >= 1.0f ? pm::offDb : fromLevel + p * (toLevel - fromLevel);
                    target.lowCut  = std::exp2 (std::log2 (from.lowCut) + p * (std::log2 (target.lowCut) - std::log2 (from.lowCut)));
                    target.highCut = std::exp2 (std::log2 (from.highCut) + p * (std::log2 (target.highCut) - std::log2 (from.highCut)));
                    rampNow = true;
                }

                const auto suiteGap = pm::suiteGapQuarters (sections.getMap().dropGap, barQ, isKick);
                const auto trackGap = juce::jmin (gapQ, barQ);
                const auto thisGap = juce::jmax (trackGap, suiteGap);
                inGap = thisGap > 0.0 && sec.hasNext && sec.next == T::Drop && pos >= sec.endQ - thisGap && sec.endQ - sec.startQ > thisGap;
                gapNow = gapNow || inGap;

                // Expression ------------------------------------------------------------
                const auto intoQ = pos - sec.startQ;
                // Drop entrance: staggered (kick, bass a beat later, the rest after two) or filter opening.
                if (sec.type == T::Drop && intoQ >= 0.0 && intoQ < barQ)
                {
                    if (entrance == 1)
                    {
                        const auto waitQ = role == 1 ? 0.0 : role == 2 ? 1.0 : 2.0;
                        if (intoQ < waitQ) { inGap = true; gapNow = true; }
                    }
                    else if (entrance == 2 && role != 1 && role != 2)
                        target.highCut = std::min (target.highCut, 400.0f * std::pow (50.0f, (float) (intoQ / barQ)));
                }
                // Breath: a slow 8-bar swell in breakdowns and drops.
                if (breath > 0.0f && (sec.type == T::Breakdown || sec.type == T::Drop) && target.levelDb > pm::offDb + 0.5f)
                    target.levelDb += 1.5f * breath * (float) std::sin (juce::MathConstants<double>::twoPi * intoQ / (8.0 * barQ));
                // Call and response: in breakdowns, the call leads bars 1-2 of every 4, the response bars 3-4.
                if (answer != pm::answerOff && sec.type == T::Breakdown && target.levelDb > pm::offDb + 0.5f)
                {
                    const auto secondHalf = ((int64_t) std::floor (intoQ / (2.0 * barQ))) % 2 == 1;
                    if ((answer == pm::answerCall) == secondHalf) target.levelDb -= 5.0f;
                }
            }
        }

        if (follow && target.levelDb > pm::offDb + 0.5f)
        {
            // Ouroboros' Intensity scales this track's section moves; the energy curve adds to them.
            const auto k = sections.getMap().intensityScale();
            target.levelDb = juce::jlimit (pm::offDb + 1.0f, 6.0f + juce::jmax (0.0f, arcDb), target.levelDb * k + energyDb * k + arcDb);
        }

        // Low-end manager: where a kick or bass plays, other roles leave the low end to them.
        if (managerCut > 0.0f)
        {
            const auto bit = laterThis ? 6 : typeThis;   // -1 without a map: any owner counts
            const auto ownersHere = bit < 0 ? owners != 0 : (owners & (1u << bit)) != 0;
            if (ownersHere && target.lowCut < managerCut) { target.lowCut = managerCut; cutNow = managerCut; }
        }

        // Glide towards the target (instant when Morph is off).
        levelDb += glide * (target.levelDb - levelDb);
        lowLog  += glide * (std::log2 (target.lowCut) - lowLog);
        highLog += glide * (std::log2 (target.highCut) - highLog);
        gapGain = inGap ? std::max (0.0f, gapGain - gapStep) : std::min (1.0f, gapGain + gapStep);

        if ((n & 15) == 0)   // filter coefficients every 16 samples is plenty for glides
            for (int c = 0; c < 2; ++c)
            {
                lowCut[(size_t) c].set (std::exp2 (lowLog), 0.707f, sr);
                highCut[(size_t) c].set (std::exp2 (highLog), 0.707f, sr);
            }

        const auto gain = (levelDb <= pm::offDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain (levelDb)) * gapGain;
        const auto useLow = lowLog > std::log2 (pm::lowCutOff) + 0.01f, useHigh = highLog < std::log2 (pm::highCutOff) - 0.01f;

        auto processChannel = [&] (float x, size_t c)
        {
            const auto hp = lowCut[c].highpass (x);   // always run, so switching in/out is seamless
            const auto lp = highCut[c].lowpass (useLow ? hp : x);
            return (useHigh ? lp : (useLow ? hp : x)) * gain;
        };
        // Pump: duck to the suite's shared kick pattern (no routing).
        float pumpTarget = 1.0f;
        if (pump > 0.0f && kickPattern.valid && host.playing)
        {
            auto phase = std::fmod (posHere, barQ);
            if (phase < 0.0) phase += barQ;
            const auto ms = kickPattern.msSinceHit (typeThis, phase, barQ, msPerQ);
            if (ms < 1.0e8f) pumpTarget = 1.0f - pump * 0.6f * std::exp (-ms / 110.0f);
        }
        pumpGain += (pumpTarget < pumpGain ? 0.5f : pumpRelease) * (pumpTarget - pumpGain);

        const auto t = trimGain.getNextValue() * pumpGain;
        left[n] = processChannel (left[n], 0) * t;
        if (right != nullptr) right[n] = processChannel (right[n], 1) * t;
        if (carving)
        {
            left[n] = carve[0].process (left[n]);
            if (right != nullptr) right[n] = carve[1].process (right[n]);
        }

        // Loudness for the rough mix: the drops (where everything plays), or all playback without a map.
        if (host.playing && (! follow || typeNow == (int) T::Drop))
        {
            loudness.process (left[n], right != nullptr ? right[n] : left[n]);
            spectrum.process (left[n], right != nullptr ? right[n] : left[n]);
        }
    }

    currentType.store (follow ? (laterNow ? pm::laterDrop : typeNow) : -1);
    ramping.store (rampNow);
    gapping.store (gapNow);
    nowLevelDb.store (levelDb);
    nowLowCut.store (std::exp2 (lowLog));
    if (blockPower > 0.0)
    {
        inputPower.store (inputPower.load() + blockPower);
        inputCount.fetch_add (buffer.getNumSamples());
    }
    managerCutNow.store (cutNow);
    nowHighCut.store (std::exp2 (highLog));
}

PrimaMateriaProcessor::SuiteStatus PrimaMateriaProcessor::suiteStatus()
{
    SuiteStatus s;
    const auto role = getRole();
    if (role >= 0) s.flags |= suite::Status::roleSet;
    if (apvts.getRawParameterValue (pm::followId)->load() > 0.5f) s.flags |= suite::Status::followsOuroboros;
    const auto lufs = measuredLufs();
    if (lufs > -70.0f) s.flags |= suite::Status::measuring;
    if (spectrum.longSeconds.load() >= 2.0f)
    {
        s.flags |= suite::Status::hasBands;
        for (size_t b = 0; b < s.bands.size(); ++b) s.bands[b] = spectrum.longDb[b].load();
    }
    s.metrics = { (float) role, lufs, apvts.getRawParameterValue (pm::trimId)->load(), (float) soundingMask() };
    refreshLowOwners();
    return s;
}

uint32_t PrimaMateriaProcessor::soundingMask() const
{
    uint32_t mask = 0;
    for (int t = 0; t < pm::numTypes; ++t)
        if (apvts.getRawParameterValue (pm::levelId (t))->load() > pm::offDb + 0.5f) mask |= 1u << t;
    const auto later = apvts.getRawParameterValue (pm::laterOnId)->load() > 0.5f ? pm::laterDrop : (int) suite::SectionType::Drop;
    if (apvts.getRawParameterValue (pm::levelId (later))->load() > pm::offDb + 0.5f) mask |= 1u << 6;
    return mask;
}

void PrimaMateriaProcessor::refreshLowOwners()
{
    uint32_t owners = 0;
    for (const auto& e : suite::Registry::list())
        if (e.plugin == "Prima Materia" && e.id != getInstanceId() && ((int) e.metrics[0] == 1 || (int) e.metrics[0] == 2))
            owners |= (uint32_t) e.metrics[3];
    lowOwners.store (owners);
}

void PrimaMateriaProcessor::trackNameChanged (const juce::String& name)
{
    // Auto-role: only for an instance whose role hasn't been chosen yet.
    const auto role = pm::roleFromTrackName (name);
    if (role < 0 || getRole() >= 0)
        return;
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        applyRole (role);
    else
        juce::MessageManager::callAsync ([safe = juce::WeakReference<PrimaMateriaProcessor> (this), role]
        {
            if (auto* p = safe.get()) if (p->getRole() < 0) p->applyRole (role);
        });
}

std::optional<float> PrimaMateriaProcessor::roughMixTrim() const
{
    const auto role = getRole();
    const auto lufs = measuredLufs();
    if (role <= 0 || lufs <= -60.0f)   // not set, Neutral, or nothing measured yet
        return std::nullopt;

    // Reference: the loudest measured kick track in the suite.
    float kick = -100.0f;
    for (const auto& e : suite::Registry::list())
        if (e.plugin == "Prima Materia" && (int) e.metrics[0] == 1 && e.has (suite::Status::measuring))
            kick = std::max (kick, e.metrics[1]);
    if (kick <= -60.0f)
        return std::nullopt;

    const auto trim = apvts.getRawParameterValue (pm::trimId)->load();
    const auto wanted = juce::jlimit (-24.0f, 12.0f, trim + (kick + pm::roughMixOffset (role) - lufs));
    if (std::abs (wanted - trim) < 0.5f)
        return std::nullopt;
    return wanted;
}

void PrimaMateriaProcessor::applyRole (int index)
{
    const auto& roles = pm::roles();
    if (! juce::isPositiveAndBelow (index, (int) roles.size()))
        return;
    const auto& role = roles[(size_t) index];
    auto setValue = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    };
    for (int t = 0; t < pm::numTypes; ++t)
    {
        setValue (pm::levelId (t), role.settings[(size_t) t].levelDb);
        setValue (pm::lowCutId (t), role.settings[(size_t) t].lowCut);
        setValue (pm::highCutId (t), role.settings[(size_t) t].highCut);
    }
    setValue (pm::levelId (pm::laterDrop), role.later.levelDb);
    setValue (pm::lowCutId (pm::laterDrop), role.later.lowCut);
    setValue (pm::highCutId (pm::laterDrop), role.later.highCut);
    setValue (pm::laterOnId, role.useLater ? 1.0f : 0.0f);
    setValue (pm::rampId, role.ramp ? 1.0f : 0.0f);
    setValue (pm::roleId, (float) (index + 1));
    setValue (pm::gapId, (float) role.gap);
}

juce::AudioProcessorEditor* PrimaMateriaProcessor::createEditor()
{
    return new PrimaMateriaEditor (*this);
}

void PrimaMateriaProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::StringArray arc;
    for (size_t i = 0; i < arcTrims.size(); ++i)
        if (arcTrims[i].bar.load() >= 0 && std::abs (arcTrims[i].db.load()) > 1.0e-3f)
            arc.add (juce::String ((int) i) + ":" + juce::String (arcTrims[i].bar.load()) + ":" + juce::String (arcTrims[i].db.load(), 2));
    state.setProperty ("arc", arc.joinIntoString (";"), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PrimaMateriaProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            for (auto& t : arcTrims) { t.bar.store (-1); t.db.store (0.0f); }
            for (const auto& item : juce::StringArray::fromTokens (state["arc"].toString(), ";", ""))
            {
                const auto parts = juce::StringArray::fromTokens (item, ":", "");
                if (parts.size() == 3 && juce::isPositiveAndBelow (parts[0].getIntValue(), (int) arcTrims.size()))
                {
                    arcTrims[(size_t) parts[0].getIntValue()].db.store (parts[2].getFloatValue());
                    arcTrims[(size_t) parts[0].getIntValue()].bar.store (parts[1].getIntValue());
                }
            }
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrimaMateriaProcessor();
}
