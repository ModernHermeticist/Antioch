#include "PluginProcessor.h"
#include "ChordBus.h"
#include "KickBus.h"
#include "VocalBus.h"
#include "PluginEditor.h"
#include "HostInfo.h"
#include "Music.h"

namespace
{
    constexpr double maxDelaySeconds = 6.0;
}

AntiochProcessor::AntiochProcessor()
    : SuiteProcessor (BusesProperties()
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

    // Section scenes: what moves per section, and sensible starting offsets.
    using T = suite::SectionType;
    scenes.setup (apvts,
                  { { dry, "Dry" }, { echoLevel, "Echo level" }, { verbLevel, "Reverb level" }, { throwLevel, "Throw level" },
                    { Verb::decay, "Reverb decay" }, { Verb::shimmer, "Shimmer" }, { resMix, "Echo harmonics" },
                    { damping, "Echo brightness" } },
                  { { T::Breakdown, sVerb, 0.25f }, { T::Breakdown, sDecay, 0.15f }, { T::Breakdown, sShimmer, 0.30f },
                    { T::Breakdown, sEcho, 0.15f }, { T::Breakdown, sHarmonics, 0.20f },
                    { T::Build, sVerb, 0.10f }, { T::Build, sEcho, 0.10f }, { T::Build, sEchoDamp, 0.10f },
                    { T::Drop, sVerb, -0.10f }, { T::Drop, sShimmer, -0.10f },
                    { T::Intro, sVerb, 0.05f }, { T::Outro, sVerb, 0.10f }, { T::Outro, sEcho, 0.10f },
                    { T::count, sEcho, 0.10f }, { T::count, sShimmer, 0.05f } });   // Drop 2+: a little more
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
    scenes.prepare();
    reverb.prepare (sampleRate);
    freeze.prepare (sampleRate);
    freezeSectionIndex = -1;
    freezeLastPos = -1.0e9;
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
    scenes.beginBlock (host, sampleRate, numSamples);

    // ---- Key: follow Correspondence if it's broadcasting, else use local key ----
    auto root = choiceParam (rootNote)->getIndex();
    const auto bus = suite::KeyBus::read();
    const auto follow = boolParam (followKey)->get() && bus.valid;
    if (follow)
        root = bus.root;
    // Final-drop key change from Ouroboros: the harmonies move up with the song.
    if (sections.refresh() && host.playing && host.hasPpq)
        if (const auto sec = sections.locate (host.ppq, host.barQuarters()); sec.valid)
            root += sec.keyShift;
    effectiveRoot.store (root % 12);
    followingBus.store (follow);
    busScale.store (bus.scale);

    const auto rootHz = suite::music::midiToHz ((float) (12 * (intParam (rootOct)->get() + 1) + root));
    const auto synced = boolParam (Params::sync)->get();

    // ---- Echo ----
    {
        HarmonicEcho::Settings s;
        s.resonance = value (resonance);
        s.blend     = scenes.plain (sHarmonics);
        s.dampHz    = scenes.plain (sEchoDamp);
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
        s.decay = scenes.plain (sDecay);
        s.size = value (Verb::size);
        s.predelayMs = value (Verb::predelay);
        s.dampHz = value (Verb::damping);
        s.lowCutHz = value (Verb::lowCut);
        s.highCutHz = value (Verb::highCut);
        s.shimmer = scenes.plain (sShimmer);
        s.shimmerSemitones = Verb::shimmerSemitones (choiceParam (Verb::shimInt)->getIndex());
        // Tension width: the reverb opens out on chords that pull away from home.
        {
            suite::Progression prog;
            float target = 0.0f;
            if (host.playing && host.hasPpq && suite::ChordBus::read (prog))
            {
                auto origin = 0.0;
                if (sections.refresh())
                    if (const auto sec = sections.locate (host.ppq, host.barQuarters()); sec.valid) origin = sec.startQ;
                target = prog.isTension (prog.chordAt (host.ppq - origin)) ? 1.0f : 0.0f;
            }
            tensionNow.store (tensionNow.load() + 0.3f * (target - tensionNow.load()));
        }
        s.width = juce::jmin (2.0f, value (Verb::width) * (1.0f + 0.5f * value (Verb::tension) * tensionNow.load()));
        reverbWidthNow.store (s.width);
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
    const auto vocalThrows = autoIndex == Throw::autoAtVocalEnds && host.playing && host.hasPpq && suite::VocalBus::any();
    vocalMarksFound.store (suite::VocalBus::any());
    // Reverb pump: the tail ducks to the kick pattern a Kybalion shares (no routing).
    const auto pumpDepth = value (Verb::pump);
    const auto kickPattern = pumpDepth > 0.0f && host.playing && host.hasPpq ? suite::KickBus::read() : suite::KickPatternSet {};
    const auto msPerQ = 60000.0 / juce::jmax (1.0, host.bpm);
    const auto pumpRelease = 1.0f - std::exp (-1.0f / (0.003f * sr));
    float minPump = 1.0f;
    const auto barQ = host.barQuarters();
    const auto phraseQ = autoBars * host.barQuarters();
    const auto lengthQ = Throw::autoLengthQuarters (choiceParam (Throw::autoLen)->getIndex());
    const auto windowQ = autoOn ? juce::jmin (phraseQ, lengthQ) : lengthQ;
    const auto qPerSample = host.quartersPerSample (sampleRate);
    const auto freezeBarsNow = host.hasPpq ? Verb::freezeBars (choiceParam (Verb::freeze)->getIndex()) : 0;
    const auto freezeLevelNow = value (Verb::freezeLevel);
    if (freezeBarsNow == 0) freeze.stop();

    // ---- Levels ----
    const auto out = juce::Decibels::decibelsToGain (value (output));
    dryGain.setTargetValue (scenes.plain (sDry));
    echoGain.setTargetValue (scenes.plain (sEcho));
    verbGain.setTargetValue (scenes.plain (sVerb));
    throwGain.setTargetValue (scenes.plain (sThrow));
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
        if (vocalThrows && ! gateOpen)
        {
            // Throw the last words of every vocal phrase: the end of a sung bar followed by one
            // the vocal doesn't sing in (as Solve et Coagula marked them).
            const auto posQ = host.ppq + qPerSample * n;
            const auto bar = (int) std::floor (posQ / barQ + 1.0e-9) + 1;
            const auto barEnd = bar * barQ;
            gateOpen = posQ >= barEnd - juce::jmin (windowQ, barQ) && suite::VocalBus::sings (bar) && ! suite::VocalBus::sings (bar + 1);
        }
        if (! gateOpen && haveSections && host.playing && host.hasPpq && sections.getMap().momentCount > 0)
        {
            // A throw moment (Ouroboros): the beat (or the chosen length) before its bar.
            const auto posQ = host.ppq + qPerSample * n;
            const auto bar = (int) std::floor (posQ / barQ + 1.0e-9) + 1;
            gateOpen = sections.getMap().hasMoment (bar + 1, suite::momentThrow) && posQ >= bar * barQ - juce::jmin (lengthQ, barQ);
        }
        float tL, tR;
        throwDelay.process (inL, inR, gateOpen, tL, tR);
        const auto tg = throwGain.getNextValue() * tDuck;
        tL *= tg; tR *= tg;

        // Reverb gets the dry signal plus sends from the echo and throw sections.
        const auto es = echoSend.getNextValue(), ts = throwSend.getNextValue();
        float rL, rR;
        reverb.process (inL + es * eL + ts * tL, inR + es * eR + ts * tR, vDuck, rL, rR);
        auto vg = verbGain.getNextValue();
        {
            float pumpTarget = 1.0f;
            if (kickPattern.valid)
            {
                const auto posQ = host.ppq + qPerSample * n;
                auto phase = std::fmod (posQ, barQ);
                if (phase < 0.0) phase += barQ;
                int type = -1;
                if (haveSections)
                    if (const auto sec = sections.locate (posQ, barQ); sec.valid) type = (int) sec.type;
                const auto ms = kickPattern.msSinceHit (type, phase, barQ, msPerQ);
                if (ms < 1.0e8f) pumpTarget = 1.0f - pumpDepth * 0.85f * std::exp (-ms / 140.0f);
            }
            reverbPump += (pumpTarget < reverbPump ? 0.5f : pumpRelease) * (pumpTarget - reverbPump);
            minPump = juce::jmin (minPump, reverbPump);
            vg *= reverbPump;
        }

        // Reverb freeze: entering a breakdown, the last half second of reverb holds as a wash.
        float fL = 0.0f, fR = 0.0f;
        if (freezeBarsNow > 0)
        {
            freeze.push (rL * vg, rR * vg);
            if (host.playing && haveSections)
            {
                const auto posQ = host.ppq + qPerSample * n;
                const auto sec = sections.locate (posQ, barQ);
                const auto jumped = posQ < freezeLastPos - 1.0e-6 || posQ > freezeLastPos + 0.5;
                if (sec.valid && sec.index != freezeSectionIndex && freezeSectionIndex >= 0 && ! jumped && sec.type == suite::SectionType::Breakdown)
                {
                    freeze.trigger ((int) (freezeBarsNow * barQ / qPerSample), freezeLevelNow);
                    freezesPlayed.fetch_add (1);
                }
                freezeSectionIndex = sec.valid ? sec.index : -1;
                freezeLastPos = posQ;
            }
            freeze.process (fL, fR);
        }

        const auto d = dryGain.getNextValue();
        const auto o = outGain.getNextValue();
        const auto oL = (inL * d + eL + tL + rL * vg + fL) * o;
        const auto oR = (inR * d + eR + tR + rR * vg + fR) * o;

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
    pumpNow.store (minPump);
    duckAmount.store (maxDuck);
}

juce::AudioProcessorEditor* AntiochProcessor::createEditor()
{
    return new AntiochEditor (*this);
}

void AntiochProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    scenes.writeInto (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AntiochProcessor::setPluginState (const void* data, int sizeInBytes)
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
    return new AntiochProcessor();
}
