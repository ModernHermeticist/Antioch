#pragma once

// The plugins' fixes, judged (the self-check). A check under 70% asks for the plugin fixes it names;
// each one is tried in turn (one at a time, so it's clear which one helped), listened to for a few
// updates of a real play-through (no loops, no solos, nothing still settling), and kept only if its
// check moved up by more than the check's own wobble. One that didn't help is revoked: its plugin
// switches it off for this song. Kept and revoked fixes feed the learning (smaller steps, then left
// alone, across songs). Ouroboros publishes what's needed and what's revoked (LabelReadyBus).
//
// CheckNoise: how much each check's score wanders between plain play-throughs, so a fix has to beat
// that, not a fixed 3%.

#include "LabelReady.h"
#include "LabelLearning.h"
#include "LabelReadyBus.h"
#include "AutopilotBus.h"
#include "MoveLog.h"
#include <map>
#include <set>
#include <vector>

namespace lab
{
    class CheckNoise
    {
    public:
        /** The end of a play-through: each check's score, unless a fix was tried on it during the play. */
        void playEnded (const Report& rep, const std::set<juce::String>& touched)
        {
            for (const auto& c : rep.checks)
            {
                if (! c.measured || touched.count (c.name)) continue;
                auto& v = scores[c.name];
                v.push_back (c.score);
                if (v.size() > 8) v.erase (v.begin());
            }
        }
        /** How much a fix must beat (at least 2%, 3% until three plain play-throughs are known). */
        float of (const juce::String& check) const
        {
            if (! suite::Autopilot::on (suite::Autopilot::mx2NoiseFloorF)) return 0.03f;
            const auto it = scores.find (check);
            if (it == scores.end() || it->second.size() < 3) return 0.03f;
            float mean = 0.0f; for (auto s : it->second) mean += s; mean /= (float) it->second.size();
            float var = 0.0f; for (auto s : it->second) var += (s - mean) * (s - mean); var /= (float) (it->second.size() - 1);
            return juce::jlimit (0.02f, 0.15f, 2.0f * std::sqrt (var));
        }
        void reset() { scores.clear(); }
        std::map<juce::String, std::vector<float>> scores;
    };

    class PluginFixJudge
    {
    public:
        enum State { untried, trying, kept, revoked };
        struct Entry { State state = untried; juce::String check; float before = 0.0f; int updates = 0; };

        /** Is this feature one of Ouroboros' own (sent, not latched in a plugin)? Set by the host. */
        std::function<bool (int)> sentByOuroboros;
        /** Checks whose area is in "propose" mode, and the ones you approved there. */
        std::function<bool (const Check&)> allowed;
        Learning* learning = nullptr;
        const CheckNoise* noise = nullptr;
        juce::String note;

        /** Each update. `judgeable`: a real play-through (not a loop, not soloed, playing); `settling`:
            a plugin just switched a fix or a Quintessence is mid-pass. */
        void step (const Report& rep, bool judgeable, bool settling)
        {
            namespace AP = suite::Autopilot;
            const auto selfCheck = AP::on (AP::mx2SelfCheckF), oneAtATime = AP::on (AP::mx2OneAtATimeF);
            for (const auto& c : rep.checks)
            {
                if (! c.measured) continue;
                const auto fs = pluginFeatures (c);
                if (fs.empty()) continue;
                if (c.score < 0.7f && (allowed == nullptr || allowed (c)))
                {
                    // start the next one (or all of them, without one-at-a-time / the self-check)
                    bool anyTrying = false;
                    for (auto f : fs) anyTrying = anyTrying || entries[f].state == trying;
                    for (auto f : fs)
                    {
                        auto& e = entries[f];
                        if (e.state != untried) continue;
                        if (learning != nullptr && AP::on (AP::mx2LearnPluginF) && learning->retired (key (f))) continue;
                        if (oneAtATime && selfCheck && anyTrying) break;
                        e.state = trying; e.check = c.name; e.before = c.score; e.updates = 0; anyTrying = true;
                        if (! (oneAtATime && selfCheck)) continue;
                        break;
                    }
                }
            }
            // judge the ones trying
            for (auto& [f, e] : entries)
            {
                if (e.state != trying) continue;
                if (! selfCheck) { e.state = kept; continue; }
                if ((! judgeable && AP::on (AP::mx2ValidPlaysF)) || (settling && AP::on (AP::mx2SettleF))) continue;
                if (++e.updates < 5) continue;   // about ten seconds of a real play-through after it settled
                const auto now = scoreOf (rep, e.check);
                if (now < 0.0f) continue;
                const auto need = noise != nullptr ? noise->of (e.check) : 0.03f;
                const auto helped = now >= e.before + need || now >= 0.95f;
                e.state = helped ? kept : revoked;
                if (learning != nullptr && AP::on (AP::mx2LearnPluginF)) learning->record (key (f), helped);
                note = juce::String (suite::Autopilot::featureName (f)) + (helped ? ": kept" : ": no better, switched off for this song");
                suite::MoveLog::add ("Ouroboros", {}, "Label-ready autopilot: " + note, suite::MoveLog::fix);
            }
        }
        /** What the plugins are told: needed (kept and being tried), revoked. */
        suite::LabelReadyBus::Needs needs() const
        {
            suite::LabelReadyBus::Needs m {};
            for (const auto& [f, e] : entries) if (e.state == trying || e.state == kept) suite::LabelReadyBus::setBit (m, f);
            return m;
        }
        suite::LabelReadyBus::Needs revokedMask() const
        {
            suite::LabelReadyBus::Needs m {};
            for (const auto& [f, e] : entries) if (e.state == revoked) suite::LabelReadyBus::setBit (m, f);
            return m;
        }
        int countOf (State s) const { int n = 0; for (const auto& [f, e] : entries) n += e.state == s ? 1 : 0; return n; }
        State stateOf (int feature) const { const auto it = entries.find (feature); return it == entries.end() ? untried : it->second.state; }
        void reset() { entries.clear(); note = {}; }
        std::map<int, Entry> entries;

    private:
        std::vector<int> pluginFeatures (const Check& c) const
        {
            std::vector<int> out;
            if (! c.action.startsWith ("autopilot:")) return out;
            for (const auto& t : juce::StringArray::fromTokens (c.action.fromFirstOccurrenceOf (":", false, false), ",", ""))
            {
                const auto f = t.trim().getIntValue();
                if (t.trim().isNotEmpty() && f >= 0 && f < suite::Autopilot::numFeatures && (sentByOuroboros == nullptr || ! sentByOuroboros (f)) && suite::Autopilot::on ((suite::Autopilot::Feature) f))
                    out.push_back (f);
            }
            return out;
        }
        static juce::String key (int f) { return "plugin fix " + juce::String (f); }
        static float scoreOf (const Report& rep, const juce::String& name)
        {
            for (const auto& c : rep.checks) if (c.name == name && c.measured) return c.score;
            return -1.0f;
        }
    };
}
