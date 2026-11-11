#pragma once

// Lead fingerprint: your lead sound, kept as a file and matched in any project.
// A fingerprint holds the character settings (euphoria, drive, saturation, multiband, width,
// low cut) and the tonal shape of the processed lead (10 octave bands relative to their
// mean, from 125 Hz up). Matching recalls the character and moves Mud, Presence and Air so
// the new lead's tone heads for the stored shape, whatever synth it comes from.

#include "Alias.h"
#include <juce_core/juce_core.h>
#include "Analysis.h"
#include "Parameters.h"
#include <array>
#include <map>
#include <vector>

namespace fingerprint
{
    using Shape = std::array<float, analysis::numBands>;

    struct Fingerprint
    {
        juce::String name;
        Shape shape {};
        std::map<juce::String, float> character;   // parameter id -> plain value
    };

    /** The character parameters a fingerprint recalls (the EQ is matched instead). */
    inline const juce::StringArray& characterIds()
    {
        static const juce::StringArray ids { Params::euphoria, Params::lowCut, Params::drive, Params::satMix, Params::mbDepth,
                                             Params::mbTime, Params::width, Params::monoBelow };
        return ids;
    }

    /** Band levels (dB) relative to the mean of the lead's bands (125 Hz up); bands below are 0. */
    inline Shape shapeOf (const Shape& bandsDb)
    {
        Shape s {};
        float mean = 0.0f;
        for (int b = 2; b < analysis::numBands; ++b) mean += bandsDb[(size_t) b];
        mean /= (float) (analysis::numBands - 2);
        for (int b = 2; b < analysis::numBands; ++b) s[(size_t) b] = bandsDb[(size_t) b] - mean;
        return s;
    }

    inline juce::File folder()
    {
        return suite::Alias::file ("Lead fingerprints");
    }

    inline bool save (const juce::File& dir, const Fingerprint& f)
    {
        juce::StringArray shape, character;
        for (auto v : f.shape) shape.add (juce::String (v, 2));
        for (const auto& [id, v] : f.character) character.add (id + "=" + juce::String (v, 4));
        dir.createDirectory();
        return dir.getChildFile (juce::File::createLegalFileName (f.name) + ".lead")
                  .replaceWithText ("shape " + shape.joinIntoString (",") + "\ncharacter " + character.joinIntoString (",") + "\n");
    }

    inline std::vector<Fingerprint> loadAll (const juce::File& dir)
    {
        std::vector<Fingerprint> out;
        for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "*.lead"))
        {
            Fingerprint f;
            f.name = file.getFileNameWithoutExtension();
            bool haveShape = false;
            for (const auto& line : juce::StringArray::fromLines (file.loadFileAsString()))
            {
                if (line.startsWith ("shape "))
                {
                    const auto v = juce::StringArray::fromTokens (line.fromFirstOccurrenceOf (" ", false, false), ",", "");
                    if (v.size() != analysis::numBands) continue;
                    for (int b = 0; b < analysis::numBands; ++b) f.shape[(size_t) b] = v[b].getFloatValue();
                    haveShape = true;
                }
                else if (line.startsWith ("character "))
                    for (const auto& kv : juce::StringArray::fromTokens (line.fromFirstOccurrenceOf (" ", false, false), ",", ""))
                        if (characterIds().contains (kv.upToFirstOccurrenceOf ("=", false, false)))
                            f.character[kv.upToFirstOccurrenceOf ("=", false, false)] = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            }
            if (haveShape) out.push_back (std::move (f));
        }
        std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.name < b.name; });
        return out;
    }

    /** EQ moves (dB) that take `now` towards `target`: Mud from 250-500 Hz, Presence from
        2-4 kHz, Air from 8-16 kHz; 70% of the way, so a second match refines it. */
    struct EqMove { float mud = 0.0f, presence = 0.0f, air = 0.0f; };
    inline EqMove eqTowards (const Shape& now, const Shape& target)
    {
        auto diff = [&] (int a, int b) { return 0.5f * ((target[(size_t) a] - now[(size_t) a]) + (target[(size_t) b] - now[(size_t) b])); };
        return { 0.7f * diff (3, 4), 0.7f * diff (6, 7), 0.7f * diff (8, 9) };
    }
}
