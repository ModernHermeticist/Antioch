#pragma once

// Suite overview: what's loaded where, and setup advice from the registry.
// Pure logic, so it's directly testable.

#include "Registry.h"
#include <juce_core/juce_core.h>

namespace suitecheck
{
    struct Advice { juce::String text; bool problem = true; };

    inline juce::String where (const suite::SuiteEntry& e)
    {
        return e.track.isNotEmpty() ? "'" + e.track + "'" : juce::String ("an unnamed track");
    }

    inline bool onMaster (const suite::SuiteEntry& e)
    {
        const auto t = e.track.toLowerCase();
        return t.isEmpty() || t.contains ("master") || t.contains ("main");   // unknown names get the benefit of the doubt
    }

    inline std::vector<Advice> check (const std::vector<suite::SuiteEntry>& entries)
    {
        using namespace suite;
        std::vector<Advice> out;
        auto count = [&entries] (const char* plugin) { int n = 0; for (auto& e : entries) n += e.plugin == plugin; return n; };

        if (count ("Ouroboros") > 1)
            out.push_back ({ "More than one Ouroboros is loaded: keep one, so the suite follows a single song map." });
        for (const auto& e : entries)
            if (e.plugin == "Ouroboros" && ! e.has (Status::hasMap))
                out.push_back ({ "Ouroboros has no song map yet: use Arrange, a template or Listen." });

        if (count ("Correspondence") == 0 && count ("Emerald Tablet") == 0)
            out.push_back ({ "No key source: load Correspondence (or Emerald Tablet) so Azoth, Antioch and Cause & Effect play in key." });

        bool kickShared = false, suiteKickUsed = false;
        for (const auto& e : entries)
        {
            if (e.plugin == "Kybalion")
            {
                if (e.has (Status::kickSource)) kickShared = true;
                if ((int) e.metrics[0] == 2) suiteKickUsed = true;
                if (e.has (Status::sidechainWanted) && ! e.has (Status::sidechainConnected))
                    out.push_back ({ "Kybalion on " + where (e) + " is set to duck from its sidechain, but nothing is routed: route the kick to it, or choose Suite kick." });
            }
            else if (e.plugin == "Solve et Coagula" && ! e.has (Status::sidechainConnected))
                out.push_back ({ "Solve et Coagula on " + where (e) + " has no vocal on its sidechain, so it does nothing." });
            else if (e.plugin == "Prima Materia" && ! e.has (Status::roleSet))
                out.push_back ({ "Prima Materia on " + where (e) + " has no role: pick one, or name the track (Kick, Bass, Pad, Lead, Vox...)." });
            else if ((e.plugin == "Lapis" || e.plugin == "Mentalism") && ! onMaster (e))
                out.push_back ({ e.plugin + " is on " + where (e) + ": it belongs on the master." });
        }
        if (suiteKickUsed && ! kickShared)
            out.push_back ({ "A Kybalion ducks to the suite kick, but no kick is shared: turn on Share kick in a Kybalion on the kick track." });

        if (out.empty())
            out.push_back ({ "Everything is set up.", false });
        return out;
    }
}
