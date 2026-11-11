#pragma once

// Your identity in Correspondence: the progressions you use most (from the style log), and a
// bank of hooks you keep for every project.

#include "ChordEngine.h"
#include "StyleLog.h"

namespace identity
{
    /** Your progressions, most used first: (roman numerals as logged, how many songs). */
    inline std::vector<std::pair<juce::String, int>> favouriteProgressions (const std::vector<suite::StyleLog::Entry>& entries)
    {
        std::map<juce::String, int> counts;
        for (const auto& e : entries)
            if (e.kind == "song" && e.get ("degrees").isNotEmpty()) ++counts[e.get ("degrees")];
        std::vector<std::pair<juce::String, int>> out (counts.begin(), counts.end());
        std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
        return out;
    }

    /** Roman numerals ("i-VI-III-VII") as slots for key `s`: the degree, and major or minor
        where the case differs from the key's own chord. */
    inline bool parseNumerals (const juce::String& text, chords::Settings& s)
    {
        static const char* numerals[] = { "VII", "III", "IV", "VI", "II", "V", "I" };   // longest first
        static const int degrees[] = { 7, 3, 4, 6, 2, 5, 1 };
        const auto parts = juce::StringArray::fromTokens (text, "-", "");
        if (parts.size() < 2 || parts.size() > chords::maxSlots) return false;
        for (int i = 0; i < parts.size(); ++i)
        {
            const auto p = parts[i].trim().removeCharacters (juce::String::fromUTF8 ("\xc2\xb0+"));
            int degree = 0;
            for (int k = 0; k < 7 && degree == 0; ++k)
                if (p.toUpperCase() == numerals[k]) degree = degrees[k];
            if (degree == 0) return false;
            chords::Slot slot { degree, chords::Quality::Auto };
            const auto upper = p == p.toUpperCase();
            const auto own = chords::romanNumeral (s, slot);
            if ((own == own.toUpperCase()) != upper) slot.quality = upper ? chords::Quality::Major : chords::Quality::Minor;
            s.slots[(size_t) i] = slot;
        }
        s.count = parts.size();
        return true;
    }

    //==============================================================================
    // Hook bank: melodies you like, kept as files, recalled as Melodia's motif in any song.

    inline juce::File& hookFolderOverride() { static juce::File f; return f; }   // tests
    inline juce::File hookFolder()
    {
        if (hookFolderOverride() != juce::File()) return hookFolderOverride();
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Antioch Suite").getChildFile ("Hooks");
    }

    inline bool saveHook (const juce::String& name, const std::vector<chords::Note>& notes)
    {
        if (name.trim().isEmpty() || notes.empty()) return false;
        juce::StringArray lines;
        for (const auto& n : notes)
            lines.add (juce::String (n.pitch) + " " + juce::String (n.start, 4) + " " + juce::String (n.length, 4) + " " + juce::String (n.velocity));
        hookFolder().createDirectory();
        return hookFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".hook").replaceWithText (lines.joinIntoString ("\n") + "\n");
    }

    inline std::vector<std::pair<juce::String, std::vector<chords::Note>>> loadHooks()
    {
        std::vector<std::pair<juce::String, std::vector<chords::Note>>> out;
        for (const auto& f : hookFolder().findChildFiles (juce::File::findFiles, false, "*.hook"))
        {
            std::vector<chords::Note> notes;
            for (const auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
            {
                const auto v = juce::StringArray::fromTokens (line, " ", "");
                if (v.size() == 4) notes.push_back ({ v[0].getIntValue(), v[1].getDoubleValue(), v[2].getDoubleValue(), v[3].getIntValue() });
            }
            if (! notes.empty()) out.push_back ({ f.getFileNameWithoutExtension(), notes });
        }
        std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        return out;
    }
}
