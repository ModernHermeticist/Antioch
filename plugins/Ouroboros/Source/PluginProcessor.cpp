#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostInfo.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { OuroborosProcessor::broadcastId, 1 }, "Broadcast", true));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { OuroborosProcessor::snapId, 1 }, "Mark Snap",
                                                             juce::StringArray { "1 bar", "4 bars", "8 bars", "16 bars" }, 2));
        return l;
    }
}

OuroborosProcessor::OuroborosProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OuroborosState", layout())
{
    map = arrangement::fromTemplate (arrangement::templates().front());
    publish();
    startTimerHz (4);
}

OuroborosProcessor::~OuroborosProcessor()
{
    stopTimer();
    if (wasBroadcasting)
        suite::SectionBus::withdraw();
}

bool OuroborosProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && ! layouts.getMainOutputChannelSet().isDisabled();
}

void OuroborosProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    // Audio passes through untouched; we only watch the transport for the UI.
    const auto host = suite::HostInfo::read (getPlayHead(), bpm.load());
    bpm.store (host.bpm);
    barQuarters.store (host.barQuarters());
    playing.store (host.playing);
    if (host.hasPpq)
        ppq.store (host.ppq);
}

suite::SectionMap OuroborosProcessor::getMap() const
{
    const juce::ScopedLock sl (lock);
    return map;
}

void OuroborosProcessor::setMap (const suite::SectionMap& newMap)
{
    {
        const juce::ScopedLock sl (lock);
        map = newMap;
    }
    publish();
}

void OuroborosProcessor::markAtPlayhead (suite::SectionType type)
{
    const auto barQ = barQuarters.load();
    const auto bar = ppq.load() / barQ + 1.0;
    const auto snap = snapBars ((int) apvts.getRawParameterValue (snapId)->load());
    auto m = getMap();
    arrangement::insertAt (m, juce::jlimit (1, m.totalBars, arrangement::snapBar (bar, snap)), type);
    setMap (m);
}

void OuroborosProcessor::publish()
{
    const auto on = apvts.getRawParameterValue (broadcastId)->load() > 0.5f;
    if (on)
        suite::SectionBus::publish (getMap());
    else if (wasBroadcasting)
        suite::SectionBus::withdraw();
    wasBroadcasting = on;
}

juce::AudioProcessorEditor* OuroborosProcessor::createEditor()
{
    return new OuroborosEditor (*this);
}

void OuroborosProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const auto m = getMap();
    juce::ValueTree arr ("Arrangement");
    arr.setProperty ("totalBars", m.totalBars, nullptr);
    for (int i = 0; i < m.count; ++i)
    {
        juce::ValueTree s ("Section");
        s.setProperty ("start", m.sections[(size_t) i].startBar, nullptr);
        s.setProperty ("type", (int) m.sections[(size_t) i].type, nullptr);
        arr.appendChild (s, nullptr);
    }
    state.removeChild (state.getChildWithName ("Arrangement"), nullptr);
    state.appendChild (arr, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OuroborosProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto arr = state.getChildWithName ("Arrangement");
    if (arr.isValid())
    {
        suite::SectionMap m;
        m.totalBars = juce::jlimit (1, 1024, (int) arr["totalBars"]);
        for (const auto& s : arr)
            if (m.count < suite::SectionMap::maxSections)
                m.sections[(size_t) m.count++] = { juce::jmax (1, (int) s["start"]),
                                                   (suite::SectionType) juce::jlimit (0, (int) suite::SectionType::count - 1, (int) s["type"]) };
        if (m.count > 0)
            m.sections[0].startBar = 1;
        state.removeChild (arr, nullptr);
        apvts.replaceState (state);
        setMap (m);
        return;
    }
    apvts.replaceState (state);
    publish();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OuroborosProcessor();
}
