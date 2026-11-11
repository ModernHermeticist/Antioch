#include "PluginProcessor.h"
#include "ActivityBus.h"
#include "PluginEditor.h"
#include "HostInfo.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        using Attr = AudioParameterFloatAttributes;
        using P = SolveEtCoagulaProcessor;
        AudioProcessorValueTreeState::ParameterLayout l;
        auto percent = Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; });
        auto db = Attr().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });

        l.add (std::make_unique<AudioParameterFloat> (ParameterID { P::depthId, 1 }, "Depth", NormalisableRange<float> (0.0f, 12.0f), 6.0f, db));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { P::sensitivityId, 1 }, "Sensitivity", NormalisableRange<float> (0.0f, 100.0f), 35.0f, percent));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { P::focusId, 1 }, "Focus", NormalisableRange<float> (6.0f, 24.0f), 12.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return v < 9.0f ? String ("Narrow") : v > 18.0f ? String ("Wide") : String (roundToInt (v)) + " dB"; })));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { P::speedId, 1 }, "Speed", StringArray { "Fast", "Medium", "Slow" }, 1));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { P::mixId, 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f), 100.0f, percent));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { P::chopsOnId, 1 }, "Vocal Chops", false));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { P::chopsWhereId, 1 }, "Chops In", StringArray { "Builds", "Drops", "Builds and drops", "Breakdowns" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { P::chopsPatternId, 1 }, "Chop Pattern", StringArray { "Stutter", "Off-beats", "3-3-2", "Shuffled", "Words" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { P::chopsLevelId, 1 }, "Chops Level", NormalisableRange<float> (-24.0f, 6.0f), -6.0f, db));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { P::chopsOctaveId, 1 }, "Chops Octave Up", false));
        suite::SceneEngine::addParameters (l);
        return l;
    }
}

SolveEtCoagulaProcessor::SolveEtCoagulaProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "SolveEtCoagulaState", layout())
{
    // Section scenes: dense drops need more room for the vocal, sparse breakdowns less.
    using T = suite::SectionType;
    scenes.setup (apvts,
                  { { depthId, "Depth" }, { sensitivityId, "Sensitivity" }, { focusId, "Focus" }, { mixId, "Mix" } },
                  { { T::Breakdown, sDepth, -0.25f }, { T::Build, sDepth, 0.1f }, { T::Drop, sDepth, 0.2f },
                    { T::Drop, sFocus, 0.15f }, { T::count, sDepth, 0.05f } });   // Drop 2+
}

bool SolveEtCoagulaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (in != out)
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void SolveEtCoagulaProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    unmasker.prepare (sampleRate);
    chops.prepare (sampleRate);
    melody.prepare (sampleRate);
    inChopSection = false;
    scenes.prepare();
    for (int i = 0; i < 4; ++i) { presence[(size_t) i].set (i % 2 == 0 ? 1000.0f : 4000.0f, 0.7f, (float) sampleRate); presence[(size_t) i].reset(); }
    vocalPresence = musicPresence = 0.0;
    shortVocal = shortMusic = 0.0;
    shortCoeff = 1.0 - std::exp (-1.0 / (0.4 * sampleRate));
    buriedBars.store (0);
    vocalBuried.store (false);
}

void SolveEtCoagulaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Level-neutral scenes: measure what comes in, process, then bring the level back to the drop's (when it's on).
    const auto n = buffer.getNumSamples();
    const auto inPower = suite::SceneEngine::blockPower (buffer, n);
    processSuiteBlock (buffer, midi);
    const auto g = scenes.levelKeepGain (inPower, suite::SceneEngine::blockPower (buffer, n));
    if (std::abs (g - 1.0f) > 1.0e-4f) buffer.applyGain (0, n, g);
}

void SolveEtCoagulaProcessor::processSuiteBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    static const float attack[] = { 3.0f, 10.0f, 25.0f }, release[] = { 60.0f, 150.0f, 350.0f };
    const auto speed = juce::jlimit (0, 2, (int) value (speedId));
    const auto host = suite::HostInfo::read (getPlayHead());
    scenes.beginBlock (host, sampleRate, buffer.getNumSamples());
    Unmasker::Settings s;
    s.depthDb = scenes.plain (sDepth);
    s.thresholdDb = thresholdFor (scenes.plain (sSensitivity));
    s.focusDb = scenes.plain (sFocus);
    s.attackMs = attack[speed];
    s.releaseMs = release[speed];
    s.mix = scenes.plain (sMix) * 0.01f;
    unmasker.update (s);

    auto mainBus = getBusBuffer (buffer, false, 0);
    auto* scBus = getBus (true, 1);
    const auto scActive = scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0;
    sidechainConnected.store (scActive);
    const auto sc = scActive ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();

    const auto numSamples = mainBus.getNumSamples();
    auto* left = mainBus.getWritePointer (0);
    auto* right = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer (1) : nullptr;
    float keyPeak = 0.0f;

    // Vocal chops: listen to the vocal outside the chop sections, play its last bar chopped inside them.
    const auto chopsOn = value (chopsOnId) > 0.5f && scActive && sections.refresh() && host.playing && host.hasPpq;
    const auto where = (int) value (chopsWhereId);
    const auto chopPattern = (int) value (chopsPatternId);
    const auto octaveUp = value (chopsOctaveId) > 0.5f;
    const auto chopGain = juce::Decibels::decibelsToGain (value (chopsLevelId));
    const auto barQ = host.barQuarters();
    const auto qPerSample = host.quartersPerSample (sampleRate);
    bool played = false;
    const auto listenMelody = scActive && host.playing && host.hasPpq;
    if (host.bpm > 1.0) songBpm.store (host.bpm);
    const auto swing = (double) grooveSwing.load();
    auto isChopSection = [where] (suite::SectionType t)
    {
        using T = suite::SectionType;
        return where == inBuilds ? t == T::Build : where == inDrops ? t == T::Drop
             : where == inBuildsAndDrops ? (t == T::Build || t == T::Drop) : t == T::Breakdown;
    };

    for (int n = 0; n < numSamples; ++n)
    {
        float key = 0.0f;
        if (scActive)
        {
            key = sc.getSample (0, n);
            if (sc.getNumChannels() > 1) key = 0.5f * (key + sc.getSample (1, n));
            keyPeak = std::max (keyPeak, std::abs (key));
        }
        auto l = left[n];
        auto r = right != nullptr ? right[n] : l;
        unmasker.process (l, r, key);
        if (listenMelody) melody.process (key, host.ppq + qPerSample * n);
        if (chopsOn)
        {
            const auto pos = host.ppq + qPerSample * n;
            auto posInBar = std::fmod (pos, barQ);
            if (posInBar < 0.0) posInBar += barQ;
            const auto sec = sections.locate (pos, barQ);
            const auto chopHere = sec.valid && isChopSection (sec.type);
            if (chopHere && ! inChopSection) chops.finishBar (barQ, qPerSample);   // keep the bar just heard, if it was a whole one
            inChopSection = chopHere;
            if (chopHere)
            {
                const auto c = chops.play (posInBar, barQ, qPerSample, chopPattern, octaveUp, swing) * chopGain;
                l += c; r += c;
                played = played || chops.ready();
            }
            else
                chops.listen (key, posInBar, barQ, qPerSample);
        }
        left[n] = l;
        if (right != nullptr) right[n] = r;
        if (scActive)
        {
            const auto v = presence[1].lowpass (presence[0].highpass (key));
            const auto m = presence[3].lowpass (presence[2].highpass (0.5f * (l + r)));
            vocalPresence += (double) v * v;
            musicPresence += (double) m * m;
            shortVocal += shortCoeff * ((double) v * v - shortVocal);   // ~400 ms averages, for the vocal rider
            shortMusic += shortCoeff * ((double) m * m - shortMusic);
        }
    }
    chopsReady.store (chops.ready());
    chopsPlaying.store (played);
    vocalPresent.store (juce::Decibels::gainToDecibels (keyPeak, -100.0f) > s.thresholdDb);
    // Vocal rider (Prima Materia on the vocal): how far the vocal sits above the music, while it sings.
    if (scActive && vocalPresent.load() && shortVocal > 1.0e-10 && shortMusic > 1.0e-10)
    {
        vocalOverMusicDb.store ((float) (10.0 * std::log10 (shortVocal / shortMusic)));
        suite::ActivityBus::writeValue (suite::ActivityBus::vocalOverMusic, vocalOverMusicDb.load());
    }
    // Vocal-aware levels (Prima Materia): how strongly the vocal sings now, 0 at the threshold, 1 from 12 dB above it.
    if (scActive)
        suite::ActivityBus::write (suite::ActivityBus::vocal, (juce::Decibels::gainToDecibels (keyPeak, -100.0f) - s.thresholdDb) / 12.0f);

    // Tell the suite which bars the vocal sings in (Correspondence's lead leaves room there).
    if (scActive && host.playing && host.hasPpq && host.barQuarters() > 0.0)
    {
        const auto bar = (int) std::floor (host.ppq / host.barQuarters() + 1.0e-9) + 1;
        if (bar != vocalBar)
        {
            if (vocalBar >= 1 && barBlocks > 0) suite::VocalBus::mark (vocalBar, vocalBlocks * 4 > barBlocks);   // a quarter of the bar or more
            // Live check: was the vocal buried in that bar? Four sung bars in a row and it's flagged.
            if (vocalBar >= 1 && barBlocks > 0 && vocalBlocks * 2 > barBlocks)
            {
                const auto buried = musicPresence > vocalPresence * std::pow (10.0, buriedDb / 10.0) && vocalPresence > 0.0;
                buriedBars.store (buried ? buriedBars.load() + 1 : 0);
                vocalBuried.store (buriedBars.load() >= 4);
            }
            vocalPresence = musicPresence = 0.0;
            vocalBar = bar; vocalBlocks = 0; barBlocks = 0;
        }
        ++barBlocks;
        if (vocalPresent.load()) ++vocalBlocks;
    }
    else
        vocalBar = -1;
}

juce::AudioProcessorEditor* SolveEtCoagulaProcessor::createEditor() { return new SolveEtCoagulaEditor (*this); }

void SolveEtCoagulaProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    scenes.writeInto (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SolveEtCoagulaProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            scenes.readFrom (state);
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SolveEtCoagulaProcessor(); }
