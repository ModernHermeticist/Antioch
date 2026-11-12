#pragma once

// The pattern library: bass lines, arps and plucks, chords (pads, stabs and gates) and lead
// hooks, organised by mood and style. Every pattern is original, written from the conventions of
// the genre (offbeat and rolling basses, pedal and 3-3-2 arps, trance gates, anthem hooks), not
// transcribed from records.
//
// A family is one idea at three energies (low for intros and breakdowns, mid for grooves and
// the start of builds, high for drops) plus a fill bar for the end of each 8-bar phrase, so a
// song keeps one cohesive bass, arp, chord rhythm and hook from the intro to the last drop while
// its density follows the arrangement.
//
// The families live in text files, one per role (Patterns/*.txt, embedded in the plugin), so
// the library grows by writing lines, not code. Patterns are written chord-relative, a
// character per 16th, so the same family fits any progression and key:
//   r t f s   the chord's root, third, fifth, seventh (the octave on a triad)
//   R T F S   the same an octave up            l    the root an octave down
//   9 4       the ninth / the fourth above the root (in the key)
//   u d       a scale step up / down from the last note
//   k         the last note again              n N  a scale / half step leading to the next chord's root
//   a A b     the next arp tone (the family's order) / the same an octave up / the one before
//   c C o     chords: the voiced chord / with its top note doubled an octave up / root and fifth only
//   ,         a ghost: the last note, soft and short
//   z         a roll on the root to the end of the hold (timpani)
//   ~         slide: the last note runs into the next (a 303 glide)
//   -         hold the last note                .    rest
// Spaces are ignored; '/' separates bars, '+' separates voices (a piano's two hands, cellos
// under violins). A voice starting with '<' or '>' sits an octave lower or higher.
//
// Lead hooks move to the nearest instance of each chord tone, so they voice-lead smoothly over
// the changes; bass, arps and chords sit in their own registers.

#include <juce_core/juce_core.h>
#include <array>
#include <string>
#include <vector>
#include "PatternData.h"

namespace patterns
{
    enum class Mood  { Euphoric, Hopeful, Melancholic, Dark, Driving, Dreamy, Epic, count };
    enum class Style { Uplifting, Classic, Progressive, Tech, Psy, Vocal, Orchestral, count };
    enum class Part  { Bass, Arp, Chords, Lead, Piano, Strings, Brass, Choir, Timpani, Acid, Chops, Candy, Counter, count };

    inline const juce::StringArray& moodNames()
    {
        static const juce::StringArray n { "Euphoric", "Hopeful", "Melancholic", "Dark", "Driving", "Dreamy", "Epic" };
        return n;
    }
    inline const juce::StringArray& styleNames()
    {
        static const juce::StringArray n { "Uplifting", "Classic 138", "Progressive", "Tech trance", "Psy-leaning", "Vocal", "Orchestral" };
        return n;
    }
    /** The role's file name (Patterns/<name>.txt) and its name in the song. */
    inline const juce::StringArray& partFiles()
    {
        static const juce::StringArray n { "bass", "arp", "chords", "lead", "piano", "strings", "brass", "choir", "timpani", "acid", "chops", "candy", "counter" };
        return n;
    }

    /** Arp orders (for 'a'). */
    enum Order { up = 0, down, upDown, converge, skip };

    struct Family
    {
        std::string name;
        Part part = Part::Bass;
        int kind = 0;                // see kinds in PatternRender.h (per role)
        uint32_t moods = 0, styles = 0;
        std::array<std::string, 3> level;   // low, mid, high energy
        std::string fill;                   // the phrase-end bar(s)
        std::string fill2, high2;           // optional: a second fill and a second high energy, for every other phrase
        float gate = 0.8f;                  // a note's length as a share of the gap to the next one
        int order = up;
        bool tagged (Mood m) const { return ((moods >> (int) m) & 1u) != 0; }
        bool tagged (Style s) const { return ((styles >> (int) s) & 1u) != 0; }
    };

    namespace detail
    {
        inline uint32_t tagBits (const juce::String& t, const char* letters)
        {
            if (t.contains ("*")) return 0x7f;
            uint32_t bits = 0;
            for (int i = 0; letters[i] != 0; ++i) if (t.containsChar (letters[i])) bits |= 1u << i;
            return bits;
        }

        /** One role's text: a family per line (name | kind | moods | styles | gate | order | low | mid | high | fill). */
        inline void parse (const juce::String& text, Part part, std::vector<Family>& into)
        {
            for (auto line : juce::StringArray::fromLines (text))
            {
                line = line.trim();
                if (line.isEmpty() || line.startsWithChar ('#')) continue;
                auto f = juce::StringArray::fromTokens (line, "|", "");
                if (f.size() < 10) { jassertfalse; continue; }
                for (auto& x : f) x = x.trim();
                Family fam;
                fam.name = f[0].toStdString();
                fam.part = part;
                fam.kind = f[1].getIntValue();
                fam.moods = tagBits (f[2], "EHMKDRP");
                fam.styles = tagBits (f[3], "UCPTYVO");
                fam.gate = (float) f[4].getDoubleValue();
                const auto o = f[5].isEmpty() ? '-' : (char) f[5][0];
                fam.order = o == 'd' ? down : o == 'w' ? upDown : o == 'c' ? converge : o == 's' ? skip : up;
                for (int k = 0; k < 3; ++k) fam.level[(size_t) k] = f[6 + k].removeCharacters (" ").toStdString();
                fam.fill = f[9].removeCharacters (" ").toStdString();
                if (f.size() > 10) fam.fill2 = f[10].removeCharacters (" ").toStdString();
                if (f.size() > 11) fam.high2 = f[11].removeCharacters (" ").toStdString();
                into.push_back (std::move (fam));
            }
        }
    }

    /** Every family, from the embedded role files. */
    inline const std::vector<Family>& library()
    {
        static const std::vector<Family> lib = []
        {
            std::vector<Family> all;
            for (int i = 0; i < PatternData::namedResourceListSize; ++i)
            {
                const juce::String file (PatternData::originalFilenames[i]);
                const auto role = partFiles().indexOf (file.upToLastOccurrenceOf (".", false, false));
                if (role < 0) continue;
                int size = 0;
                if (const auto* data = PatternData::getNamedResource (PatternData::namedResourceList[i], size))
                    detail::parse (juce::String::fromUTF8 (data, size), (Part) role, all);
            }
            return all;
        }();
        return lib;
    }

    /** A voice's bars: 16 characters each (padded with rests), and its octave shift. */
    struct Voice { std::vector<std::string> bars; int octave = 0; };
    inline std::vector<Voice> voices (const std::string& text)
    {
        std::vector<Voice> out;
        for (auto v : juce::StringArray::fromTokens (juce::String (text), "+", ""))
        {
            Voice voice;
            v = v.removeCharacters (" ");
            while (v.startsWithChar ('<') || v.startsWithChar ('>')) { voice.octave += v[0] == '>' ? 1 : -1; v = v.substring (1); }
            for (auto b : juce::StringArray::fromTokens (v, "/", ""))
            {
                auto bar = b.toStdString();
                if (bar.size() > 16) bar.resize (16);
                while (bar.size() < 16) bar.push_back ('.');
                voice.bars.push_back (bar);
            }
            if (voice.bars.empty()) voice.bars.push_back (std::string (16, '.'));
            out.push_back (std::move (voice));
        }
        if (out.empty()) out.push_back ({ { std::string (16, '.') }, 0 });
        return out;
    }

    /** The first voice's bars (single-voice roles). */
    inline std::vector<std::string> bars (const std::string& text) { return voices (text).front().bars; }

    inline uint32_t hash (uint32_t x)
    {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return x;
    }

    /** All the families for a part and kind that fit the mood and style best (style counts more:
        it's the rhythm language; the mood colours it). */
    inline std::vector<int> candidates (Part part, int kind, Mood mood, Style style)
    {
        std::vector<int> best;
        int bestScore = -1;
        const auto& lib = library();
        for (int i = 0; i < (int) lib.size(); ++i)
        {
            const auto& f = lib[(size_t) i];
            if (f.part != part || f.kind != kind) continue;
            const auto score = ((f.styles >> (int) style) & 1u) * 3 + ((f.moods >> (int) mood) & 1u) * 2;
            if ((int) score > bestScore) { bestScore = (int) score; best.clear(); }
            if ((int) score == bestScore) best.push_back (i);
        }
        return best;
    }

    /** The song's family for a part and kind: one of the best fits, chosen by the seed (the same
        seed always gives the same song). */
    inline const Family& pick (Part part, int kind, Mood mood, Style style, uint32_t seed)
    {
        auto c = candidates (part, kind, mood, style);
        if (c.empty()) c = candidates (part, 0, mood, style);
        if (c.empty()) { static const Family none; return none; }
        return library()[(size_t) c[hash (seed * 2654435761u + (uint32_t) part * 977u + (uint32_t) kind * 131u) % c.size()]];
    }
}
