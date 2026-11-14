#pragma once

// Ouroboros' side of the label-ready checks: it measures the song on the master as it plays (bar
// by bar), gathers what the rest of the suite knows (the master from Mentalism, every track from
// the Quintessences, the parts from Correspondence, the kick from Kybalion, the director's plan),
// keeps the label profile (and its reference catalogue), scores everything (LabelReady.h), keeps
// the score's history and the milestones (the whole suite's state at "sent to vocalist",
// "A&R round 1"...), and writes the hand-off pack.

#include "LabelReady.h"
#include "LabelAutoFix.h"
#include "AutopilotBus.h"
#include "KickBus.h"
#include "WritingBus.h"
#include "Recipes.h"
#include "StyleLog.h"
#include "MoveLog.h"
#include "ReferenceBus.h"
#include "KeyBus.h"
#include "KitBus.h"
#include "MixFixBus.h"
#include <optional>
#include <thread>

namespace lab
{
    /** The song's bars, written by the audio thread (sums, so a bar heard twice averages). */
    struct LiveBars
    {
        static constexpr int maxBars = 1024;
        struct B { std::atomic<float> full { 0 }, side { 0 }, low { 0 }, onsets { 0 }, left { 0 }, right { 0 }, peak { 0 }, lowLR { 0 }, lowLL { 0 }, lowRR { 0 }; std::array<std::atomic<float>, numBands> band {}; std::array<std::atomic<float>, 12> chroma {}; std::atomic<int> samples { 0 }; };
        std::array<B, maxBars> bars;
        void add (int bar, const Accumulator& a) noexcept
        {
            if (bar < 0 || bar >= maxBars || a.samples <= 0) return;
            auto& b = bars[(size_t) bar];
            b.full.store (b.full.load() + (float) a.full); b.side.store (b.side.load() + (float) a.side); b.low.store (b.low.load() + (float) a.low);
            b.left.store (b.left.load() + (float) a.left); b.right.store (b.right.load() + (float) a.right); b.peak.store (std::max (b.peak.load(), a.peak));
            b.lowLR.store (b.lowLR.load() + (float) a.lowLR); b.lowLL.store (b.lowLL.load() + (float) a.lowLL); b.lowRR.store (b.lowRR.load() + (float) a.lowRR);
            b.onsets.store (b.onsets.load() + (float) a.onsets);
            for (int k = 0; k < numBands; ++k) b.band[(size_t) k].store (b.band[(size_t) k].load() + (float) a.band[(size_t) k]);
            for (int k = 0; k < 12; ++k) b.chroma[(size_t) k].store (b.chroma[(size_t) k].load() + (float) a.chroma[(size_t) k]);
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
                a.full = b.full.load(); a.side = b.side.load(); a.low = b.low.load(); a.left = b.left.load(); a.right = b.right.load(); a.peak = b.peak.load();
                a.lowLR = b.lowLR.load(); a.lowLL = b.lowLL.load(); a.lowRR = b.lowRR.load(); a.onsets = (int) std::lround (b.onsets.load());
                for (int k = 0; k < numBands; ++k) a.band[(size_t) k] = b.band[(size_t) k].load();
                for (int k = 0; k < 12; ++k) a.chroma[(size_t) k] = b.chroma[(size_t) k].load();
                auto bar = a.finish();
                // onsets were summed over every time the bar was heard: per hearing
                if (a.samples > 0) bar.onsets = b.onsets.load() * juce::jmin (1.0f, expectedSamples / (float) a.samples);
                out.push_back (bar);
            }
            return out;
        }
        void clear() noexcept { for (auto& b : bars) { b.full.store (0); b.side.store (0); b.low.store (0); b.onsets.store (0); for (auto& x : b.band) x.store (0); for (auto& x : b.chroma) x.store (0); b.left.store (0); b.right.store (0); b.peak.store (0); b.lowLR.store (0); b.lowLL.store (0); b.lowRR.store (0); b.samples.store (0); } }
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
        struct Milestone { juce::String name, recipe; juce::int64 timeMs = 0; int overall = 0; std::array<float, numAreas> areas {}; float lufs = -100.0f; int stage = -1; juce::String fixes; };
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
            std::map<int, std::vector<double>> recent;
            suite::Autopilot::forEachLiveOnsets (0u, [&] (int role, const double* on)
            {
                for (int k = 0; k < 16; ++k) if (on[k] > 0.0 && on[k] >= ppq - 8.0) recent[role].push_back (on[k]);
                // swing: how late the offbeat 16ths sit
                auto& sw = swingSamples[role];
                for (int k = 0; k < 16; ++k)
                {
                    if (on[k] <= 0.0 || on[k] < ppq - 8.0) continue;
                    const auto sixteenth = std::round (on[k] * 4.0);
                    if (((long long) sixteenth) % 2 != 0) sw.push_back ((float) ((on[k] - sixteenth / 4.0) * msPerQ));
                }
                while (sw.size() > 400) sw.erase (sw.begin(), sw.begin() + 100);
                auto& v = timingSamples[role];
                for (int k = 0; k < 16; ++k)
                {
                    if (on[k] <= 0.0 || on[k] < ppq - 8.0) continue;
                    const auto grid = std::round (on[k] * 4.0) / 4.0;
                    v.push_back ((float) (std::abs (on[k] - grid) * msPerQ));
                }
                while (v.size() > 400) v.erase (v.begin(), v.begin() + 100);
            });
            // kick and bass flams: the bass's hit nearest each kick, within 30 ms
            if (recent.count (1) && recent.count (2))
                for (auto k : recent[1])
                {
                    double best = 1.0e9;
                    for (auto b : recent[2]) best = std::min (best, std::abs (b - k) * msPerQ);
                    if (best < 30.0) flamSamples.push_back ((float) best);
                }
            while (flamSamples.size() > 400) flamSamples.erase (flamSamples.begin(), flamSamples.begin() + 100);
        }

        std::map<int, std::vector<float>> swingSamples;           // per role: offbeat 16ths against the grid (ms, + late)
        std::vector<float> flamSamples;                           // the bass's start against the kick (ms)

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
            for (const auto& n : suite::WritingBus::sung()) { in.vocal.push_back ({ n.pitch, (float) n.start, (float) n.length, 100 }); in.vocalCents.push_back (n.cents); }
            for (const auto& [role, v] : swingSamples)
                if (v.size() >= 12) { auto c = v; std::nth_element (c.begin(), c.begin() + (long) (c.size() / 2), c.end()); in.swingMs[role] = c[c.size() / 2]; }
            if (flamSamples.size() >= 8) { auto c = flamSamples; std::nth_element (c.begin(), c.begin() + (long) (c.size() / 2), c.end()); in.flamMs = c[c.size() / 2]; }
            in.resonanceHz = suite::Autopilot::resonancesNow();
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
            in.traits = suite::TrackTraits::list();
            const auto myProg = in.song.valid ? songProgressionPrint (in.song) : juce::String();
            in.groovePrint = groovePrintNow();
            for (const auto& e : suite::StyleLog::read())
            {
                if (e.kind == "progprint" && e.get ("print").isNotEmpty() && e.get ("print") != myProg) in.otherProgressions.push_back (e.get ("print"));
                if (e.kind == "grooveprint" && e.get ("print").isNotEmpty() && e.get ("print") != in.groovePrint) in.otherGrooves.push_back (e.get ("print"));
            }
            in.focusSection = focusSection;
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
                lastSections = in.sections;
                lastTraits = in.traits;
                lastInputs = in;
                lastInputs.profile = nullptr;
            }
            // sign-off: the gate also waits for every measured area to be signed off, when asked
            if (requireSignOff)
                for (int a = 0; a < numAreas; ++a)
                    if (report.areaMeasured[(size_t) a] && ! signOff.count (a)) { report.ready = false; report.blockers.add (juce::String (areaName (a)) + ": not signed off yet"); }
            // the label-ready autopilot: the plugins' fixes told where they're needed; Ouroboros' own fixes, one at a time
            const auto cx = stepContext();
            namespace AP = suite::Autopilot;
            if (AP::on (AP::mx2SelfCheckF) || AP::on (AP::mx2OneAtATimeF))
            {
                judge.sentByOuroboros = [] (int f) { return sentByOuroboros (f); };
                judge.allowed = [this] (const Check& c) { return autoFix.allowedByMode (c); };
                judge.learning = &autoFix.learning; judge.noise = &autoFix.noise;
                if (! (cx.rendering && AP::on (AP::mx2RenderFreezeF))) judge.step (report, cx.judgeable, cx.settling);
                suite::LabelReadyBus::publishNeeds (judge.needs());
                suite::LabelReadyBus::publishRevoked (judge.revokedMask());
            }
            else { suite::LabelReadyBus::publishNeeds (AutoFixer::needsFrom (report)); suite::LabelReadyBus::publishRevoked ({}); }
            if (autoFixOn) autoFix.step (report, lastTraits, playingNow, cx);
            // the CPU budget: the suite's Quintessences over 60% of the audio thread put their least useful fixes to sleep
            {
                float cpu = 0.0f;
                for (const auto& t : lastTraits) cpu += t.cpuPercent;
                AP::setCpuSleepLevel (AP::on (AP::mx2CpuBudgetF) ? (cpu > 80.0f ? 4 : cpu > 60.0f ? 1 : 0) : 0);
            }
            // milestones carry every plugin's fixes: a loaded milestone's fixes checked against what it saved
            if (milestoneCheckIn > 0 && --milestoneCheckIn == 0 && AP::on (AP::mx2MilestoneFixesF))
                milestoneFixNote = activeFixesSignature() == milestoneFixes ? juce::String ("the milestone's mix fixes are all back")
                                                                          : juce::String ("the milestone's mix fixes didn't all come back (a plugin missing, or changed since)");
            const auto now = juce::Time::currentTimeMillis();
            if (report.overall > 0 && (history.empty() || now - history.back().first > 60000))
            {
                history.push_back ({ now, report.overall });
                if (history.size() > 600) history.erase (history.begin());
            }
            suite::LabelReadyBus::publishOverall (report.overall, report.nextAction);
            // the score log: what each action did, two updates later
            for (auto& e : scoreLog) if (e.after < 0 && ++e.updatesSince >= 2) e.after = report.overall;
            // steering toward the label: its drop tone becomes every Quintessence's reference
            if (steerToLabel && profileFromCatalogue)
            {
                auto target = labelToneTarget();
                if (suite::Autopilot::on (suite::Autopilot::tasteF))
                {
                    int n = 0;
                    const auto taste = Learning::tasteOffset (n);
                    for (size_t k = 0; k < 10; ++k) target[k] += taste[k];
                }
                suite::ReferenceBus::publish (target);
            }
            // the regression guard: a signed-off area falling after a change is put back
            if (suite::Autopilot::on (suite::Autopilot::regressionGuardF) && lastActionMs > 0 && juce::Time::currentTimeMillis() - lastActionMs < 90000)
                for (const auto& [a, was] : signOffScore)
                    if (report.areaMeasured[(size_t) a] && report.areaScore[(size_t) a] < was - 0.1f && suite::FixBus::latest() > 0)
                    {
                        suite::FixBus::sendUndo (suite::FixBus::latest());
                        regressionNote = juce::String (areaName (a)) + " fell after the last change: undone";
                        suite::MoveLog::add ("Ouroboros", {}, "Label-ready: " + regressionNote, suite::MoveLog::fix);
                        lastActionMs = 0;
                        break;
                    }
            // the score's steadiness: the final score of each play-through
            if (wasPlaying && ! playingNow && report.overall > 0) { playthroughs.push_back (report.overall); if (playthroughs.size() > 8) playthroughs.erase (playthroughs.begin()); }
            wasPlaying = playingNow;
            // the translation loop: one fix at a time, kept only if it helped
            if (translationLoop) stepTranslationLoop();
            // the first time the gate passes: a milestone, automatically
            if (report.ready && focusSection < 0 && ! autoReadySaved) { autoReadySaved = true; saveMilestone ("Label-ready (first pass)"); }
            if (watching && watchBaseline < 0) watchBaseline = report.overall;
        }

        // ---- iteration ----
        int focusSection = -1;        // score one section only
        bool watching = false;        // watch mode: score faster while you loop, against where you started
        int watchBaseline = -1;
        void setWatching (bool on) { watching = on; watchBaseline = on ? report.overall : -1; }
        /** The score log: each thing done, the score before and (two updates later) after. */
        struct LogEntry { juce::int64 timeMs = 0; juce::String what; int before = 0, after = -1, updatesSince = 0; };
        std::vector<LogEntry> scoreLog;
        std::function<void (const juce::String&)> onAction;   // the processor: a before/after snippet around it
        void logAction (const juce::String& what)
        {
            lastActionMs = juce::Time::currentTimeMillis();
            if (onAction) onAction (what);
            scoreLog.push_back ({ juce::Time::currentTimeMillis(), what, report.overall, -1, 0 });
            if (scoreLog.size() > 200) scoreLog.erase (scoreLog.begin());
        }
        juce::String scoreLogText() const
        {
            juce::StringArray l;
            for (auto it = scoreLog.rbegin(); it != scoreLog.rend(); ++it)
                l.add (juce::Time (it->timeMs).formatted ("%H:%M") + "  " + it->what + ": " + juce::String (it->before) + " -> "
                       + (it->after >= 0 ? juce::String (it->after) + " (" + (it->after >= it->before ? "+" : "") + juce::String (it->after - it->before) + ")" : juce::String ("(listening...)")));
            return l.isEmpty() ? juce::String ("Nothing done yet.") : l.joinIntoString ("\n");
        }
        /** Sends a check's one-click fix (or its autopilot switch), and logs it. */
        bool applyCheck (const Check& c)
        {
            if (featureOf (c) == suite::Autopilot::contrastF || (c.fix == suite::Fix::None && featureOf (c) >= 0 && sentByOuroboros (featureOf (c))))
            {
                const auto seqs = autoFix.apply (c, lastTraits);
                logAction (c.name + " (the label autopilot's fix)");
                return ! seqs.empty() || autoFix.note.isNotEmpty();
            }
            if (c.fix != suite::Fix::None) { suite::FixBus::send (c.fix, c.fixData); logAction (c.name + " (" + suite::fixName (c.fix) + ")"); return true; }
            if (c.action.startsWith ("autopilot:"))
            {
                const auto f = c.action.fromFirstOccurrenceOf (":", false, false).getIntValue();
                if (f >= 0 && f < suite::Autopilot::numFeatures)
                {
                    suite::Autopilot::set ((suite::Autopilot::Feature) f, true);
                    suite::Autopilot::set ((suite::Autopilot::Group) suite::Autopilot::featureGroup (f), true);
                    logAction (c.name + " (autopilot: " + suite::Autopilot::featureName (f) + ")");
                    return true;
                }
            }
            return false;
        }

        // ---- the label-ready autopilot ----
        AutoFixer autoFix;
        bool autoFixOn = true;                    // its fixes run (each switch on the Autopilot page decides its own)
        bool playingNow = false;                  // the processor says before each update
        bool loopedNow = false, renderingNow = false;   // ... a loop in this play-through, FL rendering
        int sectionTypeNow = -1;                  // ... where the playhead is
        int heardMax = 0;                         // the most tracks heard in this play-through (a solo shows as fewer)
        PluginFixJudge judge;                     // the plugins' fixes, judged
        juce::String milestoneFixes;              // the fixes on when the last milestone was loaded (checked a few updates later)
        int milestoneCheckIn = -1;
        juce::String milestoneFixNote;
        /** What every plugin has on now: "plugin|track|feature,feature,...", sorted (for the milestone check). */
        static juce::String activeFixesSignature()
        {
            juce::StringArray lines;
            for (const auto& e : suite::MixFixBus::all())
            {
                juce::StringArray fs;
                for (int f = 0; f < suite::Autopilot::numFeatures; ++f) if (e.has (f)) fs.add (juce::String (f));
                lines.add (e.pluginName() + "|" + e.trackName() + "|" + fs.joinIntoString (","));
            }
            lines.sort (true);
            return lines.joinIntoString ("\n");
        }
        /** The mix fixes on every track, in words (Ouroboros' list). */
        static juce::String activeFixesText()
        {
            juce::StringArray l;
            for (const auto& e : suite::MixFixBus::all())
            {
                if (e.count == 0) continue;
                juce::StringArray fs;
                for (int f = 0; f < suite::Autopilot::numFeatures; ++f) if (e.has (f)) fs.add (juce::String (suite::Autopilot::featureName (f)).fromFirstOccurrenceOf (": ", false, false));
                l.add (e.trackName() + " (" + e.pluginName() + "): " + juce::String (e.count) + " on" + (std::abs (e.levelDb) > 0.05f ? ", moving it " + juce::String (e.levelDb, 1) + " dB" : juce::String())
                       + (e.budgetHit ? ", held back by its budget" : juce::String()) + "\n    " + fs.joinIntoString ("; "));
            }
            return l.isEmpty() ? juce::String ("No mix fix is on yet: they switch on where the checks find a problem.") : l.joinIntoString ("\n");
        }
        /** The plugins' fixes as judged: kept, being tried, switched off for this song. */
        juce::String judgedText() const
        {
            juce::StringArray kept, trying, off;
            for (const auto& [f, e] : judge.entries)
            {
                const auto n = juce::String (suite::Autopilot::featureName (f)) + " (" + e.check + ")";
                if (e.state == PluginFixJudge::kept) kept.add (n); else if (e.state == PluginFixJudge::trying) trying.add (n); else if (e.state == PluginFixJudge::revoked) off.add (n);
            }
            return "Kept:\n  " + (kept.isEmpty() ? juce::String ("none yet") : kept.joinIntoString ("\n  "))
                 + "\n\nBeing tried:\n  " + (trying.isEmpty() ? juce::String ("none") : trying.joinIntoString ("\n  "))
                 + "\n\nSwitched off for this song (no better):\n  " + (off.isEmpty() ? juce::String ("none") : off.joinIntoString ("\n  "));
        }

        /** The update's context for the autopilot: a real play-through, plugins settling, FL rendering. */
        AutoFixer::Context stepContext()
        {
            AutoFixer::Context cx;
            int heard = 0;
            for (const auto& t : lastTraits) heard += t.outRmsDb > -60.0f ? 1 : 0;
            if (! playingNow) heardMax = 0;
            heardMax = juce::jmax (heardMax, heard);
            const auto soloed = heardMax >= 3 && heard < (heardMax * 6) / 10;
            cx.judgeable = playingNow && ! loopedNow && ! soloed;
            cx.settling = suite::MixFixBus::anyBusy();
            cx.rendering = renderingNow;
            cx.sectionType = sectionTypeNow;
            return cx;
        }
        std::vector<suite::TrackTraits::Traits> lastTraits;
        Inputs lastInputs;                        // the last snapshot (for scoring against one reference at a time)

        // ---- deciding what to do next ----
        /** The points the overall score would gain if this check were fixed fully. */
        float predictedGain (const Check& c) const
        {
            float weights = 0.0f;
            for (int a = 0; a < numAreas; ++a) if (report.areaMeasured[(size_t) a]) weights += areaWeight (a);
            int inArea = 0;
            for (const auto& x : report.checks) inArea += x.area == c.area && x.measured ? 1 : 0;
            if (weights <= 0.0f || inArea == 0 || ! c.measured) return 0.0f;
            return 100.0f * areaWeight (c.area) / weights * (1.0f - c.score) / (float) inArea;
        }
        static int effortOf (const Check& c)
        {
            if (c.fix != suite::Fix::None || c.action.startsWith ("autopilot:")) return 1;
            if (c.action.startsWith ("director:") || c.action.startsWith ("lift:") || c.action.startsWith ("entrance:")) return 2;
            if (c.action == "write stronger") return 3;
            return 5;
        }
        /** The three changes that raise the score most for the least work. */
        juce::String sessionPlan() const
        {
            std::vector<std::pair<float, const Check*>> v;
            for (const auto& c : report.checks)
                if (c.measured && c.score < 0.8f) v.push_back ({ predictedGain (c) * (c.blocking ? 2.0f : 1.0f) / (float) effortOf (c), &c });
            std::sort (v.begin(), v.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
            juce::StringArray l { "TODAY'S PLAN: the three changes worth the most for the least work", "" };
            static const char* effort[] = { "", "one click", "one word to the director", "a writing tool", "", "by hand" };
            for (size_t i = 0; i < v.size() && i < 3; ++i)
                l.add (juce::String ((int) i + 1) + ". " + v[i].second->name + " (about +" + juce::String (predictedGain (*v[i].second), 1) + " points, " + effort[effortOf (*v[i].second)] + ")\n   "
                       + v[i].second->detail + "\n   -> " + v[i].second->explain);
            if (v.empty()) l.add ("Nothing measured is under 80%.");
            return l.joinIntoString ("\n");
        }
        /** Where the missing points are, area by area, and the cheapest way to win each back. */
        juce::String whyNot100() const
        {
            float weights = 0.0f;
            for (int a = 0; a < numAreas; ++a) if (report.areaMeasured[(size_t) a]) weights += areaWeight (a);
            juce::StringArray l { "WHY NOT 100: " + juce::String (100 - report.overall) + " points missing", "" };
            for (int a = 0; a < numAreas; ++a)
            {
                if (! report.areaMeasured[(size_t) a] || weights <= 0.0f) continue;
                const auto lost = 100.0f * areaWeight (a) / weights * (1.0f - report.areaScore[(size_t) a]);
                if (lost < 0.5f) continue;
                const Check* cheapest = nullptr; float bestRatio = 0.0f;
                for (const auto& c : report.checks)
                    if (c.area == a && c.measured && c.score < 0.95f)
                        if (const auto r = predictedGain (c) / (float) effortOf (c); r > bestRatio) { bestRatio = r; cheapest = &c; }
                l.add (juce::String (areaName (a)) + ": " + juce::String (lost, 1) + " points" + (cheapest != nullptr ? "  (cheapest: " + cheapest->name + ", " + cheapest->explain + ")" : juce::String()));
            }
            return l.joinIntoString ("\n");
        }
        /** What an A&R would say, in plain words. */
        juce::String arSummary() const
        {
            juce::StringArray l { "A&R NOTES", "", report.verdict, "" };
            std::vector<const Check*> good, bad;
            for (const auto& c : report.checks) if (c.measured && (c.area == aAR || c.area == aWriting || c.area == aReference)) (c.score >= 0.9f ? good : bad).push_back (&c);
            std::sort (bad.begin(), bad.end(), [this] (const Check* a, const Check* b) { return predictedGain (*a) > predictedGain (*b); });
            if (! good.empty()) { l.add ("What works:"); for (size_t i = 0; i < good.size() && i < 3; ++i) l.add ("  + " + good[i]->name + ": " + good[i]->detail); l.add (""); }
            l.add ("What I'd change before signing it:");
            for (size_t i = 0; i < bad.size() && i < 3; ++i) l.add ("  - " + bad[i]->name + ": " + bad[i]->detail + " -> " + bad[i]->explain);
            if (bad.empty()) l.add ("  nothing in the song itself; the mix checks have the rest.");
            l.add (""); l.add (report.ready ? "Verdict: send it." : "Verdict: one more pass. " + report.nextAction);
            return l.joinIntoString ("\n");
        }
        /** The song scored against each reference on its own. */
        juce::String scoreByReference() const
        {
            if (profile.refs.empty()) return "No references in this profile.";
            juce::StringArray l { "THE SONG AGAINST EACH REFERENCE", "" };
            for (const auto& r : profile.refs)
            {
                Profile one = profile;
                one.refs = { r };
                one.learn();
                auto in = lastInputs;
                in.profile = &one;
                const auto rep = evaluate (in);
                l.add (r.name + (r.yours ? " (yours)" : "") + ": " + juce::String (rep.overall) + "/100  (against the label: " + juce::String (rep.areaScore[(size_t) aReference] * 100.0f, 0) + "%)");
            }
            return l.joinIntoString ("\n");
        }
        /** The song against your best signed track: the closest of your own references, section by section. */
        juce::String overlayBestSigned() const
        {
            const Reference* best = nullptr; float bestD = 1.0e9f;
            for (const auto& r : profile.refs)
            {
                if (! r.yours) continue;
                float d = 0.0f; int n = 0;
                for (const auto& s : lastSections) if (s.heard() && r.has[(size_t) s.type]) { d += distance (s.band, r.tone[(size_t) s.type]); ++n; }
                if (n > 0 && d / (float) n < bestD) { bestD = d / (float) n; best = &r; }
            }
            if (best == nullptr) return "No track of yours in this profile yet (References > Add one of my tracks the label signed).";
            juce::StringArray l { "AGAINST YOUR BEST SIGNED TRACK: " + best->name, "" };
            std::array<bool, numTypes> done {};
            for (const auto& s : lastSections)
            {
                if (! s.heard() || ! best->has[(size_t) s.type] || done[(size_t) s.type]) continue;
                done[(size_t) s.type] = true;
                const auto sh = shape (s.band), th = shape (best->tone[(size_t) s.type]);
                l.add (juce::String (sectionName (s.type)) + ": tone " + juce::String (distance (s.band, best->tone[(size_t) s.type]), 1) + " dB apart (lows " + detail::db (sh[1] - th[1])
                       + ", mids " + detail::db (sh[5] - th[5]) + ", top " + detail::db (sh[8] - th[8]) + "), width " + detail::db (s.width() - best->width[(size_t) s.type]));
            }
            return l.joinIntoString ("\n");
        }
        // sign-off per area (the gate waits for it when asked)
        std::map<int, juce::String> signOff;
        bool requireSignOff = false;
        // fresh ears: the scores hidden, a reminder to rest
        bool freshEars = false;
        juce::int64 sessionStartMs = juce::Time::currentTimeMillis();
        int minutesListening() const { return (int) ((juce::Time::currentTimeMillis() - sessionStartMs) / 60000); }
        bool abBlind = false;

        // ---- confidence, stages, learning ----
        juce::int64 lastActionMs = 0;
        std::map<int, float> signOffScore;   // each signed-off area's score when it was signed
        juce::String regressionNote, preRenderNote;
        bool wasPlaying = false;
        std::vector<int> playthroughs;
        /** How steady the score is from one play-through to the next (its spread), or -1 with fewer than three. */
        float scoreSpread() const
        {
            if (playthroughs.size() < 3) return -1.0f;
            const auto [lo, hi] = std::minmax_element (playthroughs.begin(), playthroughs.end());
            return (float) (*hi - *lo);
        }
        void signOffArea (int area, bool on)
        {
            if (on) { signOff[area] = juce::Time::getCurrentTime().toString (true, true); signOffScore[area] = report.areaScore[(size_t) area]; }
            else { signOff.erase (area); signOffScore.erase (area); }
        }
        /** Stages a milestone's name can carry, and what each needs. */
        static int stageOf (const juce::String& name)
        {
            const auto n = name.toLowerCase();
            return n.contains ("sketch") ? 0 : n.contains ("arrang") ? 1 : n.contains ("mix") ? 2 : n.contains ("premaster") || n.contains ("master") || n.contains ("label") ? 3 : -1;
        }
        static const char* stageName (int s) { static const char* n[] = { "sketch", "arrangement", "mix", "premaster" }; return s >= 0 && s < 4 ? n[s] : ""; }
        /** Whether the song (now) meets a stage's definition of done, and what's missing. */
        juce::String stageDone (int stage) const
        {
            juce::StringArray missing;
            std::map<int, float> mins;
            auto need = [&mins] (int area, float min) { mins[area] = juce::jmax (mins.count (area) ? mins[area] : 0.0f, min); };
            if (stage >= 0) { need (aAR, 0.6f); need (aWriting, 0.6f); }
            if (stage >= 1) { need (aAR, 0.75f); need (aReference, 0.7f); }
            if (stage >= 2) { need (aLowEnd, 0.75f); need (aMids, 0.75f); need (aBalance, 0.75f); need (aTranslation, 0.7f); }
            for (const auto& [area, min] : mins)
                if (report.areaMeasured[(size_t) area] && report.areaScore[(size_t) area] < min)
                    missing.add (juce::String (areaName (area)) + " " + juce::String (juce::roundToInt (report.areaScore[(size_t) area] * 100.0f)) + "% (needs " + juce::String (juce::roundToInt (min * 100.0f)) + ")");
            if (stage >= 3) { if (! report.ready) missing.add ("the gate: " + (report.blockers.isEmpty() ? "the score under the label's threshold" : report.blockers.joinIntoString ("; "))); }
            return missing.isEmpty() ? juce::String ("done") : missing.joinIntoString (", ");
        }
        /** The label's structure: the closest reference's map, at this song's start. */
        suite::SectionMap proposeMap() const
        {
            const auto [best, d] = profile.closest (lastSections, 138.0);
            juce::ignoreUnused (d);
            if (best < 0) return {};
            return profile.refs[(size_t) best].map;
        }
        /** Each section's score: the average of the checks pointing inside it. */
        std::vector<float> sectionScores (const suite::SectionMap& map) const
        {
            std::vector<float> out ((size_t) map.count, -1.0f);
            for (int i = 0; i < map.count; ++i)
            {
                float s = 0.0f; int n = 0;
                for (const auto& c : report.checks) if (c.measured && c.bar >= map.sections[(size_t) i].startBar && c.bar < map.endBar (i)) { s += c.score; ++n; }
                if (n > 0) out[(size_t) i] = s / (float) n;
            }
            return out;
        }
        /** Where every track sits front to back, and who ducks under whom. */
        juce::String depthMap() const
        {
            juce::StringArray l { "FRONT TO BACK (0 = right in front, 100 = far back)", "" };
            std::vector<std::pair<float, juce::String>> v;
            for (const auto& t : lastTraits)
            {
                if (t.outRmsDb < -60.0f) continue;
                const auto wide = juce::jlimit (0.0f, 1.0f, (t.sideDb - t.outRmsDb + 20.0f) / 20.0f), soft = juce::jlimit (0.0f, 1.0f, 1.0f - (t.crestDb - 6.0f) / 12.0f);
                v.push_back ({ 0.5f * wide + 0.5f * soft, detail::trackLabel (t) });
            }
            std::sort (v.begin(), v.end());
            for (const auto& [d, name] : v) l.add (juce::String (juce::roundToInt (d * 100.0f)).paddedLeft (' ', 4) + "  " + name);
            l.add (""); l.add ("WHO DUCKS (back within ms after the duck)");
            for (const auto& t : lastTraits) if (t.duckCount >= 4) l.add ("  " + detail::trackLabel (t) + ": " + juce::String (juce::roundToInt (t.recoveryMs)) + " ms, " + juce::String (t.duckCount) + " ducks heard");
            return l.joinIntoString ("\n");
        }
        /** Ear-candy one-shots in the key: a pluck, a riser and a fall, written as WAVs. */
        bool writeEarCandyShots (const juce::File& folder) const
        {
            const auto key = suite::KeyBus::read();
            const auto song = suite::SongBus::read();
            const auto root = song.valid ? song.root : key.valid ? key.root : 9;
            const double fs = 44100.0;
            folder.createDirectory();
            auto write = [&] (const juce::String& name, int seconds10, std::function<float (double, double)> fn)
            {
                juce::AudioBuffer<float> b (2, (int) (fs * seconds10 / 10.0));
                for (int i = 0; i < b.getNumSamples(); ++i) { const auto t = (double) i / fs, pos = (double) i / b.getNumSamples(); const auto v = fn (t, pos); b.setSample (0, i, v); b.setSample (1, i, v); }
                const auto f = folder.getChildFile (name + ".wav");
                f.deleteFile();
                juce::WavAudioFormat fmt;
                std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (f), fs, 2, 24, {}, 0));
                return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            };
            const auto hz = 440.0 * std::pow (2.0, (root + 72 - 69) / 12.0);
            const auto twoPi = juce::MathConstants<double>::twoPi;
            bool ok = write ("Candy pluck", 5, [hz, twoPi] (double t, double) { return (float) (0.4 * std::exp (-t * 9.0) * (std::sin (twoPi * hz * t) + 0.3 * std::sin (twoPi * hz * 2.0 * t))); });
            ok = write ("Candy riser", 20, [hz, twoPi] (double t, double pos) { return (float) (0.25 * pos * std::sin (twoPi * hz * 0.5 * t * (1.0 + pos))); }) && ok;
            ok = write ("Candy fall", 10, [hz, twoPi] (double t, double pos) { return (float) (0.3 * (1.0 - pos) * std::sin (twoPi * hz * t * (1.0 - 0.5 * pos))); }) && ok;
            return ok;
        }
        static juce::String lastSongRecipe() { return "Your last song (label-ready milestone)"; }
        /** A new project starts set up like your last song (off unless you ask: a new idea starts clean). */
        static juce::File startLikeLastFile() { return suite::Alias::file ("Label start like last song.txt"); }
        static bool startLikeLast() { return startLikeLastFile().loadFileAsString().trim() == "on"; }
        static void setStartLikeLast (bool on) { startLikeLastFile().getParentDirectory().createDirectory(); startLikeLastFile().replaceWithText (on ? "on" : "off"); }
        /** FL started rendering: what's off right now (reported, nothing changed mid-render). */
        juce::String preRenderReport (bool abOnNow) const
        {
            juce::StringArray l;
            for (const auto& e : suite::Registry::list()) if (e.has (suite::Status::masterPreview)) l.add ("Lapis is playing the master preview: the render would be a master, not the premaster");
            if (abOnNow) l.add ("a milestone A/B is on");
            if (const auto ms = suite::LabelReadyBus::master(); ms.valid && ms.truePeakDb > -6.0f) l.add ("the master peaks at " + juce::String (ms.truePeakDb, 1) + " dBFS (the premaster wants -6)");
            for (const auto& c : report.checks) if (c.measured && c.blocking && c.score < 0.5f) l.add (c.name + ": " + c.detail);
            return l.isEmpty() ? juce::String ("Rendering: nothing in the way.") : "Rendering with: " + l.joinIntoString ("; ");
        }

        // ---- the mix toward the label ----
        bool steerToLabel = false;    // the label's drop tone as every Quintessence's reference
        /** The label's drop tone shape (the profile's medians), as a reference for the Quintessences. */
        std::array<float, 10> labelToneTarget() const
        {
            const auto b = profile.toneOf (T::Drop);
            std::array<float, 10> out {};
            for (int k = 0; k < 10 && k < numBands; ++k) out[(size_t) k] = b[(size_t) k];
            return out;
        }
        /** One step toward the label: the check against the label furthest out that has a fix. */
        juce::String matchLabelStep()
        {
            const Check* worst = nullptr;
            for (const auto& c : report.checks)
                if (c.measured && c.fix != suite::Fix::None && (c.area == aReference || c.area == aAR || c.area == aLowEnd || c.area == aBalance) && c.score < 0.8f
                    && (worst == nullptr || c.score < worst->score)) worst = &c;
            if (worst == nullptr) return "Nothing with a fix is out of the label's range.";
            const auto what = worst->name + ": " + suite::fixName (worst->fix);
            applyCheck (*worst);
            return "Sent: " + what + " (Undo in Mentalism's What changed).";
        }
        // the translation loop: the worst translation (or mono) problem's fix, kept only if it helped
        bool translationLoop = false;
        struct Trial { juce::String check; uint32_t seq = 0; float before = 0.0f; int updates = 0; };
        std::optional<Trial> trial;
        std::set<juce::String> tried;
        juce::String loopNote;
        void stepTranslationLoop()
        {
            auto scoreOf = [this] (const juce::String& name) { for (const auto& c : report.checks) if (c.name == name && c.measured) return c.score; return -1.0f; };
            if (trial)
            {
                if (++trial->updates < 3) return;   // listen for a while (about half a minute)
                const auto now = scoreOf (trial->check);
                if (now >= 0.0f && now < trial->before + 0.05f) { suite::FixBus::sendUndo (trial->seq); loopNote = trial->check + ": didn't help, undone"; }
                else loopNote = trial->check + ": kept (" + juce::String (juce::roundToInt (trial->before * 100)) + " -> " + juce::String (juce::roundToInt (now * 100)) + ")";
                logAction ("Translation loop: " + loopNote);
                tried.insert (trial->check);
                trial.reset();
                return;
            }
            const Check* worst = nullptr;
            for (const auto& c : report.checks)
                if (c.measured && c.area == aTranslation && c.fix != suite::Fix::None && c.score < 0.8f && ! tried.count (c.name) && (worst == nullptr || c.score < worst->score)) worst = &c;
            if (worst == nullptr) { loopNote = "nothing left to try"; return; }
            Trial t; t.check = worst->name; t.before = worst->score; t.seq = suite::FixBus::send (worst->fix, worst->fixData);
            loopNote = "trying " + suite::fixName (worst->fix) + " for " + worst->name;
            trial = t;
        }

        // ---- confidence ----
        bool autoReadySaved = false;
        /** What the suite can't hear this session, and why. */
        juce::String blindSpots() const
        {
            juce::StringArray l;
            l.add ("BLIND SPOTS: what the checks couldn't measure, and why");
            l.add ("");
            const auto entries = suite::Registry::list();
            auto loaded = [&entries] (const char* plugin) { for (const auto& e : entries) if (e.plugin == plugin) return true; return false; };
            for (auto [plugin, what] : { std::pair<const char*, const char*> { "Mentalism", "the master (loudness, peaks, mono, translation)" },
                                         { "Quintessence", "each track (balance, clip gain, recovery, layers, tails, depth)" },
                                         { "Correspondence", "the written parts (hook, bass, chords, key change)" },
                                         { "Kybalion", "the kick's pitch and the sidechain" } })
                if (! loaded (plugin)) l.add ("- No " + juce::String (plugin) + " loaded: nothing on " + what + ".");
            if (! profileFromCatalogue) l.add ("- No references in the profile: the label norms are generic trance, not this label's.");
            for (int a = 0; a < numAreas; ++a)
            {
                juce::StringArray miss;
                for (const auto& c : report.checks) if (c.area == a && ! c.measured) miss.add ("  " + c.name + ": " + c.detail);
                if (! miss.isEmpty()) { l.add (""); l.add (areaName (a)); l.addArray (miss); }
            }
            l.add ("");
            l.add ("Never measured (the suite can't hear these): the sound design's own quality, the vocal's performance, whether the idea is good.");
            return l.joinIntoString ("\n") + "\n";
        }
        /** The listen-through queue: every flagged bar, in order, with what to listen for. */
        struct QueueItem { int bar = 0; juce::String what; };
        std::vector<QueueItem> listenQueue() const
        {
            std::vector<QueueItem> q;
            for (const auto& c : report.checks)
                if (c.measured && c.bar > 0 && c.score < 0.8f) q.push_back ({ c.bar, c.name + ": " + c.detail });
            std::stable_sort (q.begin(), q.end(), [] (const QueueItem& a, const QueueItem& b) { return a.bar < b.bar; });
            return q;
        }
        /** What to listen for around the playhead's bar (within 2 bars after a flag), else the next flag. */
        juce::String listenNow (int bar) const
        {
            const auto q = listenQueue();
            for (const auto& i : q) if (bar >= i.bar && bar < i.bar + 2) return "Now (bar " + juce::String (i.bar) + "): " + i.what;
            for (const auto& i : q) if (i.bar > bar) return "Next: bar " + juce::String (i.bar) + " - " + i.what;
            return q.empty() ? juce::String ("Nothing flagged at a bar.") : juce::String ("Past the last flagged bar.");
        }

        // ---- the profile's thresholds ----
        /** Scores every reference as if it were the song (from its sections): how the label's own
            released tracks score, and the threshold that 80% of them pass. */
        juce::String scoreReferences (bool setThreshold)
        {
            std::vector<int> scores;
            juce::StringArray l;
            for (const auto& r : profile.refs)
            {
                Inputs in;
                in.map = r.map; in.bpm = r.bpm; in.sections = r.sections; in.profile = &profile;
                if (in.map.count == 0) for (const auto& sec : r.sections) { in.map.sections[(size_t) in.map.count].type = sec.type; in.map.sections[(size_t) in.map.count].startBar = sec.startBar; ++in.map.count; }
                in.map.totalBars = 0;
                for (const auto& sec : r.sections) in.map.totalBars = juce::jmax (in.map.totalBars, sec.startBar - 1 + sec.bars);
                const auto rep = evaluate (in);
                scores.push_back (rep.overall);
                l.add (r.name + ": " + juce::String (rep.overall));
            }
            if (scores.empty()) return "No references to score.";
            auto sorted = scores;
            std::sort (sorted.begin(), sorted.end());
            const auto p20 = sorted[(size_t) juce::jlimit (0, (int) sorted.size() - 1, (int) std::floor (0.2 * (double) sorted.size()))];
            if (setThreshold) { profile.readyThreshold = juce::jlimit (50, 95, p20); profile.save(); }
            return "The label's tracks, scored from their sections (the parts and the tracks can't be heard in a file, so only the arrangement, tone and dynamics count):\n"
                   + l.joinIntoString ("\n") + "\n\n80% of them score " + juce::String (p20) + " or more"
                   + (setThreshold ? ": the ready threshold is now " + juce::String (profile.readyThreshold) + "." : ".");
        }
        /** This label insists on a check: under `minimum` it blocks the gate. */
        void setMinimum (const juce::String& check, float minimum)
        {
            if (minimum <= 0.0f) profile.minScore.erase (check); else profile.minScore[check] = juce::jlimit (0.1f, 1.0f, minimum);
            if (profileFromCatalogue) profile.save();
        }

        // ---- milestone A/B ----
        int abA = -1, abB = -1, abShowing = -1;   // the two milestones, and which is loaded
        bool abOn = false, abSwitchPending = false;
        std::atomic<float> abGainDb { 0.0f };     // the monitor level match (Ouroboros' output, never in a render)
        /** Asks to switch at the next bar (the processor calls abTick at each bar). */
        void abToggle() { if (abOn) abSwitchPending = true; }
        void abStart (int a, int b)
        {
            if (a < 0 || b < 0 || a >= (int) milestones.size() || b >= (int) milestones.size() || a == b) return;
            abA = a; abB = b; abOn = true; abShowing = -1; abSwitchPending = true;
        }
        void abStop() { abOn = false; abSwitchPending = false; abGainDb.store (0.0f); }
        void abTick()
        {
            if (! abOn || ! abSwitchPending) return;
            abSwitchPending = false;
            abShowing = abShowing == abA ? abB : abA;
            restoreMilestone (abShowing);
            // level-matched: the louder one turned down to the quieter
            const auto la = milestones[(size_t) abA].lufs, lb = milestones[(size_t) abB].lufs;
            if (la > -90.0f && lb > -90.0f)
            {
                const auto quieter = juce::jmin (la, lb);
                abGainDb.store (juce::jlimit (-12.0f, 0.0f, quieter - milestones[(size_t) abShowing].lufs));
            }
            else abGainDb.store (0.0f);
        }

        // ---- writing tools that live here (they need the song as heard) ----
        /** The drop-impact designer: ways to make the drops hit harder, each with the jump it would
            likely add (an estimate from how each move works, not a measurement), ranked by how close
            they bring the drops to the label's impact. Each is a Check to apply (applyLabelCheck). */
        std::vector<Check> dropImpactOptions (const suite::SectionMap& map) const
        {
            float now = 0.0f; int n = 0, firstBar = -1;
            const auto& S = lastSections;
            for (size_t i = 1; i < S.size(); ++i)
                if (S[i].type == T::Drop && S[i].heard() && S[i - 1].heard()) { now += S[i].firstDb - S[i - 1].lastDb; ++n; if (firstBar < 0) firstBar = S[i].startBar; }
            now = n > 0 ? now / (float) n : 0.0f;
            const auto target = profile.impactDb.valid() ? profile.impactDb.median : 5.0f;
            struct Way { const char* name; float addDb; suite::Fix fix; const char* action; bool already; };
            const Way ways[] = {
                { "The drop arrives through a filter opening", 1.5f, suite::Fix::None, "entrance:2", map.dropEntrance == 2 },
                { "The drop arrives staggered (kick, bass, then the rest)", 1.0f, suite::Fix::None, "entrance:1", map.dropEntrance == 1 },
                { "A kick-only bar before each drop (the kick keeps playing: no silence)", 2.5f, suite::Fix::None, "gap:3", map.dropGap == 3 },
                { "The director makes every drop hit harder", 2.0f, suite::Fix::None, "director: make every drop hit harder", false },
                { "The builds hold back more and the drops do more (Transmutation)", 1.5f, suite::Fix::BuildImpact, "", false } };
            std::vector<Check> out;
            for (const auto& w : ways)
            {
                if (w.already) continue;
                Check c;
                c.area = aAR; c.name = w.name;
                const auto predicted = now + w.addDb;
                c.score = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (predicted - target) / juce::jmax (2.0f, target));
                c.detail = "about " + juce::String (now, 1) + " -> " + juce::String (predicted, 1) + " dB (the label: " + juce::String (target, 1) + ")";
                c.fix = w.fix; c.action = w.action; c.bar = firstBar;
                out.push_back (c);
            }
            std::stable_sort (out.begin(), out.end(), [] (const Check& a, const Check& b) { return a.score > b.score; });
            if (out.size() > 3) out.resize (3);
            return out;
        }
        std::vector<SectionStat> lastSections;   // the song's sections at the last update
        /** Bars where nothing new happens for 4 bars or more: a MIDI clip of short one-shots in key
            there (chord tones, high), to put on an ear-candy track. */
        juce::File writeEarCandy (const juce::File& folder, const suite::SectionMap& map, double barQ) const
        {
            const auto song = suite::SongBus::read();
            const auto bars = live.read (map.totalBars);
            std::vector<int> spots;
            int still = 0;
            for (size_t b = 1; b < bars.size(); ++b)
            {
                const auto& x = bars[b]; const auto& y = bars[b - 1];
                const auto same = x.heard && y.heard && std::abs (x.db - y.db) < 1.0f && std::abs (x.onsets - y.onsets) < 2.0f;
                still = same ? still + 1 : 0;
                if (still == 4) { spots.push_back ((int) b - 1); still = 0; }   // the 4th still bar: a candy hit at its start
            }
            juce::MidiMessageSequence seq;
            const auto root = song.valid ? song.root : suite::KeyBus::read().root;
            const auto minor = ! (song.valid && (song.scale == 1 || song.scale == 5));
            for (auto b : spots)
            {
                const auto q = (double) b * barQ;
                int type = 0; for (int i = 0; i < map.count; ++i) if (map.sections[(size_t) i].startBar <= b + 1) type = (int) map.sections[(size_t) i].type;
                const auto chordRoot = song.valid && song.chordCount[(size_t) type] > 0 ? juce::jmax (0, song.chordRoots[(size_t) type][0]) : 0;
                const int tones[] = { 0, minor ? 3 : 4, 7, 12 };
                for (int k = 0; k < 3; ++k)
                {
                    const auto pitch = 72 + (root + chordRoot) % 12 + tones[(b + k) % 4];
                    const auto t = (q + 0.5 * k) * 960.0;
                    seq.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) (90 - 15 * k)), t);
                    seq.addEvent (juce::MidiMessage::noteOff (1, pitch), t + 0.25 * 960.0);
                }
            }
            seq.updateMatchedPairs();
            juce::MidiFile mf;
            mf.setTicksPerQuarterNote (960);
            mf.addTrack (seq);
            folder.createDirectory();
            const auto f = folder.getChildFile ("Ear candy.mid");
            f.deleteFile();
            juce::FileOutputStream os (f);
            if (os.openedOk()) mf.writeTo (os);
            earCandySpots = (int) spots.size();
            return f;
        }
        mutable int earCandySpots = 0;

        // ---- prints ----
        static juce::String songProgressionPrint (const suite::SongBus::Song& song)
        {
            std::vector<int> roots;
            for (int c = 0; c < song.chordCount[(size_t) T::Drop]; ++c)
                if (const auto r = song.chordRoots[(size_t) T::Drop][(size_t) c]; r >= 0 && (roots.empty() || roots.back() != r)) roots.push_back (r);
            if (roots.size() > 2 && roots.back() == roots.front()) roots.pop_back();
            return progressionPrint (roots);
        }
        /** The kit's groove (chord stabs and arp accents on the 16ths): "x.x..X" style, 32 steps. */
        static juce::String groovePrintNow()
        {
            const auto r = suite::KitBus::read (600);
            if (! r.valid) return {};
            juce::String g;
            for (auto v : r.chords) g << (v >= 0.9f ? "X" : v >= 0.6f ? "x" : v > 0.0f ? "o" : ".");
            for (auto v : r.arp) g << (v >= 0.9f ? "X" : v >= 0.6f ? "x" : v > 0.0f ? "o" : ".");
            return g;
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
            m.fixes = activeFixesSignature();
            autoFix.milestone (report);   // the autopilot: areas that fall from here roll back
            m.stage = stageOf (m.name);
            // your taste: this song's drop tone against the label's, for the taste offset
            for (const auto& s : lastSections)
                if (s.type == T::Drop && s.heard() && profileFromCatalogue)
                {
                    const auto sh = shape (s.band), lab = profile.toneOf (T::Drop);
                    std::array<float, 10> off {};
                    for (int k = 0; k < 10 && k < numBands; ++k) off[(size_t) k] = sh[(size_t) k] - lab[(size_t) k];
                    Learning::recordTaste (off, m.name);
                    break;
                }
            if (const auto ms = suite::LabelReadyBus::master(); ms.valid && ms.measuredSeconds > 5.0f) m.lufs = ms.lufs;   // for the level-matched A/B
            suite::RecipeBus::send (suite::RecipeAction::Save, m.recipe);   // every plugin saves its state under that name
            suite::RecipeBus::send (suite::RecipeAction::Save, lastSongRecipe());   // and as "your last song" (a new project can start from it)
            milestones.push_back (m);
            const auto song = suite::SongBus::read();
            if (song.valid && ! song.hook.empty()) suite::StyleLog::add ("hookprint", { { "print", detail::hookPrint (song.hook) }, { "milestone", m.name } });
            if (song.valid) if (const auto pp = songProgressionPrint (song); pp.isNotEmpty()) suite::StyleLog::add ("progprint", { { "print", pp }, { "milestone", m.name } });
            if (const auto g = groovePrintNow(); g.isNotEmpty()) suite::StyleLog::add ("grooveprint", { { "print", g }, { "milestone", m.name } });
            suite::MoveLog::add ("Ouroboros", {}, "Milestone saved: " + m.name + " (label-ready " + juce::String (m.overall) + ")", suite::MoveLog::fix);
        }
        void restoreMilestone (int index)
        {
            if (index < 0 || index >= (int) milestones.size()) return;
            suite::RecipeBus::send (suite::RecipeAction::Load, milestones[(size_t) index].recipe);
            milestoneFixes = milestones[(size_t) index].fixes; milestoneCheckIn = milestoneFixes.isNotEmpty() ? 3 : -1; milestoneFixNote = {};
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
            t.setProperty ("steer", steerToLabel, nullptr);
            t.setProperty ("autoReady", autoReadySaved, nullptr);
            t.setProperty ("autoFix", autoFixOn, nullptr);
            {
                juce::StringArray modes;
                for (auto m : autoFix.areaMode) modes.add (juce::String (m));
                t.setProperty ("areaModes", modes.joinIntoString (","), nullptr);
                t.setProperty ("mixStrength", suite::Autopilot::mixStrength(), nullptr);
            }
            t.setProperty ("requireSignOff", requireSignOff, nullptr);
            juce::StringArray so;
            for (const auto& [a, note] : signOff) so.add (juce::String (a) + "|" + note.replaceCharacters ("|~", "  ") + "|" + juce::String (signOffScore.count (a) ? signOffScore.at (a) : 0.0f, 3));
            t.setProperty ("signOff", so.joinIntoString ("~"), nullptr);
            juce::StringArray lg;
            for (const auto& e : scoreLog) lg.add (juce::String (e.timeMs) + "|" + e.what.replaceCharacters ("|~", "  ") + "|" + juce::String (e.before) + "|" + juce::String (e.after));
            t.setProperty ("scoreLog", lg.joinIntoString ("~"), nullptr);
            juce::StringArray h; for (const auto& [ms, v] : history) h.add (juce::String (ms) + ":" + juce::String (v));
            t.setProperty ("history", h.joinIntoString (","), nullptr);
            for (const auto& m : milestones)
            {
                juce::ValueTree c ("Milestone");
                c.setProperty ("fixes", m.fixes, nullptr);
                c.setProperty ("name", m.name, nullptr); c.setProperty ("recipe", m.recipe, nullptr); c.setProperty ("time", m.timeMs, nullptr); c.setProperty ("overall", m.overall, nullptr);
                juce::StringArray a; for (auto v : m.areas) a.add (juce::String (v, 3));
                c.setProperty ("areas", a.joinIntoString (","), nullptr);
                c.setProperty ("lufs", m.lufs, nullptr);
                c.setProperty ("stage", m.stage, nullptr);
                t.appendChild (c, nullptr);
            }
            return t;
        }
        void fromTree (const juce::ValueTree& t)
        {
            if (! t.hasType ("LabelReady")) return;
            if (t["profile"].toString().isNotEmpty()) useProfile (t["profile"].toString());
            steerToLabel = (bool) t.getProperty ("steer", false);
            autoReadySaved = (bool) t.getProperty ("autoReady", false);
            autoFixOn = (bool) t.getProperty ("autoFix", true);
            {
                const auto modes = juce::StringArray::fromTokens (t.getProperty ("areaModes", "").toString(), ",", "");
                for (int a = 0; a < numAreas; ++a) autoFix.areaMode[(size_t) a] = a < modes.size() ? juce::jlimit (0, 2, modes[a].getIntValue()) : 0;
                suite::Autopilot::setMixStrength ((float) t.getProperty ("mixStrength", 1.0f));
            }
            requireSignOff = (bool) t.getProperty ("requireSignOff", false);
            signOff.clear();
            for (const auto& e : juce::StringArray::fromTokens (t["signOff"].toString(), "~", ""))
                if (e.contains ("|"))
                {
                    const auto f = juce::StringArray::fromTokens (e, "|", "");
                    signOff[f[0].getIntValue()] = f[1];
                    if (f.size() > 2) signOffScore[f[0].getIntValue()] = f[2].getFloatValue();
                }
            scoreLog.clear();
            for (const auto& e : juce::StringArray::fromTokens (t["scoreLog"].toString(), "~", ""))
            {
                const auto f = juce::StringArray::fromTokens (e, "|", "");
                if (f.size() >= 4) scoreLog.push_back ({ f[0].getLargeIntValue(), f[1], f[2].getIntValue(), f[3].getIntValue(), 2 });
            }
            history.clear();
            for (const auto& p : juce::StringArray::fromTokens (t["history"].toString(), ",", ""))
                history.push_back ({ p.upToFirstOccurrenceOf (":", false, false).getLargeIntValue(), p.fromFirstOccurrenceOf (":", false, false).getIntValue() });
            milestones.clear();
            for (const auto& c : t)
                if (c.hasType ("Milestone"))
                {
                    Milestone m; m.name = c["name"].toString(); m.recipe = c["recipe"].toString(); m.fixes = c["fixes"].toString(); m.timeMs = (juce::int64) c["time"]; m.overall = (int) c["overall"];
                    const auto a = juce::StringArray::fromTokens (c["areas"].toString(), ",", "");
                    for (int k = 0; k < numAreas && k < a.size(); ++k) m.areas[(size_t) k] = a[k].getFloatValue();
                    m.lufs = (float) c.getProperty ("lufs", -100.0f);
                    m.stage = (int) c.getProperty ("stage", -1);
                    milestones.push_back (m);
                }
        }
    };
}
