#pragma once

// Ouroboros' side of the label-ready checks: it measures the song on the master as it plays (bar
// by bar), gathers what the rest of the suite knows (the master from Mentalism, every track from
// the Quintessences, the parts from Correspondence, the kick from Kybalion, the director's plan),
// keeps the label profile (and its reference catalogue), scores everything (LabelReady.h), keeps
// the score's history and the milestones (the whole suite's state at "sent to vocalist",
// "A&R round 1"...), and writes the hand-off pack.

#include "LabelReady.h"
#include "AutopilotBus.h"
#include "KickBus.h"
#include "WritingBus.h"
#include "Recipes.h"
#include "StyleLog.h"
#include "MoveLog.h"
#include <thread>

namespace lab
{
    /** The song's bars, written by the audio thread (sums, so a bar heard twice averages). */
    struct LiveBars
    {
        static constexpr int maxBars = 1024;
        struct B { std::atomic<float> full { 0 }, side { 0 }, low { 0 }, onsets { 0 }; std::array<std::atomic<float>, numBands> band {}; std::atomic<int> samples { 0 }; };
        std::array<B, maxBars> bars;
        void add (int bar, const Accumulator& a) noexcept
        {
            if (bar < 0 || bar >= maxBars || a.samples <= 0) return;
            auto& b = bars[(size_t) bar];
            b.full.store (b.full.load() + (float) a.full); b.side.store (b.side.load() + (float) a.side); b.low.store (b.low.load() + (float) a.low);
            b.onsets.store (b.onsets.load() + (float) a.onsets);
            for (int k = 0; k < numBands; ++k) b.band[(size_t) k].store (b.band[(size_t) k].load() + (float) a.band[(size_t) k]);
            b.samples.store (b.samples.load() + a.samples);
        }
        std::vector<Bar> read (int totalBars) const
        {
            std::vector<Bar> out;
            for (int i = 0; i < juce::jlimit (0, maxBars, totalBars); ++i)
            {
                const auto& b = bars[(size_t) i];
                Accumulator a;
                a.samples = b.samples.load();
                a.full = b.full.load(); a.side = b.side.load(); a.low = b.low.load(); a.onsets = (int) std::lround (b.onsets.load());
                for (int k = 0; k < numBands; ++k) a.band[(size_t) k] = b.band[(size_t) k].load();
                auto bar = a.finish();
                // onsets were summed over every time the bar was heard: per hearing
                if (a.samples > 0) bar.onsets = b.onsets.load() * juce::jmin (1.0f, expectedSamples / (float) a.samples);
                out.push_back (bar);
            }
            return out;
        }
        void clear() noexcept { for (auto& b : bars) { b.full.store (0); b.side.store (0); b.low.store (0); b.onsets.store (0); for (auto& x : b.band) x.store (0); b.samples.store (0); } }
        float expectedSamples = 44100.0f * 1.74f;   // one bar's samples at the current tempo (the message thread keeps it up to date)
    };

    class LabelReadyHost
    {
    public:
        LiveBars live;
        Profile profile = genericProfile();
        bool profileFromCatalogue = false;
        Report report;
        std::vector<std::pair<juce::int64, int>> history;      // (time, overall), a point a minute
        struct Milestone { juce::String name, recipe; juce::int64 timeMs = 0; int overall = 0; std::array<float, numAreas> areas {}; };
        std::vector<Milestone> milestones;
        std::map<int, std::array<double, numTypes>> rolePower;   // per role, per kind of section: summed power
        std::map<int, std::array<int, numTypes>> roleCount;
        std::map<int, std::vector<float>> timingSamples;          // per role: hits off the grid (ms)
        juce::String analysing;                                   // the reference being measured (a thread), or empty
        std::thread worker;

        ~LabelReadyHost() { if (worker.joinable()) worker.join(); }

        // ---- the profile and its catalogue ----
        void useProfile (const juce::String& name)
        {
            profile = Profile::load (name);
            profileFromCatalogue = ! profile.empty();
            if (profile.empty()) { auto g = genericProfile(); g.name = name; g.readyThreshold = profile.readyThreshold; profile = g; }
        }
        /** Measures a reference file (a background thread) and adds it to the profile. */
        void addReference (const juce::File& f, double bpm, bool yours)
        {
            if (worker.joinable()) worker.join();
            analysing = f.getFileNameWithoutExtension();
            auto name = profile.name;
            worker = std::thread ([this, f, bpm, yours, name]
            {
                Reference r;
                r.yours = yours;
                const auto ok = r.analyse (f, bpm);
                const juce::ScopedLock sl (lock);
                if (ok)
                {
                    if (! profileFromCatalogue) { profile = Profile {}; profile.name = name; }
                    profile.refs.push_back (r);
                    profile.learn();
                    profile.save();
                    profileFromCatalogue = true;
                }
                lastAdded = ok ? "Added \"" + r.name + "\" to " + name : "Couldn't measure \"" + f.getFileName() + "\"";
                analysing = {};
            });
        }
        void removeReference (int index)
        {
            const juce::ScopedLock sl (lock);
            if (index < 0 || index >= (int) profile.refs.size()) return;
            profile.refs.erase (profile.refs.begin() + index);
            profile.learn();
            profile.save();
            if (profile.refs.empty()) { const auto n = profile.name; profile = genericProfile(); profile.name = n; profileFromCatalogue = false; }
        }
        juce::String lastAdded;
        juce::CriticalSection lock;

        // ---- while it plays (the timer, a few times a second) ----
        void sampleTracks (const suite::SectionMap& map, double ppq, double barQ, double bpm)
        {
            const auto bar = (int) std::floor (ppq / barQ) + 1;
            int type = -1;
            for (int i = 0; i < map.count; ++i) if (map.sections[(size_t) i].startBar <= bar) type = (int) map.sections[(size_t) i].type;
            if (type >= 0)
                suite::Autopilot::forEachLive (0u, [&] (int role, const float* bands)
                {
                    double p = 0.0;
                    for (int b = 0; b < numBands; ++b) p += std::pow (10.0, bands[b] / 10.0);
                    rolePower[role][(size_t) type] += p;
                    ++roleCount[role][(size_t) type];
                });
            // timing: each track's last hits against the 16th grid
            const auto msPerQ = 60000.0 / juce::jmax (1.0, bpm);
            suite::Autopilot::forEachLiveOnsets (0u, [&] (int role, const double* on)
            {
                auto& v = timingSamples[role];
                for (int k = 0; k < 16; ++k)
                {
                    if (on[k] <= 0.0 || on[k] < ppq - 8.0) continue;
                    const auto grid = std::round (on[k] * 4.0) / 4.0;
                    v.push_back ((float) (std::abs (on[k] - grid) * msPerQ));
                }
                while (v.size() > 400) v.erase (v.begin(), v.begin() + 100);
            });
        }

        /** Everything the checks need, gathered now. */
        Inputs gather (const suite::SectionMap& map, double bpm, double barQ, const suite::director::Plan* plan)
        {
            Inputs in;
            in.map = map; in.bpm = bpm; in.barQ = barQ;
            live.expectedSamples = (float) (44100.0 * barQ * 60.0 / juce::jmax (1.0, bpm));
            in.bars = live.read (map.totalBars);
            in.sections = sections (in.bars, map);
            in.master = suite::LabelReadyBus::master();
            in.song = suite::SongBus::read();
            for (const auto& n : suite::WritingBus::sung()) in.vocal.push_back ({ n.pitch, (float) n.start, (float) n.length, 100 });
            in.entries = suite::Registry::list();
            for (const auto& e : in.entries)
                if (e.plugin == "Quintessence" && e.has (suite::Status::hasBands))
                {
                    RoleLevel r;
                    r.role = (int) e.metrics[0]; r.track = e.track;
                    double p = 0.0;
                    for (int b = 0; b < numBands; ++b) { r.bands[(size_t) b] = e.bands[(size_t) b]; p += std::pow (10.0, e.bands[(size_t) b] / 10.0); }
                    r.levelDb = (float) (10.0 * std::log10 (p + 1.0e-12));
                    in.tracks.push_back (r);
                }
            in.plan = plan;
            in.kickHz = suite::KickBus::read (600).bodyHz;
            for (const auto& [role, v] : timingSamples)
                if (v.size() >= 16) { auto c = v; std::nth_element (c.begin(), c.begin() + (long) (c.size() / 2), c.end()); in.timingMs[role] = c[c.size() / 2]; }
            for (const auto& [role, pw] : rolePower)
            {
                std::array<float, numTypes> lv {};
                for (int t = 0; t < numTypes; ++t)
                {
                    const auto n = roleCount[role][(size_t) t];
                    lv[(size_t) t] = n > 0 ? (float) (10.0 * std::log10 (pw[(size_t) t] / n + 1.0e-12)) : -100.0f;
                }
                in.roleLevelPerType[role] = lv;
            }
            for (const auto& e : suite::StyleLog::read()) if (e.kind == "hookprint" && e.get ("print").isNotEmpty()) in.otherHooks.push_back (e.get ("print"));
            if (in.song.valid && ! in.song.hook.empty())   // this song's own hook isn't an "earlier" one
            {
                const auto mine = detail::hookPrint (in.song.hook);
                in.otherHooks.erase (std::remove (in.otherHooks.begin(), in.otherHooks.end(), mine), in.otherHooks.end());
            }
            in.fxRepitched = (int) suite::Autopilot::countOf (suite::Autopilot::fxRepitched);
            in.clicksGuarded = (int) suite::Autopilot::countOf (suite::Autopilot::clicksGuarded);
            return in;
        }

        /** Scores now; keeps a point of history a minute; tells Mentalism's strip. */
        void update (const suite::SectionMap& map, double bpm, double barQ, const suite::director::Plan* plan)
        {
            Inputs in;
            {
                const juce::ScopedLock sl (lock);
                in = gather (map, bpm, barQ, plan);
                in.profile = &profile;
                report = evaluate (in);
            }
            const auto now = juce::Time::currentTimeMillis();
            if (report.overall > 0 && (history.empty() || now - history.back().first > 60000))
            {
                history.push_back ({ now, report.overall });
                if (history.size() > 600) history.erase (history.begin());
            }
            suite::LabelReadyBus::publishOverall (report.overall, report.nextAction);
        }

        // ---- fixes ----
        /** Sends every safe one-click fix the report has (each once). Returns how many. */
        int fixAllSafe()
        {
            std::set<std::pair<int, uint32_t>> sent;
            for (const auto& c : report.checks)
                if (c.measured && c.safeFix && c.fix != suite::Fix::None && c.score < 0.8f && sent.insert ({ (int) c.fix, c.fixData }).second)
                    suite::FixBus::send (c.fix, c.fixData);
            if (! sent.empty()) suite::MoveLog::add ("Ouroboros", {}, "Label-ready: " + juce::String ((int) sent.size()) + " safe fix(es) sent", suite::MoveLog::fix);
            return (int) sent.size();
        }

        // ---- milestones ----
        void saveMilestone (const juce::String& name)
        {
            Milestone m;
            m.name = name.isNotEmpty() ? name : juce::String ("Milestone");
            m.timeMs = juce::Time::currentTimeMillis();
            m.recipe = "Milestone - " + m.name + " - " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M");
            m.overall = report.overall;
            m.areas = report.areaScore;
            suite::RecipeBus::send (suite::RecipeAction::Save, m.recipe);   // every plugin saves its state under that name
            milestones.push_back (m);
            const auto song = suite::SongBus::read();
            if (song.valid && ! song.hook.empty()) suite::StyleLog::add ("hookprint", { { "print", detail::hookPrint (song.hook) }, { "milestone", m.name } });
            suite::MoveLog::add ("Ouroboros", {}, "Milestone saved: " + m.name + " (label-ready " + juce::String (m.overall) + ")", suite::MoveLog::fix);
        }
        void restoreMilestone (int index)
        {
            if (index < 0 || index >= (int) milestones.size()) return;
            suite::RecipeBus::send (suite::RecipeAction::Load, milestones[(size_t) index].recipe);
            suite::MoveLog::add ("Ouroboros", {}, "Back to the milestone " + milestones[(size_t) index].name, suite::MoveLog::fix);
        }
        /** Which plugins' settings differ between two milestones. */
        juce::StringArray diffMilestones (int a, int b) const
        {
            juce::StringArray out;
            if (a < 0 || b < 0 || a >= (int) milestones.size() || b >= (int) milestones.size()) return out;
            const auto da = suite::Recipes::folder().getChildFile (juce::File::createLegalFileName (milestones[(size_t) a].recipe));
            const auto db = suite::Recipes::folder().getChildFile (juce::File::createLegalFileName (milestones[(size_t) b].recipe));
            std::set<juce::String> names;
            for (const auto& f : da.findChildFiles (juce::File::findFiles, false, "*.state")) names.insert (f.getFileName());
            for (const auto& f : db.findChildFiles (juce::File::findFiles, false, "*.state")) names.insert (f.getFileName());
            for (const auto& n : names)
            {
                const auto fa = da.getChildFile (n), fb = db.getChildFile (n);
                if (! fa.existsAsFile()) out.add ("added: " + n.upToLastOccurrenceOf (".", false, false));
                else if (! fb.existsAsFile()) out.add ("removed: " + n.upToLastOccurrenceOf (".", false, false));
                else if (! fa.hasIdenticalContentTo (fb)) out.add ("changed: " + n.upToLastOccurrenceOf (".", false, false));
            }
            const auto& ma = milestones[(size_t) a]; const auto& mb = milestones[(size_t) b];
            out.insert (0, "Label-ready " + juce::String (ma.overall) + " -> " + juce::String (mb.overall));
            for (int k = 0; k < numAreas; ++k)
                if (std::abs (ma.areas[(size_t) k] - mb.areas[(size_t) k]) > 0.05f)
                    out.insert (1, juce::String (areaName (k)) + ": " + juce::String (juce::roundToInt (ma.areas[(size_t) k] * 100)) + " -> " + juce::String (juce::roundToInt (mb.areas[(size_t) k] * 100)));
            return out;
        }

        // ---- reports ----
        juce::String reportText (const suite::SectionMap& map) const
        {
            juce::StringArray l;
            l.add ("LABEL-READY REPORT  " + juce::Time::getCurrentTime().toString (true, true, false, true));
            l.add ("Profile: " + profile.name + (profileFromCatalogue ? " (" + juce::String ((int) profile.refs.size()) + " references)" : juce::String (" (generic norms)")));
            l.add ("");
            l.add ("Overall: " + juce::String (report.overall) + "/100  " + (report.ready ? "READY TO SEND" : "not ready yet"));
            l.add (report.verdict);
            l.add ("Next: " + report.nextAction);
            if (! report.blockers.isEmpty()) { l.add (""); l.add ("Blocking:"); for (const auto& b : report.blockers) l.add ("  " + b); }
            for (int a = 0; a < numAreas; ++a)
            {
                l.add ("");
                l.add (juce::String (areaName (a)) + ": " + (report.areaMeasured[(size_t) a] ? juce::String (juce::roundToInt (report.areaScore[(size_t) a] * 100.0f)) + "%" : juce::String ("not measured yet")));
                for (const auto& c : report.checks)
                    if (c.area == a)
                        l.add (juce::String (c.measured ? (c.score >= 0.8f ? "  OK    " : c.score >= 0.5f ? "  ...   " : "  FIX   ") : "  --    ") + c.name + ": " + c.detail
                               + (c.bar > 0 ? " [bar " + juce::String (c.bar) + "]" : juce::String()) + (c.measured && c.score < 0.8f && c.explain.isNotEmpty() ? "  -> " + c.explain : juce::String()));
            }
            juce::ignoreUnused (map);
            return l.joinIntoString ("\n") + "\n";
        }
        /** The hand-off pack for a mix engineer or a collaborator. */
        juce::File writeHandoff (const juce::File& parent, const juce::String& who, const juce::String& sessionNotes, const juce::String& planSheet,
                                 const juce::String& renderPlan, const suite::SectionMap& map) const
        {
            const auto dir = parent.getChildFile (juce::File::createLegalFileName ("Hand-off for " + (who.isNotEmpty() ? who : juce::String ("collaborator")) + " "
                                                                                   + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M")));
            dir.createDirectory();
            dir.getChildFile ("Label-ready report.txt").replaceWithText (reportText (map));
            juce::StringArray issues;
            for (const auto& c : report.checks) if (c.measured && c.score < 0.6f) issues.add ("- " + c.name + ": " + c.detail + (c.bar > 0 ? " (bar " + juce::String (c.bar) + ")" : juce::String()) + ". " + c.explain);
            dir.getChildFile ("Known issues.txt").replaceWithText (issues.isEmpty() ? juce::String ("None the suite can hear.\n") : issues.joinIntoString ("\n") + "\n");
            dir.getChildFile ("Session notes.txt").replaceWithText (sessionNotes);
            if (planSheet.isNotEmpty()) dir.getChildFile ("Director plan.txt").replaceWithText (planSheet);
            juce::StringArray rr { renderPlan, "", "To render again exactly as it was: Ouroboros > Label-ready > Milestones, restore the milestone, then follow the render plan." };
            if (! milestones.empty()) rr.add ("Latest milestone: " + milestones.back().name + " (recipe \"" + milestones.back().recipe + "\")");
            dir.getChildFile ("Re-render instructions.txt").replaceWithText (rr.joinIntoString ("\n") + "\n");
            return dir;
        }

        // ---- saved with the project ----
        juce::ValueTree toTree() const
        {
            juce::ValueTree t ("LabelReady");
            t.setProperty ("profile", profile.name, nullptr);
            juce::StringArray h; for (const auto& [ms, v] : history) h.add (juce::String (ms) + ":" + juce::String (v));
            t.setProperty ("history", h.joinIntoString (","), nullptr);
            for (const auto& m : milestones)
            {
                juce::ValueTree c ("Milestone");
                c.setProperty ("name", m.name, nullptr); c.setProperty ("recipe", m.recipe, nullptr); c.setProperty ("time", m.timeMs, nullptr); c.setProperty ("overall", m.overall, nullptr);
                juce::StringArray a; for (auto v : m.areas) a.add (juce::String (v, 3));
                c.setProperty ("areas", a.joinIntoString (","), nullptr);
                t.appendChild (c, nullptr);
            }
            return t;
        }
        void fromTree (const juce::ValueTree& t)
        {
            if (! t.hasType ("LabelReady")) return;
            if (t["profile"].toString().isNotEmpty()) useProfile (t["profile"].toString());
            history.clear();
            for (const auto& p : juce::StringArray::fromTokens (t["history"].toString(), ",", ""))
                history.push_back ({ p.upToFirstOccurrenceOf (":", false, false).getLargeIntValue(), p.fromFirstOccurrenceOf (":", false, false).getIntValue() });
            milestones.clear();
            for (const auto& c : t)
                if (c.hasType ("Milestone"))
                {
                    Milestone m; m.name = c["name"].toString(); m.recipe = c["recipe"].toString(); m.timeMs = (juce::int64) c["time"]; m.overall = (int) c["overall"];
                    const auto a = juce::StringArray::fromTokens (c["areas"].toString(), ",", "");
                    for (int k = 0; k < numAreas && k < a.size(); ++k) m.areas[(size_t) k] = a[k].getFloatValue();
                    milestones.push_back (m);
                }
        }
    };
}
