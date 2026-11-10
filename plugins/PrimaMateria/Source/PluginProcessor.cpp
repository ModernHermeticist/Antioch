#include "PluginProcessor.h"
#include "PluginEditor.h"

PrimaMateriaProcessor::PrimaMateriaProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PrimaMateriaState", pm::layout())
{
    // Mentalism's fix-it buttons: nudge this track's section levels (muted sections stay muted).
    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix) -> std::vector<suite::FixReceiver::Change>
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
            case suite::Fix::BuildRise:
            case suite::Fix::None:
            default: return {};
        }
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

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    int typeNow = -1;
    bool rampNow = false, gapNow = false, laterNow = false;

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        pm::Setting target;   // unity when not following
        bool inGap = false;

        if (follow)
        {
            const auto pos = host.ppq + qPerSample * n;
            const auto sec = sections.locate (pos, barQ);
            if (sec.valid)
            {
                typeNow = (int) sec.type;
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

                inGap = gapQ > 0.0 && sec.hasNext && sec.next == T::Drop && pos >= sec.endQ - gapQ;
                gapNow = gapNow || inGap;
            }
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
        left[n] = processChannel (left[n], 0);
        if (right != nullptr) right[n] = processChannel (right[n], 1);
    }

    currentType.store (follow ? (laterNow ? pm::laterDrop : typeNow) : -1);
    ramping.store (rampNow);
    gapping.store (gapNow);
    nowLevelDb.store (levelDb);
    nowLowCut.store (std::exp2 (lowLog));
    nowHighCut.store (std::exp2 (highLog));
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
    setValue (pm::gapId, (float) role.gap);
}

juce::AudioProcessorEditor* PrimaMateriaProcessor::createEditor()
{
    return new PrimaMateriaEditor (*this);
}

void PrimaMateriaProcessor::getPluginState (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PrimaMateriaProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrimaMateriaProcessor();
}
