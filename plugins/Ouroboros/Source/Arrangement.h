#pragma once

// Editing operations on a section map. Pure functions, no UI or audio.
// Invariants kept by every operation: sections sorted, first starts at bar 1,
// every section at least 1 bar long, all starts <= totalBars.

#include "SectionBus.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace arrangement
{
    using suite::SectionMap;
    using suite::SectionType;

    struct Template { const char* name; int totalBars; std::vector<std::pair<int, SectionType>> sections; };

    inline const std::vector<Template>& templates()
    {
        using T = SectionType;
        static const std::vector<Template> t
        {
            { "Uplifting: classic (208 bars)", 208,
              { { 1, T::Intro }, { 33, T::Groove }, { 65, T::Breakdown }, { 97, T::Build }, { 113, T::Drop }, { 177, T::Outro } } },
            { "Uplifting: two breakdowns (232 bars)", 232,
              { { 1, T::Intro }, { 33, T::Groove }, { 65, T::Breakdown }, { 81, T::Build }, { 89, T::Drop },
                { 121, T::Breakdown }, { 153, T::Build }, { 169, T::Drop }, { 201, T::Outro } } },
            { "Radio edit (88 bars)", 88,
              { { 1, T::Intro }, { 9, T::Groove }, { 25, T::Breakdown }, { 41, T::Build }, { 49, T::Drop }, { 81, T::Outro } } },
            { "Short loop test (16 bars)", 16,
              { { 1, T::Groove }, { 5, T::Breakdown }, { 9, T::Build }, { 13, T::Drop } } },
        };
        return t;
    }

    inline SectionMap fromTemplate (const Template& t)
    {
        SectionMap m;
        m.totalBars = t.totalBars;
        for (const auto& [bar, type] : t.sections)
            if (m.count < SectionMap::maxSections)
                m.sections[(size_t) m.count++] = { bar, type };
        return m;
    }

    //==============================================================================
    // Your own templates: song maps (with their energy curve) saved as files.

    inline juce::File userTemplateFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Antioch Suite").getChildFile ("Templates");
    }

    inline juce::String toText (const SectionMap& m)
    {
        juce::StringArray parts { juce::String (m.totalBars) };
        for (int i = 0; i < m.count; ++i)
            parts.add (juce::String (m.sections[(size_t) i].startBar) + ":" + juce::String ((int) m.sections[(size_t) i].type) + ":"
                       + juce::String (m.sections[(size_t) i].energy));
        return parts.joinIntoString (" ");
    }

    inline SectionMap fromText (const juce::String& text)
    {
        SectionMap m;
        const auto parts = juce::StringArray::fromTokens (text, " ", "");
        if (parts.isEmpty()) return m;
        m.totalBars = juce::jlimit (1, 1024, parts[0].getIntValue());
        for (int i = 1; i < parts.size() && m.count < SectionMap::maxSections; ++i)
        {
            const auto f = juce::StringArray::fromTokens (parts[i], ":", "");
            if (f.size() < 2) continue;
            m.sections[(size_t) m.count++] = { juce::jmax (1, f[0].getIntValue()),
                                               (SectionType) juce::jlimit (0, (int) SectionType::count - 1, f[1].getIntValue()),
                                               f.size() > 2 ? juce::jlimit (-1, 100, f[2].getIntValue()) : -1 };
        }
        if (m.count > 0) m.sections[0].startBar = 1;
        return m;
    }

    inline bool saveUserTemplate (const juce::File& folder, const juce::String& name, const SectionMap& m)
    {
        folder.createDirectory();
        return folder.getChildFile (juce::File::createLegalFileName (name) + ".map").replaceWithText (toText (m));
    }

    inline std::vector<std::pair<juce::String, SectionMap>> loadUserTemplates (const juce::File& folder)
    {
        std::vector<std::pair<juce::String, SectionMap>> out;
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.map"))
            if (const auto m = fromText (f.loadFileAsString()); m.count > 0)
                out.push_back ({ f.getFileNameWithoutExtension(), m });
        std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        return out;
    }

    /** Index of the section containing `bar`, or -1. */
    inline int sectionAt (const SectionMap& m, int bar)
    {
        if (m.count == 0 || bar < 1 || bar > m.totalBars) return -1;
        int i = 0;
        while (i + 1 < m.count && m.sections[(size_t) i + 1].startBar <= bar) ++i;
        return i;
    }

    /** Starts a new section of `type` at `bar` (splitting the section it falls in).
        If a section already starts there, its type is changed instead. Returns its index. */
    inline int insertAt (SectionMap& m, int bar, SectionType type)
    {
        bar = juce::jlimit (1, juce::jmax (1, m.totalBars), bar);
        if (m.count == 0)
        {
            m.sections[0] = { 1, type };
            m.count = 1;
            if (m.totalBars < 1) m.totalBars = 64;
            return 0;
        }
        for (int i = 0; i < m.count; ++i)
            if (m.sections[(size_t) i].startBar == bar)
            {
                m.sections[(size_t) i].type = type;
                return i;
            }
        if (m.count >= SectionMap::maxSections)
            return -1;

        const auto at = sectionAt (m, bar) + 1;
        for (int i = m.count; i > at; --i)
            m.sections[(size_t) i] = m.sections[(size_t) i - 1];
        m.sections[(size_t) at] = { bar, type };
        ++m.count;
        return at;
    }

    /** Removes a section; the previous one (or the next, for the first) absorbs its bars. */
    inline void remove (SectionMap& m, int index)
    {
        if (index < 0 || index >= m.count || m.count <= 1)
            return;
        if (index == 0)
            m.sections[1].startBar = 1;
        for (int i = index; i < m.count - 1; ++i)
            m.sections[(size_t) i] = m.sections[(size_t) i + 1];
        --m.count;
    }

    /** Moves the start of section `index` (> 0), keeping at least 1 bar on each side. */
    inline void moveStart (SectionMap& m, int index, int newStart)
    {
        if (index <= 0 || index >= m.count)
            return;
        const auto lo = m.sections[(size_t) index - 1].startBar + 1;
        const auto hi = (index + 1 < m.count ? m.sections[(size_t) index + 1].startBar : m.totalBars + 1) - 1;
        m.sections[(size_t) index].startBar = juce::jlimit (lo, juce::jmax (lo, hi), newStart);
    }

    /** Changes the song length; sections that would start past the end are dropped. */
    inline void setTotalBars (SectionMap& m, int bars)
    {
        m.totalBars = juce::jlimit (1, 1024, bars);
        while (m.count > 1 && m.sections[(size_t) m.count - 1].startBar > m.totalBars)
            --m.count;
    }

    /** Rounds a bar to the nearest multiple of `snap` bars from bar 1 (1, 1+snap, ...). */
    inline int snapBar (double bar, int snap)
    {
        snap = juce::jmax (1, snap);
        return (int) std::round ((bar - 1.0) / snap) * snap + 1;
    }

    inline juce::String toString (const SectionMap& m)
    {
        juce::StringArray parts;
        for (int i = 0; i < m.count; ++i)
            parts.add (juce::String (m.sections[(size_t) i].startBar) + ":" + suite::sectionName (m.sections[(size_t) i].type));
        return parts.joinIntoString (" ") + " /" + juce::String (m.totalBars);
    }

    //==============================================================================
    // Arrange: build a map from a few choices, on 8-bar phrases.

    struct GenSettings
    {
        int totalBars = 208;        // rounded to 8-bar phrases
        int drops = 2;              // 1..3
        bool djIntro = true;        // 32-bar intro and outro (else 16)
        bool longBreakdown = true;  // 32-bar first breakdown + 16-bar build (else 16 + 8)
    };

    inline SectionMap generate (GenSettings g)
    {
        using T = SectionType;
        constexpr int phrase = 8;
        g.drops = juce::jlimit (1, 3, g.drops);
        const auto target = juce::jlimit (1, 128, (int) std::round (g.totalBars / (double) phrase));

        struct Part { T type; int len, floor; int growRank; int shrinkRank; };   // lengths in phrases; lower rank = changed first
        std::vector<Part> parts;
        const auto edge = g.djIntro ? 4 : 2;
        const auto bd = g.longBreakdown ? 4 : 2, build = g.longBreakdown ? 2 : 1;
        parts.push_back ({ T::Intro, edge, 1, 4, 2 });
        parts.push_back ({ T::Groove, 4, 1, 1, 0 });
        for (int d = 0; d < g.drops; ++d)
        {
            parts.push_back ({ T::Breakdown, d == 0 ? bd : std::max (1, bd / 2), 1, d == 0 ? 2 : 6, d == 0 ? 3 : 1 });
            parts.push_back ({ T::Build, build, 1, 5, 5 });
            parts.push_back ({ T::Drop, 4, 1, d == g.drops - 1 ? 0 : 3, 4 });
        }
        parts.push_back ({ T::Outro, edge, 1, 4, 2 });

        auto sum = [&] { int n = 0; for (auto& p : parts) n += p.len; return n; };

        // Too long: shrink groove, later breakdowns, intro/outro, first breakdown, drops, builds (in that
        // order), one phrase at a time. Pass 1 keeps drops at 32 bars and the rest at 16; pass 2 allows
        // 16-bar drops and 8-bar sections; pass 3 lets drops go down to 8 bars.
        for (int pass = 0; pass < 3; ++pass)
            for (int rank = 0; rank <= 5 && sum() > target; ++rank)
            {
                bool changed = true;
                while (sum() > target && changed)
                {
                    changed = false;
                    for (auto it = parts.rbegin(); it != parts.rend() && sum() > target; ++it)   // later parts first
                    {
                        const auto floor = it->type == T::Drop ? (pass == 0 ? 4 : pass == 1 ? 2 : 1) : (pass == 0 ? 2 : 1);
                        if (it->shrinkRank == rank && it->len > floor) { --it->len; changed = true; }
                    }
                }
            }
        // Still too long for this many sections: drop a breakdown/build/drop group (never the last drop).
        while (sum() > target && g.drops > 1)
        {
            --g.drops;
            parts.erase (parts.begin() + 2, parts.begin() + 5);
        }
        while (sum() > target && parts.size() > 1)
        {
            // Tiny songs: lose the outro, then the intro, then the groove.
            auto it = std::find_if (parts.begin(), parts.end(), [] (const Part& p) { return p.type == T::Outro; });
            if (it == parts.end()) it = std::find_if (parts.begin(), parts.end(), [] (const Part& p) { return p.type == T::Intro || p.type == T::Groove; });
            if (it == parts.end()) break;
            parts.erase (it);
        }

        // Too short: grow by phrase, round-robin over the growable parts in rank order.
        while (sum() < target)
        {
            bool grew = false;
            for (int rank = 0; rank <= 9 && sum() < target; ++rank)
                for (auto& p : parts)
                {
                    const auto cap = p.type == T::Drop || p.type == T::Groove ? 8 : p.type == T::Build ? 4 : 6;
                    if (p.growRank == rank && sum() < target && p.len < cap) { ++p.len; grew = true; }
                }
            if (! grew)   // everything at its longest: the last drop takes the rest
            {
                for (auto it = parts.rbegin(); it != parts.rend(); ++it)
                    if (it->type == T::Drop) { it->len += target - sum(); break; }
                break;
            }
        }

        SectionMap m;
        int bar = 1;
        for (const auto& p : parts)
        {
            if (m.count >= SectionMap::maxSections) break;
            m.sections[(size_t) m.count++] = { bar, p.type };
            bar += p.len * phrase;
        }
        m.totalBars = bar - 1;
        return m;
    }
}
