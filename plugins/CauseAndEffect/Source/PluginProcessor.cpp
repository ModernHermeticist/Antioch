#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KeyBus.h"
#include "Music.h"

CauseEffectProcessor::CauseEffectProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CauseEffectState", Params::createLayout())
{
    // Mentalism's fix-it buttons: a build that doesn't rise gets more riser, noise and wash.
    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix, uint32_t data) -> std::vector<suite::FixReceiver::Change>
    {
        using R = suite::FixReceiver;
        if (fix == suite::Fix::BuildImpact && data == 0)
        {
            // The problem watcher: the drop doesn't jump out of the build: a deeper build (the
            // filter sweeps higher, the image narrows more, the tails cut at the drop)
            return { R::nudge (apvts, Params::filterMax, 0.12f), R::nudge (apvts, Params::widen, 0.15f), { Params::dropCut, 1.0f } };
        }
        if (fix != suite::Fix::BuildRise) return {};
        return { R::nudge (apvts, Params::riser, 0.1f), R::nudge (apvts, Params::noise, 0.1f), R::nudge (apvts, Params::wash, 0.1f) };
    });

    // Spread the riser oscillators' start phases so they don't begin in lock-step.
    for (size_t i = 0; i < saws.size(); ++i)
        saws[i].setPhase ((float) i * 0.27f);
}

bool CauseEffectProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out || in == juce::AudioChannelSet::mono();
}

void CauseEffectProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    const auto sr = (float) sampleRate;

    clock.reset();
    washVerb.prepare (sampleRate);
    for (auto* f : { &hpL, &hpR, &noiseBpL, &noiseBpR, &riserLpL, &riserLpR, &washHpL, &washHpR })
        f->reset();
    washHpL.set (220.0f, 0.707f, sr);
    washHpR.set (220.0f, 0.707f, sr);

    manual.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.03);
    manual.setCurrentAndTargetValue (value (Params::build));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (Params::output)));

    buildSmoothing = 1.0f - std::exp (-1.0f / (0.003f * sr));   // 3 ms: fast enough to snap at the drop
    stutterSmoothing = 1.0f - std::exp (-1.0f / (0.002f * sr));
    tailStep = 1.0f / (0.015f * sr);                              // 15 ms tail cut
    build = 0.0f;
    stutterGain = tailGain = 1.0f;
    levelIn = levelOut = 0.0; levelGain = 1.0f; minLevelGain = 1.0f;
    levelCoeff = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));
    levelAttack = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
    levelRelease = 1.0f - std::exp (-1.0f / (0.2f * (float) sampleRate));
    tailFading = false;
    manualArmed = false;
    inSectionBuild = false;
}

void CauseEffectProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    using namespace Params;

    const auto numSamples = buffer.getNumSamples();
    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto sr = (float) sampleRate;
    const auto host = suite::HostInfo::read (getPlayHead());
    clock.beginBlock (host, sampleRate, Params::lengthBars (choice (Params::length)));
    const auto haveSections = sections.refresh();
    const auto followSections = value (Params::follow) > 0.5f && haveSections && host.playing && host.hasPpq;
    ouroborosFound.store (sections.isAvailable());
    const auto barQ = host.barQuarters();
    double sectionProgress = -1.0, sectionBars = 0.0;
    // A clean ending: past the map's last bar, the wash and risers fade out over 2 bars
    endFade = 1.0f;
    if (suite::Autopilot::on (suite::Autopilot::mixing) && haveSections && host.playing && host.hasPpq && sections.getMap().totalBars > 0)
    {
        const auto over = (host.ppq - sections.getMap().totalBars * barQ) / (2.0 * barQ);
        if (over > 0.0) endFade = (float) juce::jlimit (0.0, 1.0, 1.0 - over);
    }

    // ---- Key ----
    auto root = choice (rootNote);
    const auto bus = suite::KeyBus::read();
    const auto follow = value (followKey) > 0.5f && bus.valid;
    if (follow) root = bus.root;
    // Final-drop key change: a build into the lifted drop rises to the new root.
    if (haveSections && host.playing && host.hasPpq)
        if (const auto sec = sections.locate (host.ppq, barQ); sec.valid)
            root += sec.type == suite::SectionType::Build ? sec.nextKeyShift : sec.keyShift;
    effectiveRoot.store (root % 12);
    followingBus.store (follow);
    busScale.store (bus.scale);

    // ---- Block-rate settings ----
    const auto trig = value (trigger) > 0.5f;
    const auto curveExp = value (curve);
    const auto cutTails = value (dropCut) > 0.5f;
    const auto maxCut = value (filterMax);
    const auto q = 0.7f + 4.0f * value (filterRes);
    const auto washAmt = value (wash);
    const auto noiseAmt = value (noise);
    const auto riserAmt = value (riser);
    const auto riserTop = 60.0f + (float) root;                          // lands on the root at the drop
    const auto riserBottom = riserTop - 12.0f * (float) riserOctaves (choice (riserRange));
    const auto stutterAmt = value (stutter);
    const auto stutterFinal = stutterMaxQuarters (choice (stutterMax));
    const auto widenAmt = value (widen);

    washVerb.setParameters (value (washDecay), 0.85f, 7000.0f, 0.5f);
    manual.setTargetValue (value (Params::build));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (value (output)));

    auto* left  = buffer.getWritePointer (0);
    auto* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto* inRight = numIn > 1 ? buffer.getReadPointer (1) : nullptr;
    static constexpr float detune[4] = { -0.14f, 0.05f, -0.05f, 0.14f };

    for (int n = 0; n < numSamples; ++n)
    {
        // ---- Build amount: manual knob or timed trigger, whichever is higher ----
        bool drop = false;
        const auto posQ = clock.getPosition();
        const auto ramp = clock.next (trig, drop);

        // Ouroboros: ramp across a Build section and drop when playback crosses its end.
        auto sectionRamp = 0.0f;
        if (followSections)
        {
            const auto sec = sections.locate (posQ, barQ);
            const auto building = sec.valid && sec.type == suite::SectionType::Build;
            if (building)
            {
                sectionRamp = (float) juce::jlimit (0.0, 1.0, sec.progress (posQ));
                sectionProgress = sec.progress (posQ);
                sectionBars = (sec.endQ - sec.startQ) / barQ;
                sectionBuildEndQ = sec.endQ;
            }
            else if (inSectionBuild && posQ >= sectionBuildEndQ - 1.0e-6 && posQ < sectionBuildEndQ + barQ)
            {
                drop = true;   // crossed the end of the build (not a jump elsewhere)
            }
            inSectionBuild = building;
        }
        else
        {
            inSectionBuild = false;
        }
        const auto m = manual.getNextValue();

        // Manual automation coming back down from a real build to ~0 is also a drop.
        if (m > 0.3f)
            manualArmed = true;
        else if (manualArmed && m < 0.05f)
        {
            manualArmed = false;
            drop = true;
        }

        const auto raw = juce::jmax (m, ramp, sectionRamp);
        const auto target = raw > 0.0f ? std::pow (raw, curveExp) : 0.0f;
        build += buildSmoothing * (target - build);
        const auto b = build;

        if (drop)
        {
            dropCount.fetch_add (1);
            if (cutTails) tailFading = true;
        }

        // Tail cut: fade the wash/noise/riser out, clear the reverb, then re-open.
        if (tailFading)
        {
            tailGain -= tailStep;
            if (tailGain <= 0.0f)
            {
                tailGain = 0.0f;
                washVerb.reset();
                tailFading = false;
            }
        }
        else if (tailGain < 1.0f)
        {
            tailGain = juce::jmin (1.0f, tailGain + tailStep);
        }

        const auto inL = left[n];
        const auto inR = inRight != nullptr ? inRight[n] : inL;

        // Per-sample coefficient updates are only needed while building.
        const auto active = b > 1.0e-4f;

        // ---- High-pass sweep (exponential, faded in over the first few % of the build) ----
        float xL = inL, xR = inR;
        if (active)
        {
            const auto cutoff = 20.0f * std::pow (maxCut / 20.0f, b);
            hpL.set (cutoff, q, sr);
            hpR.set (cutoff, q, sr);
            const auto fade = juce::jmin (1.0f, b * 20.0f);
            xL += fade * (hpL.highpass (inL) - inL);
            xR += fade * (hpR.highpass (inR) - inR);
        }
        else
        {
            hpL.highpass (inL);
            hpR.highpass (inR);
        }

        // ---- Stutter: volume chops that speed up in stages as the build rises ----
        auto gateTarget = 1.0f;
        if (stutterAmt > 0.0f && b >= 0.25f)
        {
            const auto rateQ = b < 0.5f ? 1.0 : b < 0.75f ? 0.5 : b < 0.9f ? 0.25 : stutterFinal;
            const auto phase = std::fmod (clock.getPosition(), rateQ) / rateQ;
            gateTarget = phase < 0.5 ? 1.0f : 1.0f - stutterAmt;
        }
        stutterGain += stutterSmoothing * (gateTarget - stutterGain);
        xL *= stutterGain;
        xR *= stutterGain;

        // ---- Wash: reverb send rises with the build, tail rings unless cut at the drop ----
        const auto send = active ? washAmt * std::pow (b, 1.5f) : 0.0f;
        float wL, wR;
        washVerb.process (washHpL.highpass (xL * send), washHpR.highpass (xR * send), wL, wR);

        // ---- Noise riser: band-passed noise sweeping 400 Hz -> 12 kHz ----
        float nL = 0.0f, nR = 0.0f;
        const auto nzL = noiseL.process(), nzR = noiseR.process();
        if (active && noiseAmt > 0.0f)
        {
            const auto centre = 400.0f * std::pow (30.0f, b);
            noiseBpL.set (centre, 1.2f, sr);
            noiseBpR.set (centre, 1.2f, sr);
            const auto lvl = noiseAmt * b * b * 0.5f;
            nL = noiseBpL.bandpass (nzL) * lvl;
            nR = noiseBpR.bandpass (nzR) * lvl;
        }

        // ---- Tonal riser: detuned saws gliding up to the root note ----
        float rL = 0.0f, rR = 0.0f;
        if (active && riserAmt > 0.0f)
        {
            const auto note = riserBottom + (riserTop - riserBottom) * b;
            for (size_t i = 0; i < saws.size(); ++i)
            {
                saws[i].setFrequency (suite::music::midiToHz (note + detune[i]), sr);
                (i % 2 == 0 ? rL : rR) += saws[i].process();
            }
            const auto cutoff = 600.0f * std::pow (15.0f, b);
            riserLpL.set (cutoff, 0.9f, sr);
            riserLpR.set (cutoff, 0.9f, sr);
            const auto lvl = riserAmt * b * b * 0.12f;
            rL = riserLpL.lowpass (rL) * lvl;
            rR = riserLpR.lowpass (rR) * lvl;
        }

        // ---- Widen: push the sides up as the build rises ----
        auto outL = xL + (wL * 0.9f + nL + rL) * tailGain * endFade;
        auto outR = xR + (wR * 0.9f + nR + rR) * tailGain * endFade;
        if (active && widenAmt > 0.0f)
        {
            const auto mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR);
            const auto sideGain = 1.0f + widenAmt * b, midGain = 1.0f - 0.2f * widenAmt * b;
            outL = mid * midGain + side * sideGain;
            outR = mid * midGain - side * sideGain;
        }

        // Level-matched builds: everything the build adds (the resonant sweep, wash, risers, widening)
        // changes the sound, never the level - the output is held at the input's loudness (300 ms
        // RMS; a silent input still lets the risers through at up to -24 dBFS). Only ever turns down.
        {
            const auto inP = 0.5 * ((double) inL * inL + (double) inR * inR), outP = 0.5 * ((double) outL * outL + (double) outR * outR);
            levelIn += levelCoeff * (inP - levelIn);
            levelOut += levelCoeff * (outP - levelOut);
            const auto allowed = juce::jmax (levelIn, 0.004);
            const auto target = levelOut > allowed * 1.0233 ? (float) std::sqrt (allowed / levelOut) : 1.0f;   // (0.1 dB of slack)
            levelGain += (target < levelGain ? levelAttack : levelRelease) * (target - levelGain);
            if (levelGain > 0.9999f && target >= 1.0f) levelGain = 1.0f;
            outL *= levelGain; outR *= levelGain;
            minLevelGain = juce::jmin (minLevelGain, levelGain);
        }

        const auto o = outGain.getNextValue();
        if (right != nullptr)
        {
            left[n] = outL * o;
            right[n] = outR * o;
        }
        else
        {
            left[n] = 0.5f * (outL + outR) * o;
        }
        inPeakBlock = juce::jmax (inPeakBlock, std::abs (inL));
        outPeakBlock = juce::jmax (outPeakBlock, std::abs (outL), std::abs (outR));
    }

    // Autopilot (stability): the build filter ringing on its own - its output far over its input
    // and over full scale for 50 ms or more - is flagged; the message thread takes the resonance down
    {
        const auto ringing = outPeakBlock > 1.0f && outPeakBlock > 4.0f * inPeakBlock;
        ringingSeconds = ringing ? ringingSeconds + numSamples / sr : 0.0f;
        if (ringingSeconds > 0.05f) { ringingNow.store (true); ringingSeconds = 0.0f; }
        inPeakBlock = outPeakBlock = 0.0f;
    }

    levelHoldDb.store (juce::Decibels::gainToDecibels (minLevelGain));
    minLevelGain = 1.0f;
    buildLevel.store (build);
    sectionBuild.store (sectionProgress >= 0.0);
    progress.store (sectionProgress >= 0.0 ? sectionProgress : clock.getProgress());
    lengthBars.store (sectionProgress >= 0.0 ? sectionBars : clock.getLengthBars());
}

juce::AudioProcessorEditor* CauseEffectProcessor::createEditor()
{
    return new CauseEffectEditor (*this);
}

void CauseEffectProcessor::getPluginState (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CauseEffectProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CauseEffectProcessor();
}

bool CauseEffectProcessor::ringingTick()
{
    if (! ringingNow.exchange (false) || ! suite::Autopilot::on (suite::Autopilot::stability)) return false;
    const auto res = apvts.getRawParameterValue (Params::filterRes)->load();
    if (res <= 0.05f) return false;
    setParameterPlain (apvts, Params::filterRes, juce::jmax (0.0f, res - 0.15f));
    ++ringingCaught;
    suite::Autopilot::count (suite::Autopilot::runawaysCaught);
    suite::MoveLog::add (getName(), getTrackName(), "Runaway caught: the build filter rang on its own; resonance " + juce::String (juce::roundToInt (res * 100.0f)) + "% -> "
                         + juce::String (juce::roundToInt (juce::jmax (0.0f, res - 0.15f) * 100.0f)) + "%", suite::MoveLog::fix);
    return true;
}
