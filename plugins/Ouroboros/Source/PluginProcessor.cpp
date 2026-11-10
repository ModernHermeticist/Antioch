#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Music.h"
#include "HostInfo.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { OuroborosProcessor::broadcastId, 1 }, "Broadcast", true));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { OuroborosProcessor::snapId, 1 }, "Mark Snap",
                                                             juce::StringArray { "1 bar", "4 bars", "8 bars", "16 bars" }, 2));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { OuroborosProcessor::liftId, 1 }, "Final Drop Key Change",
                                                             juce::StringArray { "None", "+1 semitone", "+2 semitones" }, 0));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { OuroborosProcessor::intensityId, 1 }, "Intensity",
                                                            juce::NormalisableRange<float> (0.0f, 200.0f), 100.0f,
                                                            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { OuroborosProcessor::liveId, 1 }, "Live Mode", false));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { OuroborosProcessor::dropGapId, 1 }, "Before Drops",
                                                             juce::StringArray { "Per track", "1/2 beat silence", "1 beat silence", "Kick-only bar" }, 0));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { OuroborosProcessor::finalSnapId, 1 }, "Final Drop Snapshot",
                                                             juce::StringArray { "None", "A", "B", "C", "D" }, 0));
        return l;
    }
}

OuroborosProcessor::OuroborosProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OuroborosState", layout())
{
    map = arrangement::fromTemplate (arrangement::templates().front());
    publish();
    startTimerHz (20);   // live sections must reach the suite well before the next bar line
}

OuroborosProcessor::~OuroborosProcessor()
{
    stopTimer();
    if (referenceThread.joinable()) referenceThread.join();
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

void OuroborosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Audio passes through untouched; we watch the transport and (when listening) measure the mix.
    const auto host = suite::HostInfo::read (getPlayHead(), bpm.load());
    bpm.store (host.bpm);
    barQuarters.store (host.barQuarters());
    playing.store (host.playing);
    if (host.hasPpq)
        ppq.store (host.ppq);

    // Live mode: a note picks the section that starts on the next bar line.
    if (apvts.getRawParameterValue (liveId)->load() > 0.5f && host.hasPpq)
    {
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (! msg.isNoteOn()) continue;
            const auto type = liveTypeForNote (msg.getNoteNumber());
            if (type < 0) continue;
            const auto at = host.ppq + host.quartersPerSample (sampleRate) * meta.samplePosition;
            livePrevType.store (liveType.load() >= 0 ? liveType.load() : type);
            liveType.store (type);
            liveStartQ.store (liveStartFor (at, host.barQuarters()));
            liveIndex.fetch_add (1);
        }
    }
    else
        liveType.store (-1);
    midi.clear();   // the notes are for Ouroboros only

    // Final-drop snapshot: tell the suite how far to morph at this position (also while stopped,
    // so the plugins match wherever the playhead is).
    if (const auto slot = (int) apvts.getRawParameterValue (finalSnapId)->load() - 1; slot >= 0 && host.hasPpq && morphFollower.refresh())
    {
        const auto amount = finalMorphAmount (morphFollower.locate (host.ppq, host.barQuarters()), morphFollower.getMap(), host.ppq);
        morphAmount.store (amount);
        suite::MorphBus::publish (slot, amount);
    }
    else
        morphAmount.store (0.0f);

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

suite::SectionMap OuroborosProcessor::proposeFromFile (const juce::File& file, double referenceBpm)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return {};
    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 15.0 * 60.0));   // up to 15 minutes
    juce::AudioBuffer<float> audio ((int) juce::jlimit (1u, 2u, reader->numChannels), length);
    reader->read (&audio, 0, length, 0, true, true);
    return listen::propose (listen::analyseAudio (audio, reader->sampleRate, referenceBpm));
}

void OuroborosProcessor::analyseReference (const juce::File& file, double referenceBpm)
{
    if (analysingReference.exchange (true))
        return;
    if (referenceThread.joinable()) referenceThread.join();
    referenceThread = std::thread ([this, file, referenceBpm]
    {
        auto proposal = proposeFromFile (file, referenceBpm);
        {
            const juce::ScopedLock sl (lock);
            referenceResult = proposal;
            lastReferenceName = file.getFileNameWithoutExtension();
        }
        analysingReference.store (false);
    });
}

std::optional<suite::SectionMap> OuroborosProcessor::takeReferenceProposal()
{
    const juce::ScopedLock sl (lock);
    auto r = referenceResult;
    referenceResult.reset();
    return r;
}

void OuroborosProcessor::logActivity()
{
    static const char* fixNames[] = { "", "breakdowns quieter", "breakdowns louder", "more rise in the build", "intro quieter",
                                      "bigger later drops", "rough mix", "DJ-friendly intro/outro", "tone matched to the reference", "masking carve" };
    for (auto seq = seenFix + 1; seq <= suite::FixBus::latest() && seq != 0; ++seq)
    {
        suite::FixBus::Message m;
        if (! suite::FixBus::read (seq, m)) continue;
        const auto f = (int) m.fix;
        addNote (m.undo ? juce::String ("Undid a fix")
                        : juce::String ("Fix: ") + (f > 0 && f < (int) std::size (fixNames) ? fixNames[f] : "other"));
    }
    seenFix = suite::FixBus::latest();

    static const char* snapshotVerbs[] = { "", "Saved snapshot", "Recalled snapshot", "Cleared snapshot" };
    for (auto seq = seenSnapshot + 1; seq <= suite::SnapshotBus::latest() && seq != 0; ++seq)
    {
        suite::SnapshotAction action {};
        int slot = 0;
        if (! suite::SnapshotBus::read (seq, action, slot)) continue;
        if (action == suite::SnapshotAction::ApplyVersion)
            addNote (slot == suite::radioEdit ? "Made the radio edit" : "Made the extended mix");
        else if ((int) action >= 1 && (int) action <= 3)
            addNote (juce::String (snapshotVerbs[(int) action]) + " " + SuiteProcessor::slotName (slot));
    }
    seenSnapshot = suite::SnapshotBus::latest();

    for (auto seq = seenSession + 1; seq <= suite::SessionBus::latest() && seq != 0; ++seq)
    {
        suite::SessionRequest r;
        if (suite::SessionBus::read (seq, r) && juce::isPositiveAndBelow (r.style, (int) suite::sessionStyles().size()))
            addNote (juce::String ("New song: ") + suite::sessionStyles()[(size_t) r.style].name + " in "
                     + suite::music::noteNames()[juce::jlimit (0, 11, r.root)] + " " + suite::music::scales()[(size_t) juce::jlimit (0, 5, r.scale)].name);
    }
    seenSession = suite::SessionBus::latest();

    for (auto seq = seenRecipe + 1; seq <= suite::RecipeBus::latest() && seq != 0; ++seq)
    {
        suite::RecipeAction a {};
        juce::String name;
        if (suite::RecipeBus::read (seq, a, name))
            addNote ((a == suite::RecipeAction::Save ? "Saved recipe '" : "Loaded recipe '") + name + "'");
    }
    seenRecipe = suite::RecipeBus::latest();
}

void OuroborosProcessor::publish()
{
    const auto on = apvts.getRawParameterValue (broadcastId)->load() > 0.5f;
    if (on)
    {
        auto m = getMap();
        m.finalLift = (int) apvts.getRawParameterValue (liftId)->load();   // the whole suite follows the key change
        m.dropGap = (int) apvts.getRawParameterValue (dropGapId)->load();   // Prima Materia on every track mutes before drops
        m.intensity = juce::roundToInt (apvts.getRawParameterValue (intensityId)->load());
        if (liveType.load() >= 0)
        {
            m.liveType = liveType.load();
            m.livePrevType = livePrevType.load();
            m.liveIndex = liveIndex.load();
            m.liveStartQ = liveStartQ.load();
        }
        suite::SectionBus::publish (m);
    }
    else if (wasBroadcasting)
        suite::SectionBus::withdraw();
    wasBroadcasting = on;
}

juce::AudioProcessorEditor* OuroborosProcessor::createEditor()
{
    return new OuroborosEditor (*this);
}

void OuroborosProcessor::getPluginState (juce::MemoryBlock& destData)
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
        if (m.sections[(size_t) i].energy >= 0) s.setProperty ("energy", m.sections[(size_t) i].energy, nullptr);
        arr.appendChild (s, nullptr);
    }
    state.removeChild (state.getChildWithName ("Arrangement"), nullptr);
    state.appendChild (arr, nullptr);
    state.removeChild (state.getChildWithName ("Notes"), nullptr);
    juce::ValueTree notesTree ("Notes");
    notesTree.setProperty ("text", getNotes().joinIntoString ("\n"), nullptr);
    state.appendChild (notesTree, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OuroborosProcessor::setPluginState (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (const auto n = state.getChildWithName ("Notes"); n.isValid())
    {
        const juce::ScopedLock sl (notesLock);
        notes = juce::StringArray::fromLines (n["text"].toString());
        notes.removeEmptyStrings();
        state.removeChild (n, nullptr);
    }
    const auto arr = state.getChildWithName ("Arrangement");
    if (arr.isValid())
    {
        suite::SectionMap m;
        m.totalBars = juce::jlimit (1, 1024, (int) arr["totalBars"]);
        for (const auto& s : arr)
            if (m.count < suite::SectionMap::maxSections)
                m.sections[(size_t) m.count++] = { juce::jmax (1, (int) s["start"]),
                                                   (suite::SectionType) juce::jlimit (0, (int) suite::SectionType::count - 1, (int) s["type"]),
                                                   s.hasProperty ("energy") ? juce::jlimit (0, 100, (int) s["energy"]) : -1 };
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
