#pragma once

// Auto-reference: from a folder of reference tracks, pick the one closest to this song: its
// tonal balance (the shape of the long-term spectrum, against the mix so far), and its key and
// tempo when the file name says them ("Artist - Track (Am, 138).wav", "... 140 BPM F#m ...").

#include "Advisor.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <optional>

namespace autoref
{
    /** Key in a file name, as root 0-11 and minor (true) / major, if one is there. */
    inline std::optional<std::pair<int, bool>> keyFromName (const juce::String& fileName)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        const auto tokens = juce::StringArray::fromTokens (fileName.replaceCharacters ("()[]_,.-", "        "), " ", "");
        for (int t = 0; t < tokens.size(); ++t)
        {
            const auto tok = tokens[t];
            for (int r = 0; r < 12; ++r)
                for (const auto* n : { names[r], flats[r] })
                {
                    const juce::String root (n);
                    if (! tok.startsWith (root)) continue;
                    const auto rest = tok.substring (root.length());
                    const auto next = tokens[t + 1].toLowerCase();
                    if (rest == "m" || rest == "min" || (rest.isEmpty() && (next == "minor" || next == "min"))) return std::make_pair (r, true);
                    if (rest == "maj" || (rest.isEmpty() && (next == "major" || next == "maj"))) return std::make_pair (r, false);
                }
        }
        return std::nullopt;
    }

    /** Tempo in a file name (a number from 90 to 180), if one is there. */
    inline float bpmFromName (const juce::String& fileName)
    {
        for (const auto& tok : juce::StringArray::fromTokens (fileName.replaceCharacters ("()[]_,.-", "        "), " ", ""))
        {
            const auto digits = tok.retainCharacters ("0123456789");
            if (digits.isEmpty() || digits.length() != tok.toLowerCase().removeCharacters ("bpm").length()) continue;
            const auto v = digits.getFloatValue();
            if (v >= 90.0f && v <= 180.0f) return v;
        }
        return 0.0f;
    }

    struct Candidate { juce::String name; advisor::Bands bands {}; bool valid = false; };
    struct Pick { int index = -1; float score = 1.0e9f; juce::String why; };

    /** The closest candidate: tonal shape (mean absolute dB difference, both curves relative to
        their own average) plus 3 points for a different key (1.5 for the relative key's mode) and a
        point per 4 BPM away, when the names say. `root` < 0 / `bpm` 0 = unknown. */
    inline Pick choose (const std::vector<Candidate>& candidates, const advisor::Bands& mix, bool haveMix, int root, bool minor, float bpm)
    {
        Pick best;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const auto& c = candidates[i];
            if (! c.valid) continue;
            float score = 0.0f;
            juce::StringArray why;
            if (haveMix)
            {
                float mc = 0.0f, mm = 0.0f;
                const auto n = (float) mix.size();
                for (size_t b = 0; b < mix.size(); ++b) { mc += c.bands[b] / n; mm += mix[b] / n; }
                float d = 0.0f;
                for (size_t b = 0; b < mix.size(); ++b) d += std::abs ((c.bands[b] - mc) - (mix[b] - mm)) / n;
                score += d;
                why.add ("tone within " + juce::String (d, 1) + " dB");
            }
            if (root >= 0)
                if (const auto key = keyFromName (c.name))
                {
                    const auto same = key->first == root && key->second == minor;
                    const auto relative = key->second != minor && ((minor && key->first == (root + 3) % 12) || (! minor && key->first == (root + 9) % 12));
                    score += same ? 0.0f : relative ? 1.5f : 3.0f;
                    if (same) why.add ("same key");
                }
            if (bpm > 0.0f)
                if (const auto b = bpmFromName (c.name); b > 0.0f)
                {
                    score += std::abs (b - bpm) / 4.0f;
                    if (std::abs (b - bpm) < 1.5f) why.add ("same tempo");
                }
            if (score < best.score) best = { (int) i, score, why.joinIntoString (", ") };
        }
        return best;
    }
}
