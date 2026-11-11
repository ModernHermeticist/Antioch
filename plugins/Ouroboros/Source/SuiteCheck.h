#pragma once

#include <map>

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

    /** Project tidy: what can go, to keep the project light, as a plan. */
    inline juce::String tidyPlan (const std::vector<suite::SuiteEntry>& entries)
    {
        juce::StringArray lines { "PROJECT TIDY", "" };
        const auto idle = unused (entries);
        std::map<juce::String, juce::StringArray> perTrack;
        for (const auto& e : entries) perTrack[e.plugin + "|" + e.track].add (where (e));
        juce::StringArray doubled;
        for (const auto& [key, places] : perTrack)
            if (places.size() > 1) doubled.add (key.upToFirstOccurrenceOf ("|", false, false) + " twice on " + places[0]);
        int masters = 0;
        for (const auto& e : entries) masters += e.plugin == "Lapis";
        if (idle.isEmpty() && doubled.isEmpty() && masters <= 1)
        {
            lines.add ("Nothing to remove: every suite plugin in the project is doing something.");
            return lines.joinIntoString ("\n") + "\n";
        }
        if (! idle.isEmpty())
        {
            lines.add ("Remove (loaded, but doing nothing):");
            for (const auto& i : idle) lines.add ("  - " + i);
        }
        if (! doubled.isEmpty())
        {
            lines.add ("Doubled up (one is enough):");
            for (const auto& d : doubled) lines.add ("  - " + d);
        }
        if (masters > 1) lines.add ("More than one Lapis: keep the one on the master.");
        lines.add ("");
        lines.add ("Save a recipe first (Recipes...) if you might want them back. Plugins that are bypassed in FL can't be seen from here: check the mixer for those.");
        return lines.joinIntoString ("\n") + "\n";
    }

    /** Stem plan for a remix pack: which tracks go into which stem, from Prima Materia's roles
        (and track names where a track has no role), with the file names to export them as. */
    inline juce::String stemPlan (const std::vector<suite::SuiteEntry>& entries, const juce::String& songName)
    {
        // Prima Materia roles: 0 Neutral, 1 Kick, 2 Bass, 3 Hats, 4 Pads, 5 Lead, 6 Vocal, 7 FX
        static const char* stemOfRole[] = { "", "Drums", "Bass", "Drums", "Music", "Leads", "Vocals", "FX" };
        static const char* order[] = { "Drums", "Bass", "Music", "Leads", "Vocals", "FX", "Unassigned" };
        auto fromName = [] (const juce::String& track)
        {
            const auto n = track.toLowerCase();
            auto any = [&n] (std::initializer_list<const char*> words) { for (auto* w : words) if (n.contains (w)) return true; return false; };
            if (any ({ "kick", "snare", "clap", "hat", "perc", "drum", "ride", "crash", "top" })) return "Drums";
            if (any ({ "bass", "sub" })) return "Bass";
            if (any ({ "vox", "vocal", "voc" })) return "Vocals";
            if (any ({ "lead", "saw" })) return "Leads";
            if (any ({ "pad", "chord", "pluck", "arp", "string", "piano", "keys", "stab" })) return "Music";
            if (any ({ "fx", "riser", "impact", "sweep", "atmos", "noise" })) return "FX";
            return "Unassigned";
        };
        std::map<juce::String, juce::StringArray> stems;
        juce::StringArray seen;
        for (const auto& e : entries)
        {
            if (e.track.isEmpty() || e.plugin == "Lapis" || e.plugin == "Mentalism" || e.plugin == "Ouroboros") continue;
            if (seen.contains (e.track)) continue;   // one line per track
            const auto role = e.plugin == "Prima Materia" ? (int) e.metrics[0] : -1;
            const juce::String stem = role >= 1 && role <= 7 ? stemOfRole[role] : fromName (e.track);
            if (e.plugin != "Prima Materia" && stem == "Unassigned") continue;   // only tracks we can place (or that have a role)
            stems[stem].addIfNotAlreadyThere (e.track);
            seen.add (e.track);
        }
        // Tracks a Prima Materia gave a role to override what another plugin's name guess said.
        juce::StringArray lines;
        const auto base = songName.isNotEmpty() ? songName : juce::String ("Song");
        lines.add ("STEM PLAN" + (songName.isNotEmpty() ? " - " + songName : juce::String()));
        lines.add ("");
        if (stems.empty())
        {
            lines.add ("No tracks to place yet: give each track's Prima Materia a role (or name the tracks Kick, Bass, Pad, Lead, Vox, FX).");
            return lines.joinIntoString ("\n") + "\n";
        }
        for (auto* stem : order)
        {
            const auto it = stems.find (stem);
            if (it == stems.end()) continue;
            lines.add (juce::String (stem) + ": " + it->second.joinIntoString (", "));
            if (juce::String (stem) != "Unassigned")
                lines.add ("   file: " + juce::File::createLegalFileName (base + " - " + stem + ".wav"));
        }
        lines.add ("");
        lines.add ("Export each stem from the start of the song to the end, with the master chain (Lapis) bypassed and the "
                   "master fader at 0 dB, so the stems add up to the mix. 24-bit WAV at the project's sample rate.");
        if (stems.count ("Unassigned") > 0)
            lines.add ("Unassigned tracks: give them a role in Prima Materia, or add them to the nearest stem by hand.");
        return lines.joinIntoString ("\n") + "\n";
    }

    /** Session health, from opening a project: plugins saved by an older version, missing samples. */
    inline std::vector<Advice> health (const std::vector<suite::SuiteEntry>& entries)
    {
        std::vector<Advice> out;
        for (const auto& e : entries)
        {
            if (e.has (suite::Status::missingFiles))
                out.push_back ({ e.plugin + " on " + where (e) + " can't find " + juce::String (juce::jmax (1, (int) e.metrics[0]))
                                     + " of its samples (moved or deleted): Fix the session puts the synthesised sounds back (undo in Mentalism's History), or load them again." });
            if (e.has (suite::Status::olderState))
                out.push_back ({ e.plugin + " on " + where (e) + " was saved by an older version of the suite: it loads fine, and its new options start "
                                     "at their defaults (save the project to bring it up to date).", false });
        }
        return out;
    }
    inline bool needsSessionFix (const std::vector<suite::SuiteEntry>& entries)
    {
        for (const auto& e : entries) if (e.has (suite::Status::missingFiles)) return true;
        return false;
    }

    /** The single most useful thing to do now (first match wins). `mentalism` = the Mentalism
        entry if one is loaded: metrics are [coach problems, checklist failures, seconds measured, ready]. */
    inline Advice nextStep (const std::vector<suite::SuiteEntry>& entries, const suite::SectionMap* map)
    {
        using namespace suite;
        auto find = [&entries] (const char* plugin) -> const SuiteEntry* { for (auto& e : entries) if (e.plugin == plugin) return &e; return nullptr; };
        if (map == nullptr || map->count == 0)
            return { "Start the song: New song..., Arrange... or a template in Ouroboros." };
        for (const auto& e : entries)
            if (e.has (Status::missingFiles))
                return { "Fix the session: " + e.plugin + " on " + where (e) + " can't find its samples (Fix the session, below)." };
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

        for (const auto& h : health (entries)) out.push_back (h);
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
