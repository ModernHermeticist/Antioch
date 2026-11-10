#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "TargetBus.h"

//==============================================================================
ReferenceProfile ReferenceProfile::analyse (const juce::File& file, const std::atomic<bool>* cancel)
{
    ReferenceProfile profile;
    profile.name = file.getFileNameWithoutExtension();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return profile;

    auto meterL = std::make_unique<analysis::LoudnessMeter>();
    auto meterTp = std::make_unique<analysis::TruePeakMeter>();
    auto meterBands = std::make_unique<analysis::BandAnalyzer>();
    meterL->prepare (reader->sampleRate);
    meterTp->reset();
    meterBands->prepare (reader->sampleRate);

    constexpr int chunk = 1 << 15;
    juce::AudioBuffer<float> buffer (2, chunk);
    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += chunk)
    {
        if (cancel != nullptr && cancel->load())
            return profile;
        const auto n = (int) std::min<juce::int64> (chunk, reader->lengthInSamples - pos);
        reader->read (&buffer, 0, n, pos, true, true);
        const auto* l = buffer.getReadPointer (0);
        const auto* r = reader->numChannels > 1 ? buffer.getReadPointer (1) : l;
        for (int i = 0; i < n; ++i)
        {
            meterL->process (l[i], r[i]);
            meterTp->process (l[i], r[i]);
            meterBands->process (l[i], r[i]);
        }
    }

    profile.lufs = meterL->integrated();
    profile.truePeakDb = meterTp->peakDb();
    for (size_t b = 0; b < (size_t) analysis::numBands; ++b)
        profile.bands[b] = meterBands->longDb[b].load();
    profile.valid = profile.lufs > -70.0f;
    return profile;
}

//==============================================================================
MentalismProcessor::MentalismProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MentalismState", Params::createLayout())
{
    timerCallback();
    startTimerHz (4);
}

void MentalismProcessor::timerCallback()
{
    // Tell Lapis (and anything else listening) which loudness target we're measuring against.
    const auto r = report();
    suite::TargetBus::publish (r.targetLufs, r.ceiling);
}

MentalismProcessor::~MentalismProcessor()
{
    stopTimer();
    suite::TargetBus::withdraw();
    cancelLoad.store (true);
    if (loader.joinable())
        loader.join();
}

bool MentalismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

void MentalismProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    loudness.prepare (sampleRate);
    truePeak.reset();
    stereo.prepare (sampleRate);
    bands.prepare (sampleRate);
    mono.assign ((size_t) samplesPerBlock, 0.0f);
}

void MentalismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (resetRequested.exchange (false))
    {
        loudness.reset();
        truePeak.reset();
        bands.reset();
    }

    // Measure only: the audio passes through untouched.
    const auto n = buffer.getNumSamples();
    const auto* l = buffer.getReadPointer (0);
    const auto* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
    if ((int) mono.size() < n) mono.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        loudness.process (l[i], r[i]);
        truePeak.process (l[i], r[i]);
        stereo.process (l[i], r[i]);
        bands.process (l[i], r[i]);
        mono[(size_t) i] = 0.5f * (l[i] + r[i]);
    }
    spectrumTap.push (mono.data(), n);
}

void MentalismProcessor::loadReference (const juce::File& file)
{
    if (loading.load())
        return;
    if (loader.joinable())
        loader.join();

    loading.store (true);
    cancelLoad.store (false);
    loader = std::thread ([this, file]
    {
        const auto profile = ReferenceProfile::analyse (file, &cancelLoad);
        if (profile.valid)
            setReference (profile);
        loading.store (false);
    });
}

ReferenceProfile MentalismProcessor::getReference() const
{
    const juce::ScopedLock sl (referenceLock);
    return reference;
}

void MentalismProcessor::setReference (const ReferenceProfile& p)
{
    const juce::ScopedLock sl (referenceLock);
    reference = p;
}

MentalismProcessor::Report MentalismProcessor::report() const
{
    Report rep;
    const auto ref = getReference();
    const auto loudIndex = (int) apvts.getRawParameterValue (Params::loudnessTarget)->load();
    const auto tonalIndex = (int) apvts.getRawParameterValue (Params::tonalTarget)->load();

    const auto& lt = advisor::loudnessTargets()[(size_t) juce::jlimit (0, (int) advisor::loudnessTargets().size() - 1, loudIndex)];
    rep.ceiling = lt.ceilingDbtp;
    if (loudIndex == advisor::matchReferenceIndex && ref.valid)
    {
        rep.targetLufs = ref.lufs;
        rep.targetName = "the reference (" + juce::String (ref.lufs, 1) + " LUFS)";
    }
    else
    {
        rep.targetLufs = loudIndex == advisor::matchReferenceIndex ? advisor::loudnessTargets()[0].lufs : lt.lufs;
        rep.targetName = loudIndex == advisor::matchReferenceIndex ? juce::String ("the club target (load a reference to match it)")
                                                                   : juce::String (lt.name);
    }

    advisor::Bands target {};
    if (tonalIndex == advisor::referenceTonalIndex)
    {
        rep.tonalAvailable = ref.valid;
        target = ref.bands;
    }
    else
    {
        rep.tonalAvailable = true;
        target = advisor::tonalTargets()[(size_t) juce::jlimit (0, 1, tonalIndex)].curve;
    }

    advisor::Bands shortNow {}, longNow {};
    for (size_t b = 0; b < (size_t) analysis::numBands; ++b)
    {
        shortNow[b] = bands.shortDb[b].load();
        longNow[b] = bands.longDb[b].load();
    }
    rep.measuredSeconds = bands.longSeconds.load();
    rep.shortDiff = advisor::difference (shortNow, target);
    rep.longDiff = advisor::difference (longNow, target);

    auto& m = rep.measurement;
    m.integratedLufs = loudness.integrated();
    m.truePeakDb = truePeak.peakDb();
    m.correlation = stereo.correlation.load();
    m.lowCorrelation = stereo.lowCorrelation.load();
    m.monoLossDb = stereo.monoLossDb.load();
    m.haveTonal = rep.tonalAvailable && rep.measuredSeconds >= 10.0f; // need some music before judging balance
    m.bandDiff = rep.longDiff;

    rep.momentary = loudness.momentary.load();
    rep.shortTerm = loudness.shortTerm.load();
    rep.advice = advisor::suggestions (m, rep.targetLufs, rep.ceiling, rep.targetName);
    return rep;
}

juce::AudioProcessorEditor* MentalismProcessor::createEditor()
{
    return new MentalismEditor (*this);
}

void MentalismProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const auto ref = getReference();
    if (ref.valid)
    {
        juce::ValueTree r ("Reference");
        r.setProperty ("name", ref.name, nullptr);
        r.setProperty ("lufs", ref.lufs, nullptr);
        r.setProperty ("tp", ref.truePeakDb, nullptr);
        juce::StringArray b;
        for (auto v : ref.bands) b.add (juce::String (v, 3));
        r.setProperty ("bands", b.joinIntoString (","), nullptr);
        state.removeChild (state.getChildWithName ("Reference"), nullptr);
        state.appendChild (r, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MentalismProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto r = state.getChildWithName ("Reference");
    if (r.isValid())
    {
        ReferenceProfile p;
        p.name = r["name"].toString();
        p.lufs = (float) r["lufs"];
        p.truePeakDb = (float) r["tp"];
        const auto values = juce::StringArray::fromTokens (r["bands"].toString(), ",", "");
        for (int i = 0; i < juce::jmin (values.size(), analysis::numBands); ++i)
            p.bands[(size_t) i] = values[i].getFloatValue();
        p.valid = values.size() == analysis::numBands;
        setReference (p);
        state.removeChild (r, nullptr);
    }
    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MentalismProcessor();
}
