#include "PluginProcessor.h"
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
    scenes.prepare();
}

void SolveEtCoagulaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    static const float attack[] = { 3.0f, 10.0f, 25.0f }, release[] = { 60.0f, 150.0f, 350.0f };
    const auto speed = juce::jlimit (0, 2, (int) value (speedId));
    scenes.beginBlock (suite::HostInfo::read (getPlayHead()), sampleRate, buffer.getNumSamples());
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
        left[n] = l;
        if (right != nullptr) right[n] = r;
    }
    vocalPresent.store (juce::Decibels::gainToDecibels (keyPeak, -100.0f) > s.thresholdDb);
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
