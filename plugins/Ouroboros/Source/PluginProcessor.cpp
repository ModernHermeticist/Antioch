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

void OuroborosProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    const auto sr = (float) sampleRate;
    lowBand.set (100.0f, 0.707f, sr);
    highBand.set (6000.0f, 0.707f, sr);
    lowBand.reset();
    highBand.reset();
    envAttack = 1.0f - std::exp (-1.0f / (0.002f * sr));
    envRelease = 1.0f - std::exp (-1.0f / (0.04f * sr));
    lowEnv = 0.0f;
}

void OuroborosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Audio passes through untouched; we watch the transport and (when listening) measure the mix.
    const auto host = suite::HostInfo::read (getPlayHead(), bpm.load());
    bpm.store (host.bpm);
    barQuarters.store (host.barQuarters());
    playing.store (host.playing);
    if (host.hasPpq)
        ppq.store (host.ppq);

    if (resetFeatures.exchange (false))
        for (auto& b : barData) { b.flux.store (0.0f); b.high.store (0.0f); b.full.store (0.0f); b.samples.store (0); }

    if (! listening.load() || ! host.playing || ! host.hasPpq || buffer.getNumChannels() == 0)
        return;

    const auto* l = buffer.getReadPointer (0);
    const auto* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    const auto qPerSample = host.quartersPerSample (sampleRate);
    const auto barQ = host.barQuarters();

    int currentBar = -1, count = 0;
    float flux = 0.0f, high = 0.0f, full = 0.0f;
    auto flush = [&]
    {
        if (currentBar < 0 || currentBar >= maxListenBars || count == 0) return;
        auto& b = barData[(size_t) currentBar];
        b.flux.store (b.flux.load() + flux);
        b.high.store (b.high.load() + high);
        b.full.store (b.full.load() + full);
        b.samples.store (b.samples.load() + count);
    };

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        const auto bar = (int) std::floor ((host.ppq + qPerSample * n) / barQ);
        if (bar != currentBar)
        {
            flush();
            currentBar = bar; count = 0; flux = high = full = 0.0f;
        }

        const auto mono = 0.5f * (l[n] + r[n]);
        const auto lo = std::abs (lowBand.lowpass (mono));
        const auto hi = highBand.highpass (mono);
        const auto before = lowEnv;
        lowEnv += (lo > lowEnv ? envAttack : envRelease) * (lo - lowEnv);
        flux += std::max (0.0f, lowEnv - before);     // low-end attacks: kicks
        high += hi * hi;
        full += mono * mono;
        ++count;
    }
    flush();
}

std::vector<listen::BarFeature> OuroborosProcessor::features() const
{
    std::vector<listen::BarFeature> out;
    int last = -1;
    for (int b = 0; b < maxListenBars; ++b)
        if (barData[(size_t) b].samples.load() > 0) last = b;
    for (int b = 0; b <= last; ++b)
    {
        const auto& d = barData[(size_t) b];
        listen::BarFeature f;
        const auto n = d.samples.load();
        f.heard = n > 0;
        if (f.heard)
        {
            f.kickFlux = d.flux.load();
            f.highDb = 10.0f * std::log10 (d.high.load() / (float) n + 1.0e-12f);
            f.fullDb = 10.0f * std::log10 (d.full.load() / (float) n + 1.0e-12f);
        }
        out.push_back (f);
    }
    return out;
}

int OuroborosProcessor::barsHeard() const
{
    int n = 0;
    for (const auto& b : barData) if (b.samples.load() > 0) ++n;
    return n;
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
