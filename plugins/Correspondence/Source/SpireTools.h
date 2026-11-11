#pragma once

// Spire helpers: the song's knowledge, written for the synth you play it on.
//
//  - Macro rides: CC automation for Spire's four macros in every song clip, following the
//    song (MIDI-learn each macro to its CC once, in your patch). Macro 1 brightness opens
//    through builds, macro 2 drive grows into the drops, macro 3 space blooms in breakdowns,
//    macro 4 movement rises through builds.
//  - Layers: the parts a typical Spire stack plays: an octave layer over the lead in the
//    drops, a pluck on the lead's beats in builds and drops, and a sub under the bass
//    (out in breakdowns), each a clip for its own Spire.
//  - FL piano roll scripts: any clip as a script for FL Studio's piano roll (Tools >
//    Scripting), the notes inside it, so it pastes straight into the open pattern.

#include "ChordEngine.h"
#include <cmath>

namespace spire
{
    using chords::Note;
    using T = suite::SectionType;

    /** The four macros' names, for the UI and the notes. */
    inline const char* macroName (int macro)
    {
        static const char* n[] = { "brightness (filter)", "drive", "space (reverb / delay)", "movement (LFO / mod)" };
        return n[juce::jlimit (0, 3, macro)];
    }

    /** Macro `macro` (0-3) at song position `q` (quarters), 0-127; -1 outside the map. */
    inline int macroValue (const suite::SectionMap& map, int macro, double q, double barQuarters = 4.0)
    {
        const auto bar = (int) std::floor (q / barQuarters + 1.0e-9) + 1;
        if (map.count == 0 || bar < map.sections[0].startBar || bar > map.totalBars) return -1;
        int i = 0;
        while (i + 1 < map.count && map.sections[(size_t) i + 1].startBar <= bar) ++i;
        const auto startQ = (map.sections[(size_t) i].startBar - 1) * barQuarters, endQ = (map.endBar (i) - 1) * barQuarters;
        const auto p = (float) juce::jlimit (0.0, 1.0, (q - startQ) / juce::jmax (1.0e-6, endQ - startQ));
        int earlierDrops = 0;
        for (int k = 0; k < i; ++k) earlierDrops += map.sections[(size_t) k].type == T::Drop;
        auto lerp = [p] (float a, float b) { return a + p * (b - a); };
        float v = 0.0f;
        switch (map.sections[(size_t) i].type)
        {
            case T::Intro:     v = macro == 0 ? lerp (50, 70) : macro == 1 ? 30 : macro == 2 ? 60 : 20; break;
            case T::Groove:    v = macro == 0 ? 75 : macro == 1 ? 50 : macro == 2 ? 40 : 25; break;
            case T::Breakdown: v = macro == 0 ? (p < 0.75f ? 35.0f : 35.0f + (p - 0.75f) * 4.0f * 20.0f) : macro == 1 ? 10 : macro == 2 ? 110 : 30; break;
            case T::Build:     v = macro == 0 ? lerp (40, 127) : macro == 1 ? lerp (40, 90) : macro == 2 ? lerp (70, 20) : lerp (0, 127); break;
            case T::Drop:      v = macro == 0 ? 127 : macro == 1 ? (earlierDrops > 0 ? 127 : 100) : macro == 2 ? 30 : 60; break;
            case T::Outro:     v = macro == 0 ? lerp (70, 40) : macro == 1 ? 30 : macro == 2 ? 60 : 20; break;
            default:           v = 64; break;
        }
        return juce::jlimit (0, 127, juce::roundToInt (v));
    }

    /** CC numbers for the four macros: 0 = off, 1 = CC 20-23, 2 = CC 102-105. */
    inline int macroBaseCC (int choice) { return choice == 1 ? 20 : choice == 2 ? 102 : 0; }

    /** Adds the macro rides to a clip's MIDI (a CC every 1/16 while it changes, otherwise on change). */
    inline void addMacroRides (juce::MidiMessageSequence& seq, const suite::SectionMap& map, int baseCC, int channel, int ppq)
    {
        if (baseCC <= 0 || map.count == 0) return;
        const auto endQ = map.totalBars * 4.0;
        for (int macro = 0; macro < 4; ++macro)
        {
            int last = -1;
            for (double q = 0.0; q < endQ; q += 0.25)
            {
                const auto v = macroValue (map, macro, q);
                if (v < 0 || v == last) continue;
                seq.addEvent (juce::MidiMessage::controllerEvent (channel, baseCC + macro, v), q * ppq);
                last = v;
            }
        }
        seq.sort();
        seq.updateMatchedPairs();
    }

    inline juce::MidiFile withMacroRides (const juce::MidiFile& file, const suite::SectionMap& map, int baseCC)
    {
        if (baseCC <= 0 || file.getNumTracks() == 0) return file;
        juce::MidiMessageSequence seq (*file.getTrack (0));
        addMacroRides (seq, map, baseCC, 1, file.getTimeFormat());
        juce::MidiFile out;
        out.setTicksPerQuarterNote (file.getTimeFormat());
        out.addTrack (seq);
        return out;
    }

    //==============================================================================
    inline T typeAt (const suite::SectionMap& map, double q)
    {
        const auto bar = (int) std::floor (q / 4.0 + 1.0e-9) + 1;
        int i = 0;
        while (i + 1 < map.count && map.sections[(size_t) i + 1].startBar <= bar) ++i;
        return map.count > 0 ? map.sections[(size_t) i].type : T::Drop;
    }

    /** The lead an octave up, in the drops only, a little softer (a second Spire, or a layer). */
    inline std::vector<Note> octaveLayer (const std::vector<Note>& lead, const suite::SectionMap& map)
    {
        std::vector<Note> out;
        for (auto n : lead)
            if (typeAt (map, n.start) == T::Drop && n.pitch + 12 <= 108)
            {
                n.pitch += 12;
                n.velocity = juce::jmax (1, n.velocity - 12);
                out.push_back (n);
            }
        return out;
    }

    /** A pluck on the lead: its notes that start on a beat, short, in builds and drops. */
    inline std::vector<Note> pluckLayer (const std::vector<Note>& lead, const suite::SectionMap& map)
    {
        std::vector<Note> out;
        for (auto n : lead)
        {
            const auto t = typeAt (map, n.start);
            const auto onBeat = std::abs (n.start - std::round (n.start)) < 1.0e-6;
            if ((t == T::Drop || t == T::Build) && onBeat)
            {
                n.length = juce::jmin (n.length, 0.25);
                out.push_back (n);
            }
        }
        return out;
    }

    /** A sub under the bass: each bass note in the first bass octave (C1-B1), held to the next
        one (legato, for a mono sine), and nothing in breakdowns. */
    inline std::vector<Note> subLayer (std::vector<Note> bass, const suite::SectionMap& map)
    {
        std::sort (bass.begin(), bass.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        std::vector<Note> out;
        for (size_t i = 0; i < bass.size(); ++i)
        {
            auto n = bass[i];
            if (typeAt (map, n.start) == T::Breakdown) continue;
            if (! out.empty() && std::abs (out.back().start - n.start) < 1.0e-6) continue;   // one note at a time
            const auto pc = ((n.pitch % 12) + 12) % 12;
            n.pitch = 24 + pc;
            // Held until the next bass note or the end of the bar, whichever is first.
            const auto barEnd = (std::floor (n.start / 4.0 + 1.0e-9) + 1.0) * 4.0;
            const auto next = i + 1 < bass.size() ? bass[i + 1].start : barEnd;
            n.length = juce::jmax (0.05, juce::jmin (next, barEnd) - n.start - 0.01);
            if (! out.empty() && out.back().pitch == n.pitch && out.back().start + out.back().length >= n.start - 0.02)
            {
                out.back().length = n.start + n.length - out.back().start;   // same note: keep it held
                continue;
            }
            out.push_back (n);
        }
        return out;
    }

    //==============================================================================
    /** A piano roll script for FL Studio (Tools > Scripting) that pastes `notes` into the open
        pattern: at its start or after its last note, keeping or replacing what's there. */
    inline juce::String pianoRollScript (const std::vector<Note>& notes, const juce::String& title, const juce::String& description)
    {
        juce::StringArray lines;
        lines.add ("# " + title + " - written by Correspondence (Antioch Suite).");
        lines.add ("# In FL Studio's piano roll: Tools > Scripting > this script.");
        lines.add ("import flpianoroll as flp");
        lines.add ("");
        lines.add ("NOTES = [");
        for (const auto& n : notes)
            lines.add ("    (" + juce::String (n.pitch) + ", " + juce::String (n.start, 4) + ", " + juce::String (n.length, 4) + ", "
                       + juce::String (juce::jlimit (1, 127, n.velocity)) + "),");
        lines.add ("]");
        lines.add ("");
        lines.add ("def createDialog():");
        lines.add ("    form = flp.ScriptDialog(\"" + title.replace ("\"", "'") + "\", \"" + description.replace ("\"", "'") + "\")");
        lines.add ("    form.AddInputCombo(\"Where\", \"Start of the pattern,After the last note\", 0)");
        lines.add ("    form.AddInputCombo(\"Existing notes\", \"Keep them,Replace them\", 0)");
        lines.add ("    return form");
        lines.add ("");
        lines.add ("def apply(form):");
        lines.add ("    ppq = flp.score.PPQ");
        lines.add ("    if form.GetInputValue(\"Existing notes\") == 1:");
        lines.add ("        flp.score.clearNotes(True)");
        lines.add ("    offset = 0");
        lines.add ("    if form.GetInputValue(\"Where\") == 1:");
        lines.add ("        for i in range(flp.score.noteCount):");
        lines.add ("            n = flp.score.getNote(i)");
        lines.add ("            offset = max(offset, n.time + n.length)");
        lines.add ("    for pitch, start, length, velocity in NOTES:");
        lines.add ("        n = flp.Note()");
        lines.add ("        n.number = pitch");
        lines.add ("        n.time = offset + int(round(start * ppq))");
        lines.add ("        n.length = max(1, int(round(length * ppq)))");
        lines.add ("        n.velocity = velocity / 127.0");
        lines.add ("        flp.score.addNote(n)");
        return lines.joinIntoString ("\n") + "\n";
    }

    /** FL Studio's piano roll scripts folder (Documents/Image-Line/FL Studio/Settings/Piano roll scripts). */
    inline juce::File& scriptFolderOverride() { static juce::File f; return f; }   // tests
    inline juce::File pianoRollScriptFolder()
    {
        if (scriptFolderOverride() != juce::File()) return scriptFolderOverride();
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Image-Line").getChildFile ("FL Studio").getChildFile ("Settings").getChildFile ("Piano roll scripts");
    }
}
