#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KeyBus.h"
#include "Music.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        using Attr = AudioParameterFloatAttributes;
        auto percent = Attr().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
        AudioProcessorValueTreeState::ParameterLayout l;
        auto id = [] (const String& s) { return ParameterID { s, 1 }; };

        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::followId), "Follow Ouroboros", true));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::levelId), "FX Level", NormalisableRange<float> (-24.0f, 6.0f), -6.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::toneId), "Tone", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::impactId), "Impact", NormalisableRange<float> (0.0f, 1.0f), 0.8f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::crashId), "Crash", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::swellId), "Swell", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::downId), "Downlifter", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::rollId), "Snare Roll", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::swellLenId), "Swell Length", StringArray { "1 beat", "2 beats", "1 bar", "2 bars" }, 1));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::rollStartId), "Roll Starts", StringArray { "Whole build", "Second half", "Last 4 bars" }, 0));
        l.add (std::make_unique<AudioParameterFloat> (id (AzothProcessor::rollPitchId), "Roll Pitch Rise", NormalisableRange<float> (0.0f, 12.0f, 1.0f), 7.0f,
                                                      Attr().withStringFromValueFunction ([] (float v, int) { return "+" + String (roundToInt (v)) + " st"; })));
        l.add (std::make_unique<AudioParameterChoice> (id (AzothProcessor::gapId), "Gap Before Drop", StringArray { "None", "1/2 beat", "1 beat" }, 0));
        l.add (std::make_unique<AudioParameterBool> (id (AzothProcessor::grooveCrashId), "Crash Into Groove", true));
        return l;
    }
}

AzothProcessor::AzothProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AzothState", layout())
{
}

bool AzothProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
        && (layouts.getMainInputChannelSet() == out || layouts.getMainInputChannelSet().isDisabled());
}

void AzothProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    const auto sr = (float) sampleRate;
    impact.prepare (sr);
    crash.prepare (sr);
    downlifter.prepare (sr);
    for (auto& s : snares) s.prepare (sr);
    swellFilter.reset();
    previousIndex = -1;
    rollPos = -1.0e9;
}

void AzothProcessor::enterSection (suite::SectionType type, suite::SectionType previous, double bpm, double barQ, float rootHz)
{
    using T = suite::SectionType;
    if (type == T::Drop)
    {
        impact.trigger (rootHz, value (impactId));
        crash.trigger (value (crashId), value (toneId));
    }
    else if (type == T::Breakdown)
    {
        const auto twoBars = (float) (2.0 * barQ * 60.0 / bpm);
        downlifter.trigger (twoBars, rootHz * 4.0f, value (downId));
    }
    else if (type == T::Groove && previous == T::Intro && value (grooveCrashId) > 0.5f)
    {
        crash.trigger (0.6f * value (crashId), value (toneId));
    }
    else
    {
        return;   // nothing plays when entering this section
    }
    eventsFired.fetch_add (1);
}

void AzothProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    using T = suite::SectionType;

    const auto host = suite::HostInfo::read (getPlayHead());
    const auto haveMap = sections.refresh();
    mapAvailable.store (haveMap);
    const auto follow = value (followId) > 0.5f && haveMap && host.playing && host.hasPpq;
    const auto barQ = host.barQuarters();
    const auto qPerSample = host.quartersPerSample (sampleRate);

    // Sub elements land on the song's root (octave 1), from Correspondence/Emerald Tablet; A if unknown.
    const auto key = suite::KeyBus::read();
    const auto rootHz = suite::music::midiToHz ((float) (24 + (key.valid ? key.root : 9)));

    const auto sr = (float) sampleRate;
    const auto level = juce::Decibels::decibelsToGain (value (levelId));
    const auto swellLevel = value (swellId), rollLevel = value (rollId);
    const auto swellQ = swellQuarters ((int) value (swellLenId));
    const auto gapQ = gapQuarters ((int) value (gapId));
    const auto rollStart = (int) value (rollStartId);
    const auto rollPitch = value (rollPitchId);

    // Auditions from the UI play immediately.
    if (const auto a = pendingAudition.exchange (0))
    {
        if (a & aImpact) impact.trigger (rootHz, juce::jmax (0.5f, value (impactId)));
        if (a & aCrash)  crash.trigger (juce::jmax (0.5f, value (crashId)), value (toneId));
        if (a & aDown)   downlifter.trigger ((float) (2.0 * barQ * 60.0 / host.bpm), rootHz * 4.0f, juce::jmax (0.5f, value (downId)));
        if (a & aSnare)  snares[0].trigger (juce::jmax (0.5f, rollLevel), 1.0f);
    }

    // Status for the UI: what's next and when.
    if (follow)
    {
        const auto now = sections.locate (host.ppq, barQ);
        nextType.store (now.valid && now.hasNext ? (int) now.next : -1);
        barsToNext.store (now.valid ? (float) ((now.endQ - host.ppq) / barQ) : 0.0f);
    }
    else
    {
        nextType.store (-1);
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        float fx = 0.0f;

        if (follow)
        {
            const auto pos = host.ppq + qPerSample * n;
            const auto sec = sections.locate (pos, barQ);
            const auto jumped = previousIndex >= 0 && (pos < previousPos - 1.0e-6 || pos > previousPos + 0.5);

            // Entering a new section while playing forward fires its transition.
            if (sec.valid && previousIndex >= 0 && ! jumped && sec.index != previousIndex)
                enterSection (sec.type, previousType, host.bpm, barQ, rootHz);
            previousIndex = sec.valid ? sec.index : -1;
            if (sec.valid) previousType = sec.type;
            previousPos = pos;
            const auto rollPrevPos = rollPos;
            rollPos = pos;

            if (sec.valid)
            {
                const auto end = sec.endQ - (sec.hasNext && sec.next == T::Drop ? gapQ : 0.0);

                // Snare roll through the Build, accelerating in steps and rising in pitch.
                if (sec.type == T::Build && rollLevel > 0.0f)
                {
                    const auto startQ = rollStart == 0 ? sec.startQ
                                      : rollStart == 1 ? 0.5 * (sec.startQ + sec.endQ)
                                                       : juce::jmax (sec.startQ, sec.endQ - 4.0 * barQ);
                    if (pos >= startQ && pos < end)
                    {
                        const auto p = (pos - startQ) / juce::jmax (1.0e-6, end - startQ);
                        const auto rate = p < 0.25 ? 1.0 : p < 0.5 ? 0.5 : p < 0.75 ? 0.25 : 0.125;
                        // A hit whenever playback crosses a grid line at the current rate.
                        const auto crossed = rollPrevPos < pos && pos - rollPrevPos < 0.5
                                             && (int64_t) std::floor (pos / rate) != (int64_t) std::floor (rollPrevPos / rate);
                        if (crossed)
                        {
                            snares[(size_t) nextSnare].trigger (rollLevel * (0.35f + 0.65f * (float) p),
                                                                std::exp2 (rollPitch * (float) p / 12.0f));
                            nextSnare = (nextSnare + 1) % (int) snares.size();
                            snareHits.fetch_add (1);
                        }
                    }
                }

                // Swell: rising, opening noise into every Drop.
                if (swellLevel > 0.0f && sec.hasNext && sec.next == T::Drop)
                {
                    const auto s = (pos - (end - swellQ)) / swellQ;
                    if (s >= 0.0 && s < 1.0)
                    {
                        const auto x = (float) s;
                        swellFilter.set (400.0f * std::pow (30.0f, x), 0.7f, sr);
                        fx += swellFilter.lowpass (swellNoise.process()) * x * x * swellLevel * 0.6f;
                    }
                }
            }
        }
        else
        {
            previousIndex = -1;
        }

        fx += impact.process() + crash.process() + downlifter.process();
        for (auto& s : snares) fx += s.process();

        fx *= level;
        left[n] += fx;
        if (right != nullptr) right[n] += fx;
    }
}

juce::AudioProcessorEditor* AzothProcessor::createEditor()
{
    return new AzothEditor (*this);
}

void AzothProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AzothProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AzothProcessor();
}
