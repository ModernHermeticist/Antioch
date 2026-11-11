#pragma once

// Suite overview: what's loaded where, and setup advice from the registry.
// Pure logic, so it's directly testable.

#include "Registry.h"
#include "SectionBus.h"
#include "KeyBus.h"
#include "Music.h"
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

    /** Features that work against each other with this map. */
    inline void collisions (const std::vector<suite::SuiteEntry>& entries, const suite::SectionMap& map, std::vector<Advice>& out)
    {
        using namespace suite;
        bool vocal = false, fills = false;
        for (const auto& e : entries)
        {
            if (e.plugin == "Prima Materia" && (int) e.metrics[0] == 6) vocal = true;
            if (e.plugin == "Kybalion" && e.has (Status::fillsOn)) fills = true;
        }
        if (map.finalLift != 0 && vocal)
            out.push_back ({ "The final drop goes up " + juce::String (map.finalLift) + (map.finalLift == 1 ? " semitone" : " semitones")
                             + ", but a vocal track can't follow: pitch the vocal up in FL for that drop, or set the key change to None." });
        if (map.dropGap == 3 && fills)
            out.push_back ({ "Kick-only bar before drops and Kybalion fills both use that bar: the gated tracks are muted, so their fill won't be heard. "
                             "Choose a shorter silence or switch the fills off." });
        for (int i = 1; i < map.count; ++i)
            if (map.sections[(size_t) i].type == SectionType::Drop)
                for (int k = i - 1; k >= 0; --k)
                    if (map.sections[(size_t) k].type == SectionType::Breakdown)
                    {
                        if (map.energyOf (i) <= map.energyOf (k))
                            out.push_back ({ "The drop at bar " + juce::String (map.sections[(size_t) i].startBar)
                                             + " is drawn no higher than the breakdown before it on the energy curve." });
                        break;
                    }
        if (map.intensity < 30)
            out.push_back ({ "Intensity is at " + juce::String (map.intensity) + "%: most of the suite's section moves are switched off." });
    }

    /** Emerald Tablets on single tracks: a loop or sample whose key doesn't fit the song's. */
    inline void keyClashes (const std::vector<suite::SuiteEntry>& entries, std::vector<Advice>& out)
    {
        const auto song = suite::KeyBus::read();
        if (! song.valid) return;
        auto majorRoot = [] (int root, bool minor) { return minor ? (root + 3) % 12 : root % 12; };   // relative keys share notes
        const auto songMajor = majorRoot (song.root, song.scale == 0);
        for (const auto& e : entries)
        {
            if (e.plugin != "Emerald Tablet" || onMaster (e)) continue;
            const auto best = (int) e.metrics[0];
            if (best < 0 || e.metrics[1] < 0.5f || e.metrics[2] < 12.0f) continue;   // not sure yet
            const auto root = best % 12;
            const auto minor = best >= 12;
            if (majorRoot (root, minor) == songMajor) continue;
            out.push_back ({ where (e) + " sounds in " + suite::music::noteNames()[root] + (minor ? " minor" : " major") + ", but the song is in "
                             + suite::music::noteNames()[song.root] + (song.scale == 0 ? " minor" : " major")
                             + ": retune or replace that loop or sample (or check it's the right one)." });
        }
    }

    /** Plugins loaded but doing nothing, worth removing to keep the project light. */
    inline juce::StringArray unused (const std::vector<suite::SuiteEntry>& entries)
    {
        juce::StringArray out;
        for (const auto& e : entries)
            if (e.has (suite::Status::idle)) out.add (e.plugin + " on " + where (e));
        return out;
    }

    /** The single most useful thing to do now (first match wins). `mentalism` = the Mentalism
        entry if one is loaded: metrics are [coach problems, checklist failures, seconds measured, ready]. */
    inline Advice nextStep (const std::vector<suite::SuiteEntry>& entries, const suite::SectionMap* map)
    {
        using namespace suite;
        auto find = [&entries] (const char* plugin) -> const SuiteEntry* { for (auto& e : entries) if (e.plugin == plugin) return &e; return nullptr; };
        if (map == nullptr || map->count == 0)
            return { "Start the song: New song..., Arrange... or a template in Ouroboros." };
        if (find ("Correspondence") == nullptr && find ("Emerald Tablet") == nullptr)
            return { "Load Correspondence and choose the chords: the whole suite plays in its key." };
        for (const auto& e : entries)
            if (e.plugin == "Prima Materia" && ! e.has (Status::roleSet))
                return { "Give Prima Materia on " + where (e) + " a role (or name the track: Kick, Bass, Pad, Lead, Vox)." };
        std::vector<Advice> clashes;
        keyClashes (entries, clashes);
        if (! clashes.empty()) return clashes.front();
        const auto* m = find ("Mentalism");
        if (m == nullptr)
            return { "Put Mentalism (and Lapis) on the master to measure the mix." };
        if (m->metrics[2] < 30.0f)
            return { "Press Reset in Mentalism and play the whole song through once." };
        if (m->metrics[0] > 0.5f)
            return { "Mentalism's coach has " + juce::String ((int) m->metrics[0]) + " arrangement note(s): Auto-coach or Finish the song deals with them." };
        if (m->metrics[1] > 0.5f)
            return { "The export checklist has " + juce::String ((int) m->metrics[1]) + " item(s) to fix: open it in Mentalism." };
        for (const auto& e : entries)
            if (e.has (Status::highLatency))
                return { "Ready to render. Before recording anything new, switch off Azoth's reverse swells on " + where (e) + " (3 s latency).", false };
        if (m->metrics[3] > 0.5f)
            return { "Ready to export: Make versions in Ouroboros, then see the Render plan.", false };
        return { "Keep going: the suite has nothing urgent.", false };
    }

    inline std::vector<Advice> check (const std::vector<suite::SuiteEntry>& entries, const suite::SectionMap* map = nullptr)
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

        if (map != nullptr)
            collisions (entries, *map, out);
        keyClashes (entries, out);
        for (const auto& e : entries)
            if (e.has (Status::highLatency))
                out.push_back ({ e.plugin + " on " + where (e) + " adds 3 s of latency (reverse swells): fine for mixing, but switch it off while you record." });

        if (out.empty())
            out.push_back ({ "Everything is set up.", false });
        return out;
    }
}
