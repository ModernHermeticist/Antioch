#pragma once

// Editing operations on a section map. Pure functions, no UI or audio.
// Invariants kept by every operation: sections sorted, first starts at bar 1,
// every section at least 1 bar long, all starts <= totalBars.

#include "SectionBus.h"
#include <juce_core/juce_core.h>
#include <vector>

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
}
