#pragma once

// Music-theory helpers shared by the suite: note names, scales, tempo divisions.

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace suite::music
{
    inline const juce::StringArray& noteNames()
    {
        static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names;
    }

    inline float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

    //==============================================================================
    struct Scale
    {
        const char* name;
        std::array<int, 7> steps;
    };

    inline const std::array<Scale, 6>& scales()
    {
        static const std::array<Scale, 6> s
        { {
            { "Minor",          { 0, 2, 3, 5, 7, 8, 10 } },
            { "Major",          { 0, 2, 4, 5, 7, 9, 11 } },
            { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } },
            { "Dorian",         { 0, 2, 3, 5, 7, 9, 10 } },
            { "Phrygian",       { 0, 1, 3, 5, 7, 8, 10 } },
            { "Mixolydian",     { 0, 2, 4, 5, 7, 9, 10 } },
        } };
        return s;
    }

    inline juce::StringArray scaleNames()
    {
        juce::StringArray names;
        for (const auto& s : scales())
            names.add (s.name);
        return names;
    }

    /** Semitone offset of a diatonic step (can be negative or > 6; wraps octaves). */
    inline int scaleStep (int scaleIndex, int step) noexcept
    {
        const auto& steps = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scaleIndex)].steps;
        const auto octave = (step >= 0 ? step / 7 : (step - 6) / 7);
        const auto idx = step - octave * 7;
        return octave * 12 + steps[(size_t) idx];
    }

    inline juce::String keyName (int root, int scaleIndex)
    {
        return noteNames()[juce::jlimit (0, 11, root)] + " " + scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scaleIndex)].name;
    }

    //==============================================================================
    struct Division { const char* name; double quarters; };

    inline const std::array<Division, 14>& divisions()
    {
        static const std::array<Division, 14> d
        { {
            { "1/32",  0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 },
            { "1/8T",  1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8D", 0.75 },
            { "1/4T",  2.0 / 3.0 }, { "1/4", 1.0 }, { "1/4D", 1.5 },
            { "1/2T",  4.0 / 3.0 }, { "1/2", 2.0 }, { "1/2D", 3.0 }, { "1 Bar", 4.0 },
        } };
        return d;
    }

    inline juce::StringArray divisionNames()
    {
        juce::StringArray names;
        for (const auto& d : divisions())
            names.add (d.name);
        return names;
    }

    inline int divisionIndex (const char* name)
    {
        for (size_t i = 0; i < divisions().size(); ++i)
            if (juce::String (divisions()[i].name) == name)
                return (int) i;
        jassertfalse;
        return 0;
    }

    inline double divisionQuarters (int index) noexcept
    {
        return divisions()[(size_t) juce::jlimit (0, (int) divisions().size() - 1, index)].quarters;
    }
}
