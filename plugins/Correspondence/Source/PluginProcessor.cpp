#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostInfo.h"
#include "KeyBus.h"

CorrespondenceProcessor::CorrespondenceProcessor()
    : SuiteProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CorrespondenceState", Params::createLayout())
{
    // Ignore suggestions made before we loaded (loading a project shouldn't change its key).
    lastSuggestion = suite::KeyBus::readSuggestion().sequence;
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
    // Adopt a key detected by Emerald Tablet, if allowed.
    const auto suggestion = suite::KeyBus::readSuggestion();
    if (suggestion.valid && suggestion.sequence != lastSuggestion)
    {
        lastSuggestion = suggestion.sequence;
        if (apvts.getRawParameterValue (Params::acceptDetected)->load() > 0.5f)
        {
            for (auto [id, v] : { std::pair<juce::String, int> { Params::rootNote, suggestion.root }, { Params::scale, suggestion.scale } })
                if (auto* p = apvts.getParameter (id))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
                    p->endChangeGesture();
                }
            adoptedDetectedKey.store (true);
        }
    }

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

int CorrespondenceProcessor::learnMotif (const juce::File& file)
{
    juce::MidiFile midi;
    juce::FileInputStream in (file);
    if (! in.openedOk() || ! midi.readFrom (in) || midi.getTimeFormat() <= 0)
        return 0;

    const auto ppq = (double) midi.getTimeFormat();
    std::vector<chords::Note> notes;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*midi.getTrack (t));
        seq.updateMatchedPairs();
        for (auto* e : seq)
            if (e->message.isNoteOn())
            {
                const auto start = e->message.getTimeStamp() / ppq;
                const auto end = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / ppq : start + 0.25;
                notes.push_back ({ e->message.getNoteNumber(), start, std::max (0.05, end - start), e->message.getVelocity() });
            }
    }
    if (notes.empty())
        return 0;

    // Start at the bar of the first note; keep two bars; one note per onset (the highest, as a lead line).
    std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start || (std::abs (a.start - b.start) < 1.0e-6 && a.pitch > b.pitch); });
    const auto barStart = std::floor (notes.front().start / 4.0) * 4.0;
    std::vector<chords::Note> learned;
    for (auto n : notes)
    {
        n.start -= barStart;
        if (n.start >= 8.0 - 1.0e-9) break;
        if (! learned.empty() && std::abs (learned.back().start - n.start) < 1.0e-6) continue;
        learned.push_back (n);
    }

    const juce::ScopedLock sl (motifLock);
    motif = learned;
    return (int) learned.size();
}

void CorrespondenceProcessor::forgetMotif()
{
    const juce::ScopedLock sl (motifLock);
    motif.clear();
}

std::vector<chords::Note> CorrespondenceProcessor::getMotif() const
{
    const juce::ScopedLock sl (motifLock);
    return motif;
}

chords::MelodySettings CorrespondenceProcessor::melodySettings() const
{
    auto m = Params::readMelody (apvts);
    m.motif = getMotif();
    return m;
}

juce::File CorrespondenceProcessor::exportClip (const juce::String& kind, const juce::File& directory) const
{
    const auto s = Params::readSettings (apvts);
    std::vector<chords::Note> notes;
    if (kind.startsWith ("Song "))
    {
        // Full-song clip following the Ouroboros map.
        suite::SectionMap map;
        if (! suite::SectionBus::read (map))
            return {};
        const auto part = kind.endsWith ("Bass") ? chords::SongPart::Bass : kind.endsWith ("Arp") ? chords::SongPart::Arp
                        : kind.endsWith ("Lead") ? chords::SongPart::Lead : chords::SongPart::Chords;
        auto patterns = Params::readSongPatterns (apvts);
        patterns.melody = melodySettings();
        notes = chords::songClip (s, map, patterns, part);
    }
    else
    {
        notes = kind == "Bass" ? chords::bassClip (s) : kind == "Arp" ? chords::arpClip (s)
              : kind == "Melody" ? chords::melody (s, melodySettings()) : chords::chordClip (s);
    }

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

juce::File CorrespondenceProcessor::exportEverything (const juce::File& parent) const
{
    const auto s = Params::readSettings (apvts);
    const auto key = suite::music::noteNames()[s.root] + (s.scale == 1 ? "" : "m");
    const auto bpm = currentBpm.load();
    auto folder = parent.getChildFile (juce::File::createLegalFileName (key + " " + juce::String (juce::roundToInt (bpm)) + " BPM "
                                                                        + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M")));
    folder = folder.getNonexistentSibling (false);
    folder.createDirectory();

    for (auto* kind : { "Chords", "Bass", "Arp", "Melody", "Song Chords", "Song Bass", "Song Arp", "Song Lead" })
        exportClip (kind, folder);   // song clips are skipped without an Ouroboros map

    suite::SectionMap map;
    if (suite::SectionBus::read (map) && map.count > 0)
    {
        auto patterns = Params::readSongPatterns (apvts);
        patterns.melody = melodySettings();
        std::vector<std::pair<juce::String, std::vector<chords::Note>>> parts;
        for (const auto& [name, part] : { std::pair<const char*, chords::SongPart> { "Chords", chords::SongPart::Chords },
                                          { "Bass", chords::SongPart::Bass }, { "Arp", chords::SongPart::Arp }, { "Lead", chords::SongPart::Lead } })
            parts.push_back ({ name, chords::songClip (s, map, patterns, part) });

        std::vector<std::pair<double, juce::String>> markers;
        std::array<int, (size_t) suite::SectionType::count> seen {};
        for (int i = 0; i < map.count; ++i)
        {
            const auto type = map.sections[(size_t) i].type;
            const auto n = ++seen[(size_t) type];
            markers.push_back ({ (map.sections[(size_t) i].startBar - 1) * 4.0,
                                 juce::String (suite::sectionName (type)) + (n > 1 ? " " + juce::String (n) : juce::String()) });
        }
        const auto file = folder.getChildFile (juce::File::createLegalFileName (key + " - all parts.mid"));
        if (auto stream = file.createOutputStream())
            chords::toMultiTrackMidi (parts, bpm, markers).writeTo (*stream);
    }
    return folder;
}

juce::AudioProcessorEditor* CorrespondenceProcessor::createEditor()
{
    return new CorrespondenceEditor (*this);
}

void CorrespondenceProcessor::getPluginState (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Motif"), nullptr);
    juce::ValueTree m ("Motif");
    for (const auto& n : getMotif())
    {
        juce::ValueTree note ("Note");
        note.setProperty ("p", n.pitch, nullptr);
        note.setProperty ("s", n.start, nullptr);
        note.setProperty ("l", n.length, nullptr);
        note.setProperty ("v", n.velocity, nullptr);
        m.appendChild (note, nullptr);
    }
    state.appendChild (m, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CorrespondenceProcessor::setPluginState (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            std::vector<chords::Note> loaded;
            for (const auto& note : state.getChildWithName ("Motif"))
                loaded.push_back ({ (int) note["p"], (double) note["s"], (double) note["l"], (int) note["v"] });
            {
                const juce::ScopedLock sl (motifLock);
                motif = loaded;
            }
            state.removeChild (state.getChildWithName ("Motif"), nullptr);
            apvts.replaceState (state);
        }
    publishKey();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CorrespondenceProcessor();
}
