#include "EmeraldTabletProcessor.h"
#include "EmeraldTabletEditor.h"

EmeraldTabletProcessor::EmeraldTabletProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EmeraldTabletState", []
      {
          juce::AudioProcessorValueTreeState::ParameterLayout l;
          l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoSendId, 1 }, "Auto-send to Correspondence", true));
          l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { broadcastAloneId, 1 }, "Broadcast without Correspondence", true));
          return l;
      }())
{
    startTimerHz (2);
}

EmeraldTabletProcessor::~EmeraldTabletProcessor()
{
    stopTimer();
    if (broadcastingAlone.load())
        suite::KeyBus::withdraw();
}

bool EmeraldTabletProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && ! layouts.getMainOutputChannelSet().isDisabled();
}

void EmeraldTabletProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    detector.prepare (sampleRate);
    mono.assign ((size_t) samplesPerBlock, 0.0f);
}

void EmeraldTabletProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (resetRequested.exchange (false))
        detector.reset();

    // Listen only: the audio passes through untouched. With Mentalism on the master, the key comes
    // from the whole mix instead (update()), and this track's own audio isn't needed.
    if (fromMaster.load()) return;
    const auto n = buffer.getNumSamples();
    if ((int) mono.size() < n) mono.resize ((size_t) n);
    const auto* l = buffer.getReadPointer (0);
    const auto* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    for (int i = 0; i < n; ++i)
        mono[(size_t) i] = 0.5f * (l[i] + r[i]);
    detector.process (mono.data(), n);
}

void EmeraldTabletProcessor::update()
{
    if (const auto master = suite::MasterKeyBus::read(); master.valid)
    {
        fromMaster.store (true);
        detector.setChroma (master.chroma, master.seconds);
    }
    else fromMaster.store (false);
    const auto best = detector.best.load();
    const auto seconds = detector.seconds.load();
    const auto confident = best >= 0 && detector.confidence.load() >= minConfidence && seconds >= minSeconds;

    // The same key must win for a while before we act on it.
    if (best != stableCandidate)
    {
        stableCandidate = best;
        stableSince = seconds;
    }
    const auto stable = confident && seconds - stableSince >= stableSeconds;

    if (stable && apvts.getRawParameterValue (autoSendId)->load() > 0.5f && best != lastSent.load())
        sendNow();

    // With no Correspondence running, publish the key to the suite ourselves.
    const auto current = suite::KeyBus::read();
    const auto alone = apvts.getRawParameterValue (broadcastAloneId)->load() > 0.5f && (! current.valid || current.fromDetector);
    if (alone && lastSent.load() >= 0)
    {
        const auto k = lastSent.load();
        suite::KeyBus::publish (k % 12, scaleIndexFor (k >= 12), true);
        broadcastingAlone.store (true);
    }
    else if (broadcastingAlone.load() && ! alone)
    {
        broadcastingAlone.store (false);
    }
}

void EmeraldTabletProcessor::sendNow()
{
    const auto best = detector.best.load();
    if (best < 0) return;
    suite::KeyBus::suggest (best % 12, scaleIndexFor (best >= 12));
    lastSent.store (best);
}

juce::AudioProcessorEditor* EmeraldTabletProcessor::createEditor()
{
    return new EmeraldTabletEditor (*this);
}

void EmeraldTabletProcessor::getPluginState (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EmeraldTabletProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

