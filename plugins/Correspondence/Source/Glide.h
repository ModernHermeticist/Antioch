#pragma once

// Lead expression for exported melodies: glide and scoops.
//
//  - Glide: on a leap of a minor third or more, the note runs into the next one (they
//    overlap slightly), so a mono synth in legato / portamento mode slides between them.
//  - Scoops: the first note of each phrase bends up into its pitch (2 semitones, over a
//    1/16), the classic trance lead "scoop". Written as pitch-bend events; set the synth's
//    bend range to match (2 or 12 semitones).

#include "ChordEngine.h"

namespace chords
{
    struct LeadExpression
    {
        bool glide = true, scoops = true;
        int bendRange = 2;           // the synth's pitch-bend range in semitones
        int scoopSemitones = 2;
        double scoopQuarters = 0.25;
    };

    /** Bends (in semitones) for each note of a lead line, and the line with glides applied. */
    struct ExpressiveLine
    {
        std::vector<Note> notes;
        std::vector<int> scoop;   // per note: semitones it bends up from (0 = none)
    };

    inline ExpressiveLine expressLead (std::vector<Note> notes, const LeadExpression& x)
    {
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        ExpressiveLine out;
        out.scoop.assign (notes.size(), 0);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& n = notes[i];
            const auto prevEnd = i > 0 ? notes[i - 1].start + notes[i - 1].length : -1.0e9;
            if (x.scoops && n.length >= 0.45 && n.start - prevEnd >= 1.0 - 1.0e-6)
                out.scoop[i] = x.scoopSemitones;   // the start of a phrase
            if (x.glide && i + 1 < notes.size())
            {
                const auto& next = notes[i + 1];
                const auto gap = next.start - (n.start + n.length);
                if (std::abs (next.pitch - n.pitch) >= 3 && gap < 0.5 && next.start > n.start + 1.0e-6)
                    n.length = next.start - n.start + 0.03;   // overlap: legato slide on a mono synth
            }
        }
        out.notes = std::move (notes);
        return out;
    }

    /** Pitch-bend events (14-bit values, centre 8192) for the scoops: below pitch at the note
        start, gliding back to centre over the scoop time. */
    inline void addScoops (juce::MidiMessageSequence& seq, const ExpressiveLine& line, const LeadExpression& x, int ppq)
    {
        auto wheel = [&x] (double semitones)
        {
            return juce::jlimit (0, 16383, 8192 + (int) std::lround (semitones / juce::jmax (1, x.bendRange) * 8191.0));
        };
        for (size_t i = 0; i < line.notes.size(); ++i)
        {
            if (line.scoop[i] == 0) continue;
            const auto& n = line.notes[i];
            const auto depth = -juce::jmin (line.scoop[i], x.bendRange);
            const auto len = juce::jmin (x.scoopQuarters, n.length * 0.5);
            constexpr int steps = 8;
            for (int k = 0; k <= steps; ++k)
            {
                const auto t = (double) k / steps;
                const auto curve = (1.0 - t) * (1.0 - t);   // fast at first, settling into the note
                seq.addEvent (juce::MidiMessage::pitchWheel (1, wheel (depth * curve)),
                              juce::jmax (0.0, (n.start + t * len) * ppq - (k == 0 ? 1.0 : 0.0)));
            }
        }
    }

    /** A lead clip as MIDI with glides and scoops (see toMidiFile for the rest). */
    inline juce::MidiFile toLeadMidiFile (const std::vector<Note>& notes, double bpm, const juce::String& name, int accentCC,
                                          const LeadExpression& x)
    {
        const auto line = expressLead (notes, x);
        const auto plain = toMidiFile (line.notes, bpm, name, accentCC);
        juce::MidiMessageSequence seq (*plain.getTrack (0));
        addScoops (seq, line, x, plain.getTimeFormat());
        seq.sort();
        seq.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (plain.getTimeFormat());
        file.addTrack (seq);
        return file;
    }
}
