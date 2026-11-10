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

std::vector<chords::Note> CorrespondenceProcessor::readMidiNotes (const juce::File& file)
{
    juce::MidiFile midi;
    juce::FileInputStream in (file);
    std::vector<chords::Note> notes;
    if (! in.openedOk() || ! midi.readFrom (in) || midi.getTimeFormat() <= 0)
        return notes;
    const auto ppq = (double) midi.getTimeFormat();
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
    std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    return notes;
}

void CorrespondenceProcessor::applyProgression (const chords::ProgressionGuess& g)
{
    if (! g.valid) return;
    setParameterPlain (apvts, Params::rootNote, (float) g.root);
    setParameterPlain (apvts, Params::scale, (float) g.scale);
    setParameterPlain (apvts, Params::count, (float) g.count);
    setParameterPlain (apvts, Params::perChord, g.quartersPerChord < 3.0 ? 0.0f : g.quartersPerChord < 6.0 ? 1.0f : 2.0f);
    for (int i = 0; i < chords::maxSlots; ++i)
    {
        setParameterPlain (apvts, Params::slotDegree (i), (float) (g.slots[(size_t) i].degree - 1));
        setParameterPlain (apvts, Params::slotQuality (i), (float) (int) g.slots[(size_t) i].quality);
    }
}

juce::String CorrespondenceProcessor::learnFromFiles (const juce::StringArray& paths)
{
    std::vector<std::vector<chords::Note>> clips;
    for (const auto& p : paths)
        if (auto notes = readMidiNotes (juce::File (p)); ! notes.empty()) clips.push_back (std::move (notes));
    if (clips.empty()) return "No notes in that MIDI";

    const auto s = Params::readSettings (apvts);
    if (clips.size() >= 2)
    {
        auto learned = chords::learnDna (clips, s.root, s.scale);
        if (! learned.valid) return "Not enough notes to learn from";
        { const juce::ScopedLock sl (motifLock); dna = learned; motif.clear(); }
        return "Learned your melodic DNA from " + juce::String (learned.clips) + " clips (" + juce::String (learned.notes) + " notes)";
    }
    if (chords::looksLikeChords (clips[0]))
    {
        const auto g = chords::progressionFromNotes (clips[0]);
        if (! g.valid) return "Couldn't read chords from that clip";
        applyProgression (g);
        return "Read " + juce::String (g.count) + " chords";
    }
    const auto n = learnMotif (juce::File (paths[0]));
    return n > 0 ? "Learned a " + juce::String (n) + "-note motif" : "No notes in that MIDI";
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
    m.dna = getDna();
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
                        : kind.endsWith ("Lead") ? chords::SongPart::Lead : kind.endsWith ("Harmony") ? chords::SongPart::Harmony
                        : kind.endsWith ("Counter") ? chords::SongPart::Counter
                        : chords::SongPart::Chords;
        auto patterns = Params::readSongPatterns (apvts);
        patterns.melody = melodySettings();
        notes = chords::songClip (s, map, patterns, part);
        if (notes.empty() && (part == chords::SongPart::Harmony || part == chords::SongPart::Counter))
            return {};   // the harmony line only exists when there's a later drop
    }
    else
    {
        notes = kind == "Bass" ? chords::bassClip (s) : kind == "Arp" ? chords::arpClip (s)
              : kind == "Melody" ? chords::melody (s, melodySettings())
              : kind == "Counter" ? chords::counterMelody (chords::melody (s, melodySettings()), s) : chords::chordClip (s);
    }

    juce::String progression;
    for (int i = 0; i < s.count; ++i)
        progression << (i > 0 ? "-" : "") << chords::chordName (s, s.slots[(size_t) i]);

    const auto key = suite::music::noteNames()[s.root] + (s.scale == 1 ? "" : "m");
    const auto file = directory.getChildFile (juce::File::createLegalFileName (key + " " + kind + " " + progression + ".mid"));

    directory.createDirectory();
    file.deleteFile();
    const auto shaped = chords::humanise (chords::applyGroove (notes, Params::grooveSwing ((int) apvts.getRawParameterValue (Params::groove)->load())),
                                          apvts.getRawParameterValue (Params::humanise)->load(), kind.hashCode());
    const auto cc = apvts.getRawParameterValue (Params::accentCC)->load() > 0.5f ? 74 : 0;
    const auto isLead = kind == "Melody" || kind == "Song Lead";   // the lead line: glides and scoops
    if (auto stream = file.createOutputStream())
        (isLead ? chords::toLeadMidiFile (shaped, currentBpm.load(), "Correspondence " + kind, cc, leadExpression())
                : chords::toMidiFile (shaped, currentBpm.load(), "Correspondence " + kind, cc)).writeTo (*stream);
    return file;
}

juce::File CorrespondenceProcessor::exportEverything (const juce::File& parent) const
{
    const auto s = Params::readSettings (apvts);
    const auto key = suite::music::noteNames()[s.root] + (s.scale == 1 ? "" : "m");
    const auto bpm = currentBpm.load();
    const auto title = getSongTitle().trim(), artist = getArtist().trim();
    const auto keyBpm = key + " " + juce::String (juce::roundToInt (bpm)) + " BPM";
    const auto folderName = title.isNotEmpty() ? (artist.isNotEmpty() ? artist + " - " : juce::String()) + title + " (" + keyBpm + ")"
                                               : keyBpm + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M");
    auto folder = parent.getChildFile (juce::File::createLegalFileName (folderName));
    folder = folder.getNonexistentSibling (false);
    folder.createDirectory();

    for (auto* kind : { "Chords", "Bass", "Arp", "Melody", "Counter", "Song Chords", "Song Bass", "Song Arp", "Song Lead", "Song Harmony", "Song Counter" })
        exportClip (kind, folder);   // song clips are skipped without an Ouroboros map

    // A song sheet for labels and DJs: title, artist, key, tempo, progression, structure.
    {
        juce::StringArray sheet;
        if (title.isNotEmpty()) sheet.add ("Title:  " + title);
        if (artist.isNotEmpty()) sheet.add ("Artist: " + artist);
        sheet.add ("Key:    " + suite::music::noteNames()[s.root] + " " + suite::music::scales()[(size_t) s.scale].name);
        sheet.add ("Tempo:  " + juce::String (bpm, 1) + " BPM");
        juce::StringArray progression;
        for (int i = 0; i < s.count; ++i) progression.add (chords::chordName (s, s.slots[(size_t) i]));
        sheet.add ("Chords: " + progression.joinIntoString (" - "));
        suite::SectionMap shared;
        if (suite::SectionBus::read (shared) && shared.count > 0)
        {
            const auto seconds = shared.totalBars * 4.0 * 60.0 / bpm;
            sheet.add ("Length: " + juce::String (shared.totalBars) + " bars (" + juce::String ((int) seconds / 60) + ":"
                       + juce::String ((int) seconds % 60).paddedLeft ('0', 2) + ")");
            sheet.add ("");
            sheet.add ("Structure:");
            for (int i = 0; i < shared.count; ++i)
            {
                const auto& sec = shared.sections[(size_t) i];
                const auto startSeconds = (sec.startBar - 1) * 4.0 * 60.0 / bpm;
                sheet.add ("  bar " + juce::String (sec.startBar).paddedLeft (' ', 3) + "  " + juce::String ((int) startSeconds / 60) + ":"
                           + juce::String ((int) startSeconds % 60).paddedLeft ('0', 2) + "  " + suite::sectionName (sec.type)
                           + " (" + juce::String (shared.endBar (i) - sec.startBar) + " bars)"
                           + (shared.keyShiftOf (i) != 0 && i == shared.lastDropIndex() ? "  key +" + juce::String (shared.keyShiftOf (i)) : juce::String()));
            }
        }
        folder.getChildFile ("Song sheet.txt").replaceWithText (sheet.joinIntoString ("\n") + "\n");
    }

    suite::SectionMap map;
    if (suite::SectionBus::read (map) && map.count > 0)
    {
        auto patterns = Params::readSongPatterns (apvts);
        patterns.melody = melodySettings();
        std::vector<std::pair<juce::String, std::vector<chords::Note>>> parts;
        for (const auto& [name, part] : { std::pair<const char*, chords::SongPart> { "Chords", chords::SongPart::Chords },
                                          { "Bass", chords::SongPart::Bass }, { "Arp", chords::SongPart::Arp }, { "Lead", chords::SongPart::Lead },
                                          { "Harmony", chords::SongPart::Harmony }, { "Counter", chords::SongPart::Counter } })
            if (auto notes = chords::songClip (s, map, patterns, part); ! notes.empty() || part == chords::SongPart::Chords || part == chords::SongPart::Bass
                                                                         || part == chords::SongPart::Arp || part == chords::SongPart::Lead)
                parts.push_back ({ name, std::move (notes) });

        std::vector<std::pair<double, juce::String>> markers;
        std::array<int, (size_t) suite::SectionType::count> seen {};
        for (int i = 0; i < map.count; ++i)
        {
            const auto type = map.sections[(size_t) i].type;
            const auto n = ++seen[(size_t) type];
            markers.push_back ({ (map.sections[(size_t) i].startBar - 1) * 4.0,
                                 juce::String (suite::sectionName (type)) + (n > 1 ? " " + juce::String (n) : juce::String())
                                     + (map.keyShiftOf (i) != 0 && i == map.lastDropIndex() ? " (key +" + juce::String (map.keyShiftOf (i)) + ")" : juce::String()) });
        }
        const auto amount = apvts.getRawParameterValue (Params::humanise)->load();
        const auto swing = Params::grooveSwing ((int) apvts.getRawParameterValue (Params::groove)->load());
        for (auto& [name, notes] : parts)
        {
            notes = chords::humanise (chords::applyGroove (notes, swing), amount, ("Song " + name).hashCode());
            if (name == "Lead") notes = chords::expressLead (notes, leadExpression()).notes;   // glides (scoops are in the Song Lead clip)
        }
        const auto file = folder.getChildFile (juce::File::createLegalFileName (key + " - all parts.mid"));
        if (auto stream = file.createOutputStream())
            chords::toMultiTrackMidi (parts, bpm, markers, apvts.getRawParameterValue (Params::accentCC)->load() > 0.5f ? 74 : 0).writeTo (*stream);
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
    state.removeChild (state.getChildWithName ("Dna"), nullptr);
    if (const auto d = getDna(); d.valid)
    {
        juce::ValueTree dt ("Dna");
        juce::StringArray onset, steps;
        for (auto v : d.onset) onset.add (juce::String (v, 4));
        for (auto v : d.steps) steps.add (juce::String (v, 2));
        dt.setProperty ("onset", onset.joinIntoString (","), nullptr);
        dt.setProperty ("steps", steps.joinIntoString (","), nullptr);
        dt.setProperty ("clips", d.clips, nullptr);
        dt.setProperty ("notes", d.notes, nullptr);
        state.appendChild (dt, nullptr);
    }
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
            chords::MelodyDna learned;
            if (const auto dt = state.getChildWithName ("Dna"); dt.isValid())
            {
                const auto onset = juce::StringArray::fromTokens (dt["onset"].toString(), ",", "");
                const auto steps = juce::StringArray::fromTokens (dt["steps"].toString(), ",", "");
                if (onset.size() == 32 && steps.size() == 9)
                {
                    for (int i = 0; i < 32; ++i) learned.onset[(size_t) i] = onset[i].getFloatValue();
                    for (int i = 0; i < 9; ++i) learned.steps[(size_t) i] = steps[i].getFloatValue();
                    learned.clips = (int) dt["clips"];
                    learned.notes = (int) dt["notes"];
                    learned.valid = true;
                }
                state.removeChild (dt, nullptr);
            }
            { const juce::ScopedLock sl (motifLock); dna = learned; }
            apvts.replaceState (state);
        }
    publishKey();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CorrespondenceProcessor();
}
