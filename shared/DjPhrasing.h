#pragma once

// DJ phrasing: DJs mix in phrases of 16 and 32 bars, so the intro and outro should be whole
// 16-bar phrases and every section should start on the 8-bar grid. Pure logic: Mentalism's
// checklist reports it, Ouroboros applies the fix.

#include "SectionBus.h"
#include <juce_core/juce_core.h>

namespace suite::dj
{
    /** What's off, in plain words (empty = DJ-friendly). */
    inline juce::StringArray phrasingIssues (const SectionMap& m)
    {
        juce::StringArray issues;
        if (m.count == 0) return issues;
        auto len = [&m] (int i) { return m.endBar (i) - m.sections[(size_t) i].startBar; };
        if (m.sections[0].type == SectionType::Intro && len (0) % 16 != 0)
            issues.add ("intro is " + juce::String (len (0)) + " bars (DJs count in 16s)");
        const auto last = m.count - 1;
        if (last > 0 && m.sections[(size_t) last].type == SectionType::Outro && len (last) % 16 != 0)
            issues.add ("outro is " + juce::String (len (last)) + " bars");
        for (int i = 1; i < m.count; ++i)
            if ((m.sections[(size_t) i].startBar - 1) % 8 != 0)
                issues.add (juce::String (sectionName (m.sections[(size_t) i].type)) + " starts on bar "
                            + juce::String (m.sections[(size_t) i].startBar) + ", off the 8-bar grid");
        return issues;
    }

    /** The map with whole-phrase intro/outro (16 or 32 bars, the nearest) and every section
        start snapped to the 8-bar grid; sections keep their order and at least 8 bars. */
    inline SectionMap fixPhrasing (const SectionMap& m)
    {
        if (m.count == 0) return m;
        SectionMap f = m;
        auto len = [&m] (int i) { return m.endBar (i) - m.sections[(size_t) i].startBar; };
        auto round16 = [] (int bars) { return juce::jmax (16, (int) std::round (bars / 16.0) * 16); };
        auto round8 = [] (int bar) { return (int) std::round ((bar - 1) / 8.0) * 8 + 1; };

        int bar = 1;
        for (int i = 0; i < m.count; ++i)
        {
            f.sections[(size_t) i].startBar = bar;
            int length;
            if (i == 0 && m.sections[0].type == SectionType::Intro) length = round16 (len (0));
            else if (i == m.count - 1 && i > 0 && m.sections[(size_t) i].type == SectionType::Outro) length = round16 (len (i));
            else length = juce::jmax (8, round8 (m.sections[(size_t) i].startBar + len (i)) - round8 (m.sections[(size_t) i].startBar));
            bar += length;
        }
        f.totalBars = bar - 1;
        return f;
    }
}
