#pragma once

// Tools for the pattern library (included at the end of PatternLibrary.h):
//   - your favourites and bans, and what your edits taught it (a text file in the suite folder)
//   - a quality score per family (range, repetition, chord tones, rhythmic interest)
//   - how a kit's parts interlock, and how many of them push
//   - a validator for the role files and kits (run by the tests: bad lines fail the build)
//   - a coverage report (mood x style gaps, near-duplicate families)
//   - a kit generator: new kits matched on character, feel and register, ranked by quality and interlock
//   - your own kits (My kits.txt), loaded with the built-in ones

#include "Alias.h"
#include <map>
#include <set>

namespace patterns
{
    //==============================================================================
    // Favourites, bans and learned edits
    struct Learned
    {
        std::set<std::string> favourite, banned;                // "kit:Name", "lead:Family name", ...
        std::map<int, float> density, velocity, octave, gate;   // per part: from your edits
    };
    inline juce::File& learnedFileOverride() { static juce::File f; return f; }
    inline juce::File learnedFile() { return learnedFileOverride() != juce::File() ? learnedFileOverride() : suite::Alias::file ("Writing prefs.txt"); }
    inline Learned& learnedStore() { static Learned l; return l; }
    inline bool& learnedLoaded() { static bool b = false; return b; }
    inline Learned& learned()
    {
        auto& l = learnedStore();
        if (! learnedLoaded())
        {
            learnedLoaded() = true;
            l = {};
            for (const auto& line : juce::StringArray::fromLines (learnedFile().loadFileAsString()))
            {
                const auto parts = juce::StringArray::fromTokens (line, "\t", "");
                if (parts.size() < 2) continue;
                if (parts[0] == "fav") l.favourite.insert (parts[1].toStdString());
                else if (parts[0] == "ban") l.banned.insert (parts[1].toStdString());
                else if (parts[0] == "edit" && parts.size() >= 6)
                {
                    const auto p = parts[1].getIntValue();
                    l.density[p] = (float) parts[2].getDoubleValue(); l.velocity[p] = (float) parts[3].getDoubleValue();
                    l.octave[p] = (float) parts[4].getDoubleValue(); l.gate[p] = (float) parts[5].getDoubleValue();
                }
            }
        }
        return l;
    }
    /** Tests: use another file (and forget what was loaded). */
    inline void setLearnedFileForTests (const juce::File& f) { learnedFileOverride() = f; learnedLoaded() = false; }
    inline void saveLearned()
    {
        const auto& l = learned();
        juce::StringArray lines { "# Correspondence: your favourite and banned kits and families, and what your edits taught it" };
        for (const auto& f : l.favourite) lines.add ("fav\t" + juce::String (f));
        for (const auto& b : l.banned) lines.add ("ban\t" + juce::String (b));
        for (const auto& [p, d] : l.density)
            lines.add ("edit\t" + juce::String (p) + "\t" + juce::String (d, 3) + "\t" + juce::String (l.velocity.at (p), 2) + "\t"
                       + juce::String (l.octave.at (p), 2) + "\t" + juce::String (l.gate.at (p), 3));
        learnedFile().getParentDirectory().createDirectory();
        learnedFile().replaceWithText (lines.joinIntoString ("\n") + "\n");
    }
    inline std::string familyKey (const Family& f) { return partFiles()[(int) f.part].toStdString() + ":" + f.name; }
    inline std::string kitKey (const Kit& k) { return "kit:" + k.name; }
    /** 1 favourite, -1 banned, 0 neither. */
    inline void setFavourite (const std::string& key, int how)
    {
        auto& l = learned();
        l.favourite.erase (key); l.banned.erase (key);
        if (how > 0) l.favourite.insert (key);
        if (how < 0) l.banned.insert (key);
        saveLearned();
    }
    inline int favourite (const std::string& key)
    {
        const auto& l = learned();
        return l.favourite.count (key) ? 1 : l.banned.count (key) ? -1 : 0;
    }

    //==============================================================================
    // Quality

    /** The pitch steps a pattern text takes (scale-relative guesses, for scoring: chord tones as
        0/3/5/7, steps as +-1/2, octave letters +12). */
    inline std::vector<int> contour (const std::string& text)
    {
        std::vector<int> out;
        int last = 0;
        for (const auto& v : voices (text))
            for (const auto& b : v.bars)
                for (auto c : b)
                {
                    int p;
                    switch (c)
                    {
                        case 'r': p = 0; break; case 't': p = 4; break; case 'f': p = 7; break; case 's': p = 10; break;
                        case 'R': p = 12; break; case 'T': p = 16; break; case 'F': p = 19; break; case 'S': p = 22; break;
                        case 'l': p = -12; break; case '9': p = 14; break; case '4': p = 5; break;
                        case 'u': p = last + 2; break; case 'd': p = last - 2; break; case 'k': case ',': p = last; break;
                        case 'n': case 'N': p = last + 1; break;
                        case 'a': case 'A': case 'b': case 'c': case 'C': case 'o': case 'p': case 'P': case 'z': p = c == 'A' ? 12 : 0; break;
                        default: continue;
                    }
                    out.push_back (p);
                    last = p;
                }
        return out;
    }
    inline int onsetCount (const std::string& text)
    {
        int n = 0;
        for (const auto& v : voices (text)) for (const auto& b : v.bars) for (auto c : b) if (c != '.' && c != '-' && c != '~') ++n;
        return n;
    }

    /** 23: a family's quality, 0-1: rhythmic interest (onsets neither empty nor wall-to-wall, some
        off the beat), melodic shape for melodic roles (range, not too repetitive, leaps
        recovered), chord tones on strong steps, and growth between its energies. */
    inline float quality (const Family& f)
    {
        float score = 1.0f;
        const auto& mid = f.level[1].empty() ? f.level[2] : f.level[1];
        const auto bars = (int) voices (mid).front().bars.size();
        const auto density = (float) onsetCount (mid) / (float) std::max (1, 16 * bars);
        if (density < 0.05f) score -= 0.35f;
        if (density > 0.95f) score -= 0.15f;
        // growth: the high energy has at least what the low has
        if (onsetCount (f.level[2]) < onsetCount (f.level[0])) score -= 0.1f;
        const auto melodic = f.part == Part::Lead || f.part == Part::Counter || f.part == Part::Chops || f.part == Part::Candy;
        if (melodic)
        {
            const auto c = contour (f.level[2]);
            if (! c.empty())
            {
                const auto [lo, hi] = std::minmax_element (c.begin(), c.end());
                const auto range = *hi - *lo;
                if (range < 3) score -= 0.2f;        // a monotone
                if (range > 22) score -= 0.15f;      // too wide to sing
                int repeats = 0, bad = 0;
                for (size_t i = 1; i < c.size(); ++i)
                {
                    if (c[i] == c[i - 1]) ++repeats;
                    if (i > 1 && std::abs (c[i - 1] - c[i - 2]) >= 8 && (c[i - 1] > c[i - 2]) == (c[i] > c[i - 1]) && std::abs (c[i] - c[i - 1]) >= 3) ++bad;
                }
                if (c.size() > 4 && (float) repeats / (float) c.size() > 0.6f) score -= 0.15f;
                score -= 0.05f * (float) std::min (bad, 4);
            }
        }
        // strong steps (beats) on chord letters for the bass and chord roles
        if (f.part == Part::Bass || f.part == Part::Chords || f.part == Part::Piano)
        {
            int strong = 0, chordish = 0;
            const auto vs = voices (f.level[2]);
            for (const auto& b : vs.front().bars)
                for (size_t k = 0; k < b.size(); k += std::max<size_t> (1, b.size() / 4))
                    if (b[k] != '.' && b[k] != '-') { ++strong; if (std::string ("rtfsRTFSlcCopPa,z").find (b[k]) != std::string::npos) ++chordish; }
            if (strong > 0 && (float) chordish / (float) strong < 0.5f) score -= 0.15f;
        }
        if (f.fill.empty()) score -= 0.05f;
        return juce::jlimit (0.0f, 1.0f, score);
    }

    /** 10: how a set of families' drop rhythms fill the bar together: the share of 16ths
        covered, less a penalty for stacking (two or more parts starting on the same 16th
        beyond the downbeats). 0-1, higher interlocks better. */
    inline float interlock (const std::vector<const Family*>& fams)
    {
        std::array<int, 16> hits {};
        for (const auto* f : fams) if (f != nullptr && ! f->name.empty()) { const auto o = onsets16 (*f); for (size_t k = 0; k < 16; ++k) hits[k] += o[k]; }
        int covered = 0, stacked = 0;
        for (size_t k = 0; k < 16; ++k) { covered += hits[k] > 0; if (k % 4 != 0 && hits[k] > 1) stacked += hits[k] - 1; }
        return juce::jlimit (0.0f, 1.0f, (float) covered / 16.0f - 0.04f * (float) stacked + 0.2f);
    }

    /** The families a kit lists, resolved (one per role: its first). */
    inline std::vector<const Family*> kitFamilies (const Kit& k)
    {
        std::vector<const Family*> out;
        for (int r = 0; r < (int) Part::count; ++r)
            for (const auto& name : k.parts[(size_t) r])
            {
                const Family* found = nullptr;
                for (const auto& f : library()) if ((int) f.part == r && f.name == name) { found = &f; break; }
                if (found != nullptr) { out.push_back (found); break; }
            }
        return out;
    }
    /** 11: how many of a kit's parts push into the next chord. */
    inline int pushers (const Kit& k)
    {
        int n = 0;
        for (const auto* f : kitFamilies (k)) if (pushes (*f)) ++n;
        return n;
    }
    /** A kit's score: its families' quality, its core rhythm's interlock, a push budget of two,
        and your favourites. */
    inline float kitScore (const Kit& k)
    {
        const auto fams = kitFamilies (k);
        if (fams.empty()) return 0.0f;
        float q = 0.0f;
        for (const auto* f : fams) q += quality (*f);
        q /= (float) fams.size();
        std::vector<const Family*> core;
        for (const auto* f : fams) if (f->part == Part::Bass || f->part == Part::Arp || f->part == Part::Chords) core.push_back (f);
        auto s = 0.6f * q + 0.4f * interlock (core) - 0.08f * (float) std::max (0, pushers (k) - 2);
        s += 0.3f * (float) favourite (kitKey (k));
        return s;
    }

    //==============================================================================
    // 51: the validator
    /** Problems in the role files and kits: lines that don't parse, bars of the wrong length,
        empty energies, unknown letters, kits naming families that don't exist. */
    inline juce::StringArray validate()
    {
        juce::StringArray problems;
        static const std::string letters = "rtfsRTFSl94udknNaAbcCo,z~-.pP";
        for (int i = 0; i < PatternData::namedResourceListSize; ++i)
        {
            const juce::String file (PatternData::originalFilenames[i]);
            if (file == "characters.txt" || file == "kits.txt") continue;
            int size = 0;
            const auto* data = PatternData::getNamedResource (PatternData::namedResourceList[i], size);
            if (data == nullptr) continue;
            int lineNo = 0;
            for (auto line : juce::StringArray::fromLines (juce::String::fromUTF8 (data, size)))
            {
                ++lineNo;
                line = line.trim();
                if (line.isEmpty() || line.startsWithChar ('#')) continue;
                auto f = juce::StringArray::fromTokens (line, "|", "");
                const auto where = file + ":" + juce::String (lineNo) + " (" + f[0].trim() + ")";
                if (f.size() < 10) { problems.add (where + ": fewer than 10 fields"); continue; }
                for (int k = 6; k < juce::jmin (f.size(), 12); ++k)
                {
                    if (k == 10 || k == 11) { if (f[k].trim() == "-") continue; }
                    const auto text = f[k].removeCharacters (" ");
                    if (text.isEmpty()) { if (k <= 8) problems.add (where + ": empty energy " + juce::String (k - 6)); continue; }
                    for (auto v : juce::StringArray::fromTokens (text, "+", ""))
                    {
                        while (v.startsWithChar ('<') || v.startsWithChar ('>')) v = v.substring (1);
                        for (auto b : juce::StringArray::fromTokens (v, "/", ""))
                        {
                            int steps = 0;
                            for (auto c : b)
                            {
                                if (c == '!' || c == '\'' || c == '^' || c == '_') continue;
                                if (letters.find ((char) c) == std::string::npos) problems.add (where + ": unknown letter '" + juce::String::charToString (c) + "'");
                                ++steps;
                            }
                            if (steps != 16 && steps != 12 && steps != 24) problems.add (where + ": a bar of " + juce::String (steps) + " steps");
                        }
                    }
                }
            }
        }
        for (const auto& k : kits())
            for (int r = 0; r < (int) Part::count; ++r)
                for (const auto& name : k.parts[(size_t) r])
                    if (std::none_of (library().begin(), library().end(), [&] (const Family& f) { return (int) f.part == r && f.name == name; }))
                        problems.add ("kit " + juce::String (k.name) + ": no " + partFiles()[r] + " family '" + juce::String (name) + "'");
        return problems;
    }

    //==============================================================================
    // 53: the coverage report
    /** How alike two families are, 0-1: their drop rhythms and their shapes. */
    inline float similarity (const Family& a, const Family& b)
    {
        if (a.part != b.part) return 0.0f;
        const auto rhythm = 1.0f - std::abs (overlap (a, b) - 1.0f);
        const auto ca = contour (a.level[2]), cb = contour (b.level[2]);
        if (ca.empty() || cb.empty()) return rhythm;
        const auto n = std::min (ca.size(), cb.size());
        int same = 0;
        for (size_t i = 1; i < n; ++i) same += ((ca[i] > ca[i - 1]) - (ca[i] < ca[i - 1])) == ((cb[i] > cb[i - 1]) - (cb[i] < cb[i - 1]));
        const auto shape = n > 1 ? (float) same / (float) (n - 1) : 1.0f;
        const auto x = onsets16 (a), y = onsets16 (b);
        int eq = 0; for (size_t k = 0; k < 16; ++k) eq += x[k] == y[k];
        return 0.5f * ((float) eq / 16.0f) + 0.5f * shape * rhythm;
    }
    inline juce::String coverageReport()
    {
        juce::StringArray out { "Pattern library coverage", "" };
        const auto& lib = library();
        out.add (juce::String ((int) lib.size()) + " families, " + juce::String ((int) kits().size()) + " kits");
        for (int r = 0; r < (int) Part::count; ++r)
        {
            juce::StringArray gaps;
            int count = 0;
            for (const auto& f : lib) if ((int) f.part == r) ++count;
            for (int m = 0; m < (int) Mood::count; ++m)
                for (int s = 0; s < (int) Style::count; ++s)
                {
                    bool any = false;
                    for (const auto& f : lib) if ((int) f.part == r && f.tagged ((Mood) m) && f.tagged ((Style) s)) { any = true; break; }
                    if (! any) gaps.add (moodNames()[m] + " x " + styleNames()[s]);
                }
            out.add ("");
            out.add (partFiles()[r] + ": " + juce::String (count) + " families" + (gaps.isEmpty() ? juce::String (", every mood x style covered") : ", gaps: " + juce::String (gaps.size())));
            if (! gaps.isEmpty()) out.add ("  " + gaps.joinIntoString (", "));
        }
        out.add ("");
        out.add ("Near-duplicates (similarity 0.9 or more):");
        int dupes = 0;
        for (size_t i = 0; i < lib.size(); ++i)
            for (size_t j = i + 1; j < lib.size(); ++j)
                if (lib[i].part == lib[j].part && lib[i].kind == lib[j].kind && similarity (lib[i], lib[j]) >= 0.9f && dupes < 60)
                {
                    out.add ("  " + partFiles()[(int) lib[i].part] + ": " + juce::String (lib[i].name) + " ~ " + juce::String (lib[j].name));
                    ++dupes;
                }
        if (dupes == 0) out.add ("  none");
        out.add ("");
        out.add ("Weakest families (quality under 0.7: rewrite these first):");
        int weak = 0;
        for (const auto& f : lib) if (quality (f) < 0.7f && weak < 60) { out.add ("  " + juce::String (familyKey (f)) + " (" + juce::String (quality (f), 2) + ")"); ++weak; }
        if (weak == 0) out.add ("  none");
        return out.joinIntoString ("\n");
    }

    //==============================================================================
    // 55: the kit generator, and your own kits
    inline juce::File userKitsFile() { return userKitsPath(); }

    /** New kits for a mood and style: for each role, the best families by quality that share the
        hook's character and the bass's feel, with the core interlocking; ranked by kitScore. */
    inline std::vector<Kit> generateKits (Mood mood, Style style, int howMany, uint32_t seed)
    {
        std::vector<Kit> out;
        for (int attempt = 0; attempt < howMany * 6 && (int) out.size() < howMany; ++attempt)
        {
            Kit k;
            k.name = std::string (moodNames()[(int) mood].toStdString()) + " " + styleNames()[(int) style].toStdString() + " " + std::to_string (attempt + 1) + " (generated)";
            k.moods = 1u << (int) mood; k.styles = 1u << (int) style;
            const auto s = hash (seed + (uint32_t) attempt * 7919u);
            const auto& lead = pick (Part::Lead, 0, mood, style, s);
            std::vector<const Family*> chosen;
            for (int r = 0; r < (int) Part::count; ++r)
            {
                const auto part = (Part) r;
                const auto kind = part == Part::Bass ? 1 : part == Part::Arp ? 1 : part == Part::Chords ? 1 : 0;
                const auto& f = r == (int) Part::Lead ? lead : pickFitted (part, kind, mood, style, s, chosen, -1, lead.character);
                if (f.name.empty() || favourite (familyKey (f)) < 0) continue;
                if (quality (f) < 0.5f) continue;
                k.parts[(size_t) r].push_back (f.name);
                chosen.push_back (&f);
            }
            if (std::none_of (out.begin(), out.end(), [&] (const Kit& o) { return o.parts == k.parts; }))
                out.push_back (std::move (k));
        }
        std::stable_sort (out.begin(), out.end(), [] (const Kit& a, const Kit& b) { return kitScore (a) > kitScore (b); });
        if ((int) out.size() > howMany) out.resize ((size_t) howMany);
        return out;
    }
    /** A kit as a line of kits.txt. */
    inline juce::String kitLine (const Kit& k)
    {
        juce::String moods, styles;
        for (int m = 0; m < 7; ++m) if ((k.moods >> m) & 1u) moods << juce::String::charToString ("EHMKDRP"[m]);
        for (int s = 0; s < 7; ++s) if ((k.styles >> s) & 1u) styles << juce::String::charToString ("UCPTYVO"[s]);
        juce::StringArray roles;
        for (int r = 0; r < (int) Part::count; ++r)
            if (! k.parts[(size_t) r].empty())
            {
                juce::StringArray names;
                for (const auto& n : k.parts[(size_t) r]) names.add (n);
                roles.add (partFiles()[r] + ": " + names.joinIntoString (", "));
            }
        return juce::String (k.name) + " | " + moods + " | " + styles + " | " + roles.joinIntoString (" | ");
    }
}
