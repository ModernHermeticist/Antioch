// Offline tests for Correspondence. Usage: CorrespondenceTests [output-dir]

#include "PluginProcessor.h"
#include "KeyBus.h"
#include "SectionBus.h"
#include "TestUtils.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<CorrespondenceProcessor> make()
    {
        auto p = std::make_unique<CorrespondenceProcessor>();
        prepare (*p);
        return p;
    }

    chords::Settings aMinor()
    {
        chords::Settings s;
        s.root = 9; s.scale = 0; s.count = 4;
        const int degrees[] = { 1, 6, 3, 7 };
        for (int i = 0; i < 4; ++i) s.slots[(size_t) i].degree = degrees[i];
        return s;
    }

    std::set<int> pitchClasses (const std::vector<int>& v)
    {
        std::set<int> pcs;
        for (auto p : v) pcs.insert (((p % 12) + 12) % 12);
        return pcs;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Chord spelling");
    {
        auto s = aMinor();
        juce::StringArray names;
        for (int i = 0; i < 4; ++i) names.add (chords::chordName (s, s.slots[(size_t) i]));
        check (names.joinIntoString (" ") == "Am F C G", "A minor anthem spells Am F C G (" + names.joinIntoString (" ") + ")");

        juce::StringArray romans;
        for (int i = 0; i < 4; ++i) romans.add (chords::romanNumeral (s, s.slots[(size_t) i]));
        check (romans.joinIntoString (" ") == "i VI III VII", "roman numerals i VI III VII (" + romans.joinIntoString (" ") + ")");

        chords::Slot v7 { 5, chords::Quality::Major };
        check (chords::chordName (s, v7) == "E", "major V override gives E");
        s.scale = 2; // harmonic minor
        check (chords::chordName (s, { 5 }) == "E", "harmonic minor V is major automatically");
        check (chords::chordName (s, { 7 }) == juce::String ("G#dim"), "harmonic minor vii is diminished");
        s.scale = 0;
        check (chords::chordName (s, { 1, chords::Quality::Seventh }) == "Am7", "i7 = Am7");
        check (chords::chordName (s, { 6, chords::Quality::Seventh }) == "Fmaj7", "VI7 = Fmaj7");
        check (chords::chordName (s, { 2, chords::Quality::Seventh }) == "Bm7b5", "ii7 = Bm7b5");
        check (chords::chordName (s, { 1, chords::Quality::Sus2 }) == "Asus2", "sus2");
        check (chords::chordName (s, { 4, chords::Quality::Add9 }) == "Dmadd9", "add9 keeps the minor third");

        // Every diatonic triad in every scale stays inside the scale.
        bool allInScale = true;
        for (int sc = 0; sc < (int) suite::music::scales().size(); ++sc)
            for (int d = 1; d <= 7; ++d)
            {
                chords::Settings t; t.root = 0; t.scale = sc;
                const auto c = chords::buildChord (t, { d });
                for (auto iv : c.intervals)
                {
                    const auto pc = (c.root + iv) % 12;
                    const auto& steps = suite::music::scales()[(size_t) sc].steps;
                    if (std::find (steps.begin(), steps.end(), pc) == steps.end()) allInScale = false;
                }
            }
        check (allInScale, "Auto chords are diatonic in all 6 scales");
    }

    section ("Voicing and voice leading");
    {
        // C major I-IV-V-I: root-position chords jump around, voice leading should not.
        chords::Settings vl;
        vl.root = 0; vl.scale = 1; vl.count = 4;
        const int degrees[] = { 1, 4, 5, 1 };
        for (int i = 0; i < 4; ++i) vl.slots[(size_t) i].degree = degrees[i];
        vl.voicing = chords::Voicing::Close;
        vl.voiceLead = false;
        const auto plain = chords::voiceProgression (vl);
        vl.voiceLead = true;
        const auto ledC = chords::voiceProgression (vl);

        auto s = aMinor();
        s.voicing = chords::Voicing::Close;
        s.voiceLead = true;
        const auto led = chords::voiceProgression (s);

        auto totalMove = [] (const std::vector<std::vector<int>>& v)
        {
            int total = 0;
            for (size_t i = 1; i < v.size(); ++i)
                for (size_t k = 0; k < std::min (v[i].size(), v[i - 1].size()); ++k)
                    total += std::abs (v[i][k] - v[i - 1][k]);
            return total;
        };
        check (totalMove (ledC) < totalMove (plain), "voice leading reduces movement (" + juce::String (totalMove (ledC))
                                                       + " vs " + juce::String (totalMove (plain)) + " semitones)");

        bool tonesRight = true, inRegister = true;
        for (int i = 0; i < 4; ++i)
        {
            const auto c = chords::buildChord (s, s.slots[(size_t) i]);
            std::set<int> expected;
            for (auto iv : c.intervals) expected.insert ((s.root + c.root + iv) % 12);
            if (pitchClasses (led[(size_t) i]) != expected) tonesRight = false;
            for (auto p : led[(size_t) i]) if (p < 50 || p > 80) inRegister = false;
        }
        check (tonesRight, "voiced chords contain exactly the chord tones");
        bool rootPosition = true;
        for (int i = 0; i < 4; ++i)
            if (plain[(size_t) i].front() % 12 != chords::chordRootPitchClass (vl, i)) rootPosition = false;
        check (rootPosition, "voice leading off keeps every chord in root position");
        check (inRegister, "chords sit around the chosen register (C4 area)");

        s.voicing = chords::Voicing::Open;
        const auto open = chords::voiceProgression (s);
        check (open[0].back() - open[0].front() > led[0].back() - led[0].front(), "open voicing is wider than close");

        s.addBass = true;
        const auto withBass = chords::voiceProgression (s);
        check (withBass[1].front() % 12 == 5 && withBass[1].front() < 48, "bass note adds the chord root low (F)");
    }

    section ("Rhythm patterns");
    {
        auto s = aMinor();
        s.bassPattern = chords::BassPattern::Offbeat;
        const auto off = chords::bassClip (s);
        bool allOffbeat = true;
        for (auto& n : off) if (std::abs (std::fmod (n.start, 1.0) - 0.5) > 1.0e-9) allOffbeat = false;
        check (off.size() == 16 && allOffbeat, "offbeat bass: 4 hits per bar, all on the '&'");
        check (off.front().pitch == 45, "bass root A2 for Am (MIDI 45)");

        s.bassPattern = chords::BassPattern::Rolling;
        check (chords::bassClip (s).size() == 48, "rolling bass: 12 sixteenths per bar (kick spots left free)");

        s.chordPattern = chords::ChordPattern::Pluck332;
        const auto pluck = chords::chordClip (s);
        check (pluck.size() == 4u * 6u * chords::voiceProgression (s)[0].size(), "3-3-2 pluck: 6 hits per bar per voice");

        double end = 0.0;
        for (auto& n : chords::chordClip (s)) end = std::max (end, n.start + n.length);
        check (end <= 16.0 + 1.0e-9, "clip fits in count x length (4 bars)");

        s.arpOctaves = 2;
        s.arpPattern = chords::ArpPattern::Up;
        const auto arp = chords::arpClip (s);
        check (arp.size() == 64, "arp: one 16th per step over 4 bars");
        check (arp[1].pitch > arp[0].pitch && arp[2].pitch > arp[1].pitch, "arp up ascends");
    }

    section ("MIDI export");
    {
        auto p = make();
        p->applyPreset (3); // Emotional: i iv VI V(major)
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceTest");
        const auto file = p->exportClip ("Chords", dir);
        check (file.existsAsFile(), "file written: " + file.getFileName());
        check (file.getFileName().contains ("Am-Dm-F-E"), "file name carries the progression");

        juce::MidiFile midi;
        juce::FileInputStream in (file);
        check (midi.readFrom (in), "reads back as a MIDI file");
        int noteOns = 0;
        if (midi.getNumTracks() > 0)
            for (auto* e : *midi.getTrack (0))
                if (e->message.isNoteOn()) ++noteOns;
        const auto expected = (int) chords::chordClip (Params::readSettings (p->apvts)).size();
        check (noteOns == expected, "all notes exported (" + juce::String (noteOns) + ")");
        dir.deleteRecursively();
    }

    section ("Key broadcast");
    {
        auto p = make();
        set (*p, Params::rootNote, 4.0f);   // E
        set (*p, Params::scale, 3.0f);      // Dorian
        p->publishKey();
        const auto k = suite::KeyBus::read();
        check (k.valid && k.root == 4 && k.scale == 3, "other plugins read E Dorian");

        set (*p, Params::broadcast, 0.0f);
        p->publishKey();
        check (! suite::KeyBus::read().valid, "switching broadcast off withdraws the key");
    }

    section ("Full-song clips follow the Ouroboros map");
    {
        // Groove bars 1-2, Breakdown 3-4, Build 5-6, Drop 7-8 (4/4: 8 quarters per section).
        suite::SectionMap map;
        map.totalBars = 8; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 3, suite::SectionType::Breakdown };
        map.sections[2] = { 5, suite::SectionType::Build };
        map.sections[3] = { 7, suite::SectionType::Drop };

        auto s = aMinor();               // Am F C G, 1 bar per chord
        const chords::SongPatterns patterns;   // defaults
        const auto chordsSong = chords::songClip (s, map, patterns, chords::SongPart::Chords);
        const auto bassSong = chords::songClip (s, map, patterns, chords::SongPart::Bass);

        auto countIn = [] (const std::vector<chords::Note>& v, double from, double to)
        {
            int n = 0;
            for (auto& note : v) if (note.start >= from && note.start < to) ++n;
            return n;
        };
        check (countIn (chordsSong, 0, 8) == 0 && countIn (chordsSong, 8, 16) > 0, "no chords in the groove, pads in the breakdown");
        check (countIn (bassSong, 0, 8) > 0 && countIn (bassSong, 8, 24) == 0 && countIn (bassSong, 24, 32) > 0,
               "bass in groove and drop, none in breakdown and build");

        double latestEnd = 0.0;
        bool crossesSection = false;
        for (auto& n : chordsSong)
        {
            latestEnd = std::max (latestEnd, n.start + n.length);
            if ((int) std::floor (n.start / 8.0) != (int) std::floor ((n.start + n.length - 1.0e-6) / 8.0)) crossesSection = true;
        }
        check (! crossesSection && latestEnd <= 32.0, "no note spills into the next section");

        // The progression restarts at every section: the drop's first chord is Am (root A).
        int dropFirstLowest = 128;
        for (auto& n : chordsSong) if (std::abs (n.start - 24.0) < 1.0e-9) dropFirstLowest = std::min (dropFirstLowest, n.pitch);
        check (dropFirstLowest % 12 == 9 || dropFirstLowest % 12 == 0 || dropFirstLowest % 12 == 4, "the drop starts on chord 1 (Am)");

        // Through the plugin: drag/save works with a map, and is refused without one.
        auto p = make();
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceSongTest");
        suite::SectionBus::publish (map);
        const auto file = p->exportClip ("Song Bass", dir);
        juce::MidiFile midi;
        juce::FileInputStream in (file);
        check (file.existsAsFile() && midi.readFrom (in), "song bass clip written: " + file.getFileName());
        suite::SectionBus::withdraw();
        check (! p->exportClip ("Song Chords", dir).existsAsFile(), "without an Ouroboros map there's nothing to export");
        dir.deleteRecursively();
    }

    section ("Adopting a key from Emerald Tablet");
    {
        auto p = make();
        suite::KeyBus::suggest (7, 1);   // G major detected
        p->publishKey();
        check ((int) p->apvts.getRawParameterValue (Params::rootNote)->load() == 7
                   && (int) p->apvts.getRawParameterValue (Params::scale)->load() == 1, "adopts a new detected key (G major)");

        set (*p, Params::acceptDetected, 0.0f);
        suite::KeyBus::suggest (2, 0);   // D minor detected, but accepting is off
        p->publishKey();
        check ((int) p->apvts.getRawParameterValue (Params::rootNote)->load() == 7, "ignores suggestions when Accept detected is off");

        suite::KeyBus::suggest (4, 0);
        auto q = make();                 // a project loading after the suggestion was made
        q->publishKey();
        check ((int) q->apvts.getRawParameterValue (Params::rootNote)->load() == 9, "a freshly loaded project keeps its own key");
        suite::KeyBus::withdraw();
    }

    section ("Audio: pass-through and preview");
    {
        auto p = make();
        juce::AudioBuffer<float> audio;
        render (*p, 0.2, pluck(), &audio);
        juce::AudioBuffer<float> ref (2, audio.getNumSamples());
        ref.clear();
        pluck() (ref, 0);
        float diff = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i)
            diff = std::max (diff, std::abs (audio.getSample (0, i) - ref.getSample (0, i)));
        check (diff < 1.0e-6f, "audio passes through untouched");

        p->preview.playChord ({ 57, 60, 64 });
        render (*p, 0.5, {}, &audio);
        check (rmsBetween (audio, 0.1, 0.5) > 0.01f, "preview chord is audible");
        p->preview.stop();
        render (*p, 2.0, {}, &audio);
        check (peakBetween (audio, 1.8, 2.0) < 1.0e-3f * peakBetween (audio, 0.0, 0.2), "preview releases to silence (peak "
                                                         + juce::String (peakBetween (audio, 0.0, 0.2), 4) + " -> "
                                                         + juce::String (peakBetween (audio, 1.8, 2.0), 6) + ")");
    }

    if (outDir != juce::File())
    {
        auto p = make();
        p->applyPreset (8); // Epic 8
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("correspondence.png"));
        editor.reset();
    }

    return finish();
}
