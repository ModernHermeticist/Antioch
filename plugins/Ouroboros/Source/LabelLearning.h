#pragma once

// What the label autopilot learns from you, across songs (a small file in the suite's folder):
//  - fixes you keep: their checks are tried first next time (your defaults);
//  - fixes you keep undoing (or that the autopilot undid as no better): their steps get smaller,
//    and after three undos with no keeps the check is left alone;
//  - your taste: where your songs consistently sit against the label (always a little brighter,
//    a little more sub...), averaged over your milestones, as an offset on the label's tone the
//    Quintessences steer to (with its own switch).

#include "Alias.h"
#include "StyleLog.h"
#include <juce_core/juce_core.h>
#include <array>
#include <map>

namespace lab
{
    class Learning
    {
    public:
        struct Record { int kept = 0, undone = 0; };

        static juce::File file() { return suite::Alias::file ("Label autopilot memory.txt"); }

        void load()
        {
            records.clear();
            for (const auto& line : juce::StringArray::fromLines (file().loadFileAsString()))
            {
                const auto f = juce::StringArray::fromTokens (line, "\t", "");
                if (f.size() >= 3) records[f[0]] = { f[1].getIntValue(), f[2].getIntValue() };
            }
            loaded = true;
        }
        void save() const
        {
            juce::StringArray l;
            for (const auto& [name, r] : records) l.add (name.replaceCharacters ("\t\n", "  ") + "\t" + juce::String (r.kept) + "\t" + juce::String (r.undone));
            file().getParentDirectory().createDirectory();
            file().replaceWithText (l.joinIntoString ("\n") + "\n");
        }
        void record (const juce::String& check, bool kept)
        {
            if (! loaded) load();
            auto& r = records[stem (check)];
            (kept ? r.kept : r.undone)++;
            save();
        }
        /** Left alone: undone three times or more and never kept. */
        bool retired (const juce::String& check)
        {
            if (! loaded) load();
            const auto it = records.find (stem (check));
            return it != records.end() && it->second.undone >= 3 && it->second.kept == 0;
        }
        /** How big a step to take: smaller each time it was undone (never below a quarter). */
        float stepScale (const juce::String& check)
        {
            if (! loaded) load();
            const auto it = records.find (stem (check));
            if (it == records.end()) return 1.0f;
            return juce::jmax (0.25f, 1.0f / (1.0f + (float) it->second.undone - 0.5f * (float) it->second.kept));
        }
        /** Kept before: tried first. */
        bool favoured (const juce::String& check)
        {
            if (! loaded) load();
            const auto it = records.find (stem (check));
            return it != records.end() && it->second.kept >= 2 && it->second.kept > it->second.undone;
        }
        const std::map<juce::String, Record>& all() { if (! loaded) load(); return records; }

        /** Your taste: each milestone's drop tone against the label's, averaged (needs three). */
        static std::array<float, 10> tasteOffset (int& count)
        {
            std::array<float, 10> sum {};
            count = 0;
            for (const auto& e : suite::StyleLog::read())
            {
                if (e.kind != "taste") continue;
                const auto v = juce::StringArray::fromTokens (e.get ("offset"), ",", "");
                if (v.size() != 10) continue;
                for (int k = 0; k < 10; ++k) sum[(size_t) k] += v[k].getFloatValue();
                ++count;
            }
            if (count < 3) return {};
            for (auto& s : sum) s = juce::jlimit (-3.0f, 3.0f, s / (float) count);
            return sum;
        }
        static void recordTaste (const std::array<float, 10>& offset, const juce::String& milestone)
        {
            juce::StringArray v;
            for (auto x : offset) v.add (juce::String (x, 2));
            suite::StyleLog::add ("taste", { { "offset", v.joinIntoString (",") }, { "milestone", milestone } });
        }

    private:
        std::map<juce::String, Record> records;
        bool loaded = false;
        /** "Build tension at bar 17" and "Build tension at bar 57" are the same check. */
        static juce::String stem (const juce::String& check) { return check.upToFirstOccurrenceOf (" at bar", false, false).upToFirstOccurrenceOf (" (bar", false, false); }
    };
}
