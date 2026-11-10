#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostInfo.h"
#include "KeyBus.h"

CorrespondenceProcessor::CorrespondenceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CorrespondenceState", Params::createLayout())
{
    publishKey();
    startTimerHz (4);
}

CorrespondenceProcessor::~CorrespondenceProcessor()
{
    stopTimer();
    if (wasBroadcasting)
        suite::KeyBus::withdraw();
}

bool CorrespondenceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out || in == juce::AudioChannelSet::mono();
}

void CorrespondenceProcessor::prepareToPlay (double sampleRate, int)
{
    preview.prepare (sampleRate);
}

void CorrespondenceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    currentBpm.store (suite::HostInfo::read (getPlayHead(), currentBpm.load()).bpm);

    // Audio passes straight through; only the audition voice is added.
    preview.render (buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr,
                    buffer.getNumSamples(), apvts.getRawParameterValue (Params::preview)->load());
}

void CorrespondenceProcessor::publishKey()
{
    const auto on = apvts.getRawParameterValue (Params::broadcast)->load() > 0.5f;
    if (on)
    {
        suite::KeyBus::publish ((int) apvts.getRawParameterValue (Params::rootNote)->load(),
                                (int) apvts.getRawParameterValue (Params::scale)->load());
    }
    else if (wasBroadcasting)
    {
        suite::KeyBus::withdraw();
    }
    wasBroadcasting = on;
}

void CorrespondenceProcessor::applyPreset (int index)
{
    const auto& presets = chords::presets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;
    const auto& p = presets[(size_t) index];

    auto setChoice = [this] (const juce::String& id, int value)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) value));
            param->endChangeGesture();
        }
    };

    if (p.scale >= 0)
        setChoice (Params::scale, p.scale);
    setChoice (Params::count, p.count);
    for (int i = 0; i < chords::maxSlots; ++i)
    {
        setChoice (Params::slotDegree (i), p.slots[(size_t) i].degree - 1);
        setChoice (Params::slotQuality (i), (int) p.slots[(size_t) i].quality);
    }
}

juce::File CorrespondenceProcessor::exportClip (const juce::String& kind, const juce::File& directory) const
{
    const auto s = Params::readSettings (apvts);
    const auto notes = kind == "Bass" ? chords::bassClip (s) : kind == "Arp" ? chords::arpClip (s) : chords::chordClip (s);

    juce::String progression;
    for (int i = 0; i < s.count; ++i)
        progression << (i > 0 ? "-" : "") << chords::chordName (s, s.slots[(size_t) i]);

    const auto key = suite::music::noteNames()[s.root] + (s.scale == 1 ? "" : "m");
    const auto file = directory.getChildFile (juce::File::createLegalFileName (key + " " + kind + " " + progression + ".mid"));

    directory.createDirectory();
    file.deleteFile();
    if (auto stream = file.createOutputStream())
        chords::toMidiFile (notes, currentBpm.load(), "Correspondence " + kind).writeTo (*stream);
    return file;
}

juce::AudioProcessorEditor* CorrespondenceProcessor::createEditor()
{
    return new CorrespondenceEditor (*this);
}

void CorrespondenceProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CorrespondenceProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    publishKey();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CorrespondenceProcessor();
}
