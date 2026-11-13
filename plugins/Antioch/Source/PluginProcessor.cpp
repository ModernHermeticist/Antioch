#include "PluginProcessor.h"
#include "ChordBus.h"
#include "KickBus.h"
#include "ActivityBus.h"
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
    // The problem watcher's mud ladder: the master is heavy in the low mids and the reverb carries
    // them: its low cut half an octave up (at most 900 Hz)
    fixes = std::make_unique<suite::FixReceiver> (apvts, [this] (suite::Fix fix, uint32_t) -> std::vector<suite::FixReceiver::Change>
    {
        if (fix != suite::Fix::ReverbLowCut) return {};
        const auto now = apvts.getRawParameterValue (Params::Verb::lowCut)->load();
        if (now >= 899.0f) return {};
        return { { Params::Verb::lowCut, juce::jmin (900.0f, now * 1.41f) } };
    });
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
    for (auto& f : sibSend) { f.set (7000.0f, 1.5f, (float) newSampleRate); f.reset(); }
    sibEnv.set (1.0f, 60.0f, (float) newSampleRate); sibEnv.reset();
    sibSlow = 0.0f; sibSlowCoeff = 1.0f / (0.4f * (float) newSampleRate); endFade = 1.0f;
    {
        const auto fs = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        lvlFast = 1.0 - std::exp (-1.0 / (0.3 * fs)); lvlSlow = 1.0 - std::exp (-1.0 / (3.0 * fs));
        lvlAttack = 1.0f - std::exp (-1.0f / (0.02f * (float) fs)); lvlRelease = 1.0f - std::exp (-1.0f / (0.3f * (float) fs));
        wetCoeff = 1.0 - std::exp (-1.0 / (0.3 * fs));
        lvlIn = lvlInSlow = lvlOut = 0.0; lvlGain = minLvlGain = 1.0f; wetLR = wetLL = wetRR = 0.0; wetWidth = 1.0f;
    }
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

void AntiochProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Level-neutral scenes: measure what comes in, process, then bring the level back to the drop's (when it's on).
    const auto n = buffer.getNumSamples();
    const auto inPower = suite::SceneEngine::blockPower (buffer, n);
    processSuiteBlock (buffer, midi);
    const auto g = scenes.levelKeepGain (inPower, suite::SceneEngine::blockPower (buffer, n));
    if (std::abs (g - 1.0f) > 1.0e-4f) buffer.applyGain (0, n, g);
}

void AntiochProcessor::processSuiteBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
    // Chord-aware: the resonators ring on the current chord's root (from Correspondence), counted from the section start.
    if (boolParam (Params::followChords)->get() && host.playing && host.hasPpq)
    {
        suite::Progression prog;
        if (suite::ChordBus::read (prog))
        {
            double origin = 0.0;
            if (sections.refresh())
                if (const auto sec = sections.locate (host.ppq, host.barQuarters()); sec.valid) origin = sec.startQ;
            auto r = prog.chordRoots[(size_t) (prog.chordAt (host.ppq - origin) & 7)];
            if (r > 6) r -= 12;   // the nearest way: up to a tritone either side
            root += r;
            chordShift.store (r);
        }
    }
    else chordShift.store (0);
    effectiveRoot.store (((root % 12) + 12) % 12);
    followingBus.store (follow);
    busScale.store (bus.scale);

    const auto rootHz = suite::music::midiToHz ((float) (12 * (intParam (rootOct)->get() + 1) + root));
    const auto synced = boolParam (Params::sync)->get();

    // ---- Echo ----
    {
        HarmonicEcho::Settings s;
        s.resonance = value (resonance);
        s.blend     = scenes.plain (sHarmonics);
        s.dampHz    = scenes.plain (sEchoDamp) * (1.0f - 0.5f * darkening);
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
        // Autopilot (mixing): the space in time with the song - the pre-delay on the nearest note
        // value (1/128 to 1/16), the decay down to whole beats (the section scenes still lengthen it in
        // breakdowns and tighten it in drops)
        if (suite::Autopilot::on (suite::Autopilot::tempoSpaceF) && host.bpm > 1.0)
        {
            const auto beat = (float) (60.0 / host.bpm);
            if (s.predelayMs > 1.0f)
            {
                float best = s.predelayMs, bestErr = 1.0e9f;
                for (const auto div : { 32.0f, 16.0f, 8.0f, 4.0f })   // a beat over these: 1/128, 1/64, 1/32 and 1/16 notes
                {
                    const auto ms = 1000.0f * beat / div;
                    if (std::abs (std::log (ms / s.predelayMs)) < bestErr) { bestErr = std::abs (std::log (ms / s.predelayMs)); best = ms; }
                }
                s.predelayMs = best;
            }
            s.decay = juce::jmax (beat, std::floor (s.decay / beat + 1.0e-3f) * beat);   // (rounded down: never longer than set)
            timedNow.store (true);
        }
        else timedNow.store (false);
        // Autopilot (harshness): while the master is harsh, the tails darken (damping down an octave at most)
        const auto harsh = suite::Autopilot::on (suite::Autopilot::tailDarkenF) ? suite::Autopilot::harshNow() : 0.0f;
        darkening += 0.2f * (harsh - darkening);
        if (! darkened && darkening > 0.3f) { darkened = true; suite::Autopilot::count (suite::Autopilot::tailsDamped); }
        if (darkening < 0.05f) darkened = false;
        s.dampHz = value (Verb::damping) * (1.0f - 0.5f * darkening);
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
    // Autopilot (mixing): the space always makes room - it pumps with the kick (at least 35%) and
    // steps back for the vocal as well as the lead
    const auto mixingRoom = suite::Autopilot::on (suite::Autopilot::reverbRoomF);
    const auto noteRoomOn = suite::Autopilot::on (suite::Autopilot::noteRoomF), wetSafeOn = suite::Autopilot::on (suite::Autopilot::wetMonoSafeF),
               deessOn = suite::Autopilot::on (suite::Autopilot::deessSpaceF);
    const auto pumpDepth = mixingRoom ? juce::jmax (0.35f, value (Verb::pump)) : value (Verb::pump);
    const auto kickPattern = pumpDepth > 0.0f && host.playing && host.hasPpq ? suite::KickBus::read() : suite::KickPatternSet {};
    const auto msPerQ = 60000.0 / juce::jmax (1.0, host.bpm);
    const auto pumpRelease = 1.0f - std::exp (-1.0f / (0.003f * sr));
    float minPump = 1.0f;
    // The song's end (the map's last bar): the space fades to silence over the next 2 bars
    {
        float target = 1.0f;
        if (suite::Autopilot::on (suite::Autopilot::cleanEndingF) && haveSections && host.playing && host.hasPpq && sections.getMap().totalBars > 0)
        {
            const auto over = (host.ppq - sections.getMap().totalBars * host.barQuarters()) / (2.0 * host.barQuarters());
            if (over > 0.0) target = (float) juce::jlimit (0.0, 1.0, 1.0 - over);
        }
        if (target < 0.99f && endFade >= 0.99f) suite::Autopilot::count (suite::Autopilot::endingsFaded);
        endFade = target;
    }
    const auto holdLevel = suite::Autopilot::on (suite::Autopilot::levelMatchF);
    // Lead-aware space: the effects step back while the lead plays, and bloom in its gaps.
    const auto leadAmount = onLeadTrack.load() ? 0.0f : value (Verb::leadRoom);
    {
        const auto hz = noteRoomOn && ! onLeadTrack.load() ? suite::Autopilot::leadPitch() : 0.0f;
        const auto act = hz > 100.0f ? juce::jlimit (0.0f, 1.0f, suite::ActivityBus::read (suite::ActivityBus::lead)) : 0.0f;
        if (hz > 100.0f && std::abs (hz - noteDipHz) > 0.01f * hz)
        {
            for (int c = 0; c < 2; ++c) { noteDip[(size_t) c].set (hz, 4.0f, sr); noteDip[(size_t) c + 2].set (juce::jmin (hz * 2.0f, 0.45f * sr), 4.0f, sr); }
            noteDipHz = hz;
        }
        noteRoom += 0.3f * (act - noteRoom);
        if (noteRoom > 0.5f && ! noteRoomCounted) { noteRoomCounted = true; suite::Autopilot::count (suite::Autopilot::reverbMadeRoom); }
        if (noteRoom < 0.1f) noteRoomCounted = false;
        noteRoomNow.store (noteRoom);
    }
    const auto leadNow = mixingRoom ? juce::jmax (suite::ActivityBus::read (suite::ActivityBus::lead), suite::ActivityBus::read (suite::ActivityBus::vocal))
                                    : suite::ActivityBus::read (suite::ActivityBus::lead);
    const auto leadTarget = 1.0f - 0.6f * leadAmount * juce::jlimit (0.0f, 1.0f, leadNow);
    if (mixingRoom && (leadTarget < 0.8f || (kickPattern.valid && pumpDepth > 0.0f && host.playing)) && ! madeRoom) { madeRoom = true; suite::Autopilot::count (suite::Autopilot::reverbMadeRoom); }
    const auto leadAttack = 1.0f - std::exp (-1.0f / (0.02f * sr)), leadRelease = 1.0f - std::exp (-1.0f / (0.25f * sr));
    const auto barQ = host.barQuarters();
    const auto phraseQ = autoBars * host.barQuarters();
    const auto lengthQ = Throw::autoLengthQuarters (choiceParam (Throw::autoLen)->getIndex());
    const auto windowQ = autoOn ? juce::jmin (phraseQ, lengthQ) : lengthQ;
    const auto qPerSample = host.quartersPerSample (sampleRate);
    const auto freezeBarsNow = host.hasPpq ? Verb::freezeBars (choiceParam (Verb::freeze)->getIndex()) : 0;
    const auto freezeLevelNow = value (Verb::freezeLevel);
    const auto directorLive = suiteDirector.refreshFor (getInstanceId()) && host.playing && host.hasPpq;
    const auto dirMuteStep = (float) (1.0 / (0.003 * sampleRate));
    if (freezeBarsNow == 0 && ! directorLive) freeze.stop();

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

    // The space: its own trim (unless the suite's automation is bypassed), and the energy curve:
    // where the energy is drawn up the space tightens (up to 3 dB), where it's drawn down it opens
    float energyDb = 0.0f;
    if (sections.refresh() && host.hasPpq)
        if (const auto at = sections.locate (host.ppq, host.barQuarters()); at.valid)
            energyDb = juce::jlimit (-3.0f, 3.0f, -3.0f * (at.energyRatio - 1.0f));
    const auto spaceGain = juce::Decibels::decibelsToGain ((suite::AutomationBus::bypassed() ? 0.0f : value (spaceTrim)) + energyDb);
    // The lead's phrases: while it plays (Vibration says so), automatic throws on other tracks wait
    const auto holdForLead = ! onLeadTrack.load() && suite::ActivityBus::read (suite::ActivityBus::lead) > 0.5f;
    throwsHeld.store (holdForLead);
    if (spaceReset.exchange (false)) { spaceDry = spaceWet = 0.0; spaceSamples = 0; spaceHeard.store (0.0f); spaceTailDb.store (-100.0f); }
    bool spaceHere = true;   // outside breakdowns
    if (haveSections && host.hasPpq)
        if (const auto sec = sections.locate (host.ppq, host.barQuarters()); sec.valid && sec.type == suite::SectionType::Breakdown) spaceHere = false;

    auto* left  = buffer.getWritePointer (0);
    auto* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto* inRight = numIn > 1 ? buffer.getReadPointer (1) : nullptr;
    float maxDuck = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto inL = left[n];
        const auto inR = inRight != nullptr ? inRight[n] : inL;
        auto sendL = inL, sendR = inR;   // what feeds the echo, reverb and throws
        // Autopilot (mixing): the space is de-essed - an "s" that spikes 6 dB over the recent 7 kHz
        // level comes down in what's sent in (the track itself keeps it)
        if (deessOn)
        {
            const auto bl = sibSend[0].bandpass (inL) / 1.5f, br = sibSend[1].bandpass (inR) / 1.5f;
            const auto e = sibEnv.process (0.5f * (std::abs (bl) + std::abs (br)));
            sibSlow += sibSlowCoeff * (e * e - sibSlow);
            const auto thr = 2.0f * std::sqrt (sibSlow) + 1.0e-5f;
            const auto g = e > thr ? juce::jmax (0.25f, thr / e) : 1.0f;
            sendL -= bl * (1.0f - g); sendR -= br * (1.0f - g);
            minSib = juce::jmin (minSib, g);
        }
        const auto mono = 0.5f * (sendL + sendR);

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

        // The suite director (every 32 samples): this track's throw, wash or freeze, the tails cut
        // at drops, dry into the hit and wide on it, and the director's silences
        if (directorLive && (n & 31) == 0)
        {
            namespace dr = suite::director;
            const auto posQ = host.ppq + qPerSample * n;
            dirThrow = false; dirWash = 0.0f; dirSpace = 1.0f; dirWide = 0.0f;
            if (const auto at = suiteDirector.actingAt (posQ, dr::pAntioch);
                at >= 0 && (suiteDirector[at].heroRole == 0 || trackRole.load() <= 0 || trackRole.load() == (int) suiteDirector[at].heroRole))   // the leading track's Antioch (or a return)
            {
                const auto& m = suiteDirector[at];
                const auto part = m.hero == dr::pAntioch ? 1.0f : 0.6f;
                const auto g = m.gesture[dr::pAntioch];
                if (g == dr::gAnThrow) dirThrow = true;
                if (g == dr::gAnWash) dirWash = 0.8f * part;
                if (g == dr::gAnFreeze && at != dirFrozeAt) { freeze.trigger ((int) (2.0 * barQ / qPerSample), 0.7f * part); dirFrozeAt = at; freezesPlayed.fetch_add (1); }
                if (at != dirReported) { suite::DirectorBus::reportActed (suiteDirector.hash(), at, dr::pAntioch); dirReported = at; }
            }
            if (const auto at = suiteDirector.momentNear (posQ, barQ, barQ, dr::kIntoDrop | dr::kPin); at >= 0)
            {
                const auto& m = suiteDirector[at];
                if (posQ < m.atQ)
                {
                    const auto into = juce::jlimit (0.0, 1.0, (posQ - (m.atQ - barQ)) / barQ);
                    dirSpace = 1.0f - m.dryBefore * (float) into;                                // dry into the hit
                    if (m.cutTails && posQ >= (m.silence != dr::gNone ? m.silenceStartQ : m.atQ - 0.25f)) dirSpace = 0.0f;   // tails cut
                }
                else
                {
                    dirWide = m.wideOnHit * (float) (1.0 - (posQ - m.atQ) / barQ);              // wide on the one, settling over the bar
                }
            }
            dirSilent = suiteDirector.silenceAt (posQ) >= 0;
        }
        if (! directorLive) { dirThrow = false; dirWash = 0.0f; dirSpace = 1.0f; dirWide = 0.0f; dirSilent = false; }
        dirSpaceGain += (dirSpace < dirSpaceGain ? 0.004f : 0.0002f) * (dirSpace - dirSpaceGain);   // cut in ~5 ms, back over ~100 ms
        dirMute += juce::jlimit (-dirMuteStep, dirMuteStep, (dirSilent ? 0.0f : 1.0f) - dirMute);

        // Throw (manual button/automation or automatic phrase-end window)
        bool gateOpen = manualThrow || dirThrow;
        if (autoOn && ! gateOpen && ! holdForLead)
        {
            auto pos = std::fmod (host.ppq + qPerSample * n, phraseQ);
            if (pos < 0.0) pos += phraseQ;
            gateOpen = pos >= phraseQ - windowQ;
        }
        if (sectionThrows && ! gateOpen && ! holdForLead)
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
        throwDelay.process (sendL, sendR, gateOpen, tL, tR);
        const auto tg = throwGain.getNextValue() * tDuck;
        tL *= tg; tR *= tg;

        // Reverb gets the dry signal plus sends from the echo and throw sections.
        const auto es = echoSend.getNextValue(), ts = throwSend.getNextValue();
        float rL, rR;
        reverb.process (sendL + es * eL + ts * tL, sendR + es * eR + ts * tR, vDuck, rL, rR);
        // Autopilot (mixing): the tail makes room at the lead's note and its octave while it plays
        if (noteRoom > 0.01f)
        {
            const auto g = 1.0f - 0.5f * noteRoom;   // up to 6 dB
            rL -= (noteDip[0].bandpass (rL) + noteDip[2].bandpass (rL)) / 4.0f * (1.0f - g);
            rR -= (noteDip[1].bandpass (rR) + noteDip[3].bandpass (rR)) / 4.0f * (1.0f - g);
        }
        leadRoomGain += (leadTarget < leadRoomGain ? leadAttack : leadRelease) * (leadTarget - leadRoomGain);
        eL *= leadRoomGain; eR *= leadRoomGain; tL *= leadRoomGain; tR *= leadRoomGain;
        auto vg = verbGain.getNextValue() * leadRoomGain;
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
        if (dirWash > 0.0f) vg *= 1.0f + dirWash;   // the director's wash: the space opens up
        if (freezeBarsNow > 0 || directorLive)
        {
            freeze.push (rL * vg, rR * vg);
            if (host.playing && haveSections)
            {
                const auto posQ = host.ppq + qPerSample * n;
                const auto sec = sections.locate (posQ, barQ);
                const auto jumped = posQ < freezeLastPos - 1.0e-6 || posQ > freezeLastPos + 0.5;
                if (freezeBarsNow > 0 && ! directorLive && sec.valid && sec.index != freezeSectionIndex && freezeSectionIndex >= 0 && ! jumped && sec.type == suite::SectionType::Breakdown)
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
        auto spaceL = (eL + rL * vg + fL) * spaceGain * dirSpaceGain, spaceR = (eR + rR * vg + fR) * spaceGain * dirSpaceGain;
        if (dirWide > 0.01f)
        {
            // wide on the hit (the mono-safety below still narrows anything that would cancel)
            const auto mid = 0.5f * (spaceL + spaceR), side = 0.5f * (spaceL - spaceR) * (1.0f + dirWide);
            spaceL = mid + side; spaceR = mid - side;
        }
        // Autopilot (mixing): the space stays mono-safe - where its two sides would cancel (correlation
        // under -0.2 over 300 ms), its stereo part narrows to half until it's safe again
        {
            wetLR += wetCoeff * ((double) spaceL * spaceR - wetLR);
            wetLL += wetCoeff * ((double) spaceL * spaceL - wetLL);
            wetRR += wetCoeff * ((double) spaceR * spaceR - wetRR);
            const auto corr = wetLL * wetRR > 1.0e-14 ? wetLR / std::sqrt (wetLL * wetRR) : 1.0;
            const auto target = wetSafeOn && corr < -0.2 ? 0.5f : 1.0f;
            wetWidth += (target < wetWidth ? 0.002f : 0.0002f) * (target - wetWidth);
            if (wetWidth < 0.999f)
            {
                const auto mid = 0.5f * (spaceL + spaceR), side = 0.5f * (spaceL - spaceR) * wetWidth;
                spaceL = mid + side; spaceR = mid - side;
            }
            if (! wetNarrowing && wetWidth < 0.7f) { wetNarrowing = true; suite::Autopilot::count (suite::Autopilot::wetNarrowed); }
            if (wetWidth > 0.95f) wetNarrowing = false;
        }
        // Autopilot (mixing): a clean ending - past the map's last bar, the space fades out over 2 bars
        if (endFade < 1.0f) { spaceL *= endFade; spaceR *= endFade; tL *= endFade; tR *= endFade; }
        auto oL = (inL * d + spaceL + tL) * o * dirMute;
        auto oR = (inR * d + spaceR + tR) * o * dirMute;
        // Autopilot (stability): level-matched - what the space adds never lifts the track over its
        // input's loudness (300 ms; the last 3 s of input carry the tails after a phrase ends)
        if (holdLevel)
        {
            const auto inP = 0.5 * ((double) inL * inL + (double) inR * inR) * (double) o * o, outP = 0.5 * ((double) oL * oL + (double) oR * oR);
            lvlIn += lvlFast * (inP - lvlIn);
            lvlInSlow += lvlSlow * (inP - lvlInSlow);
            lvlOut += lvlFast * (outP - lvlOut);
            const auto allowed = juce::jmax (lvlIn, lvlInSlow) * 1.0233;
            const auto target = lvlOut > allowed && lvlOut > 1.0e-9 ? (float) std::sqrt (allowed / lvlOut) : 1.0f;
            lvlGain += (target < lvlGain ? lvlAttack : lvlRelease) * (target - lvlGain);
            if (lvlGain > 0.9999f && target >= 1.0f) lvlGain = 1.0f;
            oL *= lvlGain; oR *= lvlGain;
            minLvlGain = juce::jmin (minLvlGain, lvlGain);
        }
        if (spaceHere)
        {
            spaceDry += 0.5 * ((double) inL * inL + (double) inR * inR) * d * d;
            spaceWet += 0.5 * ((double) spaceL * spaceL + (double) spaceR * spaceR);
            if (std::abs (inL) + std::abs (inR) > 2.0e-3f) ++spaceSamples;   // the part is playing
        }

        if (right != nullptr)
        {
            left[n] = oL;
            right[n] = oR;
        }
        else
        {
            left[n] = 0.5f * (oL + oR);
        }
        // Autopilot (stability): the tails' level and the input's, for the runaway guard
        runWet += 0.5 * ((double) (spaceL + tL) * (spaceL + tL) + (double) (spaceR + tR) * (spaceR + tR));
        runIn += 0.5 * ((double) inL * inL + (double) inR * inR);
        runPeak = juce::jmax (runPeak, std::abs (spaceL + tL), std::abs (spaceR + tR));
    }

    // Runaway guard: tails that grow while the input is quiet (feedback or shimmer feeding itself),
    // or that run far over full scale, are flagged; the message thread takes the feedback down
    {
        const auto wetRms = std::sqrt (runWet / juce::jmax (1, numSamples)), inRms = std::sqrt (runIn / juce::jmax (1, numSamples));
        const auto growing = inRms < 1.0e-3 && wetRms > 1.0e-3 && wetRms > lastWetRms * 1.002;
        risingSeconds = growing ? risingSeconds + numSamples / (float) sampleRate : 0.0f;
        if (risingSeconds > 0.75f || runPeak > 2.0f) { runawayNow.store (true); risingSeconds = 0.0f; }
        lastWetRms = wetRms;
        runWet = runIn = 0.0; runPeak = 0.0f;
    }

    spaceHeard.store ((float) ((double) spaceSamples / sampleRate));
    if (spaceDry > 1.0e-9 && spaceWet > 1.0e-12) spaceTailDb.store ((float) (10.0 * std::log10 (spaceWet / spaceDry)));
    throwGate.store (throwDelay.gateLevel());
    pumpNow.store (minPump);
    sibNow.store (juce::Decibels::gainToDecibels (minSib));
    if (! sibCounted && minSib < 0.7f) { sibCounted = true; suite::Autopilot::count (suite::Autopilot::spaceDeessed); }
    minSib = 1.0f;
    levelHoldDb.store (juce::Decibels::gainToDecibels (minLvlGain));
    if (! levelHeld && minLvlGain < 0.85f) { levelHeld = true; suite::Autopilot::count (suite::Autopilot::levelMatched); }
    if (minLvlGain > 0.97f) levelHeld = false;
    minLvlGain = 1.0f;
    leadRoomNow.store (leadRoomGain);
    duckAmount.store (maxDuck);
}

bool AntiochProcessor::glueRoomTick()
{
    if (glueRoomChecked || getTrackName().isEmpty()) return false;
    glueRoomChecked = true;   // one chance, while it's untouched
    if (! onBusTrack() || ! untouched()) return false;
    using namespace Params;
    for (const auto& [id, v] : std::initializer_list<std::pair<juce::String, float>> {
             { dry, 1.0f }, { echoLevel, 0.0f }, { throwLevel, 0.0f }, { verbLevel, 0.25f }, { Verb::decay, 0.9f }, { Verb::size, 0.35f },
             { Verb::predelay, 12.0f }, { Verb::shimmer, 0.0f }, { Verb::lowCut, 400.0f }, { autoSpace, 1.0f } })
        setParameterPlain (apvts, id, v);
    captureFreshValues();
    suite::MoveLog::add (getName(), getTrackName(), "Glue room: one short shared room (0.9 s) for everything on the bus, its level set by itself");
    return true;
}

float AntiochProcessor::spaceTargetFor (int role)
{
    switch (role)
    {
        case suite::roleLead:  return -12.0f;
        case suite::roleVocal: return -14.0f;
        case suite::rolePads:  return -10.0f;
        case suite::roleHats:  return -16.0f;
        case suite::roleFx:    return -8.0f;
        default:               return 0.0f;   // kick and bass stay dry
    }
}

bool AntiochProcessor::spacePass()
{
    if (value (Params::autoSpace) < 0.5f || spaceHeard.load() < spacePassSeconds) return false;
    const auto role = roleForTrack();
    const auto target = onBusTrack() ? -18.0f : spaceTargetFor (role);   // the music bus: a glue room, well under the music
    const auto tail = spaceTailDb.load();
    spaceReset.store (true);
    if (target == 0.0f || tail < -90.0f) return false;
    const auto err = std::abs (tail - target);
    static const char* names[] = { "track", "kick", "bass", "hats", "pad", "lead", "vocal", "FX" };
    const juce::String name = names[juce::jlimit (0, 7, role)];

    if (space.pending)
    {
        space.pending = false;
        const auto masterWorse = suite::Conductor::verdictFor (getInstanceId()) == suite::Conductor::worse;   // Mentalism's verdict on the master
        suite::Conductor::release (getInstanceId());
        if (err < space.errBefore - 0.5f && ! masterWorse)
        {
            ++spaceKept;
            suite::MoveLog::add (getName(), getTrackName(), "Kept: space " + juce::String (value (Params::spaceTrim), 1) + " dB (the tail "
                                 + juce::String (space.errBefore, 1) + " -> " + juce::String (err, 1) + " dB from a " + name + "'s)", suite::MoveLog::kept);
        }
        else
        {
            setParameterPlain (apvts, Params::spaceTrim, space.trimBefore);
            ++spaceUndone;
            space.rest = 6;
            suite::MoveLog::add (getName(), getTrackName(), "Undid: space back to " + juce::String (space.trimBefore, 1) + " dB: the tail didn't get closer to a "
                                 + name + "'s (" + juce::String (space.errBefore, 1) + " -> " + juce::String (err, 1) + " dB)", suite::MoveLog::undone);
            return true;
        }
    }
    if (space.rest > 0) { --space.rest; return false; }
    if (err <= 1.5f) return false;
    const auto trim = value (Params::spaceTrim);
    const auto step = std::round (juce::jlimit (-1.5f, 1.5f, target - tail) * 2.0f) * 0.5f;
    const auto next = juce::jlimit (-12.0f, 6.0f, trim + step);
    if (std::abs (next - trim) < 0.05f) return false;
    if (! suite::Conductor::request (getInstanceId())) return false;   // one move at a time across the mix
    space = { true, trim, err, 0 };
    setParameterPlain (apvts, Params::spaceTrim, next);
    suite::MoveLog::add (getName(), getTrackName(), "Space " + juce::String (trim, 1) + " -> " + juce::String (next, 1) + " dB (the echo and reverb sit "
                         + juce::String (-tail, 1) + " dB under the " + name + "; it usually wants about " + juce::String (juce::roundToInt (-target)) + ")");
    return true;
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

bool AntiochProcessor::runawayTick()
{
    if (! runawayNow.exchange (false) || ! suite::Autopilot::on (suite::Autopilot::runawayGuardF)) return false;
    using namespace Params;
    juce::StringArray moved;
    for (int t = 0; t < numTaps; ++t)
        if (const auto fb = apvts.getRawParameterValue (tapId (t, Tap::feedback))->load(); fb > 0.05f)
            setParameterPlain (apvts, tapId (t, Tap::feedback), fb * 0.75f);
    if (const auto fb = apvts.getRawParameterValue (Throw::feedback)->load(); fb > 0.05f) setParameterPlain (apvts, Throw::feedback, fb * 0.75f);
    if (const auto sh = apvts.getRawParameterValue (Verb::shimmer)->load(); sh > 0.05f) setParameterPlain (apvts, Verb::shimmer, sh * 0.75f);
    ++runawaysCaught;
    suite::Autopilot::count (suite::Autopilot::runawaysCaught);
    suite::MoveLog::add (getName(), getTrackName(), "Runaway caught: the tails were growing on their own; feedback and shimmer down a quarter", suite::MoveLog::fix);
    return true;
}
