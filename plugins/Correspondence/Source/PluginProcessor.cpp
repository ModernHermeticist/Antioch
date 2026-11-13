#include <set>
#include "PluginProcessor.h"
#include "TempoBus.h"
#include "SpireTools.h"
#include "StyleLog.h"
#include "VocalBus.h"
#include "ChordBus.h"
#include "PluginEditor.h"
#include "HostInfo.h"
#include "KeyBus.h"
#include "AutopilotBus.h"

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
    fixes = std::make_unique<suite::FixReceiver> (apvts, [] (suite::Fix) { return std::vector<suite::FixReceiver::Change> {}; });
    fixes->setCustom ([this] (suite::Fix fix, uint32_t) -> std::function<void()>
    {
        if (fix == suite::Fix::DraftSong) draftAt.store (juce::Time::getMillisecondCounterHiRes() + 1000.0);   // after the new song's key and chords
        if (fix == suite::Fix::ExportAll)   // one-click finish (Ouroboros)
        {
            lastDraft = exportEverything (suite::Alias::file ("Exports"));
            suite::MoveLog::add (getName(), getTrackName(), "Exported every part to " + lastDraft.getFullPathName(), suite::MoveLog::fix);
            return [] {};
        }
        return {};
    });
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
        // The progression too, so the suite can follow the harmony (Azoth's riser, tension width).
        const auto s = currentSettings();
        suite::Progression prog;
        prog.root = s.root; prog.scale = s.scale; prog.count = juce::jlimit (1, 8, s.count);
        prog.quartersPerChord = s.quartersPerChord;
        for (int i = 0; i < prog.count; ++i) prog.chordRoots[(size_t) i] = ((chords::chordRootPitchClass (s, i) - s.root) % 12 + 12) % 12;
        suite::ChordBus::publish (prog);
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

void CorrespondenceProcessor::applySlots (const chords::Settings& s)
{
    auto setChoice = [this] (const juce::String& id, int value)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) value));
            param->endChangeGesture();
        }
    };
    setChoice (Params::count, s.count);
    for (int i = 0; i < chords::maxSlots; ++i)
    {
        setChoice (Params::slotDegree (i), s.slots[(size_t) i].degree - 1);
        setChoice (Params::slotQuality (i), (int) s.slots[(size_t) i].quality);
    }
}

bool CorrespondenceProcessor::applyHarmonyIdea (int index)
{
    const auto list = harmonyIdeas();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return false;
    beforeIdea = currentSettings();
    applySlots (list[(size_t) index].result);
    return true;
}

bool CorrespondenceProcessor::applyVariation (suite::SectionType type, int index)
{
    const auto list = sectionVariations (type);
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return false;
    const auto& v = list[(size_t) index];
    beforeVariation.clear();
    auto set = [this] (const juce::String& id, int value)
    {
        if (value < 0) return;
        if (auto* param = apvts.getParameter (id))
        {
            beforeVariation.push_back ({ id, param->convertFrom0to1 (param->getValue()) });
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) value));
            param->endChangeGesture();
        }
    };
    const auto t = (int) type;
    set (Params::songId (chords::SongPart::Chords, t), v.chords);
    set (Params::songId (chords::SongPart::Bass, t), v.bass);
    set (Params::songId (chords::SongPart::Arp, t), v.arp);
    set (Params::songId (chords::SongPart::Lead, t), v.lead);
    set (Params::songBreakdown, v.breakdownStart);
    return true;
}

bool CorrespondenceProcessor::undoVariation()
{
    if (beforeVariation.empty()) return false;
    for (const auto& [id, value] : beforeVariation)
        if (auto* param = apvts.getParameter (id)) param->setValueNotifyingHost (param->convertTo0to1 (value));
    beforeVariation.clear();
    return true;
}

bool CorrespondenceProcessor::undoHarmonyIdea()
{
    if (! beforeIdea) return false;
    applySlots (*beforeIdea);
    beforeIdea.reset();
    return true;
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

    const auto s = currentSettings();
    const auto allBass = std::all_of (clips.begin(), clips.end(), [] (const auto& c) { return chords::looksLikeBass (c); });
    if (allBass)
    {
        // Bass clips: your bassline DNA, and the Mine bass pattern to play it.
        const auto learnedBass = chords::learnBassDna (clips);
        if (! learnedBass.valid) return "Not enough bass notes to learn from";
        { const juce::ScopedLock sl (motifLock); bassDna = learnedBass; }
        setParameterPlain (apvts, Params::bassPattern, (float) (int) chords::BassPattern::Mine);
        return "Learned your bassline from " + juce::String (learnedBass.clips) + (learnedBass.clips == 1 ? " clip" : " clips")
             + ": Bass pattern set to Mine";
    }
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
        // ...and how you voice them.
        const auto v = chords::voicingFromNotes (clips[0]);
        if (! v.valid) return "Read " + juce::String (g.count) + " chords";
        setParameterPlain (apvts, Params::voicing, (float) (int) v.voicing);
        setParameterPlain (apvts, Params::registerOct, (float) v.registerOctave);
        setParameterPlain (apvts, Params::addBass, v.addBass ? 1.0f : 0.0f);
        static const char* names[] = { "close", "open", "wide" };
        return "Read " + juce::String (g.count) + " chords and your voicing (" + names[(int) v.voicing] + ", octave " + juce::String (v.registerOctave)
             + (v.addBass ? ", root underneath)" : ")");
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

bool CorrespondenceProcessor::applyNumerals (const juce::String& numerals)
{
    auto s = currentSettings();
    if (! identity::parseNumerals (numerals, s)) return false;
    setParameterPlain (apvts, Params::count, (float) s.count);
    for (int i = 0; i < s.count; ++i)
    {
        setParameterPlain (apvts, Params::slotDegree (i), (float) (s.slots[(size_t) i].degree - 1));
        setParameterPlain (apvts, Params::slotQuality (i), (float) (int) s.slots[(size_t) i].quality);
    }
    return true;
}

bool CorrespondenceProcessor::saveHook (const juce::String& name)
{
    auto notes = getMotif();
    if (notes.empty())
        for (const auto& n : chords::melody (currentSettings(), melodySettings()))
            if (n.start < 8.0 - 1.0e-9) notes.push_back (n);   // the call: the melody's first two bars
    return identity::saveHook (name, notes);
}

void CorrespondenceProcessor::useHook (const std::vector<chords::Note>& notes)
{
    const juce::ScopedLock sl (motifLock);
    motif = notes;
    dna = {};
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

std::vector<chords::Note> CorrespondenceProcessor::clipNotes (const juce::String& kind, std::vector<std::pair<double, bool>>& pianoPedal, bool& available) const
{
    const auto s = currentSettings();
    std::vector<chords::Note> notes;
    available = true;
    auto none = [&available] { available = false; return std::vector<chords::Note> {}; };
    if (kind.startsWith ("Song "))
    {
        // Full-song clip following the Ouroboros map.
        suite::SectionMap map;
        if (! suite::SectionBus::read (map))
            return none();
        if (kind == "Song Lead Octave" || kind == "Song Lead Pluck")   // Spire layers over the lead
        {
            const auto lead = chords::songClip (s, map, currentSongPatterns(), chords::SongPart::Lead);
            notes = kind.endsWith ("Octave") ? spire::octaveLayer (lead, map) : spire::pluckLayer (lead, map);
            if (notes.empty()) return none();
        }
        else if (kind == "Song Sub")   // a sub under the bass, for its own Spire
        {
            notes = spire::subLayer (chords::songClip (s, map, currentSongPatterns(), chords::SongPart::Bass), map);
            if (notes.empty()) return none();
        }
        else if (kind == "Song Fills")
        {
            if (apvts.getRawParameterValue (Params::songFills)->load() < 0.5f) return none();
            notes = chords::fillsClip (map);
        }
        else if (kind == "Song Layer")   // a sustained layer for the sections Mentalism hears as thin
        {
            notes = vocalaware::layerClip (s, map, suite::WritingBus::thin());
            if (notes.empty()) return none();
        }
        else if (kind == "Song Vocal Answer")   // a line in the vocal's gaps (needs Solve et Coagula's vocal bars)
        {
            notes = chords::vocalAnswer (s, map, currentSongPatterns().vocalBars);
            if (notes.empty()) return none();
        }
        else if (kind == "Song Piano" && ! currentSongPatterns().library)
        {
            notes = chords::pianoClip (s, map, pianoPedal);
            if (notes.empty()) return none();   // no breakdown in the map
        }
        else if (const auto layer = Params::layerNames().indexOf (kind.fromFirstOccurrenceOf ("Song ", false, false)); layer >= 0)
        {
            // The pattern library's layers: piano, orchestra, electronic specifics
            notes = chords::songClip (s, map, currentSongPatterns(), (chords::SongPart) ((int) chords::SongPart::Piano + layer));
            if (notes.empty()) return none();   // not in this style, or switched off
            if (layer == 0)   // the piano's sustain pedal: caught again at each chord change, lifted after the last note
            {
                const auto L = s.quartersPerChord;
                double end = 0.0;
                for (const auto& n : notes) end = std::max (end, n.start + n.length);
                std::set<double> changes;
                for (const auto& n : notes) changes.insert (std::floor (n.start / L + 1.0e-9) * L);
                for (const auto t : changes) { pianoPedal.push_back ({ t, false }); pianoPedal.push_back ({ t + 0.05, true }); }
                pianoPedal.push_back ({ end, false });
            }
        }
        else
        {
        const auto part = kind.endsWith ("Bass") ? chords::SongPart::Bass : kind.endsWith ("Arp") ? chords::SongPart::Arp
                        : kind.endsWith ("Lead") ? chords::SongPart::Lead : kind.endsWith ("Harmony") ? chords::SongPart::Harmony
                        : kind.endsWith ("Counter") ? chords::SongPart::Counter
                        : chords::SongPart::Chords;
        notes = chords::songClip (s, map, currentSongPatterns(), part);
        if (notes.empty() && (part == chords::SongPart::Harmony || part == chords::SongPart::Counter))
            return none();   // the harmony line only exists when there's a later drop
        // Around the vocal: melodic lines step off notes that rub against what the vocal holds
        if (part == chords::SongPart::Lead || part == chords::SongPart::Arp || part == chords::SongPart::Counter || part == chords::SongPart::Harmony)
            movedForVocal.store (vocalaware::avoidVocal (notes, suite::WritingBus::sung(), s));
        }
    }
    else
    {
        notes = kind == "Bass" ? chords::bassClip (s) : kind == "Arp" ? chords::arpClip (s)
              : kind == "Melody" ? chords::melody (s, melodySettings())
              : kind == "Counter" ? chords::counterMelody (chords::melody (s, melodySettings()), s) : chords::chordClip (s);
    }
    // Autopilot (arrangement): song parts don't ring on into a chord they clash with
    if (kind.startsWith ("Song ") && kind != "Song Chords" && kind != "Song Fills" && suite::Autopilot::on (suite::Autopilot::hangoverTrimF))
        if (suite::SectionMap map; suite::SectionBus::read (map))
            if (const auto t = chords::trimChordHangovers (notes, s, map); t > 0) suite::Autopilot::count (suite::Autopilot::hangoversTrimmed, (uint32_t) t);
    // Autopilot (stability): bass notes under D1 move up an octave
    if (kind.endsWith ("Bass") && suite::Autopilot::on (suite::Autopilot::lowBassFoldF))
        if (const auto moved = chords::foldLowBass (notes); moved > 0) suite::Autopilot::count (suite::Autopilot::lowNotesFolded, (uint32_t) moved);
    // Writing (feel): each part's own micro-timing and the style's groove
    if (kind.startsWith ("Song ") && kind != "Song Fills")
        if (const auto sp = currentSongPatterns(); sp.write.feel && sp.library)
        {
            const auto part = kind.endsWith ("Bass") ? patterns::Part::Bass : kind.endsWith ("Arp") ? patterns::Part::Arp : kind.endsWith ("Chords") ? patterns::Part::Chords
                            : kind.endsWith ("Lead") || kind.endsWith ("Harmony") ? patterns::Part::Lead : kind.endsWith ("Counter") ? patterns::Part::Counter
                            : (patterns::Part) juce::jmax (0, patterns::partFiles().indexOf (kind.fromFirstOccurrenceOf ("Song ", false, false).toLowerCase()));
            notes = chords::writing::microTiming (notes, part, (patterns::Style) juce::jlimit (0, 6, sp.libStyle));
        }
    return chords::humanise (chords::applyGroove (notes, Params::grooveSwing ((int) apvts.getRawParameterValue (Params::groove)->load())),
                             apvts.getRawParameterValue (Params::humanise)->load(), kind.hashCode());
}

juce::Array<juce::File> CorrespondenceProcessor::sendToPianoRoll() const
{
    // Loop clips always; the song clips too when Ouroboros has a map.
    juce::Array<juce::File> written;
    const auto dir = spire::pianoRollScriptFolder();
    dir.createDirectory();
    const auto s = currentSettings();
    const auto key = suite::music::noteNames()[s.root] + (s.scale == 1 ? "" : "m");
    for (auto* kind : { "Chords", "Bass", "Arp", "Melody", "Song Chords", "Song Bass", "Song Arp", "Song Lead", "Song Lead Octave", "Song Lead Pluck", "Song Sub" })
    {
        std::vector<std::pair<double, bool>> pedal;
        bool available = false;
        const auto notes = clipNotes (kind, pedal, available);
        if (! available || notes.empty()) continue;
        const auto title = juce::String ("Antioch - ") + kind;
        const auto f = dir.getChildFile (juce::File::createLegalFileName (title + ".pyscript"));
        if (f.replaceWithText (spire::pianoRollScript (notes, title + " (" + key + ")",
                                                       juce::String (kind) + " in " + key + " at " + juce::String (juce::roundToInt (currentBpm.load())) + " BPM, from Correspondence")))
            written.add (f);
    }
    return written;
}

std::vector<clash::Clash> CorrespondenceProcessor::clashes() const
{
    suite::SectionMap map;
    if (! suite::SectionBus::read (map) || map.count == 0) return {};
    return clash::find (currentSettings(), map, currentSongPatterns(), (int) apvts.getRawParameterValue (Params::melodyOctave)->load(),
                        (int) apvts.getRawParameterValue (Params::arpOctaves)->load(), Params::melodyOctave, Params::arpOctaves);
}

bool CorrespondenceProcessor::fixClash (const clash::Clash& c)
{
    auto* p = apvts.getParameter (c.paramId);
    if (p == nullptr) return false;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (c.newValue));
    p->endChangeGesture();
    return true;
}

juce::File CorrespondenceProcessor::exportClip (const juce::String& kind, const juce::File& directory) const
{
    const auto s = currentSettings();
    std::vector<std::pair<double, bool>> pianoPedal;
    bool available = false;
    const auto shaped = clipNotes (kind, pianoPedal, available);
    if (! available) return {};

    juce::String progression;
    for (int i = 0; i < s.count; ++i)
        progression << (i > 0 ? "-" : "") << chords::chordName (s, s.slots[(size_t) i]);

    const auto key = suite::music::noteNames()[s.root] + (s.scale == 1 ? "" : "m");
    const auto file = directory.getChildFile (juce::File::createLegalFileName (key + " " + kind + " " + progression + ".mid"));

    directory.createDirectory();
    file.deleteFile();
    const auto cc = apvts.getRawParameterValue (Params::accentCC)->load() > 0.5f ? 74 : 0;
    const auto isLead = kind == "Melody" || kind == "Song Lead";   // the lead line: glides and scoops
    auto midi = isLead ? chords::toLeadMidiFile (shaped, currentBpm.load(), "Correspondence " + kind, cc, leadExpression())
                       : chords::toMidiFile (shaped, currentBpm.load(), "Correspondence " + kind, cc);
    if (! pianoPedal.empty())
    {
        // The breakdown piano's sustain pedal (CC 64).
        juce::MidiMessageSequence seq (*midi.getTrack (0));
        for (const auto& [t, down] : pianoPedal)
            seq.addEvent (juce::MidiMessage::controllerEvent (1, 64, down ? 127 : 0), t * midi.getTimeFormat());
        seq.sort();
        seq.updateMatchedPairs();
        juce::MidiFile withPedal;
        withPedal.setTicksPerQuarterNote (midi.getTimeFormat());
        withPedal.addTrack (seq);
        midi = withPedal;
    }
    // Spire macro rides in the song clips (not the drums).
    if (kind.startsWith ("Song ") && kind != "Song Fills")
        if (suite::SectionMap map; suite::SectionBus::read (map))
            midi = spire::withMacroRides (midi, map, spire::macroBaseCC ((int) apvts.getRawParameterValue (Params::songMacros)->load()),
                                          (int) apvts.getRawParameterValue (Params::macroPlan)->load());
    if (auto stream = file.createOutputStream())
        midi.writeTo (*stream);
    return file;
}

chords::SongPatterns CorrespondenceProcessor::currentSongPatterns() const
{
    auto patterns = Params::readSongPatterns (apvts);
    patterns.melody = melodySettings();
    patterns.vocalBars = suite::VocalBus::read();
    patterns.manageDensity = suite::Autopilot::on (suite::Autopilot::densityF);
    patterns.takeTurns = suite::Autopilot::on (suite::Autopilot::turnTakingF);
    return patterns;
}

juce::File CorrespondenceProcessor::exportSongPack (const juce::File& parent) const
{
    const auto s = currentSettings();
    const auto key = suite::music::noteNames()[s.root] + (s.scale == 1 ? "" : "m");
    const auto bpm = currentBpm.load();
    const auto title = getSongTitle().trim(), artist = getArtist().trim();
    auto folder = parent.getChildFile (juce::File::createLegalFileName ((title.isNotEmpty() ? title : key + " " + juce::String (juce::roundToInt (bpm)) + " BPM")
                                                                        + " - song pack")).getNonexistentSibling (false);
    folder.createDirectory();

    suite::SectionMap map;
    const auto haveMap = suite::SectionBus::read (map) && map.count > 0;
    static const char* const songKinds[] { "Song Chords", "Song Bass", "Song Lead" };
    static const char* const loopKinds[] { "Chords", "Bass", "Melody" };
    for (auto* kind : haveMap ? songKinds : loopKinds)
        exportClip (kind, folder);

    // The lead guide's range, for the singer.
    std::vector<chords::Note> lead = haveMap ? chords::songClip (s, map, currentSongPatterns(), chords::SongPart::Lead)
                                             : chords::melody (s, melodySettings());
    int low = 127, high = 0;
    for (const auto& n : lead) { low = std::min (low, n.pitch); high = std::max (high, n.pitch); }
    auto noteName = [] (int p) { return suite::music::noteNames()[p % 12] + juce::String (p / 12 - 1); };

    juce::StringArray text;
    text.add ((title.isNotEmpty() ? title : juce::String ("This song")) + (artist.isNotEmpty() ? " by " + artist : juce::String()));
    text.add ("");
    text.add ("Key:     " + suite::music::noteNames()[s.root] + " " + suite::music::scales()[(size_t) s.scale].name);
    text.add ("Tempo:   " + juce::String (bpm, 1) + " BPM");
    juce::StringArray progression;
    for (int i = 0; i < s.count; ++i) progression.add (chords::chordName (s, s.slots[(size_t) i]));
    text.add ("Chords:  " + progression.joinIntoString (" - ") + " (" + juce::String (s.quartersPerChord / 4.0, 1) + " bar each, from the start of every section)");
    if (! lead.empty()) text.add ("Lead guide range: " + noteName (low) + " to " + noteName (high) + " (the guide is a suggestion: the topline is yours)");
    if (haveMap)
    {
        text.add ("");
        text.add ("Where a vocal usually goes: the breakdowns (the emotional part) and over the drops.");
        for (int i = 0; i < map.count; ++i)
        {
            const auto& sec = map.sections[(size_t) i];
            text.add ("  bar " + juce::String (sec.startBar).paddedLeft (' ', 3) + "  " + suite::TempoBus::clock (suite::TempoBus::secondsTo (sec.startBar, (float) bpm))
                      + "  " + suite::sectionName (sec.type)
                      + " (" + juce::String (map.endBar (i) - sec.startBar) + " bars)"
                      + (map.keyShiftOf (i) != 0 && i == map.lastDropIndex() ? "  key up " + juce::String (map.keyShiftOf (i)) : juce::String()));
        }
        // Markers as a MIDI file too, for the collaborator's DAW.
        std::vector<std::pair<double, juce::String>> markers;
        for (int i = 0; i < map.count; ++i)
            markers.push_back ({ (map.sections[(size_t) i].startBar - 1) * 4.0, suite::sectionName (map.sections[(size_t) i].type) });
        if (auto stream = folder.getChildFile ("Sections (markers).mid").createOutputStream())
            chords::toMultiTrackMidi ({}, bpm, markers).writeTo (*stream);
    }
    text.add ("");
    text.add ("In this folder: the chords, bass and lead guide as MIDI (drag into any DAW at " + juce::String (juce::roundToInt (bpm)) + " BPM)"
              + juce::String (haveMap ? ", and the sections as markers." : "."));
    folder.getChildFile ("For the vocalist.txt").replaceWithText (text.joinIntoString ("\n") + "\n");
    return folder;
}

juce::File CorrespondenceProcessor::exportEverything (const juce::File& parent) const
{
    const auto s = currentSettings();
    const auto key = suite::music::noteNames()[s.root] + (s.scale == 1 ? "" : "m");
    const auto bpm = currentBpm.load();
    const auto title = getSongTitle().trim(), artist = getArtist().trim();
    const auto keyBpm = key + " " + juce::String (juce::roundToInt (bpm)) + " BPM";
    const auto folderName = title.isNotEmpty() ? (artist.isNotEmpty() ? artist + " - " : juce::String()) + title + " (" + keyBpm + ")"
                                               : keyBpm + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M");
    auto folder = parent.getChildFile (juce::File::createLegalFileName (folderName));
    folder = folder.getNonexistentSibling (false);
    folder.createDirectory();

    for (auto* kind : { "Chords", "Bass", "Arp", "Melody", "Counter", "Song Chords", "Song Bass", "Song Arp", "Song Lead", "Song Harmony", "Song Counter", "Song Fills", "Song Piano",
                       "Song Lead Octave", "Song Lead Pluck", "Song Sub", "Song Vocal Answer", "Song Layer",
                       "Song Strings", "Song Brass", "Song Choir", "Song Timpani", "Song Acid", "Song Chops", "Song Candy" })
        exportClip (kind, folder);   // song clips are skipped without an Ouroboros map
    // The mix follows the writing: parts whose notes changed since the last export are measured
    // afresh (Quintessence and Prima Materia on those roles re-learn)
    {
        uint32_t changed = 0;
        const std::pair<const char*, int> parts[] { { "Song Chords", 4 }, { "Song Bass", 2 }, { "Song Arp", 5 }, { "Song Lead", 5 }, { "Song Counter", 5 }, { "Song Layer", 4 } };
        for (const auto& [kind, role] : parts)
        {
            std::vector<std::pair<double, bool>> pedal;
            bool available = false;
            const auto notes = clipNotes (kind, pedal, available);
            juce::int64 h = 17;
            for (const auto& n : notes) h = h * 31 + n.pitch * 1009 + (juce::int64) std::llround (n.start * 960.0);
            auto& last = lastExportHash[kind];
            if (last != 0 && last != h) changed |= 1u << role;
            last = h;
        }
        if (changed != 0) suite::FixBus::send (suite::Fix::Relearn, changed);
        lastRelearnMask.store (changed);
    }
    // The Spire macro plan on its own (four CC lanes for the whole song)
    if (suite::SectionMap macroMap; suite::SectionBus::read (macroMap) && macroMap.count > 0)
        if (auto stream = folder.getChildFile ("Spire macros.mid").createOutputStream())
            spire::macroFile (macroMap, spire::macroBaseCC ((int) apvts.getRawParameterValue (Params::songMacros)->load()),
                              (int) apvts.getRawParameterValue (Params::macroPlan)->load(), bpm).writeTo (*stream);

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
            sheet.add ("Length: " + juce::String (shared.totalBars) + " bars (" + suite::TempoBus::clock (suite::TempoBus::secondsTo (shared.totalBars + 1, (float) bpm)) + ")");
            if (const auto tempos = suite::TempoBus::changes (shared.totalBars, (float) bpm); tempos.size() > 1)
            {
                juce::StringArray t;
                for (const auto& [bar, tempo] : tempos) t.add ("bar " + juce::String (bar) + " " + juce::String (tempo, 1));
                sheet.add ("Tempo changes: " + t.joinIntoString (", ") + " BPM");
            }
            sheet.add ("");
            sheet.add ("Structure:");
            for (int i = 0; i < shared.count; ++i)
            {
                const auto& sec = shared.sections[(size_t) i];
                sheet.add ("  bar " + juce::String (sec.startBar).paddedLeft (' ', 3) + "  " + suite::TempoBus::clock (suite::TempoBus::secondsTo (sec.startBar, (float) bpm))
                           + "  " + suite::sectionName (sec.type)
                           + " (" + juce::String (shared.endBar (i) - sec.startBar) + " bars)"
                           + (shared.keyShiftOf (i) != 0 && i == shared.lastDropIndex() ? "  key +" + juce::String (shared.keyShiftOf (i)) : juce::String()));
            }
        }
        folder.getChildFile ("Song sheet.txt").replaceWithText (sheet.joinIntoString ("\n") + "\n");

        // The style log: how you make trance, one exported song at a time.
        juce::StringArray degrees, structure;
        for (int i = 0; i < s.count; ++i) degrees.add (chords::romanNumeral (s, s.slots[(size_t) i]));
        for (int i = 0; i < shared.count; ++i) structure.add (suite::sectionName (shared.sections[(size_t) i].type));
        suite::StyleLog::add ("song", { { "title", title }, { "key", key }, { "bpm", juce::String (juce::roundToInt (bpm)) },
                                        { "progression", progression.joinIntoString ("-") }, { "degrees", degrees.joinIntoString ("-") },
                                        { "bars", shared.count > 0 ? juce::String (shared.totalBars) : juce::String() },
                                        { "structure", structure.joinIntoString ("-") } });
    }

    suite::SectionMap map;
    if (suite::SectionBus::read (map) && map.count > 0)
    {
        const auto patterns = currentSongPatterns();
        std::vector<std::pair<juce::String, std::vector<chords::Note>>> parts;
        int thinned = 0, turned = 0;
        for (const auto& [name, part] : { std::pair<const char*, chords::SongPart> { "Chords", chords::SongPart::Chords },
                                          { "Bass", chords::SongPart::Bass }, { "Arp", chords::SongPart::Arp }, { "Lead", chords::SongPart::Lead },
                                          { "Harmony", chords::SongPart::Harmony }, { "Counter", chords::SongPart::Counter },
                                          { "Piano", chords::SongPart::Piano }, { "Strings", chords::SongPart::Strings }, { "Brass", chords::SongPart::Brass },
                                          { "Choir", chords::SongPart::Choir }, { "Timpani", chords::SongPart::Timpani }, { "Acid", chords::SongPart::Acid },
                                          { "Chops", chords::SongPart::Chops }, { "Candy", chords::SongPart::Candy } })
            if (auto notes = chords::songClip (s, map, patterns, part, &thinned, &turned); ! notes.empty() || part == chords::SongPart::Chords || part == chords::SongPart::Bass
                                                                         || part == chords::SongPart::Arp || part == chords::SongPart::Lead)
                parts.push_back ({ name, std::move (notes) });
        if (apvts.getRawParameterValue (Params::songFills)->load() > 0.5f)
            parts.push_back ({ "Fills", chords::fillsClip (map) });   // General MIDI drums: use FPC or a drum kit
        for (auto& [name, notes] : parts)
            if (name != "Chords" && name != "Fills" && suite::Autopilot::on (suite::Autopilot::hangoverTrimF))
                if (const auto t = chords::trimChordHangovers (notes, s, map); t > 0) suite::Autopilot::count (suite::Autopilot::hangoversTrimmed, (uint32_t) t);
        for (auto& [name, notes] : parts)
            if (name == "Bass" && suite::Autopilot::on (suite::Autopilot::lowBassFoldF))
                if (const auto moved = chords::foldLowBass (notes); moved > 0) suite::Autopilot::count (suite::Autopilot::lowNotesFolded, (uint32_t) moved);
        if (thinned > 0) suite::Autopilot::count (suite::Autopilot::partsThinned, (uint32_t) thinned);
        if (turned > 0) suite::Autopilot::count (suite::Autopilot::turnsTaken, (uint32_t) turned);

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
        const auto spFeel = currentSongPatterns();
        for (auto& [name, notes] : parts)
        {
            if (spFeel.write.feel && spFeel.library)
            {
                auto part = (patterns::Part) juce::jmax (0, patterns::partFiles().indexOf (name.toLowerCase()));
                if (name == "Harmony") part = patterns::Part::Lead;
                notes = chords::writing::microTiming (notes, part, (patterns::Style) juce::jlimit (0, 6, spFeel.libStyle));
            }
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
    state.removeChild (state.getChildWithName ("BassDna"), nullptr);
    if (const auto b = getBassDna(); b.valid)
    {
        juce::ValueTree bt ("BassDna");
        juce::StringArray onset, octave;
        for (auto v : b.onset) onset.add (juce::String (v, 3));
        for (auto v : b.octave) octave.add (juce::String (v, 3));
        bt.setProperty ("onset", onset.joinIntoString (","), nullptr);
        bt.setProperty ("octave", octave.joinIntoString (","), nullptr);
        bt.setProperty ("clips", b.clips, nullptr);
        bt.setProperty ("notes", b.notes, nullptr);
        state.appendChild (bt, nullptr);
    }
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
            chords::BassDna learnedBass;
            if (const auto bt = state.getChildWithName ("BassDna"); bt.isValid())
            {
                const auto onset = juce::StringArray::fromTokens (bt["onset"].toString(), ",", "");
                const auto octave = juce::StringArray::fromTokens (bt["octave"].toString(), ",", "");
                if (onset.size() == 16 && octave.size() == 16)
                {
                    for (int i = 0; i < 16; ++i) { learnedBass.onset[(size_t) i] = onset[i].getFloatValue(); learnedBass.octave[(size_t) i] = octave[i].getFloatValue(); }
                    learnedBass.clips = (int) bt["clips"];
                    learnedBass.notes = (int) bt["notes"];
                    learnedBass.valid = true;
                }
            }
            state.removeChild (state.getChildWithName ("BassDna"), nullptr);
            { const juce::ScopedLock sl (motifLock); bassDna = learnedBass; }
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
