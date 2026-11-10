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
        suite::SectionMap map;
        map.totalBars = 16; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 5, suite::SectionType::Drop };
        map.sections[2] = { 9, suite::SectionType::Breakdown };
        map.sections[3] = { 13, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        const auto parent = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CorrespondenceExportAll");
        parent.deleteRecursively();
        const auto folder = p->exportEverything (parent);
        const auto files = folder.findChildFiles (juce::File::findFiles, false, "*.mid");
        check (folder.isDirectory() && files.size() == 12, "a song folder with 11 clips (incl. counter-melody and drop-2 harmony) and one all-parts file (" + juce::String (files.size()) + " files in "
                   + folder.getFileName() + ")");

        juce::File all;
        for (auto& f : files) if (f.getFileName().contains ("all parts")) all = f;
        juce::MidiFile midi;
        juce::FileInputStream in (all);
        const auto read = all.existsAsFile() && midi.readFrom (in);
        check (read && midi.getNumTracks() == 7, "all parts: a section track plus Chords, Bass, Arp, Lead, Harmony and Counter (" + juce::String (midi.getNumTracks()) + " tracks)");
        if (read && midi.getNumTracks() == 7)
        {
            juce::StringArray markers;
            for (auto* e : *midi.getTrack (0))
                if (e->message.isTextMetaEvent() && e->message.getMetaEventType() == 6)
                    markers.add (e->message.getTextFromTextMetaEvent() + "@" + juce::String (e->message.getTimeStamp() / 960.0, 0));
            check (markers.joinIntoString (" ") == "Groove@0 Drop@16 Breakdown@32 Drop 2@48", "sections as markers: " + markers.joinIntoString (" "));
            int notes = 0;
            for (int t = 1; t < 7; ++t)
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
