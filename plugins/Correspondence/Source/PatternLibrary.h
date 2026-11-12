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
        float swing = 0.0f;                 // the off 16ths late by this share of a 16th (a shuffle written into the pattern)
        bool add7 = false, add9 = false;    // chord colours: the chord's 7th / 9th added to every chord hit
        int inversion = 0;                  // chord hits with their lowest 1 or 2 notes an octave up
        uint32_t where = 0;                 // only for these kinds of section (bit per SectionType); 0 = the song's family everywhere
        uint32_t character = 0;             // what it feels like (bit per characterNames(), from Patterns/characters.txt)
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
                auto opt = [&] (int i) { return f.size() > i && f[i] != "-" ? f[i].removeCharacters (" ").toStdString() : std::string(); };
                fam.fill2 = opt (10);
                fam.high2 = opt (11);
                // feel: "s" a trance shuffle, or a number (share of a 16th)
                if (const auto feel = juce::String (opt (12)); feel.isNotEmpty())
                    fam.swing = feel == "s" ? 0.33f : juce::jlimit (0.0f, 0.6f, (float) feel.getDoubleValue());
                // colour: 7, 9, i1, i2
                if (const auto colour = juce::String (opt (13)); colour.isNotEmpty())
                {
                    fam.add7 = colour.containsChar ('7');
                    fam.add9 = colour.containsChar ('9');
                    fam.inversion = colour.contains ("i2") ? 2 : colour.contains ("i1") ? 1 : 0;
                }
                // where: I intro, G groove, K breakdown, B build, D drop, O outro
                if (const auto where = juce::String (opt (14)); where.isNotEmpty())
                    for (int t = 0; t < 6; ++t) if (where.containsChar ("IGKBDO"[t])) fam.where |= 1u << t;
                into.push_back (std::move (fam));
            }
        }
    }

    /** Every family, from the embedded role files. */
    /** Characters: what a family feels like beyond its mood. A song's parts share its hook's. */
    inline const juce::StringArray& characterNames()
    {
        static const juce::StringArray n { "heroic", "intimate", "hypnotic", "playful", "soaring" };
        return n;
    }

    inline const std::vector<Family>& library()
    {
        static const std::vector<Family> lib = []
        {
            std::vector<Family> all;
            juce::String characters;
            for (int i = 0; i < PatternData::namedResourceListSize; ++i)
            {
                const juce::String file (PatternData::originalFilenames[i]);
                const auto role = partFiles().indexOf (file.upToLastOccurrenceOf (".", false, false));
                int size = 0;
                const auto* data = PatternData::getNamedResource (PatternData::namedResourceList[i], size);
                if (data == nullptr) continue;
                if (file == "characters.txt") { characters = juce::String::fromUTF8 (data, size); continue; }
                if (role < 0) continue;
                detail::parse (juce::String::fromUTF8 (data, size), (Part) role, all);
            }
            // character | role: family, family | role: ...
            for (auto line : juce::StringArray::fromLines (characters))
            {
                line = line.trim();
                if (line.isEmpty() || line.startsWithChar ('#')) continue;
                auto f = juce::StringArray::fromTokens (line, "|", "");
                const auto bit = characterNames().indexOf (f[0].trim());
                if (bit < 0) continue;
                for (int j = 1; j < f.size(); ++j)
                {
                    const auto role = partFiles().indexOf (f[j].upToFirstOccurrenceOf (":", false, false).trim());
                    for (auto n : juce::StringArray::fromTokens (f[j].fromFirstOccurrenceOf (":", false, false), ",", ""))
                        for (auto& fam : all)
                            if ((int) fam.part == role && fam.name == n.trim().toStdString()) fam.character |= 1u << bit;
                }
            }
            return all;
        }();
        return lib;
    }

    /** A kit: one family per role, chosen to go together (Patterns/kits.txt). A role can list a
        family per kind (a long, an offbeat and a rolling bass, say) and section figures. */
    struct Kit
    {
        std::string name;
        uint32_t moods = 0, styles = 0;
        std::array<std::vector<std::string>, (size_t) Part::count> parts;
    };
    inline const std::vector<Kit>& kits()
    {
        static const std::vector<Kit> all = []
        {
            std::vector<Kit> out;
            for (int i = 0; i < PatternData::namedResourceListSize; ++i)
            {
                if (juce::String (PatternData::originalFilenames[i]) != "kits.txt") continue;
                int size = 0;
                const auto* data = PatternData::getNamedResource (PatternData::namedResourceList[i], size);
                if (data == nullptr) continue;
                for (auto line : juce::StringArray::fromLines (juce::String::fromUTF8 (data, size)))
                {
                    line = line.trim();
                    if (line.isEmpty() || line.startsWithChar ('#')) continue;
                    auto f = juce::StringArray::fromTokens (line, "|", "");
                    if (f.size() < 3) continue;
                    Kit k;
                    k.name = f[0].trim().toStdString();
                    k.moods = detail::tagBits (f[1].trim(), "EHMKDRP");
                    k.styles = detail::tagBits (f[2].trim(), "UCPTYVO");
                    for (int j = 3; j < f.size(); ++j)   // role: family, family, ...
                    {
                        const auto role = partFiles().indexOf (f[j].upToFirstOccurrenceOf (":", false, false).trim());
                        if (role < 0) continue;
                        for (auto n : juce::StringArray::fromTokens (f[j].fromFirstOccurrenceOf (":", false, false), ",", ""))
                            if (n.trim().isNotEmpty()) k.parts[(size_t) role].push_back (n.trim().toStdString());
                    }
                    out.push_back (std::move (k));
                }
            }
            return out;
        }();
        return all;
    }

    /** A voice's bars and its octave shift. A bar is 16 steps (16ths), 12 (8th-note triplets) or
        24 (16th triplets); '!' before a step accents it, an apostrophe softens it (neither takes a step). */
    struct Voice
    {
        std::vector<std::string> bars, marks;   // marks: per step ' ', '!' or '\''
        int octave = 0;
    };
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
                std::string bar, mark;
                char pending = ' ';
                for (auto c : b)
                {
                    if (c == '!' || c == '\'') { pending = (char) c; continue; }
                    bar.push_back ((char) c);
                    mark.push_back (pending);
                    pending = ' ';
                }
                if (bar.size() != 12 && bar.size() != 24)
                {
                    if (bar.size() > 16) { bar.resize (16); mark.resize (16); }
                    while (bar.size() < 16) { bar.push_back ('.'); mark.push_back (' '); }
                }
                voice.bars.push_back (bar);
                voice.marks.push_back (mark);
            }
            if (voice.bars.empty()) { voice.bars.push_back (std::string (16, '.')); voice.marks.push_back (std::string (16, ' ')); }
            out.push_back (std::move (voice));
        }
        if (out.empty()) out.push_back ({ { std::string (16, '.') }, { std::string (16, ' ') }, 0 });
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
    inline std::vector<int> candidates (Part part, int kind, Mood mood, Style style, int section = -1)
    {
        std::vector<int> best;
        int bestScore = -1;
        const auto& lib = library();
        for (int i = 0; i < (int) lib.size(); ++i)
        {
            const auto& f = lib[(size_t) i];
            if (f.part != part || f.kind != kind) continue;
            if (section < 0 ? f.where != 0 : ((f.where >> section) & 1u) == 0) continue;   // section figures only where they belong
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

    /** The song's kit: one that fits the mood and the style (the style must match), chosen by the
        seed; nullptr when no kit fits the style. */
    inline const Kit* pickKit (Mood mood, Style style, uint32_t seed)
    {
        std::vector<const Kit*> best;
        int bestScore = -1;
        for (const auto& k : kits())
        {
            if (((k.styles >> (int) style) & 1u) == 0) continue;
            const auto score = (int) ((k.moods >> (int) mood) & 1u);
            if (score > bestScore) { bestScore = score; best.clear(); }
            if (score == bestScore) best.push_back (&k);
        }
        return best.empty() ? nullptr : best[hash (seed * 40503u + 17u) % best.size()];
    }
    /** The kit's family for a role and kind (a section figure first, where one is listed for the section). */
    inline const Family* kitFamily (const Kit& kit, Part part, int kind, int section)
    {
        const Family* plain = nullptr;
        for (const auto& name : kit.parts[(size_t) part])
            for (const auto& f : library())
                if (f.part == part && f.kind == kind && f.name == name)
                {
                    if (section >= 0 && ((f.where >> section) & 1u) != 0) return &f;
                    if (f.where == 0 && plain == nullptr) plain = &f;
                }
        return plain;
    }

    /** Where a family's notes start in its drop pattern (first voice, first bar, on a 16th grid). */
    inline std::array<bool, 16> onsets16 (const Family& f)
    {
        std::array<bool, 16> o {};
        const auto v = voices (f.level[2]).front();
        const auto& bar = v.bars.front();
        for (size_t k = 0; k < bar.size(); ++k)
            if (bar[k] != '.' && bar[k] != '-' && bar[k] != '~') o[(k * 16) / bar.size()] = true;
        return o;
    }
    /** How much two families' rhythms land on the same 16ths (0: they interlock, 1: in unison). */
    inline float overlap (const Family& a, const Family& b)
    {
        const auto x = onsets16 (a), y = onsets16 (b);
        int na = 0, nb = 0, both = 0;
        for (size_t k = 0; k < 16; ++k) { na += x[k]; nb += y[k]; both += x[k] && y[k]; }
        return na == 0 || nb == 0 ? 0.0f : (float) both / (float) std::min (na, nb);
    }

    /** A family's feel: 0 straight, 1 shuffled, 2 triplets. */
    inline int feelOf (const Family& f)
    {
        for (const auto& v : voices (f.level[2])) for (const auto& b : v.bars) if (b.size() == 12 || b.size() == 24) return 2;
        return f.swing > 0.01f ? 1 : 0;
    }
    /** Whether a family anticipates the next chord ('p' / 'P'). */
    inline bool pushes (const Family& f)
    {
        for (const auto& t : { f.level[1], f.level[2], f.high2 }) if (t.find ('p') != std::string::npos || t.find ('P') != std::string::npos) return true;
        return false;
    }

    /** Like pick, but among the best fits it prefers rhythms that interlock with the parts already
        chosen (`others`): an arp that fills the gaps of the bass, chords that leave both room. A
        section figure (a build riser, an intro version) is chosen where one fits that section. */
    inline const Family& pickFitted (Part part, int kind, Mood mood, Style style, uint32_t seed,
                                     const std::vector<const Family*>& others, int section = -1, uint32_t character = 0)
    {
        auto c = section >= 0 ? candidates (part, kind, mood, style, section) : std::vector<int> {};
        const auto sectionFigure = ! c.empty();
        if (c.empty()) c = candidates (part, kind, mood, style);
        if (c.empty()) c = candidates (part, 0, mood, style);
        if (c.empty()) { static const Family none; return none; }
        const auto& lib = library();
        // Coherence with the first reference (the bass): the same feel (straight, shuffled or
        // triplets), and for chord parts the same habit of pushing into the next chord, where the
        // library has such families
        if (! others.empty() && others.front() != nullptr && ! others.front()->name.empty() && ! sectionFigure && c.size() > 1)
        {
            const auto& ref = *others.front();
            auto keepIf = [&] (auto pred) { std::vector<int> k; for (auto i : c) if (pred (lib[(size_t) i])) k.push_back (i); if (! k.empty()) c = k; };
            keepIf ([&] (const Family& f) { return feelOf (f) == feelOf (ref); });
            if (part == Part::Chords || part == Part::Piano || part == Part::Strings || part == Part::Brass || part == Part::Choir)
                keepIf ([&] (const Family& f) { return pushes (f) == pushes (ref); });
        }
        // The song's character (its hook's): parts that feel the same, where the library has them
        if (character != 0 && ! sectionFigure && c.size() > 1)
        {
            std::vector<int> k;
            for (auto i : c) if ((lib[(size_t) i].character & character) != 0) k.push_back (i);
            if (! k.empty()) c = k;
        }
        if (! others.empty() && ! sectionFigure && c.size() > 1)
        {
            std::vector<std::pair<float, int>> scored;
            for (auto i : c)
            {
                float worst = 0.0f;
                for (const auto* o : others) if (o != nullptr && ! o->name.empty()) worst = std::max (worst, overlap (lib[(size_t) i], *o));
                scored.push_back ({ worst, i });
            }
            const auto best = std::min_element (scored.begin(), scored.end())->first;
            c.clear();
            for (const auto& [score, i] : scored) if (score <= best + 0.15f) c.push_back (i);
        }
        return lib[(size_t) c[hash (seed * 2654435761u + (uint32_t) part * 977u + (uint32_t) kind * 131u + (uint32_t) (section + 1) * 7919u) % c.size()]];
    }
}
