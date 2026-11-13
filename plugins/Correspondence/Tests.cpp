// Offline tests for Correspondence. Usage: CorrespondenceTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TempoBus.h"
#include "SpireTools.h"
#include "Harmony.h"
#include "Clash.h"
#include "VocalAware.h"
#include "FixBus.h"
#include "KeyBus.h"
#include "SectionBus.h"
#include "TestUtils.h"
#include "Audition.h"
#include "ChordBus.h"
#include "VocalBus.h"
#include "WritingBus.h"
#include "StyleLog.h"

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

    juce::File writeClip (const juce::File& file, const std::vector<chords::Note>& notes)
    {
        juce::MidiMessageSequence seq;
        for (const auto& n : notes)
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity), n.start * 960.0);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), (n.start + n.length) * 960.0);
        }
        seq.updateMatchedPairs();
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (960);
        midi.addTrack (seq);
        juce::FileOutputStream out (file);
        out.setPosition (0); out.truncate();
        midi.writeTo (out);
        return file;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    suite::WritingBus::clearSung();   // nothing left over from another plugin's tests
    suite::WritingBus::publishThin (0);
    suite::TempoBus::clear();   // no tempo map left over from another test run
    // Never write the real style log from tests.
    const auto testStyleLog = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SuiteTestsStyleLog.txt");
    suite::StyleLog::setFileForTests (testStyleLog);
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

    section ("Walking bass and breakdown re-harmonisation");
    {
        auto s = aMinor();   // Am F C G, 1 bar each, bass in octave 2
        s.bassPattern = chords::BassPattern::Walking;
        const auto bass = chords::bassClip (s);
        // Bar 1 (A) walking to F: the last two 16ths step down through the scale, ending a step above F.
        auto at = [&bass] (double t) { for (auto& n : bass) if (std::abs (n.start - t) < 1.0e-6) return n.pitch; return -1; };
        const auto a = at (0.25), passing = at (3.5), approach = at (3.75), f = at (4.25);
        check (a % 12 == 9 && f % 12 == 5, "rolls on each chord's root (A, then F)");
        check (approach == f + 2 && passing == a, "walks A, G, F (a step from A, so no extra passing note) (" + juce::String (passing) + ", " + juce::String (approach)
                   + " -> " + juce::String (f) + ")");
        const auto& steps = suite::music::scales()[0].steps;
        bool inKey = true;
        for (auto& n : bass) inKey = inKey && std::find (steps.begin(), steps.end(), ((n.pitch - 9) % 12 + 12) % 12) != steps.end();
        check (inKey, "every walking note is in the key");
        // G (bar 4) back to A for the loop: the approach note is a step below A, i.e. G itself.
        check (at (15.75) == at (15.25) && at (15.25) % 12 == 7, "G to A is a single step: it just leads in on G");
        // C (bar 3) to G: a fourth down, so it walks C, B... A, then G.
        check (at (11.5) % 12 == 11 && at (11.75) % 12 == 9 && at (12.25) % 12 == 7, "C walks down B, A into G");

        // Breakdowns start on VI (F): Am F C G becomes F C G Am there.
        suite::SectionMap map;
        map.totalBars = 8; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 5, suite::SectionType::Drop };
        chords::SongPatterns patterns;
        patterns.chords[(size_t) suite::SectionType::Breakdown] = 1;   // sustained pads
        patterns.bass[(size_t) suite::SectionType::Breakdown] = 1;     // offbeat bass, so we can see its roots
        const auto bd = chords::songClip (s, map, patterns, chords::SongPart::Bass);
        int firstBreakdown = -1, firstDrop = -1;
        for (auto& n : bd) { if (n.start < 16.0 && firstBreakdown < 0) firstBreakdown = n.pitch; if (n.start >= 16.0 && firstDrop < 0) firstDrop = n.pitch; }
        check (firstBreakdown % 12 == 5 && firstDrop % 12 == 9, "the breakdown starts on F (VI), the drop on Am");
        patterns.breakdownStart = 2;
        const auto iv = chords::songClip (s, map, patterns, chords::SongPart::Bass);
        check (! iv.empty() && iv.front().pitch % 12 == 2, "'Start on IV' puts Dm (not in the progression) first");
        patterns.breakdownStart = 0;
        check (chords::songClip (s, map, patterns, chords::SongPart::Bass).front().pitch % 12 == 9, "'Same as the drop' keeps Am first");
    }

    section ("Counter-melody");
    {
        auto s = aMinor();
        const auto mel = chords::melody (s, {});
        const auto counter = chords::counterMelody (mel, s);
        check (counter.size() >= 4, "answers the lead (" + juce::String ((int) counter.size()) + " notes)");
        bool inGaps = true, below = true, inKey = true, strongOnChord = true;
        const auto& steps = suite::music::scales()[0].steps;
        for (const auto& c : counter)
        {
            for (const auto& m : mel)
                if (c.start < m.start + 1.0e-6 && m.start < c.start + c.length - 1.0e-6) inGaps = false;   // runs into the next lead note
            int nearest = 0; double best = 1.0e9;
            for (const auto& m : mel) if (m.start <= c.start && c.start - m.start < best) { best = c.start - m.start; nearest = m.pitch; }
            below = below && c.pitch < nearest;
            inKey = inKey && std::find (steps.begin(), steps.end(), ((c.pitch - 9) % 12 + 12) % 12) != steps.end();
            const auto pcs = chords::chordTonesAt (s, c.start);
            if (std::abs (std::fmod (c.start, 1.0)) < 1.0e-9)
                strongOnChord = strongOnChord && std::find (pcs.begin(), pcs.end(), c.pitch % 12) != pcs.end();
        }
        check (inGaps, "between the lead's notes, never on or across one");
        check (below && inKey && strongOnChord, "below the lead, in key, with chord tones on the beat");

        suite::SectionMap map;
        map.totalBars = 8; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 5, suite::SectionType::Drop };
        chords::SongPatterns patterns;
        const auto song = chords::songClip (s, map, patterns, chords::SongPart::Counter);
        bool dropOnly = ! song.empty();
        for (auto& n : song) dropOnly = dropOnly && n.start >= 16.0;
        check (dropOnly, "in the song: wherever the full lead plays (the drop), not under the breakdown teaser");
        patterns.counter = false;
        check (chords::songClip (s, map, patterns, chords::SongPart::Counter).empty(), "and can be switched off");
    }

    section ("Humanised exports");
    {
        auto s = aMinor();
        s.bassPattern = chords::BassPattern::Rolling;
        const auto raw = chords::bassClip (s);
        const auto human = chords::humanise (raw, 1.0f, 42);
        double onBeat = 0.0, other = 0.0; int nOn = 0, nOther = 0;
        bool timingSmall = human.size() == raw.size(), sameVelocities = true;
        for (size_t i = 0; i < raw.size() && i < human.size(); ++i)
        {
            const auto inBeat = raw[i].start - std::floor (raw[i].start);
            if (std::abs (inBeat - 0.5) < 1.0e-6) { onBeat += human[i].velocity; ++nOn; }
            else if (inBeat > 1.0e-6) { other += human[i].velocity; ++nOther; }
            timingSmall = timingSmall && std::abs (human[i].start - raw[i].start) <= 0.0121;
            sameVelocities = sameVelocities && human[i].velocity == raw[i].velocity;
        }
        check (nOn > 0 && nOther > 0 && onBeat / nOn > other / nOther + 4.0, "off-beat bass notes are accented over the rest ("
                   + juce::String (onBeat / nOn, 1) + " vs " + juce::String (other / nOther, 1) + ")");
        check (timingSmall && ! sameVelocities, "timing moves by at most about 5 ms; velocities vary");
        const auto again = chords::humanise (raw, 1.0f, 42);
        bool same = again.size() == human.size();
        for (size_t i = 0; same && i < again.size(); ++i) same = again[i].velocity == human[i].velocity && std::abs (again[i].start - human[i].start) < 1.0e-12;
        check (same, "the same clip humanises the same way every time");
        const auto none = chords::humanise (raw, 0.0f, 42);
        bool untouched = none.size() == raw.size();
        for (size_t i = 0; untouched && i < raw.size(); ++i) untouched = none[i].velocity == raw[i].velocity && std::abs (none[i].start - raw[i].start) < 1.0e-12;
        check (untouched, "Humanise 0%: exactly the plain pattern");
    }

    section ("New song (session preset)");
    {
        auto p = make();
        suite::SessionBus::send ({ 2, 7, 1 });   // Tech trance in G major
        p->pollSession();
        check ((int) p->apvts.getRawParameterValue (Params::rootNote)->load() == 7 && (int) p->apvts.getRawParameterValue (Params::scale)->load() == 1,
               "sets the key (G major)");
        const auto s = Params::readSettings (p->apvts);
        const auto& driving = chords::presets()[(size_t) suite::sessionStyles()[2].progression];
        check (s.count == driving.count && s.slots[0].degree == driving.slots[0].degree, "and the style's progression (" + juce::String (driving.name) + ")");
        auto q = make();
        q->pollSession();
        check ((int) q->apvts.getRawParameterValue (Params::rootNote)->load() == 9, "a plugin loaded afterwards doesn't replay it");

        suite::SessionBus::send ({ 2, 7, 0, 3 });   // Tech trance, but Melancholic
        p->pollSession();
        const auto sad = Params::readSettings (p->apvts);
        const auto& heartbreak = chords::presets()[(size_t) suite::sessionMood (3).progression];
        bool same = sad.count == heartbreak.count;
        for (int i = 0; same && i < sad.count; ++i) same = sad.slots[(size_t) i].degree == heartbreak.slots[(size_t) i].degree;
        check (same, "a mood picks its own progression over the style's (" + juce::String (heartbreak.name) + ")");
    }

    section ("Final-drop key change");
    {
        auto s = aMinor();
        suite::SectionMap map;
        map.totalBars = 12; map.count = 3; map.finalLift = 1;
        map.sections[0] = { 1, suite::SectionType::Drop };
        map.sections[1] = { 5, suite::SectionType::Breakdown };
        map.sections[2] = { 9, suite::SectionType::Drop };
        chords::SongPatterns patterns;
        auto noLift = map; noLift.finalLift = 0;
        const auto lifted = chords::songClip (s, map, patterns, chords::SongPart::Chords);
        const auto plain = chords::songClip (s, noLift, patterns, chords::SongPart::Chords);
        bool firstSame = lifted.size() == plain.size(), lastUp = lifted.size() == plain.size();
        for (size_t i = 0; i < lifted.size() && i < plain.size(); ++i)
        {
            if (lifted[i].start < 16.0) firstSame = firstSame && lifted[i].pitch == plain[i].pitch;
            if (lifted[i].start >= 32.0) lastUp = lastUp && lifted[i].pitch == plain[i].pitch + 1;
        }
        check (firstSame && lastUp, "song chords: the first drop is unchanged, the last drop is a semitone up");
        check (map.keyShiftOf (0) == 0 && map.keyShiftOf (1) == 0 && map.keyShiftOf (2) == 1, "only the last drop (and after) is shifted");
    }

    section ("Melodia: later drops vary, with a harmony line");
    {
        auto s = aMinor();
        suite::SectionMap map;   // Drop 1-4, Breakdown 5-8, Drop 9-12
        map.totalBars = 12; map.count = 3;
        map.sections[0] = { 1, suite::SectionType::Drop };
        map.sections[1] = { 5, suite::SectionType::Breakdown };
        map.sections[2] = { 9, suite::SectionType::Drop };
        chords::SongPatterns patterns;
        patterns.doubleFinalDrop = false;   // compared note for note with the harmony (doubling has its own test)
        const auto lead = chords::songClip (s, map, patterns, chords::SongPart::Lead);
        std::vector<chords::Note> first, second;
        for (const auto& n : lead) { if (n.start < 16.0) first.push_back (n); else if (n.start >= 32.0) second.push_back (n); }
        check (second.size() > first.size(), "the second drop's lead has extra passing notes (" + juce::String ((int) first.size()) + " -> "
                   + juce::String ((int) second.size()) + ")");
        const auto& steps = suite::music::scales()[0].steps;
        bool inKey = true;
        for (const auto& n : second) inKey = inKey && std::find (steps.begin(), steps.end(), ((n.pitch - 9) % 12 + 12) % 12) != steps.end();
        check (inKey, "every added note is in the key");
        int maxFirst = 0, maxSecond = 0;
        for (auto& n : first) maxFirst = std::max (maxFirst, n.pitch);
        for (auto& n : second) maxSecond = std::max (maxSecond, n.pitch);
        check (maxSecond >= maxFirst + 12 + 7, "a climax note leaps above the (octave-lifted) line (" + juce::String (maxFirst) + " -> " + juce::String (maxSecond) + ")");

        const auto harm = chords::songClip (s, map, patterns, chords::SongPart::Harmony);
        bool onlyLater = ! harm.empty(), thirds = true;
        for (const auto& n : harm) onlyLater = onlyLater && n.start >= 32.0;
        for (size_t i = 0; i < harm.size() && i < second.size(); ++i)
        {
            const auto d = second[i].pitch - harm[i].pitch;
            thirds = thirds && (d == 3 || d == 4);
        }
        check (onlyLater && harm.size() == second.size() && thirds, "the harmony is a diatonic third under the lead, only in the later drop");

        patterns.varyLaterDrops = false;
        check (chords::songClip (s, map, patterns, chords::SongPart::Harmony).empty(), "switching variation off removes the harmony");
    }

    section ("Export all");
    {
        auto p = make();
        set (*p, Params::libOn, 0.0f);   // the classic parts (the library's layers are checked with the library)
        suite::SectionMap map;
        map.totalBars = 16; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 5, suite::SectionType::Drop };
        map.sections[2] = { 9, suite::SectionType::Breakdown };
        map.sections[3] = { 13, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        suite::VocalBus::clear();   // no vocal: no vocal answer clip
        const auto parent = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceExportAll");
        parent.deleteRecursively();
        const auto folder = p->exportEverything (parent);
        const auto files = folder.findChildFiles (juce::File::findFiles, false, "*.mid");
        check (folder.isDirectory() && files.size() == 18, "a song folder with 16 clips (incl. counter-melody, drop-2 harmony, drum fills, breakdown piano and the three Spire layers), the Spire macros and one all-parts file (" + juce::String (files.size()) + " files in "
                   + folder.getFileName() + ")");

        juce::File all;
        for (auto& f : files) if (f.getFileName().contains ("all parts")) all = f;
        juce::MidiFile midi;
        juce::FileInputStream in (all);
        const auto read = all.existsAsFile() && midi.readFrom (in);
        check (read && midi.getNumTracks() == 8, "all parts: a section track plus Chords, Bass, Arp, Lead, Harmony, Counter and Fills (" + juce::String (midi.getNumTracks()) + " tracks)");
        if (read && midi.getNumTracks() == 8)
        {
            juce::StringArray markers;
            for (auto* e : *midi.getTrack (0))
                if (e->message.isTextMetaEvent() && e->message.getMetaEventType() == 6)
                    markers.add (e->message.getTextFromTextMetaEvent() + "@" + juce::String (e->message.getTimeStamp() / 960.0, 0));
            check (markers.joinIntoString (" ") == "Groove@0 Drop@16 Breakdown@32 Drop 2@48", "sections as markers: " + markers.joinIntoString (" "));
            int notes = 0;
            for (int t = 1; t < 8; ++t)
                for (auto* e : *midi.getTrack (t)) notes += e->message.isNoteOn();
            check (notes > 50, "with the notes of every part (" + juce::String (notes) + ")");
        }

        suite::SectionBus::withdraw();
        const auto noMap = p->exportEverything (parent);
        check (noMap != folder && noMap.findChildFiles (juce::File::findFiles, false, "*.mid").size() == 5,
               "without Ouroboros: just the five loop clips, in a new folder");
        parent.deleteRecursively();
    }

    section ("Melodia");
    {
        auto s = aMinor();   // Am F C G, 1 bar each
        chords::MelodySettings m;
        const auto mel = chords::melody (s, m);
        check (mel.size() > 10, "writes a melody (" + juce::String ((int) mel.size()) + " notes over 4 bars)");

        bool fits = true, strongOnChord = true;
        int lo = 128, hi = 0;
        for (const auto& n : mel)
        {
            const auto pcs = chords::chordTonesAt (s, n.start);
            const auto inChord = std::find (pcs.begin(), pcs.end(), n.pitch % 12) != pcs.end();
            const auto& steps = suite::music::scales()[0].steps;
            const auto inScale = std::find (steps.begin(), steps.end(), ((n.pitch - 9) % 12 + 12) % 12) != steps.end();
            if (! inScale && ! inChord) fits = false;
            if (std::abs (std::fmod (n.start, 1.0)) < 1.0e-9 && ! inChord) strongOnChord = false;
            lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch);
        }
        check (fits, "every note is in the key (or the chord under it)");
        check (strongOnChord, "notes on the beat are chord tones");
        check (hi - lo <= 19, "stays within a singable range (" + juce::String (hi - lo) + " semitones)");
        check (mel.back().pitch % 12 == 7 && mel.back().start + mel.back().length > 15.9, "ends on the root of the last chord (G), held to the end");

        // The answer (bars 3-4) repeats the call's rhythm (bars 1-2).
        std::vector<double> call, answer;
        for (const auto& n : mel) { if (n.start < 8.0) call.push_back (n.start); else answer.push_back (n.start - 8.0); }
        bool sameRhythm = call.size() == answer.size();
        for (size_t i = 0; sameRhythm && i < call.size(); ++i) sameRhythm = std::abs (call[i] - answer[i]) < 1.0e-9;
        check (sameRhythm, "the answer repeats the call's rhythm");

        auto again = chords::melody (s, m);
        bool same = again.size() == mel.size();
        for (size_t i = 0; same && i < mel.size(); ++i) same = again[i].pitch == mel[i].pitch;
        check (same, "the same seed gives the same melody");
        m.seed = 8;
        const auto other = chords::melody (s, m);
        bool differs = other.size() != mel.size();
        for (size_t i = 0; ! differs && i < mel.size(); ++i) differs = other[i].pitch != mel[i].pitch;
        check (differs, "a new seed gives a new melody");

        // Song lead: teaser in the breakdown, full melody in the drop.
        suite::SectionMap map;
        map.totalBars = 8; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 5, suite::SectionType::Drop };
        const chords::SongPatterns patterns;
        const auto lead = chords::songClip (s, map, patterns, chords::SongPart::Lead);
        int inBreakdown = 0, inDrop = 0;
        for (auto& n : lead) (n.start < 16.0 ? inBreakdown : inDrop)++;
        check (inBreakdown > 0 && inBreakdown < inDrop, "breakdown teases (" + juce::String (inBreakdown) + " notes), drop plays it all ("
                                                             + juce::String (inDrop) + ")");

        // Later drops lift the lead an octave.
        suite::SectionMap twoDrops;
        twoDrops.totalBars = 8; twoDrops.count = 2;
        twoDrops.sections[0] = { 1, suite::SectionType::Drop };
        twoDrops.sections[1] = { 5, suite::SectionType::Drop };
        const auto lifted = chords::songClip (s, twoDrops, patterns, chords::SongPart::Lead);
        int firstPitch = -1, secondPitch = -1;
        for (auto& n : lifted)
        {
            if (firstPitch < 0 && n.start < 16.0) firstPitch = n.pitch;
            if (secondPitch < 0 && n.start >= 16.0) secondPitch = n.pitch;
        }
        check (secondPitch == firstPitch + 12, "the lead goes up an octave in the second drop");

        auto p = make();
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceMelodyTest");
        check (p->exportClip ("Melody", dir).existsAsFile(), "Melody tile exports a MIDI file");
        dir.deleteRecursively();
    }

    section ("Melodia: learning a motif");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceMotifTest");
        dir.createDirectory();

        // A 2-bar A minor motif, starting in bar 3 of the clip (Melodia should rebase it to bar 1).
        const int pitches[] = { 69, 72, 76, 74, 72, 71, 69 };
        const double starts[] = { 0.0, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0 };
        juce::MidiMessageSequence seq;
        for (int i = 0; i < 7; ++i)
        {
            const auto t = (8.0 + starts[i]) * 960.0;
            seq.addEvent (juce::MidiMessage::noteOn (1, pitches[i], (juce::uint8) 100), t);
            seq.addEvent (juce::MidiMessage::noteOff (1, pitches[i]), t + 0.5 * 960.0);
            if (i == 2) seq.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 80), t);   // a lower harmony note at the same time
        }
        seq.addEvent (juce::MidiMessage::noteOff (1, 64), (8.0 + 2.0) * 960.0);
        seq.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), (8.0 + 9.0) * 960.0);   // past the 2 bars: ignored
        seq.addEvent (juce::MidiMessage::noteOff (1, 60), (8.0 + 9.5) * 960.0);
        seq.updateMatchedPairs();
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (960);
        midi.addTrack (seq);
        const auto file = dir.getChildFile ("motif.mid");
        { juce::FileOutputStream out (file); out.setPosition (0); out.truncate(); midi.writeTo (out); }

        auto p = make();
        const auto learned = p->learnMotif (file);
        check (learned == 7, "learns 7 notes (one per onset, highest wins, 2 bars) - got " + juce::String (learned));
        const auto motif = p->getMotif();
        check (! motif.empty() && std::abs (motif.front().start) < 1.0e-9 && std::abs (motif.back().start - 6.0) < 1.0e-9,
               "the motif is rebased to the start of its bar");

        auto s = aMinor();
        const auto mel = chords::melody (s, p->melodySettings());
        bool callMatches = mel.size() >= 7;
        for (int i = 0; callMatches && i < 7; ++i)
            callMatches = std::abs (mel[(size_t) i].start - starts[i]) < 1.0e-9
                       && mel[(size_t) i].pitch - mel[0].pitch == pitches[i] - pitches[0]
                       && mel[(size_t) i].pitch % 12 == pitches[i] % 12;
        check (callMatches, "the call is the motif as played (rhythm, notes and shape)");

        std::vector<double> answer;
        for (const auto& n : mel) if (n.start >= 8.0 && n.start < 16.0) answer.push_back (n.start - 8.0);
        bool sameRhythm = answer.size() == 7;
        for (size_t i = 0; sameRhythm && i < answer.size(); ++i) sameRhythm = std::abs (answer[i] - starts[i]) < 1.0e-9;
        check (sameRhythm, "the answer repeats the motif's rhythm");
        const auto answerStart = std::find_if (mel.begin(), mel.end(), [] (const auto& n) { return n.start >= 8.0; });
        const auto tones = chords::chordTonesAt (s, 8.0);
        check (answerStart != mel.end() && std::find (tones.begin(), tones.end(), answerStart->pitch % 12) != tones.end(),
               "the answer starts on a chord tone of bar 3");

        // An out-of-key motif note is pulled into the key.
        auto m = p->melodySettings();
        m.motif[1].pitch = 73;   // C#, not in A minor
        const auto snapped = chords::melody (s, m);
        const auto& steps = suite::music::scales()[0].steps;
        check (std::find (steps.begin(), steps.end(), ((snapped[1].pitch - 9) % 12 + 12) % 12) != steps.end(),
               "out-of-key motif notes are fitted to the key");

        // State round trip.
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getMotif().size() == 7, "the motif is saved with the project");

        p->forgetMotif();
        check (p->getMotif().empty() && chords::melody (s, p->melodySettings()).size() == chords::melody (s, chords::MelodySettings {}).size(),
               "Forget motif goes back to generated melodies");

        const auto junk = dir.getChildFile ("notmidi.mid");
        junk.replaceWithText ("hello");
        check (p->learnMotif (junk) == 0 && p->getMotif().empty(), "a file that isn't MIDI is ignored");
        dir.deleteRecursively();
    }

    section ("Lead glide and scoops");
    {
        // A phrase at 0, a rest, a new phrase at 2 with a leap (C5 -> E5) then a step.
        const std::vector<chords::Note> line { { 72, 0.0, 0.5, 100 }, { 72, 2.0, 1.0, 100 }, { 76, 3.0, 0.5, 100 }, { 77, 3.5, 0.5, 100 } };
        chords::LeadExpression x;
        const auto e = chords::expressLead (line, x);
        check (e.scoop[0] == 2 && e.scoop[1] == 2 && e.scoop[2] == 0 && e.scoop[3] == 0, "scoops into the first note of each phrase only");
        check (e.notes[1].start + e.notes[1].length > 3.0 && std::abs (e.notes[2].length - 0.5) < 1e-9,
               "the leap glides (C runs into E), the step doesn't");
        const auto midi = chords::toLeadMidiFile (line, 138.0, "Lead", 0, x);
        std::vector<std::pair<double, int>> wheel;
        for (auto* ev : *midi.getTrack (0))
            if (ev->message.isPitchWheel()) wheel.push_back ({ ev->message.getTimeStamp() / 960.0, ev->message.getPitchWheelValue() });
        check (wheel.size() == 18 && wheel.front().second < 100 && wheel[8].second == 8192 && wheel.back().second == 8192,
               "pitch bend: from 2 semitones below (bend range 2) back to centre within a 1/16 (" + juce::String ((int) wheel.size()) + " events)");
        x.bendRange = 12;
        const auto wide = chords::toLeadMidiFile (line, 138.0, "Lead", 0, x);
        int first = -1;
        for (auto* ev : *wide.getTrack (0)) if (ev->message.isPitchWheel() && first < 0) first = ev->message.getPitchWheelValue();
        check (std::abs (first - (8192 - 1365)) < 3, "with a 12-semitone bend range the same scoop is a smaller wheel move (" + juce::String (first) + ")");

        auto p = make();
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceLead");
        dir.deleteRecursively();
        auto bends = [&] (const juce::File& f)
        {
            juce::MidiFile m;
            juce::FileInputStream in (f);
            int n = 0;
            if (m.readFrom (in)) for (auto* ev : *m.getTrack (0)) n += ev->message.isPitchWheel();
            return n;
        };
        const auto withScoops = bends (p->exportClip ("Melody", dir));
        set (*p, Params::leadExpression, 0.0f);
        const auto plain = bends (p->exportClip ("Melody", dir.getChildFile ("plain")));
        const auto chordsClip = bends (p->exportClip ("Chords", dir));
        check (withScoops > 0 && plain == 0 && chordsClip == 0, "the exported MELODY carries scoops by default (" + juce::String (withScoops)
                   + " bend events); none when plain, and never on chords");
        dir.deleteRecursively();
    }

    section ("Phrase turnarounds");
    {
        auto s = aMinor();
        suite::SectionMap map;
        map.totalBars = 16; map.count = 1;
        map.sections[0] = { 1, suite::SectionType::Drop };   // one 16-bar drop: phrases end at bars 8 and 16
        chords::SongPatterns on, off;
        off.phraseVariation = false;
        for (auto part : { chords::SongPart::Bass, chords::SongPart::Arp })
        {
            const auto a = chords::songClip (s, map, on, part), b = chords::songClip (s, map, off, part);
            bool sameShape = a.size() == b.size() && ! a.empty();
            int changedIn8 = 0, changedElsewhere = 0;
            for (size_t i = 0; sameShape && i < a.size(); ++i)
            {
                if (a[i].pitch == b[i].pitch) continue;
                const auto inBar8 = a[i].start >= 28.0 && a[i].start < 32.0;
                (inBar8 ? changedIn8 : changedElsewhere)++;
                sameShape = a[i].pitch - b[i].pitch == 12;
            }
            check (sameShape && changedIn8 > 0 && changedElsewhere == 0,
                   juce::String (part == chords::SongPart::Bass ? "bass" : "arp") + ": only bar 8 turns around, an octave up ("
                       + juce::String (changedIn8) + " notes; the section's last bar is left alone)");
        }
    }

    section ("Progression DNA and the hook bank");
    {
        auto s = aMinor();
        check (identity::parseNumerals ("i-iv-VI-V", s) && s.count == 4 && s.slots[1].degree == 4 && s.slots[1].quality == chords::Quality::Auto
                   && s.slots[3].degree == 5 && s.slots[3].quality == chords::Quality::Major,
               "roman numerals read back as slots (V major in A minor, iv as the key's own)");

        const auto log = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceProgressionDna.txt");
        log.deleteFile();
        suite::StyleLog::setFileForTests (log);
        for (int k = 0; k < 2; ++k) suite::StyleLog::add ("song", { { "degrees", "i-iv-VI-V" } });
        suite::StyleLog::add ("song", { { "degrees", "i-VI-III-VII" } });
        const auto mine = identity::favouriteProgressions (suite::StyleLog::read());
        check (mine.size() == 2 && mine[0].first == "i-iv-VI-V" && mine[0].second == 2, "your most used progression comes first");
        auto p = make();
        suite::SessionBus::send ({ 0, 9, 0, 0 });   // New song, mood "as the style"
        p->pollSession();
        const auto set = Params::readSettings (p->apvts);
        check (set.count == 4 && set.slots[1].degree == 4 && set.slots[3].degree == 5, "New song starts from it (used in 2 songs)");
        suite::StyleLog::setFileForTests (testStyleLog);
        log.deleteFile();

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceHooks");
        dir.deleteRecursively();
        identity::hookFolderOverride() = dir;
        check (p->saveHook ("Big hook"), "saves the melody's first two bars as a hook");
        const auto hooks = identity::loadHooks();
        check (hooks.size() == 1 && hooks[0].first == "Big hook" && ! hooks[0].second.empty(), "the hook bank lists it");
        auto q = make();
        q->useHook (hooks[0].second);
        check (q->getMotif().size() == hooks[0].second.size(), "and a hook becomes another project's motif");
        identity::hookFolderOverride() = juce::File();
        dir.deleteRecursively();
    }

    section ("Breakdown piano, breakdown lift, the lead answering the vocal, the chord bus");
    {
        auto s = aMinor();
        suite::SectionMap map;   // Drop 1-8, Breakdown 9-24, Drop 25-32
        map.totalBars = 32; map.count = 3;
        map.sections[0] = { 1, suite::SectionType::Drop };
        map.sections[1] = { 9, suite::SectionType::Breakdown };
        map.sections[2] = { 25, suite::SectionType::Drop };
        std::vector<std::pair<double, bool>> pedal;
        const auto piano = chords::pianoClip (s, map, pedal);
        bool inBreakdown = ! piano.empty();
        for (auto& n : piano) inBreakdown = inBreakdown && n.start >= 32.0 && n.start < 96.0;
        int downs = 0;
        for (auto& [t, d] : pedal) downs += d;
        const auto first = std::count_if (piano.begin(), piano.end(), [] (const chords::Note& n) { return n.start >= 32.0 && n.start < 36.0; });
        check (inBreakdown && downs == 16 && first == 5, "breakdown piano: only in the breakdown, a low root and the chord broken upwards (5 notes per chord), pedal down for each of 16 chords");

        chords::SongPatterns p;
        p.breakdownStart = 3;   // lift to III halfway
        const auto lifted = chords::songClip (s, map, p, chords::SongPart::Chords);
        p.breakdownStart = 0;
        const auto same = chords::songClip (s, map, p, chords::SongPart::Chords);
        auto pcsAt = [] (const std::vector<chords::Note>& v, double q) { std::vector<int> o; for (auto& n : v) if (std::abs (n.start - q) < 1.0e-6) o.push_back (n.pitch); return pitchClasses (o); };
        check (pcsAt (lifted, 32.0) == pcsAt (same, 32.0) && pcsAt (lifted, 64.0) == std::set<int> { 0, 4, 7 } && pcsAt (same, 64.0) == std::set<int> { 9, 0, 4 },
               "breakdown lift: the first half as the drop, the second half starts on III (C major in A minor)");

        p = {};
        p.lead[(size_t) suite::SectionType::Breakdown] = 0;   // no lead in the breakdown
        p.vocalBars.assign (32, false);
        for (int b = 8; b < 10; ++b) p.vocalBars[(size_t) b] = true;   // the vocal sings bars 9-10
        const auto lead = chords::songClip (s, map, p, chords::SongPart::Lead);
        int answer = 0, elsewhere = 0;
        for (auto& n : lead)
        {
            if (n.start >= 40.0 && n.start < 42.0) ++answer;                 // bar 11, first two beats
            else if (n.start >= 32.0 && n.start < 96.0) ++elsewhere;
        }
        check (answer > 0 && elsewhere == 0, "the lead answers in the bar after the vocal phrase (" + juce::String (answer) + " notes), and nowhere else in the breakdown");

        auto c = make();
        c->applyPreset (0);
        c->publishKey();
        suite::Progression prog;
        check (suite::ChordBus::read (prog) && prog.count == 4 && prog.chordRoots[0] == 0 && prog.chordRoots[1] == 8 && prog.chordRoots[2] == 3 && prog.chordRoots[3] == 10
                   && ! prog.isTension (0) && prog.isTension (1) && ! prog.isTension (2),
               "the progression is shared with the suite: i VI III VII, with VI and VII as tension chords");
        suite::ChordBus::withdraw();
    }

    section ("Doubling the final drop");
    {
        auto s = aMinor();
        suite::SectionMap map;   // Drop 1-8, Breakdown 9-16, Drop 17-24
        map.totalBars = 24; map.count = 3;
        map.sections[0] = { 1, suite::SectionType::Drop };
        map.sections[1] = { 9, suite::SectionType::Breakdown };
        map.sections[2] = { 17, suite::SectionType::Drop };
        chords::SongPatterns p;
        p.ghostNotes = 0.0f;
        auto count = [] (const std::vector<chords::Note>& v, double a, double b) { int c = 0; for (auto& n : v) c += n.start >= a && n.start < b; return c; };
        auto lowest = [] (const std::vector<chords::Note>& v, double a, double b) { int lo = 127; for (auto& n : v) if (n.start >= a && n.start < b) lo = std::min (lo, n.pitch); return lo; };
        const auto lead = chords::songClip (s, map, p, chords::SongPart::Lead), bass = chords::songClip (s, map, p, chords::SongPart::Bass);
        p.doubleFinalDrop = false;
        const auto leadOff = chords::songClip (s, map, p, chords::SongPart::Lead), bassOff = chords::songClip (s, map, p, chords::SongPart::Bass);
        check (count (lead, 64.0, 96.0) == 2 * count (leadOff, 64.0, 96.0) && count (lead, 0.0, 32.0) == count (leadOff, 0.0, 32.0),
               "the final drop's lead is doubled (" + juce::String (count (leadOff, 64.0, 96.0)) + " -> " + juce::String (count (lead, 64.0, 96.0))
               + " notes); the first drop is untouched");
        bool octaves = true;
        for (auto& n : leadOff)
            if (n.start >= 64.0)
            {
                bool partner = false;
                for (auto& m : lead) partner = partner || (m.start == n.start && std::abs (m.pitch - n.pitch) == 12);
                octaves = octaves && partner;
            }
        check (octaves, "every double is an octave from its note");
        check (lowest (bass, 64.0, 96.0) == lowest (bassOff, 64.0, 96.0) - 12 && count (bass, 0.0, 32.0) == count (bassOff, 0.0, 32.0),
               "and the bass gains a sub octave there");
        chords::SongPatterns single;
        suite::SectionMap one = map;
        one.count = 2; one.totalBars = 16;
        check (chords::songClip (s, one, single, chords::SongPart::Lead).size() == chords::songClip (s, one, p, chords::SongPart::Lead).size(),
               "a song with one drop is left alone");
    }

    section ("Climbing chords, ghost notes, drum fills, room for the vocal");
    {
        auto s = aMinor();
        suite::SectionMap map;   // Groove 1-8, Drop 9-24, Breakdown 25-32, Drop 33-40
        map.totalBars = 40; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 9, suite::SectionType::Drop };
        map.sections[2] = { 25, suite::SectionType::Breakdown };
        map.sections[3] = { 33, suite::SectionType::Drop };
        chords::SongPatterns p;
        p.ghostNotes = 0.0f;
        auto inRange = [] (const std::vector<chords::Note>& v, double a, double b) { std::vector<chords::Note> o; for (auto& n : v) if (n.start >= a && n.start < b) o.push_back (n); return o; };
        const auto chordNotes = chords::songClip (s, map, p, chords::SongPart::Chords);
        const auto first = inRange (chordNotes, 32.0, 36.0), second = inRange (chordNotes, 128.0, 132.0);
        int topFirst = 0, topSecond = 0;
        for (auto& n : first) topFirst = std::max (topFirst, n.pitch);
        for (auto& n : second) topSecond = std::max (topSecond, n.pitch);
        check (second.size() > first.size() && topSecond >= topFirst + 9, "drop 2's chords climb: an extra top note an octave up ("
                   + juce::String ((int) first.size()) + " -> " + juce::String ((int) second.size()) + " notes, top " + juce::String (topFirst) + " -> " + juce::String (topSecond) + ")");

        p.ghostNotes = 0.35f;
        p.bass[(size_t) suite::SectionType::Groove] = 1;   // off-beat bass: gaps to fill
        p.bass[(size_t) suite::SectionType::Drop] = 1;
        const auto bass = chords::songClip (s, map, p, chords::SongPart::Bass);
        int ghostsDrop = 0, ghostsGroove = 0;
        bool noOverlap = true;
        for (size_t i = 0; i < bass.size(); ++i)
        {
            if (bass[i].velocity == 38 && bass[i].start >= 32.0 && bass[i].start < 64.0) ++ghostsDrop;   // first 8 bars of the drop
            if (bass[i].velocity == 38 && bass[i].start < 32.0) ++ghostsGroove;
            if (i + 1 < bass.size() && bass[i + 1].pitch == bass[i].pitch && bass[i].start + bass[i].length > bass[i + 1].start + 1.0e-6) noOverlap = false;
        }
        p.ghostNotes = 0.0f;
        int none = 0;
        for (auto& n : chords::songClip (s, map, p, chords::SongPart::Bass)) none += n.velocity == 38;
        check (ghostsDrop > ghostsGroove && ghostsDrop > 0 && none == 0 && noOverlap, "ghost notes: soft in-between bass notes, more in 8 bars of drop ("
                   + juce::String (ghostsDrop) + ") than 8 bars of groove (" + juce::String (ghostsGroove) + "), never overlapping; none when off");

        const auto fills = chords::fillsClip (map);
        int rollInto1 = 0, toms = 0, crashes = 0, phraseFill = 0;
        for (auto& n : fills)
        {
            if (n.pitch == 38 && n.start >= 28.0 && n.start < 32.0) ++rollInto1;
            if ((n.pitch == 43 || n.pitch == 45 || n.pitch == 48 || n.pitch == 50) && n.start >= 94.0 && n.start < 96.0) ++toms;
            if (n.pitch == 49) ++crashes;
            if (n.pitch == 38 && n.start >= 63.0 && n.start < 64.0) ++phraseFill;
        }
        check (rollInto1 == 16 && toms == 8 && crashes == 3 && phraseFill == 2,
               "drum fills: a 16-hit roll into the drop, toms into the breakdown, a crash on each change, a small fill at the drop's 8-bar phrase end");

        p.vocalBars.assign (40, false);
        for (int b = 8; b < 12; ++b) p.vocalBars[(size_t) b] = true;   // bars 9-12
        const auto lead = chords::songClip (s, map, p, chords::SongPart::Lead);
        p.leaveRoomForVocal = false;
        const auto fullLead = chords::songClip (s, map, p, chords::SongPart::Lead);
        bool onlyStrongBeats = true;
        for (auto& n : inRange (lead, 32.0, 48.0)) onlyStrongBeats = onlyStrongBeats && std::abs (std::fmod (n.start, 2.0)) < 1.0e-6;
        check (onlyStrongBeats && inRange (lead, 32.0, 48.0).size() < inRange (fullLead, 32.0, 48.0).size()
                   && inRange (lead, 48.0, 96.0).size() == inRange (fullLead, 48.0, 96.0).size(),
               "where the vocal sings (bars 9-12) the lead keeps only beats 1 and 3; elsewhere it's unchanged ("
                   + juce::String ((int) inRange (fullLead, 32.0, 48.0).size()) + " -> " + juce::String ((int) inRange (lead, 32.0, 48.0).size()) + " notes)");
    }

    section ("Bassline DNA and voicing DNA from MIDI");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceBassDna");
        dir.deleteRecursively();
        dir.createDirectory();
        // Two bars of an off-beat bass with an octave jump on the last 16th of each beat.
        std::vector<chords::Note> bassClip;
        for (int beat = 0; beat < 8; ++beat)
        {
            bassClip.push_back ({ 33, beat + 0.5, 0.2, 100 });
            bassClip.push_back ({ 45, beat + 0.75, 0.15, 90 });
        }
        check (chords::looksLikeBass (bassClip) && ! chords::looksLikeChords (bassClip), "a low single-note clip looks like a bassline");
        auto p = make();
        const auto msg = p->learnFromFiles ({ writeClip (dir.getChildFile ("bass.mid"), bassClip).getFullPathName() });
        const auto dna = p->getBassDna();
        check (dna.valid && (int) p->apvts.getRawParameterValue (Params::bassPattern)->load() == (int) chords::BassPattern::Mine
                   && dna.onset[2] > 0.9f && dna.onset[3] > 0.9f && dna.octave[3] > 0.9f && dna.onset[0] < 0.1f,
               "dropping it learns your bassline and picks the Mine pattern (" + msg + ")");
        const auto mine = chords::bassClip (p->currentSettings());
        bool onDna = ! mine.empty();
        int ups = 0;
        for (auto& n : mine)
        {
            const auto step = (int) std::lround (std::fmod (n.start, 1.0) * 4.0);
            onDna = onDna && (step == 2 || step == 3);
            ups += step == 3 && n.pitch % 12 == mine.front().pitch % 12 && n.pitch > mine.front().pitch;
        }
        check (onDna && ups > 0, "the Mine bass plays your steps over any chords, with your octave jumps");
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getBassDna().valid, "the bassline DNA is saved with the project");

        // A wide chord clip with the root underneath: Am F C G.
        std::vector<chords::Note> wide;
        const std::vector<std::vector<int>> voicings { { 45, 69, 72, 76, 81 }, { 41, 65, 69, 72, 77 }, { 36, 64, 67, 72, 76 }, { 43, 67, 71, 74, 79 } };
        for (int i = 0; i < 4; ++i) for (auto pitch : voicings[(size_t) i]) wide.push_back ({ pitch, i * 4.0, 3.9, 90 });
        const auto v = chords::voicingFromNotes (wide);
        check (v.valid && v.addBass && v.registerOctave == 4, "voicing DNA: the root underneath and the register are read from the clip");
        auto r = make();
        r->learnFromFiles ({ writeClip (dir.getChildFile ("wide.mid"), wide).getFullPathName() });
        check (r->apvts.getRawParameterValue (Params::addBass)->load() > 0.5f && (int) r->apvts.getRawParameterValue (Params::voicing)->load() == (int) v.voicing,
               "and set when you drop a chord clip");
        dir.deleteRecursively();
    }

    section ("Learning chords, DNA and groove from MIDI");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceLearnTest");
        dir.deleteRecursively();
        dir.createDirectory();

        // Am F C G, a bar each, played as triads in D-something voicings.
        const std::vector<std::vector<int>> triads { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
        std::vector<chords::Note> chordClip;
        for (int i = 0; i < 4; ++i)
            for (auto pitch : triads[(size_t) i]) chordClip.push_back ({ pitch, i * 4.0, 3.9, 90 });
        check (chords::looksLikeChords (chordClip), "a block-chord clip looks like chords");

        auto p = make();
        set (*p, Params::rootNote, 0.0f);
        const auto msg = p->learnFromFiles ({ writeClip (dir.getChildFile ("chords.mid"), chordClip).getFullPathName() });
        const auto s = Params::readSettings (p->apvts);
        check (s.root == 9 && s.scale == 0 && s.count == 4, "dropping Am F C G sets A minor with 4 chords (" + msg + ")");
        const int expected[] = { 1, 6, 3, 7 };
        bool degrees = true;
        for (int i = 0; i < 4; ++i) degrees = degrees && s.slots[(size_t) i].degree == expected[i];
        check (degrees, "... as degrees i VI III VII");

        // Two melody clips: melodic DNA.
        std::vector<chords::Note> a, b;
        const int mel[] = { 69, 72, 71, 69, 67, 69, 72, 74 };
        for (int i = 0; i < 8; ++i)
        {
            a.push_back ({ mel[i], i * 0.75, 0.5, 100 });
            b.push_back ({ mel[7 - i], 16.0 + i * 0.75, 0.5, 100 });
        }
        check (! chords::looksLikeChords (a), "a single-note line doesn't look like chords");
        const auto dnaMsg = p->learnFromFiles ({ writeClip (dir.getChildFile ("a.mid"), a).getFullPathName(),
                                                 writeClip (dir.getChildFile ("b.mid"), b).getFullPathName() });
        const auto dna = p->getDna();
        check (dna.valid && dna.clips == 2 && dna.notes == 16, "two clips teach melodic DNA (" + dnaMsg + ")");
        check (dna.onset[3] > 0.5f && dna.onset[1] < 0.01f, "the DNA keeps the clips' dotted rhythm");

        auto settings = aMinor();
        const auto withDna = chords::melody (settings, p->melodySettings());
        int onDna = 0;
        for (const auto& n : withDna)
            onDna += dna.onset[(size_t) ((int) std::lround (std::fmod (n.start, 8.0) * 4.0) % 32)] > 0.0f;
        check (! withDna.empty() && onDna * 10 >= (int) withDna.size() * 9, "new melodies land on the DNA's rhythm (" + juce::String (onDna) + "/" + juce::String ((int) withDna.size()) + ")");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getDna().valid && q->getDna().notes == 16, "the DNA is saved with the project");
        p->forgetDna();
        check (! p->getDna().valid, "Forget clears it");

        // Groove: off 16ths move later, on-beats stay.
        std::vector<chords::Note> grid { { 60, 0.0, 0.2, 100 }, { 60, 0.25, 0.2, 100 }, { 60, 0.5, 0.2, 100 }, { 60, 0.75, 0.2, 100 } };
        const auto swung = chords::applyGroove (grid, Params::grooveSwing (1));
        check (std::abs (swung[0].start) < 1e-9 && std::abs (swung[2].start - 0.5) < 1e-9
                   && std::abs (swung[1].start - 0.2875) < 1e-6 && std::abs (swung[3].start - 0.7875) < 1e-6,
               "trance shuffle delays the off 16ths only");

        // Accent CC in exported MIDI.
        const auto midi = chords::toMidiFile (grid, 138.0, "Test", 74);
        int ccs = 0;
        for (auto* e : *midi.getTrack (0)) ccs += e->message.isControllerOfType (74);
        check (ccs == 4, "exports carry accents as CC 74 (" + juce::String (ccs) + " events)");
        int none = 0;
        const auto plain = chords::toMidiFile (grid, 138.0, "Test");
        for (auto* e : *plain.getTrack (0)) none += e->message.isController();
        check (none == 0, "... and none when it's off");
        dir.deleteRecursively();
    }

    section ("Song title and song sheet");
    {
        auto p = make();
        p->applyPreset (8);
        p->setSongInfo ("Sunrise", "Me");
        suite::SectionMap map;
        map.totalBars = 16; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 9, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        const auto parent = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceSheet");
        parent.deleteRecursively();
        const auto folder = p->exportEverything (parent);
        check (folder.getFileName().startsWith ("Me - Sunrise ("), "the song folder is named after the song: " + folder.getFileName());
        const auto sheet = folder.getChildFile ("Song sheet.txt").loadFileAsString();
        check (sheet.contains ("Sunrise") && sheet.contains ("Breakdown") && sheet.contains ("Drop"), "with a song sheet listing the structure");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getSongTitle() == "Sunrise", "the title is saved with the project");
        suite::SectionBus::withdraw();
        parent.deleteRecursively();
    }

    section ("Style log");
    {
        const auto log = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceStyleLog.txt");
        log.deleteFile();
        suite::StyleLog::setFileForTests (log);
        auto p = make();
        p->applyPreset (0);
        suite::SectionMap map;
        map.totalBars = 16; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 9, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        const auto parent = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceLogExport");
        parent.deleteRecursively();
        p->exportEverything (parent);
        const auto entries = suite::StyleLog::read();
        check (entries.size() == 1 && entries[0].kind == "song" && entries[0].get ("key") == "Am" && entries[0].get ("degrees") == "i-VI-III-VII"
                   && entries[0].get ("bars") == "16" && entries[0].get ("structure") == "Breakdown-Drop",
               "Export all logs the song for your style report (" + (entries.empty() ? juce::String ("nothing") : entries[0].get ("degrees")) + ")");
        parent.deleteRecursively();
        log.deleteFile();
        suite::StyleLog::setFileForTests (testStyleLog);
        suite::SectionBus::withdraw();
    }

    section ("Song draft (from Ouroboros)");
    {
        suite::FixBus::reset();
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceDrafts");
        dir.deleteRecursively();
        CorrespondenceProcessor::draftFolderOverride() = dir;
        auto p = make();
        p->applyPreset (0);
        suite::SectionMap map;
        map.totalBars = 16; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 9, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        p->exportDraftIfDue (true);
        check (! dir.exists(), "nothing is drafted until Ouroboros asks");
        suite::FixBus::send (suite::Fix::DraftSong);
        p->fixes->poll();
        p->exportDraftIfDue();
        check (! dir.exists(), "it waits a moment, for the new song to be set up");
        p->exportDraftIfDue (true);
        const auto mids = p->lastDraft.findChildFiles (juce::File::findFiles, false, "*.mid");
        check (p->lastDraft.isDirectory() && p->lastDraft.isAChildOf (dir) && mids.size() >= 10,
               "then the whole song's MIDI is written into Drafts (" + juce::String (mids.size()) + " files)");
        suite::SectionBus::withdraw();
        CorrespondenceProcessor::draftFolderOverride() = juce::File();
        dir.deleteRecursively();
    }

    section ("Spire: macro rides, layers, FL piano roll scripts");
    {
        suite::SectionMap map;   // Breakdown 1-8, Build 9-16, Drop 17-24, Drop 25-32
        map.totalBars = 32; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 9, suite::SectionType::Build };
        map.sections[2] = { 17, suite::SectionType::Drop };
        map.sections[3] = { 25, suite::SectionType::Drop };
        check (spire::macroValue (map, 0, 0.0) == 35 && spire::macroValue (map, 0, 32.0) == 40 && spire::macroValue (map, 0, 63.9) > 120
                   && spire::macroValue (map, 0, 64.0) == 127,
               "brightness: low in the breakdown, opening through the build, full in the drop");
        check (spire::macroValue (map, 2, 4.0) == 110 && spire::macroValue (map, 2, 70.0) == 30 && spire::macroValue (map, 1, 70.0) == 100
                   && spire::macroValue (map, 1, 100.0) == 127 && spire::macroValue (map, 0, 200.0) == -1,
               "space blooms in the breakdown; drive grows from the first drop to the second; nothing past the end");

        auto p = make();
        p->applyPreset (0);
        suite::SectionBus::publish (map);
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceSpire");
        dir.deleteRecursively();
        const auto lead = p->exportClip ("Song Lead", dir);
        juce::MidiFile file;
        {
            juce::FileInputStream in (lead);
            file.readFrom (in);
        }
        std::array<int, 4> ccCount {};
        int bright = -1, brightAt64 = -1;
        for (const auto* e : *file.getTrack (0))
            if (e->message.isController() && e->message.getControllerNumber() >= 20 && e->message.getControllerNumber() <= 23)
            {
                ++ccCount[(size_t) (e->message.getControllerNumber() - 20)];
                if (e->message.getControllerNumber() == 20 && e->message.getTimeStamp() <= 64.0 * file.getTimeFormat()) brightAt64 = e->message.getControllerValue();
                if (e->message.getControllerNumber() == 20) bright = e->message.getControllerValue();
            }
        check (ccCount[0] > 10 && ccCount[1] > 1 && ccCount[2] > 1 && ccCount[3] > 10 && brightAt64 == 127 && bright == 127,
               "the Song Lead clip carries the four macro rides on CC 20-23 (" + juce::String (ccCount[0]) + " brightness moves)");
        set (*p, Params::songMacros, 0.0f);
        const auto plain = p->exportClip ("Song Lead", dir.getChildFile ("plain"));
        juce::MidiFile plainFile;
        { juce::FileInputStream in (plain); plainFile.readFrom (in); }
        int plainCCs = 0;
        for (const auto* e : *plainFile.getTrack (0)) plainCCs += e->message.isController() && e->message.getControllerNumber() >= 20 && e->message.getControllerNumber() <= 23;
        check (plainCCs == 0, "and none when the rides are off");

        std::vector<std::pair<double, bool>> pedal;
        bool available = false;
        const auto leadNotes = p->clipNotes ("Song Lead", pedal, available);
        const auto octave = p->clipNotes ("Song Lead Octave", pedal, available);
        const auto pluck = p->clipNotes ("Song Lead Pluck", pedal, available);
        const auto sub = p->clipNotes ("Song Sub", pedal, available);
        bool octaveInDrops = ! octave.empty(), pluckShort = ! pluck.empty(), subLow = ! sub.empty();
        for (auto& n : octave) octaveInDrops = octaveInDrops && n.start >= 64.0 - 0.05;
        for (auto& n : pluck) pluckShort = pluckShort && n.length <= 0.25 + 1.0e-9 && n.start >= 32.0 - 0.05;
        for (auto& n : sub) subLow = subLow && n.pitch >= 24 && n.pitch <= 35 && n.start >= 32.0 - 0.05;
        check (octaveInDrops && pluckShort && subLow && octave.size() < leadNotes.size(),
               "Spire layers: the lead octave plays only in the drops, the pluck is short and starts at the build, the sub sits in C1-B1 and skips the breakdown");

        const auto scripts = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceScripts");
        scripts.deleteRecursively();
        spire::scriptFolderOverride() = scripts;
        const auto written = p->sendToPianoRoll();
        const auto chordsScript = scripts.getChildFile ("Antioch - Chords.pyscript").loadFileAsString();
        check (written.size() == 11 && chordsScript.contains ("import flpianoroll as flp") && chordsScript.contains ("def apply(form):")
                   && chordsScript.contains ("flp.score.addNote(n)"),
               "11 piano roll scripts (loop and song clips, and the layers) for FL's Tools > Scripting");
        int noteLines = 0;
        for (const auto& line : juce::StringArray::fromLines (chordsScript)) noteLines += line.trim().startsWithChar ('(');
        std::vector<std::pair<double, bool>> pedal2;
        check (noteLines == (int) p->clipNotes ("Chords", pedal2, available).size() && noteLines > 0,
               "with every note of the clip inside (" + juce::String (noteLines) + " for the chords)");
        if (outDir != juce::File()) scripts.getChildFile ("Antioch - Chords.pyscript").copyFileTo (outDir.getChildFile ("Antioch - Chords.pyscript"));
        spire::scriptFolderOverride() = juce::File();
        scripts.deleteRecursively();
        dir.deleteRecursively();
        suite::SectionBus::withdraw();
    }

    section ("Harmony explorer and variation engine");
    {
        auto s = aMinor();   // i VI III VII in A minor
        const auto ideas = harmony::ideas (s);
        juce::StringArray names;
        for (auto& i : ideas) names.add (i.name);
        check (names.contains ("End on a major V") && names.contains ("Picardy I") && names.contains ("Sus4 before the loop") && ! names.contains ("Borrowed iv"),
               "ideas for a minor progression: " + names.joinIntoString (", "));
        bool oneChange = true;
        for (auto& i : ideas)
            if (i.name == "Picardy I") oneChange = i.result.slots[0].quality == chords::Quality::Major && i.result.slots[1].degree == s.slots[1].degree;
        check (oneChange, "Picardy I turns only the home chord major");
        auto major = s; major.scale = 1; major.slots[1] = { 4 };
        juce::StringArray majorNames;
        for (auto& i : harmony::ideas (major)) majorNames.add (i.name);
        check (majorNames.contains ("Borrowed iv") && ! majorNames.contains ("Picardy I"), "a major one gets a borrowed iv instead");

        auto p = make();
        p->applyPreset (0);
        const auto before = p->currentSettings();
        int sus = -1;
        const auto list = p->harmonyIdeas();
        for (size_t i = 0; i < list.size(); ++i) if (list[i].name == "Sus4 before the loop") sus = (int) i;
        check (p->applyHarmonyIdea (sus) && p->currentSettings().slots[(size_t) before.count - 1].quality == chords::Quality::Sus4, "applying an idea changes the progression");
        check (p->undoHarmonyIdea() && p->currentSettings().slots[(size_t) before.count - 1].quality == before.slots[(size_t) before.count - 1].quality, "and Undo puts it back");

        const auto vars = p->sectionVariations (suite::SectionType::Drop);
        check (vars.size() == 4 && vars[0].name == "Re-rhythmed" && vars[1].name == "Thinned out" && vars[3].name == "New groove", "four variations for the drop");
        check (p->sectionVariations (suite::SectionType::Breakdown).back().name == "Re-harmonised", "and the breakdown can be re-harmonised");
        const auto dropChords = p->currentSongPatterns().chords[(size_t) suite::SectionType::Drop];
        check (p->applyVariation (suite::SectionType::Drop, 1) && p->currentSongPatterns().arp[(size_t) suite::SectionType::Drop] == 0
                   && p->currentSongPatterns().chords[(size_t) suite::SectionType::Drop] == 1,
               "thinned out: the drop's arp goes and its chords sustain");
        check (p->undoVariation() && p->currentSongPatterns().chords[(size_t) suite::SectionType::Drop] == dropChords, "and Undo brings the drop back");
    }

    section ("Writing tools: arps, basslines, breakdowns, hook variations");
    {
        auto s = aMinor();
        s.arpOctaves = 2;
        auto arpWith = [&] (chords::ArpPattern a) { auto t = s; t.arpPattern = a; return chords::arpClip (t); };
        const auto gated = arpWith (chords::ArpPattern::Gated), threeThree = arpWith (chords::ArpPattern::ThreeThreeTwo);
        const auto broken = arpWith (chords::ArpPattern::Broken), jumps = arpWith (chords::ArpPattern::OctaveJumps);
        check (gated.size() == 48 && threeThree.size() == 24, "gated arp: x x - x (48 notes); 3-3-2 arp: 3 hits per 2 beats (24)");
        check (broken[1].pitch > broken[2].pitch && broken[2].pitch > broken[0].pitch, "broken arp: 1-3-2-4");
        check (jumps[1].pitch == jumps[0].pitch + 12, "octave-jump arp: each note, then an octave up");
        auto bassWith = [&] (chords::BassPattern b) { auto t = s; t.bassPattern = b; return chords::bassClip (t); };
        const auto oct = bassWith (chords::BassPattern::OctaveOffbeat), pump = bassWith (chords::BassPattern::Pumping);
        check (oct.size() == 16 && oct[1].pitch == oct[0].pitch + 12 && std::abs (oct[0].start - 0.5) < 1.0e-9, "octave offbeat bass: offbeats, every second one an octave up");
        check (pump.size() == 16 && std::abs (pump[0].start - 0.25) < 1.0e-9 && pump[0].length > 0.6, "pumping bass: a 16th after each beat, held to the next");

        chords::MelodySettings ms;
        const auto mel = chords::melody (s, ms);
        const auto cycleQ = s.quartersPerChord * s.count;
        const auto hint = chords::hookHint (mel, cycleQ);
        bool slow = ! hint.empty();
        for (const auto& n : hint) slow = slow && n.start < cycleQ && std::any_of (mel.begin(), mel.end(), [&] (const chords::Note& m) { return std::abs (m.start * 2.0 - n.start) < 1.0e-9 && m.pitch == n.pitch; });
        check (slow, "hook hint: the melody's opening at half speed (" + juce::String ((int) hint.size()) + " notes)");

        const auto answer = chords::callResponse (mel, s);
        const auto half = cycleQ / 2.0;
        std::vector<double> callTimes, answerTimes;
        for (const auto& n : answer) (n.start < half ? callTimes : answerTimes).push_back (n.start < half ? n.start : n.start - half);
        const auto last = std::max_element (answer.begin(), answer.end(), [] (const chords::Note& a, const chords::Note& b) { return a.start < b.start; });
        const auto lastSlot = (int) std::floor (last->start / s.quartersPerChord);
        check (callTimes == answerTimes && ((last->pitch % 12) + 12) % 12 == chords::chordRootPitchClass (s, lastSlot),
               "call and response: the answer repeats the call's rhythm and ends on its chord's root");

        // Four drops: each later one varies the hook its own way
        suite::SectionMap map;
        map.totalBars = 28; map.count = 7;
        const int starts[] = { 1, 5, 9, 13, 17, 21, 25 };
        for (int i = 0; i < 7; ++i) map.sections[(size_t) i] = { starts[i], i % 2 == 0 ? suite::SectionType::Drop : suite::SectionType::Breakdown };
        chords::SongPatterns patterns;
        patterns.liftLaterDrops = false; patterns.doubleFinalDrop = false; patterns.phraseVariation = false;
        patterns.lead = { 0, 0, 0, 0, 1, 0 };
        const auto lead = chords::songClip (s, map, patterns, chords::SongPart::Lead);
        std::array<std::vector<std::pair<int, int>>, 4> drops;
        for (const auto& n : lead)
        {
            const auto section = (int) std::floor (n.start / 16.0 + 1.0e-9);
            if (section % 2 == 0) drops[(size_t) section / 2].push_back ({ (int) std::lround ((n.start - section * 16.0) * 4.0), n.pitch });
        }
        for (auto& d : drops) std::sort (d.begin(), d.end());
        if (std::getenv ("CDEBUG")) for (auto& d : drops) { for (auto& [t, pch] : d) std::cout << t << ":" << pch << " "; std::cout << std::endl; }
        check (drops[1] != drops[0] && drops[2] != drops[1] && drops[3] != drops[2] && drops[3] != drops[1],
               "four drops, four versions of the hook: as written, embellished, answered, pushed");

        const auto vars = harmony::variations (patterns, suite::SectionType::Breakdown);
        const auto writer = std::find_if (vars.begin(), vars.end(), [] (const harmony::Variation& v) { return v.name == "Breakdown writer"; });
        check (writer != vars.end() && writer->lead == 3 && writer->bass == 0 && writer->breakdownStart == 1, "the breakdown writer: held chords from VI, no bass, the hook hinted");
    }

    section ("Spire macro plans, the vocal answer, the clash check");
    {
        suite::SectionMap map;
        map.totalBars = 16; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 9, suite::SectionType::Drop };
        const auto uplifting = spire::macroValue (map, 2, 4.0, 4.0, 0), big = spire::macroValue (map, 2, 4.0, 4.0, 2);
        const auto drop = spire::macroValue (map, 0, 40.0, 4.0, 0), gentle = spire::macroValue (map, 0, 40.0, 4.0, 1);
        check (big == 127 && uplifting == 110 && gentle < drop && gentle > 64, "macro plans: big breakdowns open the space all the way; progressive moves less");
        const auto file = spire::macroFile (map, 20, 0, 138.0);
        int ccs = 0;
        for (const auto* e : *file.getTrack (0)) ccs += e->message.isController() ? 1 : 0;
        check (ccs > 8, "the macro plan as its own MIDI file: four CC lanes (" + juce::String (ccs) + " moves)");

        auto s = aMinor();
        std::vector<bool> vocal (16, false);
        for (int b : { 0, 1, 2, 8, 9 }) vocal[(size_t) b] = true;   // phrases in bars 1-3 and 9-10
        const auto answer = chords::vocalAnswer (s, map, vocal);
        bool inGaps = ! answer.empty();
        for (const auto& n : answer) inGaps = inGaps && ! vocal[(size_t) std::floor (n.start / 4.0)] && n.pitch >= 72;
        const auto firstBar = answer.empty() ? -1 : (int) std::floor (answer.front().start / 4.0);
        check (inGaps && firstBar == 3, "the vocal answer plays only in the gaps, from the bar after each phrase, above the vocal");

        chords::SongPatterns patterns;
        patterns.arp = { 0, 0, 0, 0, 1, 0 };
        patterns.lead = { 0, 0, 0, 0, 1, 0 };
        auto clashing = s;
        clashing.arpOctaves = 3;
        const auto found = clash::find (clashing, map, patterns, 5, 3, "mel_octave", "arpoct");
        check (! found.empty() && found[0].text.startsWith ("Drop") && found[0].paramId == "mel_octave" && std::abs (found[0].newValue - 6.0f) < 0.01f,
               "the clash check finds the lead on top of the arp in the drop, and offers the lead up an octave ("
                   + (found.empty() ? juce::String ("none") : found[0].text) + ")");
    }

    section ("Writing around the vocal; a layer for thin sections; the mix re-learns what changed");
    {
        auto s = aMinor();
        // The vocal holds C5 (72) for two bars; a written B4 (71) and C#5 would rub against it
        std::vector<suite::WritingBus::SungNote> sung { { 72, 0.0, 8.0 } };
        std::vector<chords::Note> line { { 71, 1.0, 0.5, 100 }, { 69, 2.0, 0.5, 100 }, { 71, 12.0, 0.5, 100 } };
        const auto moved = vocalaware::avoidVocal (line, sung, s);
        check (moved == 1 && line[0].pitch != 71 && ! vocalaware::rubs (line[0].pitch, 72) && line[1].pitch == 69 && line[2].pitch == 71,
               "a note rubbing against the vocal's held note steps off it (" + juce::String (line[0].pitch) + "); the others and the ones after it stay");

        suite::SectionMap map;
        map.totalBars = 16; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 9, suite::SectionType::Drop };
        const auto layer = vocalaware::layerClip (s, map, 1u << (int) suite::SectionType::Groove);
        bool inGroove = ! layer.empty();
        for (const auto& n : layer) inGroove = inGroove && n.start < 32.0 && n.pitch >= 55 && n.pitch <= 79;
        check (inGroove, "a thin groove gets a sustained chord layer (and only the groove)");

        auto p = make();
        set (*p, Params::libLead, 0.0f);   // the lead from Melodia, so a new seed is a new lead
        suite::SectionBus::publish (map);
        suite::FixBus::reset();
        const auto parent = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceRelearn");
        parent.deleteRecursively();
        p->exportEverything (parent);
        check (p->lastRelearnMask.load() == 0, "a first export has nothing to compare with");
        set (*p, Params::melodySeed, 123.0f);   // a new lead
        p->exportEverything (parent);
        suite::FixBus::Message m;
        check ((p->lastRelearnMask.load() & (1u << 5)) != 0 && (p->lastRelearnMask.load() & (1u << 2)) == 0 && suite::FixBus::read (suite::FixBus::latest(), m) && m.fix == suite::Fix::Relearn,
               "a new lead exported: the lead's tracks re-learn, the bass's don't");
        parent.deleteRecursively();
        suite::SectionBus::withdraw();
        suite::FixBus::reset();
    }

    section ("Song pack for a vocalist");
    {
        auto p = make();
        p->applyPreset (0);
        p->setSongInfo ("Sunrise", "Me");
        suite::SectionMap map;
        map.totalBars = 16; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 9, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        const auto parent = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondencePack");
        parent.deleteRecursively();
        const auto pack = p->exportSongPack (parent);
        const auto mids = pack.findChildFiles (juce::File::findFiles, false, "*.mid");
        const auto note = pack.getChildFile ("For the vocalist.txt").loadFileAsString();
        check (pack.getFileName() == "Sunrise - song pack" && mids.size() == 4 && note.contains ("Key:     A Minor") && note.contains ("Lead guide range")
                   && note.contains ("Breakdown") && note.contains ("Am - F - C - G"),
               "a folder with chords, bass, lead guide and section markers, and a note on key, chords, range and structure");
        parent.deleteRecursively();
        suite::SectionBus::withdraw();
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
        if (auto* ce = dynamic_cast<CorrespondenceEditor*> (editor.get()))
        {
            ce->showPage (1);
            snapshot (*editor, outDir.getChildFile ("correspondence_song.png"));
            ce->setSize (1500, 980);
            snapshot (*editor, outDir.getChildFile ("correspondence_song_large.png"));
            ce->showPage (0);
        }
        editor.reset();
    }

    section ("The pattern library");
    {
        using namespace patterns;
        const auto& lib = library();
        // Every family is well formed: whole bars, known characters, density rising with the energy
        bool wellFormed = true, rising = true;
        juce::String bad;
        const std::string known = "rtfsRTFSl94udknNaAbcCozpP~,-.!'^_";
        auto onsets = [&] (const std::string& t) { int n = 0; for (const auto& v : voices (t)) for (const auto& b : v.bars) for (auto c : b) n += (c != '.' && c != '-' && c != '~') ? 1 : 0; return n; };
        for (const auto& f : lib)
        {
            for (const auto& t : { f.level[0], f.level[1], f.level[2], f.fill, f.fill2, f.high2 })
            {
                for (auto v : juce::StringArray::fromTokens (juce::String (t), "+", ""))
                {
                    v = v.trimCharactersAtStart ("<>");
                    for (auto b : juce::StringArray::fromTokens (v, "/", ""))
                    {
                        const auto steps = b.removeCharacters ("!'^_").length();
                        if (steps != 16 && steps != 12 && steps != 24) { wellFormed = false; bad = juce::String (f.name) + " (" + b + ")"; }
                        for (auto c : b) if (known.find ((char) c) == std::string::npos) { wellFormed = false; bad = juce::String (f.name) + " ('" + juce::String::charToString (c) + "')"; }
                    }
                }
            }
            if (! (onsets (f.level[0]) <= onsets (f.level[1]) && onsets (f.level[1]) <= onsets (f.level[2]))) { rising = false; bad = juce::String (f.name); }
            if (! f.high2.empty() && onsets (f.high2) < onsets (f.level[1])) { rising = false; bad = juce::String (f.name) + " (high2)"; }
        }
        check (wellFormed, "every pattern is whole 16-step bars of known notation" + (wellFormed ? juce::String() : " (" + bad + ")"));
        check (rising, "every family gets busier from low to high energy" + (rising ? juce::String() : " (" + bad + ")"));
        check (lib.size() >= 60, juce::String ((int) lib.size()) + " families");

        // Every mood and style finds a family for every part and kind, and fits it where one exists
        bool covered = true;
        int exact = 0, pairs = 0;
        for (int m = 0; m < (int) Mood::count; ++m)
            for (int st = 0; st < (int) Style::count; ++st)
                for (auto [part, kinds] : { std::pair { Part::Bass, 3 }, { Part::Arp, 3 }, { Part::Chords, 3 }, { Part::Lead, 1 } })   // (the layers are checked below)
                    for (int k = 0; k < kinds; ++k)
                    {
                        const auto c = candidates (part, k, (Mood) m, (Style) st);
                        covered = covered && ! c.empty();
                        ++pairs;
                        if (! c.empty() && ((lib[(size_t) c[0]].styles >> st) & 1u) != 0) ++exact;
                    }
        check (covered, "every mood x style finds a family for every part");
        check (exact * 10 >= pairs * 7, "and most (" + juce::String (exact) + " of " + juce::String (pairs) + ") match the style exactly");

        // Rendering: chord-relative, in key, in range, one note at a time for bass and lead
        chords::Settings s;
        s.root = 9; s.scale = 0; s.count = 4;   // A minor: i VI III VII
        s.slots[0] = { 1 }; s.slots[1] = { 6 }; s.slots[2] = { 3 }; s.slots[3] = { 7 };
        const auto& keySteps = suite::music::scales()[0].steps;
        auto inKey = [&] (int p) { return std::find (keySteps.begin(), keySteps.end(), ((p - 9) % 12 + 12) % 12) != keySteps.end(); };
        bool allInKey = true, inRange = true, mono = true;
        juce::String where;
        for (const auto& f : lib)
            for (int lv = 0; lv < 3; ++lv)
            {
                const auto notes = chords::lib::render (f, lv, false, s, f.part == Part::Lead ? 32.0 : 16.0);
                for (size_t i = 0; i < notes.size(); ++i)
                {
                    const auto& n = notes[i];
                    if (! inKey (n.pitch) && f.level[(size_t) lv].find ('N') == std::string::npos && f.fill.find ('N') == std::string::npos) { allInKey = false; where = juce::String (f.name); }
                    const auto low = f.part == Part::Bass || f.part == Part::Acid || f.part == Part::Timpani;
                    const auto lo = low ? 21 : f.part == Part::Lead || f.part == Part::Chops || f.part == Part::Candy ? 55 : 33, hi = low ? (f.part == Part::Acid ? 76 : 64) : 108;
                    if (n.pitch < lo || n.pitch > hi || n.length <= 0.0) { inRange = false; where = juce::String (f.name) + " (" + juce::String (n.pitch) + ")"; }
                    if (chords::lib::monophonic (f.part) && i + 1 < notes.size() && n.start + n.length > notes[i + 1].start + 1.0e-6) { mono = false; where = juce::String (f.name); }
                }
            }
        check (allInKey, "every family stays in the key over i-VI-III-VII" + (allInKey ? juce::String() : " (" + where + ")"));
        check (inRange, "bass, arps, chords and leads each stay in their register" + (inRange ? juce::String() : " (" + where + ")"));
        check (mono, "bass lines and hooks play one note at a time" + (mono ? juce::String() : " (" + where + ")"));

        auto findFamily = [&] (const char* name) -> const Family& { for (const auto& f : lib) if (f.name == name) return f; return lib.front(); };
        const auto offbeat = chords::lib::render (findFamily ("Classic offbeat"), 2, false, s, 16.0);
        bool offbeats = offbeat.size() == 16;
        for (const auto& n : offbeat) offbeats = offbeats && std::abs (std::fmod (n.start, 1.0) - 0.5) < 1.0e-6;
        check (offbeats && offbeat[0].pitch % 12 == 9 && offbeat[4].pitch % 12 == 5, "the classic offbeat bass: every offbeat, on each chord's root (A, then F)");
        const auto hook = chords::lib::render (findFamily ("Anthem long notes"), 1, false, s, 32.0);
        int maxLeap = 0;
        for (size_t i = 1; i < hook.size(); ++i) maxLeap = juce::jmax (maxLeap, std::abs (hook[i].pitch - hook[i - 1].pitch));
        check (! hook.empty() && maxLeap <= 9, "a hook voice-leads over the changes (largest leap " + juce::String (maxLeap) + " semitones)");
        bool answered = false;
        for (const auto& n : hook) answered = answered || (n.start >= 24.0 && n.start < 32.0);
        check (answered, "and answers itself in the last two bars of the phrase");

        // In the song: one family per part, busier in the drop than the intro, fills at phrase ends
        suite::SectionMap map;
        map.totalBars = 48; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Intro };
        map.sections[1] = { 17, suite::SectionType::Build };
        map.sections[2] = { 25, suite::SectionType::Drop };
        map.sections[3] = { 41, suite::SectionType::Outro };
        chords::SongPatterns sp;
        sp.library = true; sp.libMood = (int) Mood::Euphoric; sp.libStyle = (int) Style::Uplifting; sp.libSeed = 42;
        sp.bass = { 2, 2, 2, 2, 2, 2 };   // rolling
        sp.ghostNotes = 0.0f;
        const auto bass = chords::songClip (s, map, sp, chords::SongPart::Bass);
        auto countIn = [&] (const std::vector<chords::Note>& v, double a, double b) { int n = 0; for (const auto& x : v) n += x.start >= a && x.start < b ? 1 : 0; return n; };
        check (countIn (bass, 96.0, 112.0) > countIn (bass, 0.0, 16.0), "the drop's bass is busier than the intro's (" + juce::String (countIn (bass, 96.0, 112.0)) + " vs "
                   + juce::String (countIn (bass, 0.0, 16.0)) + " notes in 4 bars)");
        check (countIn (bass, 92.0, 96.0) >= countIn (bass, 64.0, 68.0), "the build climbs: its last bars at least as busy as its first");
        const auto plain = [&] { auto q = sp; q.library = false; return chords::songClip (s, map, q, chords::SongPart::Bass); }();
        check (bass.size() != plain.size() || bass[0].start != plain[0].start || bass[0].pitch != plain[0].pitch, "(the library writes it, not the old patterns)");
        auto other = sp;
        other.libSeed = 43;
        other.libStyle = (int) Style::Tech; other.libMood = (int) Mood::Dark;
        const auto techBass = chords::songClip (s, map, other, chords::SongPart::Bass);
        bool differs = techBass.size() != bass.size();
        for (size_t i = 0; ! differs && i < bass.size(); ++i) differs = bass[i].start != techBass[i].start || bass[i].pitch != techBass[i].pitch;
        check (differs, "a dark tech song gets a different bass from a euphoric uplifting one");
        const auto again = chords::songClip (s, map, sp, chords::SongPart::Bass);
        check (again.size() == bass.size(), "the same seed always writes the same song");
        sp.lead = { 0, 0, 0, 0, 1, 0 };   // the drop
        const auto lead = chords::songClip (s, map, sp, chords::SongPart::Lead);
        {
            const auto counterLine = chords::songClip (s, map, sp, chords::SongPart::Counter);
            int together = 0;
            for (const auto& c : counterLine) for (const auto& l : lead) together += std::abs (c.start - l.start) < 1.0e-6 ? 1 : 0;
            check (! counterLine.empty() && together == 0, "the counter-line answers the hook: it never starts a note with it (" + juce::String ((int) counterLine.size()) + " notes, " + juce::String (together) + " together)");
        }
        check (countIn (lead, 96.0, 160.0) > 0 && countIn (lead, 0.0, 96.0) == 0, "the hook plays in the drop");

        // Variety: families with a second high and a second fill play them in every other phrase of a drop
        {
            int withVariants = 0;
            for (const auto& f : lib) withVariants += ! f.fill2.empty() || ! f.high2.empty() ? 1 : 0;
            check (withVariants >= 40, juce::String (withVariants) + " families carry a second fill or a second high energy");
            suite::SectionMap longDrop;
            longDrop.totalBars = 32; longDrop.count = 1;
            longDrop.sections[0] = { 1, suite::SectionType::Drop };
            bool alternates = false;
            for (uint32_t seed = 1; seed < 40 && ! alternates; ++seed)
            {
                auto vp = sp; vp.libSeed = seed;
                const auto& fam = pick (Part::Bass, 2, Mood::Euphoric, Style::Uplifting, seed);
                if (fam.high2.empty()) continue;
                const auto drop = chords::songClip (s, longDrop, vp, chords::SongPart::Bass);
                std::vector<std::pair<double, int>> first, second;
                for (const auto& n : drop)
                {
                    if (n.start < 16.0) first.push_back ({ n.start, n.pitch });
                    if (n.start >= 32.0 && n.start < 48.0) second.push_back ({ n.start - 32.0, n.pitch });
                }
                alternates = first != second;
            }
            check (alternates, "a 32-bar drop: its second phrase plays the second high energy, not a copy of the first");
        }

        // The notation: triplets, the family's own shuffle, accents, pushes, chord colours
        {
            auto make = [] (Part part, const std::string& high)
            {
                Family f;
                f.name = "test"; f.part = part; f.level = { high, high, high }; f.fill = high; f.gate = 0.5f;
                return f;
            };
            const auto triplets = chords::lib::render (make (Part::Bass, "rrr rrr rrr rrr"), 2, false, s, 4.0);
            check (triplets.size() == 12 && std::abs (triplets[1].start - 1.0 / 3.0) < 1.0e-6 && std::abs (triplets[3].start - 1.0) < 1.0e-6,
                   "a 12-step bar plays 8th-note triplets");
            auto swung = make (Part::Bass, "rrrr rrrr rrrr rrrr");
            swung.swing = 0.33f;
            const auto sw = chords::lib::render (swung, 2, false, s, 4.0);
            check (sw.size() == 16 && std::abs (sw[1].start - (0.25 + 0.33 * 0.25)) < 1.0e-4 && std::abs (sw[2].start - 0.5) < 1.0e-6,
                   "a family's own shuffle delays the off 16ths only");
            const auto acc = chords::lib::render (make (Part::Bass, "!r.r. 'r.r. r... ...."), 2, false, s, 4.0);
            check (acc.size() == 5 && acc[0].velocity > acc[1].velocity && acc[2].velocity == 60, "written accents and soft notes");
            const auto push = chords::lib::render (make (Part::Chords, "c--- ---- ---- ---p"), 2, false, s, 8.0);
            const auto voicedProg = chords::voiceProgression (s);
            std::vector<int> pushed;
            for (const auto& n : push) if (std::abs (n.start - 3.75) < 1.0e-6) pushed.push_back (n.pitch);
            std::sort (pushed.begin(), pushed.end());
            auto second = voicedProg[1];
            std::sort (second.begin(), second.end());
            check (pushed == second, "'p' plays the next chord a 16th early (the push)");
            auto coloured = make (Part::Chords, "c... .... .... ....");
            coloured.add7 = true;
            const auto seventh = chords::lib::render (coloured, 2, false, s, 4.0);   // Am7: G in the chord
            check (seventh.size() == voicedProg[0].size() + 1 && std::any_of (seventh.begin(), seventh.end(), [] (const chords::Note& n) { return n.pitch % 12 == 7; }),
                   "a 7th colour adds the chord's 7th (G over A minor)");
            auto inverted = make (Part::Chords, "c... .... .... ....");
            inverted.inversion = 1;
            const auto inv = chords::lib::render (inverted, 2, false, s, 4.0);
            int lowPlain = 200, lowInv = 200;
            for (auto pch : voicedProg[0]) lowPlain = std::min (lowPlain, pch);
            for (const auto& n : inv) lowInv = std::min (lowInv, n.pitch);
            check (lowInv > lowPlain, "an inversion lifts the lowest note an octave");

            // Long hooks: an 8-bar melody plays through (no answer in bars 7-8)
        {
            Family longHook;
            longHook.name = "long"; longHook.part = Part::Lead; longHook.gate = 0.9f;
            const std::string eight = "r--- ---- ---- ----/t--- ---- ---- ----/f--- ---- ---- ----/R--- ---- ---- ----/f--- ---- ---- ----/t--- ---- ---- ----/u--- ---- ---- ----/r--- ---- ---- ----";
            longHook.level = { eight, eight, eight };
            longHook.fill = "r... r... r... r...";
            const auto notes = chords::lib::render (longHook, 1, false, s, 32.0);
            check (notes.size() == 8, "an 8-bar hook plays its 8 bars through (" + juce::String ((int) notes.size()) + " notes, none from the answer)");
        }
        // The fit check: an offbeat bass and an offbeat arp land together; a 16th arp around them doesn't
            const auto bassF = make (Part::Bass, "..r. ..r. ..r. ..r.");
            check (overlap (make (Part::Arp, "..a. ..a. ..a. ..a."), bassF) > 0.99f && overlap (make (Part::Arp, "a... a... a... a..."), bassF) < 0.01f,
                   "interlocking rhythms score 0, unison 1");
            // Section figures: a build-only family plays in the build and nowhere else
            int buildOnly = 0, misplaced = 0;
            for (const auto& f : lib) if (f.where != 0) ++buildOnly;
            for (uint32_t seed = 1; seed < 20; ++seed)
                for (int k = 0; k < 3; ++k)
                    misplaced += pickFitted (Part::Arp, k, Mood::Euphoric, Style::Uplifting, seed, {}).where != 0 ? 1 : 0;
            check (buildOnly >= 10 && misplaced == 0, juce::String (buildOnly) + " section figures, never picked for the whole song");
            bool buildFigure = false;
            for (uint32_t seed = 1; seed < 20 && ! buildFigure; ++seed)
                for (int k = 0; k < 3 && ! buildFigure; ++k)
                    buildFigure = (pickFitted (Part::Arp, k, Mood::Euphoric, Style::Uplifting, seed, {}, (int) suite::SectionType::Build).where
                                   & (1u << (int) suite::SectionType::Build)) != 0;
            check (buildFigure, "a build gets a build figure where the library has one");
            // In a song, the arp is chosen to interlock with the bass
            float fitted = 0.0f, random = 0.0f;
            for (uint32_t seed = 1; seed <= 30; ++seed)
            {
                auto fp = sp; fp.libSeed = seed; fp.bass = { 1, 1, 1, 1, 1, 1 }; fp.libKits = false;   // offbeat basses, no kit
                const auto& b = chords::songFamily (chords::SongPart::Bass, 1, fp, -1);
                fitted += overlap (chords::songFamily (chords::SongPart::Arp, 1, fp, -1), b);
                // a blind pick among the arps in the bass's feel (the feel lock comes first): their average
                float sum = 0.0f; int n = 0;
                const auto hookChar = chords::songFamily (chords::SongPart::Lead, 0, fp, -1).character;
                std::vector<int> pool;
                for (auto i : candidates (Part::Arp, 1, Mood::Euphoric, Style::Uplifting)) if (feelOf (lib[(size_t) i]) == feelOf (b)) pool.push_back (i);
                std::vector<int> sameChar;
                for (auto i : pool) if ((lib[(size_t) i].character & hookChar) != 0) sameChar.push_back (i);
                if (! sameChar.empty()) pool = sameChar;
                for (auto i : pool) { sum += overlap (lib[(size_t) i], b); ++n; }
                random += n > 0 ? sum / (float) n : 0.0f;
            }
            check (fitted <= random, "the song's arp interlocks with its bass at least as well as a blind pick (" + juce::String (fitted / 30.0f, 2) + " vs "
                                         + juce::String (random / 30.0f, 2) + " overlap)");
        }

        // Kits: families chosen to go together
        {
            bool resolved = true;
            juce::String missing;
            std::array<int, (size_t) Style::count> perStyle {};
            for (const auto& k : kits())
            {
                for (int st = 0; st < (int) Style::count; ++st) perStyle[(size_t) st] += (k.styles >> st) & 1u;
                for (size_t role = 0; role < k.parts.size(); ++role)
                    for (const auto& n : k.parts[role])
                        if (std::none_of (lib.begin(), lib.end(), [&] (const Family& f) { return f.name == n && (size_t) f.part == role; }))
                        { resolved = false; missing = juce::String (k.name) + ": " + n; }
            }
            check (kits().size() >= 40 && resolved, juce::String ((int) kits().size()) + " kits, every family they name is in the library" + (resolved ? juce::String() : " (" + missing + ")"));
            check (*std::min_element (perStyle.begin(), perStyle.end()) >= 4, "every style has at least four kits");
            int pairs = 0;
            for (int m = 0; m < (int) Mood::count; ++m)
                for (int st = 0; st < (int) Style::count; ++st)
                    pairs += std::any_of (kits().begin(), kits().end(), [&] (const Kit& k) { return ((k.moods >> m) & 1u) != 0 && ((k.styles >> st) & 1u) != 0; }) ? 1 : 0;
            check (pairs == 49, "every mood x style has a kit (" + juce::String (pairs) + "/49)");
            auto kp = sp;
            kp.libStyle = (int) Style::Tech; kp.libMood = (int) Mood::Dark; kp.libKits = true;
            int fromKit = 0;
            for (uint32_t seed = 1; seed <= 20; ++seed)
            {
                kp.libSeed = seed;
                const auto* kit = pickKit (Mood::Dark, Style::Tech, seed);
                const auto& bass = chords::songFamily (chords::SongPart::Bass, 2, kp, -1);
                if (kit != nullptr && std::find (kit->parts[(size_t) Part::Bass].begin(), kit->parts[(size_t) Part::Bass].end(), bass.name) != kit->parts[(size_t) Part::Bass].end()) ++fromKit;
            }
            check (fromKit == 20, "a dark tech song plays its kit's bass (" + juce::String (fromKit) + " of 20 seeds)");

            // Without a kit: the parts keep the bass's feel and pushing habit where the library allows
            auto rp = sp; rp.libKits = false;
            int feelChecks = 0, feelKept = 0, pushChecks = 0, pushKept = 0;
            for (int st = 0; st < (int) Style::count; ++st)
                for (uint32_t seed = 1; seed <= 25; ++seed)
                {
                    rp.libStyle = st; rp.libSeed = seed;
                    const auto& b = chords::songFamily (chords::SongPart::Bass, 2, rp, -1);
                    const auto& a = chords::songFamily (chords::SongPart::Arp, 1, rp, -1);
                    const auto& c = chords::songFamily (chords::SongPart::Chords, 2, rp, -1);
                    const auto arpOptions = candidates (Part::Arp, 1, (Mood) rp.libMood, (Style) st);
                    if (std::any_of (arpOptions.begin(), arpOptions.end(), [&] (int i) { return feelOf (lib[(size_t) i]) == feelOf (b); }))
                    { ++feelChecks; feelKept += feelOf (a) == feelOf (b) ? 1 : 0; }
                    const auto chordOptions = candidates (Part::Chords, 2, (Mood) rp.libMood, (Style) st);
                    if (std::any_of (chordOptions.begin(), chordOptions.end(), [&] (int i) { return pushes (lib[(size_t) i]) == pushes (b); }))
                    { ++pushChecks; pushKept += pushes (c) == pushes (b) ? 1 : 0; }
                }
            check (feelChecks > 0 && feelKept == feelChecks, "the arp keeps the bass's feel (straight, shuffled, triplets) wherever it can (" + juce::String (feelKept) + "/" + juce::String (feelChecks) + ")");
            check (pushChecks > 0 && pushKept == pushChecks, "chords push into the next chord when the bass does, and not when it doesn't (" + juce::String (pushKept) + "/" + juce::String (pushChecks) + ")");

            // Character: every family has one or two, and without a kit the parts share the hook's
            int tagged = 0;
            for (const auto& f : lib) tagged += f.character != 0 ? 1 : 0;
            check (tagged * 100 >= (int) lib.size() * 99, juce::String (tagged) + " of " + juce::String ((int) lib.size()) + " families have a character");
            int charChecks = 0, charKept = 0;
            for (int st = 0; st < (int) Style::count; ++st)
                for (uint32_t seed = 1; seed <= 20; ++seed)
                {
                    rp.libStyle = st; rp.libSeed = seed;
                    const auto hookChar = chords::songFamily (chords::SongPart::Lead, 0, rp, -1).character;
                    const auto& c = chords::songFamily (chords::SongPart::Chords, 1, rp, -1);
                    // only where a chord family fits the bass's feel and pushing habit and the hook's character (they come first)
                    const auto& bassRef = chords::songFamily (chords::SongPart::Bass, chords::lib::kindFor (chords::SongPart::Bass, rp.bass[(size_t) suite::SectionType::Drop], suite::SectionType::Drop), rp, -1);
                    const auto options = candidates (Part::Chords, 1, (Mood) rp.libMood, (Style) st);
                    auto fits = [&] (const Family& f) { return feelOf (f) == feelOf (bassRef) && pushes (f) == pushes (bassRef); };
                    if (! std::any_of (options.begin(), options.end(), [&] (int i) { return fits (lib[(size_t) i]); })) continue;
                    if (hookChar != 0 && std::any_of (options.begin(), options.end(), [&] (int i) { return fits (lib[(size_t) i]) && (lib[(size_t) i].character & hookChar) != 0; }))
                    { ++charChecks; charKept += (c.character & hookChar) != 0 ? 1 : 0; }
                }
            check (charChecks > 0 && charKept * 10 >= charChecks * 9, "the chords share the hook's character (" + juce::String (charKept) + "/" + juce::String (charChecks) + ")");

            // Register: the arp stays clear of the hook
            int clear = 0, songs = 0;
            auto mp = sp; mp.arp = { 2, 2, 2, 2, 2, 2 }; mp.lead = { 0, 0, 0, 0, 1, 0 };
            for (uint32_t seed = 1; seed <= 12; ++seed)
            {
                mp.libSeed = seed;
                auto median = [] (std::vector<chords::Note> v, double a, double b)
                {
                    std::vector<int> p;
                    for (const auto& n : v) if (n.start >= a && n.start < b) p.push_back (n.pitch);
                    std::sort (p.begin(), p.end());
                    return p.empty() ? 0 : p[p.size() / 2];
                };
                const auto arp = median (chords::songClip (s, map, mp, chords::SongPart::Arp), 96.0, 160.0);
                const auto hook = median (chords::songClip (s, map, mp, chords::SongPart::Lead), 96.0, 160.0);
                if (arp == 0 || hook == 0) continue;
                ++songs; clear += std::abs (arp - hook) >= 5 ? 1 : 0;
            }
            check (songs > 0 && clear == songs, "the arp sits at least 5 semitones from the hook (" + juce::String (clear) + "/" + juce::String (songs) + " songs)");
        }

        // The layers: piano and orchestra in breakdowns and the last drop, timpani rolling into it, acid where the style uses it
        {
            suite::SectionMap song;
            song.totalBars = 64; song.count = 5;
            song.sections[0] = { 1, suite::SectionType::Intro };
            song.sections[1] = { 9, suite::SectionType::Breakdown };
            song.sections[2] = { 25, suite::SectionType::Build };
            song.sections[3] = { 33, suite::SectionType::Drop };
            song.sections[4] = { 49, suite::SectionType::Outro };
            auto lp = sp;
            lp.libStyle = (int) Style::Uplifting;
            auto in = [&] (chords::SongPart part, double a, double b) { return countIn (chords::songClip (s, song, lp, part), a, b); };
            check (in (chords::SongPart::Piano, 32.0, 96.0) > 0 && in (chords::SongPart::Strings, 32.0, 96.0) > 0 && in (chords::SongPart::Choir, 32.0, 96.0) > 0,
                   "the breakdown gets piano, strings and choir");
            check (in (chords::SongPart::Brass, 128.0, 192.0) > 0 && in (chords::SongPart::Brass, 0.0, 128.0) == 0, "brass waits for the (last) drop");
            check (in (chords::SongPart::Timpani, 96.0, 128.0) > 0, "timpani roll through the build");
            check (in (chords::SongPart::Piano, 0.0, 32.0) > 0, "broken piano in the intro");
            auto tech = lp; tech.libStyle = (int) Style::Tech;
            auto vocal = lp; vocal.libStyle = (int) Style::Vocal;
            check (countIn (chords::songClip (s, song, tech, chords::SongPart::Acid), 128.0, 192.0) > 0 && chords::songClip (s, song, vocal, chords::SongPart::Acid).empty(),
                   "an acid line in a tech song's drop, none in a vocal song");
            const auto chant = chords::songClip (s, song, tech, chords::SongPart::Choir);
            const auto pad = chords::songClip (s, song, lp, chords::SongPart::Choir);
            auto longest = [] (const std::vector<chords::Note>& v, double a) { double m = 0.0; for (const auto& n : v) if (n.start >= a) m = std::max (m, n.length); return m; };
            check (! chant.empty() && longest (chant, 128.0) < longest (pad, 128.0), "the last drop's choir chants in a tech song, holds a pad in an uplifting one");
            auto off = lp; off.libLayers &= ~1u;   // piano off
            check (chords::songClip (s, song, off, chords::SongPart::Piano).empty(), "each layer can be switched off");
            off.library = false;
            check (chords::songClip (s, song, off, chords::SongPart::Strings).empty(), "and none without the library");
        }

        // A new song from Ouroboros sets the library's style and mood
        auto p = make();
        suite::SessionStyle tech = suite::sessionStyles()[2];
        check (CorrespondenceProcessor::libraryStyleFor (tech) == (int) Style::Tech && CorrespondenceProcessor::libraryMoodFor (0, tech) == (int) Mood::Driving
                   && CorrespondenceProcessor::libraryMoodFor (4, tech) == (int) Mood::Dark,
               "a tech trance session: tech patterns, driving (or dark, if the mood says so)");
    }


    section ("Autopilot (arrangement and stability): density, turn-taking, low bass");
    {
        using patterns::Mood; using patterns::Style;
        chords::Settings s;
        suite::SectionMap map;
        map.totalBars = 64; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 17, suite::SectionType::Breakdown };
        map.sections[2] = { 33, suite::SectionType::Drop };
        map.sections[3] = { 49, suite::SectionType::Drop };
        auto layersIn = [&] (const chords::SongPatterns& sp, double a, double b)
        {
            int n = 0;
            for (int p = (int) chords::SongPart::Piano; p <= (int) chords::SongPart::Candy; ++p)
            {
                const auto notes = chords::songClip (s, map, sp, (chords::SongPart) p);
                n += std::any_of (notes.begin(), notes.end(), [a, b] (const chords::Note& x) { return x.start >= a && x.start < b; }) ? 1 : 0;
            }
            return n;
        };
        chords::SongPatterns sp;
        sp.library = true; sp.libMood = (int) Mood::Euphoric; sp.libStyle = (int) Style::Orchestral; sp.libSeed = 7;
        auto off = sp;
        off.manageDensity = off.takeTurns = false;
        const auto managed = layersIn (sp, 192.0, 256.0), all = layersIn (off, 192.0, 256.0);
        check (all > managed && managed >= 1, "the final drop of an orchestral song thins its layers (" + juce::String (all) + " -> " + juce::String (managed) + ")");
        check (chords::thinnedLayers (map, sp, 3) != 0 && chords::thinnedLayers (map, off, 3) == 0, "and only with the manager on");
        const auto piano = chords::songClip (s, map, sp, chords::SongPart::Piano);
        check (std::any_of (piano.begin(), piano.end(), [] (const chords::Note& x) { return x.start >= 64.0 && x.start < 128.0; }), "the breakdown keeps its piano");
        int thinned = 0, turned = 0;
        for (int p = (int) chords::SongPart::Piano; p <= (int) chords::SongPart::Candy; ++p) chords::songClip (s, map, sp, (chords::SongPart) p, &thinned, &turned);
        check (thinned > 0, juce::String (thinned) + " layer-sections thinned, counted for the health readout");

        // Turn-taking: in a style that uses chops or candy, they never start while the hook is moving
        bool found = false;
        for (int st = 0; st < 7 && ! found; ++st)
            for (const auto part : { chords::SongPart::Chops, chords::SongPart::Candy })
            {
                auto q = sp; q.libStyle = st; q.lead = { 0, 0, 0, 0, 1, 0 };
                auto qo = q; qo.takeTurns = false;
                const auto hook = chords::songClip (s, map, q, chords::SongPart::Lead);
                const auto with = chords::songClip (s, map, q, part), without = chords::songClip (s, map, qo, part);
                if (without.empty() || hook.empty()) continue;
                int clashes = 0;
                for (const auto& n : with)
                    for (const auto& h : hook) clashes += h.start >= n.start - 0.5 - 1.0e-6 && h.start <= n.start + 0.25 + 1.0e-6 ? 1 : 0;
                check (clashes == 0 && with.size() < without.size(), juce::String (part == chords::SongPart::Chops ? "chops" : "candy") + " step back while the hook moves ("
                           + juce::String ((int) without.size()) + " -> " + juce::String ((int) with.size()) + " notes)");
                found = true;
                break;
            }
        check (found, "(a style with chops or candy was found)");

        std::vector<chords::Note> low { { 21, 0.0, 1.0, 100 }, { 33, 1.0, 1.0, 100 }, { 24, 2.0, 1.0, 100 } };
        check (chords::foldLowBass (low) == 1 && low[0].pitch == 33 && low[1].pitch == 33 && low[2].pitch == 24, "a bass note under C1 folds up an octave; the rest stay");
    }


    section ("Round B: notes ringing over a chord change");
    {
        chords::Settings s;   // A minor, Am F C G, a bar each
        s.slots[0].degree = 1; s.slots[1].degree = 6; s.slots[2].degree = 3; s.slots[3].degree = 7;
        suite::SectionMap map;
        map.totalBars = 8; map.count = 1;
        map.sections[0] = { 1, suite::SectionType::Drop };
        // A held E (in Am, not in F) across the change to F; a held C (in both Am and F) too
        std::vector<chords::Note> notes { { 76, 2.0, 4.0, 100 }, { 72, 2.0, 4.0, 100 } };
        const auto t = chords::trimChordHangovers (notes, s, map);
        check (t == 1 && std::abs (notes[0].start + notes[0].length - 4.0) < 0.02 && std::abs (notes[1].length - 4.0) < 1.0e-6,
               "a held E ending into F is trimmed at the change; a C that fits F rings on");
    }


    section ("Writing: phrases, development, harmony, feel, parts, arrangement, learning, the library's tools");
    {
        using chords::Note;
        namespace W = chords::writing;
        const auto prefs = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceTestsWritingPrefs.txt");
        prefs.deleteFile();
        patterns::setLearnedFileForTests (prefs);
        chords::Settings s;   // A minor: Am F C G
        s.slots[0].degree = 1; s.slots[1].degree = 6; s.slots[2].degree = 3; s.slots[3].degree = 7;

        // 51: the validator
        const auto problems = patterns::validate();
        check (problems.isEmpty(), "every family and kit passes the validator" + (problems.isEmpty() ? juce::String() : ": " + problems.joinIntoString ("; ").substring (0, 400)));
        // 32, 54: the new polymetric arps and the articulation marks
        const patterns::Family* three = nullptr;
        for (const auto& f : patterns::library()) if (f.name == "Three over four") three = &f;
        check (three != nullptr && patterns::voices (three->level[2]).front().bars.size() == 3, "polymetric arps: three over four, realigning every 3 bars");
        {
            const auto v = patterns::voices ("!a^a_a.");
            check (v.front().artic.front()[1] == '^' && v.front().artic.front()[2] == '_' && v.front().marks.front()[0] == '!', "'^' (staccato) and '_' (legato) are read per step");
            patterns::Family f; f.part = patterns::Part::Arp; f.gate = 0.9f;
            f.level = { "^a.a.a.a.a.a.a.a.", "^a.a.a.a.a.a.a.a.", "^a.a.a.a.a.a.a.a." };
            const auto n = chords::lib::render (f, 2, false, s, 4.0);
            check (! n.empty() && n.front().length <= 0.125 && n[1].length > 0.3, "a staccato step plays short, the next as gated");
        }
        // 23, 53: quality and coverage
        float worst = 1.0f; int scored = 0;
        for (const auto& f : patterns::library()) { worst = std::min (worst, patterns::quality (f)); ++scored; }
        const auto report = patterns::coverageReport();
        check (scored > 100 && report.contains ("Near-duplicates") && report.contains ("Weakest"), "every family has a quality score (lowest " + juce::String (worst, 2) + "); the coverage report lists gaps, near-duplicates and the weakest");
        // 10: interlock
        {
            patterns::Family a, b, c;
            a.name = "a"; b.name = "b"; c.name = "c";
            a.level[2] = "r...r...r...r..."; b.level[2] = "..r...r...r...r."; c.level[2] = "r...r...r...r...";
            check (patterns::interlock ({ &a, &b }) > patterns::interlock ({ &a, &c }), "parts that fill each other's gaps score as interlocking better than parts in unison");
        }
        // 18, 22, 45: kit fields
        const patterns::Kit* sunrise = nullptr;
        for (const auto& k : patterns::kits()) if (k.name == "Sunrise anthem") sunrise = &k;
        check (sunrise != nullptr && sunrise->enterBar[(size_t) patterns::Part::Strings] == 9 && sunrise->fromDrop[(size_t) patterns::Part::Choir] == 2
                   && sunrise->breakdownVoice == (int) patterns::Part::Piano && ((sunrise->extras >> (int) patterns::Part::Candy) & 1u) != 0,
               "kits say when layers enter, which are extra, and who sings the breakdown");
        // 25: favourites and bans
        {
            const auto* first = patterns::pickKit (patterns::Mood::Euphoric, patterns::Style::Uplifting, 5);
            check (first != nullptr, "(a kit for euphoric uplifting)");
            if (first != nullptr)
            {
                patterns::setFavourite (patterns::kitKey (*first), -1);
                bool never = true;
                for (uint32_t seed = 1; seed < 200; ++seed) if (const auto* k = patterns::pickKit (patterns::Mood::Euphoric, patterns::Style::Uplifting, seed); k != nullptr && k->name == first->name) never = false;
                check (never, "a banned kit never comes up");
                patterns::setFavourite (patterns::kitKey (*first), 1);
                int hits = 0;
                for (uint32_t seed = 1; seed < 200; ++seed) if (const auto* k = patterns::pickKit (patterns::Mood::Euphoric, patterns::Style::Uplifting, seed); k != nullptr && k->name == first->name) ++hits;
                patterns::setFavourite (patterns::kitKey (*first), 0);
                int plain = 0;
                for (uint32_t seed = 1; seed < 200; ++seed) if (const auto* k = patterns::pickKit (patterns::Mood::Euphoric, patterns::Style::Uplifting, seed); k != nullptr && k->name == first->name) ++plain;
                check (hits > plain, "a favourite kit comes up more often (" + juce::String (plain) + " -> " + juce::String (hits) + " in 199 songs)");
            }
        }
        // 55: the kit generator
        {
            const auto made = patterns::generateKits (patterns::Mood::Euphoric, patterns::Style::Uplifting, 3, 42u);
            std::vector<patterns::Kit> back;
            if (! made.empty()) patterns::detail::parseKits (patterns::kitLine (made.front()), back, true);
            check (! made.empty() && back.size() == 1 && back.front().parts == made.front().parts, "the kit generator proposes kits, written as kits.txt lines that read back the same");
        }
        // A: the phrase arc and its parts
        {
            const auto& hookFam = patterns::pick (patterns::Part::Lead, 0, patterns::Mood::Euphoric, patterns::Style::Uplifting, 3);
            const auto hook = chords::lib::render (hookFam, 2, false, s, 32.0);
            const auto arc = W::phraseArc (hook, s, 32.0, 1u);
            auto median = [] (const std::vector<Note>& v, double a, double b) { std::vector<int> p; for (const auto& n : v) if (n.start >= a && n.start < b) p.push_back (n.pitch); std::sort (p.begin(), p.end()); return p.empty() ? 0 : p[p.size() / 2]; };
            check (median (arc, 16.0, 24.0) > median (hook, 16.0, 24.0), "the phrase arc: bars 5-6 climb");
            const Note* half = nullptr; for (const auto& n : arc) if (n.start >= 8.0 && n.start < 16.0) half = &n;
            check (half != nullptr && ((half->pitch % 12) == (9 + 7) % 12 || (half->pitch % 12) == (9 + 2) % 12), "the half-phrase ends open (on the 5th or 2nd)");
            std::vector<Note> wide { { 60, 0.0, 1.0, 100 }, { 86, 1.0, 1.0, 100 }, { 62, 2.0, 1.0, 100 } };
            const auto sung = W::singable (wide, s);
            check (std::abs (sung[1].pitch - sung[0].pitch) <= 15, "singable: a two-octave leap is folded");
            const auto aug = W::develop (hook, W::devAugment, s, 64.0), frag = W::develop (hook, W::devFragment, s, 32.0), inv = W::develop (hook, W::devInvert, s, 32.0);
            check (! aug.empty() && std::abs (aug.front().length - 2.0 * hook.front().length) < 1.0e-6 && ! frag.empty() && ! inv.empty() && inv.front().pitch == hook.front().pitch,
                   "development: augmented twice as slow, fragmented, inverted around its first note");
            const auto fs = W::foreshadow (hook);
            bool fewer = fs.size() < hook.size();
            check (fewer, "foreshadowing keeps three notes of each two bars");
            std::vector<Note> top { { 81, 0.0, 1.0, 100 }, { 76, 4.0, 1.0, 100 } };
            W::climax (top, s, 81, true, false, 0.0);
            check (top[0].pitch < 81, "the breakdown stays under the drop's climax");
        }
        // C: harmony
        {
            const auto mix = W::modalMixture ([&] { auto x = s; x.slots[1].degree = 4; return x; }());
            check (mix.slots[1].quality == chords::Quality::Major, "modal mixture: the breakdown's iv becomes IV in a minor key");
            auto dim = s; dim.slots[0].degree = 2;
            check (! W::colourFits (dim, 0, true) && W::colourFits (s, 0, true), "a 9th that would be a b9 is left off; on the tonic it fits");
            std::vector<Note> bar { { 57, 0.0, 1.0, 100 }, { 60, 1.0, 1.0, 100 }, { 64, 2.0, 1.0, 100 }, { 69, 3.0, 1.0, 100 } };
            W::prepareKeyChange (bar, 11, 0.0, false);   // into B minor: its V is F#
            bool onV = true;
            for (const auto& n : bar) onV = onV && (n.pitch % 12 == 6 || n.pitch % 12 == 10 || n.pitch % 12 == 1);
            check (onV, "the build's last bar lands on the new key's V");
            std::vector<Note> b { { 45, 0.0, 1.0, 100 }, { 41, 4.0, 1.0, 100 } };
            W::pedal (b, s);
            check (b[1].pitch % 12 == 9, "a pedal holds the key's root under the chords");
            std::vector<Note> fill { { 57, 0.0, 1.0, 100 }, { 57, 2.0, 1.0, 100 } };
            W::passingChord (fill, s, 0.0, 4.0, true);
            check (fill[1].pitch % 12 == (5 + 7) % 12 && fill[0].pitch == 57, "a passing chord: the fill's last two beats on the next chord's dominant");
        }
        // B, J: feel and parts
        {
            std::vector<Note> notes { { 50, 0.0, 0.5, 100 }, { 50, 0.25, 0.5, 100 } };
            const auto late = W::microTiming (notes, patterns::Part::Bass, patterns::Style::Uplifting);
            check (late[0].start > notes[0].start, "micro-timing: the bass sits a touch behind");
            const auto shaped = W::velocityContour (notes, 0.0, 32.0, false, patterns::Style::Uplifting);
            check (shaped[0].velocity > shaped[1].velocity, "velocity contours: the beat stronger than the 16th after it");
            std::vector<Note> hi { { 75, 0.0, 1.0, 100 }, { 77, 1.0, 1.0, 100 } };
            W::keepInBand (hi, patterns::Part::Bass);
            check (hi[0].pitch <= 55 && hi[1].pitch <= 55, "registers: a bass line moves down into the bass's band");
            std::vector<Note> hook { { 76, 0.0, 2.0, 100 }, { 77, 2.0, 2.0, 100 } };
            std::vector<Note> ctr { { 76, 0.0, 2.0, 90 }, { 70, 2.0, 2.0, 90 } };
            const auto cp = W::counterpoint (ctr, hook, s, 0.0);
            const auto iv = (hook[0].pitch - cp[0].pitch) % 12;
            check (cp[0].pitch < hook[0].pitch && (iv == 3 || iv == 4 || iv == 8 || iv == 9), "counterpoint: under the hook, a third or sixth on the beat");
        }
        // D: song-level arrangement with every group on
        {
            suite::SectionMap map;
            map.count = 4; map.totalBars = 96;
            map.sections[0] = { 1, suite::SectionType::Intro }; map.sections[1] = { 33, suite::SectionType::Breakdown };
            map.sections[2] = { 49, suite::SectionType::Build }; map.sections[3] = { 65, suite::SectionType::Drop };
            chords::SongPatterns sp;
            sp.library = true; sp.libMood = 0; sp.libStyle = 0; sp.libSeed = 7;
            sp.chords = { 1, 1, 1, 4, 3, 1 }; sp.arp = { 1, 1, 1, 1, 1, 1 };
            auto spOn = sp; spOn.write = W::Options::all(); spOn.write.learned = false;
            const auto chordsOff = chords::songClip (s, map, sp, chords::SongPart::Chords), chordsOn = chords::songClip (s, map, spOn, chords::SongPart::Chords);
            auto startsIn = [] (const std::vector<Note>& v, double a, double b) { int k = 0; for (const auto& n : v) if (n.start >= a - 1.0e-6 && n.start < b - 1.0e-6) ++k; return k; };
            check (startsIn (chordsOff, 0.0, 64.0) > 0 && startsIn (chordsOn, 0.0, 64.0) == 0, "a DJ-friendly intro: its first 16 bars are the bass's alone");
            check (startsIn (chordsOn, 64 * 4.0 - 1.0, 64 * 4.0) == 0, "a breath before the drop: nothing starts in the build's last beat");
            const auto leadOff = chords::songClip (s, map, sp, chords::SongPart::Lead), leadOn = chords::songClip (s, map, spOn, chords::SongPart::Lead);
            check (startsIn (leadOn, 33 * 4.0 - 4.0, 41 * 4.0 - 4.0) != startsIn (leadOff, 33 * 4.0 - 4.0, 41 * 4.0 - 4.0) || leadOn.size() != leadOff.size(),
                   "the hook is shaped and developed (the breakdown foreshadows it, the build fragments it)");
            const auto arpOn = chords::songClip (s, map, spOn, chords::SongPart::Arp);
            check (startsIn (arpOn, 32 * 4.0, 32 * 4.0 + 1.5) == 3, "the song's signature: the hook's first three notes open the breakdown");
            bool finite = true;
            for (auto p : { chords::SongPart::Bass, chords::SongPart::Counter, chords::SongPart::Piano, chords::SongPart::Strings, chords::SongPart::Brass, chords::SongPart::Choir })
                for (const auto& n : chords::songClip (s, map, spOn, p)) finite = finite && n.pitch >= 0 && n.pitch <= 127 && n.length > 0.0 && std::isfinite (n.start);
            check (finite, "every part renders sensibly with every Writing group on");
        }
        // F: learning from an edit
        {
            std::vector<Note> original, edited;
            for (int k = 0; k < 32; ++k) original.push_back ({ 60, k * 0.5, 0.4, 100 });
            for (int k = 0; k < 32; k += 2) edited.push_back ({ 72, k * 0.5, 0.4, 90 });
            W::learnEdit (patterns::Part::Arp, original, edited);
            const auto applied = W::applyLearned (original, patterns::Part::Arp);
            check (applied.size() < original.size() && applied.front().pitch > 60, "your edit is learned: fewer notes and an octave up next time");
        }
        // 52: the audition renderer
        {
            const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceAudition.wav");
            chords::SongPatterns sp; sp.library = true;
            check (chords::audition::renderDrop (wav, s, sp) && wav.getSize() > 100000, "a kit's drop renders to a WAV for audition");
            wav.deleteFile();
        }
        prefs.deleteFile();
    }

    return finish();
}
